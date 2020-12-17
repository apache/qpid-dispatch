/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <inttypes.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include "qpid/dispatch/ctools.h"
#include "qpid/dispatch/log.h"
#include "qpid/dispatch/tempstore.h"
#include "qpid/dispatch/router_core.h"
#include "qpid/dispatch/protocol_adaptor.h"

typedef struct qdts_state_t qdts_state_t;
typedef struct qdts_file_t qdts_file_t;

struct qdts_file_t {
    DEQ_LINKS(qdts_file_t);
    char *file_path;
    int   write_count;
};

DEQ_DECLARE(qdts_file_t, qdts_file_list_t);

#define MAX_TEMPLATE 500

struct qdts_state_t {
    qdr_core_t         *core;
    qd_log_source_t    *log;
    char               *address;
    bool                initialized;
    bool                failed;
    qdr_subscription_t *subscription;
    char                template[MAX_TEMPLATE];
    char               *path;
    qdts_file_list_t    files;
};

static qdts_state_t qdts_state;


bool qd_temp_is_store_created(void)
{
    return qdts_state.initialized;
}


char* qd_temp_get_path(const char* filename)
{
    qdts_state_t *state = &qdts_state;

    if (!state->initialized)
        return false;

    size_t path_len = strlen(state->path) + strlen(filename) + 2;
    char *full_path = (char*) malloc(path_len);
    strcpy(full_path, state->path);
    strcat(full_path, "/");
    strcat(full_path, filename);

    qdts_file_t *file = DEQ_HEAD(state->files);
    while (!!file) {
        if (strcmp(file->file_path, full_path) == 0) {
            return full_path;
        }
        file = DEQ_NEXT(file);
    }

    free(full_path);
    return 0;
}


static void qdts_init_storage(qdts_state_t *state)
{
    char *temp_dir;
    char *template = "qdrouterd-filestore.XXXXXX";

    //
    // If the TMPDIR environment variable is set, use that as the prefix path for
    // the temporary directory.  Otherwise, use "/tmp".
    //
    temp_dir = getenv("TMPDIR");
    if (!temp_dir)
        temp_dir = "/tmp";

    if (strlen(temp_dir) + strlen(template) + 2 > MAX_TEMPLATE) {
        qd_log(state->log, QD_LOG_CRITICAL, "Path from environment variable TMPDIR is too long - using '/tmp'");
        temp_dir = "/tmp";
    }
    
    strcpy(state->template, temp_dir);
    strcat(state->template, "/");
    strcat(state->template, template);

    state->path = mkdtemp(state->template);
    if (!state->path) {
        qd_log(state->log, QD_LOG_CRITICAL, "Failed to create temporary directory: %s (%s)",
               state->template, strerror(errno));
        state->failed = true;
        return;
    }

    state->initialized = true;
    qd_log(state->log, QD_LOG_INFO, "Store initialized, path: %s", state->path);
}


static bool qdts_write_file(qdts_state_t *state, const char *filename, const char *body, int limit)
{
    char *full_path = (char*) malloc(strlen(state->path) + strlen(filename) + 2);

    strcpy(full_path, state->path);
    strcat(full_path, "/");
    strcat(full_path, filename);

    qdts_file_t *file = DEQ_HEAD(state->files);
    while (!!file && strcmp(file->file_path, full_path) != 0) {
        file = DEQ_NEXT(file);
    }

    if (!file && DEQ_SIZE(state->files) >= limit && limit != 0) {
        free(full_path);
        return false;
    }

    int fd = open(full_path, O_CREAT | O_TRUNC | O_WRONLY, S_IRUSR | S_IWUSR);
    if (fd > 0) {
        ssize_t written = write(fd, body, strlen(body));
        if (written < 0) {
            qd_log(state->log, QD_LOG_ERROR, "Failed to write file '%s' (%s)", full_path, strerror(errno));
            free(full_path);
            return true;
        }
        close(fd);
    } else {
        qd_log(state->log, QD_LOG_ERROR, "Failed to open file '%s' (%s)", full_path, strerror(errno));
        free(full_path);
        return true;
    }

    if (!!file) {
        file->write_count++;
        free(full_path);
    } else {
        file = NEW(qdts_file_t);
        DEQ_ITEM_INIT(file);
        file->file_path   = full_path;
        file->write_count = 1;
        DEQ_INSERT_TAIL(state->files, file);
    }

    qd_log(state->log, QD_LOG_INFO, "Temporary file stored: %s", filename);
    return true;
}


static uint64_t qdts_on_message(void *context, qd_message_t *msg, int link_maskbit, int inter_router_cost,
                                uint64_t conn_id, const qd_policy_spec_t *policy_spec, qdr_error_t **error)
{
    qdts_state_t *state = (qdts_state_t*) context;

    if (!!policy_spec && !policy_spec->allowTempFile) {
        qd_log(state->log, QD_LOG_ERROR, "[C%"PRIu64"] Policy violation - temp file store not permitted", conn_id);
        *error = qdr_error(QD_AMQP_COND_UNAUTHORIZED_ACCESS, "File store access forbidden");
        return PN_REJECTED;
    }

    if (state->failed) {
        *error = qdr_error(QD_AMQP_COND_INTERNAL_ERROR, "File store initialization failed");
        return PN_REJECTED;
    }

    if (qd_message_check_depth(msg, QD_DEPTH_BODY) != QD_MESSAGE_DEPTH_OK) {
        qd_log(state->log, QD_LOG_ERROR, "Invalid message received");
        *error = qdr_error(QD_AMQP_COND_DECODE_ERROR, "Parse error in message");
        return PN_REJECTED;
    }

    qd_iterator_t *subject_iter = qd_message_field_iterator(msg, QD_FIELD_SUBJECT);
    if (!subject_iter) {
        qd_log(state->log, QD_LOG_ERROR, "Invalid message received - No Subject");
        *error = qdr_error(QD_AMQP_COND_INVALID_FIELD, "Invalid request - no subject header");
        return PN_REJECTED;
    }

    char *filename = (char*) qd_iterator_copy(subject_iter);
    qd_iterator_free(subject_iter);

    if (!!strchr(filename, '/') || !!strchr(filename, '\\') || !!strstr(filename, "..")) {
        qd_log(state->log, QD_LOG_ERROR, "Invalid message received - File name with path traversal characters");
        free(filename);
        *error = qdr_error(QD_AMQP_COND_INVALID_FIELD, "File name contains path traversal characters");
        return PN_REJECTED;
    }

    qd_iterator_t *body_iter = qd_message_field_iterator(msg, QD_FIELD_BODY);
    char          *body      = 0;

    if (!!body_iter) {
        //
        // Check the body size
        //
        if (!!policy_spec) {
            int length = qd_iterator_length(body_iter);
            if (length > policy_spec->maxTempFileSize) {
                qd_log(state->log, QD_LOG_ERROR, "[C%"PRIu64"] Policy violation - temp file larger than max size: %d", conn_id, length);
                free(filename);
                qd_iterator_free(body_iter);
                *error = qdr_error(QD_AMQP_COND_UNAUTHORIZED_ACCESS, "File larger than maximum allowed size");
                return PN_REJECTED;
            }
        }

        qd_parsed_field_t *field = qd_parse(body_iter);
        if (qd_parse_ok(field)) {
            uint8_t tag = qd_parse_tag(field);
            if (tag == QD_AMQP_STR8_UTF8 || tag == QD_AMQP_STR32_UTF8) {
                body = (char*) qd_iterator_copy(qd_parse_raw(field));
            }
        }
        qd_parse_free(field);
        qd_iterator_free(body_iter);
    }

    if (!state->initialized)
        qdts_init_storage(state);

    if (state->initialized) {
        bool success = qdts_write_file(state, filename, body, !!policy_spec ? policy_spec->maxTempFileCount : 0);
        if (!success) {
            free(filename);
            free(body);
            qd_log(state->log, QD_LOG_ERROR, "[C%"PRIu64"] Policy violation - too many files in the temporary store", conn_id);
            *error = qdr_error(QD_AMQP_COND_UNAUTHORIZED_ACCESS, "Too many files in file store");
            return PN_REJECTED;
        }
    }

    free(filename);
    free(body);
    return PN_ACCEPTED;
}


static void qd_tempstore_init(qdr_core_t *core, void **adaptor_context)
{
    qdts_state_t *state = &qdts_state;

    ZERO(state);
    state->core         = core;
    state->address      = "_$qd.store_file";
    state->initialized  = false;
    state->subscription = qdr_core_subscribe(core, state->address, QD_ITER_HASH_PREFIX_MOBILE, '0',
                                             QD_TREATMENT_ANYCAST_CLOSEST, false, qdts_on_message, state);

    state->log = qd_log_source("TEMPSTORE");

    *adaptor_context = state;
}


static void qd_tempstore_final(void *adaptor_context)
{
    qdts_state_t *state = (qdts_state_t*) adaptor_context;
    qdr_core_unsubscribe(state->subscription);
    if (state->initialized) {
        qdts_file_t *file = DEQ_HEAD(state->files);
        while (file) {
            DEQ_REMOVE_HEAD(state->files);
            remove(file->file_path);
            free(file->file_path);
            free(file);
            file = DEQ_HEAD(state->files);
        }
        remove(state->path);
        qd_log(state->log, QD_LOG_INFO, "Temporary store deleted");
    }
}


//
// Even though this is not technically a protocol adaptor, we will use the
// adaptor declaration facility because it will initialize this module at
// the right time.
//
QDR_CORE_ADAPTOR_DECLARE("tempstore", qd_tempstore_init, qd_tempstore_final)

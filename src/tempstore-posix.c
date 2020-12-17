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


static void qdts_write_file(qdts_state_t *state, const char *filename, const char *body)
{
    char *full_path = (char*) malloc(strlen(state->path) + strlen(filename) + 2);

    strcpy(full_path, state->path);
    strcat(full_path, "/");
    strcat(full_path, filename);

    int fd = open(full_path, O_CREAT | O_TRUNC | O_WRONLY, S_IRUSR | S_IWUSR);
    if (fd > 0) {
        write(fd, body, strlen(body));
        close(fd);
    } else {
        qd_log(state->log, QD_LOG_ERROR, "Failed to open file '%s' (%s)", full_path, strerror(errno));
        free(full_path);
        return;
    }

    qdts_file_t *file = DEQ_HEAD(state->files);
    while (!!file && strcmp(file->file_path, full_path) != 0) {
        file = DEQ_NEXT(file);
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
}


static void qdts_on_message(void *context, qd_message_t *msg, int link_maskbit, int inter_router_cost, uint64_t conn_id)
{
    qdts_state_t *state = (qdts_state_t*) context;

    if (state->failed)
        return;

    if (qd_message_check_depth(msg, QD_DEPTH_BODY) != QD_MESSAGE_DEPTH_OK) {
        qd_log(state->log, QD_LOG_ERROR, "Invalid message received");
        return;
    }

    qd_iterator_t *subject_iter = qd_message_field_iterator(msg, QD_FIELD_SUBJECT);
    if (!subject_iter) {
        qd_log(state->log, QD_LOG_ERROR, "Invalid message received - No Subject");
        return;
    }

    char *filename = (char*) qd_iterator_copy(subject_iter);
    qd_iterator_free(subject_iter);

    if (!!strchr(filename, '/') || !!strchr(filename, '\\') || !!strstr(filename, "..")) {
        qd_log(state->log, QD_LOG_ERROR, "Invalid message received - File name with path traversal characters");
        free(filename);
        return;
    }

    qd_iterator_t *body_iter = qd_message_field_iterator(msg, QD_FIELD_BODY);
    char          *body      = 0;

    if (!!body_iter) {
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
        qdts_write_file(state, filename, body);
        qd_log(state->log, QD_LOG_INFO, "Temporary file stored: %s", filename);
    }

    free(filename);
    free(body);
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

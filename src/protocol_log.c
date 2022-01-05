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

#include "qpid/dispatch/protocol_log.h"
#include "qpid/dispatch/ctools.h"
#include "qpid/dispatch/alloc.h"
#include "qpid/dispatch/io_module.h"
#include "qpid/dispatch/threading.h"
#include "qpid/dispatch/log.h"
#include "qpid/dispatch/compose.h"
#include "qpid/dispatch/amqp.h"
#include "dispatch_private.h"
#include "stdbool.h"
#include <inttypes.h>
#include <stdlib.h>

typedef struct plog_identity_t {
    uint32_t site_id;
    uint32_t router_id;
    uint64_t record_id;
} plog_identity_t;

typedef struct plog_attribute_data_t {
    DEQ_LINKS(struct plog_attribute_data_t);
    plog_attribute_t  attribute_type;
    uint32_t          emit_ordinal;
    union {
        uint64_t         uint_val;
        char            *string_val;
        plog_identity_t  ref_val;
    } value;
} plog_attribute_data_t;

ALLOC_DECLARE(plog_attribute_data_t);
ALLOC_DEFINE(plog_attribute_data_t);
DEQ_DECLARE(plog_attribute_data_t, plog_attribute_data_list_t);

DEQ_DECLARE(plog_record_t, plog_record_list_t);

struct plog_record_t {
    DEQ_LINKS(plog_record_t);
    DEQ_LINKS_N(UNFLUSHED, plog_record_t);
    plog_record_type_t          record_type;
    plog_record_t              *parent;
    plog_record_list_t          children;
    plog_identity_t             identity;
    plog_attribute_data_list_t  attributes;
    uint32_t                    emit_ordinal;
    bool                        unflushed;
    bool                        never_flushed;
    bool                        never_logged;
    bool                        force_log;
    bool                        ended;
};

ALLOC_DECLARE(plog_record_t);
ALLOC_DEFINE(plog_record_t);

typedef struct plog_work_t plog_work_t;

typedef void (*plog_work_handler_t) (plog_work_t *work, bool discard);

struct plog_work_t {
    DEQ_LINKS(plog_work_t);
    plog_work_handler_t  handler;
    plog_record_t       *record;
    plog_attribute_t     attribute;
    union {
        char            *string_val;
        uint64_t         int_val;
        plog_identity_t  ref_val;
    } value;
};

ALLOC_DECLARE(plog_work_t);
ALLOC_DEFINE(plog_work_t);
DEQ_DECLARE(plog_work_t, plog_work_list_t);

static sys_mutex_t        *lock;
static sys_cond_t         *condition;
static sys_thread_t       *thread;
static bool                sleeping = false;
static qd_log_source_t    *log;
static plog_work_list_t    work_list         = DEQ_EMPTY;
static plog_record_list_t  unflushed_records = DEQ_EMPTY;
static plog_record_t      *local_router      = 0;
static uint32_t            site_id;
static uint32_t            router_id;
static uint64_t            next_identity     = 0;


/**
 * @brief Find either the existing attribute record or the insertion point for a new attribute.
 * 
 * @param record The record with the attribute list that should be searched
 * @param attr The attribute type to search for
 * @return data Pointer to result:
 *     If 0, insert new data at head
 *     If data has the same attribute type, overwrite this data record with new values
 *     If data has a different attribute type, insert new data record after this data record
 */
static plog_attribute_data_t* _plog_find_attribute(plog_record_t *record, plog_attribute_t attr)
{
    plog_attribute_data_t *data = DEQ_TAIL(record->attributes);

    while(!!data) {
        if (data->attribute_type <= attr) {
            //
            // Indicate the overwrite or insert-before case
            //
            return data;
        }
        data = DEQ_PREV(data);
    }

    //
    // Indicate the insert-at-tail case
    //
    return 0;
}


/**
 * @brief Assign a unique identity for a locally-sourced record.
 * 
 * @param identity (out) New, unique identity
 */
static void _plog_next_id(plog_identity_t *identity)
{
    identity->site_id   = site_id;
    identity->router_id = router_id;
    identity->record_id = next_identity++;
}


/**
 * @brief Concatenate the string representation of an id onto a string.
 *
 * @param buffer Target string for concatenation
 * @param n String size limit
 * @param id Identity to be string encoded
 */
static void _plog_strncat_id(char *buffer, size_t n, const plog_identity_t *id)
{
#define ID_TEXT_MAX 60
    char text[ID_TEXT_MAX + 1];

    snprintf(text, ID_TEXT_MAX, "%"PRIu32":%"PRIu32":%"PRIu64, id->site_id, id->router_id, id->record_id);
    strncat(buffer, text, n);
}


/**
 * @brief Concatenate the attribute name onto a string.
 *
 * @param buffer Target string for concatenation
 * @param n String size limit
 * @param data Data value to extrace the attribute-type from
 */
static void _plog_strncat_attribute(char *buffer, size_t n, const plog_attribute_data_t *data)
{
#define ATTR_TEXT_MAX 65
    char  text[ATTR_TEXT_MAX + 1];
    char *text_ptr = text;

    text[0] = '\0';

    if (1 << data->attribute_type & VALID_UINT_ATTRS) {
        sprintf(text, "%"PRIu64, data->value.uint_val);
    } else if (1 << data->attribute_type & (VALID_STRING_ATTRS | VALID_TRACE_ATTRS)) {
        text_ptr = data->value.string_val;
    } else if (1 << data->attribute_type & VALID_REF_ATTRS) {
        _plog_strncat_id(text, ATTR_TEXT_MAX, &data->value.ref_val);
    }

    strncat(buffer, text_ptr, n);
}


/**
 * @brief Work handler for plog_start_record
 * 
 * @param work Pointer to work context
 * @param discard Indicator that this work must be discarded
 */
static void _plog_start_record_TH(plog_work_t *work, bool discard)
{
    if (discard) {
        return;
    }

    //
    // The record was allocated in the IO thread call (so the pointer could
    // be returned synchronously to the caller)
    //
    plog_record_t *record = work->record;
    if (record->parent == 0) {
        record->parent = local_router;
    }

    //
    // Assign a unique identity to the new record
    //
    _plog_next_id(&record->identity);

    //
    // Place the new record on the parent's list of children
    //
    DEQ_INSERT_TAIL(record->parent->children, record);


    //
    // If this record has a parent and the parent has never been logged,
    // flag it as needing to be flushed and logged.
    //
    if (record->parent->never_logged) {
        record->parent->force_log = true;
        if (!record->parent->unflushed) {
            record->parent->unflushed = true;
            DEQ_INSERT_TAIL_N(UNFLUSHED, unflushed_records, record->parent);
        }
    }

    //
    // Place the new record on the unflushed list to be pushed out later.
    // Mark the record as never-flushed so we can avoid the situation where
    // a record that references this record gets flushed before this record
    // is initially flushed.
    //
    if (!record->unflushed) {
        record->unflushed = true;
        DEQ_INSERT_TAIL_N(UNFLUSHED, unflushed_records, record);
    }
}


/**
 * @brief Work handler for plog_end_record
 * 
 * @param work Pointer to work context
 * @param discard Indicator that this work must be discarded
 */
static void _plog_end_record_TH(plog_work_t *work, bool discard)
{
    if (discard) {
        return;
    }

    plog_record_t *record = work->record;

    //
    // Mark the record as ended to designate the lifecycle end
    //
    record->ended = true;

    //
    // If the record has been flushed, schedule it for re-flushing
    // with the updated lifecycle information.
    //
    if (!record->unflushed) {
        DEQ_INSERT_TAIL_N(UNFLUSHED, unflushed_records, record);
        record->unflushed = true;
    }
}


/**
 * @brief Work handler for plog_set_ref
 * 
 * @param work Pointer to work context
 * @param discard Indicator that this work must be discarded
 */
static void _plog_set_ref_TH(plog_work_t *work, bool discard)
{
    if (discard) {
        return;
    }

    plog_record_t         *record = work->record;
    plog_attribute_data_t *insert = _plog_find_attribute(record, work->attribute);
    plog_attribute_data_t *data;

    if (!insert || insert->attribute_type != work->attribute) {
        //
        // The attribute does not exist, create a new one and insert appropriately
        //
        data = new_plog_attribute_data_t();
        ZERO(data);
        data->attribute_type = work->attribute;
        data->emit_ordinal   = record->emit_ordinal;
        data->value.ref_val  = work->value.ref_val;
        if (!!insert) {
            DEQ_INSERT_AFTER(record->attributes, data, insert);
        } else {
            DEQ_INSERT_HEAD(record->attributes, data);
        }
    } else {
        //
        // The attribute already exists, overwrite the value
        //
        insert->value.ref_val = work->value.ref_val;
        insert->emit_ordinal  = record->emit_ordinal;
    }

    if (!record->unflushed) {
        DEQ_INSERT_TAIL_N(UNFLUSHED, unflushed_records, record);
        record->unflushed = true;
    }
}


/**
 * @brief Work handler for plog_set_string
 * 
 * @param work Pointer to work context
 * @param discard Indicator that this work must be discarded
 */
static void _plog_set_string_TH(plog_work_t *work, bool discard)
{
    if (discard) {
        free(work->value.string_val);
        return;
    }

    plog_record_t         *record = work->record;
    plog_attribute_data_t *insert = _plog_find_attribute(record, work->attribute);
    plog_attribute_data_t *data;

    if (!insert || insert->attribute_type != work->attribute) {
        //
        // The attribute does not exist, create a new one and insert appropriately
        //
        data = new_plog_attribute_data_t();
        ZERO(data);
        data->attribute_type   = work->attribute;
        data->emit_ordinal     = record->emit_ordinal;
        data->value.string_val = work->value.string_val;
        if (!!insert) {
            DEQ_INSERT_AFTER(record->attributes, data, insert);
        } else {
            DEQ_INSERT_HEAD(record->attributes, data);
        }
    } else {
        //
        // The attribute already exists, overwrite the value
        //
        free(insert->value.string_val);
        insert->value.string_val = work->value.string_val;
        insert->emit_ordinal     = record->emit_ordinal;
    }

    if (!record->unflushed) {
        DEQ_INSERT_TAIL_N(UNFLUSHED, unflushed_records, record);
        record->unflushed = true;
    }
}


/**
 * @brief Work handler for plog_set_int
 * 
 * @param work Pointer to work context
 * @param discard Indicator that this work must be discarded
 */
static void _plog_set_int_TH(plog_work_t *work, bool discard)
{
    if (discard) {
        return;
    }

    plog_record_t         *record = work->record;
    plog_attribute_data_t *insert = _plog_find_attribute(record, work->attribute);
    plog_attribute_data_t *data;

    if (!insert || insert->attribute_type != work->attribute) {
        //
        // The attribute does not exist, create a new one and insert appropriately
        //
        data = new_plog_attribute_data_t();
        ZERO(data);
        data->attribute_type = work->attribute;
        data->emit_ordinal   = record->emit_ordinal;
        data->value.uint_val = work->value.int_val;
        if (!!insert) {
            DEQ_INSERT_AFTER(record->attributes, data, insert);
        } else {
            DEQ_INSERT_HEAD(record->attributes, data);
        }
    } else {
        //
        // The attribute already exists, overwrite the value
        //
        insert->value.uint_val = work->value.int_val;
        insert->emit_ordinal   = record->emit_ordinal;
    }

    if (!record->unflushed) {
        DEQ_INSERT_TAIL_N(UNFLUSHED, unflushed_records, record);
        record->unflushed = true;
    }
}


/**
 * @brief Allocate a work object pre-loaded with a handler.
 * 
 * @param handler The handler to be called on the plog thread to do the work
 * @return plog_work_t* Pointer to the allocated work that should be posted for processing
 */
static plog_work_t *_plog_work(plog_work_handler_t handler)
{
    plog_work_t *work = new_plog_work_t();
    ZERO(work);
    work->handler = handler;
    return work;
}


/**
 * @brief Post work for processing in the plog thread
 * 
 * @param work Pointer to the work to be processed
 */
static void _plog_post_work(plog_work_t *work)
{
    sys_mutex_lock(lock);
    DEQ_INSERT_TAIL(work_list, work);
    bool need_signal = sleeping;
    sys_mutex_unlock(lock);

    if (need_signal) {
        sys_cond_signal(condition);
    }
}


/**
 * @brief Create the record that represents the local router.
 */
static void _plog_create_router_record(void)
{
    local_router = plog_start_record(PLOG_RECORD_ROUTER, 0);

    const char *hostname   = getenv("HOSTNAME");
    const char *namespace  = getenv("POD_NAMESPACE");
    const char *image_name = getenv("APPLICATION_NAME");
    const char *version    = getenv("VERSION");

    if (!!hostname) {
        plog_set_string(local_router, PLOG_ATTRIBUTE_HOST_NAME, hostname);
    }

    if (!!namespace) {
        plog_set_string(local_router, PLOG_ATTRIBUTE_NAMESPACE, namespace);
    }

    if (!!image_name) {
        plog_set_string(local_router, PLOG_ATTRIBUTE_IMAGE_NAME, image_name);
    }

    if (!!version) {
        plog_set_string(local_router, PLOG_ATTRIBUTE_IMAGE_VERSION, version);
    }

    plog_set_string(local_router, PLOG_ATTRIBUTE_BUILD_VERSION, QPID_DISPATCH_VERSION);
}


/**
 * @brief Recursively free the given record and all of its children
 * 
 * @param record Pointer to the record to be freed.
 */
static void _plog_free_record(plog_record_t *record)
{
    //
    // If this record is a child of a parent, remove it from the parent's child list
    //
    if (!!record->parent) {
        DEQ_REMOVE(record->parent->children, record);
    }

    //
    // Remove the record from the unflushed list if needed
    //
    if (record->unflushed) {
        DEQ_REMOVE_N(UNFLUSHED, unflushed_records, record);
        record->unflushed = false;
    }

    //
    // Remove all of this record's children
    //
    while (!DEQ_IS_EMPTY(record->children)) {
        _plog_free_record(DEQ_HEAD(record->children));
    }

    //
    // Free all of this record's attributes
    //
    plog_attribute_data_t *data = DEQ_HEAD(record->attributes);
    while (!!data) {
        DEQ_REMOVE_HEAD(record->attributes);
        if (1 << data->attribute_type & (VALID_STRING_ATTRS | VALID_TRACE_ATTRS)) {
            free(data->value.string_val);
        }
        free_plog_attribute_data_t(data);
        data = DEQ_HEAD(record->attributes);
    }

    //
    // Free the record
    //
    free_plog_record_t(record);
}


static const char *_plog_record_type_name(const plog_record_t *record)
{
    switch (record->record_type) {
    case PLOG_RECORD_SITE       : return "SITE";
    case PLOG_RECORD_ROUTER     : return "ROUTER";
    case PLOG_RECORD_LINK       : return "LINK";
    case PLOG_RECORD_CONTROLLER : return "CONTROLLER";
    case PLOG_RECORD_LISTENER   : return "LISTENER";
    case PLOG_RECORD_CONNECTOR  : return "CONNECTOR";
    case PLOG_RECORD_FLOW       : return "FLOW";
    case PLOG_RECORD_PROCESS    : return "PROCESS";
    }
    return "UNKNOWN";
}


static const char *_plog_attribute_name(const plog_attribute_data_t *data)
{
    switch (data->attribute_type) {
    case PLOG_ATTRIBUTE_IDENTITY         : return "identity";
    case PLOG_ATTRIBUTE_PARENT           : return "parent";
    case PLOG_ATTRIBUTE_SIBLING          : return "sibling";
    case PLOG_ATTRIBUTE_PROCESS          : return "process";
    case PLOG_ATTRIBUTE_SIBLING_ORDINAL  : return "sib_ordinal";
    case PLOG_ATTRIBUTE_LOCATION         : return "location";
    case PLOG_ATTRIBUTE_PROVIDER         : return "provider";
    case PLOG_ATTRIBUTE_PLATFORM         : return "platform";
    case PLOG_ATTRIBUTE_NAMESPACE        : return "namespace";
    case PLOG_ATTRIBUTE_MODE             : return "mode";
    case PLOG_ATTRIBUTE_SOURCE_HOST      : return "source_host";
    case PLOG_ATTRIBUTE_DESTINATION_HOST : return "dest_host";
    case PLOG_ATTRIBUTE_PROTOCOL         : return "protocol";
    case PLOG_ATTRIBUTE_SOURCE_PORT      : return "source_port";
    case PLOG_ATTRIBUTE_DESTINATION_PORT : return "dest_port";
    case PLOG_ATTRIBUTE_VAN_ADDRESS      : return "van_address";
    case PLOG_ATTRIBUTE_IMAGE_NAME       : return "image_name";
    case PLOG_ATTRIBUTE_IMAGE_VERSION    : return "image_version";
    case PLOG_ATTRIBUTE_HOST_NAME        : return "hostname";
    case PLOG_ATTRIBUTE_FLOW_TYPE        : return "flow_type";
    case PLOG_ATTRIBUTE_OCTETS           : return "octets";
    case PLOG_ATTRIBUTE_START_LATENCY    : return "start_latency";
    case PLOG_ATTRIBUTE_BACKLOG          : return "backlog";
    case PLOG_ATTRIBUTE_METHOD           : return "method";
    case PLOG_ATTRIBUTE_RESULT           : return "result";
    case PLOG_ATTRIBUTE_REASON           : return "reason";
    case PLOG_ATTRIBUTE_NAME             : return "name";
    case PLOG_ATTRIBUTE_TRACE            : return "trace";
    case PLOG_ATTRIBUTE_BUILD_VERSION    : return "build_version";
    }
    return "UNKNOWN";
}


/**
 * @brief Extract the value of a record identity from its serialized form in an iterator
 * 
 * @param field Pointer to the parsed field containing the serialized identity
 * @param identity [out] Pointer to the identity to be overwritten
 * @return True iff the serialized identity was well formed
 */
static bool _plog_unserialize_identity(qd_parsed_field_t *field, plog_identity_t *identity)
{
    if (!qd_parse_is_list(field) || qd_parse_sub_count(field) != 3) {
        return false;
    }

    qd_parsed_field_t *site_id_field   = qd_parse_sub_value(field, 0);
    qd_parsed_field_t *router_id_field = qd_parse_sub_value(field, 1);
    qd_parsed_field_t *record_id_field = qd_parse_sub_value(field, 2);

    if (!qd_parse_is_scalar(site_id_field) || !qd_parse_is_scalar(router_id_field) || !qd_parse_is_scalar(record_id_field)) {
        return false;
    }

    identity->site_id   = qd_parse_as_uint(site_id_field);
    identity->router_id = qd_parse_as_uint(router_id_field);
    identity->record_id = qd_parse_as_ulong(record_id_field);

    return true;
}


/**
 * @brief Emit a single record as a log event
 * 
 * @param record Pointer to the record to be emitted
 */
static void _plog_emit_record_as_log(plog_record_t *record)
{
#define LINE_MAX 1000
    char line[LINE_MAX + 1];

    strcpy(line, _plog_record_type_name(record));
    strcat(line, " [");
    _plog_strncat_id(line, LINE_MAX, &record->identity);
    strcat(line, "]");
    if (record->never_logged) {
        strcat(line, " BEGIN");
    }
    if (record->ended) {
        strcat(line, " END");
    }

    if ((record->ended || record->never_logged) && !!record->parent) {
        strcat(line, " parent=");
        _plog_strncat_id(line, LINE_MAX, &record->parent->identity);
    }

    plog_attribute_data_t *data = DEQ_HEAD(record->attributes);
    while (data) {
        strncat(line, " ", LINE_MAX);
        strncat(line, _plog_attribute_name(data), LINE_MAX);
        strncat(line, "=", LINE_MAX);
        _plog_strncat_attribute(line, LINE_MAX, data);
        data = DEQ_NEXT(data);
    }

    record->never_logged = false;
    qd_log(log, QD_LOG_INFO, line);
}


/**
 * @brief Emit all of the unflushed records
 * 
 */
static void _plog_flush(void)
{
    plog_record_t *record = DEQ_HEAD(unflushed_records);
    while (!!record) {
        DEQ_REMOVE_HEAD_N(UNFLUSHED, unflushed_records);
        assert(record->unflushed);
        record->unflushed = false;

        //
        // TODO - Emit event to collectors
        //

        //
        // If this record has been ended, emit the log line.
        //
        if (record->ended || record->force_log) {
            record->force_log = false;
            _plog_emit_record_as_log(record);
        }

        record->never_flushed = false;
        record->emit_ordinal++;
        if (record->ended) {
            _plog_free_record(record);
        }
        record = DEQ_HEAD(unflushed_records);
    }
}


/**
 * @brief Main function for the plog thread.  This thread runs for the entire
 * lifecycle of the router.
 * 
 * @param unused Unused
 * @return void* Unused
 */
static void *_plog_thread(void *unused)
{
    bool             running         = true;
    bool             do_flush;
    plog_work_list_t local_work_list = DEQ_EMPTY;

    qd_log(log, QD_LOG_INFO, "Protocol logging started");

    _plog_create_router_record();

    while (running) {
        //
        // Use the lock only to protect the condition variable and the work lists
        //
        sys_mutex_lock(lock);
        for (;;) {
            if (!DEQ_IS_EMPTY(work_list)) {
                DEQ_MOVE(work_list, local_work_list);
                do_flush = false;
                break;
            } else {
                do_flush = !DEQ_IS_EMPTY(unflushed_records);
            }

            if (do_flush || !running) {
                break;
            }

            //
            // Block on the condition variable when there is no work to do
            //
            sleeping = true;
            sys_cond_wait(condition, lock);
            sleeping = false;
        }
        sys_mutex_unlock(lock);

        if (do_flush) {
            _plog_flush();
        }

        //
        // Process the local work list with the lock not held
        //
        plog_work_t *work = DEQ_HEAD(local_work_list);
        while (work) {
            DEQ_REMOVE_HEAD(local_work_list);
            if (!!work->handler) {
                work->handler(work, !running);
            } else {
                //
                // The thread is signalled to exit by posting work with a null handler.
                //
                running = false;
            }
            free_plog_work_t(work);
            work = DEQ_HEAD(local_work_list);
        }
    }

    _plog_free_record(local_router);
    plog_record_t *record = DEQ_HEAD(unflushed_records);
    while (!!record) {
        _plog_free_record(record);
        record = DEQ_HEAD(unflushed_records);
    }

    qd_log(log, QD_LOG_INFO, "Protocol logging completed");
    return 0;
}


plog_record_t *plog_start_record(plog_record_type_t record_type, plog_record_t *parent)
{
    plog_record_t *record = new_plog_record_t();
    plog_work_t   *work   = _plog_work(_plog_start_record_TH);
    ZERO(record);
    record->record_type   = record_type;
    record->parent        = parent;
    record->unflushed     = false;
    record->never_flushed = true;
    record->never_logged  = true;
    record->force_log     = false;
    record->ended         = false;

    work->record = record;

    _plog_post_work(work);
    return record;
}


void plog_end_record(plog_record_t *record)
{
    if (!!record) {
        plog_work_t *work = _plog_work(_plog_end_record_TH);
        work->record = record;
        _plog_post_work(work);
    }
}


void plog_serialize_identity(const plog_record_t *record, qd_composed_field_t *field)
{
    assert(!!record);
    if (!!record) {
        qd_compose_start_list(field);
        qd_compose_insert_uint(field, record->identity.site_id);
        qd_compose_insert_uint(field, record->identity.router_id);
        qd_compose_insert_ulong(field, record->identity.record_id);
        qd_compose_end_list(field);
    }
}


void plog_set_ref_from_record(plog_record_t *record, plog_attribute_t attribute_type, plog_record_t *referenced_record)
{
    assert(1 << attribute_type & VALID_REF_ATTRS);
    plog_work_t *work = _plog_work(_plog_set_ref_TH);
    work->record        = record;
    work->attribute     = attribute_type;
    work->value.ref_val = referenced_record->identity;
    _plog_post_work(work);
}


void plog_set_ref_from_parsed(plog_record_t *record, plog_attribute_t attribute_type, qd_parsed_field_t *field)
{
    assert(1 << attribute_type & VALID_REF_ATTRS);
    plog_work_t *work = _plog_work(_plog_set_ref_TH);
    work->record    = record;
    work->attribute = attribute_type;
    bool good_id = _plog_unserialize_identity(field, &work->value.ref_val);

    if (good_id) {
        _plog_post_work(work);
    } else {
        free_plog_work_t(work);
        qd_log(log, QD_LOG_WARNING, "Reference ID cannot be parsed from the received field");
    }
}


void plog_set_string(plog_record_t *record, plog_attribute_t attribute_type, const char *value)
{
#define MAX_STRING_VALUE 300
    assert(1 << attribute_type & VALID_STRING_ATTRS);
    plog_work_t *work = _plog_work(_plog_set_string_TH);
    work->record           = record;
    work->attribute        = attribute_type;
    work->value.string_val = !!value ? strndup(value, strnlen(value, MAX_STRING_VALUE)) : 0;
    _plog_post_work(work);
}


void plog_set_uint64(plog_record_t *record, plog_attribute_t attribute_type, uint64_t value)
{
    assert(1 << attribute_type & VALID_UINT_ATTRS);
    plog_work_t *work = _plog_work(_plog_set_int_TH);
    work->record        = record;
    work->attribute     = attribute_type;
    work->value.int_val = value;
    _plog_post_work(work);
}


void plog_set_trace(plog_record_t *record, qd_message_t *msg)
{
#define MAX_TRACE_BUFFER 1000
    qd_iterator_t *ma_iter = qd_message_field_iterator(msg, QD_FIELD_MESSAGE_ANNOTATION);
    char *trace_text     = "Local";
    char *trace_text_ptr = trace_text;
    char  trace_buffer[MAX_TRACE_BUFFER + 1];

    do {
        if (!ma_iter) {
            break;
        }

        qd_parsed_field_t *ma = qd_parse(ma_iter);
        if (!ma) {
            break;
        }

        do {
            if (!qd_parse_ok(ma) || !qd_parse_is_map(ma)) {
                break;
            }

            uint32_t count = qd_parse_sub_count(ma);
            qd_parsed_field_t *trace_value = 0;
            for (uint32_t i = 0; i < count; i++) {
                qd_parsed_field_t *key = qd_parse_sub_key(ma, i);
                if (key == 0) {
                    break;
                }
                qd_iterator_t *key_iter = qd_parse_raw(key);
                if (!!key_iter && qd_iterator_equal(key_iter, (const unsigned char*) QD_MA_TRACE)) {
                    trace_value = qd_parse_sub_value(ma, i);
                    break;
                }
            }

            if (!!trace_value && qd_parse_is_list(trace_value)) {
                trace_text_ptr = trace_buffer;
                char *cursor   = trace_text_ptr;
                uint32_t trace_count = qd_parse_sub_count(trace_value);
                for (uint32_t i = 0; i < trace_count; i++) {
                    qd_parsed_field_t *trace_item = qd_parse_sub_value(trace_value, i);
                    if (i > 0) {
                        *(cursor++) = '|';
                    }
                    if (qd_parse_is_scalar(trace_item)) {
                        cursor += qd_iterator_ncopy(qd_parse_raw(trace_item),
                                                    (uint8_t*) cursor, MAX_TRACE_BUFFER - (cursor - trace_text_ptr));;
                    }
                }
                *(cursor++) = '\0';
            }
        } while (false);
        qd_parse_free(ma);
    } while (false);
    qd_iterator_free(ma_iter);

    plog_work_t *work = _plog_work(_plog_set_string_TH);
    work->record    = record;
    work->attribute = PLOG_ATTRIBUTE_TRACE;
    work->value.string_val = strdup(trace_text_ptr);
    _plog_post_work(work);
}


/**
 * @brief Module initializer
 * 
 * @param core Pointer to the core object
 * @param adaptor_context (out) Unused context
 */
static void _plog_init(qdr_core_t *core, void **adaptor_context)
{
    router_id = qdr_core_dispatch(core)->plog_router_id;
    site_id   = qdr_core_dispatch(core)->plog_site_id;
    log       = qd_log_source("PROTOCOL_LOG");
    lock      = sys_mutex();
    condition = sys_cond();
    thread    = sys_thread(_plog_thread, 0);
    *adaptor_context = 0;
}


/**
 * @brief Module finalizer
 * 
 * @param adaptor_context Unused
 */
static void _plog_final(void *adaptor_context)
{
    _plog_post_work(_plog_work(0));  // Signal for the thread to exit
    sys_thread_join(thread);
    sys_thread_free(thread);
    sys_cond_free(condition);
    sys_mutex_free(lock);
}


QDR_CORE_ADAPTOR_DECLARE_ORD("Protocol Logging", _plog_init, _plog_final, 10)

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

#include "agent_config_address.h"

#include "qpid/dispatch/ctools.h"

#include <inttypes.h>
#include <stdio.h>

#define QDR_CONFIG_ADDRESS_NAME          0
#define QDR_CONFIG_ADDRESS_IDENTITY      1
#define QDR_CONFIG_ADDRESS_TYPE          2
#define QDR_CONFIG_ADDRESS_PREFIX        3
#define QDR_CONFIG_ADDRESS_DISTRIBUTION  4
#define QDR_CONFIG_ADDRESS_WAYPOINT      5
#define QDR_CONFIG_ADDRESS_IN_PHASE      6
#define QDR_CONFIG_ADDRESS_OUT_PHASE     7
#define QDR_CONFIG_ADDRESS_PATTERN       8
#define QDR_CONFIG_ADDRESS_PRIORITY      9
#define QDR_CONFIG_ADDRESS_FALLBACK      10

const char *qdr_config_address_columns[] =
    {"name",
     "identity",
     "type",
     "prefix",
     "distribution",
     "waypoint",
     "ingressPhase",
     "egressPhase",
     "pattern",
     "priority",
     "fallback",
     0};

const char *CONFIG_ADDRESS_TYPE = "org.apache.qpid.dispatch.router.config.address";
const char CONFIG_ADDRESS_PREFIX = 'C';

static void qdr_config_address_insert_column_CT(qdr_address_config_t *addr, int col, qd_composed_field_t *body, bool as_map)
{
    const char *text = 0;

    if (as_map)
        qd_compose_insert_string(body, qdr_config_address_columns[col]);

    switch(col) {
    case QDR_CONFIG_ADDRESS_NAME:
        if (addr->name)
            qd_compose_insert_string(body, addr->name);
        else
            qd_compose_insert_null(body);
        break;

    case QDR_CONFIG_ADDRESS_IDENTITY: {
        char id_str[100];
        snprintf(id_str, 100, "%"PRId64, addr->identity);
        qd_compose_insert_string(body, id_str);
        break;
    }

    case QDR_CONFIG_ADDRESS_TYPE:
        qd_compose_insert_string(body, CONFIG_ADDRESS_TYPE);
        break;

    case QDR_CONFIG_ADDRESS_PREFIX:
        if (addr->is_prefix && addr->pattern) {
            // Note (kgiusti): internally we prepend a '/#' to the configured
            // prefix and treat it like a pattern.  Remove trailing '/#' to put
            // it back into its original form
            const size_t len = strlen(addr->pattern);
            assert(len > 1);
            qd_compose_insert_string_n(body, addr->pattern, len - 2);
        } else
            qd_compose_insert_null(body);
        break;

    case QDR_CONFIG_ADDRESS_PATTERN:
        if (!addr->is_prefix && addr->pattern)
            qd_compose_insert_string(body, addr->pattern);
        else
            qd_compose_insert_null(body);
        break;

    case QDR_CONFIG_ADDRESS_DISTRIBUTION:
        switch (addr->treatment) {
        case QD_TREATMENT_MULTICAST_FLOOD:
        case QD_TREATMENT_MULTICAST_ONCE:   text = "multicast"; break;
        case QD_TREATMENT_ANYCAST_CLOSEST:  text = "closest";   break;
        case QD_TREATMENT_ANYCAST_BALANCED: text = "balanced";  break;
        default:
            text = 0;
        }

        if (text)
            qd_compose_insert_string(body, text);
        else
            qd_compose_insert_null(body);

        break;

    case QDR_CONFIG_ADDRESS_WAYPOINT:
        qd_compose_insert_bool(body, addr->in_phase == 0 && addr->out_phase == 1);
        break;

    case QDR_CONFIG_ADDRESS_IN_PHASE:
        qd_compose_insert_int(body, addr->in_phase);
        break;

    case QDR_CONFIG_ADDRESS_OUT_PHASE:
        qd_compose_insert_int(body, addr->out_phase);
        break;

    case QDR_CONFIG_ADDRESS_PRIORITY:
        qd_compose_insert_int(body, addr->priority);
        break;

    case QDR_CONFIG_ADDRESS_FALLBACK:
        qd_compose_insert_bool(body, addr->fallback);
        break;
    }
}


static void qdr_agent_write_config_address_CT(qdr_query_t *query,  qdr_address_config_t *addr)
{
    qd_composed_field_t *body = query->body;

    qd_compose_start_list(body);
    int i = 0;
    while (query->columns[i] >= 0) {
        qdr_config_address_insert_column_CT(addr, query->columns[i], body, false);
        i++;
    }
    qd_compose_end_list(body);
}


static void qdr_manage_advance_config_address_CT(qdr_query_t *query, qdr_address_config_t *addr)
{
    if (addr) {
        addr = DEQ_NEXT(addr);
        query->more = !!addr;
        query->next_offset++;
    }
    else {
        query->more = false;
    }
}


void qdra_config_address_get_first_CT(qdr_core_t *core, qdr_query_t *query, int offset)
{
    //
    // Queries that get this far will always succeed.
    //
    query->status = QD_AMQP_OK;

    //
    // If the offset goes beyond the set of objects, end the query now.
    //
    if (offset >= DEQ_SIZE(core->addr_config)) {
        query->more = false;
        qdr_agent_enqueue_response_CT(core, query);
        return;
    }

    //
    // Run to the object at the offset.
    //
    qdr_address_config_t *addr = DEQ_HEAD(core->addr_config);
    for (int i = 0; i < offset && addr; i++)
        addr = DEQ_NEXT(addr);
    assert(addr);

    if (addr) {
        //
        // Write the columns of the object into the response body.
        //
        qdr_agent_write_config_address_CT(query, addr);

        //
        // Advance to the next address
        //
        query->next_offset = offset;
        qdr_manage_advance_config_address_CT(query, addr);

    }
    else {
        query->more = false;
    }



    //
    // Enqueue the response.
    //
    qdr_agent_enqueue_response_CT(core, query);
}


void qdra_config_address_get_next_CT(qdr_core_t *core, qdr_query_t *query)
{
    qdr_address_config_t *addr = 0;

    if (query->next_offset < DEQ_SIZE(core->addr_config)) {
        addr = DEQ_HEAD(core->addr_config);

        if (!addr) {
            query->more = false;
            qdr_agent_enqueue_response_CT(core, query);
            return;
        }

        for (int i = 0; i < query->next_offset && addr; i++)
            addr = DEQ_NEXT(addr);
    }

    if (addr) {
        //
        // Write the columns of the addr entity into the response body.
        //
        qdr_agent_write_config_address_CT(query, addr);

        //
        // Advance to the next object
        //
        qdr_manage_advance_config_address_CT(query, addr);
    } else
        query->more = false;

    //
    // Enqueue the response.
    //
    qdr_agent_enqueue_response_CT(core, query);
}


static qd_address_treatment_t qdra_address_treatment_CT(qd_parsed_field_t *field)
{
    if (field) {
        qd_iterator_t *iter = qd_parse_raw(field);
        if (qd_iterator_equal(iter, (unsigned char*) "multicast"))    return QD_TREATMENT_MULTICAST_ONCE;
        if (qd_iterator_equal(iter, (unsigned char*) "closest"))      return QD_TREATMENT_ANYCAST_CLOSEST;
        if (qd_iterator_equal(iter, (unsigned char*) "balanced"))     return QD_TREATMENT_ANYCAST_BALANCED;
        if (qd_iterator_equal(iter, (unsigned char*) "unavailable"))  return QD_TREATMENT_UNAVAILABLE;
    }
    return QD_TREATMENT_ANYCAST_BALANCED;
}


static qdr_address_config_t *qdr_address_config_find_by_identity_CT(qdr_core_t *core, qd_iterator_t *identity)
{
    if (!identity)
        return 0;

    qdr_address_config_t *rc = DEQ_HEAD(core->addr_config);
    while (rc) {
        // Convert the passed in identity to a char*
        char id[100];
        snprintf(id, 100, "%"PRId64, rc->identity);
        if (qd_iterator_equal(identity, (const unsigned char*) id))
            break;
        rc = DEQ_NEXT(rc);
    }

    return rc;

}


static qdr_address_config_t *qdr_address_config_find_by_name_CT(qdr_core_t *core, qd_iterator_t *name)
{
    if (!name)
        return 0;

    qdr_address_config_t *rc = DEQ_HEAD(core->addr_config);
    while (rc) { // Sometimes the name can be null
        if (rc->name && qd_iterator_equal(name, (const unsigned char*) rc->name))
            break;
        rc = DEQ_NEXT(rc);
    }

    return rc;
}


void qdra_config_address_delete_CT(qdr_core_t    *core,
                                   qdr_query_t   *query,
                                   qd_iterator_t *name,
                                   qd_iterator_t *identity)
{
    qdr_address_config_t *addr = 0;

    if (!name && !identity) {
        query->status = QD_AMQP_BAD_REQUEST;
        query->status.description = "No name or identity provided";
        qd_log(core->agent_log, QD_LOG_ERROR, "Error performing DELETE of %s: %s", CONFIG_ADDRESS_TYPE, query->status.description);
    }
    else {
        if (identity)
            addr = qdr_address_config_find_by_identity_CT(core, identity);
        else if (name)
            addr = qdr_address_config_find_by_name_CT(core, name);

        if (addr) {
            qdr_core_remove_address_config(core, addr);
            query->status = QD_AMQP_NO_CONTENT;
        } else
            query->status = QD_AMQP_NOT_FOUND;
    }

    //
    // Enqueue the response.
    //
    qdr_agent_enqueue_response_CT(core, query);
}


void qdra_config_address_create_CT(qdr_core_t         *core,
                                   qd_iterator_t      *name,
                                   qdr_query_t        *query,
                                   qd_parsed_field_t  *in_body)
{
    char *pattern = NULL;

    while (true) {
        //
        // Ensure there isn't a duplicate name
        //
        qdr_address_config_t *addr = 0;
        if (name) {
            qd_iterator_view_t iter_view = qd_iterator_get_view(name);
            qd_iterator_annotate_prefix(name, CONFIG_ADDRESS_PREFIX);
            qd_iterator_reset_view(name, ITER_VIEW_ADDRESS_HASH);
            qd_hash_retrieve(core->addr_lr_al_hash, name, (void**) &addr);
            qd_iterator_reset_view(name, iter_view);
        }

        if (!!addr) {
            query->status = QD_AMQP_BAD_REQUEST;
            query->status.description = "Name conflicts with an existing entity";
            qd_log(core->agent_log, QD_LOG_ERROR, "Error performing CREATE of %s: %s", CONFIG_ADDRESS_TYPE, query->status.description);
            break;
        }

        // Ensure that the body is a map
        if (!qd_parse_is_map(in_body)) {
            query->status = QD_AMQP_BAD_REQUEST;
            query->status.description = "Body of request must be a map";
            qd_log(core->agent_log, QD_LOG_ERROR, "Error performing CREATE of %s: %s", CONFIG_ADDRESS_TYPE, query->status.description);
            break;
        }

        //
        // Extract the fields from the request
        //
        qd_parsed_field_t *prefix_field    = qd_parse_value_by_key(in_body, qdr_config_address_columns[QDR_CONFIG_ADDRESS_PREFIX]);
        qd_parsed_field_t *pattern_field   = qd_parse_value_by_key(in_body, qdr_config_address_columns[QDR_CONFIG_ADDRESS_PATTERN]);
        qd_parsed_field_t *distrib_field   = qd_parse_value_by_key(in_body, qdr_config_address_columns[QDR_CONFIG_ADDRESS_DISTRIBUTION]);
        qd_parsed_field_t *waypoint_field  = qd_parse_value_by_key(in_body, qdr_config_address_columns[QDR_CONFIG_ADDRESS_WAYPOINT]);
        qd_parsed_field_t *in_phase_field  = qd_parse_value_by_key(in_body, qdr_config_address_columns[QDR_CONFIG_ADDRESS_IN_PHASE]);
        qd_parsed_field_t *out_phase_field = qd_parse_value_by_key(in_body, qdr_config_address_columns[QDR_CONFIG_ADDRESS_OUT_PHASE]);
        qd_parsed_field_t *priority_field  = qd_parse_value_by_key(in_body, qdr_config_address_columns[QDR_CONFIG_ADDRESS_PRIORITY]);
        qd_parsed_field_t *fallback_field  = qd_parse_value_by_key(in_body, qdr_config_address_columns[QDR_CONFIG_ADDRESS_FALLBACK]);

        bool waypoint  = waypoint_field  ? qd_parse_as_bool(waypoint_field)  : false;
        long in_phase  = in_phase_field  ? qd_parse_as_long(in_phase_field)  : -1;
        long out_phase = out_phase_field ? qd_parse_as_long(out_phase_field) : -1;
        long priority  = priority_field  ? qd_parse_as_long(priority_field)  : -1;
        bool fallback  = fallback_field  ? qd_parse_as_bool(fallback_field)  : false;

        //
        // Either a prefix or a pattern field is mandatory.  Prefix and pattern
        // are mutually exclusive. Fail if either both or none are given.
        //
        const char *msg = NULL;
        if (!prefix_field && !pattern_field) {
            msg = "Either a 'prefix' or 'pattern' attribute must be provided";
        } else if (prefix_field && pattern_field) {
            msg = "Cannot specify both a 'prefix' and a 'pattern' attribute";
        }

        if (fallback && (waypoint || in_phase > 0 || out_phase > 0)) {
            msg = "Fallback cannot be specified with waypoint or non-zero ingress and egress phases";
        }

        if (msg) {
            query->status = QD_AMQP_BAD_REQUEST;
            query->status.description = msg;
            qd_log(core->agent_log, QD_LOG_ERROR, "Error performing CREATE of %s: %s", CONFIG_ADDRESS_TYPE, query->status.description);
            break;
        }

        // validate the pattern/prefix, add "/#" if prefix
        pattern = qdra_config_address_validate_pattern_CT((prefix_field) ? prefix_field : pattern_field,
                                                          !!prefix_field,
                                                          &msg);
        if (!pattern) {
            query->status = QD_AMQP_BAD_REQUEST;
            query->status.description = msg;
            qd_log(core->agent_log, QD_LOG_ERROR, "Error performing CREATE of %s: %s", CONFIG_ADDRESS_TYPE, query->status.description);
            break;
        }

        //
        // Handle the address-phasing logic.  If the phases are provided, use them.  Otherwise
        // use the waypoint flag to set the most common defaults.
        //
        if (in_phase == -1 && out_phase == -1) {
            in_phase  = 0;
            out_phase = waypoint ? 1 : 0;
        }

        //
        // Validate the phase values
        //
        if (in_phase < 0 || in_phase > 9 || out_phase < 0 || out_phase > 9) {
            query->status = QD_AMQP_BAD_REQUEST;
            query->status.description = "Phase values must be between 0 and 9";
            qd_log(core->agent_log, QD_LOG_ERROR, "Error performing CREATE of %s: %s", CONFIG_ADDRESS_TYPE, query->status.description);
            break;
        }

        //
        // Validate the priority values.
        //
        if (priority > QDR_MAX_PRIORITY ) {
            query->status = QD_AMQP_BAD_REQUEST;
            query->status.description = "Priority value, if present, must be between 0 and QDR_MAX_PRIORITY";
            qd_log(core->agent_log, QD_LOG_ERROR, "Error performing CREATE of %s: %s", CONFIG_ADDRESS_TYPE, query->status.description);
            break;
        }

        //
        // The request is valid.  Attempt to insert the address pattern into
        // the parse tree, fail if there is already an entry for that pattern
        //
        addr = new_qdr_address_config_t();
        if (!addr) {
            query->status = QD_AMQP_BAD_REQUEST;
            query->status.description = "Out of memory";
            qd_log(core->agent_log, QD_LOG_ERROR, "Error performing CREATE of %s: %s", CONFIG_ADDRESS_TYPE, query->status.description);
            break;
        }
        ZERO(addr);

        //
        // Insert the uninitialized address to check if it already exists in
        // the parse tree.  On success initialize it.  This is thread safe
        // since the current thread (core) is the only thread allowed to use
        // the parse tree
        //

        qd_error_t rc = qd_parse_tree_add_pattern_str(core->addr_parse_tree, pattern, addr);
        if (rc) {
            free_qdr_address_config_t(addr);
            query->status = QD_AMQP_BAD_REQUEST;
            query->status.description = qd_error_name(rc);
            qd_log(core->agent_log, QD_LOG_ERROR, "Error performing CREATE of %s: %s", CONFIG_ADDRESS_TYPE, query->status.description);
            break;
        }

        addr->ref_count = 1; // Represents the reference from the addr_config list
        addr->name      = name ? (char*) qd_iterator_copy(name) : 0;
        addr->identity  = qdr_identifier(core);
        addr->treatment = qdra_address_treatment_CT(distrib_field);
        addr->in_phase  = in_phase;
        addr->out_phase = out_phase;
        addr->is_prefix = !!prefix_field;
        addr->pattern   = pattern;
        addr->priority  = priority;
        addr->fallback  = fallback;
        pattern = 0;

        DEQ_INSERT_TAIL(core->addr_config, addr);
        if (name) {
            qd_iterator_view_t iter_view = qd_iterator_get_view(name);
            qd_iterator_reset_view(name, ITER_VIEW_ADDRESS_HASH);
            qd_hash_insert(core->addr_lr_al_hash, name, addr, &addr->hash_handle);
            qd_iterator_reset_view(name, iter_view);
        }
        //
        // Compose the result map for the response.
        //
        if (query->body) {
            qd_compose_start_map(query->body);
            for (int col = 0; col < QDR_CONFIG_ADDRESS_COLUMN_COUNT; col++)
                qdr_config_address_insert_column_CT(addr, col, query->body, true);
            qd_compose_end_map(query->body);
        }

        query->status = QD_AMQP_CREATED;
        break;
    }

    //
    // Enqueue the response if there is a body. If there is no body, this is a management
    // operation created internally by the configuration file parser.
    //
    if (query->body) {
        //
        // If there was an error in processing the create, insert a NULL value into the body.
        //
        if (query->status.status / 100 > 2)
            qd_compose_insert_null(query->body);
        qdr_agent_enqueue_response_CT(core, query);
    } else
        qdr_query_free(query);

    free(pattern);
}


static void qdr_manage_write_config_address_map_CT(qdr_core_t          *core,
                                                   qdr_address_config_t *addr,
                                                   qd_composed_field_t *body,
                                                   const char          *qdr_config_address_columns[])
{
    qd_compose_start_map(body);

    for(int i = 0; i < QDR_CONFIG_ADDRESS_COLUMN_COUNT; i++) {
        qd_compose_insert_string(body, qdr_config_address_columns[i]);
        qdr_config_address_insert_column_CT(addr, i, body, false);
    }

    qd_compose_end_map(body);
}


void qdra_config_address_get_CT(qdr_core_t    *core,
                                qd_iterator_t *name,
                                qd_iterator_t *identity,
                                qdr_query_t   *query,
                                const char    *qdr_config_address_columns[])
{
    qdr_address_config_t *addr = 0;

    if (!name && !identity) {
        query->status = QD_AMQP_BAD_REQUEST;
        query->status.description = "No name or identity provided";
        qd_log(core->agent_log, QD_LOG_ERROR, "Error performing READ of %s: %s", CONFIG_ADDRESS_TYPE, query->status.description);
    }
    else {
        if (identity) //If there is identity, ignore the name
            addr = qdr_address_config_find_by_identity_CT(core, identity);
        else if (name)
            addr = qdr_address_config_find_by_name_CT(core, name);

        if (addr == 0) {
            // Send back a 404
            query->status = QD_AMQP_NOT_FOUND;
        }
        else {
            //
            // Write the columns of the address entity into the response body.
            //
            qdr_manage_write_config_address_map_CT(core, addr, query->body, qdr_config_address_columns);
            query->status = QD_AMQP_OK;
        }
    }

    //
    // Enqueue the response.
    //
    qdr_agent_enqueue_response_CT(core, query);

}


// given an address pattern parsed field, validate it and convert it to a string
char *qdra_config_address_validate_pattern_CT(qd_parsed_field_t *pattern_field,
                                              bool is_prefix,
                                              const char **error)
{
    char *buf = NULL;
    char *pattern = NULL;
    uint8_t tag = qd_parse_tag(pattern_field);
    qd_iterator_t *p_iter = qd_parse_raw(pattern_field);
    int len = qd_iterator_length(p_iter);
    *error = NULL;

    if ((tag != QD_AMQP_STR8_UTF8 && tag != QD_AMQP_STR32_UTF8)
        || len == 0)
    {
        *error = ((is_prefix)
                  ? "Prefix must be a non-empty string type"
                  : "Pattern must be a non-empty string type");
        goto exit;
    }

    buf = (char *)qd_iterator_copy(p_iter);
    char *begin = buf;
    // strip leading token separators
    // note: see parse_tree.c for acceptable separator characters
    while (*begin && strchr("./", *begin))
        begin++;

    // strip trailing separators
    while (*begin) {
        char *end = &begin[strlen(begin) - 1];
        if (!strchr("./", *end))
            break;
        *end = 0;
    }

    if (*begin == 0) {
        *error = ((is_prefix)
                  ? "Prefix invalid - no tokens"
                  : "Pattern invalid - no tokens");
        goto exit;
    }

    if (is_prefix) {
        // convert a prefix match into a valid pattern by appending "/#"
        pattern = malloc(strlen(begin) + 3);
        strcpy(pattern, begin);
        strcat(pattern, "/#");
    } else {
        pattern = strdup(begin);
    }

exit:
    free(buf);
    return pattern;
}

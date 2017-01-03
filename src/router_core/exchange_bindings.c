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
#include <qpid/dispatch/ctools.h>
#include "exchange_bindings.h"
#include <inttypes.h>
#include <stdio.h>


typedef struct qdr_binding qdr_binding_t;
typedef struct qdr_exchange qdr_exchange_t;
typedef struct key_node key_node_t;


////
//
// Iterator for a binding string
// @TODO(kgiusti): replace with qd_iterator_t
//
typedef struct token {
    const char *begin;
    const char *end;
} token_t;

typedef struct token_iterator {
    token_t token;           // token at head of string
    const char *terminator;  // end of entire string
} token_iterator_t;


// Represents a subject key --> next hop mapping
typedef struct qdr_binding
{
    DEQ_LINKS_N(exchange_list, qdr_binding_t);  // exchange's list of all bindings
    DEQ_LINKS_N(node_list, qdr_binding_t);      // parse tree node list of
                                                // matching bindings
    qd_iterator_t       *name;
    unsigned char       *name_str;

    uint64_t             identity;

    qd_iterator_t       *key;
    unsigned char       *key_str;

    qd_iterator_t       *next_hop;
    unsigned char       *next_hop_str;

    qdr_exchange_t      *exchange;

    // statistics
    uint64_t             msgs_matched;
} qdr_binding_t;

ALLOC_DECLARE(qdr_binding_t);
ALLOC_DEFINE(qdr_binding_t);
DEQ_DECLARE(qdr_binding_t, qdr_binding_list_t);


typedef struct qdr_exchange {
    DEQ_LINKS(qdr_exchange_t);          // for core->exchanges
    uint64_t            identity;
    qd_iterator_t      *name;
    unsigned char      *name_str;
    qd_iterator_t      *alternate;
    unsigned char      *alternate_str;
    key_node_t         *parse_tree;
    qdr_address_t      *qdr_addr;
    char               *scratch;
    size_t              scratch_len;
    qdr_binding_list_t  bindings;
    qdr_core_t         *core;

    uint64_t msgs_received;
    uint64_t msgs_dropped;
    uint64_t msgs_routed;

} qdr_exchange_t;

ALLOC_DECLARE(qdr_exchange_t);
ALLOC_DEFINE(qdr_exchange_t);


static void token_iterator_init(token_iterator_t *t, const char *str);
static int send_message(qdr_core_t     *core,
                        qd_iterator_t  *to_addr,
                        const char     *to_addr_str,
                        qd_message_t   *msg,
                        qdr_delivery_t *in_delivery,
                        bool            exclude_inprocess,
                        bool            control);
static int key_node_forward(key_node_t       *node,
                            qdr_exchange_t   *ex,
                            token_iterator_t *subject,
                            qd_message_t    *msg,
                            qdr_delivery_t    *in_delivery,
                            bool              exclude_inprocess,
                            bool              control);


// Forwarder handler
//
int qdr_forward_exchange_CT(qdr_core_t    *core,
                            qdr_address_t *addr,
                            qd_message_t  *msg,
                            qdr_delivery_t *in_delivery,
                            bool           exclude_inprocess,
                            bool           control)
{
    assert(addr->treatment == QD_TREATMENT_EXCHANGE);
    assert(addr->exchange != NULL);

    int forwarded = 0;
    qdr_exchange_t *ex = addr->exchange;
    qd_iterator_t *subject = qd_message_check(msg, QD_DEPTH_PROPERTIES)
        ? qd_message_field_iterator(msg, QD_FIELD_SUBJECT)
        : NULL;

    ex->msgs_received += 1;
    if (subject) {
        // TODO(kgiusti): use qd_iterator_t instead
        int len = qd_iterator_length(subject);
        if (len >= ex->scratch_len) {
            ex->scratch_len = len + 1;
            free(ex->scratch);
            ex->scratch = malloc(ex->scratch_len);
        }
        qd_iterator_strncpy(subject, ex->scratch, ex->scratch_len);
        qd_iterator_free(subject);
        token_iterator_t subj;
        token_iterator_init(&subj, ex->scratch);
        forwarded = key_node_forward(ex->parse_tree, ex, &subj,
                                     msg, in_delivery,
                                     exclude_inprocess,
                                     control);
    }

    if (forwarded == 0 && ex->alternate) {
        // forward to alternate exchange
        forwarded = send_message(core, ex->alternate, (char *)ex->alternate_str,
                                 msg, in_delivery, exclude_inprocess,
                                 control);
    }
    if (forwarded == 0) {
        ++ex->msgs_dropped;
    } else {
        ++ex->msgs_routed;
    }

    return forwarded;
}


/////////////////////////////
// Exchange Management API //
/////////////////////////////

#define QDR_CONFIG_EXCHANGE_NAME          0
#define QDR_CONFIG_EXCHANGE_IDENTITY      1
#define QDR_CONFIG_EXCHANGE_TYPE          2
#define QDR_CONFIG_EXCHANGE_ALTERNATE     3
#define QDR_CONFIG_EXCHANGE_BINDING_COUNT 4
#define QDR_CONFIG_EXCHANGE_RECEIVED      5
#define QDR_CONFIG_EXCHANGE_DROPPED       6
#define QDR_CONFIG_EXCHANGE_FORWARDED     7

const char *qdr_config_exchange_columns[QDR_CONFIG_EXCHANGE_COLUMN_COUNT + 1] =
    {"name",
     "identity",
     "type",
     "alternate",
     "bindingCount",
     "msgReceived",
     "msgDropped",
     "msgForwarded",
     0};

static const char *CONFIG_EXCHANGE_TYPE = "org.apache.qpid.dispatch.router.config.exchange";

#define QDR_CONFIG_BINDING_NAME         0
#define QDR_CONFIG_BINDING_IDENTITY     1
#define QDR_CONFIG_BINDING_TYPE         2
#define QDR_CONFIG_BINDING_EXCHANGE     3
#define QDR_CONFIG_BINDING_KEY          4
#define QDR_CONFIG_BINDING_NEXTHOP      5
#define QDR_CONFIG_BINDING_MATCHED      6

const char *qdr_config_binding_columns[QDR_CONFIG_BINDING_COLUMN_COUNT + 1] =
    {"name",
     "identity",
     "type",
     "exchange",
     "key",
     "nextHop",
     "msgMatched",
     0};

static const char *CONFIG_BINDING_TYPE = "org.apache.qpid.dispatch.router.config.binding";


static qdr_exchange_t *qdr_exchange(qdr_core_t *core,
                                    qd_iterator_t *name,
                                    qd_iterator_t *alternate,
                                    qdr_address_t *address);
static void write_config_exchange_map(qdr_exchange_t      *ex,
                                      qd_composed_field_t *body);

// Exchange CREATE
//
//
void qdra_config_exchange_create_CT(qdr_core_t         *core,
                                    qd_iterator_t      *name,
                                    qdr_query_t        *query,
                                    qd_parsed_field_t  *in_body)
{
    qdr_exchange_t *ex = NULL;
    query->status = QD_AMQP_BAD_REQUEST;

    if (!qd_parse_is_map(in_body)) {
        query->status.description = "Body of request must be a map";
        goto exit;
    }

    if (!name) {
        query->status.description = "exchange name is mandatory";
        goto exit;
    }

    ex = DEQ_HEAD(core->exchanges);
    while (ex) {
        if (qd_iterator_equal(name, ex->name_str)) {
            query->status.description = "Name conflicts with an existing exchange";
            goto exit;
        }
        ex = DEQ_NEXT(ex);
    }

    qd_iterator_t *alternate = NULL;
    qd_parsed_field_t *alternate_field = qd_parse_value_by_key(in_body,
                                                               qdr_config_exchange_columns[QDR_CONFIG_EXCHANGE_ALTERNATE]);
    if (alternate_field)
        alternate = qd_parse_raw(alternate_field);

    qdr_address_t *addr;
    qd_iterator_t *tmp = qd_iterator_dup(name);
    qd_iterator_reset_view(tmp, ITER_VIEW_ADDRESS_HASH);
    qd_hash_retrieve(core->addr_hash, tmp, (void**) &addr);
    if (addr) {
        qd_iterator_free(tmp);
        query->status.description = "Exchange name conflicts with existing address";
        goto exit;
    }
    addr = qdr_address_CT(core, QD_TREATMENT_EXCHANGE);
    ex = qdr_exchange(core, name, alternate, addr);
    addr->exchange = ex;
    qd_iterator_reset_view(tmp, ITER_VIEW_ADDRESS_HASH);
    qd_hash_insert(core->addr_hash, tmp, addr, &addr->hash_handle);
    DEQ_INSERT_TAIL(core->addrs, addr);
    qd_iterator_free(tmp);
    query->status = QD_AMQP_CREATED;

    if (query->body) {
        write_config_exchange_map(ex, query->body);
    }

 exit:

    if (query->status.status == QD_AMQP_CREATED.status) {
        qd_log(core->agent_log, QD_LOG_DEBUG,
               "Exchange %s CREATED (id=%"PRIu64")", ex->name_str, ex->identity);

    } else {
        qd_log(core->agent_log, QD_LOG_ERROR,
               "Error performing CREATE of %s: %s", CONFIG_EXCHANGE_TYPE, query->status.description);
        // return a NULL body:
        if (query->body) qd_compose_insert_null(query->body);
    }

    if (query->body) {
        qdr_agent_enqueue_response_CT(core, query);
    } else
        // no body == create from internal config parser
        qdr_query_free(query);
}


static qdr_exchange_t *find_exchange(qdr_core_t *core, qd_iterator_t *identity, qd_iterator_t *name);
static void qdr_exchange_free(qdr_exchange_t *ex);

// Exchange DELETE:
//
void qdra_config_exchange_delete_CT(qdr_core_t    *core,
                                    qdr_query_t   *query,
                                    qd_iterator_t *name,
                                    qd_iterator_t *identity)
{
    qdr_exchange_t *ex = 0;

    if (!name && !identity) {
        query->status = QD_AMQP_BAD_REQUEST;
        query->status.description = "No name or identity provided";
        qd_log(core->agent_log, QD_LOG_ERROR, "Error performing DELETE of %s: %s", CONFIG_EXCHANGE_TYPE, query->status.description);
    }
    else {
        ex = find_exchange(core, identity, name);
        if (ex) {
            DEQ_REMOVE(core->exchanges, ex);
            qdr_exchange_free(ex);
            query->status = QD_AMQP_NO_CONTENT;
        } else
            query->status = QD_AMQP_NOT_FOUND;
    }

    qdr_agent_enqueue_response_CT(core, query);
}

// Exchange GET
//
void qdra_config_exchange_get_CT(qdr_core_t    *core,
                                 qd_iterator_t *name,
                                 qd_iterator_t *identity,
                                 qdr_query_t   *query,
                                 const char    *columns[])
{
    qdr_exchange_t *ex = 0;

    if (!name && !identity) {
        query->status = QD_AMQP_BAD_REQUEST;
        query->status.description = "No name or identity provided";
        qd_log(core->agent_log, QD_LOG_ERROR, "Error performing READ of %s: %s", CONFIG_EXCHANGE_TYPE, query->status.description);
    }
    else {
        ex = find_exchange(core, identity, name);
        if (!ex) {
            query->status = QD_AMQP_NOT_FOUND;
        }
        else {
            if (query->body) write_config_exchange_map(ex, query->body);
            query->status = QD_AMQP_OK;
        }
    }

    qdr_agent_enqueue_response_CT(core, query);
}


static void write_config_exchange_list(qdr_exchange_t *ex,
                                       qdr_query_t    *query);

// Exchange GET first:
//
void qdra_config_exchange_get_first_CT(qdr_core_t *core, qdr_query_t *query, int offset)
{
    //
    // Queries that get this far will always succeed.
    //
    query->status = QD_AMQP_OK;

    //
    // If the offset goes beyond the set of objects, end the query now.
    //
    if (offset >= DEQ_SIZE(core->exchanges)) {
        query->more = false;
        qdr_agent_enqueue_response_CT(core, query);
        return;
    }

    //
    // Run to the object at the offset.
    //
    qdr_exchange_t *ex = DEQ_HEAD(core->exchanges);
    for (int i = 0; i < offset && ex; i++)
        ex = DEQ_NEXT(ex);
    assert(ex);

    //
    // Write the columns of the object into the response body.
    //
    if (query->body) write_config_exchange_list(ex, query);

    //
    // Advance to the next address
    //
    query->next_offset = offset + 1;
    query->more = !!DEQ_NEXT(ex);

    //
    // Enqueue the response.
    //
    qdr_agent_enqueue_response_CT(core, query);
}

// Exchange GET-NEXT
//
void qdra_config_exchange_get_next_CT(qdr_core_t *core, qdr_query_t *query)
{
    qdr_exchange_t *ex = 0;

    if (query->next_offset < DEQ_SIZE(core->exchanges)) {
        ex = DEQ_HEAD(core->exchanges);
        for (int i = 0; i < query->next_offset && ex; i++)
            ex = DEQ_NEXT(ex);
    }

    if (ex) {
        //
        // Write the columns of the addr entity into the response body.
        //
        if (query->body) write_config_exchange_list(ex, query);

        //
        // Advance to the next object
        //
        query->next_offset++;
        query->more = !!DEQ_NEXT(ex);
    } else
        query->more = false;

    //
    // Enqueue the response.
    //
    qdr_agent_enqueue_response_CT(core, query);
}


static qdr_binding_t *qdr_binding(qdr_core_t *core,
                                  qdr_exchange_t *ex,
                                  qd_iterator_t *name,
                                  qd_iterator_t *key,
                                  qd_iterator_t *next_hop);
static void write_config_binding_map(qdr_binding_t       *binding,
                                     qd_composed_field_t *body);
static key_node_t *key_node_add_binding(key_node_t *node,
                                        token_iterator_t *key,
                                        qdr_binding_t *binding);

///

// Binding CREATE
//
void qdra_config_binding_create_CT(qdr_core_t         *core,
                                   qd_iterator_t      *name,
                                   qdr_query_t        *query,
                                   qd_parsed_field_t  *in_body)
{
    qdr_binding_t *binding = NULL;
    qdr_exchange_t *ex = NULL;

    query->status = QD_AMQP_BAD_REQUEST;

    if (!qd_parse_is_map(in_body)) {
        query->status.description = "Body of request must be a map";
        goto exit;
    }

    qd_parsed_field_t *exchange_field = qd_parse_value_by_key(in_body,
                                                              qdr_config_binding_columns[QDR_CONFIG_BINDING_EXCHANGE]);
    if (!exchange_field) {
        query->status.description = "Binding configuration requires an exchange";
        goto exit;
    }

    // lookup the exchange by its name:
    ex = find_exchange(core, NULL, qd_parse_raw(exchange_field));
    if (!ex) {
        query->status.description = "Binding's exchange does not exist";
        goto exit;
    }

    qd_parsed_field_t *key_field = qd_parse_value_by_key(in_body,
                                                         qdr_config_binding_columns[QDR_CONFIG_BINDING_KEY]);
    if (!key_field) {
        query->status.description = "A binding key is required";
        goto exit;
    }

    qd_parsed_field_t *next_hop_field = qd_parse_value_by_key(in_body,
                                                              qdr_config_binding_columns[QDR_CONFIG_BINDING_NEXTHOP]);
    if (!next_hop_field) {
        query->status.description = "No next hop specified";
        goto exit;
    }
    qd_parsed_field_t *name_field = qd_parse_value_by_key(in_body,
                                                         qdr_config_binding_columns[QDR_CONFIG_BINDING_NAME]);

    binding = qdr_binding(core, ex, (name_field) ? qd_parse_raw(name_field) : NULL,
                          qd_parse_raw(key_field), qd_parse_raw(next_hop_field));
    token_iterator_t key;
    token_iterator_init(&key, (char *)binding->key_str);
    key_node_add_binding(ex->parse_tree, &key, binding);
    query->status = QD_AMQP_CREATED;

    if (query->body) {
        write_config_binding_map(binding, query->body);
    }

 exit:

    if (query->status.status == QD_AMQP_CREATED.status) {
        qd_log(core->agent_log, QD_LOG_DEBUG,
               "Exchange %s Binding %s -> %s CREATED (id=%"PRIu64")", ex->name_str,
               binding->key_str, binding->next_hop_str, binding->identity);

    } else {
        qd_log(core->agent_log, QD_LOG_ERROR,
               "Error performing CREATE of %s: %s", CONFIG_BINDING_TYPE, query->status.description);
        // return a NULL body:
        if (query->body) qd_compose_insert_null(query->body);
    }

    if (query->body) {
        qdr_agent_enqueue_response_CT(core, query);
    } else
        // no body == create from internal config parser
        qdr_query_free(query);
}


static qdr_binding_t *find_binding(qdr_core_t *core, qd_iterator_t *identity, qd_iterator_t *name);
static bool key_node_remove_binding(key_node_t *node,
                                    token_iterator_t *key,
                                    qdr_binding_t *binding);
static void qdr_binding_free(qdr_binding_t *b);

// Binding DELETE
//
void qdra_config_binding_delete_CT(qdr_core_t    *core,
                                   qdr_query_t   *query,
                                   qd_iterator_t *name,
                                   qd_iterator_t *identity)
{
    if (!identity && !name) {
        query->status = QD_AMQP_BAD_REQUEST;
        query->status.description = "No binding name or identity provided";
        qd_log(core->agent_log, QD_LOG_ERROR, "Error performing DELETE of %s: %s", CONFIG_BINDING_TYPE, query->status.description);
    } else {
        qdr_binding_t *binding = find_binding(core, identity, name);
        if (!binding) {
            query->status = QD_AMQP_NOT_FOUND;
        } else {
            token_iterator_t key;
            token_iterator_init(&key, (char *)binding->key_str);
            key_node_remove_binding(binding->exchange->parse_tree, &key, binding);
            qdr_binding_free(binding);
            query->status = QD_AMQP_NO_CONTENT;
        }
    }

    qdr_agent_enqueue_response_CT(core, query);
}


// Binding GET
//
void qdra_config_binding_get_CT(qdr_core_t    *core,
                                qd_iterator_t *name,
                                qd_iterator_t *identity,
                                qdr_query_t   *query,
                                const char    *columns[])
{
    if (!identity && !name) {
        query->status = QD_AMQP_BAD_REQUEST;
        query->status.description = "No binding name or identity provided";
        qd_log(core->agent_log, QD_LOG_ERROR, "Error performing READ of %s: %s", CONFIG_BINDING_TYPE, query->status.description);
    } else {
        qdr_binding_t *binding = find_binding(core, identity, name);
        if (binding == 0) {
            query->status = QD_AMQP_NOT_FOUND;
        }
        else {
            if (query->body) write_config_binding_map(binding, query->body);
            query->status = QD_AMQP_OK;
        }
    }

    qdr_agent_enqueue_response_CT(core, query);
}


static void write_config_binding_list(qdr_binding_t *binding,
                                      qdr_query_t   *query);
static qdr_binding_t *get_binding_at_index(qdr_core_t *core, int index);

// Binding GET first
//
void qdra_config_binding_get_first_CT(qdr_core_t *core, qdr_query_t *query, int offset)
{
    query->status = QD_AMQP_OK;

    qdr_binding_t *binding = get_binding_at_index(core, offset);
    if (!binding) {
        query->more = false;
        qdr_agent_enqueue_response_CT(core, query);
        return;
    }

    if (query->body) write_config_binding_list(binding, query);
    query->next_offset = offset + 1;
    query->more = !!(DEQ_NEXT_N(exchange_list, binding) || DEQ_NEXT(binding->exchange));
    qdr_agent_enqueue_response_CT(core, query);
}


// Binding GET-NEXT
//
void qdra_config_binding_get_next_CT(qdr_core_t *core, qdr_query_t *query)
{
    qdr_binding_t *binding = get_binding_at_index(core, query->next_offset);
    if (binding) {
        if (query->body) write_config_binding_list(binding, query);
        query->next_offset++;
        query->more = !!(DEQ_NEXT_N(exchange_list, binding) || DEQ_NEXT(binding->exchange));
    } else
        query->more = false;
    qdr_agent_enqueue_response_CT(core, query);
}


// create a new iterator with its own copy of the data. caller must free both the
// iterator and the returned data buffer.  Used for retaining data extracted
// from query messages.
static qd_iterator_t *clone_iterator(qd_iterator_t *iter, unsigned char **data)
{
    *data = qd_iterator_copy(iter);
    qd_iterator_reset(iter);
    return qd_iterator_string((char *)*data, qd_iterator_get_view(iter));
}


static key_node_t *new_key_node(const token_t *t);

// Exchange constructor/destructor

static qdr_exchange_t *qdr_exchange(qdr_core_t *core,
                                    qd_iterator_t *name,
                                    qd_iterator_t *alternate,
                                    qdr_address_t *address)
{
    qdr_exchange_t *ex = new_qdr_exchange_t();
    if (ex) {
        assert(name);
        DEQ_ITEM_INIT(ex);
        DEQ_INSERT_TAIL(ex->core->exchanges, ex);
        ex->identity = qdr_identifier(core);
        ex->name = clone_iterator(name, &ex->name_str);
        ex->alternate = clone_iterator(alternate, &ex->alternate_str);
        ex->qdr_addr = address;   // reference
        DEQ_INIT(ex->bindings);
        ex->parse_tree = new_key_node(NULL);
        ex->scratch_len = 128;
        ex->scratch = malloc(ex->scratch_len);
        ex->core = core;

        ex->msgs_received = 0;
        ex->msgs_dropped = 0;
        ex->msgs_routed = 0;
    }

    return ex;
}

static void qdr_exchange_free(qdr_exchange_t *ex)
{
    DEQ_REMOVE(ex->core->exchanges, ex);
    if (ex->name) qd_iterator_free(ex->name);
    if (ex->name_str) free(ex->name_str);
    if (ex->alternate) qd_iterator_free(ex->alternate);
    if (ex->alternate_str) free(ex->alternate_str);
    if (ex->scratch) free(ex->scratch);

    while (DEQ_SIZE(ex->bindings) > 0) {
        qdr_binding_t *binding = DEQ_HEAD(ex->bindings);
        token_iterator_t key;
        token_iterator_init(&key, (const char *)binding->key_str);
        key_node_remove_binding(ex->parse_tree,
                                &key,
                                binding);
        DEQ_REMOVE_N(exchange_list, ex->bindings, binding);
        qdr_binding_free(binding);
    }
    free_qdr_exchange_t(ex);
}

// Binding constructor/destructor

static char *normalize_pattern(qd_iterator_t *key);


static qdr_binding_t *qdr_binding(qdr_core_t *core,
                                  qdr_exchange_t *ex,
                                  qd_iterator_t *name,
                                  qd_iterator_t *key,
                                  qd_iterator_t *next_hop)
{
    qdr_binding_t *b = new_qdr_binding_t();
    if (b) {
        DEQ_ITEM_INIT_N(exchange_list, b);
        DEQ_ITEM_INIT_N(node_list, b);

        b->key_str = (unsigned char *)normalize_pattern(key);
        b->key = qd_iterator_string((char *)b->key_str, ITER_VIEW_ALL);
        b->identity = qdr_identifier(core);
        b->name = clone_iterator(name, &b->name_str);
        b->next_hop = clone_iterator(next_hop, &b->next_hop_str);

        b->exchange = ex;
        DEQ_INSERT_TAIL_N(exchange_list, ex->bindings, b);
    }
    return b;
}


static void qdr_binding_free(qdr_binding_t *b)
{
    if (b->name) qd_iterator_free(b->name);
    if (b->name_str) free(b->name_str);
    if (b->key) qd_iterator_free(b->key);
    if (b->key_str) free(b->key_str);
    if (b->next_hop) qd_iterator_free(b->next_hop);
    if (b->next_hop_str) free(b->next_hop_str);
    DEQ_REMOVE_N(exchange_list, b->exchange->bindings, b);
    free_qdr_binding_t(b);
}

// lookup

static qdr_exchange_t *find_exchange(qdr_core_t *core, qd_iterator_t *identity, qd_iterator_t *name)
{
    qdr_exchange_t *ex = 0;
    for (ex = DEQ_HEAD(core->exchanges); ex; ex = DEQ_NEXT(ex)) {
        if (identity) {  // ignore name - identity takes precedence
            // Convert the identity for comparison
            char id[100];
            snprintf(id, 100, "%"PRId64, ex->identity);
            if (qd_iterator_equal(identity, (const unsigned char*) id))
                break;
        } else if (name && qd_iterator_equal(name, ex->name_str))
            break;
    }
    return ex;
}


static qdr_binding_t *find_binding(qdr_core_t *core, qd_iterator_t *identity, qd_iterator_t *name)
{
    for (qdr_exchange_t *ex = DEQ_HEAD(core->exchanges); ex; ex = DEQ_NEXT(ex)) {
        for (qdr_binding_t *binding = DEQ_HEAD(ex->bindings); binding; binding = DEQ_NEXT_N(exchange_list, binding)) {
            if (identity) {  // ignore name - identity takes precedence
                // Convert the identity for comparison
                char id[100];
                snprintf(id, 100, "%"PRId64, binding->identity);
                if (qd_iterator_equal(identity, (const unsigned char*) id))
                    return binding;
            } else if (name && qd_iterator_equal(name, binding->name_str))
                return binding;
        }
    }
    return NULL;
}

// Management helper routines

static void exchange_insert_column(qdr_exchange_t *ex, int col, qd_composed_field_t *body)
{
    switch(col) {
    case QDR_CONFIG_EXCHANGE_NAME:
        qd_compose_insert_string(body, (const char *)ex->name_str);
        break;

    case QDR_CONFIG_EXCHANGE_IDENTITY: {
        char id_str[100];
        snprintf(id_str, 100, "%"PRId64, ex->identity);
        qd_compose_insert_string(body, id_str);
        break;
    }

    case QDR_CONFIG_EXCHANGE_TYPE:
        qd_compose_insert_string(body, CONFIG_EXCHANGE_TYPE);
        break;

    case QDR_CONFIG_EXCHANGE_ALTERNATE:
        if (ex->alternate_str)
            qd_compose_insert_string(body, (const char *)ex->alternate_str);
        else
            qd_compose_insert_null(body);
        break;

    case QDR_CONFIG_EXCHANGE_BINDING_COUNT:
        qd_compose_insert_uint(body, DEQ_SIZE(ex->bindings));
        break;

    case QDR_CONFIG_EXCHANGE_RECEIVED:
        qd_compose_insert_ulong(body, ex->msgs_received);
        break;

    case QDR_CONFIG_EXCHANGE_DROPPED:
        qd_compose_insert_ulong(body, ex->msgs_dropped);
        break;

    case QDR_CONFIG_EXCHANGE_FORWARDED:
        qd_compose_insert_ulong(body, ex->msgs_routed);
        break;
    }
}


static void binding_insert_column(qdr_binding_t *b, int col, qd_composed_field_t *body)
{
    switch(col) {
    case QDR_CONFIG_BINDING_NAME:
        if (b->name_str)
            qd_compose_insert_string(body, (char *)b->name_str);
        else
            qd_compose_insert_null(body);
        break;

    case QDR_CONFIG_BINDING_IDENTITY: {
        char id_str[100];
        snprintf(id_str, 100, "%"PRIu64, b->identity);
        qd_compose_insert_string(body, id_str);
        break;
    }

    case QDR_CONFIG_BINDING_TYPE:
        qd_compose_insert_string(body, CONFIG_BINDING_TYPE);
        break;

    case QDR_CONFIG_BINDING_KEY:
        qd_compose_insert_string(body, (char *)b->key_str);
        break;

    case QDR_CONFIG_BINDING_NEXTHOP:
        qd_compose_insert_string(body, (char *)b->next_hop_str);
        break;

    case QDR_CONFIG_BINDING_MATCHED:
        qd_compose_insert_ulong(body, b->msgs_matched);
        break;
    }
}


static void write_config_exchange_map(qdr_exchange_t      *ex,
                                      qd_composed_field_t *body)
{
    qd_compose_start_map(body);

    for(int i = 0; i < QDR_CONFIG_EXCHANGE_COLUMN_COUNT; i++) {
        qd_compose_insert_string(body, qdr_config_exchange_columns[i]);
        exchange_insert_column(ex, i, body);
    }

    qd_compose_end_map(body);
}


static void write_config_exchange_list(qdr_exchange_t *ex,
                                       qdr_query_t    *query)
{
    qd_compose_start_list(query->body);

    int i = 0;
    while (query->columns[i] >= 0) {
        exchange_insert_column(ex, query->columns[i], query->body);
        i++;
    }

    qd_compose_end_list(query->body);
}


static void write_config_binding_map(qdr_binding_t       *binding,
                                     qd_composed_field_t *body)
{
    qd_compose_start_map(body);

    for(int i = 0; i < QDR_CONFIG_BINDING_COLUMN_COUNT; i++) {
        qd_compose_insert_string(body, qdr_config_binding_columns[i]);
        binding_insert_column(binding, i, body);
    }

    qd_compose_end_map(body);
}

static void write_config_binding_list(qdr_binding_t *binding,
                                      qdr_query_t   *query)
{
    qd_compose_start_list(query->body);

    int i = 0;
    while (query->columns[i] >= 0) {
        binding_insert_column(binding, query->columns[i], query->body);
        i++;
    }

    qd_compose_end_list(query->body);
}


static qdr_binding_t *get_binding_at_index(qdr_core_t *core, int index)
{
    qdr_binding_t *binding = 0;

    // skip to the proper exchange:
    qdr_exchange_t *ex = DEQ_HEAD(core->exchanges);
    while (ex && index >= DEQ_SIZE(ex->bindings)) {
        index -= DEQ_SIZE(ex->bindings);
        ex = DEQ_NEXT(ex);
    }

    if (ex) {
        // then to the target binding
        binding = DEQ_HEAD(ex->bindings);
        while (index--) {
            binding = DEQ_NEXT_N(exchange_list, binding);
        }
        assert(binding);
    }
    return binding;
}

// TODO(kgiusti): really should create a token parsing qd_iterator_t view.
// Then all this token/token_iterator stuff can be removed...
//
// binding key token parsing
//
// The format of a binding key string is a series of tokens separated by '.'  A
// token can contain any characters but '.', '*', or '#'.
//
// Wildcard matching:
// '*' - match any single token
// '#' - match zero or more tokens
//

static const char STAR = '*';
static const char HASH = '#';
static const char TOKEN_SEP = '.';

#define TOKEN_LEN(t) ((t).end - (t).begin)
static bool token_match_str(const token_t *t, const char *str)
{
    return !strncmp(t->begin, str, TOKEN_LEN(*t));
}


static void token_iterator_init(token_iterator_t *t, const char *str)
{
    const char *tend = strchr(str, TOKEN_SEP);
    t->terminator = str + strlen(str);
    t->token.begin = str;
    t->token.end = (tend) ? tend : t->terminator;
}


static void token_iterator_next(token_iterator_t *t)
{
    if (t->token.end == t->terminator) {
        t->token.begin = t->terminator;
    } else {
        const char *tend;
        t->token.begin = t->token.end + 1;
        tend = strchr(t->token.begin, TOKEN_SEP);
        t->token.end = (tend) ? tend : t->terminator;
    }
}


static bool token_iterator_done(const token_iterator_t *t)
{
    return t->token.begin == t->terminator;
}


static void token_iterator_pop(token_iterator_t *t, token_t *head)
{
    if (head) *head = t->token;
    token_iterator_next(t);
}


// True if token matches the given char value
static bool token_iterator_match_char(const token_iterator_t *t,
                                      const char c)
{
    return TOKEN_LEN(t->token) == 1 && *t->token.begin == c;
}


// Binding database (per exchange):
// The dotted form of a binding key is broken up and stored in a directed tree graph.
// Common binding prefix are merged.  This allows the route match alogrithm to quickly
// isolate those sub-trees that match a given routingKey.
// For example, given the routes:
//     a.b.c.<...>
//     a.b.d.<...>
//     a.x.y.<...>
// The resulting tree would be:
//    a-->b-->c-->...
//    |   +-->d-->...
//    +-->x-->y-->...
//

// Optimize the key pattern match code by performing the following
// transformations to the pattern:
// #.* ---> *.#
// #.# ---> #
//
static char *normalize_pattern(qd_iterator_t *key)
{
    token_iterator_t t;
    char *p = (char *)qd_iterator_copy(key);
    if (p == NULL) return NULL;

    token_iterator_init(&t, p);
    while (!token_iterator_done(&t)) {
        if (token_iterator_match_char(&t, HASH)) {
            token_t last_token;
            token_iterator_pop(&t, &last_token);
            if (token_iterator_done(&t)) break;
            if (token_iterator_match_char(&t, HASH)) {  // #.# --> #
                char *src = (char *)t.token.begin;
                char *dest = (char *)last_token.begin;
                // note: overlapping strings, can't strcpy
                while (*src)
                    *dest++ = *src++;
                *dest = (char)0;
                t.terminator = dest;
                t.token = last_token;
            } else if (token_iterator_match_char(&t, STAR)) { // #.* --> *.#
                *(char *)t.token.begin = HASH;
                *(char *)last_token.begin = STAR;
            } else {
                token_iterator_next(&t);
            }
        } else {
            token_iterator_next(&t);
        }
    }
    return p;
}


// Parse tree for resolving subject field pattern matching to the appropriate
// set of bindings
DEQ_DECLARE(key_node_t, key_node_list_t);
typedef struct key_node {
    DEQ_LINKS(key_node_t);      // siblings
    char *token;                // portion of pattern represented by this node
    bool is_star;
    bool is_hash;
    char *route_pattern;        // entire normalized pattern matching this node
    key_node_list_t  children;
    struct key_node  *star_child;
    struct key_node  *hash_child;
    qdr_binding_list_t next_hops;  // for a match against this node
} key_node_t;
ALLOC_DECLARE(key_node_t);
ALLOC_DEFINE(key_node_t);

static key_node_t *new_key_node(const token_t *t)
{
    key_node_t *n = new_key_node_t();
    if (n) {
        DEQ_ITEM_INIT(n);
        DEQ_INIT(n->children);
        DEQ_INIT(n->next_hops);
        n->route_pattern = NULL;
        n->star_child = n->hash_child = NULL;

        if (t) {
            const size_t tlen = TOKEN_LEN(*t);
            n->token = malloc(tlen + 1);
            strncpy(n->token, t->begin, tlen);
            n->token[tlen] = '0';
            n->is_star = (tlen == 1 && *t->begin == STAR);
            n->is_hash = (tlen == 1 && *t->begin == HASH);
        } else {  // root
            n->token = NULL;
            n->is_star = n->is_hash = false;
        }
    }
    return n;
}

static void free_key_node(key_node_t *n)
{
    // TODO: cleanup children????
    if (n->token) free(n->token);
    if (n->route_pattern) free(n->route_pattern);
    free_key_node_t(n);
}

// return count of child nodes
static int key_node_child_count(const key_node_t *n)
{
    return DEQ_SIZE(n->children)
        + n->star_child ? 1 : 0
        + n->hash_child ? 1 : 0;
}

// find immediate child node matching token
static key_node_t *key_node_find_child(const key_node_t *node, const token_t *token)
{
    key_node_t *child = DEQ_HEAD(node->children);
    while (child && !token_match_str(token, child->token))
        child = DEQ_NEXT(child);
    return child;
}

// Add a new binding to the tree.  Return the address of the
// node which holds the new binding.
static key_node_t *key_node_add_binding(key_node_t *node,
                                        token_iterator_t *key,
                                        qdr_binding_t *binding)
{
    if (token_iterator_done(key)) {
        // this node's binding
        if (!node->route_pattern) {
            node->route_pattern = strdup((char *)binding->key_str);
        }
        assert(strcmp(node->route_pattern, binding->key_str) == 0);
        DEQ_INSERT_TAIL_N(node_list, node->next_hops, binding);
        return node;
    }

    if (token_iterator_match_char(key, STAR)) {
        if (!node->star_child) {
            node->star_child = new_key_node(&key->token);
        }
        token_iterator_next(key);
        return key_node_add_binding(node->star_child, key, binding);
    } else if (token_iterator_match_char(key, HASH)) {
        if (!node->hash_child) {
            node->hash_child = new_key_node(&key->token);
        }
        token_iterator_next(key);
        return key_node_add_binding(node->hash_child, key, binding);
    } else {
        // check the children nodes
        token_t current;
        token_iterator_pop(key, &current);

        key_node_t *child = key_node_find_child(node, &current);
        if (child) {
            return key_node_add_binding(child, key, binding);
        } else {
            child = new_key_node(&current);
            DEQ_INSERT_TAIL(node->children, child);
            return key_node_add_binding(child, key, binding);
        }
    }
}

// remove binding from the subtree.  return true if this node may be deleted
static bool key_node_remove_binding(key_node_t *node,
                                    token_iterator_t *key,
                                    qdr_binding_t *binding)
{
    if (token_iterator_done(key)) {
        // this node's binding
        assert(strcmp(node->route_pattern, binding->key_str) == 0);
        qdr_binding_t *ptr = DEQ_HEAD(node->next_hops);
        DEQ_FIND_N(node_list, ptr, (ptr == binding));
        if (ptr) {
            DEQ_REMOVE_N(node_list, node->next_hops, ptr);
        }
    } else if (token_iterator_match_char(key, STAR)) {
        assert(node->star_child);
        token_iterator_next(key);
        if (key_node_remove_binding(node->star_child, key, binding)) {
            free_key_node(node->star_child);
            node->star_child = NULL;
        }
    } else if (token_iterator_match_char(key, HASH)) {
        assert(node->hash_child);
        token_iterator_next(key);
        if (key_node_remove_binding(node->hash_child, key, binding)) {
            free_key_node(node->hash_child);
            node->hash_child = NULL;
        }
    } else {
        token_t current;
        token_iterator_pop(key, &current);
        key_node_t *child = key_node_find_child(node, &current);
        if (child) {
            if (key_node_remove_binding(child, key, binding)) {
                DEQ_REMOVE(node->children, child);
                free_key_node(child);
            }
        }
    }
    return DEQ_SIZE(node->next_hops) == 0 && key_node_child_count(node) == 0;
}

// Forward a copy of the message to the to_addr address
static int send_message(qdr_core_t     *core,
                        qd_iterator_t  *to_addr,
                        const char     *to_addr_str,
                        qd_message_t   *msg,
                        qdr_delivery_t *in_delivery,
                        bool            exclude_inprocess,
                        bool            control)
{
    int count = 0;
    qdr_address_t *addr;

    qd_hash_retrieve(core->addr_hash, to_addr, (void **)&addr);
    qd_iterator_reset(to_addr);
    if (addr && addr->treatment != QD_TREATMENT_EXCHANGE /* avoid infinite
                                                            recursion */) {
        qd_message_t *copy = qd_message_copy(msg);
        // set "to" field override in annotations
        qd_composed_field_t *to_field = qd_compose_subfield(0);
        qd_compose_insert_string(to_field, to_addr_str);
        qd_message_set_to_override_annotation(copy, to_field);  // frees to_field
        count = qdr_forward_message_CT(core, addr, copy, in_delivery, exclude_inprocess, control);
        qd_message_free(copy);
    }
    return count;
}

// forward the msg to all next_hops in the binding list
static int forward_message(qdr_binding_list_t *bindings,
                           qdr_exchange_t     *exchange,
                           qd_message_t       *msg,
                           qdr_delivery_t     *in_delivery,
                           bool                exclude_inprocess,
                           bool                control)
{
    int count = 0;
    qdr_core_t *core = exchange->core;

    qdr_binding_t *binding = DEQ_HEAD(*bindings);
    while (binding) {
        ++binding->msgs_matched;
        count += send_message(core, binding->next_hop, (char *)binding->next_hop_str,
                              msg, in_delivery, exclude_inprocess, control);
        binding = DEQ_NEXT_N(node_list, binding);
    }
    return count;
}



// forward to the sub-trees of this node
static int key_node_forward_children(key_node_t *node,
                                  qdr_exchange_t *ex,
                                  token_iterator_t *subject,
                                  qd_message_t    *msg,
                                  qdr_delivery_t    *in_delivery,
                                  bool              exclude_inprocess,
                                  bool              control)
{
    int count = 0;

    // always try glob - it can match empty keys
    if (node->hash_child) {
        token_iterator_t tmp = *subject;
        count += key_node_forward(node->hash_child, ex, &tmp, msg, in_delivery, exclude_inprocess, control);
    }

    if (!token_iterator_done(subject)) {

        if (node->star_child) {
            token_iterator_t tmp = *subject;
            count += key_node_forward(node->star_child, ex, &tmp, msg, in_delivery, exclude_inprocess, control);
        }

        if (DEQ_SIZE(node->children) > 0) {
            token_iterator_t tmp = *subject;
            token_t child_token;
            token_iterator_pop(&tmp, &child_token);

            key_node_t *child = key_node_find_child(node, &child_token);
            if (child) {
                count += key_node_forward(child, ex, &tmp, msg, in_delivery, exclude_inprocess, control);
            }
        }
    }

    return count;
}


// forward based on the current token
static int key_node_forward_token(key_node_t       *node,
                                  qdr_exchange_t   *ex,
                                  token_iterator_t *subject,
                                  qd_message_t    *msg,
                                  qdr_delivery_t    *in_delivery,
                                  bool              exclude_inprocess,
                                  bool              control)
{
    int count = 0;
    if (token_iterator_done(subject)) {
        // exact match this node:  forward
        count = forward_message(&node->next_hops, ex, msg, in_delivery, exclude_inprocess, control);
    }

    // continue to lower sub-trees, even if empty.
    return count + key_node_forward_children(node, ex, subject, msg, in_delivery, exclude_inprocess, control);
}

static int key_node_forward_star(key_node_t *node,
                                  qdr_exchange_t *ex,
                                  token_iterator_t *subject,
                                  qd_message_t    *msg,
                                  qdr_delivery_t    *in_delivery,
                                  bool              exclude_inprocess,
                                  bool              control)
{
    int count = 0;

    // must match exactly one token:
    if (token_iterator_done(subject))
        return 0;

    // pop the topmost token (matched)
    token_iterator_next(subject);

    if (token_iterator_done(subject)) {
        // exact match this node:  forward
        count = forward_message(&node->next_hops, ex, msg, in_delivery, exclude_inprocess, control);
    }

    // continue to lower sub-trees
    return count + key_node_forward_children(node, ex, subject, msg, in_delivery, exclude_inprocess, control);
}

// current node is hash, use hash forwarding
static int key_node_forward_hash(key_node_t *node,
                                  qdr_exchange_t *ex,
                                  token_iterator_t *subject,
                                  qd_message_t    *msg,
                                  qdr_delivery_t    *in_delivery,
                                  bool              exclude_inprocess,
                                  bool              control)
{
    int count = 0;
    // consume each token and look for a match on the
    // remaining key.
    while (!token_iterator_done(subject)) {
        count += key_node_forward_children(node, ex, subject, msg, in_delivery, exclude_inprocess, control);
        token_iterator_next(subject);
    }

    // this node matches
    count += forward_message(&node->next_hops, ex, msg, in_delivery, exclude_inprocess, control);

    return count;
}

// Forward a message via the parse_tree rooted at node
// This is the entrypoint to the subject key parsing process.
static int key_node_forward(key_node_t       *node,
                            qdr_exchange_t   *ex,
                            token_iterator_t *subject,
                            qd_message_t    *msg,
                            qdr_delivery_t    *in_delivery,
                            bool              exclude_inprocess,
                            bool              control)
{
    if (node->is_star) return key_node_forward_star(node, ex, subject, msg, in_delivery, exclude_inprocess, control);
    if (node->is_hash) return key_node_forward_hash(node, ex, subject, msg, in_delivery, exclude_inprocess, control);
    return key_node_forward_token(node, ex, subject, msg, in_delivery, exclude_inprocess, control);
}




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

#include "http_common.h"

#include <proton/listener.h>

#include <stdio.h>

ALLOC_DECLARE(qd_http_listener_t);
ALLOC_DEFINE(qd_http_listener_t);
ALLOC_DECLARE(qd_http_connector_t);
ALLOC_DEFINE(qd_http_connector_t);


static qd_error_t load_bridge_config(qd_dispatch_t *qd, qd_http_bridge_config_t *config, qd_entity_t* entity)
{
    char *version_str = 0;
    char *aggregation_str = 0;

    qd_error_clear();
    ZERO(config);

#define CHECK() if (qd_error_code()) goto error
    config->name    = qd_entity_get_string(entity, "name");            CHECK();
    config->host    = qd_entity_get_string(entity, "host");            CHECK();
    config->port    = qd_entity_get_string(entity, "port");            CHECK();
    config->address = qd_entity_get_string(entity, "address");         CHECK();
    config->site    = qd_entity_opt_string(entity, "siteId", 0);       CHECK();
    version_str     = qd_entity_get_string(entity, "protocolVersion");  CHECK();
    aggregation_str = qd_entity_opt_string(entity, "aggregation", 0);  CHECK();
    config->event_channel = qd_entity_opt_bool(entity, "eventChannel", false); CHECK();
    config->host_override  = qd_entity_opt_string(entity, "hostOverride", 0);   CHECK();

    if (strcmp(version_str, "HTTP2") == 0) {
        config->version = VERSION_HTTP2;
    } else {
        config->version = VERSION_HTTP1;
    }
    free(version_str);
    version_str = 0;

    if (aggregation_str && strcmp(aggregation_str, "json") == 0) {
        config->aggregation = QD_AGGREGATION_JSON;
    } else if (aggregation_str && strcmp(aggregation_str, "multipart") == 0) {
        config->aggregation = QD_AGGREGATION_MULTIPART;
    } else {
        config->aggregation = QD_AGGREGATION_NONE;
    }
    free(aggregation_str);
    aggregation_str = 0;

    int hplen = strlen(config->host) + strlen(config->port) + 2;
    config->host_port = malloc(hplen);
    snprintf(config->host_port, hplen, "%s:%s", config->host, config->port);

    return QD_ERROR_NONE;

error:
    qd_http_free_bridge_config(config);
    free(version_str);
    return qd_error_code();
}


void qd_http_free_bridge_config(qd_http_bridge_config_t *config)
{
    if (!config) {
        return;
    }
    free(config->host);
    free(config->port);
    free(config->name);
    free(config->address);
    free(config->site);
    free(config->host_override);
    free(config->host_port);
}


//
// HTTP Listener Management (HttpListenerEntity)
//


qd_http_listener_t *qd_dispatch_configure_http_listener(qd_dispatch_t *qd, qd_entity_t *entity)
{
    qd_http_listener_t *listener = 0;
    qd_http_bridge_config_t config;

    if (load_bridge_config(qd, &config, entity) != QD_ERROR_NONE) {
        qd_log(qd_log_source(QD_HTTP_LOG_SOURCE), QD_LOG_ERROR,
               "Unable to create http listener: %s", qd_error_message());
        return 0;
    }

    switch (config.version) {
    case VERSION_HTTP1:
        listener = qd_http1_configure_listener(qd, &config, entity);
        break;
    case VERSION_HTTP2:
        listener = qd_http2_configure_listener(qd, &config, entity);
        break;
    }

    if (!listener)
        qd_http_free_bridge_config(&config);

    return listener;
}


void qd_dispatch_delete_http_listener(qd_dispatch_t *qd, void *impl)
{
    qd_http_listener_t *listener = (qd_http_listener_t*) impl;
    if (listener) {
        switch (listener->config.version) {
        case VERSION_HTTP1:
            qd_http1_delete_listener(qd, listener);
            break;
        case VERSION_HTTP2:
            qd_http2_delete_listener(qd, listener);
            break;
        }
    }
}


qd_error_t qd_entity_refresh_httpListener(qd_entity_t* entity, void *impl)
{
    return QD_ERROR_NONE;
}


//
// HTTP Connector Management (HttpConnectorEntity)
//


qd_http_connector_t *qd_dispatch_configure_http_connector(qd_dispatch_t *qd, qd_entity_t *entity)
{
    qd_http_connector_t *conn = 0;
    qd_http_bridge_config_t config;

    if (load_bridge_config(qd, &config, entity) != QD_ERROR_NONE) {
        qd_log(qd_log_source(QD_HTTP_LOG_SOURCE), QD_LOG_ERROR,
               "Unable to create http connector: %s", qd_error_message());
        return 0;
    }

    switch (config.version) {
    case VERSION_HTTP1:
        conn = qd_http1_configure_connector(qd, &config, entity);
        break;
    case VERSION_HTTP2:
        conn = qd_http2_configure_connector(qd, &config, entity);
        break;
    }

    if (!conn)
        qd_http_free_bridge_config(&config);

    return conn;
}


void qd_dispatch_delete_http_connector(qd_dispatch_t *qd, void *impl)
{
    qd_http_connector_t *conn = (qd_http_connector_t*) impl;

    if (conn) {
        switch (conn->config.version) {
        case VERSION_HTTP1:
            qd_http1_delete_connector(qd, conn);
            break;
        case VERSION_HTTP2:
            qd_http2_delete_connector(qd, conn);
            break;
        }
    }
}

qd_error_t qd_entity_refresh_httpConnector(qd_entity_t* entity, void *impl)
{
    return QD_ERROR_NONE;
}

//
// qd_http_listener_t constructor
//

qd_http_listener_t *qd_http_listener(qd_server_t *server, qd_server_event_handler_t handler)
{
    qd_http_listener_t *li = new_qd_http_listener_t();
    if (!li)
        return 0;
    ZERO(li);

    li->pn_listener = pn_listener();
    if (!li->pn_listener) {
        free_qd_http_listener_t(li);
        return 0;
    }

    sys_atomic_init(&li->ref_count, 1);
    li->server = server;
    li->context.context = li;
    li->context.handler = handler;
    pn_listener_set_context(li->pn_listener, &li->context);

    return li;
}

void qd_http_listener_decref(qd_http_listener_t* li)
{
    if (li && sys_atomic_dec(&li->ref_count) == 1) {
        qd_http_free_bridge_config(&li->config);
        free_qd_http_listener_t(li);
    }
}

//
// qd_http_connector_t constructor
//

qd_http_connector_t *qd_http_connector(qd_server_t *server)
{
    qd_http_connector_t *c = new_qd_http_connector_t();
    if (!c) return 0;
    ZERO(c);
    sys_atomic_init(&c->ref_count, 1);
    c->server      = server;
    return c;
}

void qd_http_connector_decref(qd_http_connector_t* c)
{
    if (c && sys_atomic_dec(&c->ref_count) == 1) {
        qd_http_free_bridge_config(&c->config);
        free_qd_http_connector_t(c);
    }
}


typedef struct qdr_http_method_status_t  qdr_http_method_status_t;

struct qdr_http_method_status_t {
    DEQ_LINKS(qdr_http_method_status_t);

    char     *key;
    uint64_t requests;
};
DEQ_DECLARE(qdr_http_method_status_t, qdr_http_method_status_list_t);

typedef struct qdr_http_request_info_t  qdr_http_request_info_t;

struct qdr_http_request_info_t {
    DEQ_LINKS(qdr_http_request_info_t);

    char     *key;
    char     *address;
    char     *host;
    char     *site;
    bool      ingress;
    uint64_t  requests;
    uint64_t  bytes_in;
    uint64_t  bytes_out;
    uint64_t  max_latency;
    qdr_http_method_status_list_t detail;
};
DEQ_DECLARE(qdr_http_request_info_t, qdr_http_request_info_list_t);

#define QDR_HTTP_REQUEST_INFO_NAME                   0
#define QDR_HTTP_REQUEST_INFO_IDENTITY               1
#define QDR_HTTP_REQUEST_INFO_ADDRESS                2
#define QDR_HTTP_REQUEST_INFO_HOST                   3
#define QDR_HTTP_REQUEST_INFO_SITE                   4
#define QDR_HTTP_REQUEST_INFO_DIRECTION              5
#define QDR_HTTP_REQUEST_INFO_REQUESTS               6
#define QDR_HTTP_REQUEST_INFO_BYTES_IN               7
#define QDR_HTTP_REQUEST_INFO_BYTES_OUT              8
#define QDR_HTTP_REQUEST_INFO_MAX_LATENCY            9
#define QDR_HTTP_REQUEST_INFO_DETAIL                10


const char * const QDR_HTTP_REQUEST_INFO_DIRECTION_IN  = "in";
const char * const QDR_HTTP_REQUEST_INFO_DIRECTION_OUT = "out";

const char *qdr_http_request_info_columns[] =
    {"name",
     "identity",
     "address",
     "host",
     "site",
     "direction",
     "requests",
     "bytesIn",
     "bytesOut",
     "maxLatency",
     "details",
     0};

const char *HTTP_REQUEST_INFO_TYPE = "org.apache.qpid.dispatch.httpRequestInfo";

typedef struct {
    qdr_http_request_info_list_t records;
} http_request_info_records_t;

static http_request_info_records_t* request_info = 0;

static http_request_info_records_t *_get_request_info()
{
    if (!request_info) {
        request_info = NEW(http_request_info_records_t);
        DEQ_INIT(request_info->records);
    }
    return request_info;
}

static void insert_column(qdr_core_t *core, qdr_http_request_info_t *record, int col, qd_composed_field_t *body)
{
    qd_log(qd_log_source(QD_HTTP_LOG_SOURCE), QD_LOG_DEBUG, "Insert column %i for %p", col, (void*) record);

    if (!record)
        return;

    switch(col) {
    case QDR_HTTP_REQUEST_INFO_NAME:
        qd_compose_insert_string(body, record->key);
        break;

    case QDR_HTTP_REQUEST_INFO_IDENTITY: {
        qd_compose_insert_string(body, record->key);
        break;
    }

    case QDR_HTTP_REQUEST_INFO_ADDRESS:
        qd_compose_insert_string(body, record->address);
        break;

    case QDR_HTTP_REQUEST_INFO_HOST:
        qd_compose_insert_string(body, record->host);
        break;

    case QDR_HTTP_REQUEST_INFO_SITE:
        qd_compose_insert_string(body, record->site);
        break;

    case QDR_HTTP_REQUEST_INFO_DIRECTION:
        if (record->ingress)
            qd_compose_insert_string(body, QDR_HTTP_REQUEST_INFO_DIRECTION_IN);
        else
            qd_compose_insert_string(body, QDR_HTTP_REQUEST_INFO_DIRECTION_OUT);
        break;

    case QDR_HTTP_REQUEST_INFO_REQUESTS:
        qd_compose_insert_uint(body, record->requests);
        break;

    case QDR_HTTP_REQUEST_INFO_BYTES_IN:
        qd_compose_insert_uint(body, record->bytes_in);
        break;

    case QDR_HTTP_REQUEST_INFO_BYTES_OUT:
        qd_compose_insert_uint(body, record->bytes_out);
        break;

    case QDR_HTTP_REQUEST_INFO_MAX_LATENCY:
        qd_compose_insert_uint(body, record->max_latency);
        break;

    case QDR_HTTP_REQUEST_INFO_DETAIL:
        qd_compose_start_map(body);
        for (qdr_http_method_status_t *item = DEQ_HEAD(record->detail); item; item = DEQ_NEXT(item)) {
            qd_compose_insert_string(body, item->key);
            qd_compose_insert_int(body, item->requests);
        }
        qd_compose_end_map(body);
        break;

    }
}


static void write_list(qdr_core_t *core, qdr_query_t *query,  qdr_http_request_info_t *record)
{
    qd_composed_field_t *body = query->body;

    qd_compose_start_list(body);

    if (record) {
        int i = 0;
        while (query->columns[i] >= 0) {
            insert_column(core, record, query->columns[i], body);
            i++;
        }
    }
    qd_compose_end_list(body);
}

static void write_map(qdr_core_t           *core,
                      qdr_http_request_info_t *record,
                      qd_composed_field_t  *body,
                      const char           *qdr_connection_columns[])
{
    qd_compose_start_map(body);

    for(int i = 0; i < QDR_HTTP_REQUEST_INFO_COLUMN_COUNT; i++) {
        qd_compose_insert_string(body, qdr_connection_columns[i]);
        insert_column(core, record, i, body);
    }

    qd_compose_end_map(body);
}

static void advance(qdr_query_t *query, qdr_http_request_info_t *record)
{
    if (record) {
        query->next_offset++;
        record = DEQ_NEXT(record);
        query->more = !!record;
    }
    else {
        query->more = false;
    }
}

static qdr_http_request_info_t *find_by_identity(qdr_core_t *core, qd_iterator_t *identity)
{
    if (!identity)
        return 0;

    qdr_http_request_info_t *record = DEQ_HEAD(_get_request_info()->records);
    while (record) {
        // Convert the passed in identity to a char*
        if (qd_iterator_equal(identity, (const unsigned char*) record->key))
            break;
        record = DEQ_NEXT(record);
    }

    return record;

}

void qdra_http_request_info_get_first_CT(qdr_core_t *core, qdr_query_t *query, int offset)
{
    qd_log(qd_log_source(QD_HTTP_LOG_SOURCE), QD_LOG_DEBUG, "query for first http request info (%i)", offset);
    query->status = QD_AMQP_OK;

    if (offset >= DEQ_SIZE(_get_request_info()->records)) {
        query->more = false;
        qdr_agent_enqueue_response_CT(core, query);
        return;
    }

    qdr_http_request_info_t *record = DEQ_HEAD(_get_request_info()->records);
    for (int i = 0; i < offset && record; i++)
        record = DEQ_NEXT(record);
    assert(record);

    if (record) {
        write_list(core, query, record);
        query->next_offset = offset;
        advance(query, record);
    } else {
        query->more = false;
    }

    qdr_agent_enqueue_response_CT(core, query);
}

void qdra_http_request_info_get_next_CT(qdr_core_t *core, qdr_query_t *query)
{
    qdr_http_request_info_t *record = 0;

    if (query->next_offset < DEQ_SIZE(_get_request_info()->records)) {
        record = DEQ_HEAD(_get_request_info()->records);
        for (int i = 0; i < query->next_offset && record; i++)
            record = DEQ_NEXT(record);
    }

    if (record) {
        write_list(core, query, record);
        advance(query, record);
    } else {
        query->more = false;
    }
    qdr_agent_enqueue_response_CT(core, query);
}

void qdra_http_request_info_get_CT(qdr_core_t          *core,
                               qd_iterator_t       *name,
                               qd_iterator_t       *identity,
                               qdr_query_t         *query,
                               const char          *qdr_http_request_info_columns[])
{
    qdr_http_request_info_t *record = 0;

    if (!identity) {
        query->status = QD_AMQP_BAD_REQUEST;
        query->status.description = "Name not supported. Identity required";
        qd_log(core->agent_log, QD_LOG_ERROR, "Error performing READ of %s: %s", HTTP_REQUEST_INFO_TYPE, query->status.description);
    } else {
        record = find_by_identity(core, identity);

        if (record == 0) {
            query->status = QD_AMQP_NOT_FOUND;
        } else {
            write_map(core, record, query->body, qdr_http_request_info_columns);
            query->status = QD_AMQP_OK;
        }
    }
    qdr_agent_enqueue_response_CT(core, query);
}

static const char* UNKNOWN = "unknown";

static qdr_http_method_status_t* _new_qdr_http_method_status_t(const char *const method, int status)
{
    qdr_http_method_status_t* record = NEW(qdr_http_method_status_t);
    ZERO(record);

    if (status >= 600 || status < 100) {
        status = 500;
    }
    if (method) {
        size_t key_len = strlen(method) + 5;
        record->key = malloc(key_len);
        snprintf(record->key, key_len, "%s:%03i", method, status);
    } else {
        record->key = qd_strdup(UNKNOWN);
    }

    return record;
}

static void _free_qdr_http_method_status(qdr_http_method_status_t* record)
{
    free(record->key);
    free(record);
}

static void _free_qdr_http_request_info(qdr_http_request_info_t* record)
{
    if (record->key) {
        free(record->key);
    }
    if (record->address) {
        free(record->address);
    }
    if (record->host) {
        free(record->host);
    }
    if (record->site) {
        free(record->site);
    }
    for (qdr_http_method_status_t *item = DEQ_HEAD(record->detail); item; item = DEQ_HEAD(record->detail)) {
        _free_qdr_http_method_status(item);
    }
    free(record);
}

static void _update_http_method_status_detail(qdr_http_method_status_list_t *detail, qdr_http_method_status_t *addition)
{
    bool updated = false;
    for (qdr_http_method_status_t *item = DEQ_HEAD(*detail); item && !updated; item = DEQ_NEXT(item)) {
        if (strcmp(item->key, addition->key) == 0) {
            item->requests += addition->requests;
            _free_qdr_http_method_status(addition);
            updated = true;
        }
    }
    if (!updated) {
        DEQ_INSERT_TAIL(*detail, addition);
    }
}

static bool _update_qdr_http_request_info(qdr_http_request_info_t* record, qdr_http_request_info_t* additions)
{
    if (strcmp(record->key, additions->key) == 0) {
        record->requests += additions->requests;
        record->bytes_in += additions->bytes_in;
        record->bytes_out += additions->bytes_out;
        if (additions->max_latency > record->max_latency) {
            record->max_latency = additions->max_latency;
        }
        for (qdr_http_method_status_t *item = DEQ_HEAD(additions->detail); item; item = DEQ_HEAD(additions->detail)) {
            DEQ_REMOVE_HEAD(additions->detail);
            _update_http_method_status_detail(&record->detail, item);
        }
        return true;
    } else {
        return false;
    }
}

static void _add_http_request_info_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    qdr_http_request_info_t *update = (qdr_http_request_info_t*) action->args.general.context_1;
    bool updated = false;
    for (qdr_http_request_info_t *record = DEQ_HEAD(_get_request_info()->records); record && !updated; record = DEQ_NEXT(record)) {
        if (_update_qdr_http_request_info(record, update)) {
            updated = true;
            _free_qdr_http_request_info(update);
            qd_log(qd_log_source(QD_HTTP_LOG_SOURCE), QD_LOG_DEBUG, "Updated http request info %s", record->key);
        }
    }
    if (!updated) {
        DEQ_INSERT_TAIL(_get_request_info()->records, update);
        qd_log(qd_log_source(QD_HTTP_LOG_SOURCE), QD_LOG_DEBUG, "Added http request info %s (%zu)", update->key, DEQ_SIZE(_get_request_info()->records));
    }
}

static void _add_http_request_info(qdr_core_t *core, qdr_http_request_info_t* record)
{
    qdr_action_t *action = qdr_action(_add_http_request_info_CT, "add_http_request_info");
    action->args.general.context_1 = record;
    qdr_action_enqueue(core, action);
}

static qdr_http_request_info_t* _new_qdr_http_request_info_t()
{
    qdr_http_request_info_t* record = NEW(qdr_http_request_info_t);
    ZERO(record);
    DEQ_INIT(record->detail);
    return record;
}

static char *_record_key(const char *host, const char *address, const char* site, bool ingress)
{
    if (!host)
        return 0;

    size_t hostlen = strlen(host);
    size_t addresslen = address ? strlen(address) + 1 : 0;
    size_t sitelen = site ? strlen(site) + 1 : 0;
    char *key = malloc(hostlen + addresslen + sitelen + 3);
    size_t i = 0;
    key[i++] = ingress ? 'i' : 'o';
    key[i++] = '_';
    strcpy(key+i, host);
    i += hostlen;
    if (address) {
        key[i++] = '_';
        strcpy(key+i, address);
        i += (addresslen-1);
    }
    if (site) {
        key[i++] = '@';
        strcpy(key+i, site);
    }
    return key;
}

void qd_http_record_request(qdr_core_t *core, const char * method, uint32_t status_code, const char *address, const char *host,
                             const char *local_site, const char *remote_site, bool ingress,
                             uint64_t bytes_in, uint64_t bytes_out, uint64_t latency)
{
    //
    // The _record_key() returns zero if there is no host. The qdr_http_request_info_t (record) should not even be created
    // if the passed in host parameter is zero. There is no point in having a record without a key.
    //
    if (!host)
        return;

    qdr_http_request_info_t* record = _new_qdr_http_request_info_t();
    record->ingress = ingress;
    record->address = address ? qd_strdup(address) : 0;
    record->host = host ? qd_strdup(host) : 0;
    record->site = remote_site ? qd_strdup(remote_site) : 0;
    record->key = _record_key(record->host, record->address, remote_site, record->ingress);
    record->requests = 1;
    record->bytes_in = bytes_in;
    record->bytes_out = bytes_out;
    record->max_latency = latency;

    qdr_http_method_status_t *detail = _new_qdr_http_method_status_t(method, (int) status_code);
    detail->requests = 1;
    DEQ_INSERT_TAIL(record->detail, detail);

    qd_log(qd_log_source(QD_HTTP_LOG_SOURCE), QD_LOG_DEBUG, "Adding http request info %s", record->key);
    _add_http_request_info(core, record);
}

char *qd_get_host_from_host_port(const char *host_port)
{
    char *end = strchr(host_port, ':');
    if (end == NULL) {
        return 0;
    } else {
        size_t len = end - host_port;
        char *host = malloc(len + 1);
        strncpy(host, host_port, len);
        host[len] = '\0';
        return host;
    }
}


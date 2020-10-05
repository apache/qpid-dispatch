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

#include <proton/condition.h>
#include <proton/listener.h>
#include <proton/netaddr.h>
#include <proton/proactor.h>
#include <proton/raw_connection.h>
#include "qpid/dispatch/ctools.h"
#include "qpid/dispatch/protocol_adaptor.h"
#include "delivery.h"
#include "tcp_adaptor.h"
#include <stdio.h>
#include <inttypes.h>

ALLOC_DEFINE(qd_tcp_listener_t);
ALLOC_DEFINE(qd_tcp_connector_t);

#define READ_BUFFERS 4
#define WRITE_BUFFERS 4

typedef struct qdr_tcp_connection_t qdr_tcp_connection_t;

struct qdr_tcp_connection_t {
    qd_handler_context_t  context;
    char                 *reply_to;
    qdr_connection_t     *conn;
    uint64_t              conn_id;
    qdr_link_t           *incoming;
    uint64_t              incoming_id;
    qdr_link_t           *outgoing;
    uint64_t              outgoing_id;
    pn_raw_connection_t  *socket;
    qdr_delivery_t       *instream;
    qdr_delivery_t       *outstream;
    bool                  ingress;
    bool                  egress_dispatcher;
    bool                  connector_closed;//only used if egress_dispatcher=true
    qd_timer_t           *activate_timer;
    qd_bridge_config_t   config;
    qd_server_t          *server;
    char                 *remote_address;
    char                 *global_id;
    uint64_t              bytes_in;
    uint64_t              bytes_out;
    uint64_t              opened_time;
    uint64_t              last_in_time;
    uint64_t              last_out_time;

    DEQ_LINKS(qdr_tcp_connection_t);
};

DEQ_DECLARE(qdr_tcp_connection_t, qdr_tcp_connection_list_t);

typedef struct qdr_tcp_adaptor_t {
    qdr_core_t               *core;
    qdr_protocol_adaptor_t   *adaptor;
    qd_tcp_listener_list_t    listeners;
    qd_tcp_connector_list_t   connectors;
    qdr_tcp_connection_list_t connections;
    qd_log_source_t          *log_source;
} qdr_tcp_adaptor_t;

static qdr_tcp_adaptor_t *tcp_adaptor;

static void qdr_add_tcp_connection_CT(qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_del_tcp_connection_CT(qdr_core_t *core, qdr_action_t *action, bool discard);

static void handle_disconnected(qdr_tcp_connection_t* conn);
static void free_qdr_tcp_connection(qdr_tcp_connection_t* conn);

static void on_activate(void *context)
{
    qdr_tcp_connection_t* conn = (qdr_tcp_connection_t*) context;

    qd_log(tcp_adaptor->log_source, QD_LOG_DEBUG, "[C%i] on_activate", conn->conn_id);
    while (qdr_connection_process(conn->conn)) {}
    if (conn->egress_dispatcher && conn->connector_closed) {
        qdr_connection_closed(conn->conn);
        qdr_connection_set_context(conn->conn, 0);
        free_qdr_tcp_connection(conn);
    }
}

static void grant_read_buffers(qdr_tcp_connection_t *conn)
{
    pn_raw_buffer_t raw_buffers[READ_BUFFERS];
    // Give proactor more read buffers for the socket
    if (!pn_raw_connection_is_read_closed(conn->socket)) {
        size_t desired = pn_raw_connection_read_buffers_capacity(conn->socket);
        while (desired) {
            size_t i;
            for (i = 0; i < desired && i < READ_BUFFERS; ++i) {
                qd_buffer_t *buf = qd_buffer();
                raw_buffers[i].bytes = (char*) qd_buffer_base(buf);
                raw_buffers[i].capacity = qd_buffer_capacity(buf);
                raw_buffers[i].size = 0;
                raw_buffers[i].offset = 0;
                raw_buffers[i].context = (uintptr_t) buf;
            }
            desired -= i;
            pn_raw_connection_give_read_buffers(conn->socket, raw_buffers, i);
        }
    }
}

static int handle_incoming(qdr_tcp_connection_t *conn)
{
    qd_buffer_list_t buffers;
    DEQ_INIT(buffers);
    pn_raw_buffer_t raw_buffers[READ_BUFFERS];
    size_t n;
    int count = 0;
    while ( (n = pn_raw_connection_take_read_buffers(conn->socket, raw_buffers, READ_BUFFERS)) ) {
        for (size_t i = 0; i < n && raw_buffers[i].bytes; ++i) {
            qd_buffer_t *buf = (qd_buffer_t*) raw_buffers[i].context;
            qd_buffer_insert(buf, raw_buffers[i].size);
            count += raw_buffers[i].size;
            DEQ_INSERT_TAIL(buffers, buf);
        }
    }

    grant_read_buffers(conn);

    if (conn->instream) {
        qd_composed_field_t *field = qd_compose(QD_PERFORMATIVE_BODY_DATA, 0);
        qd_compose_insert_binary_buffers(field, &buffers);
        qd_message_extend(qdr_delivery_message(conn->instream), field);
        qd_compose_free(field);
        qdr_delivery_continue(tcp_adaptor->core, conn->instream, false);
        qd_log(tcp_adaptor->log_source, QD_LOG_DEBUG, "[C%i][L%i] Continuing message with %i bytes", conn->conn_id, conn->incoming_id, count);
    } else {
        qd_message_t *msg = qd_message();

        qd_composed_field_t *props = qd_compose(QD_PERFORMATIVE_PROPERTIES, 0);
        qd_compose_start_list(props);
        qd_compose_insert_null(props);                      // message-id
        qd_compose_insert_null(props);                      // user-id
        if (conn->ingress) {
            qd_compose_insert_string(props, conn->config.address); // to
            qd_compose_insert_string(props, conn->global_id);   // subject
            qd_compose_insert_string(props, conn->reply_to);    // reply-to
        } else {
            qd_compose_insert_string(props, conn->reply_to); // to
            qd_compose_insert_string(props, conn->global_id);   // subject
            qd_compose_insert_null(props);    // reply-to
        }
        //qd_compose_insert_null(props);                      // correlation-id
        //qd_compose_insert_null(props);                      // content-type
        //qd_compose_insert_null(props);                      // content-encoding
        //qd_compose_insert_timestamp(props, 0);              // absolute-expiry-time
        //qd_compose_insert_timestamp(props, 0);              // creation-time
        //qd_compose_insert_null(props);                      // group-id
        //qd_compose_insert_uint(props, 0);                   // group-sequence
        //qd_compose_insert_null(props);                      // reply-to-group-id
        qd_compose_end_list(props);

        if (count > 0) {
            props = qd_compose(QD_PERFORMATIVE_BODY_DATA, props);
            qd_compose_insert_binary_buffers(props, &buffers);
        }

        qd_message_compose_2(msg, props, false);
        qd_compose_free(props);

        conn->instream = qdr_link_deliver(conn->incoming, msg, 0, false, 0, 0, 0, 0);
        qd_log(tcp_adaptor->log_source, QD_LOG_DEBUG, "[C%i][L%i] Initiating message with %i bytes", conn->conn_id, conn->incoming_id, count);
    }
    return count;
}

static void free_qdr_tcp_connection(qdr_tcp_connection_t* tc)
{
    qd_log(tcp_adaptor->log_source, QD_LOG_DEBUG, "Freeing %p", (void*) tc);
    if (tc->reply_to) {
        free(tc->reply_to);
    }
    if(tc->remote_address) {
        free(tc->remote_address);
    }
    if(tc->global_id) {
        free(tc->global_id);
    }
    if (tc->activate_timer) {
        qd_timer_free(tc->activate_timer);
    }
    //proactor will free the socket
    free(tc);
}

static void handle_disconnected(qdr_tcp_connection_t* conn)
{
    if (conn->instream) {
        qd_message_set_receive_complete(qdr_delivery_message(conn->instream));
        qdr_delivery_continue(tcp_adaptor->core, conn->instream, true);
    }
    qdr_connection_closed(conn->conn);
    qdr_connection_set_context(conn->conn, 0);
    //need to free on core thread to avoid deleting while in use by management agent
    qdr_action_t *action = qdr_action(qdr_del_tcp_connection_CT, "delete_tcp_connection");
    action->args.general.context_1 = conn;
    qdr_action_enqueue(tcp_adaptor->core, action);
}

static int read_message_body(qdr_tcp_connection_t *conn, qd_message_t *msg, pn_raw_buffer_t *buffers, int count)
{
    int used = 0;
    qd_message_body_data_t *body_data;
    while (used < count) {
        qd_message_body_data_result_t body_data_result = qd_message_next_body_data(msg, &body_data);
        if (body_data_result == QD_MESSAGE_BODY_DATA_OK) {
            used += qd_message_body_data_buffers(body_data, buffers + used, used, count - used);
            if (used > 0) {
                buffers[used-1].context = (uintptr_t) body_data;
            }
        } else if (body_data_result == QD_MESSAGE_BODY_DATA_INCOMPLETE) {
            return used;
        } else {
            switch (body_data_result) {
            case QD_MESSAGE_BODY_DATA_NO_MORE:
                qd_log(tcp_adaptor->log_source, QD_LOG_INFO, "[C%i] EOS", conn->conn_id); break;
            case QD_MESSAGE_BODY_DATA_INVALID:
                qd_log(tcp_adaptor->log_source, QD_LOG_ERROR, "[C%i] Invalid body data for streaming message", conn->conn_id); break;
            case QD_MESSAGE_BODY_DATA_NOT_DATA:
                qd_log(tcp_adaptor->log_source, QD_LOG_ERROR, "[C%i] Invalid body; expected data section", conn->conn_id); break;
            default:
                break;
            }
            qd_message_set_send_complete(msg);
            return -1;
        }
    }
    return used;
}

static void handle_outgoing(qdr_tcp_connection_t *conn)
{
    if (conn->outstream) {
        qd_message_t *msg = qdr_delivery_message(conn->outstream);
        pn_raw_buffer_t buffs[WRITE_BUFFERS];
        for (int i = 0; i < WRITE_BUFFERS; i++) {
            buffs[i].context = 0;
            buffs[i].bytes = 0;
            buffs[i].capacity = 0;
            buffs[i].size = 0;
            buffs[i].offset = 0;
        }
        int n = read_message_body(conn, msg, buffs, WRITE_BUFFERS);
        if (n > 0) {
            size_t used = pn_raw_connection_write_buffers(conn->socket, buffs, n);
            int bytes_written = 0;
            for (size_t i = 0; i < used; i++) {
                if (buffs[i].bytes) {
                    bytes_written += buffs[i].size;
                } else {
                    qd_log(tcp_adaptor->log_source, QD_LOG_ERROR, "[C%i] empty buffer can't be written (%i of %i)", conn->conn_id, i+1, used);
                }
            }
            qd_log(tcp_adaptor->log_source, QD_LOG_DEBUG, "[C%i] Writing %i bytes", conn->conn_id, bytes_written);
        }
        if (qd_message_receive_complete(msg) || qd_message_send_complete(msg)) {
            pn_raw_connection_close(conn->socket);
        }
    }
}

static char *get_global_id(char *site_id, char *host_port)
{
    int len1 = strlen(host_port);
    int len = site_id ? len1 + strlen(site_id) + 2 : len1 + 1;
    char *result = malloc(len);
    strcpy(result, host_port);
    if (site_id) {
        result[len1] = '@';
        strcpy(result+len1+1, site_id);
    }
    return result;
}

static char *get_address_string(pn_raw_connection_t *socket)
{
    const pn_netaddr_t *netaddr = pn_raw_connection_remote_addr(socket);
    char buffer[1024];
    int len = pn_netaddr_str(netaddr, buffer, 1024);
    if (len <= 1024) {
        return strdup(buffer);
    } else {
        return strndup(buffer, 1024);
    }
}

static void qdr_tcp_connection_ingress_accept(qdr_tcp_connection_t* tc)
{
    tc->remote_address = get_address_string(tc->socket);
    tc->global_id = get_global_id(tc->config.site_id, tc->remote_address);
    qdr_connection_info_t *info = qdr_connection_info(false, //bool             is_encrypted,
                                                      false, //bool             is_authenticated,
                                                      true,  //bool             opened,
                                                      "",   //char            *sasl_mechanisms,
                                                      QD_INCOMING, //qd_direction_t   dir,
                                                      tc->remote_address,    //const char      *host,
                                                      "",    //const char      *ssl_proto,
                                                      "",    //const char      *ssl_cipher,
                                                      "",    //const char      *user,
                                                      "TcpAdaptor",    //const char      *container,
                                                      pn_data(0),     //pn_data_t       *connection_properties,
                                                      0,     //int              ssl_ssf,
                                                      false, //bool             ssl,
                                                      // set if remote is a qdrouter
                                                      0);    //const qdr_router_version_t *version)

    tc->conn_id = qd_server_allocate_connection_id(tc->server);
    qdr_connection_t *conn = qdr_connection_opened(tcp_adaptor->core,
                                                   tcp_adaptor->adaptor,
                                                   true,
                                                   QDR_ROLE_NORMAL,
                                                   1,
                                                   tc->conn_id,
                                                   0,
                                                   0,
                                                   false,
                                                   false,
                                                   false,
                                                   false,
                                                   250,
                                                   0,
                                                   info,
                                                   0,
                                                   0);
    tc->conn = conn;
    qdr_connection_set_context(conn, tc);

    qdr_terminus_t *dynamic_source = qdr_terminus(0);
    qdr_terminus_set_dynamic(dynamic_source);
    qdr_terminus_t *target = qdr_terminus(0);
    qdr_terminus_set_address(target, tc->config.address);

    tc->outgoing = qdr_link_first_attach(conn,
                                         QD_OUTGOING,
                                         dynamic_source,   //qdr_terminus_t   *source,
                                         qdr_terminus(0),  //qdr_terminus_t   *target,
                                         "tcp.ingress.out",        //const char       *name,
                                         0,                //const char       *terminus_addr,
                                         false,
                                         NULL,
                                         &(tc->outgoing_id));
    qdr_link_set_context(tc->outgoing, tc);
    tc->incoming = qdr_link_first_attach(conn,
                                         QD_INCOMING,
                                         qdr_terminus(0),  //qdr_terminus_t   *source,
                                         target,           //qdr_terminus_t   *target,
                                         "tcp.ingress.in",         //const char       *name,
                                         0,                //const char       *terminus_addr,
                                         false,
                                         NULL,
                                         &(tc->incoming_id));
    tc->opened_time = tcp_adaptor->core->uptime_ticks;
    qdr_link_set_context(tc->incoming, tc);

    grant_read_buffers(tc);

    qdr_action_t *action = qdr_action(qdr_add_tcp_connection_CT, "add_tcp_connection");
    action->args.general.context_1 = tc;
    qdr_action_enqueue(tcp_adaptor->core, action);
}

static void handle_connection_event(pn_event_t *e, qd_server_t *qd_server, void *context)
{
    qdr_tcp_connection_t *conn = (qdr_tcp_connection_t*) context;
    qd_log_source_t *log = tcp_adaptor->log_source;
    switch (pn_event_type(e)) {
    case PN_RAW_CONNECTION_CONNECTED: {
        if (conn->ingress) {
            qdr_tcp_connection_ingress_accept(conn);
            qd_log(log, QD_LOG_INFO, "[C%i] Accepted from %s", conn->conn_id, conn->remote_address);
            break;
        } else {
            conn->remote_address = get_address_string(conn->socket);
            conn->opened_time = tcp_adaptor->core->uptime_ticks;
            qd_log(log, QD_LOG_INFO, "[C%i] Connected", conn->conn_id);
            qdr_connection_process(conn->conn);
            break;
        }
    }
    case PN_RAW_CONNECTION_CLOSED_READ: {
        qd_log(log, QD_LOG_DEBUG, "[C%i] Closed for reading", conn->conn_id);
        pn_raw_connection_close(conn->socket);
        break;
    }
    case PN_RAW_CONNECTION_CLOSED_WRITE: {
        qd_log(log, QD_LOG_DEBUG, "[C%i] Closed for writing", conn->conn_id);
        pn_raw_connection_close(conn->socket);
        break;
    }
    case PN_RAW_CONNECTION_DISCONNECTED: {
        qd_log(log, QD_LOG_INFO, "[C%i] Disconnected", conn->conn_id);
        handle_disconnected(conn);
        break;
    }
    case PN_RAW_CONNECTION_NEED_WRITE_BUFFERS: {
        qd_log(log, QD_LOG_DEBUG, "[C%i] Need write buffers", conn->conn_id);
        while (qdr_connection_process(conn->conn)) {}
        break;
    }
    case PN_RAW_CONNECTION_NEED_READ_BUFFERS: {
        qd_log(log, QD_LOG_DEBUG, "[C%i] Need read buffers", conn->conn_id);
        while (qdr_connection_process(conn->conn)) {}
        break;
    }
    case PN_RAW_CONNECTION_WAKE: {
        qd_log(log, QD_LOG_DEBUG, "[C%i] Wake-up", conn->conn_id);
        while (qdr_connection_process(conn->conn)) {}
        break;
    }
    case PN_RAW_CONNECTION_READ: {
        int read = handle_incoming(conn);
        conn->last_in_time = tcp_adaptor->core->uptime_ticks;
        conn->bytes_in += read;
        qd_log(log, QD_LOG_DEBUG, "[C%i] Read %i bytes", conn->conn_id, read);
        while (qdr_connection_process(conn->conn)) {}
        break;
    }
    case PN_RAW_CONNECTION_WRITTEN: {
        pn_raw_buffer_t buffs[WRITE_BUFFERS];
        size_t pn_raw_connection_take_written_buffers(pn_raw_connection_t *connection, pn_raw_buffer_t *buffers, size_t num);
        size_t n;
        size_t written = 0;
        while ( (n = pn_raw_connection_take_written_buffers(conn->socket, buffs, WRITE_BUFFERS)) ) {
            for (size_t i = 0; i < n; ++i) {
                written += buffs[i].size;
                if (buffs[i].context) {
                    qd_message_body_data_release((qd_message_body_data_t*) buffs[i].context);
                }
            }
        }
        qd_log(log, QD_LOG_DEBUG, "[C%i] Wrote %i bytes", conn->conn_id, written);
        conn->last_out_time = tcp_adaptor->core->uptime_ticks;
        conn->bytes_out += written;
        while (qdr_connection_process(conn->conn)) {}
        break;
    }
    default:
        break;
    }
}

static qdr_tcp_connection_t *qdr_tcp_connection_ingress(qd_tcp_listener_t* listener)
{
    qdr_tcp_connection_t* tc = NEW(qdr_tcp_connection_t);
    ZERO(tc);
    tc->ingress = true;
    tc->context.context = tc;
    tc->context.handler = &handle_connection_event;
    tc->config = listener->config;
    tc->server = listener->server;
    tc->socket = pn_raw_connection();
    pn_raw_connection_set_context(tc->socket, tc);
    //the following call will cause a PN_RAW_CONNECTION_CONNECTED
    //event on another thread, which is where the rest of the
    //initialisation will happen, through a call to
    //qdr_tcp_connection_ingress_accept
    pn_listener_raw_accept(listener->pn_listener, tc->socket);
    return tc;
}

static void tcp_connector_establish(qdr_tcp_connection_t *conn)
{
    qd_log(tcp_adaptor->log_source, QD_LOG_INFO, "[C%i] Connecting to: %s", conn->conn_id, conn->config.host_port);
    conn->socket = pn_raw_connection();
    pn_raw_connection_set_context(conn->socket, conn);
    pn_proactor_raw_connect(qd_server_proactor(conn->server), conn->socket, conn->config.host_port);
}

static qdr_tcp_connection_t *qdr_tcp_connection_egress(qd_bridge_config_t *config, qd_server_t *server, qdr_delivery_t *initial_delivery)
{
    qdr_tcp_connection_t* tc = NEW(qdr_tcp_connection_t);
    ZERO(tc);
    if (initial_delivery) {
        tc->egress_dispatcher = false;
    } else {
        tc->activate_timer = qd_timer(tcp_adaptor->core->qd, on_activate, tc);
        tc->egress_dispatcher = true;
    }
    tc->ingress = false;
    tc->context.context = tc;
    tc->context.handler = &handle_connection_event;
    tc->config = *config;
    tc->server = server;
    qdr_connection_info_t *info = qdr_connection_info(false, //bool             is_encrypted,
                                                      false, //bool             is_authenticated,
                                                      true,  //bool             opened,
                                                      "",   //char            *sasl_mechanisms,
                                                      QD_OUTGOING, //qd_direction_t   dir,
                                                      tc->egress_dispatcher ? "egress-dispatch" : tc->config.host_port,    //const char      *host,
                                                      "",    //const char      *ssl_proto,
                                                      "",    //const char      *ssl_cipher,
                                                      "",    //const char      *user,
                                                      "TcpAdaptor",    //const char      *container,
                                                      pn_data(0),     //pn_data_t       *connection_properties,
                                                      0,     //int              ssl_ssf,
                                                      false, //bool             ssl,
                                                      // set if remote is a qdrouter
                                                      0);    //const qdr_router_version_t *version)

    tc->conn_id = qd_server_allocate_connection_id(tc->server);
    qdr_connection_t *conn = qdr_connection_opened(tcp_adaptor->core,
                                                   tcp_adaptor->adaptor,
                                                   false,
                                                   QDR_ROLE_NORMAL,
                                                   1,
                                                   tc->conn_id,
                                                   0,
                                                   0,
                                                   false,
                                                   false,
                                                   false,
                                                   false,
                                                   250,
                                                   0,
                                                   info,
                                                   0,
                                                   0);
    tc->conn = conn;
    qdr_connection_set_context(conn, tc);

    qdr_terminus_t *source = qdr_terminus(0);
    qdr_terminus_set_address(source, tc->config.address);

    tc->outgoing = qdr_link_first_attach(conn,
                          QD_OUTGOING,
                          source,           //qdr_terminus_t   *source,
                          qdr_terminus(0),  //qdr_terminus_t   *target,
                          "tcp.egress.out", //const char       *name,
                          0,                //const char       *terminus_addr,
                          !(tc->egress_dispatcher),
                          initial_delivery,
                          &(tc->outgoing_id));
    qdr_link_set_context(tc->outgoing, tc);
    //the incoming link for egress is created once we receive the
    //message which has the reply to address (and read buffers are
    //granted at that point)
    if (!tc->egress_dispatcher) {
        tcp_connector_establish(tc);
    }

    return tc;
}

static void free_bridge_config(qd_bridge_config_t *config)
{
    if (!config) return;
    free(config->host);
    free(config->port);
    free(config->name);
    free(config->site_id);
    free(config->host_port);
}

#define CHECK() if (qd_error_code()) goto error

static qd_error_t load_bridge_config(qd_dispatch_t *qd, qd_bridge_config_t *config, qd_entity_t* entity, bool is_listener)
{
    qd_error_clear();
    ZERO(config);

    config->name                 = qd_entity_get_string(entity, "name");              CHECK();
    config->address              = qd_entity_get_string(entity, "address");           CHECK();
    config->host                 = qd_entity_get_string(entity, "host");              CHECK();
    config->port                 = qd_entity_get_string(entity, "port");              CHECK();
    config->site_id              = qd_entity_opt_string(entity, "siteId", 0);        CHECK();

    int hplen = strlen(config->host) + strlen(config->port) + 2;
    config->host_port = malloc(hplen);
    snprintf(config->host_port, hplen, "%s:%s", config->host, config->port);

    return QD_ERROR_NONE;

 error:
    free_bridge_config(config);
    return qd_error_code();
}

static void log_tcp_bridge_config(qd_log_source_t *log, qd_bridge_config_t *c, const char *what) {
    qd_log(log, QD_LOG_INFO, "Configured %s for %s, %s:%s", what, c->address, c->host, c->port);
}

void qd_tcp_listener_decref(qd_tcp_listener_t* li)
{
    if (li && sys_atomic_dec(&li->ref_count) == 1) {
        free_bridge_config(&li->config);
        free_qd_tcp_listener_t(li);
    }
}

static void handle_listener_event(pn_event_t *e, qd_server_t *qd_server, void *context) {
    qd_log_source_t *log = tcp_adaptor->log_source;

    qd_tcp_listener_t *li = (qd_tcp_listener_t*) context;
    const char *host_port = li->config.host_port;

    switch (pn_event_type(e)) {

    case PN_LISTENER_OPEN: {
        qd_log(log, QD_LOG_NOTICE, "Listening on %s", host_port);
        break;
    }

    case PN_LISTENER_ACCEPT: {
        qd_log(log, QD_LOG_INFO, "Accepting TCP connection on %s", host_port);
        qdr_tcp_connection_ingress(li);
        break;
    }

    case PN_LISTENER_CLOSE:
        if (li->pn_listener) {
            pn_condition_t *cond = pn_listener_condition(li->pn_listener);
            if (pn_condition_is_set(cond)) {
                qd_log(log, QD_LOG_ERROR, "Listener error on %s: %s (%s)", host_port,
                       pn_condition_get_description(cond),
                       pn_condition_get_name(cond));
            } else {
                qd_log(log, QD_LOG_TRACE, "Listener closed on %s", host_port);
            }
            pn_listener_set_context(li->pn_listener, 0);
            li->pn_listener = 0;
            qd_tcp_listener_decref(li);
        }
        break;

    default:
        break;
    }
}

static qd_tcp_listener_t *qd_tcp_listener(qd_server_t *server)
{
    qd_tcp_listener_t *li = new_qd_tcp_listener_t();
    if (!li) return 0;
    ZERO(li);
    sys_atomic_init(&li->ref_count, 1);
    li->server = server;
    li->context.context = li;
    li->context.handler = &handle_listener_event;
    return li;
}

static const int BACKLOG = 50;  /* Listening backlog */

static bool tcp_listener_listen(qd_tcp_listener_t *li) {
   li->pn_listener = pn_listener();
    if (li->pn_listener) {
        pn_listener_set_context(li->pn_listener, &li->context);
        pn_proactor_listen(qd_server_proactor(li->server), li->pn_listener, li->config.host_port, BACKLOG);
        sys_atomic_inc(&li->ref_count); /* In use by proactor, PN_LISTENER_CLOSE will dec */
        /* Listen is asynchronous, log "listening" message on PN_LISTENER_OPEN event */
    } else {
        qd_log(tcp_adaptor->log_source, QD_LOG_CRITICAL, "Failed to create listener for %s",
               li->config.host_port);
     }
    return li->pn_listener;
}

qd_tcp_listener_t *qd_dispatch_configure_tcp_listener(qd_dispatch_t *qd, qd_entity_t *entity)
{
    qd_tcp_listener_t *li = qd_tcp_listener(qd->server);
    if (!li || load_bridge_config(qd, &li->config, entity, true) != QD_ERROR_NONE) {
        qd_log(tcp_adaptor->log_source, QD_LOG_ERROR, "Unable to create tcp listener: %s", qd_error_message());
        qd_tcp_listener_decref(li);
        return 0;
    }
    DEQ_ITEM_INIT(li);
    DEQ_INSERT_TAIL(tcp_adaptor->listeners, li);
    log_tcp_bridge_config(tcp_adaptor->log_source, &li->config, "TcpListener");
    tcp_listener_listen(li);
    return li;
}

void qd_dispatch_delete_tcp_listener(qd_dispatch_t *qd, void *impl)
{
    qd_tcp_listener_t *li = (qd_tcp_listener_t*) impl;
    if (li) {
        if (li->pn_listener) {
            pn_listener_close(li->pn_listener);
        }
        DEQ_REMOVE(tcp_adaptor->listeners, li);
        qd_log(tcp_adaptor->log_source, QD_LOG_INFO, "Deleted TcpListener for %s, %s:%s", li->config.address, li->config.host, li->config.port);
        qd_tcp_listener_decref(li);
    }
}

qd_error_t qd_entity_refresh_tcpListener(qd_entity_t* entity, void *impl)
{
    return QD_ERROR_NONE;
}

static qd_tcp_connector_t *qd_tcp_connector(qd_server_t *server)
{
    qd_tcp_connector_t *c = new_qd_tcp_connector_t();
    if (!c) return 0;
    ZERO(c);
    sys_atomic_init(&c->ref_count, 1);
    c->server      = server;
    return c;
}

void qd_tcp_connector_decref(qd_tcp_connector_t* c)
{
    if (c && sys_atomic_dec(&c->ref_count) == 1) {
        free_bridge_config(&c->config);
        free_qd_tcp_connector_t(c);
    }
}

qd_tcp_connector_t *qd_dispatch_configure_tcp_connector(qd_dispatch_t *qd, qd_entity_t *entity)
{
    qd_tcp_connector_t *c = qd_tcp_connector(qd->server);
    if (!c || load_bridge_config(qd, &c->config, entity, true) != QD_ERROR_NONE) {
        qd_log(tcp_adaptor->log_source, QD_LOG_ERROR, "Unable to create tcp connector: %s", qd_error_message());
        qd_tcp_connector_decref(c);
        return 0;
    }
    DEQ_ITEM_INIT(c);
    DEQ_INSERT_TAIL(tcp_adaptor->connectors, c);
    log_tcp_bridge_config(tcp_adaptor->log_source, &c->config, "TcpConnector");
    c->dispatcher = qdr_tcp_connection_egress(&(c->config), c->server, NULL);
    return c;
}

static void close_egress_dispatcher(qdr_tcp_connection_t *context)
{
    //actual close needs to happen on connection thread
    context->connector_closed = true;
    qd_timer_schedule(context->activate_timer, 0);
}

void qd_dispatch_delete_tcp_connector(qd_dispatch_t *qd, void *impl)
{
    qd_tcp_connector_t *ct = (qd_tcp_connector_t*) impl;
    if (ct) {
        //need to close the pseudo-connection used for dispatching
        //deliveries out to live connnections:
        qd_log(tcp_adaptor->log_source, QD_LOG_INFO, "Deleted TcpConnector for %s, %s:%s", ct->config.address, ct->config.host, ct->config.port);
        close_egress_dispatcher((qdr_tcp_connection_t*) ct->dispatcher);
        DEQ_REMOVE(tcp_adaptor->connectors, ct);
        qd_tcp_connector_decref(ct);
    }
}

qd_error_t qd_entity_refresh_tcpConnector(qd_entity_t* entity, void *impl)
{
    return QD_ERROR_NONE;
}

static void qdr_tcp_first_attach(void *context, qdr_connection_t *conn, qdr_link_t *link,
                                 qdr_terminus_t *source, qdr_terminus_t *target,
                                 qd_session_class_t session_class)
{
}

static void qdr_tcp_connection_copy_reply_to(qdr_tcp_connection_t* tc, qd_iterator_t* reply_to)
{
    int length = qd_iterator_length(reply_to);
    tc->reply_to = malloc(length + 1);
    qd_iterator_strncpy(reply_to, tc->reply_to, length + 1);
}

static void qdr_tcp_connection_copy_global_id(qdr_tcp_connection_t* tc, qd_iterator_t* subject)
{
    int length = qd_iterator_length(subject);
    tc->global_id = malloc(length + 1);
    qd_iterator_strncpy(subject, tc->global_id, length + 1);
}

static void qdr_tcp_second_attach(void *context, qdr_link_t *link,
                                  qdr_terminus_t *source, qdr_terminus_t *target)
{
    void* link_context = qdr_link_get_context(link);
    if (link_context) {
        qdr_tcp_connection_t* tc = (qdr_tcp_connection_t*) link_context;
        if (qdr_link_direction(link) == QD_OUTGOING) {
            if (tc->ingress) {
                qdr_tcp_connection_copy_reply_to(tc, qdr_terminus_get_address(source));
                // for ingress, can start reading from socket once we have
                // a reply to address, as that is when we are able to send
                // out a message
                grant_read_buffers(tc);
                handle_incoming(tc);
            }
            qdr_link_flow(tcp_adaptor->core, link, 10, false);
        } else if (!tc->ingress) {
            //for egress we can start reading from the socket once we
            //have the link to send messages over
            grant_read_buffers(tc);
        }
    }
}


static void qdr_tcp_detach(void *context, qdr_link_t *link, qdr_error_t *error, bool first, bool close)
{
}


static void qdr_tcp_flow(void *context, qdr_link_t *link, int credit)
{
}


static void qdr_tcp_offer(void *context, qdr_link_t *link, int delivery_count)
{
}


static void qdr_tcp_drained(void *context, qdr_link_t *link)
{
}


static void qdr_tcp_drain(void *context, qdr_link_t *link, bool mode)
{
}


static int qdr_tcp_push(void *context, qdr_link_t *link, int limit)
{
    return qdr_link_process_deliveries(tcp_adaptor->core, link, limit);
}


static uint64_t qdr_tcp_deliver(void *context, qdr_link_t *link, qdr_delivery_t *delivery, bool settled)
{
    void* link_context = qdr_link_get_context(link);
    if (link_context) {
            qdr_tcp_connection_t* tc = (qdr_tcp_connection_t*) link_context;
            qd_log(tcp_adaptor->log_source, QD_LOG_DEBUG, "[C%i][L%i] Delivery event", tc->conn_id, tc->outgoing_id);
            if (tc->egress_dispatcher) {
                qdr_tcp_connection_egress(&(tc->config), tc->server, delivery);
            } else if (!tc->outstream) {
                tc->outstream = delivery;
                if (!tc->ingress) {
                    //on egress, can only set up link for the reverse
                    //direction once we receive the first part of the
                    //message from client to server
                    qd_message_t *msg = qdr_delivery_message(delivery);
                    qdr_tcp_connection_copy_global_id(tc, qd_message_field_iterator(msg, QD_FIELD_SUBJECT));
                    qdr_tcp_connection_copy_reply_to(tc, qd_message_field_iterator(msg, QD_FIELD_REPLY_TO));
                    qdr_terminus_t *target = qdr_terminus(0);
                    qdr_terminus_set_address(target, tc->reply_to);
                    tc->incoming = qdr_link_first_attach(tc->conn,
                                                         QD_INCOMING,
                                                         qdr_terminus(0),  //qdr_terminus_t   *source,
                                                         target, //qdr_terminus_t   *target,
                                                         "tcp.egress.in",  //const char       *name,
                                                         0,                //const char       *terminus_addr,
                                                         false,
                                                         NULL,
                                                         &(tc->incoming_id));
                    qdr_link_set_context(tc->incoming, tc);
                    //add this connection to those visible through management now that we have the global_id
                    qdr_action_t *action = qdr_action(qdr_add_tcp_connection_CT, "add_tcp_connection");
                    action->args.general.context_1 = tc;
                    qdr_action_enqueue(tcp_adaptor->core, action);

                    handle_incoming(tc);
                }
            }
            handle_outgoing(tc);
    }
    return 0;
}


static int qdr_tcp_get_credit(void *context, qdr_link_t *link)
{
    return 10;
}


static void qdr_tcp_delivery_update(void *context, qdr_delivery_t *dlv, uint64_t disp, bool settled)
{
    void* link_context = qdr_link_get_context(qdr_delivery_link(dlv));
    if (link_context) {
        qdr_tcp_connection_t* tc = (qdr_tcp_connection_t*) link_context;
        qd_log(tcp_adaptor->log_source, QD_LOG_DEBUG, "[C%i] Delivery update", tc->conn_id);
    }
}


static void qdr_tcp_conn_close(void *context, qdr_connection_t *conn, qdr_error_t *error)
{
    
}


static void qdr_tcp_conn_trace(void *context, qdr_connection_t *conn, bool trace)
{
}

static void qdr_tcp_activate(void *notused, qdr_connection_t *c)
{
    void *context = qdr_connection_get_context(c);
    if (context) {
        qdr_tcp_connection_t* conn = (qdr_tcp_connection_t*) context;
        if (conn->socket) {
            pn_raw_connection_wake(conn->socket);
        } else if (conn->activate_timer) {
            // On egress, the raw connection is only created once the
            // first part of the message encapsulating the
            // client->server half of the stream has been
            // received. Prior to that however a subscribing link (and
            // its associated connection must be setup), for which we
            // fake wakeup by using a timer.
            qd_timer_schedule(conn->activate_timer, 0);
        } else {
            qd_log(tcp_adaptor->log_source, QD_LOG_ERROR, "[C%i] Cannot activate", conn->conn_id);
        }
    }
}

/**
 * This initialization function will be invoked when the router core is ready for the protocol
 * adaptor to be created.  This function must:
 *
 *   1) Register the protocol adaptor with the router-core.
 *   2) Prepare the protocol adaptor to be configured.
 */
static void qdr_tcp_adaptor_init(qdr_core_t *core, void **adaptor_context)
{
    qdr_tcp_adaptor_t *adaptor = NEW(qdr_tcp_adaptor_t);
    adaptor->core    = core;
    adaptor->adaptor = qdr_protocol_adaptor(core,
                                            "tcp",                // name
                                            adaptor,              // context
                                            qdr_tcp_activate,                    // activate
                                            qdr_tcp_first_attach,
                                            qdr_tcp_second_attach,
                                            qdr_tcp_detach,
                                            qdr_tcp_flow,
                                            qdr_tcp_offer,
                                            qdr_tcp_drained,
                                            qdr_tcp_drain,
                                            qdr_tcp_push,
                                            qdr_tcp_deliver,
                                            qdr_tcp_get_credit,
                                            qdr_tcp_delivery_update,
                                            qdr_tcp_conn_close,
                                            qdr_tcp_conn_trace);
    adaptor->log_source  = qd_log_source("TCP_ADAPTOR");
    DEQ_INIT(adaptor->listeners);
    DEQ_INIT(adaptor->connectors);
    *adaptor_context = adaptor;

    tcp_adaptor = adaptor;
}


static void qdr_tcp_adaptor_final(void *adaptor_context)
{
    qdr_tcp_adaptor_t *adaptor = (qdr_tcp_adaptor_t*) adaptor_context;
    qdr_protocol_adaptor_free(adaptor->core, adaptor->adaptor);
    free(adaptor);
    tcp_adaptor =  NULL;
}

/**
 * Declare the adaptor so that it will self-register on process startup.
 */
QDR_CORE_ADAPTOR_DECLARE("tcp-adaptor", qdr_tcp_adaptor_init, qdr_tcp_adaptor_final)

#define QDR_TCP_CONNECTION_NAME                   0
#define QDR_TCP_CONNECTION_IDENTITY               1
#define QDR_TCP_CONNECTION_ADDRESS                2
#define QDR_TCP_CONNECTION_HOST                   3
#define QDR_TCP_CONNECTION_DIRECTION              4
#define QDR_TCP_CONNECTION_BYTES_IN               5
#define QDR_TCP_CONNECTION_BYTES_OUT              6
#define QDR_TCP_CONNECTION_UPTIME_SECONDS         7
#define QDR_TCP_CONNECTION_LAST_IN_SECONDS        8
#define QDR_TCP_CONNECTION_LAST_OUT_SECONDS       9


const char * const QDR_TCP_CONNECTION_DIRECTION_IN  = "in";
const char * const QDR_TCP_CONNECTION_DIRECTION_OUT = "out";

const char *qdr_tcp_connection_columns[] =
    {"name",
     "identity",
     "address",
     "host",
     "direction",
     "bytesIn",
     "bytesOut",
     "uptimeSeconds",
     "lastInSeconds",
     "lastOutSeconds",
     0};

const char *TCP_CONNECTION_TYPE = "org.apache.qpid.dispatch.tcpConnection";

static void insert_column(qdr_core_t *core, qdr_tcp_connection_t *conn, int col, qd_composed_field_t *body)
{
    qd_log(tcp_adaptor->log_source, QD_LOG_DEBUG, "Insert column %i for %p", col, (void*) conn);
    char id_str[100];

    if (!conn)
        return;

    switch(col) {
    case QDR_TCP_CONNECTION_NAME:
        qd_compose_insert_string(body, conn->global_id);
        break;

    case QDR_TCP_CONNECTION_IDENTITY: {
        snprintf(id_str, 100, "%"PRId64, conn->conn_id);
        qd_compose_insert_string(body, id_str);
        break;
    }

    case QDR_TCP_CONNECTION_ADDRESS:
        qd_compose_insert_string(body, conn->config.address);
        break;

    case QDR_TCP_CONNECTION_HOST:
        qd_compose_insert_string(body, conn->remote_address);
        break;

    case QDR_TCP_CONNECTION_DIRECTION:
        if (conn->ingress)
            qd_compose_insert_string(body, QDR_TCP_CONNECTION_DIRECTION_IN);
        else
            qd_compose_insert_string(body, QDR_TCP_CONNECTION_DIRECTION_OUT);
        break;

    case QDR_TCP_CONNECTION_BYTES_IN:
        qd_compose_insert_uint(body, conn->bytes_in);
        break;

    case QDR_TCP_CONNECTION_BYTES_OUT:
        qd_compose_insert_uint(body, conn->bytes_out);
        break;

    case QDR_TCP_CONNECTION_UPTIME_SECONDS:
        qd_compose_insert_uint(body, core->uptime_ticks - conn->opened_time);
        break;

    case QDR_TCP_CONNECTION_LAST_IN_SECONDS:
        if (conn->last_in_time==0)
            qd_compose_insert_null(body);
        else
            qd_compose_insert_uint(body, core->uptime_ticks - conn->last_in_time);
        break;

    case QDR_TCP_CONNECTION_LAST_OUT_SECONDS:
        if (conn->last_out_time==0)
            qd_compose_insert_null(body);
        else
            qd_compose_insert_uint(body, core->uptime_ticks - conn->last_out_time);
        break;

    }
}


static void write_list(qdr_core_t *core, qdr_query_t *query,  qdr_tcp_connection_t *conn)
{
    qd_composed_field_t *body = query->body;

    qd_compose_start_list(body);

    if (conn) {
        int i = 0;
        while (query->columns[i] >= 0) {
            insert_column(core, conn, query->columns[i], body);
            i++;
        }
    }
    qd_compose_end_list(body);
}

static void write_map(qdr_core_t           *core,
                      qdr_tcp_connection_t *conn,
                      qd_composed_field_t  *body,
                      const char           *qdr_connection_columns[])
{
    qd_compose_start_map(body);

    for(int i = 0; i < QDR_TCP_CONNECTION_COLUMN_COUNT; i++) {
        qd_compose_insert_string(body, qdr_connection_columns[i]);
        insert_column(core, conn, i, body);
    }

    qd_compose_end_map(body);
}

static void advance(qdr_query_t *query, qdr_tcp_connection_t *conn)
{
    if (conn) {
        query->next_offset++;
        conn = DEQ_NEXT(conn);
        query->more = !!conn;
    }
    else {
        query->more = false;
    }
}

static qdr_tcp_connection_t *find_by_identity(qdr_core_t *core, qd_iterator_t *identity)
{
    if (!identity)
        return 0;

    qdr_tcp_connection_t *conn = DEQ_HEAD(tcp_adaptor->connections);
    while (conn) {
        // Convert the passed in identity to a char*
        char id[100];
        snprintf(id, 100, "%"PRId64, conn->conn_id);
        if (qd_iterator_equal(identity, (const unsigned char*) id))
            break;
        conn = DEQ_NEXT(conn);
    }

    return conn;

}

void qdra_tcp_connection_get_first_CT(qdr_core_t *core, qdr_query_t *query, int offset)
{
    qd_log(tcp_adaptor->log_source, QD_LOG_DEBUG, "query for first tcp connection (%i)", offset);
    query->status = QD_AMQP_OK;

    if (offset >= DEQ_SIZE(tcp_adaptor->connections)) {
        query->more = false;
        qdr_agent_enqueue_response_CT(core, query);
        return;
    }

    qdr_tcp_connection_t *conn = DEQ_HEAD(tcp_adaptor->connections);
    for (int i = 0; i < offset && conn; i++)
        conn = DEQ_NEXT(conn);
    assert(conn);

    if (conn) {
        write_list(core, query, conn);
        query->next_offset = offset;
        advance(query, conn);
    } else {
        query->more = false;
    }

    qdr_agent_enqueue_response_CT(core, query);
}

void qdra_tcp_connection_get_next_CT(qdr_core_t *core, qdr_query_t *query)
{
    qdr_tcp_connection_t *conn = 0;

    if (query->next_offset < DEQ_SIZE(tcp_adaptor->connections)) {
        conn = DEQ_HEAD(tcp_adaptor->connections);
        for (int i = 0; i < query->next_offset && conn; i++)
            conn = DEQ_NEXT(conn);
    }

    if (conn) {
        write_list(core, query, conn);
        advance(query, conn);
    } else {
        query->more = false;
    }
    qdr_agent_enqueue_response_CT(core, query);
}

void qdra_tcp_connection_get_CT(qdr_core_t          *core,
                               qd_iterator_t       *name,
                               qd_iterator_t       *identity,
                               qdr_query_t         *query,
                               const char          *qdr_tcp_connection_columns[])
{
    qdr_tcp_connection_t *conn = 0;

    if (!identity) {
        query->status = QD_AMQP_BAD_REQUEST;
        query->status.description = "Name not supported. Identity required";
        qd_log(core->agent_log, QD_LOG_ERROR, "Error performing READ of %s: %s", TCP_CONNECTION_TYPE, query->status.description);
    } else {
        conn = find_by_identity(core, identity);

        if (conn == 0) {
            query->status = QD_AMQP_NOT_FOUND;
        } else {
            write_map(core, conn, query->body, qdr_tcp_connection_columns);
            query->status = QD_AMQP_OK;
        }
    }
    qdr_agent_enqueue_response_CT(core, query);
}

static void qdr_add_tcp_connection_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    qdr_tcp_connection_t *conn = (qdr_tcp_connection_t*) action->args.general.context_1;
    DEQ_INSERT_TAIL(tcp_adaptor->connections, conn);
    qd_log(tcp_adaptor->log_source, QD_LOG_DEBUG, "Added tcp connection %s (%i)", conn->config.host_port, DEQ_SIZE(tcp_adaptor->connections));
}

static void qdr_del_tcp_connection_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    qdr_tcp_connection_t *conn = (qdr_tcp_connection_t*) action->args.general.context_1;
    DEQ_REMOVE(tcp_adaptor->connections, conn);
    qd_log(tcp_adaptor->log_source, QD_LOG_DEBUG, "Removed tcp connection %s (%i)", conn->config.host_port, DEQ_SIZE(tcp_adaptor->connections));
    free_qdr_tcp_connection(conn);
}

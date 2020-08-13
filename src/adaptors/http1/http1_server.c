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

#include "http1_private.h"
#include "adaptors/adaptor_utils.h"

#include <proton/proactor.h>

#define DEFAULT_CAPACITY 250
#define RETRY_PAUSE_MSEC 500
#define MAX_RECONNECT    5  // 5 * 500 = 2.5 sec

static void _server_tx_buffers_cb(h1_codec_request_state_t *lib_hrs, qd_buffer_list_t *blist, unsigned int len);
static void _server_tx_body_data_cb(h1_codec_request_state_t *lib_hrs, qd_message_body_data_t *body_data);
static int  _server_rx_request_cb(h1_codec_request_state_t *hrs,
                                  const char *method,
                                  const char *target,
                                  uint32_t version_major,
                                  uint32_t version_minor);
static int  _server_rx_response_cb(h1_codec_request_state_t *hrs,
                                   int status_code,
                                   const char *reason_phrase,
                                   uint32_t version_major,
                                   uint32_t version_minor);
static int _server_rx_header_cb(h1_codec_request_state_t *hrs, const char *key, const char *value);
static int _server_rx_headers_done_cb(h1_codec_request_state_t *hrs, bool has_body);
static int _server_rx_body_cb(h1_codec_request_state_t *hrs, qd_buffer_list_t *body, size_t offset, size_t len,
                              bool more);
static void _server_rx_done_cb(h1_codec_request_state_t *hrs);
static void _server_request_complete_cb(h1_codec_request_state_t *hrs, bool cancelled);
static void _handle_connection_events(pn_event_t *e, qd_server_t *qd_server, void *context);
static void _do_reconnect(void *context);


////////////////////////////////////////////////////////
// HTTP/1.x Server Connector
////////////////////////////////////////////////////////


// An HttpConnector has been created.  Create an qdr_http_connection_t for it.
// Do not create a raw connection - this is done on demand when the router
// sends a delivery over the connector.
//
static qdr_http1_connection_t *_create_server_connection(qd_http_connector_t *ctor,
                                                         qd_dispatch_t *qd,
                                                         const qd_http_bridge_config_t *bconfig)
{
    qdr_http1_connection_t *hconn = new_qdr_http1_connection_t();

    ZERO(hconn);
    hconn->type = HTTP1_CONN_SERVER;
    hconn->qd_server = qd->server;
    hconn->adaptor = qdr_http1_adaptor;
    hconn->handler_context.handler = &_handle_connection_events;
    hconn->handler_context.context = hconn;
    hconn->cfg.host = qd_strdup(bconfig->host);
    hconn->cfg.port = qd_strdup(bconfig->port);
    hconn->cfg.address = qd_strdup(bconfig->address);
    hconn->cfg.host_port = qd_strdup(bconfig->host_port);

    // for initiating a connection to the server
    hconn->server.reconnect_timer = qd_timer(qdr_http1_adaptor->core->qd, _do_reconnect, hconn);

    // Create the qdr_connection

    qdr_connection_info_t *info = qdr_connection_info(false, //bool             is_encrypted,
                                                      false, //bool             is_authenticated,
                                                      true,  //bool             opened,
                                                      "",   //char            *sasl_mechanisms,
                                                      QD_OUTGOING, //qd_direction_t   dir,
                                                      hconn->cfg.host_port,    //const char      *host,
                                                      "",    //const char      *ssl_proto,
                                                      "",    //const char      *ssl_cipher,
                                                      "",    //const char      *user,
                                                      "HTTP/1.x Adaptor",    //const char      *container,
                                                      pn_data(0),     //pn_data_t       *connection_properties,
                                                      0,     //int              ssl_ssf,
                                                      false, //bool             ssl,
                                                      // set if remote is a qdrouter
                                                      0);    //const qdr_router_version_t *version)

    hconn->conn_id = qd_server_allocate_connection_id(hconn->qd_server);
    hconn->qdr_conn = qdr_connection_opened(qdr_http1_adaptor->core,
                                            qdr_http1_adaptor->adaptor,
                                            false,  // incoming
                                            QDR_ROLE_NORMAL,
                                            1,      // cost
                                            hconn->conn_id,
                                            0,  // label
                                            0,  // remote container id
                                            false,  // strip annotations in
                                            false,  // strip annotations out
                                            false,  // allow dynamic link routes
                                            false,  // allow admin status update
                                            DEFAULT_CAPACITY,
                                            0,      // vhost
                                            info,
                                            0,      // bind context
                                            0);     // bind token
    qdr_connection_set_context(hconn->qdr_conn, hconn);

    qd_log(hconn->adaptor->log, QD_LOG_DEBUG, "[C%i] HTTP connection to server created", hconn->conn_id);

    // wait for the raw connection to come up before creating the in and out links

    hconn->raw_conn = pn_raw_connection();
    pn_raw_connection_set_context(hconn->raw_conn, &hconn->handler_context);

    return hconn;
}


// Management Agent API - Create
//
qd_http_connector_t *qd_http1_configure_connector(qd_dispatch_t *qd, const qd_http_bridge_config_t *config, qd_entity_t *entity)
{
    qd_http_connector_t *c = qd_http_connector(qd->server);
    if (!c) {
        qd_log(qdr_http1_adaptor->log, QD_LOG_ERROR, "Unable to create http connector: no memory");
        return 0;
    }

    DEQ_ITEM_INIT(c);
    qdr_http1_connection_t *hconn = _create_server_connection(c, qd, config);
    if (hconn) {
        DEQ_INSERT_TAIL(qdr_http1_adaptor->connectors, c);
        // activate the raw connection. This connection may be scheduled on
        // another thread by this call:
        qd_log(qdr_http1_adaptor->log, QD_LOG_DEBUG,
               "[C%"PRIu64"] Initiating connection to HTTP server %s",
               hconn->conn_id, hconn->cfg.host_port);
        pn_proactor_raw_connect(qd_server_proactor(hconn->qd_server), hconn->raw_conn, hconn->cfg.host_port);
        return c;
    } else {
        qd_http_connector_decref(c);
        c = 0;
    }

    return c;
}


// Management Agent API - Delete
//
void qd_http1_delete_connector(qd_dispatch_t *qd, qd_http_connector_t *ct)
{
    if (ct) {
        qd_log(qdr_http1_adaptor->log, QD_LOG_INFO, "Deleted HttpConnector for %s, %s:%s", ct->config.address, ct->config.host, ct->config.port);
        DEQ_REMOVE(qdr_http1_adaptor->connectors, ct);
        qd_http_connector_decref(ct);

        // TODO(kgiusti): do we now close all related connections?
    }
}




////////////////////////////////////////////////////////
// Raw Connector Events
////////////////////////////////////////////////////////


// Create the qdr links and HTTP codec when the server connection comes up.
// These links & codec will persist across temporary drops in the connection to
// the server (like when closing the connection to indicate end of response
// message).  However if the connection to the server cannot be re-established
// in a "reasonable" amount of time we consider the server unavailable and
// these links and codec will be closed - aborting any pending requests.  Once
// the connection to the server is reestablished these links & codec will be
// recreated.
//
static void _setup_server_links(qdr_http1_connection_t *hconn)
{
    if (!hconn->in_link) {
        // simulate an anonymous link for responses from the server
        hconn->in_link = qdr_link_first_attach(hconn->qdr_conn,
                                               QD_INCOMING,
                                               qdr_terminus(0),  //qdr_terminus_t   *source,
                                               qdr_terminus(0),  //qdr_terminus_t   *target
                                               "http1.server.in",  //const char       *name,
                                               0,                //const char       *terminus_addr,
                                               false,
                                               NULL,
                                               &(hconn->in_link_id));
        qdr_link_set_context(hconn->in_link, hconn);

        qd_log(hconn->adaptor->log, QD_LOG_DEBUG,
               "[C%"PRIu64"][L%"PRIu64"] HTTP server response link created",
               hconn->conn_id, hconn->in_link_id);
    }

    if (!hconn->out_link) {
        // simulate a server subscription for its service address
        qdr_terminus_t *source = qdr_terminus(0);
        qdr_terminus_set_address(source, hconn->cfg.address);
        hconn->out_link = qdr_link_first_attach(hconn->qdr_conn,
                                                QD_OUTGOING,
                                                source,           //qdr_terminus_t   *source,
                                                qdr_terminus(0),  //qdr_terminus_t   *target,
                                                "http1.server.out", //const char       *name,
                                                0,                //const char       *terminus_addr,
                                                false,
                                                0,      // initial delivery
                                                &(hconn->out_link_id));
        qdr_link_set_context(hconn->out_link, hconn);

        hconn->out_link_credit = DEFAULT_CAPACITY;
        qdr_link_flow(hconn->adaptor->core, hconn->out_link, DEFAULT_CAPACITY, false);

        qd_log(hconn->adaptor->log, QD_LOG_DEBUG,
               "[C%"PRIu64"][L%"PRIu64"] HTTP server request link created",
               hconn->conn_id, hconn->out_link_id);
    }

    if (!hconn->http_conn) {
        h1_codec_config_t config = {0};
        config.type             = HTTP1_CONN_SERVER;
        config.tx_buffers       = _server_tx_buffers_cb;
        config.tx_body_data     = _server_tx_body_data_cb;
        config.rx_request       = _server_rx_request_cb;
        config.rx_response      = _server_rx_response_cb;
        config.rx_header        = _server_rx_header_cb;
        config.rx_headers_done  = _server_rx_headers_done_cb;
        config.rx_body          = _server_rx_body_cb;
        config.rx_done          = _server_rx_done_cb;
        config.request_complete = _server_request_complete_cb;
        hconn->http_conn = h1_codec_connection(&config, hconn);
    }
}


// Tear down the qdr links and the codec.  This is called when the
// connection to the server has dropped and cannot be re-established in a
// timely manner.
//
static void _teardown_server_links(qdr_http1_connection_t *hconn)
{
    // @TODO(kgiusti): should we PN_RELEASE all unsent outbound deliveries first?
    qdr_http1_request_t *hreq = DEQ_HEAD(hconn->requests);
    while (hreq) {
        qdr_http1_request_free(hreq);
        hreq = DEQ_HEAD(hconn->requests);
    }
    h1_codec_connection_free(hconn->http_conn);
    hconn->http_conn = 0;

    if (hconn->out_link) {
        qdr_link_set_context(hconn->out_link, 0);
        qdr_link_detach(hconn->out_link, QD_CLOSED, 0);
        hconn->out_link = 0;
    }

    if (hconn->in_link) {
        qdr_link_set_context(hconn->in_link, 0);
        qdr_link_detach(hconn->in_link, QD_CLOSED, 0);
        hconn->in_link = 0;
    }
}


// This adapter attempts to keep the connection to the server up as long as the
// connector is configured.  This is called via a timer scheduled when the
// PN_CONNECTION_CLOSE event is handled.
//
static void _do_reconnect(void *context)
{
    qdr_http1_connection_t *hconn = (qdr_http1_connection_t*) context;
    if (!hconn->raw_conn) {
        qd_log(qdr_http1_adaptor->log, QD_LOG_DEBUG, "[C%"PRIu64"] Connecting to HTTP server...", hconn->conn_id);
        hconn->raw_conn = pn_raw_connection();
        pn_raw_connection_set_context(hconn->raw_conn, &hconn->handler_context);
        // this call may reschedule the connection on another I/O thread:
        pn_proactor_raw_connect(qd_server_proactor(hconn->qd_server), hconn->raw_conn, hconn->cfg.host_port);
    }
}


// Proton Raw Connection Events
//
static void _handle_connection_events(pn_event_t *e, qd_server_t *qd_server, void *context)
{
    qdr_http1_connection_t *hconn = (qdr_http1_connection_t*) context;
    qd_log_source_t *log = qdr_http1_adaptor->log;

    qd_log(log, QD_LOG_DEBUG, "RAW CONNECTION EVENT %s\n", pn_event_type_name(pn_event_type(e)));

    if (!hconn) return;

    switch (pn_event_type(e)) {

    case PN_RAW_CONNECTION_CONNECTED: {
        hconn->server.reconnect_count = 0;
        _setup_server_links(hconn);
        // send any pending requests to the server
        qdr_http1_write_out_data(hconn);
        while (qdr_connection_process(hconn->qdr_conn)) {}
        break;
    }
    case PN_RAW_CONNECTION_CLOSED_READ: {
        qdr_http1_request_t *hreq = DEQ_HEAD(hconn->requests);
        if (hreq && hreq->lib_rs) {
            // notify the codec so it can complete the current request
            h1_codec_connection_closed(hconn->http_conn);
            if (!hreq->codec_completed)
                // unable to complete - cancel it
                h1_codec_request_state_cancel(hreq->lib_rs);
        }
    }
    // fall through
    case PN_RAW_CONNECTION_CLOSED_WRITE: {
        qd_log(log, QD_LOG_DEBUG, "[C%i] Closed for %s", hconn->conn_id,
               pn_event_type(e) == PN_RAW_CONNECTION_CLOSED_READ
               ? "reading" : "writing");
        pn_raw_connection_close(hconn->raw_conn);
        break;
    }
    case PN_RAW_CONNECTION_DISCONNECTED: {
        pn_raw_connection_set_context(hconn->raw_conn, 0);
        hconn->raw_conn = 0;
        if (!hconn->qdr_conn) {
            // the router has closed this connection so do not try to
            // re-establish it
            qd_log(log, QD_LOG_INFO, "[C%i] Connection closed", hconn->conn_id);
            qdr_http1_connection_free(hconn);
            return;
        }

        // reconnect to the server. Leave the links intact so pending requests
        // are not aborted.  Once we've failed to reconnect after MAX_RECONNECT
        // tries drop the links to prevent additional request from arriving.
        //
        qd_duration_t nap_time = RETRY_PAUSE_MSEC * hconn->server.reconnect_count;
        if (hconn->server.reconnect_count == MAX_RECONNECT) {
            qd_log(log, QD_LOG_INFO, "[C%i] Server not responding - disconnecting...", hconn->conn_id);
            _teardown_server_links(hconn);
        } else {
            hconn->server.reconnect_count += 1;  // increase next sleep interval
        }
        qd_timer_schedule(hconn->server.reconnect_timer, nap_time);
        return;
    }
    case PN_RAW_CONNECTION_NEED_WRITE_BUFFERS: {
        qd_log(log, QD_LOG_DEBUG, "[C%"PRIu64"] Need write buffers", hconn->conn_id);
        int written = qdr_http1_write_out_data(hconn);
        if (written)
            qd_log(log, QD_LOG_DEBUG, "[C%"PRIu64"] %d buffers written",
                   hconn->conn_id, written);
        break;
    }
    case PN_RAW_CONNECTION_NEED_READ_BUFFERS: {
        qd_log(log, QD_LOG_DEBUG, "[C%i] Need read buffers", hconn->conn_id);
        // @TODO(kgiusti): backpressure if no credit
        // if (hconn->in_link_credit > 0 */)
        int granted = qda_raw_conn_grant_read_buffers(hconn->raw_conn);
        qd_log(log, QD_LOG_DEBUG, "[C%"PRIu64"] %d read buffers granted",
               hconn->conn_id, granted);
        break;
    }
    case PN_RAW_CONNECTION_WAKE: {
        qd_log(log, QD_LOG_DEBUG, "[C%i] Wake-up", hconn->conn_id);
        while (qdr_connection_process(hconn->qdr_conn)) {}
        qd_log(log, QD_LOG_DEBUG, "[C%i] Connection processing complete", hconn->conn_id);
        break;
    }
    case PN_RAW_CONNECTION_READ: {
        qd_buffer_list_t blist;
        uintmax_t length;
        qda_raw_conn_get_read_buffers(hconn->raw_conn, &blist, &length);

        if (0) {
            // remove this
            fprintf(stdout, "\nServer raw buffer READ %"PRIuMAX" total octets\n", length);
            qd_buffer_t *bb = DEQ_HEAD(blist);
            while (bb) {
                fprintf(stdout, "  buffer='%.*s'\n", (int)qd_buffer_size(bb), (char*)&bb[1]);
                bb = DEQ_NEXT(bb);
            }
            fflush(stdout);
        }

        if (length) {
            qd_log(log, QD_LOG_DEBUG, "[C%"PRIu64"][L%"PRIu64"] Read %"PRIuMAX" bytes from server",
                   hconn->conn_id, hconn->in_link_id, length);
            hconn->in_http1_octets += length;
            int error = h1_codec_connection_rx_data(hconn->http_conn, &blist, length);
            if (error)
                qdr_http1_close_connection(hconn, "Incoming response message failed to parse");
        }
        break;
    }
    case PN_RAW_CONNECTION_WRITTEN: {
        qdr_http1_free_written_buffers(hconn);
        break;
    }
    default:
        break;
    }

    // Is the current request done?
    //

    // remove me:
    if (hconn) {
        qdr_http1_request_t *hreq = DEQ_HEAD(hconn->requests);
        if (hreq) {
            qd_log(log, QD_LOG_DEBUG, "[C%"PRIu64"] HTTP is server request complete????", hconn->conn_id);
            qd_log(log, QD_LOG_DEBUG, "   codec=%s req-dlv=%p resp-dlv=%d req_msg=%p %s",
                   hreq->codec_completed ? "Done" : "Not Done",
                   (void*)hreq->request_dlv,
                   (int)DEQ_SIZE(hreq->responses),
                   (void*)hreq->request_msg,
                   hreq->cancelled ? "Cancelled" : "Not Cancelled");
        }
    }

    qdr_http1_request_t *hreq = DEQ_HEAD(hconn->requests);
    if (hreq && (hreq->cancelled || qdr_http1_request_complete(hreq))) {
        // if cancelled with an in-progress request the protocol
        // state is unknown - reset by dropping the connection.
        bool close_me = hreq->cancelled && hreq->out_http1_octets;
        qdr_http1_request_free(hreq);
        qd_log(log, QD_LOG_DEBUG, "[C%"PRIu64"] HTTP request completed!", hconn->conn_id);
        if (close_me) {
            qd_log(log, QD_LOG_DEBUG, "[C%"PRIu64"] Closing connection!", hconn->conn_id);
            qdr_http1_close_connection(hconn, "Request failed");
        } else
            qdr_http1_write_out_data(hconn);
    }
}


//////////////////////////////////////////////////////////////////////
// HTTP/1.x Encoder/Decoder Callbacks
//////////////////////////////////////////////////////////////////////


// Encoder has a buffer list to send to the server
//
static void _server_tx_buffers_cb(h1_codec_request_state_t *hrs, qd_buffer_list_t *blist, unsigned int len)
{
    qdr_http1_request_t *hreq = (qdr_http1_request_t*) h1_codec_request_state_get_context(hrs);
    qdr_http1_connection_t *hconn = hreq->hconn;

    qd_log(qdr_http1_adaptor->log, QD_LOG_TRACE,
           "[C%"PRIu64"][L%"PRIu64"] Sending %u octets to server",
           hconn->conn_id, hconn->out_link_id, len);
    qdr_http1_write_buffer_list(hreq, blist);
}


// Encoder has body data to send to the server
//
static void _server_tx_body_data_cb(h1_codec_request_state_t *hrs, qd_message_body_data_t *body_data)
{
    qdr_http1_request_t *hreq = (qdr_http1_request_t*) h1_codec_request_state_get_context(hrs);
    qdr_http1_connection_t *hconn = hreq->hconn;

    qd_log(qdr_http1_adaptor->log, QD_LOG_TRACE,
           "[C%"PRIu64"][L%"PRIu64"] Sending body data to server",
           hconn->conn_id, hconn->out_link_id);
    qdr_http1_write_body_data(hreq, body_data);
}


// Server will not be sending us HTTP requests
//
static int _server_rx_request_cb(h1_codec_request_state_t *hrs,
                                 const char *method,
                                 const char *target,
                                 uint32_t version_major,
                                 uint32_t version_minor)
{
    qdr_http1_request_t *hreq = (qdr_http1_request_t*) h1_codec_request_state_get_context(hrs);
    qdr_http1_connection_t *hconn = hreq->hconn;

    qd_log(qdr_http1_adaptor->log, QD_LOG_ERROR,
           "[C%"PRIu64"][L%"PRIu64"] Spurious HTTP request received from server",
           hconn->conn_id, hconn->in_link_id);
    return HTTP1_STATUS_BAD_REQ;
}


// called when decoding an HTTP response from the server.
//
static int _server_rx_response_cb(h1_codec_request_state_t *hrs,
                                  int status_code,
                                  const char *reason_phrase,
                                  uint32_t version_major,
                                  uint32_t version_minor)
{
    qdr_http1_request_t *hreq = (qdr_http1_request_t*) h1_codec_request_state_get_context(hrs);
    assert(hreq && hreq == DEQ_HEAD(hreq->hconn->requests));  // expected to be in-order

    qd_log(qdr_http1_adaptor->log, QD_LOG_TRACE,
           "[C%"PRIu64"][L%"PRIu64"] HTTP response received: status=%d phrase=%s version=%"PRIi32".%"PRIi32,
           hreq->hconn->conn_id, hreq->hconn->in_link_id, status_code, reason_phrase ? reason_phrase : "<NONE>",
           version_major, version_minor);

    hreq->app_props = qd_compose(QD_PERFORMATIVE_APPLICATION_PROPERTIES, 0);
    qd_compose_start_map(hreq->app_props);
    {
        char version[64];
        snprintf(version, 64, "%"PRIi32".%"PRIi32, version_major, version_minor);
        qd_compose_insert_symbol(hreq->app_props, RESPONSE_HEADER_KEY);
        qd_compose_insert_string(hreq->app_props, version);

        qd_compose_insert_symbol(hreq->app_props, STATUS_HEADER_KEY);
        qd_compose_insert_int(hreq->app_props, (int32_t)status_code);

        if (reason_phrase) {
            qd_compose_insert_symbol(hreq->app_props, REASON_HEADER_KEY);
            qd_compose_insert_string(hreq->app_props, reason_phrase);
        }
    }

    return 0;
}


// called for each decoded HTTP header.
//
static int _server_rx_header_cb(h1_codec_request_state_t *hrs, const char *key, const char *value)
{
    qdr_http1_request_t *hreq = (qdr_http1_request_t*) h1_codec_request_state_get_context(hrs);

    qd_log(qdr_http1_adaptor->log, QD_LOG_TRACE,
           "[C%"PRIu64"]L%"PRIu64"] HTTP response header received: key='%s' value='%s'",
           hreq->hconn->conn_id, hreq->hconn->in_link_id, key, value);

    // We need to filter the connection header out
    // @TODO(kgiusti): also have to remove headers given in value!
    if (strcasecmp(key, "connection") != 0) {
        qd_compose_insert_symbol(hreq->app_props, key);
        qd_compose_insert_string(hreq->app_props, value);
    }

    return 0;
}


// called after the last header is decoded, before decoding any body data.
//
static int _server_rx_headers_done_cb(h1_codec_request_state_t *hrs, bool has_body)
{
    qdr_http1_request_t *hreq = (qdr_http1_request_t*) h1_codec_request_state_get_context(hrs);
    qdr_http1_connection_t *hconn = hreq->hconn;

    qd_log(qdr_http1_adaptor->log, QD_LOG_TRACE,
           "[C%"PRIu64"][L%"PRIu64" HTTP response headers done.",
           hconn->conn_id, hconn->in_link_id);

    // start building the AMQP message

    qdr_http1_response_msg_t *rmsg = new_qdr_http1_response_msg_t();
    ZERO(rmsg);
    rmsg->in_msg = qd_message();
    DEQ_INSERT_TAIL(hreq->responses, rmsg);

    qd_composed_field_t *hdrs = qd_compose(QD_PERFORMATIVE_HEADER, 0);
    qd_compose_start_list(hdrs);
    qd_compose_insert_bool(hdrs, 0);     // durable
    qd_compose_insert_null(hdrs);        // priority
    //qd_compose_insert_null(hdrs);        // ttl
    //qd_compose_insert_bool(hdrs, 0);     // first-acquirer
    //qd_compose_insert_uint(hdrs, 0);     // delivery-count
    qd_compose_end_list(hdrs);

    qd_composed_field_t *props = qd_compose(QD_PERFORMATIVE_PROPERTIES, hdrs);
    qd_compose_start_list(props);
    qd_compose_insert_null(props);     // message-id
    qd_compose_insert_null(props);     // user-id
    qd_compose_insert_string(props, hreq->response_addr); // to
    // subject:
    qd_compose_insert_string(props, h1_codec_request_state_method(hrs));
    qd_compose_insert_null(props);   // reply-to
    qd_compose_insert_ulong(props, hreq->msg_id);  // correlation-id
    qd_compose_end_list(props);

    qd_compose_end_map(hreq->app_props);

    if (!has_body) {
        // @TODO(kgiusti): fixme: tack on an empty body data performative.  The
        // message decoder will barf otherwise
        qd_buffer_list_t empty = DEQ_EMPTY;
        hreq->app_props = qd_compose(QD_PERFORMATIVE_BODY_DATA, hreq->app_props);
        qd_compose_insert_binary_buffers(hreq->app_props, &empty);
    }

    qd_message_compose_3(rmsg->in_msg, props, hreq->app_props, !has_body);
    qd_compose_free(props);
    qd_compose_free(hreq->app_props);
    hreq->app_props = 0;

    if (hconn->in_link_credit > 0 && rmsg == DEQ_HEAD(hreq->responses)) {
        hconn->in_link_credit -= 1;

        qd_log(hconn->adaptor->log, QD_LOG_TRACE,
               "[C%"PRIu64"][L%"PRIu64"] Delivering response to router",
               hconn->conn_id, hconn->in_link_id);

        qd_iterator_t *addr = qd_iterator_string(hreq->response_addr, ITER_VIEW_ADDRESS_HASH);
        rmsg->dlv = qdr_link_deliver_to(hconn->in_link, rmsg->in_msg, 0, addr, false, 0, 0, 0, 0);
        qdr_delivery_set_context(rmsg->dlv, (void*) hreq);
        qdr_delivery_incref(rmsg->dlv, "referenced by HTTP1 adaptor");
        rmsg->in_msg = 0;
    }

    return 0;
}


// Called with decoded body data.  This may be called multiple times as body
// data becomes available.
//
static int _server_rx_body_cb(h1_codec_request_state_t *hrs, qd_buffer_list_t *body, size_t offset, size_t len,
                              bool more)
{
    qdr_http1_request_t *hreq = (qdr_http1_request_t*) h1_codec_request_state_get_context(hrs);
    qdr_http1_connection_t *hconn = hreq->hconn;
    qdr_http1_response_msg_t *rmsg = DEQ_TAIL(hreq->responses);
    qd_message_t *msg = rmsg->in_msg ? rmsg->in_msg : qdr_delivery_message(rmsg->dlv);

    qd_log(qdr_http1_adaptor->log, QD_LOG_TRACE,
           "[C%"PRIu64"][L%"PRIu64"] HTTP response body received len=%zu.",
           hconn->conn_id, hconn->in_link_id, len);

    if (offset) {
        // dispatch assumes all body data starts at the buffer base so it cannot deal with offsets.
        // Remove the offset by shifting the content of the head buffer forward
        //
        qd_buffer_t *head = DEQ_HEAD(*body);
        memmove(qd_buffer_base(head), qd_buffer_base(head) + offset, qd_buffer_size(head) - offset);
        head->size -= offset;
    }

    if (0) {
        // remove this
        qd_buffer_t *bb = DEQ_HEAD(*body);
        fprintf(stdout, "\n\nServer rx_body_cb sending %zu octets\n", len);
        size_t actual = 0;
        while (bb) {
            fprintf(stdout, " buffer: len=%d\n  value='%.*s'\n",
                    (int)qd_buffer_size(bb), (int)qd_buffer_size(bb), (char*)&bb[1]);
            actual += qd_buffer_size(bb);
            bb = DEQ_NEXT(bb);
        }
        fprintf(stdout, "Server body buffer done, actual=%zu\n\n", actual);
        fflush(stdout);
    }

    //
    // Compose a DATA performative for this section of the stream
    //
    qd_composed_field_t *field = qd_compose(QD_PERFORMATIVE_BODY_DATA, 0);
    qd_compose_insert_binary_buffers(field, body);

    //
    // Extend the streaming message and free the composed field
    //
    qd_message_extend(msg, field);
    qd_compose_free(field);

    //
    // Notify the router that more data is ready to be pushed out on the delivery
    //
    if (!more)
        qd_message_set_receive_complete(msg);

    if (rmsg->dlv)
        qdr_delivery_continue(qdr_http1_adaptor->core, rmsg->dlv, false);

    return 0;
}

// Called at the completion of response decoding.
//
static void _server_rx_done_cb(h1_codec_request_state_t *hrs)
{
    qdr_http1_request_t *hreq = (qdr_http1_request_t*) h1_codec_request_state_get_context(hrs);
    qdr_http1_connection_t *hconn = hreq->hconn;
    qdr_http1_response_msg_t *rmsg = DEQ_TAIL(hreq->responses);
    qd_message_t *msg = rmsg->in_msg ? rmsg->in_msg : qdr_delivery_message(rmsg->dlv);

    qd_log(qdr_http1_adaptor->log, QD_LOG_TRACE,
           "[C%"PRIu64"][L%"PRIu64"] HTTP response receive complete.",
           hconn->conn_id, hconn->in_link_id);

    if (!qd_message_receive_complete(msg)) {
        qd_message_set_receive_complete(msg);
        if (rmsg->dlv) {
            qdr_delivery_continue(qdr_http1_adaptor->core, rmsg->dlv, false);
        }
    }
}


// called at the completion of a full Request/Response exchange.  The hrs will
// be deleted on return from this call.  Any hrs related state must be
// released before returning from this callback.
//
static void _server_request_complete_cb(h1_codec_request_state_t *hrs, bool cancelled)
{
    qdr_http1_request_t *hreq = (qdr_http1_request_t*) h1_codec_request_state_get_context(hrs);

    if (hreq) {
        qdr_http1_connection_t *hconn = hreq->hconn;

        hreq->lib_rs = 0;
        hreq->codec_completed = true;
        hreq->cancelled = hreq->cancelled || cancelled;

        qd_log(qdr_http1_adaptor->log, QD_LOG_TRACE,
               "[C%"PRIu64"] HTTP request/response %s.", hconn->conn_id,
               cancelled ? "cancelled!" : "codec done");

        if (!cancelled) {
            // The response has been completely received from the server. Now
            // settle the request_dlv
            if (hreq->request_dlv) {
                // can set the disposition and settle the request delivery now
                // that the response message has been completed.

                qd_log(qdr_http1_adaptor->log, QD_LOG_TRACE,
                       "[C%"PRIu64"][L%"PRIu64"] HTTP server settling request, dispo=0x%"PRIx64,
                       hconn->conn_id, hconn->out_link_id, hreq->out_dispo);

                qdr_delivery_remote_state_updated(qdr_http1_adaptor->core,
                                                  hreq->request_dlv,
                                                  hreq->out_dispo,
                                                  true,   // settled
                                                  0,      // error
                                                  0,      // dispo data
                                                  false);
                qdr_delivery_set_context(hreq->request_dlv, 0);
                qdr_delivery_decref(qdr_http1_adaptor->core, hreq->request_dlv, "HTTP1 adaptor request settled");
                hreq->request_dlv = 0;
            }
        }
    }
}


//////////////////////////////////////////////////////////////////////
// Router Protocol Adapter Callbacks
//////////////////////////////////////////////////////////////////////


// credit has been granted - responses may now be sent to the
// router core.
//
void qdr_http1_server_core_link_flow(qdr_http1_adaptor_t    *adaptor,
                                     qdr_http1_connection_t *hconn,
                                     qdr_link_t             *link,
                                     int                     credit)
{
    assert(link == hconn->in_link);   // router only grants flow on incoming link

    assert(qdr_link_is_anonymous(link));  // remove me
    hconn->in_link_credit += credit;

    qd_log(adaptor->log, QD_LOG_TRACE,
           "[C%"PRIu64"][L%"PRIu64"] Credit granted on response link: %d",
           hconn->conn_id, hconn->in_link_id, hconn->in_link_credit);

    if (hconn->in_link_credit > 0) {

        if (hconn->raw_conn)
            qda_raw_conn_grant_read_buffers(hconn->raw_conn);

        // is the current response message blocked by lack of credit?

        qdr_http1_request_t *hreq = DEQ_HEAD(hconn->requests);
        if (hreq) {
            qdr_http1_response_msg_t *rmsg = DEQ_HEAD(hreq->responses);
            if (rmsg && rmsg->in_msg) {

                assert(!rmsg->dlv);
                hconn->in_link_credit -= 1;

                qd_log(adaptor->log, QD_LOG_TRACE,
                       "[C%"PRIu64"][L%"PRIu64"] Delivering response to router",
                       hconn->conn_id, hconn->in_link_id);

                qd_iterator_t *addr = qd_iterator_string(hreq->response_addr, ITER_VIEW_ADDRESS_HASH);
                rmsg->dlv = qdr_link_deliver_to(hconn->in_link, rmsg->in_msg, 0, addr, false, 0, 0, 0, 0);
                qdr_delivery_set_context(rmsg->dlv, (void*) hreq);
                qdr_delivery_incref(rmsg->dlv, "referenced by HTTP1 adaptor");
                rmsg->in_msg = 0;
            }
        }
    }
}


// Handle disposition/settlement update for the outstanding HTTP response.
// Regardless of error now is when the response delivery can be
// settled
//
void qdr_http1_server_core_delivery_update(qdr_http1_adaptor_t    *adaptor,
                                           qdr_http1_connection_t *hconn,
                                           qdr_http1_request_t    *hreq,
                                           qdr_delivery_t         *dlv,
                                           uint64_t                disp,
                                           bool                    settled)
{
    qdr_http1_response_msg_t *rmsg = DEQ_HEAD(hreq->responses);
    assert(rmsg && rmsg->dlv == dlv);  // only one response outstanding

    qd_log(qdr_http1_adaptor->log, QD_LOG_TRACE,
           "[C%"PRIu64"][L%"PRIu64"] HTTP response delivery update, outcome=0x%"PRIx64"%s",
           hconn->conn_id, hconn->in_link_id, disp, settled ? " settled": "");

    if (disp && disp != PN_RECEIVED) {
        // terminal disposition
        rmsg->dispo = disp;
        if (disp != PN_ACCEPTED) {
            qd_log(adaptor->log, QD_LOG_WARNING,
                   "[C%"PRIu64"][L%"PRIu64"] response message not received, outcome=0x%"PRIx64,
                   hconn->conn_id, hconn->in_link_id, disp);
        }
    }

    if (settled) {
        DEQ_REMOVE(hreq->responses, rmsg);
        qdr_delivery_set_context(rmsg->dlv, 0);
        qdr_delivery_decref(qdr_http1_adaptor->core, rmsg->dlv, "HTTP1 adaptor response settled");
        free_qdr_http1_response_msg_t(rmsg);
        if (hconn->in_link_credit > 0 &&
            (rmsg = DEQ_HEAD(hreq->responses)) != 0 &&
            rmsg->in_msg) {

            assert(!rmsg->dlv);
            hconn->in_link_credit -= 1;

            qd_log(adaptor->log, QD_LOG_TRACE,
                   "[C%"PRIu64"][L%"PRIu64"] Delivering response to router",
                   hconn->conn_id, hconn->in_link_id);

            qd_iterator_t *addr = qd_iterator_string(hreq->response_addr, ITER_VIEW_ADDRESS_HASH);
            rmsg->dlv = qdr_link_deliver_to(hconn->in_link, rmsg->in_msg, 0, addr, false, 0, 0, 0, 0);
            qdr_delivery_set_context(rmsg->dlv, (void*) hreq);
            qdr_delivery_incref(rmsg->dlv, "referenced by HTTP1 adaptor");
            rmsg->in_msg = 0;
        }
    }
}


//
// Request message forwarding
//


// Create a request context for a new request in msg, which is valid to a depth
// of at least QD_DEPTH_PROPERTIES
//
static qdr_http1_request_t *_create_request_context(qdr_http1_connection_t *hconn, qd_message_t *msg)
{
    uint64_t msg_id = 0;
    char *reply_to = 0;
    bool ok = false;
    qd_parsed_field_t *msg_id_pf = 0;

    qd_iterator_t *msg_id_itr = qd_message_field_iterator_typed(msg, QD_FIELD_MESSAGE_ID);  // ulong
    if (msg_id_itr) {
        msg_id_pf = qd_parse(msg_id_itr);
        if (msg_id_pf && qd_parse_ok(msg_id_pf)) {
            msg_id = qd_parse_as_ulong(msg_id_pf);
            ok = qd_parse_ok(msg_id_pf);
        }
    }
    qd_parse_free(msg_id_pf);
    qd_iterator_free(msg_id_itr);

    if (!ok) {
        qd_log(qdr_http1_adaptor->log, QD_LOG_WARNING,
               "[C%"PRIu64"][L%"PRIu64"] Rejecting message missing id.",
               hconn->conn_id, hconn->out_link_id);
        return 0;
    }

    qd_iterator_t *reply_to_itr = qd_message_field_iterator(msg, QD_FIELD_REPLY_TO);
    reply_to = (char*) qd_iterator_copy(reply_to_itr);
    qd_iterator_free(reply_to_itr);

    assert(reply_to && strlen(reply_to));  // remove me
    if (!reply_to) {
        qd_log(qdr_http1_adaptor->log, QD_LOG_WARNING,
               "[C%"PRIu64"][L%"PRIu64"] Rejecting message no reply-to.",
               hconn->conn_id, hconn->out_link_id);
        return 0;
    }

    qdr_http1_request_t *hreq = new_qdr_http1_request_t();
    ZERO(hreq);
    hreq->hconn = hconn;
    hreq->msg_id = msg_id;
    hreq->response_addr = reply_to;
    DEQ_INIT(hreq->out_fifo);
    DEQ_INSERT_TAIL(hconn->requests, hreq);

    qd_log(qdr_http1_adaptor->log, QD_LOG_TRACE,
           "[C%"PRIu64"][L%"PRIu64"] New HTTP Request msg_id=%"PRIu64" reply-to=%s.",
           hconn->conn_id, hconn->out_link_id, msg_id, reply_to);
    return hreq;
}


// Start a new request to the server.  msg has been validated to at least
// application properties depth.  Returns 0 on success.
//
static uint64_t _send_request_headers(qdr_http1_request_t *hreq, qd_message_t *msg)
{
    // start encoding HTTP request.  Need method, target and version

    char *method_str = 0;
    char *target_str = 0;
    qd_parsed_field_t *app_props = 0;
    uint32_t major = 1;
    uint32_t minor = 1;
    uint64_t outcome = 0;

    assert(!hreq->lib_rs);

    // method is passed in the SUBJECT field
    qd_iterator_t *method_iter = qd_message_field_iterator(msg, QD_FIELD_SUBJECT);
    if (!method_iter) {
        return PN_REJECTED;
    }

    method_str = (char*) qd_iterator_copy(method_iter);
    qd_iterator_free(method_iter);
    if (!method_str) {
        return PN_REJECTED;
    }

    // target, version info and other headers are in the app properties
    qd_iterator_t *app_props_iter = qd_message_field_iterator(msg, QD_FIELD_APPLICATION_PROPERTIES);
    if (!app_props_iter) {
        outcome = PN_REJECTED;
        goto exit;
    }

    app_props = qd_parse(app_props_iter);
    qd_iterator_free(app_props_iter);
    if (!app_props) {
        outcome = PN_REJECTED;
        goto exit;
    }

    qd_parsed_field_t *ref = qd_parse_value_by_key(app_props, TARGET_HEADER_KEY);
    target_str = (char*) qd_iterator_copy(qd_parse_raw(ref));
    if (!target_str) {
        outcome = PN_REJECTED;
        goto exit;
    }


    // Pull the version info from the app properties (e.g. "1.1")
    ref = qd_parse_value_by_key(app_props, REQUEST_HEADER_KEY);
    if (ref) {  // optional
        char *version_str = (char*) qd_iterator_copy(qd_parse_raw(ref));
        if (version_str)
            sscanf(version_str, "%"SCNu32".%"SCNu32, &major, &minor);
        free(version_str);
    }

    // done copying and converting!

    qd_log(hreq->hconn->adaptor->log, QD_LOG_TRACE,
           "[C%"PRIu64"][L%"PRIu64"] Encoding request method=%s target=%s",
           hreq->hconn->conn_id, hreq->hconn->out_link_id, method_str, target_str);

    hreq->lib_rs = h1_codec_tx_request(hreq->hconn->http_conn, method_str, target_str, major, minor);
    if (!hreq->lib_rs) {
        outcome = PN_REJECTED;
        goto exit;
    }

    h1_codec_request_state_set_context(hreq->lib_rs, (void*) hreq);

    // now send all headers in app properties
    qd_parsed_field_t *key = qd_field_first_child(app_props);
    bool ok = true;
    while (ok && key) {
        qd_parsed_field_t *value = qd_field_next_child(key);
        if (!value)
            break;

        qd_iterator_t *i_key = qd_parse_raw(key);
        if (!i_key)
            break;

        // ignore the special headers added by the mapping
        if (!qd_iterator_prefix(i_key, HTTP1_HEADER_PREFIX)) {
            qd_iterator_t *i_value = qd_parse_raw(value);
            if (!i_value)
                break;

            char *header_key = (char*) qd_iterator_copy(i_key);
            char *header_value = (char*) qd_iterator_copy(i_value);

            qd_log(hreq->hconn->adaptor->log, QD_LOG_TRACE,
                   "[C%"PRIu64"][L%"PRIu64"] Encoding request header %s:%s",
                   hreq->hconn->conn_id, hreq->hconn->out_link_id,
                   header_key, header_value);

            ok = !h1_codec_tx_add_header(hreq->lib_rs, header_key, header_value);

            free(header_key);
            free(header_value);
        }

        key = qd_field_next_child(value);
    }

    if (!ok)
        outcome = PN_REJECTED;

exit:

    free(method_str);
    free(target_str);
    qd_parse_free(app_props);

    return outcome;
}


// Encode an outbound AMQP message as an HTTP Request.  Returns PN_ACCEPTED
// when complete, 0 if incomplete and PN_REJECTED if encoding error.
//
static uint64_t _encode_request_message(qdr_http1_request_t *hreq)
{
    qdr_http1_connection_t *hconn = hreq->hconn;
    qd_message_t *msg = qdr_delivery_message(hreq->request_dlv);
    qd_message_depth_status_t status = qd_message_check_depth(msg, QD_DEPTH_BODY);

    if (status == QD_MESSAGE_DEPTH_INCOMPLETE)
        return 0;

    if (status == QD_MESSAGE_DEPTH_INVALID) {
        qd_log(qdr_http1_adaptor->log, QD_LOG_WARNING,
               "[C%"PRIu64"][L%"PRIu64"] body data depth check failed",
               hconn->conn_id, hconn->out_link_id);
        return PN_REJECTED;
    }

    assert(status == QD_MESSAGE_DEPTH_OK);

    if (!hreq->headers_sent) {
        uint64_t outcome = _send_request_headers(hreq, msg);
        if (outcome) {
            qd_log(qdr_http1_adaptor->log, QD_LOG_WARNING,
                   "[C%"PRIu64"][L%"PRIu64"] Rejecting malformed message.", hconn->conn_id, hconn->out_link_id);
            return outcome;
        }
        hreq->headers_sent = true;
    }

    qd_message_body_data_t *body_data = 0;

    while (true) {
        switch (qd_message_next_body_data(msg, &body_data)) {
        case QD_MESSAGE_BODY_DATA_OK: {

            qd_log(hconn->adaptor->log, QD_LOG_TRACE,
                   "[C%"PRIu64"][L%"PRIu64"] Encoding request body data",
                   hreq->hconn->conn_id, hreq->hconn->out_link_id);

            if (h1_codec_tx_body(hreq->lib_rs, body_data)) {
                qd_log(qdr_http1_adaptor->log, QD_LOG_WARNING,
                       "[C%"PRIu64"][L%"PRIu64"] body data encode failed",
                       hconn->conn_id, hconn->out_link_id);
                return PN_REJECTED;
            }
            break;
        }

        case QD_MESSAGE_BODY_DATA_NO_MORE:
            // indicate this message is complete
            qd_log(qdr_http1_adaptor->log, QD_LOG_DEBUG,
                   "[C%"PRIu64"][L%"PRIu64"] request message encoding completed",
                   hconn->conn_id, hconn->out_link_id);
            return PN_ACCEPTED;

        case QD_MESSAGE_BODY_DATA_INCOMPLETE:
            qd_log(qdr_http1_adaptor->log, QD_LOG_DEBUG,
                   "[C%"PRIu64"][L%"PRIu64"] body data need more",
                   hconn->conn_id, hconn->out_link_id);
            return 0;  // wait for more

        case QD_MESSAGE_BODY_DATA_INVALID:
        case QD_MESSAGE_BODY_DATA_NOT_DATA:
            qd_log(qdr_http1_adaptor->log, QD_LOG_WARNING,
                   "[C%"PRIu64"][L%"PRIu64"] Rejecting corrupted body data.",
                   hconn->conn_id, hconn->out_link_id);
            return PN_REJECTED;
        }
    }
}


// The I/O thread wants to send this delivery out the link. This is either the
// start of a new incoming HTTP request or the continuation of an existing one.
// Note: returning a non-zero value will cause the delivery to be settled!
//
uint64_t qdr_http1_server_core_link_deliver(qdr_http1_adaptor_t    *adaptor,
                                            qdr_http1_connection_t *hconn,
                                            qdr_link_t             *link,
                                            qdr_delivery_t         *delivery,
                                            bool                    settled)
{
    qd_message_t        *msg = qdr_delivery_message(delivery);
    qdr_http1_request_t *hreq = (qdr_http1_request_t*) qdr_delivery_get_context(delivery);
    if (!hreq) {
        // new delivery - create new request:
        switch (qd_message_check_depth(msg, QD_DEPTH_PROPERTIES)) {
        case QD_MESSAGE_DEPTH_INCOMPLETE:
            return 0;

        case QD_MESSAGE_DEPTH_INVALID:
            qd_log(qdr_http1_adaptor->log, QD_LOG_WARNING,
                   "[C%"PRIu64"][L%"PRIu64"] Malformed HTTP/1.x message",
                   hconn->conn_id, link->identity);
            qd_message_set_send_complete(msg);
            qdr_link_flow(qdr_http1_adaptor->core, link, 1, false);
            return PN_REJECTED;

        case QD_MESSAGE_DEPTH_OK:
            hreq = _create_request_context(hconn, msg);
            if (!hreq) {
                qd_log(qdr_http1_adaptor->log, QD_LOG_WARNING,
                       "[C%"PRIu64"][L%"PRIu64"] Discarding malformed message.", hconn->conn_id, link->identity);
                qd_message_set_send_complete(msg);
                qdr_link_flow(qdr_http1_adaptor->core, link, 1, false);
                return PN_REJECTED;
            }

            hreq->request_dlv = delivery;
            qdr_delivery_set_context(delivery, (void*) hreq);
            qdr_delivery_incref(delivery, "referenced by HTTP1 adaptor");
            break;
        }
    }

    hreq->request_dispo = _encode_request_message(hreq);
    if (hreq->request_dispo == PN_ACCEPTED) {
        bool ignore;
        qd_message_set_send_complete(msg);
        qdr_link_flow(qdr_http1_adaptor->core, link, 1, false);
        h1_codec_tx_done(hreq->lib_rs, &ignore);

        qd_log(qdr_http1_adaptor->log, QD_LOG_TRACE,
               "[C%"PRIu64"][L%"PRIu64"] HTTP server update request dispo=0x%"PRIx64,
               hconn->conn_id, hreq->hconn->out_link_id, hreq->out_dispo);

        qdr_delivery_remote_state_updated(qdr_http1_adaptor->core,
                                          hreq->request_dlv,
                                          hreq->request_dispo,
                                          false,   // settled
                                          0,      // error
                                          0,      // dispo data
                                          false);
    } else if (hreq->request_dispo) { // the request message is bad.  Cancel it.

        // returning a non-zero value will cause the delivery to be settled, so
        // drop our reference.
        qdr_delivery_set_context(hreq->request_dlv, 0);
        qdr_delivery_decref(qdr_http1_adaptor->core, hreq->request_dlv, "HTTP1 adaptor invalid request");
        hreq->request_dlv = 0;

        qd_message_set_send_complete(msg);
        qdr_link_flow(qdr_http1_adaptor->core, link, 1, false);
        if (hreq->lib_rs) {
            h1_codec_request_state_cancel(hreq->lib_rs);
        } else {
            // failure occurred before the encoder has run so set the flags
            // that are usually set in the request_completed callback
            hreq->cancelled = true;
            hreq->codec_completed = true;
        }

        return hreq->out_dispo;
    }

    return 0;
}


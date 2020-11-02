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

//
// This file contains code specific to HTTP server processing.  The raw
// connection is terminated at an HTTP server, not an HTTP client.
//


//
// State for a single response message arriving via the raw connection.  This
// message will be decoded into a single AMQP message and forwarded into the
// core.
//
// This object is instantiated when the HTTP1 codec indicates the arrival of a
// response message (See _server_rx_response_cb()).  The response is considered
// "complete" after it has been fully encoded and delivered to the core.  The
// _server_response_msg_t is freed at this point - we do not wait for dispo or
// settlement from the core since we cannot do anything meaningful should the
// delivery fail (other than log it).
//
typedef struct _server_response_msg_t {
    DEQ_LINKS(struct _server_response_msg_t);

    struct _server_request_t *hreq; // owning request

    qd_message_t        *msg;       // hold incoming message
    qd_composed_field_t *msg_props; // hold incoming headers
    qdr_delivery_t      *dlv;       // inbound to router (qdr_link_deliver)
    bool                 rx_complete; // response rx complete
} _server_response_msg_t;
ALLOC_DECLARE(_server_response_msg_t);
ALLOC_DEFINE(_server_response_msg_t);
DEQ_DECLARE(_server_response_msg_t, _server_response_msg_list_t);


//
// State for an HTTP/1.x Request+Response exchange, server facing
//
typedef struct _server_request_t {
    qdr_http1_request_base_t   base;

    // The request arrives via the router core in an AMQP message
    // (qd_message_t).  These fields are used to encode the response and send
    // it out the raw connection.
    //
    qdr_delivery_t *request_dlv;     // outbound from core_link_deliver
    uint64_t        request_dispo;   // set by adaptor during encode
    bool            request_settled; // set by adaptor
    bool            request_acked;   // true if dispo sent to core
    bool            request_encoded; // true when encoding done
    bool            headers_encoded; // True when header encode done
    bool            response_settled;

    qdr_http1_out_data_fifo_t out_data;  // encoded request written to raw conn

    _server_response_msg_list_t responses;  // response(s) to this request

    bool codec_completed;     // Request and Response HTTP msgs OK
    bool cancelled;
    bool close_on_complete;   // close the conn when this request is complete
} _server_request_t;
ALLOC_DECLARE(_server_request_t);
ALLOC_DEFINE(_server_request_t);


//
// This file contains code specific to HTTP server processing.  The raw
// connection is terminated at an HTTP server, not an HTTP client.
//


#define DEFAULT_CAPACITY 250
#define RETRY_PAUSE_MSEC ((qd_duration_t)500)
#define MAX_RECONNECT    5  // 5 * 500 = 2.5 sec

static void _server_tx_buffers_cb(h1_codec_request_state_t *lib_hrs, qd_buffer_list_t *blist, unsigned int len);
static void _server_tx_stream_data_cb(h1_codec_request_state_t *lib_hrs, qd_message_stream_data_t *stream_data);
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
static int _server_rx_body_cb(h1_codec_request_state_t *hrs, qd_buffer_list_t *body, size_t len, bool more);
static void _server_rx_done_cb(h1_codec_request_state_t *hrs);
static void _server_request_complete_cb(h1_codec_request_state_t *hrs, bool cancelled);
static void _handle_connection_events(pn_event_t *e, qd_server_t *qd_server, void *context);
static void _do_reconnect(void *context);
static void _do_activate(void *context);
static void _server_response_msg_free(_server_request_t *req, _server_response_msg_t *rmsg);
static void _server_request_free(_server_request_t *hreq);
static void _write_pending_request(_server_request_t *req);
static void _cancel_request(_server_request_t *req);


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
    hconn->cfg.site = bconfig->site ? qd_strdup(bconfig->site) : 0;
    hconn->cfg.host_port = qd_strdup(bconfig->host_port);
    hconn->cfg.event_channel = bconfig->event_channel;
    hconn->cfg.aggregation = bconfig->aggregation;

    // for initiating a connection to the server
    hconn->server.reconnect_timer = qd_timer(qdr_http1_adaptor->core->qd, _do_reconnect, hconn);

    // to run qdr_connection_process() when there is no raw connection to wake
    hconn->server.activate_timer = qd_timer(qdr_http1_adaptor->core->qd, _do_activate, hconn);

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
                                                      "",                  // peer router version,
                                                      false);              // streaming links

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

    qd_log(hconn->adaptor->log, QD_LOG_DEBUG, "[C%"PRIu64"] HTTP connection to server created", hconn->conn_id);

    // wait for the raw connection to come up before creating the in and out links

    hconn->raw_conn = pn_raw_connection();
    pn_raw_connection_set_context(hconn->raw_conn, &hconn->handler_context);

    sys_mutex_lock(qdr_http1_adaptor->lock);
    DEQ_INSERT_TAIL(qdr_http1_adaptor->connections, hconn);
    sys_mutex_unlock(qdr_http1_adaptor->lock);

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
    c->config = *config;
    DEQ_ITEM_INIT(c);

    qdr_http1_connection_t *hconn = _create_server_connection(c, qd, config);
    if (hconn) {
        sys_mutex_lock(qdr_http1_adaptor->lock);
        DEQ_INSERT_TAIL(qdr_http1_adaptor->connectors, c);
        sys_mutex_unlock(qdr_http1_adaptor->lock);

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
void qd_http1_delete_connector(qd_dispatch_t *ignored, qd_http_connector_t *ct)
{
    if (ct) {
        qd_log(qdr_http1_adaptor->log, QD_LOG_INFO, "Deleted HttpConnector for %s, %s:%s", ct->config.address, ct->config.host, ct->config.port);

        sys_mutex_lock(qdr_http1_adaptor->lock);
        DEQ_REMOVE(qdr_http1_adaptor->connectors, ct);
        sys_mutex_unlock(qdr_http1_adaptor->lock);

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
        config.tx_stream_data   = _server_tx_stream_data_cb;
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
    _server_request_t *hreq = (_server_request_t*) DEQ_HEAD(hconn->requests);
    while (hreq) {
        _server_request_free(hreq);
        hreq = (_server_request_t*) DEQ_HEAD(hconn->requests);
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


//
// A note about reconnect and activate timer handlers:
//
// Both _do_reconnect and _do_activate are run via separate qd_timers.
// qd_timers execute on an arbitrary I/O thread and are guaranteed NOT to be
// run in parallel.  The _do_activate timer is started by the core thread via
// _core_connection_activate_CT (http1_adaptor.c).  The _do_reconnect timer is
// started by the I/O thread handling the server raw connection
// PN_RAW_CONNECTION_DISCONNECTED event.
//
// Since the server PN_RAW_CONNECTION_DISCONNECTED handler releases the raw
// connection and at a later point in time _do_reconnect creates a new raw
// connection it is guaranteed that _do_reconnect will NOT run in parallel with
// an I/O thread running the raw connection event handler (since no such raw
// connection exists when _do_reconnect is run)
//
// However it is possible to have a race between an I/O thread running
// _do_activate and an I/O thread running the raw connection event handler IF
// _do_activate runs _after_ _do_reconnect has run (since a new raw connection
// is created and can be immediately scheduled).
//
// To avoid this race the _do_reconnect handler cancels the _do_activate timer
// to prevent it from running immediately after _do_reconnect completes
// (remember: timer handlers never run in parallel).  To prevent the core
// thread from rescheduling _do_activate after _do_reconnect runs a lock is
// held by _do_reconnect while it sets hconn->raw_conn.
//


// This adapter attempts to keep the connection to the server up as long as the
// connector is configured.  This is called via a timer scheduled when the
// PN_CONNECTION_CLOSE event is handled.
// (See above note)
//
static void _do_reconnect(void *context)
{
    qdr_http1_connection_t *hconn = (qdr_http1_connection_t*) context;
    bool connecting = false;

    // lock out core activation
    sys_mutex_lock(qdr_http1_adaptor->lock);

    // prevent _do_activate() from trying to process the qdr_connection after
    // we schedule the raw connection on another thread
    if (hconn->server.activate_timer)
        qd_timer_cancel(hconn->server.activate_timer);
    if (!hconn->raw_conn) {
        connecting = true;
        hconn->raw_conn = pn_raw_connection();
        pn_raw_connection_set_context(hconn->raw_conn, &hconn->handler_context);
        // this call may reschedule the connection on another I/O thread:
        pn_proactor_raw_connect(qd_server_proactor(hconn->qd_server), hconn->raw_conn, hconn->cfg.host_port);
    }

    sys_mutex_unlock(qdr_http1_adaptor->lock);

    if (connecting)
        qd_log(qdr_http1_adaptor->log, QD_LOG_DEBUG,
               "[C%"PRIu64"] Connecting to HTTP server...", hconn->conn_id);
}


// This adapter attempts to keep the qdr_connection_t open as it tries to
// re-connect to the server.  During this reconnect phase there is no raw
// connection.  If the core needs to process the qdr_connection_t when there is
// no raw connection to wake this zero-length timer handler will perform the
// connection processing (under the I/O thread).
// (See above note)
//
static void _do_activate(void *context)
{
    qdr_http1_connection_t *hconn = (qdr_http1_connection_t*) context;
    if (!hconn->raw_conn && hconn->qdr_conn) {
        while (qdr_connection_process(hconn->qdr_conn)) {}
        if (!hconn->qdr_conn) {
            // the qdr_connection_t has been closed
            qd_log(qdr_http1_adaptor->log, QD_LOG_DEBUG,
                   "[C%"PRIu64"] HTTP/1.x server connection closed", hconn->conn_id);
            qdr_http1_connection_free(hconn);
        }
    }
}

static void _accept_and_settle_request(_server_request_t *hreq)
{
    qdr_delivery_remote_state_updated(qdr_http1_adaptor->core,
                                      hreq->request_dlv,
                                      hreq->request_dispo,
                                      true,   // settled
                                      0,      // error
                                      0,      // dispo data
                                      false);
    // can now release the delivery
    qdr_delivery_set_context(hreq->request_dlv, 0);
    qdr_delivery_decref(qdr_http1_adaptor->core, hreq->request_dlv, "HTTP1 adaptor request settled");
    hreq->request_dlv = 0;

    hreq->request_settled = true;
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
        while (qdr_connection_process(hconn->qdr_conn)) {}
        break;
    }
    case PN_RAW_CONNECTION_CLOSED_READ: {
        // notify the codec so it can complete the current response
        // message (response body terminated on connection closed)
        h1_codec_connection_closed(hconn->http_conn);
    }
    // fall through
    case PN_RAW_CONNECTION_CLOSED_WRITE: {
        qd_log(log, QD_LOG_DEBUG, "[C%"PRIu64"] Closed for %s", hconn->conn_id,
               pn_event_type(e) == PN_RAW_CONNECTION_CLOSED_READ
               ? "reading" : "writing");
        pn_raw_connection_close(hconn->raw_conn);
        break;
    }
    case PN_RAW_CONNECTION_DISCONNECTED: {
        pn_raw_connection_set_context(hconn->raw_conn, 0);
        hconn->close_connection = false;

        qd_log(log, QD_LOG_INFO, "[C%"PRIu64"] Connection closed", hconn->conn_id);

        // prevent core from activating raw conn since it will no longer exist
        // on return from the handler
        sys_mutex_lock(qdr_http1_adaptor->lock);
        hconn->raw_conn = 0;
        sys_mutex_unlock(qdr_http1_adaptor->lock);

        // if the current request was not completed, cancel it.  it's ok if
        // there are outstanding *response* deliveries in flight as long as the
        // response(s) have been completely received from the server
        // (request_complete == true).

        _server_request_t *hreq = (_server_request_t*) DEQ_HEAD(hconn->requests);
        if (hreq && !hreq->codec_completed && hreq->base.out_http1_octets > 0) {
            _cancel_request(hreq);
        }

        if (hconn->qdr_conn) {
            //
            // reconnect to the server. Leave the links intact so pending requests
            // are not aborted.  Once we've failed to reconnect after MAX_RECONNECT
            // tries drop the links to prevent additional request from arriving.
            //
            qd_duration_t nap_time = RETRY_PAUSE_MSEC * hconn->server.reconnect_count;
            if (hconn->server.reconnect_count == MAX_RECONNECT) {
                qd_log(log, QD_LOG_INFO, "[C%"PRIu64"] Server not responding - disconnecting...", hconn->conn_id);
                _teardown_server_links(hconn);
            } else {
                hconn->server.reconnect_count += 1;  // increase next sleep interval
            }
            qd_timer_schedule(hconn->server.reconnect_timer, nap_time);
        }
        break;
    }
    case PN_RAW_CONNECTION_NEED_WRITE_BUFFERS: {
        qd_log(log, QD_LOG_DEBUG, "[C%"PRIu64"] Need write buffers", hconn->conn_id);
        _write_pending_request((_server_request_t*) DEQ_HEAD(hconn->requests));
        break;
    }
    case PN_RAW_CONNECTION_NEED_READ_BUFFERS: {
        qd_log(log, QD_LOG_DEBUG, "[C%"PRIu64"] Need read buffers", hconn->conn_id);
        // @TODO(kgiusti): backpressure if no credit
        // if (hconn->in_link_credit > 0 */)
        if (!hconn->close_connection) {
            int granted = qda_raw_conn_grant_read_buffers(hconn->raw_conn);
            qd_log(log, QD_LOG_DEBUG, "[C%"PRIu64"] %d read buffers granted",
                   hconn->conn_id, granted);
        }
        break;
    }
    case PN_RAW_CONNECTION_WAKE: {
        qd_log(log, QD_LOG_DEBUG, "[C%"PRIu64"] Wake-up", hconn->conn_id);
        while (qdr_connection_process(hconn->qdr_conn)) {}
        qd_log(log, QD_LOG_DEBUG, "[C%"PRIu64"] Connection processing complete", hconn->conn_id);
        break;
    }
    case PN_RAW_CONNECTION_READ: {
        qd_buffer_list_t blist;
        uintmax_t length;
        qda_raw_conn_get_read_buffers(hconn->raw_conn, &blist, &length);

        if (HTTP1_DUMP_BUFFERS) {
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

    //
    // After each event check connection and request status
    //
    if (!hconn->qdr_conn) {
        qd_log(log, QD_LOG_DEBUG, "[C%"PRIu64"] HTTP/1.x server connection closed", hconn->conn_id);
        qdr_http1_connection_free(hconn);

    } else {

        bool need_close = false;
        _server_request_t *hreq = (_server_request_t*) DEQ_HEAD(hconn->requests);
        if (hreq) {
            // remove me:
            qd_log(log, QD_LOG_DEBUG, "[C%"PRIu64"] HTTP is server request complete????", hconn->conn_id);
            qd_log(log, QD_LOG_DEBUG, "   codec_completed=%s cancelled=%s",
                   hreq->codec_completed ? "Complete" : "Not Complete",
                   hreq->cancelled ? "Cancelled" : "Not Cancelled");
            qd_log(log, QD_LOG_DEBUG, "   Req: dlv=%p dispo=%"PRIu64" settled=%d acked=%d",
                   (void*) hreq->request_dlv, hreq->request_dispo, hreq->request_settled,
                   hreq->request_acked);
            qd_log(log, QD_LOG_DEBUG, "   Req: out_data=%d pton=%d resp-count=%d",
                   (int) DEQ_SIZE(hreq->out_data.fifo),
                   qdr_http1_out_data_buffers_outstanding(&hreq->out_data),
                   (int) DEQ_SIZE(hreq->responses));

            // Check for completed or cancelled requests

            if (hreq->cancelled) {

                // request:  have to wait until all buffers returned from proton
                // before we can release the request delivery...
                if (qdr_http1_out_data_buffers_outstanding(&hreq->out_data))
                    return;

                if (hreq->request_dlv) {
                    // let the message drain... (TODO@(kgiusti) is this necessary?
                    if (!qdr_delivery_receive_complete(hreq->request_dlv))
                        return;

                    uint64_t dispo = hreq->request_dispo ? hreq->request_dispo : PN_MODIFIED;
                    qdr_delivery_remote_state_updated(qdr_http1_adaptor->core,
                                                      hreq->request_dlv,
                                                      dispo,
                                                      true,   // settled
                                                      0,      // error
                                                      0,      // dispo data
                                                      false);
                    qdr_delivery_set_context(hreq->request_dlv, 0);
                    qdr_delivery_decref(qdr_http1_adaptor->core, hreq->request_dlv, "HTTP1 adaptor request cancelled");
                    hreq->request_dlv = 0;
                }

                _server_response_msg_t *rmsg = DEQ_HEAD(hreq->responses);
                while (rmsg) {
                    if (rmsg->dlv) {
                        qd_message_set_receive_complete(qdr_delivery_message(rmsg->dlv));
                        qdr_delivery_set_aborted(rmsg->dlv, true);
                    }
                    _server_response_msg_free(hreq, rmsg);
                    rmsg = DEQ_HEAD(hreq->responses);
                }

                // The state of the connection to the server will be unknown if
                // this request was not completed.
                if (!hreq->codec_completed && hreq->base.out_http1_octets > 0)
                    need_close = true;

                _server_request_free(hreq);

            } else {

                // Can the request disposition be updated?  Disposition can be
                // updated after the entire encoded request has been written to the
                // server.
                if (!hreq->request_acked &&
                    hreq->request_encoded &&
                    DEQ_SIZE(hreq->out_data.fifo) == 0 &&
                    (hconn->cfg.aggregation == QD_AGGREGATION_NONE || hreq->response_settled)) {

                    qdr_delivery_remote_state_updated(qdr_http1_adaptor->core,
                                                      hreq->request_dlv,
                                                      hreq->request_dispo,
                                                      false,   // settled
                                                      0,      // error
                                                      0,      // dispo data
                                                      false);
                    hreq->request_acked = true;
                }

                // Can we settle request?  Settle the request delivery after all
                // response messages have been received from the server
                // (codec_complete).  Note that the responses may not have finished
                // being delivered to the core (lack of credit, etc.)
                //
                if (!hreq->request_settled &&
                    hreq->request_acked &&  // implies out_data done
                    hreq->codec_completed) {
                    _accept_and_settle_request(hreq);
                }

                // Has the entire request/response completed?  It is complete after
                // the request message has been settled and all responses have been
                // delivered to the core.
                //
                if (hreq->request_acked &&
                    hreq->request_settled &&
                    DEQ_SIZE(hreq->responses) == 0) {

                    qd_log(log, QD_LOG_DEBUG, "[C%"PRIu64"] HTTP request completed!", hconn->conn_id);
                    _server_request_free(hreq);

                    // coverity ignores the fact that _server_request_free() calls
                    // the base cleanup which removes hreq from hconn->requests.
                    // coverity[use_after_free]
                    hreq = (_server_request_t*) DEQ_HEAD(hconn->requests);
                    if (hreq)
                        _write_pending_request(hreq);
                }
            }
        }

        if (need_close) {
            qd_log(log, QD_LOG_DEBUG, "[C%"PRIu64"] Closing connection!", hconn->conn_id);
            qdr_http1_close_connection(hconn, "Request cancelled");
        }
    }
}


//////////////////////////////////////////////////////////////////////
// HTTP/1.x Encoder/Decoder Callbacks
//////////////////////////////////////////////////////////////////////


// Encoder has a buffer list to send to the server
//
static void _server_tx_buffers_cb(h1_codec_request_state_t *hrs, qd_buffer_list_t *blist, unsigned int len)
{
    _server_request_t       *hreq = (_server_request_t*) h1_codec_request_state_get_context(hrs);
    qdr_http1_connection_t *hconn = hreq->base.hconn;

    qd_log(qdr_http1_adaptor->log, QD_LOG_TRACE,
           "[C%"PRIu64"][L%"PRIu64"] Sending %u octets to server",
           hconn->conn_id, hconn->out_link_id, len);
    qdr_http1_enqueue_buffer_list(&hreq->out_data, blist);
    if (hreq == (_server_request_t*) DEQ_HEAD(hconn->requests)) {
        _write_pending_request(hreq);
    }
}


// Encoder has body data to send to the server
//
static void _server_tx_stream_data_cb(h1_codec_request_state_t *hrs, qd_message_stream_data_t *stream_data)
{
    _server_request_t       *hreq = (_server_request_t*) h1_codec_request_state_get_context(hrs);
    qdr_http1_connection_t *hconn = hreq->base.hconn;

    qd_log(qdr_http1_adaptor->log, QD_LOG_TRACE,
           "[C%"PRIu64"][L%"PRIu64"] Sending body data to server",
           hconn->conn_id, hconn->out_link_id);
    qdr_http1_enqueue_stream_data(&hreq->out_data, stream_data);
    if (hreq == (_server_request_t*) DEQ_HEAD(hconn->requests) && hconn->raw_conn) {
        _write_pending_request(hreq);
    }
}


// Server will not be sending us HTTP requests
//
static int _server_rx_request_cb(h1_codec_request_state_t *hrs,
                                 const char *method,
                                 const char *target,
                                 uint32_t version_major,
                                 uint32_t version_minor)
{
    _server_request_t       *hreq = (_server_request_t*) h1_codec_request_state_get_context(hrs);
    qdr_http1_connection_t *hconn = hreq->base.hconn;

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
    _server_request_t       *hreq = (_server_request_t*) h1_codec_request_state_get_context(hrs);
    qdr_http1_connection_t *hconn = hreq->base.hconn;

    // expected to be in-order
    assert(hreq && hreq == (_server_request_t*) DEQ_HEAD(hconn->requests));

    qd_log(qdr_http1_adaptor->log, QD_LOG_TRACE,
           "[C%"PRIu64"][L%"PRIu64"] HTTP response received: status=%d phrase=%s version=%"PRIi32".%"PRIi32,
           hconn->conn_id, hconn->in_link_id, status_code, reason_phrase ? reason_phrase : "<NONE>",
           version_major, version_minor);

    _server_response_msg_t *rmsg = new__server_response_msg_t();
    ZERO(rmsg);
    rmsg->hreq = hreq;
    DEQ_INSERT_TAIL(hreq->responses, rmsg);

    rmsg->msg_props = qd_compose(QD_PERFORMATIVE_APPLICATION_PROPERTIES, 0);
    qd_compose_start_map(rmsg->msg_props);
    {
        char version[64];
        snprintf(version, 64, "%"PRIi32".%"PRIi32, version_major, version_minor);
        qd_compose_insert_symbol(rmsg->msg_props, RESPONSE_HEADER_KEY);
        qd_compose_insert_string(rmsg->msg_props, version);

        qd_compose_insert_symbol(rmsg->msg_props, STATUS_HEADER_KEY);
        qd_compose_insert_int(rmsg->msg_props, (int32_t)status_code);

        if (reason_phrase) {
            qd_compose_insert_symbol(rmsg->msg_props, REASON_HEADER_KEY);
            qd_compose_insert_string(rmsg->msg_props, reason_phrase);
        }
    }

    return 0;
}


// called for each decoded HTTP header.
//
static int _server_rx_header_cb(h1_codec_request_state_t *hrs, const char *key, const char *value)
{
    _server_request_t       *hreq = (_server_request_t*) h1_codec_request_state_get_context(hrs);
    qdr_http1_connection_t *hconn = hreq->base.hconn;

    qd_log(qdr_http1_adaptor->log, QD_LOG_TRACE,
           "[C%"PRIu64"]L%"PRIu64"] HTTP response header received: key='%s' value='%s'",
           hconn->conn_id, hconn->in_link_id, key, value);

    // expect: running incoming request at tail
    _server_response_msg_t *rmsg = DEQ_TAIL(hreq->responses);
    assert(rmsg);

    // We need to filter the connection header out
    // @TODO(kgiusti): also have to remove headers given in value!
    if (strcasecmp(key, "connection") != 0) {
        qd_compose_insert_symbol(rmsg->msg_props, key);
        qd_compose_insert_string(rmsg->msg_props, value);
    }

    return 0;
}


// called after the last header is decoded, before decoding any body data.
//
static int _server_rx_headers_done_cb(h1_codec_request_state_t *hrs, bool has_body)
{
    _server_request_t       *hreq = (_server_request_t*) h1_codec_request_state_get_context(hrs);
    qdr_http1_connection_t *hconn = hreq->base.hconn;

    qd_log(qdr_http1_adaptor->log, QD_LOG_TRACE,
           "[C%"PRIu64"][L%"PRIu64" HTTP response headers done.",
           hconn->conn_id, hconn->in_link_id);

    // expect: running incoming request at tail
    _server_response_msg_t *rmsg = DEQ_TAIL(hreq->responses);
    assert(rmsg && !rmsg->msg);

    // start building the AMQP message

    rmsg->msg = qd_message();

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
    qd_compose_insert_string(props, hreq->base.response_addr); // to
    // subject:
    qd_compose_insert_string(props, h1_codec_request_state_method(hrs));
    qd_compose_insert_null(props);   // reply-to
    qd_compose_insert_ulong(props, hreq->base.msg_id);  // correlation-id
    qd_compose_insert_null(props);                      // content-type
    qd_compose_insert_null(props);                      // content-encoding
    qd_compose_insert_null(props);                      // absolute-expiry-time
    qd_compose_insert_null(props);                      // creation-time
    qd_compose_insert_string(props, hconn->cfg.site);   // group-id
    qd_compose_end_list(props);

    qd_compose_end_map(rmsg->msg_props);

    if (!has_body) {
        // @TODO(kgiusti): fixme: tack on an empty body data performative.  The
        // message decoder will barf otherwise
        qd_buffer_list_t empty = DEQ_EMPTY;
        rmsg->msg_props = qd_compose(QD_PERFORMATIVE_BODY_DATA, rmsg->msg_props);
        qd_compose_insert_binary_buffers(rmsg->msg_props, &empty);
    }

    qd_message_compose_3(rmsg->msg, props, rmsg->msg_props, !has_body);
    qd_compose_free(props);
    qd_compose_free(rmsg->msg_props);
    rmsg->msg_props = 0;

    // start delivery if possible
    if (hconn->in_link_credit > 0 && rmsg == DEQ_HEAD(hreq->responses)) {
        hconn->in_link_credit -= 1;

        qd_log(hconn->adaptor->log, QD_LOG_TRACE,
               "[C%"PRIu64"][L%"PRIu64"] Delivering response to router addr=%s",
               hconn->conn_id, hconn->in_link_id, hreq->base.response_addr);

        qd_iterator_t *addr = qd_message_field_iterator(rmsg->msg, QD_FIELD_TO);
        assert(addr);
        qd_iterator_reset_view(addr, ITER_VIEW_ADDRESS_HASH);
        rmsg->dlv = qdr_link_deliver_to(hconn->in_link, rmsg->msg, 0, addr, false, 0, 0, 0, 0);
        qdr_delivery_set_context(rmsg->dlv, (void*) hreq);
        rmsg->msg = 0;  // now owned by delivery
    }

    return 0;
}


// Called with decoded body data.  This may be called multiple times as body
// data becomes available.
//
static int _server_rx_body_cb(h1_codec_request_state_t *hrs, qd_buffer_list_t *body, size_t len,
                              bool more)
{
    _server_request_t       *hreq = (_server_request_t*) h1_codec_request_state_get_context(hrs);
    qdr_http1_connection_t *hconn = hreq->base.hconn;
    _server_response_msg_t *rmsg  = DEQ_TAIL(hreq->responses);

    qd_message_t *msg = rmsg->msg ? rmsg->msg : qdr_delivery_message(rmsg->dlv);

    qd_log(qdr_http1_adaptor->log, QD_LOG_TRACE,
           "[C%"PRIu64"][L%"PRIu64"] HTTP response body received len=%zu.",
           hconn->conn_id, hconn->in_link_id, len);

    qd_message_stream_data_append(msg, body);

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
    _server_request_t       *hreq = (_server_request_t*) h1_codec_request_state_get_context(hrs);
    qdr_http1_connection_t *hconn = hreq->base.hconn;
    _server_response_msg_t *rmsg  = DEQ_TAIL(hreq->responses);

    qd_message_t *msg = rmsg->msg ? rmsg->msg : qdr_delivery_message(rmsg->dlv);

    qd_log(qdr_http1_adaptor->log, QD_LOG_TRACE,
           "[C%"PRIu64"][L%"PRIu64"] HTTP response receive complete.",
           hconn->conn_id, hconn->in_link_id);

    rmsg->rx_complete = true;

    if (!qd_message_receive_complete(msg)) {
        qd_message_set_receive_complete(msg);
        if (rmsg->dlv) {
            qdr_delivery_continue(qdr_http1_adaptor->core, rmsg->dlv, false);
        }
    }

    if (rmsg->dlv && hconn->cfg.aggregation == QD_AGGREGATION_NONE) {
        // We've finished the delivery, and don't care about outcome/settlement
        _server_response_msg_free(hreq, rmsg);
    }
}


// called at the completion of a full Request/Response exchange, or as a result
// of cancelling the request.  The hrs will be deleted on return from this
// call.  Any hrs related state must be released before returning from this
// callback.
//
// Note: in the case where the request had multiple response messages, this
// call occurs when the LAST response has been completely received
// (_server_rx_done_cb())
//
static void _server_request_complete_cb(h1_codec_request_state_t *hrs, bool cancelled)
{
    _server_request_t       *hreq = (_server_request_t*) h1_codec_request_state_get_context(hrs);
    qdr_http1_connection_t *hconn = hreq->base.hconn;

    hreq->base.stop = qd_timer_now();
    qdr_http1_record_server_request_info(qdr_http1_adaptor, &hreq->base);
    hreq->base.lib_rs = 0;
    hreq->cancelled = hreq->cancelled || cancelled;
    hreq->codec_completed = !hreq->cancelled;

    uint64_t in_octets, out_octets;
    h1_codec_request_state_counters(hrs, &in_octets, &out_octets);
    qd_log(qdr_http1_adaptor->log, QD_LOG_TRACE,
           "[C%"PRIu64"] HTTP request/response %s. Octets read: %"PRIu64" written: %"PRIu64,
           hconn->conn_id,
           cancelled ? "cancelled!" : "codec done",
           in_octets, out_octets);
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

        // check for pending responses that are blocked for credit

        _server_request_t *hreq = (_server_request_t*) DEQ_HEAD(hconn->requests);
        if (hreq) {
            _server_response_msg_t *rmsg = DEQ_HEAD(hreq->responses);
            while (rmsg && rmsg->msg && hconn->in_link_credit > 0) {
                assert(!rmsg->dlv);
                hconn->in_link_credit -= 1;

                qd_log(adaptor->log, QD_LOG_TRACE,
                       "[C%"PRIu64"][L%"PRIu64"] Delivering blocked response to router addr=%s",
                       hconn->conn_id, hconn->in_link_id, hreq->base.response_addr);

                qd_iterator_t *addr = qd_message_field_iterator(rmsg->msg, QD_FIELD_TO);
                qd_iterator_reset_view(addr, ITER_VIEW_ADDRESS_HASH);
                rmsg->dlv = qdr_link_deliver_to(hconn->in_link, rmsg->msg, 0, addr, false, 0, 0, 0, 0);
                qdr_delivery_set_context(rmsg->dlv, (void*) hreq);
                rmsg->msg = 0;
                if (!rmsg->rx_complete) {
                    // stop here since response must be complete before we can deliver the next one.
                    break;
                }
                if (hconn->cfg.aggregation != QD_AGGREGATION_NONE) {
                    // stop here since response should not be freed until it is accepted
                    break;
                }
                // else the delivery is complete no need to save it
                _server_response_msg_free(hreq, rmsg);
                rmsg = DEQ_HEAD(hreq->responses);
            }
        }
    }
}


// Handle disposition/settlement update for the outstanding HTTP response.
//
void qdr_http1_server_core_delivery_update(qdr_http1_adaptor_t      *adaptor,
                                           qdr_http1_connection_t   *hconn,
                                           qdr_http1_request_base_t *hbase,
                                           qdr_delivery_t           *dlv,
                                           uint64_t                  disp,
                                           bool                      settled)
{
    qd_log(qdr_http1_adaptor->log, QD_LOG_TRACE,
           "[C%"PRIu64"][L%"PRIu64"] HTTP response delivery update, outcome=0x%"PRIx64"%s",
           hconn->conn_id, hconn->in_link_id, disp, settled ? " settled": "");

    // Not much can be done with error dispositions (I think)
    if (disp != PN_ACCEPTED) {
        qd_log(adaptor->log, QD_LOG_WARNING,
               "[C%"PRIu64"][L%"PRIu64"] response message not received, outcome=0x%"PRIx64,
               hconn->conn_id, hconn->in_link_id, disp);
    }
    if (hconn->cfg.aggregation != QD_AGGREGATION_NONE) {
        _server_request_t *hreq = (_server_request_t*)hbase;
        _accept_and_settle_request(hreq);
        hreq->request_acked = true;
        _server_response_msg_t *rmsg  = DEQ_TAIL(hreq->responses);
        _server_response_msg_free(hreq, rmsg);
    }
}


//
// Request message forwarding
//


// Create a request context for a new request in msg, which is valid to a depth
// of at least QD_DEPTH_PROPERTIES
//
static _server_request_t *_create_request_context(qdr_http1_connection_t *hconn, qd_message_t *msg)
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

    qd_iterator_t *group_id_itr = qd_message_field_iterator(msg, QD_FIELD_GROUP_ID);
    char* group_id = (char*) qd_iterator_copy(group_id_itr);
    qd_iterator_free(group_id_itr);

    _server_request_t *hreq = new__server_request_t();
    ZERO(hreq);
    hreq->base.hconn = hconn;
    hreq->base.msg_id = msg_id;
    hreq->base.response_addr = reply_to;
    hreq->base.site = group_id;
    hreq->base.start = qd_timer_now();
    DEQ_INIT(hreq->out_data.fifo);
    DEQ_INIT(hreq->responses);
    DEQ_INSERT_TAIL(hconn->requests, &hreq->base);

    qd_log(qdr_http1_adaptor->log, QD_LOG_TRACE,
           "[C%"PRIu64"][L%"PRIu64"] New HTTP Request msg_id=%"PRIu64" reply-to=%s.",
           hconn->conn_id, hconn->out_link_id, msg_id, reply_to);
    return hreq;
}


// Start a new request to the server.  msg has been validated to at least
// application properties depth.  Returns 0 on success.
//
static uint64_t _send_request_headers(_server_request_t *hreq, qd_message_t *msg)
{
    // start encoding HTTP request.  Need method, target and version

    qdr_http1_connection_t *hconn = hreq->base.hconn;
    char *method_str = 0;
    char *target_str = 0;
    qd_parsed_field_t *app_props = 0;
    uint32_t major = 1;
    uint32_t minor = 1;
    uint64_t outcome = 0;

    assert(!hreq->base.lib_rs);
    assert(qd_message_check_depth(msg, QD_DEPTH_PROPERTIES) == QD_MESSAGE_DEPTH_OK);

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

    qd_log(hconn->adaptor->log, QD_LOG_TRACE,
           "[C%"PRIu64"][L%"PRIu64"] Encoding request method=%s target=%s",
           hconn->conn_id, hconn->out_link_id, method_str, target_str);

    hreq->base.lib_rs = h1_codec_tx_request(hconn->http_conn, method_str, target_str, major, minor);
    if (!hreq->base.lib_rs) {
        outcome = PN_REJECTED;
        goto exit;
    }

    h1_codec_request_state_set_context(hreq->base.lib_rs, (void*) hreq);

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

            qd_log(qdr_http1_adaptor->log, QD_LOG_TRACE,
                   "[C%"PRIu64"][L%"PRIu64"] Encoding request header %s:%s",
                   hconn->conn_id, hconn->out_link_id,
                   header_key, header_value);

            ok = !h1_codec_tx_add_header(hreq->base.lib_rs, header_key, header_value);

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
static uint64_t _encode_request_message(_server_request_t *hreq)
{
    qdr_http1_connection_t    *hconn = hreq->base.hconn;
    qd_message_t                *msg = qdr_delivery_message(hreq->request_dlv);

    if (!hreq->headers_encoded) {
        uint64_t outcome = _send_request_headers(hreq, msg);
        if (outcome) {
            qd_log(qdr_http1_adaptor->log, QD_LOG_WARNING,
                   "[C%"PRIu64"][L%"PRIu64"] Rejecting malformed message.", hconn->conn_id, hconn->out_link_id);
            return outcome;
        }
        hreq->headers_encoded = true;
    }

    qd_message_stream_data_t *stream_data = 0;

    while (true) {
        switch (qd_message_next_stream_data(msg, &stream_data)) {
        case QD_MESSAGE_STREAM_DATA_BODY_OK: {

            qd_log(hconn->adaptor->log, QD_LOG_TRACE,
                   "[C%"PRIu64"][L%"PRIu64"] Encoding request body data",
                   hconn->conn_id, hconn->out_link_id);

            if (h1_codec_tx_body(hreq->base.lib_rs, stream_data)) {
                qd_log(qdr_http1_adaptor->log, QD_LOG_WARNING,
                       "[C%"PRIu64"][L%"PRIu64"] body data encode failed",
                       hconn->conn_id, hconn->out_link_id);
                return PN_REJECTED;
            }
            break;
        }

        case QD_MESSAGE_STREAM_DATA_FOOTER_OK:
            break;

        case QD_MESSAGE_STREAM_DATA_NO_MORE:
            // indicate this message is complete
            qd_log(qdr_http1_adaptor->log, QD_LOG_DEBUG,
                   "[C%"PRIu64"][L%"PRIu64"] request message encoding completed",
                   hconn->conn_id, hconn->out_link_id);
            return PN_ACCEPTED;

        case QD_MESSAGE_STREAM_DATA_INCOMPLETE:
            qd_log(qdr_http1_adaptor->log, QD_LOG_DEBUG,
                   "[C%"PRIu64"][L%"PRIu64"] body data need more",
                   hconn->conn_id, hconn->out_link_id);
            return 0;  // wait for more

        case QD_MESSAGE_STREAM_DATA_INVALID:
            qd_log(qdr_http1_adaptor->log, QD_LOG_WARNING,
                   "[C%"PRIu64"][L%"PRIu64"] Rejecting corrupted body data.",
                   hconn->conn_id, hconn->out_link_id);
            return PN_REJECTED;
        }
    }
}


// The router wants to send this delivery out the link. This is either the
// start of a new incoming HTTP request or the continuation of an existing one.
// Note: returning a non-zero value will cause the delivery to be settled!
//
uint64_t qdr_http1_server_core_link_deliver(qdr_http1_adaptor_t    *adaptor,
                                            qdr_http1_connection_t *hconn,
                                            qdr_link_t             *link,
                                            qdr_delivery_t         *delivery,
                                            bool                    settled)
{
    qd_message_t         *msg = qdr_delivery_message(delivery);
    _server_request_t   *hreq = (_server_request_t*) qdr_delivery_get_context(delivery);

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

    if (!hreq->request_dispo)
        hreq->request_dispo = _encode_request_message(hreq);

    if (hreq->request_dispo && qd_message_receive_complete(msg)) {

        qd_message_set_send_complete(msg);
        qdr_link_flow(qdr_http1_adaptor->core, link, 1, false);

        if (hreq->request_dispo == PN_ACCEPTED) {
            hreq->request_encoded = true;
            h1_codec_tx_done(hreq->base.lib_rs, &hreq->close_on_complete);

        } else {
            // mapping to HTTP request failed:
            _cancel_request(hreq);
        }
    }

    return 0;
}


//
// Misc
//

// free the response message
//
static void _server_response_msg_free(_server_request_t *hreq, _server_response_msg_t *rmsg)
{
    DEQ_REMOVE(hreq->responses, rmsg);
    qd_message_free(rmsg->msg);
    qd_compose_free(rmsg->msg_props);
    if (rmsg->dlv) {
        qdr_delivery_set_context(rmsg->dlv, 0);
        qdr_delivery_decref(qdr_http1_adaptor->core, rmsg->dlv, "HTTP1 adaptor response freed");
    }
    free__server_response_msg_t(rmsg);
}


// Release the request
//
static void _server_request_free(_server_request_t *hreq)
{
    if (hreq) {
        qdr_http1_request_base_cleanup(&hreq->base);
        if (hreq->request_dlv) {
            qdr_delivery_set_context(hreq->request_dlv, 0);
            qdr_delivery_decref(qdr_http1_adaptor->core, hreq->request_dlv, "HTTP1 adaptor request freed");
        }

        qdr_http1_out_data_fifo_cleanup(&hreq->out_data);

        _server_response_msg_t *rmsg = DEQ_HEAD(hreq->responses);
        while (rmsg) {
            _server_response_msg_free(hreq, rmsg);
            rmsg = DEQ_HEAD(hreq->responses);
        }

        free__server_request_t(hreq);
    }
}


static void _write_pending_request(_server_request_t *hreq)
{
    if (hreq && !hreq->cancelled && !hreq->base.hconn->close_connection) {
        assert(DEQ_PREV(&hreq->base) == 0);  // preserve order!
        uint64_t written = qdr_http1_write_out_data(hreq->base.hconn, &hreq->out_data);
        hreq->base.out_http1_octets += written;
        qd_log(qdr_http1_adaptor->log, QD_LOG_DEBUG, "[C%"PRIu64"] %"PRIu64" octets written",
               hreq->base.hconn->conn_id, written);
    }
}


void qdr_http1_server_conn_cleanup(qdr_http1_connection_t *hconn)
{
    for (_server_request_t *hreq = (_server_request_t*) DEQ_HEAD(hconn->requests);
         hreq;
         hreq = (_server_request_t*) DEQ_HEAD(hconn->requests)) {
        _server_request_free(hreq);
    }
}


static void _cancel_request(_server_request_t *hreq)
{
    if (!hreq->base.lib_rs) {
        // never even got to encoding it - manually mark it cancelled
        hreq->cancelled = true;
    } else {
        // cleanup codec state - this will call _server_request_complete_cb()
        // with cancelled = true
        h1_codec_request_state_cancel(hreq->base.lib_rs);
    }

    // cleanup occurs at the end of the connection event handler
}


// handle connection close request from management
//
void qdr_http1_server_core_conn_close(qdr_http1_adaptor_t *adaptor,
                                      qdr_http1_connection_t *hconn,
                                      const char *error)
{
    qdr_connection_t *qdr_conn = hconn->qdr_conn;

    // prevent activation by core thread
    sys_mutex_lock(qdr_http1_adaptor->lock);
    qdr_connection_set_context(hconn->qdr_conn, 0);
    hconn->qdr_conn = 0;
    sys_mutex_unlock(qdr_http1_adaptor->lock);

    qdr_connection_closed(qdr_conn);
    qdr_http1_close_connection(hconn, "Connection closed by management");

    // it is expected that this callback is the final callback before returning
    // from qdr_connection_process(). Free hconn when qdr_connection_process returns.
}

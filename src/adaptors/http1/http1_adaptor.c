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

#include <inttypes.h>
#include <stdio.h>

//
// This file contains code for the HTTP/1.x protocol adaptor.  This file
// includes code that is common to both ends of the protocol (e.g. client and
// server processing).  See http1_client.c and http1_server.c for code specific
// to HTTP endpoint processing.
//

#define RAW_BUFFER_BATCH  16


/*
  HTTP/1.x <--> AMQP message mapping

  Message Properties Section:

  HTTP Message                  AMQP Message Properties
  ------------                  -----------------------
  Request Method                subject field

  Application Properties Section:

  HTTP Message                  AMQP Message App Properties Map
  ------------                  -------------------------------
  Request Version               "http:request": "<version|1.1 default>"
  Response Version              "http:response": "<version|1.1 default>"
  Response Status Code          "http:status": <int32>
  Response Reason               "http:reason": <string>
  Request Target                "http:target": <string>
  *                             "<lowercase(key)>" <string>

  Notes:
   - Message App Properties Keys that start with "http:" are reserved by the
     adaptor for meta-data.
 */

// @TODO(kgiusti): rx complete + abort ingress deliveries when endpoint dies while msg in flight


ALLOC_DEFINE(qdr_http1_out_data_t);
ALLOC_DEFINE(qdr_http1_connection_t);


qdr_http1_adaptor_t *qdr_http1_adaptor;


void qdr_http1_request_base_cleanup(qdr_http1_request_base_t *hreq)
{
    if (hreq) {
        DEQ_REMOVE(hreq->hconn->requests, hreq);
        h1_codec_request_state_cancel(hreq->lib_rs);
        free(hreq->response_addr);
        free(hreq->site);
    }
}


void qdr_http1_connection_free(qdr_http1_connection_t *hconn)
{
    if (hconn) {
        pn_raw_connection_t *rconn = 0;
        qd_timer_t *timer = 0;

        // prevent core from activating this connection while it is being torn
        // down. Also prevent timer callbacks from running. see
        // _core_connection_activate_CT and _do_reconnect in http1_server.c
        //
        sys_mutex_lock(qdr_http1_adaptor->lock);
        {
            DEQ_REMOVE(qdr_http1_adaptor->connections, hconn);
            timer = hconn->server.reconnect_timer;
            hconn->server.reconnect_timer = 0;
            rconn = hconn->raw_conn;
            hconn->raw_conn = 0;
            if (hconn->server.connector) {
                hconn->server.connector->ctx = 0;
                hconn->server.connector = 0;
            }
            if (hconn->qdr_conn)
                qdr_connection_set_context(hconn->qdr_conn, 0);
            hconn->qdr_conn = 0;
        }
        sys_mutex_unlock(qdr_http1_adaptor->lock);

        // must free timer outside of lock since callback
        // attempts to take lock:
        qd_timer_free(timer);

        // cleanup outstanding requests
        //
        if (hconn->type == HTTP1_CONN_SERVER)
            qdr_http1_server_conn_cleanup(hconn);
        else
            qdr_http1_client_conn_cleanup(hconn);

        h1_codec_connection_free(hconn->http_conn);
        if (rconn) {
            pn_raw_connection_set_context(rconn, 0);
            pn_raw_connection_close(rconn);
        }

        sys_atomic_destroy(&hconn->q2_restart);

        free(hconn->cfg.host);
        free(hconn->cfg.port);
        free(hconn->cfg.address);
        free(hconn->cfg.site);
        free(hconn->cfg.host_override);
        free(hconn->cfg.host_port);

        free(hconn->client.client_ip_addr);
        free(hconn->client.reply_to_addr);

        free_qdr_http1_connection_t(hconn);
    }
}


void qdr_http1_out_data_fifo_cleanup(qdr_http1_out_data_fifo_t *out_data)
{
    if (out_data) {
        // expect: all buffers returned from proton!
        assert(qdr_http1_out_data_buffers_outstanding(out_data) == 0);
        qdr_http1_out_data_t *od = DEQ_HEAD(out_data->fifo);
        while (od) {
            DEQ_REMOVE_HEAD(out_data->fifo);
            if (od->stream_data)
                qd_message_stream_data_release(od->stream_data);
            else
                qd_buffer_list_free_buffers(&od->raw_buffers);
            free_qdr_http1_out_data_t(od);
            od = DEQ_HEAD(out_data->fifo);
        }
    }
}


// Return the number of buffers in the process of being written out by the proactor.
// These buffers are "owned" by proton - they must not be freed until proton has
// released them.
//
int qdr_http1_out_data_buffers_outstanding(const qdr_http1_out_data_fifo_t *out_data)
{
    int count = 0;
    if (out_data) {
        qdr_http1_out_data_t *od = DEQ_HEAD(out_data->fifo);
        while (od) {
            count += od->next_buffer - od->free_count;
            if (od == out_data->write_ptr)
                break;

            od = DEQ_NEXT(od);
        }
    }
    return count;
}


// Initiate close of the raw connection.
//
void qdr_http1_close_connection(qdr_http1_connection_t *hconn, const char *error)
{
    if (error) {
        qd_log(qdr_http1_adaptor->log, QD_LOG_ERROR,
               "[C%"PRIu64"] Connection closing: %s", hconn->conn_id, error);
    }

    if (hconn->raw_conn) {
        qd_log(qdr_http1_adaptor->log, QD_LOG_DEBUG,
               "[C%"PRIu64"] Initiating close of connection", hconn->conn_id);
        pn_raw_connection_close(hconn->raw_conn);
    }

    // clean up all connection related stuff on PN_RAW_CONNECTION_DISCONNECTED
    // event
}


void qdr_http1_rejected_response(qdr_http1_request_base_t *hreq,
                                 const qdr_error_t        *error)
{
    char *reason = 0;
    if (error) {
        size_t len = 0;
        char *ename = qdr_error_name(error);
        char *edesc = qdr_error_description(error);
        if (ename) len += strlen(ename);
        if (edesc) len += strlen(edesc);
        if (len) {
            reason = qd_malloc(len + 2);
            reason[0] = 0;
            if (ename) {
                strcat(reason, ename);
                strcat(reason, " ");
            }
            if (edesc)
                strcat(reason, edesc);
        }
        free(ename);
        free(edesc);
    }

    qdr_http1_error_response(hreq, HTTP1_STATUS_BAD_REQ,
                             reason ? reason : "Invalid Request");
    free(reason);
}


// send a server error response
//
void qdr_http1_error_response(qdr_http1_request_base_t *hreq,
                              int error_code,
                              const char *reason)
{
    if (hreq->lib_rs) {
        bool ignored;
        h1_codec_tx_response(hreq->lib_rs, error_code, reason, 1, 1);
        h1_codec_tx_add_header(hreq->lib_rs, "Content-Length", "0");
        h1_codec_tx_done(hreq->lib_rs, &ignored);
    }
}


//
// Raw Connection Write Buffer Management
//


// Write pending data out the raw connection.  Preserve order by only writing
// the head request data.
//
uint64_t qdr_http1_write_out_data(qdr_http1_connection_t *hconn, qdr_http1_out_data_fifo_t *fifo)
{
    pn_raw_buffer_t buffers[RAW_BUFFER_BATCH];
    size_t count = !hconn->raw_conn || pn_raw_connection_is_write_closed(hconn->raw_conn)
        ? 0
        : pn_raw_connection_write_buffers_capacity(hconn->raw_conn);

    uint64_t total_octets = 0;
    qdr_http1_out_data_t *od = fifo->write_ptr;
    while (count > 0 && od) {
        qd_buffer_t *wbuf   = 0;
        int          od_len = MIN(count,
                                  (od->buffer_count - od->next_buffer));
        assert(od_len);  // error: no data @ head?

        // send the out_data as a series of writes to proactor

        while (od_len) {
            size_t limit = MIN(RAW_BUFFER_BATCH, od_len);
            int written = 0;

            if (od->stream_data) {  // buffers stored in qd_message_t

                written = qd_message_stream_data_buffers(od->stream_data, buffers, od->next_buffer, limit);
                for (int i = 0; i < written; ++i) {
                    // enforce this: we expect the context can be used by the adaptor!
                    assert(buffers[i].context == 0);
                    buffers[i].context = (uintptr_t)od;
                    total_octets += buffers[i].size;
                }

            } else {   // list of buffers in od->raw_buffers
                // advance to next buffer to send in od
                if (!wbuf) {
                    wbuf = DEQ_HEAD(od->raw_buffers);
                    for (int i = 0; i < od->next_buffer; ++i)
                        wbuf = DEQ_NEXT(wbuf);
                }

                pn_raw_buffer_t *rdisc = &buffers[0];
                while (limit--) {
                    rdisc->context  = (uintptr_t)od;
                    rdisc->bytes    = (char*) qd_buffer_base(wbuf);
                    rdisc->capacity = 0;
                    rdisc->size     = qd_buffer_size(wbuf);
                    rdisc->offset   = 0;

                    total_octets += rdisc->size;
                    ++rdisc;
                    wbuf = DEQ_NEXT(wbuf);
                    written += 1;
                }
            }

            // keep me, you'll need it
            if (HTTP1_DUMP_BUFFERS) {
                for (size_t j = 0; j < written; ++j) {
                    char *ptr = (char*) buffers[j].bytes;
                    int len = (int) buffers[j].size;
                    fprintf(stdout, "\n[C%"PRIu64"] Raw Write: Ptr=%p len=%d\n  value='%.*s'\n",
                            hconn->conn_id, (void*)ptr, len, len, ptr);
                    fflush(stdout);
                }
            }

            written = pn_raw_connection_write_buffers(hconn->raw_conn, buffers, written);
            count -= written;
            od_len -= written;
            od->next_buffer += written;
        }

        if (od->next_buffer == od->buffer_count) {
            // all buffers in od have been passed to proton.
            od = DEQ_NEXT(od);
            fifo->write_ptr = od;
            wbuf = 0;
        }
    }

    hconn->out_http1_octets += total_octets;
    return total_octets;
}


// The HTTP encoder has a list of buffers to be written to the raw connection.
// Queue it to the outgoing data fifo.
//
void qdr_http1_enqueue_buffer_list(qdr_http1_out_data_fifo_t *fifo, qd_buffer_list_t *blist)
{
    int count = (int) DEQ_SIZE(*blist);
    if (count) {
        qdr_http1_out_data_t *od = new_qdr_http1_out_data_t();
        ZERO(od);
        od->owning_fifo = fifo;
        od->buffer_count = (int) DEQ_SIZE(*blist);
        od->raw_buffers = *blist;
        DEQ_INIT(*blist);

        DEQ_INSERT_TAIL(fifo->fifo, od);
        if (!fifo->write_ptr)
            fifo->write_ptr = od;
    }
}


// The HTTP encoder has a message body data to be written to the raw connection.
// Queue it to the outgoing data fifo.
//
void qdr_http1_enqueue_stream_data(qdr_http1_out_data_fifo_t *fifo, qd_message_stream_data_t *stream_data)
{
    int count = qd_message_stream_data_buffer_count(stream_data);
    if (count) {
        qdr_http1_out_data_t *od = new_qdr_http1_out_data_t();
        ZERO(od);
        od->owning_fifo = fifo;
        od->stream_data = stream_data;
        od->buffer_count = count;

        DEQ_INSERT_TAIL(fifo->fifo, od);
        if (!fifo->write_ptr)
            fifo->write_ptr = od;
    } else {
        // empty body-data
        qd_message_stream_data_release(stream_data);
    }
}


// Called during proactor event PN_RAW_CONNECTION_WRITTEN
//
void qdr_http1_free_written_buffers(qdr_http1_connection_t *hconn)
{
    pn_raw_buffer_t buffers[RAW_BUFFER_BATCH];
    size_t count;
    while ((count = pn_raw_connection_take_written_buffers(hconn->raw_conn, buffers, RAW_BUFFER_BATCH)) != 0) {
        for (size_t i = 0; i < count; ++i) {

            // keep me, you'll need it
            if (HTTP1_DUMP_BUFFERS) {
                char *ptr = (char*) buffers[i].bytes;
                int len = (int) buffers[i].size;
                fprintf(stdout, "\n[C%"PRIu64"] Raw Written: Ptr=%p len=%d c=%d o=%d\n  value='%.*s'\n",
                        hconn->conn_id, (void*)ptr, len, buffers[i].capacity, buffers[i].offset, len, ptr);
                fflush(stdout);
            }

            qdr_http1_out_data_t *od = (qdr_http1_out_data_t*) buffers[i].context;
            assert(od);
            // Note: according to proton devs the order in which write buffers
            // are released are NOT guaranteed to be in the same order in which
            // they were written!

            od->free_count += 1;
            if (od->free_count == od->buffer_count) {
                // all buffers returned
                qdr_http1_out_data_fifo_t *fifo = od->owning_fifo;
                DEQ_REMOVE(fifo->fifo, od);
                if (od->stream_data)
                    qd_message_stream_data_release(od->stream_data);
                else
                    qd_buffer_list_free_buffers(&od->raw_buffers);
                free_qdr_http1_out_data_t(od);
            }
        }
    }
}


// Per-message callback to resume receiving after Q2 is unblocked on the
// incoming link (to HTTP app).  This routine runs on another I/O thread so it
// must be thread safe!
//
void qdr_http1_q2_unblocked_handler(const qd_alloc_safe_ptr_t context)
{
    // prevent the hconn from being deleted while running:
    sys_mutex_lock(qdr_http1_adaptor->lock);

    qdr_http1_connection_t *hconn = (qdr_http1_connection_t*)qd_alloc_deref_safe_ptr(&context);
    if (hconn && hconn->raw_conn) {
        sys_atomic_set(&hconn->q2_restart, 1);
        pn_raw_connection_wake(hconn->raw_conn);
    }

    sys_mutex_unlock(qdr_http1_adaptor->lock);
}


//
// Protocol Adaptor Callbacks
//


// Invoked by the core thread to wake an I/O thread for the connection
//
static void _core_connection_activate_CT(void *context, qdr_connection_t *conn)
{
    bool activated = false;

    sys_mutex_lock(qdr_http1_adaptor->lock);
    qdr_http1_connection_t *hconn = (qdr_http1_connection_t*) qdr_connection_get_context(conn);
    if (hconn) {
        if (hconn->raw_conn) {
            pn_raw_connection_wake(hconn->raw_conn);
            activated = true;
        } else if (hconn->server.reconnect_timer) {
            assert(hconn->type == HTTP1_CONN_SERVER);
            qd_timer_schedule(hconn->server.reconnect_timer, 0);
            activated = true;
        }
    }
    sys_mutex_unlock(qdr_http1_adaptor->lock);

    qd_log(qdr_http1_adaptor->log, QD_LOG_DEBUG, "[C%"PRIu64"] Connection %s",
           conn->identity, activated ? "activated" : "down, unable to activate");
}


static void _core_link_first_attach(void               *context,
                                    qdr_connection_t   *conn,
                                    qdr_link_t         *link,
                                    qdr_terminus_t     *source,
                                    qdr_terminus_t     *target,
                                    qd_session_class_t  ssn_class)
{
    qdr_http1_connection_t *hconn = (qdr_http1_connection_t*) qdr_connection_get_context(conn);
    if (hconn)
        qd_log(qdr_http1_adaptor->log, QD_LOG_DEBUG, "[C%"PRIu64"] Link first attach", hconn->conn_id);
}


static void _core_link_second_attach(void          *context,
                                     qdr_link_t     *link,
                                     qdr_terminus_t *source,
                                     qdr_terminus_t *target)
{
    qdr_http1_connection_t *hconn = (qdr_http1_connection_t*) qdr_link_get_context(link);
    if (!hconn) return;

    qd_log(qdr_http1_adaptor->log, QD_LOG_DEBUG,
           "[C%"PRIu64"][L%"PRIu64"] Link second attach", hconn->conn_id, link->identity);

    if (hconn->type == HTTP1_CONN_CLIENT) {
        qdr_http1_client_core_second_attach((qdr_http1_adaptor_t*) context,
                                            hconn, link, source, target);
    }
}


static void _core_link_detach(void *context, qdr_link_t *link, qdr_error_t *error, bool first, bool close)
{
    qdr_http1_connection_t *hconn = (qdr_http1_connection_t*) qdr_link_get_context(link);
    if (hconn) {
        qd_log(qdr_http1_adaptor->log, QD_LOG_DEBUG,
               "[C%"PRIu64"][L%"PRIu64"] Link detach", hconn->conn_id, link->identity);

        qdr_link_set_context(link, 0);
        if (link == hconn->out_link)
            hconn->out_link = 0;
        else
            hconn->in_link = 0;
    }
}


static void _core_link_flow(void *context, qdr_link_t *link, int credit)
{
    qdr_http1_connection_t *hconn = (qdr_http1_connection_t*) qdr_link_get_context(link);
    if (hconn) {
        qd_log(qdr_http1_adaptor->log, QD_LOG_DEBUG,
               "[C%"PRIu64"][L%"PRIu64"] Link flow (%d)",
               hconn->conn_id, link->identity, credit);
        if (hconn->type == HTTP1_CONN_SERVER)
            qdr_http1_server_core_link_flow((qdr_http1_adaptor_t*) context, hconn, link, credit);
        else
            qdr_http1_client_core_link_flow((qdr_http1_adaptor_t*) context, hconn, link, credit);
    }
}


static void _core_link_offer(void *context, qdr_link_t *link, int delivery_count)
{
    qdr_http1_connection_t *hconn = (qdr_http1_connection_t*) qdr_link_get_context(link);
    if (hconn) {
        qd_log(qdr_http1_adaptor->log, QD_LOG_DEBUG,
               "[C%"PRIu64"][L%"PRIu64"] Link offer (%d)",
               hconn->conn_id, link->identity, delivery_count);
    }
}


static void _core_link_drained(void *context, qdr_link_t *link)
{
    qdr_http1_connection_t *hconn = (qdr_http1_connection_t*) qdr_link_get_context(link);
    if (hconn) {
        qd_log(qdr_http1_adaptor->log, QD_LOG_DEBUG,
               "[C%"PRIu64"][L%"PRIu64"] Link drained",
               hconn->conn_id, link->identity);
    }
}


static void _core_link_drain(void *context, qdr_link_t *link, bool mode)
{
    qdr_http1_connection_t *hconn = (qdr_http1_connection_t*) qdr_link_get_context(link);
    if (hconn) {
        qd_log(qdr_http1_adaptor->log, QD_LOG_DEBUG,
               "[C%"PRIu64"][L%"PRIu64"] Link drain %s",
               hconn->conn_id, link->identity,
               mode ? "ON" : "OFF");
    }
}


static int _core_link_push(void *context, qdr_link_t *link, int limit)
{
    qdr_http1_connection_t *hconn = (qdr_http1_connection_t*) qdr_link_get_context(link);
    if (hconn) {
        qd_log(qdr_http1_adaptor->log, QD_LOG_DEBUG,
               "[C%"PRIu64"][L%"PRIu64"] Link push %d", hconn->conn_id, link->identity, limit);
        return qdr_link_process_deliveries(qdr_http1_adaptor->core, link, limit);
    }
    return 0;
}


// The I/O thread wants to send this delivery out the link
//
static uint64_t _core_link_deliver(void *context, qdr_link_t *link, qdr_delivery_t *delivery, bool settled)
{
    qdr_http1_connection_t *hconn = (qdr_http1_connection_t*) qdr_link_get_context(link);
    uint64_t outcome = PN_RELEASED;

    if (hconn) {
        qd_log(qdr_http1_adaptor->log, QD_LOG_DEBUG,
               "[C%"PRIu64"][L%"PRIu64"] Core link deliver %p %s", hconn->conn_id, link->identity,
               (void*)delivery, settled ? "settled" : "unsettled");

        if (hconn->type == HTTP1_CONN_SERVER)
            outcome = qdr_http1_server_core_link_deliver(qdr_http1_adaptor, hconn, link, delivery, settled);
        else
            outcome = qdr_http1_client_core_link_deliver(qdr_http1_adaptor, hconn, link, delivery, settled);
    }

    return outcome;
}

static int _core_link_get_credit(void *context, qdr_link_t *link)
{
    qdr_http1_connection_t *hconn = (qdr_http1_connection_t*) qdr_link_get_context(link);
    int credit = 0;
    if (hconn) {
        credit = (link == hconn->in_link) ? hconn->in_link_credit : hconn->out_link_credit;
        qd_log(qdr_http1_adaptor->log, QD_LOG_DEBUG,
               "[C%"PRIu64"][L%"PRIu64"] Link get credit (%d)", hconn->conn_id, link->identity, credit);
    }

    return credit;
}


// Handle disposition/settlement update for the outstanding incoming HTTP message.
//
static void _core_delivery_update(void *context, qdr_delivery_t *dlv, uint64_t disp, bool settled)
{
    qdr_http1_request_base_t *hreq = (qdr_http1_request_base_t*) qdr_delivery_get_context(dlv);
    if (hreq) {
        qdr_http1_connection_t *hconn = hreq->hconn;
        qdr_link_t *link = qdr_delivery_link(dlv);
        qd_log(qdr_http1_adaptor->log, QD_LOG_DEBUG,
               "[C%"PRIu64"][L%"PRIu64"] Core Delivery update disp=0x%"PRIx64" %s",
               hconn->conn_id, link->identity, disp,
               settled ? "settled" : "unsettled");

        if (hconn->type == HTTP1_CONN_SERVER)
            qdr_http1_server_core_delivery_update(qdr_http1_adaptor, hconn, hreq, dlv, disp, settled);
        else
            qdr_http1_client_core_delivery_update(qdr_http1_adaptor, hconn, hreq, dlv, disp, settled);
    }
}


// This is invoked by management to initiate the connection close process.
// Once the raw conn is closed (DISCONNECT event) we call qdr_connection_closed()
// to finish the close processing
//
static void _core_conn_close(void *context, qdr_connection_t *conn, qdr_error_t *error)
{
    qdr_http1_connection_t *hconn = (qdr_http1_connection_t*) qdr_connection_get_context(conn);
    if (hconn) {
        assert(hconn->qdr_conn == conn);
        qd_log(qdr_http1_adaptor->log, QD_LOG_TRACE,
               "[C%"PRIu64"] HTTP/1.x closing connection", hconn->conn_id);

        char *qdr_error = error ? qdr_error_description(error) : 0;
        if (hconn->type == HTTP1_CONN_SERVER)
            qdr_http1_server_core_conn_close(qdr_http1_adaptor, hconn, qdr_error);
        else
            qdr_http1_client_core_conn_close(qdr_http1_adaptor, hconn, qdr_error);
        free(qdr_error);
    }
}


static void _core_conn_trace(void *context, qdr_connection_t *conn, bool trace)
{
    qdr_http1_connection_t *hconn = (qdr_http1_connection_t*) qdr_connection_get_context(conn);
    if (hconn) {
        hconn->trace = trace;
        if (trace)
            qd_log(qdr_http1_adaptor->log, QD_LOG_TRACE,
                   "[C%"PRIu64"] HTTP/1.x trace enabled", hconn->conn_id);
    }
}


//
// Adaptor Setup & Teardown
//


static void qd_http1_adaptor_init(qdr_core_t *core, void **adaptor_context)
{
    qdr_http1_adaptor_t *adaptor = NEW(qdr_http1_adaptor_t);

    ZERO(adaptor);
    adaptor->core    = core;
    adaptor->log = qd_log_source(QD_HTTP_LOG_SOURCE);
    adaptor->lock = sys_mutex();
    DEQ_INIT(adaptor->listeners);
    DEQ_INIT(adaptor->connectors);
    DEQ_INIT(adaptor->connections);
    adaptor->adaptor = qdr_protocol_adaptor(core,
                                            "http/1.x",
                                            adaptor,             // context
                                            _core_connection_activate_CT,  // core thread only
                                            _core_link_first_attach,
                                            _core_link_second_attach,
                                            _core_link_detach,
                                            _core_link_flow,
                                            _core_link_offer,
                                            _core_link_drained,
                                            _core_link_drain,
                                            _core_link_push,
                                            _core_link_deliver,
                                            _core_link_get_credit,
                                            _core_delivery_update,
                                            _core_conn_close,
                                            _core_conn_trace);
    *adaptor_context = adaptor;
    qdr_http1_adaptor = adaptor;
}


static void qd_http1_adaptor_final(void *adaptor_context)
{
    qdr_http1_adaptor_t *adaptor = (qdr_http1_adaptor_t*) adaptor_context;
    qdr_protocol_adaptor_free(adaptor->core, adaptor->adaptor);

    qd_http_listener_t *li = DEQ_HEAD(adaptor->listeners);
    while (li) {
        qd_http1_delete_listener(0, li);
        li = DEQ_HEAD(adaptor->listeners);
    }
    qd_http_connector_t *ct = DEQ_HEAD(adaptor->connectors);
    while (ct) {
        qd_http1_delete_connector(0, ct);
        ct = DEQ_HEAD(adaptor->connectors);
    }
    qdr_http1_connection_t *hconn = DEQ_HEAD(adaptor->connections);
    while (hconn) {
        qdr_http1_connection_free(hconn);
        hconn = DEQ_HEAD(adaptor->connections);
    }

    sys_mutex_free(adaptor->lock);
    qdr_http1_adaptor =  NULL;

    free(adaptor);
}


/**
 * Declare the adaptor so that it will self-register on process startup.
 */
QDR_CORE_ADAPTOR_DECLARE("http1.x-adaptor", qd_http1_adaptor_init, qd_http1_adaptor_final)


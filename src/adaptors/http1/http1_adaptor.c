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

#include <stdio.h>
#include <inttypes.h>

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


ALLOC_DEFINE(qdr_http1_request_t);
ALLOC_DEFINE(qdr_http1_response_msg_t);
ALLOC_DEFINE(qdr_http1_out_data_t);
ALLOC_DEFINE(qdr_http1_connection_t);


qdr_http1_adaptor_t *qdr_http1_adaptor;


void qdr_http1_request_free(qdr_http1_request_t *hreq)
{
    if (hreq) {
        DEQ_REMOVE(hreq->hconn->requests, hreq);

        h1_codec_request_state_cancel(hreq->lib_rs);
        free(hreq->response_addr);

        qd_message_free(hreq->request_msg);
        if (hreq->request_dlv) {
            qdr_delivery_set_context(hreq->request_dlv, 0);
            qdr_delivery_decref(qdr_http1_adaptor->core, hreq->request_dlv, "HTTP1 adaptor connection closed");
        }

        qdr_http1_response_msg_t *rmsg = DEQ_HEAD(hreq->responses);
        while (rmsg) {
            DEQ_REMOVE_HEAD(hreq->responses);
            qd_message_free(rmsg->in_msg);
            if (rmsg->dlv) {
                qdr_delivery_set_context(rmsg->dlv, 0);
                qdr_delivery_decref(qdr_http1_adaptor->core, rmsg->dlv, "HTTP1 adaptor connection closed");
            }
            rmsg = DEQ_HEAD(hreq->responses);
        }

        qd_compose_free(hreq->app_props);

        qdr_http1_out_data_t *od = DEQ_HEAD(hreq->out_fifo);
        while (od) {
            DEQ_REMOVE_HEAD(hreq->out_fifo);
            if (od->body_data)
                qd_message_body_data_release(od->body_data);
            else {

#if 1
                {
                    qd_buffer_t *mybuf = DEQ_HEAD(od->raw_buffers);
                    while (mybuf) {
                        fprintf(stdout, "Free buffer: Ptr=%p len=%d\n",
                                (void*)&mybuf[1], (int)mybuf->size);
                        fflush(stdout);
                        mybuf = DEQ_NEXT(mybuf);
                    }
                }
#endif
                qd_buffer_list_free_buffers(&od->raw_buffers);
            }
            free_qdr_http1_out_data_t(od);
            od = DEQ_HEAD(hreq->out_fifo);
        }

        free_qdr_http1_request_t(hreq);
    }
}


void qdr_http1_connection_free(qdr_http1_connection_t *hconn)
{
    if (hconn) {

        qdr_http1_request_t *hreq = DEQ_HEAD(hconn->requests);
        while (hreq) {
            qdr_http1_request_free(hreq);
            hreq = DEQ_HEAD(hconn->requests);
        }
        h1_codec_connection_free(hconn->http_conn);
        if (hconn->raw_conn) {
            pn_raw_connection_set_context(hconn->raw_conn, 0);
            pn_raw_connection_close(hconn->raw_conn);
        }
        if (hconn->out_link) {
            qdr_link_set_context(hconn->out_link, 0);
            qdr_link_detach(hconn->out_link, QD_CLOSED, 0);
        }
        if (hconn->in_link) {
            qdr_link_set_context(hconn->in_link, 0);
            qdr_link_detach(hconn->in_link, QD_CLOSED, 0);
        }
        if (hconn->qdr_conn) {
            qdr_connection_set_context(hconn->qdr_conn, 0);
            qdr_connection_closed(hconn->qdr_conn);
        }

        free(hconn->cfg.host);
        free(hconn->cfg.port);
        free(hconn->cfg.address);
        free(hconn->cfg.host_port);

        free(hconn->client.client_ip_addr);
        free(hconn->client.reply_to_addr);

        qd_timer_free(hconn->server.reconnect_timer);

        free_qdr_http1_connection_t(hconn);
    }
}


// Initiate close of the raw connection.
//
void qdr_http1_close_connection(qdr_http1_connection_t *hconn, const char *error)
{
    if (error) {
        qd_log(qdr_http1_adaptor->log, QD_LOG_ERROR,
               "[C%"PRIu64"] Connection closing: %s", hconn->conn_id, error);
    }

    hconn->close_connection = true;
    if (hconn->raw_conn) {
        qd_log(qdr_http1_adaptor->log, QD_LOG_DEBUG,
               "[C%"PRIu64"] Initiating close of connection", hconn->conn_id);
        pn_raw_connection_close(hconn->raw_conn);
    }

    // clean up all connection related stuff on PN_RAW_CONNECTION_DISCONNECTED
    // event
}


// Check if hreq has completed.
//
bool qdr_http1_request_complete(const qdr_http1_request_t *hreq)
{
    assert(hreq);
    return (hreq->codec_completed &&
            hreq->request_msg == 0 &&
            hreq->request_dlv == 0 &&
            DEQ_IS_EMPTY(hreq->responses) &&
            DEQ_IS_EMPTY(hreq->out_fifo));
}


void qdr_http1_rejected_response(qdr_http1_request_t *hreq,
                                 const qdr_error_t *error)
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
void qdr_http1_error_response(qdr_http1_request_t *hreq,
                              int error_code,
                              const char *reason)
{
    if (hreq->lib_rs) {
        bool ignored;
        h1_codec_tx_response(hreq->lib_rs, error_code, reason, 1, 1);
        h1_codec_tx_done(hreq->lib_rs, &ignored);
    }
}


const char *qdr_http1_token_list_next(const char *start, size_t *len, const char **next)
{
    static const char *SKIPME = ", \t";

    *len = 0;
    *next = 0;

    if (!start) return 0;

    while (*start && strchr(SKIPME, *start))
        ++start;

    if (!*start) return 0;

    const char *end = start;
    while (*end && !strchr(SKIPME, *end))
        ++end;

    *len = end - start;
    *next = end;

    while (**next && strchr(SKIPME, **next))
        ++(*next);

    return start;
}


//
// Raw Connection Write Buffer Management
//


// Write pending data out the raw connection.  Preserve order by only writing
// the head request data.
//
int qdr_http1_write_out_data(qdr_http1_connection_t *hconn)
{
    qdr_http1_request_t *hreq = DEQ_HEAD(hconn->requests);
    if (!hreq) return 0;

    pn_raw_buffer_t buffers[RAW_BUFFER_BATCH];
    size_t count = !hconn->raw_conn || pn_raw_connection_is_write_closed(hconn->raw_conn)
        ? 0
        : pn_raw_connection_write_buffers_capacity(hconn->raw_conn);


    int total_bufs = 0;
    qdr_http1_out_data_t *od = hreq->write_ptr;
    while (count > 0 && od) {
        qd_buffer_t *wbuf   = 0;
        int          od_len = MIN(count,
                                  (od->buffer_count - od->next_buffer));
        assert(od_len);  // error: no data @ head?

        // send the out_data as a series of writes to proactor

        while (od_len) {
            size_t limit = MIN(RAW_BUFFER_BATCH, od_len);
            int written = 0;
            uint64_t total_bytes = 0;

            if (od->body_data) {  // buffers stored in qd_message_t

                written = qd_message_body_data_buffers(od->body_data, buffers, od->next_buffer, limit);
                for (int i = 0; i < written; ++i) {
                    // enforce this: we expect the context can be used by the adaptor!
                    assert(buffers[i].context == 0);
                    buffers[i].context = (uintptr_t)od;
                    total_bytes += buffers[i].size;
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

                    total_bytes += rdisc->size;
                    ++rdisc;
                    wbuf = DEQ_NEXT(wbuf);
                    written += 1;
                }
            }

            written = pn_raw_connection_write_buffers(hconn->raw_conn, buffers, written);
            count -= written;
            od_len -= written;
            od->next_buffer += written;
            total_bufs += written;

            hreq->out_http1_octets  += total_bytes;
            hconn->out_http1_octets += total_bytes;
            total_bytes = 0;
        }

        if (od->next_buffer == od->buffer_count) {
            // all buffers in od have been passed to proton.
            od = DEQ_NEXT(od);
            hreq->write_ptr = od;
            wbuf = 0;
        }
    }

    return total_bufs;
}


// The HTTP encoder has a list of buffers to be written to the raw connection.
// Queue it to the requests outgoing data.  Write to the raw connection only if
// this is the current request.
//
void qdr_http1_write_buffer_list(qdr_http1_request_t *hreq, qd_buffer_list_t *blist)
{
    int count = (int) DEQ_SIZE(*blist);
    if (count) {
        qdr_http1_out_data_t *od = new_qdr_http1_out_data_t();
        ZERO(od);
        od->raw_buffers = *blist;
        od->buffer_count = (int) DEQ_SIZE(*blist);
        DEQ_INIT(*blist);

        od->hreq = hreq;
        DEQ_INSERT_TAIL(hreq->out_fifo, od);
        if (!hreq->write_ptr)
            hreq->write_ptr = od;

        if (hreq == DEQ_HEAD(hreq->hconn->requests))
            qdr_http1_write_out_data(hreq->hconn);
    }
}


// The HTTP encoder has a message body data to be written to the raw connection.
// Queue it to the requests outgoing data. Write to raw connection only if
// this is the current request.
//
void qdr_http1_write_body_data(qdr_http1_request_t *hreq, qd_message_body_data_t *body_data)
{
    int count = qd_message_body_data_buffer_count(body_data);
    if (count) {
        qdr_http1_out_data_t *od = new_qdr_http1_out_data_t();
        ZERO(od);
        od->body_data = body_data;
        od->buffer_count = count;

        od->hreq = hreq;
        DEQ_INSERT_TAIL(hreq->out_fifo, od);
        if (!hreq->write_ptr)
            hreq->write_ptr = od;

        if (hreq == DEQ_HEAD(hreq->hconn->requests))
            qdr_http1_write_out_data(hreq->hconn);

    } else {
        // empty body-data
        qd_message_body_data_release(body_data);
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

#if 1
                {
                    char *ptr = (char*) buffers[i].bytes;
                    int len = (int) buffers[i].size;
                    fprintf(stdout, "\n\nRaw Written: Ptr=%p len=%d\n  value='%.*s'\n\n", (void*)ptr, len, len, ptr);
                    fflush(stdout);
                }
#endif

            qdr_http1_out_data_t *od = (qdr_http1_out_data_t*) buffers[i].context;
            assert(od);
            // Note: according to proton devs the order in which write buffers
            // are released are NOT guaranteed to be in the same order in which
            // they were written!

            od->free_count += 1;
            if (od->free_count == od->buffer_count) {
                // all buffers returned
                qdr_http1_request_t *hreq = od->hreq;
                DEQ_REMOVE(hreq->out_fifo, od);
                if (od->body_data)
                    qd_message_body_data_release(od->body_data);
                else
                    qd_buffer_list_free_buffers(&od->raw_buffers);
                free_qdr_http1_out_data_t(od);
            }
        }
    }
}


//
// Protocol Adaptor Callbacks
//


// Invoked by the core thread to wake an I/O thread for the connection
//
static void _core_connection_activate_CT(void *context, qdr_connection_t *conn)
{
    qdr_http1_connection_t *hconn = (qdr_http1_connection_t*) qdr_connection_get_context(conn);
    if (!hconn) return;

    qd_log(qdr_http1_adaptor->log, QD_LOG_DEBUG, "[C%"PRIu64"] Connection activate", hconn->conn_id);

    if (hconn->raw_conn)
        pn_raw_connection_wake(hconn->raw_conn);
    else
        qd_log(qdr_http1_adaptor->log, QD_LOG_DEBUG, "[C%"PRIu64"] missing raw connection!", hconn->conn_id);
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


// Handle disposition/settlement update for the outstanding incoming HTTP message
//
static void _core_delivery_update(void *context, qdr_delivery_t *dlv, uint64_t disp, bool settled)
{
    qdr_http1_request_t *hreq = (qdr_http1_request_t*) qdr_delivery_get_context(dlv);
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

static void _core_conn_close(void *context, qdr_connection_t *conn, qdr_error_t *error)
{
    qdr_http1_connection_t *hconn = (qdr_http1_connection_t*) qdr_connection_get_context(conn);
    if (hconn) {
        if (hconn->trace)
            qd_log(qdr_http1_adaptor->log, QD_LOG_TRACE,
                   "[C%"PRIu64"] HTTP/1.x closing connection", hconn->conn_id);

        char *qdr_error = error ? qdr_error_description(error) : 0;
        hconn->qdr_conn = 0;
        hconn->out_link = 0;
        hconn->in_link = 0;
        qdr_http1_close_connection(hconn, qdr_error);
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
    adaptor->log = qd_log_source(QD_HTTP_LOG_SOURCE);
    DEQ_INIT(adaptor->listeners);
    DEQ_INIT(adaptor->connectors);
    *adaptor_context = adaptor;

    qdr_http1_adaptor = adaptor;
}


static void qd_http1_adaptor_final(void *adaptor_context)
{
    qdr_http1_adaptor_t *adaptor = (qdr_http1_adaptor_t*) adaptor_context;
    qdr_protocol_adaptor_free(adaptor->core, adaptor->adaptor);
    // @TODO(kgiusti): proper clean up
    free(adaptor);
    qdr_http1_adaptor =  NULL;
}


/**
 * Declare the adaptor so that it will self-register on process startup.
 */
QDR_CORE_ADAPTOR_DECLARE("http1.x-adaptor", qd_http1_adaptor_init, qd_http1_adaptor_final)


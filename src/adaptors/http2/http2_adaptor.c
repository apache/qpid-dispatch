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
#include "http2_adaptor.h"

#include "adaptors/http_common.h"

#include "qpid/dispatch/buffer.h"
#include "qpid/dispatch/protocol_adaptor.h"

#include <proton/condition.h>
#include <proton/listener.h>
#include <proton/netaddr.h>
#include <proton/proactor.h>
#include <proton/raw_connection.h>

#include <inttypes.h>
#include <nghttp2/nghttp2.h>
#include <pthread.h>
#include <stdio.h>

const char *PATH = ":path";
const char *METHOD = ":method";
const char *STATUS = ":status";
const char *CONTENT_TYPE = "content-type";
const char *CONTENT_ENCODING = "content-encoding";
static const int BACKLOG = 50;  /* Listening backlog */
static const int PING_INTERVAL = 4;  // Send a ping every 4 seconds.

#define DEFAULT_CAPACITY 250
#define READ_BUFFERS 4
#define WRITE_BUFFERS 4
#define ARRLEN(x) (sizeof(x) / sizeof(x[0]))

ALLOC_DEFINE(qdr_http2_session_data_t);
ALLOC_DEFINE(qdr_http2_stream_data_t);
ALLOC_DEFINE(qdr_http2_connection_t);
ALLOC_DEFINE(qd_http2_buffer_t);

typedef struct qdr_http2_adaptor_t {
    qdr_core_t                  *core;
    qdr_protocol_adaptor_t      *adaptor;
    qd_http_listener_list_t      listeners;   // A list of all http2 listeners
    qd_http_connector_list_t     connectors;  // A list of all http2 connectors
    qd_log_source_t             *log_source;
    void                        *callbacks;
    qd_log_source_t             *protocol_log_source; // A log source for the protocol trace
    qdr_http2_connection_list_t  connections;
    sys_mutex_t                 *lock;  // protects connections, connectors, listener lists
} qdr_http2_adaptor_t;


static qdr_http2_adaptor_t *http2_adaptor;

static void handle_connection_event(pn_event_t *e, qd_server_t *qd_server, void *context);
static void _http_record_request(qdr_http2_connection_t *conn, qdr_http2_stream_data_t *stream_data);
static void free_http2_stream_data(qdr_http2_stream_data_t *stream_data, bool on_shutdown);

static void free_all_connection_streams(qdr_http2_connection_t *http_conn, bool on_shutdown)
{
    // Free all the stream data associated with this connection/session.
    qdr_http2_stream_data_t *stream_data = DEQ_HEAD(http_conn->session_data->streams);
    while (stream_data) {
        qd_log(http2_adaptor->log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] Freeing stream in free_qdr_http2_connection", stream_data->session_data->conn->conn_id, stream_data->stream_id);
        free_http2_stream_data(stream_data, on_shutdown);
        stream_data = DEQ_HEAD(http_conn->session_data->streams);
    }
}

static void set_stream_data_delivery_flags(qdr_http2_stream_data_t * stream_data, qdr_delivery_t *dlv) {
    if (dlv == stream_data->in_dlv) {
        stream_data->in_dlv_decrefed = true;
    }
    if (dlv == stream_data->out_dlv) {
        stream_data->out_dlv_decrefed = true;
    }
}

static void advance_stream_status(qdr_http2_stream_data_t *stream_data)
{
    qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] Trying to move stream status", stream_data->session_data->conn->conn_id, stream_data->stream_id);
    if (stream_data->status == QD_STREAM_OPEN) {
        stream_data->status = QD_STREAM_HALF_CLOSED;
        qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] Moving stream status to QD_STREAM_HALF_CLOSED", stream_data->session_data->conn->conn_id, stream_data->stream_id);
    }
    else if (stream_data->status == QD_STREAM_HALF_CLOSED) {
        stream_data->status = QD_STREAM_FULLY_CLOSED;
        qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] Moving stream status to QD_STREAM_FULLY_CLOSED", stream_data->session_data->conn->conn_id, stream_data->stream_id);
    }
    else if (stream_data->status == QD_STREAM_FULLY_CLOSED) {
        qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] Not moving stream status, stream is already QD_STREAM_FULLY_CLOSED", stream_data->session_data->conn->conn_id, stream_data->stream_id);
    }
    else {
        qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] Unknown stream status", stream_data->session_data->conn->conn_id, stream_data->stream_id);
    }
}


qd_http2_buffer_t *qd_http2_buffer(void)
{
    qd_http2_buffer_t *buf = new_qd_http2_buffer_t();
    ZERO(buf);
    DEQ_ITEM_INIT(buf);
    buf->size   = 0;
    return buf;
}

void qd_http2_buffer_list_append(qd_http2_buffer_list_t *buflist, const uint8_t *data, size_t len)
{
    //
    // If len is zero, there's no work to do.
    //
    if (len == 0)
        return;

    //
    // If the buffer list is empty and there's some data, add one empty buffer before we begin.
    //
    if (DEQ_SIZE(*buflist) == 0) {
        qd_http2_buffer_t *buf = qd_http2_buffer();
        DEQ_INSERT_TAIL(*buflist, buf);
    }

    qd_http2_buffer_t *tail = DEQ_TAIL(*buflist);

    while (len > 0) {
        size_t to_copy = MIN(len, qd_http2_buffer_capacity(tail));
        if (to_copy > 0) {
            memcpy(qd_http2_buffer_cursor(tail), data, to_copy);
            qd_http2_buffer_insert(tail, to_copy);
            data += to_copy;
            len  -= to_copy;
        }
        if (len > 0) {
            tail = qd_http2_buffer();
            DEQ_INSERT_TAIL(*buflist, tail);
        }
    }
}

/**
 * HTTP :path is mapped to the AMQP 'to' field.
 */
qd_composed_field_t  *qd_message_compose_amqp(qd_message_t *msg,
                                              const char *to,
                                              const char *subject,
                                              const char *reply_to,
                                              const char *content_type,
                                              const char *content_encoding,
                                              int32_t  correlation_id,
                                              const char* group_id)
{
    qd_composed_field_t  *field   = qd_compose(QD_PERFORMATIVE_HEADER, 0);
    qd_message_content_t *content = MSG_CONTENT(msg);
    if (!content) {
        qd_compose_free(field);
        return 0;
    }
    //
    // Header
    //
    qd_compose_start_list(field);
    qd_compose_insert_bool(field, 0);     // durable
    qd_compose_insert_null(field);        // priority
    //qd_compose_insert_null(field);        // ttl
    //qd_compose_insert_bool(field, 0);     // first-acquirer
    //qd_compose_insert_uint(field, 0);     // delivery-count
    qd_compose_end_list(field);

    //
    // Properties
    //
    field = qd_compose(QD_PERFORMATIVE_PROPERTIES, field);
    qd_compose_start_list(field);
    qd_compose_insert_null(field);          // message-id
    qd_compose_insert_null(field);          // user-id
    if (to) {
        qd_compose_insert_string(field, to);    // to
    }
    else {
        qd_compose_insert_null(field);
    }

    if (subject) {
        qd_compose_insert_string(field, subject);      // subject
    }
    else {
        qd_compose_insert_null(field);
    }

    if (reply_to) {
        qd_compose_insert_string(field, reply_to); // reply-to
    }
    else {
        qd_compose_insert_null(field);
    }

    if (correlation_id > 0) {
        qd_compose_insert_int(field, correlation_id);
    }
    else {
        qd_compose_insert_null(field);          // correlation-id
    }

    if (content_type) {
        qd_compose_insert_string(field, content_type);        // content-type
    }
    else {
        qd_compose_insert_null(field);
    }
    if (content_encoding) {
        qd_compose_insert_string(field, content_encoding);               // content-encoding
    }
    else {
        qd_compose_insert_null(field);
    }
    qd_compose_insert_null(field);                      // absolute-expiry-time
    qd_compose_insert_null(field);                      // creation-time
    if (group_id) {
        qd_compose_insert_string(field, group_id);      // group-id
    } else {
        qd_compose_insert_null(field);
    }
    qd_compose_end_list(field);

    return field;
}

static size_t write_buffers(qdr_http2_connection_t *conn)
{
    qdr_http2_session_data_t *session_data = conn->session_data;
    size_t pn_buffs_to_write = pn_raw_connection_write_buffers_capacity(conn->pn_raw_conn);

    qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"] write_buffers pn_raw_connection_write_buffers_capacity=%zu", conn->conn_id,  pn_buffs_to_write);

    size_t qd_raw_buffs_to_write = DEQ_SIZE(session_data->buffs);
    size_t num_buffs = qd_raw_buffs_to_write > pn_buffs_to_write ? pn_buffs_to_write : qd_raw_buffs_to_write;

    if (num_buffs == 0) {
        //
        // No buffers to write, cannot proceed.
        //
        qd_log(http2_adaptor->log_source, QD_LOG_TRACE, "[C%"PRIu64"] Written 0 buffers in write_buffers() - pn_raw_connection_write_buffers_capacity = %zu, DEQ_SIZE(session_data->buffs) = %zu - returning", conn->conn_id, pn_buffs_to_write, DEQ_SIZE(session_data->buffs));
        return num_buffs;
    }

    pn_raw_buffer_t raw_buffers[num_buffs];
    qd_http2_buffer_t *qd_http2_buff = DEQ_HEAD(session_data->buffs);

    int i = 0;
    int total_bytes = 0;
    while (i < num_buffs && qd_http2_buff != 0) {
        raw_buffers[i].bytes = (char *)qd_http2_buffer_base(qd_http2_buff);
        size_t buffer_size = qd_http2_buffer_size(qd_http2_buff);
        raw_buffers[i].capacity = buffer_size;
        raw_buffers[i].size = buffer_size;
        total_bytes += buffer_size;
        raw_buffers[i].offset = 0;
        raw_buffers[i].context = (uintptr_t) qd_http2_buff;
        DEQ_REMOVE_HEAD(session_data->buffs);
        qd_http2_buff = DEQ_HEAD(session_data->buffs);
        i ++;

    }

    if (i >0) {
        size_t num_buffers_written = pn_raw_connection_write_buffers(session_data->conn->pn_raw_conn, raw_buffers, num_buffs);
        qd_log(http2_adaptor->log_source, QD_LOG_TRACE, "[C%"PRIu64"] Written %i buffer(s) and %i bytes in write_buffers() using pn_raw_connection_write_buffers()", conn->conn_id, num_buffers_written, total_bytes);
        if (num_buffs != num_buffers_written) {
            //TODO - This is not good.
        }
        return num_buffers_written;
    }

    return 0;
}

static void free_http2_stream_data(qdr_http2_stream_data_t *stream_data, bool on_shutdown)
{
    if (!stream_data)
        return;

    qdr_http2_session_data_t *session_data = stream_data->session_data;
    qdr_http2_connection_t *conn = session_data->conn;

    // Record the request just before freeing the stream.
    _http_record_request(conn, stream_data);

    if (!on_shutdown) {
        if (conn->qdr_conn && stream_data->in_link) {
            qdr_link_set_context(stream_data->in_link, 0);
            qdr_link_detach(stream_data->in_link, QD_CLOSED, 0);
        }
        if (conn->qdr_conn && stream_data->out_link) {
            qdr_link_set_context(stream_data->out_link, 0);
            qdr_link_detach(stream_data->out_link, QD_CLOSED, 0);
        }
    }
    free(stream_data->reply_to);
    qd_compose_free(stream_data->app_properties);
    qd_compose_free(stream_data->body);
    qd_compose_free(stream_data->footer_properties);
    if (DEQ_SIZE(session_data->streams) > 0) {
        DEQ_REMOVE(session_data->streams, stream_data);
        nghttp2_session_set_stream_user_data(session_data->session, stream_data->stream_id, NULL);
    }
    if (stream_data->method)      free(stream_data->method);
    if (stream_data->remote_site) free(stream_data->remote_site);

    qd_log(http2_adaptor->log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] Freeing stream_data in free_http2_stream_data (%lx)", conn->conn_id,  stream_data->stream_id, (long) stream_data);

    // If the httpConnector was deleted, a client request has nowhere to go because of lack of receiver and hence credit.
    // No delivery was created. The message that was created for such a hanging request must be freed here..
    if (!stream_data->in_dlv && stream_data->message) {
        qd_message_free(stream_data->message);
    }

    if (stream_data->in_dlv && !stream_data->in_dlv_decrefed) {
        qdr_delivery_decref(http2_adaptor->core, stream_data->in_dlv, "HTTP2 adaptor in_dlv - free_http2_stream_data");
    }

    if (stream_data->out_dlv && !stream_data->out_dlv_decrefed) {
        qdr_delivery_decref(http2_adaptor->core, stream_data->out_dlv, "HTTP2 adaptor out_dlv - free_http2_stream_data");
    }

    free_qdr_http2_stream_data_t(stream_data);
}


static char *get_address_string(pn_raw_connection_t *pn_raw_conn)
{
    const pn_netaddr_t *netaddr = pn_raw_connection_remote_addr(pn_raw_conn);
    char buffer[1024];
    int len = pn_netaddr_str(netaddr, buffer, 1024);
    if (len <= 1024) {
        return strdup(buffer);
    } else {
        return strndup(buffer, 1024);
    }
}

void free_qdr_http2_connection(qdr_http2_connection_t* http_conn, bool on_shutdown)
{
    // Free all the stream data associated with this connection/session.
    free_all_connection_streams(http_conn, on_shutdown);

    if(http_conn->remote_address) {
        free(http_conn->remote_address);
        http_conn->remote_address = 0;
    }
    if (http_conn->activate_timer) {
        qd_timer_free(http_conn->activate_timer);
        http_conn->activate_timer = 0;
    }

    if (http_conn->ping_timer) {
        qd_timer_free(http_conn->ping_timer);
        http_conn->ping_timer = 0;
    }

    http_conn->context.context = 0;

    if (http_conn->session_data->session)
        nghttp2_session_del(http_conn->session_data->session);

    free_qdr_http2_session_data_t(http_conn->session_data);
    http_conn->session_data = 0;
    sys_mutex_lock(http2_adaptor->lock);
    DEQ_REMOVE(http2_adaptor->connections, http_conn);
    sys_mutex_unlock(http2_adaptor->lock);

    qd_http2_buffer_t *buff = DEQ_HEAD(http_conn->granted_read_buffs);
    while (buff) {
        DEQ_REMOVE_HEAD(http_conn->granted_read_buffs);
        free_qd_http2_buffer_t(buff);
        buff = DEQ_HEAD(http_conn->granted_read_buffs);
    }

    qd_log(http2_adaptor->log_source, QD_LOG_TRACE, "[C%"PRIu64"] Freeing http2 connection in free_qdr_http2_connection", http_conn->conn_id);

    free_qdr_http2_connection_t(http_conn);
}

static qdr_http2_stream_data_t *create_http2_stream_data(qdr_http2_session_data_t *session_data, int32_t stream_id)
{
    qdr_http2_stream_data_t *stream_data = new_qdr_http2_stream_data_t();

    ZERO(stream_data);
    stream_data->stream_id = stream_id;

    qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] Created new stream_data (%lx)", session_data->conn->conn_id, stream_id, (long) stream_data);

    stream_data->message = qd_message();
    qd_message_set_stream_annotation(stream_data->message, true);
    stream_data->session_data = session_data;
    stream_data->app_properties = qd_compose(QD_PERFORMATIVE_APPLICATION_PROPERTIES, 0);
    stream_data->status = QD_STREAM_OPEN;
    stream_data->start = qd_timer_now();
    qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] Creating new stream_data->app_properties=QD_PERFORMATIVE_APPLICATION_PROPERTIES", session_data->conn->conn_id, stream_id);
    qd_compose_start_map(stream_data->app_properties);
    nghttp2_session_set_stream_user_data(session_data->session, stream_id, stream_data);
    DEQ_INSERT_TAIL(session_data->streams, stream_data);
    stream_data->out_msg_has_body = true;
    return stream_data;
}


/**
 * Callback function invoked by nghttp2_session_recv() and nghttp2_session_mem_recv() when an invalid non-DATA frame is received
 */
static int on_invalid_frame_recv_callback(nghttp2_session *session, const nghttp2_frame *frame, int lib_error_code, void *user_data)
{
    qdr_http2_connection_t *conn = (qdr_http2_connection_t *)user_data;
    int32_t stream_id = frame->hd.stream_id;
    qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] on_invalid_frame_recv_callback", conn->conn_id, stream_id);
    return 0;
}


static int on_data_chunk_recv_callback(nghttp2_session *session,
                                       uint8_t flags,
                                       int32_t stream_id,
                                       const uint8_t *data,
                                       size_t len,
                                       void *user_data)
{
    qdr_http2_connection_t *conn = (qdr_http2_connection_t *)user_data;
    qdr_http2_session_data_t *session_data = conn->session_data;
    qdr_http2_stream_data_t *stream_data = nghttp2_session_get_stream_user_data(session_data->session, stream_id);

    if (!stream_data)
        return 0;

    stream_data->bytes_in += len;
    qd_buffer_list_t buffers;
    DEQ_INIT(buffers);
    qd_buffer_list_append(&buffers, (uint8_t *)data, len);

    //
    // DISPATCH-: If an in_dlv is present it means that the qdr_link_deliver() has already been called (delivery has already been routed)
    // in which case qd_message_stream_data_append can be called to append buffers to the message body
    // If stream_data->in_dlv = 0 but stream_data->header_and_props_composed is true, it means that the message has not been routed yet
    // but the message already has headers and properties
    // in which case the qd_message_stream_data_append() can be called to add body data to the message.
    // In many cases when the response message is streamed by a server, the entire message body can arrive before we get credit to route it.
    // We want to be able to keep collecting the incoming DATA in the message object so we can ultimately route it when the credit does ultimately arrive.
    //
    if (stream_data->in_dlv || stream_data->header_and_props_composed) {
        if (!stream_data->stream_force_closed) {
            // DISPATCH-1868: Part of the HTTP2 message body arrives *before* we can route the delivery. So we accumulated that body
            // in the stream_data->body (in the else part). But before the rest of the HTTP2 data arrives, we got credit to send the delivery
            // and we have an in_dlv object now. Now, we take the buffers that were added previously to stream_data->body and call qd_message_stream_data_append
            if (stream_data->body) {
                if (!stream_data->body_data_added) {
                    qd_buffer_list_t existing_buffers;
                    DEQ_INIT(existing_buffers);
                    qd_compose_take_buffers(stream_data->body, &existing_buffers);
                    // @TODO(kgiusti): handle Q2 block event:
                    qd_message_stream_data_append(stream_data->message, &existing_buffers, 0);
                    stream_data->body_data_added = true;
                }
            }
            else {
                // Add a dummy body so that other code that checks for the presense of stream_data->body will be satisfied.
                // This dummy body field will be be used and will not be sent.
                stream_data->body = qd_compose(QD_PERFORMATIVE_BODY_DATA, 0);
                stream_data->body_data_added = true;
            }
            // @TODO(kgiusti): handle Q2 block event:
            qd_message_stream_data_append(stream_data->message, &buffers, 0);
            qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] HTTP2 DATA on_data_chunk_recv_callback qd_compose_insert_binary_buffers into stream_data->message", conn->conn_id, stream_id);
        }
        else {
            qd_buffer_list_free_buffers(&buffers);
        }
    }
    else {
        if (stream_data->stream_force_closed) {
            qd_buffer_list_free_buffers(&buffers);
        }
        else {
            stream_data->body = qd_compose(QD_PERFORMATIVE_BODY_DATA, stream_data->body);
            qd_compose_insert_binary_buffers(stream_data->body, &buffers);
            qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] HTTP2 DATA on_data_chunk_recv_callback qd_compose_insert_binary_buffers into stream_data->body", conn->conn_id, stream_id);
        }
    }

    qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] HTTP2 DATA on_data_chunk_recv_callback data length %zu", conn->conn_id, stream_id, len);

    // Calling this here to send out any WINDOW_UPDATE frames that might be necessary.
    // The only function that nghttp2 calls if it wants to send data is the send_callback.
    // The only function that calls send_callback is nghttp2_session_send
    nghttp2_session_send(session_data->session);

    //Returning zero means success.
    return 0;
}



/**
 * Callback function invoked when NGHTTP2_DATA_FLAG_NO_COPY is used in nghttp2_data_source_read_callback to send complete DATA frame.
 */
static int on_stream_close_callback(nghttp2_session *session,
                                    int32_t stream_id,
                                    nghttp2_error_code error_code,
                                    void *user_data)
{
    return 0;
}


static int snd_data_callback(nghttp2_session *session,
                             nghttp2_frame *frame,
                             const uint8_t *framehd,
                             size_t length,
                             nghttp2_data_source *source,
                             void *user_data) {
    // The frame is a DATA frame to send. The framehd is the serialized frame header (9 bytes).
    // The length is the length of application data to send (this does not include padding)
    qdr_http2_connection_t *conn = (qdr_http2_connection_t *)user_data;
    qdr_http2_session_data_t *session_data = conn->session_data;
    qdr_http2_stream_data_t *stream_data = (qdr_http2_stream_data_t *)source->ptr;

    int bytes_sent = 0; // This should not include the header length of 9.
    bool write_buffs = false;
    if (length) {
        write_buffs = true;
        qd_http2_buffer_t *http2_buff = qd_http2_buffer();
        DEQ_INSERT_TAIL(session_data->buffs, http2_buff);
        // Insert the framehd of length 9 bytes into the buffer
        memcpy(qd_http2_buffer_cursor(http2_buff), framehd, HTTP2_DATA_FRAME_HEADER_LENGTH);
        qd_http2_buffer_insert(http2_buff, HTTP2_DATA_FRAME_HEADER_LENGTH);
        pn_raw_buffer_t pn_raw_buffs[stream_data->qd_buffers_to_send];
        qd_message_stream_data_buffers(stream_data->curr_stream_data, pn_raw_buffs, 0, stream_data->qd_buffers_to_send);

        int idx = 0;
        while (idx < stream_data->qd_buffers_to_send) {
            if (pn_raw_buffs[idx].size > 0) {
                //int bytes_remaining = length - bytes_sent;
                //if (bytes_remaining > pn_raw_buffs[idx].size) {
                    memcpy(qd_http2_buffer_cursor(http2_buff), pn_raw_buffs[idx].bytes, pn_raw_buffs[idx].size);
                    qd_http2_buffer_insert(http2_buff, pn_raw_buffs[idx].size);
                    bytes_sent += pn_raw_buffs[idx].size;
                    qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] snd_data_callback memcpy pn_raw_buffs[%i].size=%zu", conn->conn_id, stream_data->stream_id, idx, pn_raw_buffs[idx].size);
//                }
//                else {
//                    memcpy(qd_http2_buffer_cursor(http2_buff), pn_raw_buffs[idx].bytes, bytes_remaining);
//                    qd_http2_buffer_insert(http2_buff, bytes_remaining);
//                    bytes_sent += bytes_remaining;
//                    qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] snd_data_callback memcpy bytes_remaining=%i", conn->conn_id, stream_data->stream_id, bytes_remaining);
//                }
                stream_data->curr_stream_data_qd_buff_offset += 1;
            }
            idx += 1;
        }
    }
    else if (length == 0 && stream_data->out_msg_data_flag_eof) {
        write_buffs = true;
        qd_http2_buffer_t *http2_buff = qd_http2_buffer();
        DEQ_INSERT_TAIL(session_data->buffs, http2_buff);
        // Insert the framehd of length 9 bytes into the buffer
        memcpy(qd_http2_buffer_cursor(http2_buff), framehd, HTTP2_DATA_FRAME_HEADER_LENGTH);
        qd_http2_buffer_insert(http2_buff, HTTP2_DATA_FRAME_HEADER_LENGTH);
    }

    if (stream_data->full_payload_handled) {
        if (!stream_data->out_msg_has_footer && stream_data->curr_stream_data) {
            qd_message_stream_data_release(stream_data->curr_stream_data);
            stream_data->curr_stream_data = 0;
            qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] snd_data_callback, full_payload_handled, no footer, qd_message_stream_data_release", conn->conn_id, stream_data->stream_id);
        }
        else {
            qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] snd_data_callback, full_payload_handled, out_msg_has_footer", conn->conn_id, stream_data->stream_id);
        }
        stream_data->curr_stream_data_qd_buff_offset = 0;
    }

    qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] HTTP2 snd_data_callback finished, length=%zu, bytes_sent=%i, stream_data=%p", conn->conn_id, stream_data->stream_id, length, bytes_sent, (void *)stream_data);

    if (length) {
        assert(bytes_sent == length);
    }

    if (write_buffs)
        write_buffers(conn);

    return 0;

}

static ssize_t send_callback(nghttp2_session *session,
                             const uint8_t *data,
                             size_t length,
                             int flags,
                             void *user_data) {
    qdr_http2_connection_t *conn = (qdr_http2_connection_t *)user_data;
    qdr_http2_session_data_t *session_data = conn->session_data;
    qd_http2_buffer_list_append(&(session_data->buffs), (uint8_t *)data, length);
    qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"] HTTP2 send_callback data length %zu", conn->conn_id, length);
    write_buffers(conn);
    return (ssize_t)length;
}

/**
 * This callback function is invoked with the reception of header block in HEADERS or PUSH_PROMISE is started.
 * The HEADERS frame can arrive from a client or server. We start building a new AMQP message (qd_message_t) in this callback and create the two links per stream.
 *
 * Return zero if function succeeds.
 */
static int on_begin_headers_callback(nghttp2_session *session,
                                     const nghttp2_frame *frame,
                                     void *user_data)
{
    qdr_http2_connection_t *conn = (qdr_http2_connection_t *)user_data;
    qdr_http2_session_data_t *session_data = conn->session_data;
    qdr_http2_stream_data_t *stream_data = 0;

    // For the client applications, frame->hd.type is either NGHTTP2_HEADERS or NGHTTP2_PUSH_PROMISE
    // TODO - deal with NGHTTP2_PUSH_PROMISE
    if (frame->hd.type == NGHTTP2_HEADERS) {
        if(frame->headers.cat == NGHTTP2_HCAT_REQUEST && conn->ingress) {
            if (!conn->qdr_conn) {
                return 0;
            }

            int32_t stream_id = frame->hd.stream_id;
            qdr_terminus_t *target = qdr_terminus(0);
            qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"] Processing incoming HTTP2 stream with id %"PRId32"", conn->conn_id, stream_id);
            stream_data = create_http2_stream_data(session_data, stream_id);

            //
            // For every single stream in the same connection, create  -
            // 1. sending link with the configured address as the target
            //
            qdr_terminus_set_address(target, conn->config->address);
            stream_data->in_link = qdr_link_first_attach(conn->qdr_conn,
                                                         QD_INCOMING,
                                                         qdr_terminus(0),  //qdr_terminus_t   *source,
                                                         target,           //qdr_terminus_t   *target,
                                                         "http.ingress.in",         //const char       *name,
                                                         0,                //const char       *terminus_addr,
                                                         false,
                                                         NULL,
                                                         &(stream_data->incoming_id));
            qdr_link_set_context(stream_data->in_link, stream_data);

            //
            // 2. dynamic receiver on which to receive back the response data for that stream.
            //
            qdr_terminus_t *dynamic_source = qdr_terminus(0);
            qdr_terminus_set_dynamic(dynamic_source);
            stream_data->out_link = qdr_link_first_attach(conn->qdr_conn,
                                                          QD_OUTGOING,   //Receiver
                                                          dynamic_source,   //qdr_terminus_t   *source,
                                                          qdr_terminus(0),  //qdr_terminus_t   *target,
                                                          "http.ingress.out",        //const char       *name,
                                                          0,                //const char       *terminus_addr,
                                                          false,
                                                          NULL,
                                                          &(stream_data->outgoing_id));
            qdr_link_set_context(stream_data->out_link, stream_data);
        }
    }

    return 0;
}

/**
 *  nghttp2_on_header_callback: Called when nghttp2 library emits
 *  single header name/value pair.
 *  Collects all headers in the application properties map of the AMQP
 *
 *  @return zero if function succeeds.
 */
static int on_header_callback(nghttp2_session *session,
                              const nghttp2_frame *frame,
                              const uint8_t *name,
                              size_t namelen,
                              const uint8_t *value,
                              size_t valuelen,
                              uint8_t flags,
                              void *user_data)
{
    int32_t stream_id = frame->hd.stream_id;
    qdr_http2_connection_t *conn = (qdr_http2_connection_t *)user_data;
    qdr_http2_session_data_t *session_data = conn->session_data;
    qdr_http2_stream_data_t *stream_data = nghttp2_session_get_stream_user_data(session_data->session, stream_id);

    switch (frame->hd.type) {
        case NGHTTP2_HEADERS: {
            if (stream_data->use_footer_properties) {
                if (!stream_data->footer_properties) {
                    stream_data->footer_properties = qd_compose(QD_PERFORMATIVE_FOOTER, 0);
                    qd_compose_start_map(stream_data->footer_properties);
                }

                qd_compose_insert_string_n(stream_data->footer_properties, (const char *)name, namelen);
                qd_compose_insert_string_n(stream_data->footer_properties, (const char *)value, valuelen);
            }
            else {
                if (strcmp(METHOD, (const char *)name) == 0) {
                    stream_data->method = qd_strdup((const char *)value);
                }
                if (strcmp(STATUS, (const char *)name) == 0) {
                    stream_data->request_status = atoi((const char *)value);
                }
                qd_compose_insert_string_n(stream_data->app_properties, (const char *)name, namelen);
                qd_compose_insert_string_n(stream_data->app_properties, (const char *)value, valuelen);
            }
            qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] HTTP2 HEADER Incoming [%s=%s]", conn->conn_id, stream_data->stream_id, (char *)name, (char *)value);
        }
        break;
        default:
            break;
    }
    return 0;
}


static bool compose_and_deliver(qdr_http2_connection_t *conn, qdr_http2_stream_data_t *stream_data, bool receive_complete)
{
    if (!stream_data->header_and_props_composed) {
        qd_composed_field_t  *header_and_props = 0;
        if (conn->ingress) {
            header_and_props = qd_message_compose_amqp(stream_data->message,
                                                  conn->config->address,  // const char *to
                                                  0,                      // const char *subject
                                                  stream_data->reply_to,  // const char *reply_to
                                                  0,                      // const char *content_type
                                                  0,                      // const char *content_encoding
                                                  0,                      // int32_t  correlation_id
                                                  conn->config->site);
        }
        else {
            header_and_props = qd_message_compose_amqp(stream_data->message,
                                                  stream_data->reply_to,  // const char *to
                                                  0,                      // const char *subject
                                                  0,                      // const char *reply_to
                                                  0,                      // const char *content_type
                                                  0,                      // const char *content_encoding
                                                  0,                      // int32_t  correlation_id
                                                  conn->config->site);
        }

        if (receive_complete) {
            qd_log(http2_adaptor->log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"][L%"PRIu64"] receive_complete = true in compose_and_deliver", conn->conn_id, stream_data->stream_id, stream_data->in_link->identity);
            if (!stream_data->body) {
                stream_data->body = qd_compose(QD_PERFORMATIVE_BODY_DATA, 0);
                qd_compose_insert_binary(stream_data->body, 0, 0);
                qd_log(http2_adaptor->log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] Inserting empty body data in compose_and_deliver", conn->conn_id, stream_data->stream_id);
            }

            if (stream_data->footer_properties) {
                qd_message_compose_5(stream_data->message, header_and_props, stream_data->app_properties, stream_data->body, stream_data->footer_properties, receive_complete);
            }
            else {
                qd_message_compose_4(stream_data->message, header_and_props, stream_data->app_properties, stream_data->body, receive_complete);
            }
        }
        else {
            if (stream_data->body) {
                qd_log(http2_adaptor->log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"][L%"PRIu64"] receive_complete = false and has stream_data->body in compose_and_deliver", conn->conn_id, stream_data->stream_id, stream_data->in_link->identity);
                if (stream_data->footer_properties) {
                    qd_message_compose_5(stream_data->message, header_and_props, stream_data->app_properties, stream_data->body, stream_data->footer_properties, receive_complete);
                }
                else {
                    qd_message_compose_4(stream_data->message, header_and_props, stream_data->app_properties, stream_data->body, receive_complete);
                }
                stream_data->body_data_added = true;
            }
            else {

                if (stream_data->footer_properties) {
                    //
                    // The footer has already arrived but there was no body. Insert an empty body
                    //
                    stream_data->body = qd_compose(QD_PERFORMATIVE_BODY_DATA, 0);
                    qd_message_compose_5(stream_data->message, header_and_props, stream_data->app_properties, stream_data->body, stream_data->footer_properties, receive_complete);
                }
                else {
                    qd_message_compose_3(stream_data->message, header_and_props, stream_data->app_properties, receive_complete);
                }
            }
        }

        // The header and properties have been added. Now we can start adding BODY DATA to this message.
        stream_data->header_and_props_composed = true;
        qd_log(http2_adaptor->log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"][L%"PRIu64"] stream_data->header_and_props_composed = true in compose_and_deliver", conn->conn_id, stream_data->stream_id, stream_data->in_link->identity);
        qd_compose_free(header_and_props);
    }

    if (!stream_data->in_dlv && stream_data->in_link_credit > 0) {
        //
        // Not doing an incref here since the qdr_link_deliver increfs the delivery twice
        //
        stream_data->in_dlv = qdr_link_deliver(stream_data->in_link, stream_data->message, 0, false, 0, 0, 0, 0);
        qd_log(http2_adaptor->log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] Routed delivery in compose_and_deliver (conn->ingress=%i) "DLV_FMT, conn->conn_id, stream_data->stream_id, conn->ingress, DLV_ARGS(stream_data->in_dlv));
        qdr_delivery_set_context(stream_data->in_dlv, stream_data);
        stream_data->in_link_credit -= 1;
        return true;
    }
    return false;
}

static bool route_delivery(qdr_http2_stream_data_t *stream_data, bool receive_complete)
{
    qdr_http2_connection_t *conn  = stream_data->session_data->conn;
    if (stream_data->in_dlv) {
        qd_log(http2_adaptor->log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] in_dlv already present, not routing delivery", conn->conn_id, stream_data->stream_id);
        return false;
    }

    bool delivery_routed = false;

    if (conn->ingress) {
        if (stream_data->reply_to && stream_data->entire_header_arrived && !stream_data->in_dlv) {
            delivery_routed = compose_and_deliver(conn, stream_data, receive_complete);
        }
        if (!stream_data->reply_to) {
            qd_log(http2_adaptor->log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"][L%"PRIu64"] stream_data->reply_to is unavailable, did not route delivery in route_delivery", conn->conn_id, stream_data->stream_id, stream_data->in_link->identity);
        }
    }
    else {
        if (stream_data->entire_header_arrived && !stream_data->in_dlv) {
            qd_log(http2_adaptor->log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] Calling compose_and_deliver, routing delivery", conn->conn_id, stream_data->stream_id);
            delivery_routed = compose_and_deliver(conn, stream_data, receive_complete);
        }
    }

    return delivery_routed;
}

static void create_settings_frame(qdr_http2_connection_t *conn)
{
    qdr_http2_session_data_t *session_data = conn->session_data;
    nghttp2_settings_entry iv[3] = {{NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 100},
                                    {NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, 65536},
                                    {NGHTTP2_SETTINGS_ENABLE_PUSH, 0}};

    // You must call nghttp2_session_send after calling nghttp2_submit_settings
    int rv = nghttp2_submit_settings(session_data->session, NGHTTP2_FLAG_NONE, iv, ARRLEN(iv));
    if (rv != 0) {
        qd_log(http2_adaptor->log_source, QD_LOG_ERROR, "[C%"PRIu64"] Fatal error sending settings frame, rv=%i", conn->conn_id, rv);
        return;
    }
    qd_log(http2_adaptor->log_source, QD_LOG_TRACE, "[C%"PRIu64"] Initial SETTINGS frame sent", conn->conn_id);
}


static void send_ping_frame(qdr_http2_connection_t *conn)
{
    qdr_http2_session_data_t *session_data = conn->session_data;
    nghttp2_submit_ping(session_data->session, NGHTTP2_FLAG_NONE, 0);
    nghttp2_session_send(session_data->session);
}

static void send_settings_frame(qdr_http2_connection_t *conn)
{
    qdr_http2_session_data_t *session_data = conn->session_data;
    create_settings_frame(conn);
    nghttp2_session_send(session_data->session);
    write_buffers(session_data->conn);
}

static void _http_record_request(qdr_http2_connection_t *conn, qdr_http2_stream_data_t *stream_data)
{
    stream_data->stop = qd_timer_now();

    bool free_remote_addr = false;
    char *remote_addr;
    if (conn->ingress) {
        remote_addr = qd_get_host_from_host_port(conn->remote_address);
        if (remote_addr) {
            free_remote_addr = true;
        } else {
            remote_addr = conn->remote_address;
        }
    } else {
        remote_addr = conn->config?conn->config->host:0;
    }
    qd_http_record_request(http2_adaptor->core,
                           stream_data->method,
                           stream_data->request_status,
                           conn->config?conn->config->address:0,
                           remote_addr, conn->config?conn->config->site:0,
                           stream_data->remote_site,
                           conn->ingress, stream_data->bytes_in, stream_data->bytes_out,
                           stream_data->stop && stream_data->start ? stream_data->stop - stream_data->start : 0);
    if (free_remote_addr) {
        free(remote_addr);
    }
}

static int on_frame_recv_callback(nghttp2_session *session,
                                  const nghttp2_frame *frame,
                                  void *user_data)
{
    qdr_http2_connection_t *conn = (qdr_http2_connection_t *)user_data;
    qdr_http2_session_data_t *session_data = conn->session_data;

    int32_t stream_id = frame->hd.stream_id;
    qdr_http2_stream_data_t *stream_data = nghttp2_session_get_stream_user_data(session_data->session, stream_id);

    switch (frame->hd.type) {
    case NGHTTP2_PING: {
        qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] HTTP2 PING frame received", conn->conn_id, stream_id);
    }
    break;
    case NGHTTP2_PRIORITY: {
        qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] HTTP2 PRIORITY frame received", conn->conn_id, stream_id);
    }
    break;
    case NGHTTP2_SETTINGS: {
        qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] HTTP2 SETTINGS frame received", conn->conn_id, stream_id);
    }
    break;
    case NGHTTP2_WINDOW_UPDATE:
        qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] HTTP2 WINDOW_UPDATE frame received", conn->conn_id, stream_id);
    break;
    case NGHTTP2_DATA: {

        if (!stream_data)
            return 0;

        qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] NGHTTP2_DATA frame received", conn->conn_id, stream_id);

        if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
            if (!stream_data->stream_force_closed) {
                qd_message_set_receive_complete(stream_data->message);
                qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] NGHTTP2_DATA NGHTTP2_FLAG_END_STREAM flag received, setting receive_complete = true", conn->conn_id, stream_id);
            }
            advance_stream_status(stream_data);
        }

        if (stream_data->in_dlv && !stream_data->stream_force_closed) {
            if (!stream_data->body) {
                stream_data->body = qd_compose(QD_PERFORMATIVE_BODY_DATA, 0);
                qd_compose_insert_binary(stream_data->body, 0, 0);
                // @TODO(kgiusti): handle Q2 block event:
                qd_message_extend(stream_data->message, stream_data->body, 0);
            }
        }

        if (stream_data->in_dlv && !stream_data->stream_force_closed) {
            qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] NGHTTP2_DATA frame received, qdr_delivery_continue "DLV_FMT, conn->conn_id, stream_id, DLV_ARGS(stream_data->in_dlv));
            qdr_delivery_continue(http2_adaptor->core, stream_data->in_dlv, false);
        }

        if (stream_data->out_dlv && !stream_data->disp_updated && !stream_data->out_dlv_decrefed && stream_data->status == QD_STREAM_FULLY_CLOSED ) {
            stream_data->disp_updated = true;
            qdr_delivery_remote_state_updated(http2_adaptor->core, stream_data->out_dlv, stream_data->out_dlv_local_disposition, true, 0, false);
            qd_log(http2_adaptor->log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] In on_frame_recv_callback NGHTTP2_DATA QD_STREAM_FULLY_CLOSED, qdr_delivery_remote_state_updated(stream_data->out_dlv)", conn->conn_id, stream_data->stream_id);
        }
    }
    break;
    case NGHTTP2_HEADERS:
    case NGHTTP2_CONTINUATION: {
        if (!stream_data)
            return 0;
        if (frame->hd.type == NGHTTP2_CONTINUATION) {
            qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] HTTP2 CONTINUATION frame received", conn->conn_id, stream_id);
        }
        else {
            qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] HTTP2 HEADERS frame received", conn->conn_id, stream_id);
        }

        if (frame->hd.flags & NGHTTP2_FLAG_END_HEADERS) {
            /* All the headers have been received. Send out the AMQP message */
            qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] HTTP2 NGHTTP2_FLAG_END_HEADERS flag received, all headers have arrived", conn->conn_id, stream_id);
            stream_data->entire_header_arrived = true;

            if (stream_data->use_footer_properties) {
                qd_compose_end_map(stream_data->footer_properties);
                stream_data->entire_footer_arrived = true;
                // @TODO(kgiusti): handle Q2 block event:
                qd_message_extend(stream_data->message, stream_data->footer_properties, 0);
                qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] Closing footer map, extending message with footer", conn->conn_id, stream_id);
            }
            else {
                //
                // All header fields have been received. End the application properties map.
                //
                stream_data->use_footer_properties = true;
                qd_compose_end_map(stream_data->app_properties);
            }

            bool receive_complete = false;
            if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
                if (stream_data->entire_footer_arrived) {
                    qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] HTTP2 NGHTTP2_FLAG_END_HEADERS and NGHTTP2_FLAG_END_STREAM flag received (footer), receive_complete=true", conn->conn_id, stream_id);
                }
                else {
                    qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] HTTP2 NGHTTP2_FLAG_END_HEADERS and NGHTTP2_FLAG_END_STREAM flag received, receive_complete=true", conn->conn_id, stream_id);
                }
                qd_message_set_receive_complete(stream_data->message);
                advance_stream_status(stream_data);
                receive_complete = true;
            }

            if (stream_data->entire_footer_arrived) {
                if (stream_data->in_dlv) {
                    qdr_delivery_continue(http2_adaptor->core, stream_data->in_dlv, false);
                    qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] Entire footer arrived, qdr_delivery_continue "DLV_FMT, conn->conn_id, stream_id, DLV_ARGS(stream_data->in_dlv));
                }
                else {
                    if (route_delivery(stream_data, receive_complete)) {
                        qd_log(http2_adaptor->log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] Entire footer arrived, delivery routed successfully (on_frame_recv_callback)", conn->conn_id, stream_id);
                    }
                }
            }
            else {
                //
                // All headers have arrived, send out the delivery with just the headers,
                // if/when the body arrives later, we will call the qdr_delivery_continue()
                //
                qd_log(http2_adaptor->log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] All headers arrived, trying to route delivery (on_frame_recv_callback)", conn->conn_id, stream_id);
                if (route_delivery(stream_data, receive_complete)) {
                    qd_log(http2_adaptor->log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] All headers arrived, delivery routed successfully (on_frame_recv_callback)", conn->conn_id, stream_id);
                }
            }

            if (stream_data->out_dlv && !stream_data->disp_updated && !stream_data->out_dlv_decrefed && stream_data->status == QD_STREAM_FULLY_CLOSED) {
                qdr_delivery_remote_state_updated(http2_adaptor->core, stream_data->out_dlv, stream_data->out_dlv_local_disposition, true, 0, false);
                qd_log(http2_adaptor->log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] In on_frame_recv_callback NGHTTP2_HEADERS QD_STREAM_FULLY_CLOSED, qdr_delivery_remote_state_updated(stream_data->out_dlv)", conn->conn_id, stream_data->stream_id);
                stream_data->disp_updated = true;
            }
        }
    }
    break;
    default:
        break;
  }
    return 0;
}


ssize_t read_data_callback(nghttp2_session *session,
                      int32_t stream_id,
                      uint8_t *buf,
                      size_t length,
                      uint32_t *data_flags,
                      nghttp2_data_source *source,
                      void *user_data)
{
    qdr_http2_connection_t *conn = (qdr_http2_connection_t *)user_data;
    qdr_http2_stream_data_t *stream_data = (qdr_http2_stream_data_t *)source->ptr;
    qd_message_t *message = qdr_delivery_message(stream_data->out_dlv);
    qd_message_depth_status_t status = qd_message_check_depth(message, QD_DEPTH_BODY);

    // This flag tells nghttp2 that the data is not being copied into its buffer (uint8_t *buf).
    *data_flags |= NGHTTP2_DATA_FLAG_NO_COPY;


    switch (status) {
    case QD_MESSAGE_DEPTH_OK: {
        //
        // At least one complete body performative has arrived.  It is now safe to switch
        // over to the per-message extraction of body-data segments.
        //
        qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] read_data_callback QD_MESSAGE_DEPTH_OK", conn->conn_id, stream_data->stream_id);


        if (stream_data->next_stream_data) {
            stream_data->curr_stream_data = stream_data->next_stream_data;
            stream_data->curr_stream_data_result = stream_data->next_stream_data_result;
            stream_data->next_stream_data = 0;
            qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] read_data_callback Use next_stream_data", conn->conn_id, stream_data->stream_id);
        }

        if (!stream_data->curr_stream_data) {
            stream_data->curr_stream_data_result = qd_message_next_stream_data(message, &stream_data->curr_stream_data);
            qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] read_data_callback No body data, get qd_message_next_stream_data", conn->conn_id, stream_data->stream_id);
        }

        if (stream_data->next_stream_data == 0 && stream_data->next_stream_data_result == QD_MESSAGE_STREAM_DATA_NO_MORE) {
            stream_data->curr_stream_data_result = stream_data->next_stream_data_result;
        }

        switch (stream_data->curr_stream_data_result) {
        case QD_MESSAGE_STREAM_DATA_BODY_OK: {
            //
            // We have a new valid body-data segment.  Handle it
            //
            size_t pn_buffs_write_capacity = pn_raw_connection_write_buffers_capacity(conn->pn_raw_conn);
            qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] read_data_callback QD_MESSAGE_STREAM_DATA_BODY_OK pn_raw_connection_write_buffers_capacity=%zu", conn->conn_id, stream_data->stream_id, pn_buffs_write_capacity);

            if (pn_buffs_write_capacity == 0) {
                //
                // Proton capacity is zero, we will come back later to write this stream, return for now.
                //
                qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] Exiting read_data_callback, QD_MESSAGE_STREAM_DATA_BODY_OK pn_buffs_write_capacity=0, pausing stream, returning NGHTTP2_ERR_DEFERRED", conn->conn_id, stream_data->stream_id);
                stream_data->out_dlv_local_disposition = 0;
                return NGHTTP2_ERR_DEFERRED;
            }

            // total length of the payload (across all qd_buffers in the current body data)
            size_t payload_length = qd_message_stream_data_payload_length(stream_data->curr_stream_data);

            if (payload_length == 0) {
                qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] read_data_callback, payload_length=0", conn->conn_id, stream_data->stream_id);

                // The payload length is zero on this body data. Look ahead one body data to see if it is QD_MESSAGE_STREAM_DATA_NO_MORE
                stream_data->next_stream_data_result = qd_message_next_stream_data(message, &stream_data->next_stream_data);
                if (stream_data->next_stream_data_result == QD_MESSAGE_STREAM_DATA_NO_MORE) {
                    if (!stream_data->out_msg_has_footer) {
                        qd_message_stream_data_release(stream_data->curr_stream_data);
                        stream_data->curr_stream_data = 0;
                    }

                    *data_flags |= NGHTTP2_DATA_FLAG_EOF;
                    stream_data->out_msg_data_flag_eof = true;
                    stream_data->out_msg_body_sent = true;
                    stream_data->full_payload_handled = true;
                    if (stream_data->next_stream_data) {
                        qd_message_stream_data_release(stream_data->next_stream_data);
                        stream_data->next_stream_data = 0;
                    }
                    stream_data->out_dlv_local_disposition = PN_ACCEPTED;
                    qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] read_data_callback, payload_length=0 and next_stream_data=QD_MESSAGE_STREAM_DATA_NO_MORE", conn->conn_id, stream_data->stream_id);
                }
                else if (stream_data->next_stream_data_result == QD_MESSAGE_STREAM_DATA_FOOTER_OK) {
                    stream_data->full_payload_handled = true;
                    qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] read_data_callback, payload_length=0 and next_stream_data_result=QD_MESSAGE_STREAM_DATA_FOOTER_OK", conn->conn_id, stream_data->stream_id);
                }
                else {
                    qd_message_stream_data_release(stream_data->curr_stream_data);
                    stream_data->curr_stream_data = 0;
                }

                //
                // The payload length on this body data is zero. Nothing to do, just return zero to move on to the next body data. Usually, zero length body datas are a result of programmer error.
                //
                qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] Exiting read_data_callback, payload_length=0, returning 0", conn->conn_id, stream_data->stream_id);
                return 0;
            }

            stream_data->stream_data_buff_count = qd_message_stream_data_buffer_count(stream_data->curr_stream_data);
            qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] read_data_callback, stream_data->stream_data_buff_count=%i, payload_length=%zu", conn->conn_id, stream_data->stream_id, stream_data->stream_data_buff_count, payload_length);

            size_t bytes_to_send = 0;
            if (payload_length) {
                size_t remaining_payload_length = payload_length - (stream_data->curr_stream_data_qd_buff_offset * BUFFER_SIZE);


                if (remaining_payload_length <= QD_HTTP2_BUFFER_SIZE) {
                    bytes_to_send = remaining_payload_length;
                    stream_data->qd_buffers_to_send = stream_data->stream_data_buff_count;
                    stream_data->full_payload_handled = true;
                    qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] read_data_callback remaining_payload_length (%zu) <= QD_HTTP2_BUFFER_SIZE(16384), bytes_to_send=%zu, stream_data->qd_buffers_to_send=%zu", conn->conn_id, stream_data->stream_id, remaining_payload_length, bytes_to_send, stream_data->qd_buffers_to_send);

                    // Look ahead one body data
                    stream_data->next_stream_data_result = qd_message_next_stream_data(message, &stream_data->next_stream_data);
                    if (stream_data->next_stream_data_result == QD_MESSAGE_STREAM_DATA_NO_MORE) {
                        *data_flags |= NGHTTP2_DATA_FLAG_EOF;
                        stream_data->out_msg_data_flag_eof = true;
                        stream_data->out_msg_body_sent = true;
                        if (stream_data->next_stream_data) {
                            qd_message_stream_data_release(stream_data->next_stream_data);
                            stream_data->next_stream_data = 0;
                        }
                        stream_data->out_dlv_local_disposition = PN_ACCEPTED;
                        qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] read_data_callback, looking ahead one body data QD_MESSAGE_STREAM_DATA_NO_MORE", conn->conn_id, stream_data->stream_id);
                    }
                    else if (stream_data->next_stream_data_result == QD_MESSAGE_STREAM_DATA_INCOMPLETE) {
                        qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] read_data_callback, looking ahead one body data QD_MESSAGE_STREAM_DATA_INCOMPLETE", conn->conn_id, stream_data->stream_id);

                    }
                    else if (stream_data->next_stream_data_result == QD_MESSAGE_STREAM_DATA_BODY_OK) {
                        qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] read_data_callback, looking ahead one body data QD_MESSAGE_STREAM_DATA_OK", conn->conn_id, stream_data->stream_id);

                    }
                    else if (stream_data->next_stream_data_result == QD_MESSAGE_STREAM_DATA_FOOTER_OK) {
                        stream_data->out_msg_body_sent = true;
                        qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] read_data_callback, looking ahead one body data, QD_MESSAGE_STREAM_DATA_FOOTER_OK", conn->conn_id, stream_data->stream_id);
                    }
                }
                else {
                    // This means that there is more that 16k worth of payload in one body data.
                    // We want to send only 16k data per read_data_callback
                    bytes_to_send = QD_HTTP2_BUFFER_SIZE;
                    stream_data->full_payload_handled = false;
                    stream_data->qd_buffers_to_send = NUM_QD_BUFFERS_IN_ONE_HTTP2_BUFFER;
                    qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] read_data_callback remaining_payload_length <= QD_HTTP2_BUFFER_SIZE ELSE bytes_to_send=%zu, stream_data->qd_buffers_to_send=%zu", conn->conn_id, stream_data->stream_id, bytes_to_send, stream_data->qd_buffers_to_send);
                }
            }

            stream_data->bytes_out += bytes_to_send;
            qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] read_data_callback returning bytes_to_send=%zu", conn->conn_id, stream_data->stream_id, bytes_to_send);
            return bytes_to_send;
        }

        case QD_MESSAGE_STREAM_DATA_FOOTER_OK:
            qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] read_data_callback QD_MESSAGE_STREAM_DATA_FOOTER_OK", conn->conn_id, stream_data->stream_id);
            stream_data->out_msg_has_footer = true;
            stream_data->next_stream_data_result = qd_message_next_stream_data(message, &stream_data->next_stream_data);
            if (stream_data->next_stream_data) {
                qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] read_data_callback QD_MESSAGE_STREAM_DATA_FOOTER_OK, we have a next_stream_data", conn->conn_id, stream_data->stream_id);
            }
            break;

        case QD_MESSAGE_STREAM_DATA_INCOMPLETE:
            //
            // A new segment has not completely arrived yet.  Check again later.
            //
            qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] read_data_callback QD_MESSAGE_STREAM_DATA_INCOMPLETE, returning NGHTTP2_ERR_DEFERRED", conn->conn_id, stream_data->stream_id);
            stream_data->out_dlv_local_disposition = 0;
            return NGHTTP2_ERR_DEFERRED;

        case QD_MESSAGE_STREAM_DATA_NO_MORE: {
            //
            // We have already handled the last body-data segment for this delivery.
            // Complete the "sending" of this delivery and replenish credit.
            //
            size_t pn_buffs_write_capacity = pn_raw_connection_write_buffers_capacity(conn->pn_raw_conn);
            if (pn_buffs_write_capacity == 0) {
                stream_data->out_dlv_local_disposition = 0;
                qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] read_data_callback QD_MESSAGE_STREAM_DATA_NO_MORE - pn_buffs_write_capacity=0 send is not complete", conn->conn_id, stream_data->stream_id);
                return NGHTTP2_ERR_DEFERRED;
            }
            else {
                stream_data->qd_buffers_to_send = 0;
                *data_flags |= NGHTTP2_DATA_FLAG_EOF;
                stream_data->out_msg_data_flag_eof = true;
                if (stream_data->out_msg_has_footer) {
                    //
                    // We have to send the trailer fields.
                    // You cannot send trailer fields after sending frame with END_STREAM
                    // set.  To avoid this problem, one can set
                    // NGHTTP2_DATA_FLAG_NO_END_STREAM along with
                    // NGHTTP2_DATA_FLAG_EOF to signal the library not to set
                    // END_STREAM in DATA frame.
                    //
                    *data_flags |= NGHTTP2_DATA_FLAG_NO_END_STREAM;
                    qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] read_data_callback stream_data->out_msg_has_footer, setting NGHTTP2_DATA_FLAG_NO_END_STREAM", conn->conn_id, stream_data->stream_id);
                }
                stream_data->full_payload_handled = true;
                stream_data->out_msg_body_sent = true;
                stream_data->out_dlv_local_disposition = PN_ACCEPTED;
                qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] read_data_callback QD_MESSAGE_STREAM_DATA_NO_MORE - stream_data->out_dlv_local_disposition = PN_ACCEPTED - send_complete=true, setting NGHTTP2_DATA_FLAG_EOF", conn->conn_id, stream_data->stream_id);
            }

            break;
        }

        case QD_MESSAGE_STREAM_DATA_INVALID:
            //
            // The body-data is corrupt in some way.  Stop handling the delivery and reject it.
            //
            *data_flags |= NGHTTP2_DATA_FLAG_EOF;
            stream_data->out_msg_data_flag_eof = true;
            if (stream_data->curr_stream_data)
                qd_message_stream_data_release(stream_data->curr_stream_data);
            stream_data->curr_stream_data = 0;
            stream_data->out_dlv_local_disposition = PN_REJECTED;
            qd_log(http2_adaptor->protocol_log_source, QD_LOG_ERROR, "[C%"PRIu64"][S%"PRId32"] read_data_callback QD_MESSAGE_STREAM_DATA_INVALID", conn->conn_id, stream_data->stream_id);
            break;
        }
        break;
    }

    case QD_MESSAGE_DEPTH_INVALID:
        qd_log(http2_adaptor->protocol_log_source, QD_LOG_ERROR, "[C%"PRIu64"][S%"PRId32"] read_data_callback QD_MESSAGE_DEPTH_INVALID", conn->conn_id, stream_data->stream_id);
        stream_data->out_dlv_local_disposition = PN_REJECTED;
        break;

    case QD_MESSAGE_DEPTH_INCOMPLETE:
        stream_data->out_dlv_local_disposition = 0;
        qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] read_data_callback QD_MESSAGE_DEPTH_INCOMPLETE", conn->conn_id, stream_data->stream_id);
        return NGHTTP2_ERR_DEFERRED;
    }

    qd_log(http2_adaptor->protocol_log_source, QD_LOG_ERROR, "[C%"PRIu64"][S%"PRId32"] read_data_callback Returning zero", conn->conn_id, stream_data->stream_id);
    return 0;
}



qdr_http2_connection_t *qdr_http_connection_ingress(qd_http_listener_t* listener)
{
    qdr_http2_connection_t* ingress_http_conn = new_qdr_http2_connection_t();
    ZERO(ingress_http_conn);
    ingress_http_conn->ingress = true;
    ingress_http_conn->context.context = ingress_http_conn;
    ingress_http_conn->context.handler = &handle_connection_event;
    ingress_http_conn->config = &(listener->config);
    ingress_http_conn->server = listener->server;
    ingress_http_conn->pn_raw_conn = pn_raw_connection();

    ingress_http_conn->session_data = new_qdr_http2_session_data_t();
    ZERO(ingress_http_conn->session_data);
    DEQ_INIT(ingress_http_conn->session_data->buffs);
    DEQ_INIT(ingress_http_conn->session_data->streams);
    DEQ_INIT(ingress_http_conn->granted_read_buffs);
    ingress_http_conn->session_data->conn = ingress_http_conn;
    ingress_http_conn->data_prd.read_callback = read_data_callback;

    sys_mutex_lock(http2_adaptor->lock);
    DEQ_INSERT_TAIL(http2_adaptor->connections, ingress_http_conn);
    sys_mutex_unlock(http2_adaptor->lock);

    nghttp2_session_server_new(&(ingress_http_conn->session_data->session), (nghttp2_session_callbacks*)http2_adaptor->callbacks, ingress_http_conn);
    pn_raw_connection_set_context(ingress_http_conn->pn_raw_conn, ingress_http_conn);
    pn_listener_raw_accept(listener->pn_listener, ingress_http_conn->pn_raw_conn);
    return ingress_http_conn;
}

static void grant_read_buffers(qdr_http2_connection_t *conn)
{
    pn_raw_buffer_t raw_buffers[READ_BUFFERS];
    if (conn->pn_raw_conn) {
        if (!pn_raw_connection_is_read_closed(conn->pn_raw_conn)) {
            size_t desired = pn_raw_connection_read_buffers_capacity(conn->pn_raw_conn);
            while (desired) {
                size_t i;
                for (i = 0; i < desired && i < READ_BUFFERS; ++i) {
                    qd_http2_buffer_t *buf = qd_http2_buffer();
                    DEQ_INSERT_TAIL(conn->granted_read_buffs, buf);
                    raw_buffers[i].bytes = (char*) qd_http2_buffer_base(buf);
                    raw_buffers[i].capacity = qd_http2_buffer_capacity(buf);
                    raw_buffers[i].size = 0;
                    raw_buffers[i].offset = 0;
                    raw_buffers[i].context = (uintptr_t) buf;
                }
                desired -= i;
                qd_log(http2_adaptor->log_source, QD_LOG_TRACE, "[C%"PRIu64"] Calling pn_raw_connection_give_read_buffers in grant_read_buffers", conn->conn_id);
                pn_raw_connection_give_read_buffers(conn->pn_raw_conn, raw_buffers, i);
            }
        }
    }
}


static void qdr_http_detach(void *context, qdr_link_t *link, qdr_error_t *error, bool first, bool close)
{
}


static void qdr_http_flow(void *context, qdr_link_t *link, int credit)
{
    if (credit > 0) {
        qdr_http2_stream_data_t *stream_data = qdr_link_get_context(link);
        if (! stream_data)
            return;
        stream_data->in_link_credit += credit;
        if (!stream_data->in_dlv) {
            if (route_delivery(stream_data, qd_message_receive_complete(stream_data->message))) {
                qd_log(http2_adaptor->log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] qdr_http_flow, delivery routed successfully", stream_data->session_data->conn->conn_id, stream_data->stream_id);
            }
        }
    }
}


static void qdr_http_offer(void *context, qdr_link_t *link, int delivery_count)
{
}


static void qdr_http_drained(void *context, qdr_link_t *link)
{
}


static void qdr_http_drain(void *context, qdr_link_t *link, bool mode)
{
}

static int qdr_http_get_credit(void *context, qdr_link_t *link)
{
    return 10;
}


ssize_t error_read_callback(nghttp2_session *session,
                      int32_t stream_id,
                      uint8_t *buf,
                      size_t length,
                      uint32_t *data_flags,
                      nghttp2_data_source *source,
                      void *user_data)
{
    size_t len = 0;
    char *error_msg = (char *) source->ptr;
    if (error_msg) {
        len  = strlen(error_msg);
        if (len > 0)
            memcpy(buf, error_msg, len);
    }
    *data_flags |= NGHTTP2_DATA_FLAG_EOF;
    return len;
}

static void qdr_http_delivery_update(void *context, qdr_delivery_t *dlv, uint64_t disp, bool settled)
{
    qdr_http2_stream_data_t* stream_data = qdr_delivery_get_context(dlv);
    if (!stream_data)
        return;

    qdr_http2_connection_t *conn = stream_data->session_data->conn;

    //
    // DISPATCH-1849: In the case of large messages, the final DATA frame arriving from the server may or may not
    // contain the END_STREAM flag. In the cases when the final DATA frame does not contain the END_STREAM flag,
    // the router ends up forwarding all the data to the curl client without sending the END_STREAM to the client. The END_STREAM does arrive from the server
    // but not before the curl client closes the client connection after receiving all the data. The curl client
    // does not wait for the router to send an END_STREAM flag to close the connection. The client connection closure
    // triggers the link cleanup on the ingress connection, in turn freeing up all deliveries and its peer deliveries.
    // The peer delivery is released while it is still receiving the END_STREAM frame and the router crashes when we try to set receive complete
    // on the message because the message has already been freed. To solve this issue,
    // the stream_data->stream_force_closed flag is set to true when the peer delivery is released and this flag is
    // check when performing further actions on the delivery. No action on the peer delivery is performed
    // if this flag is set because the delivery and its underlying message have been freed.
    //
    if (settled && !conn->ingress && (disp == PN_RELEASED || disp == PN_MODIFIED || disp == PN_REJECTED)) {
        stream_data->stream_force_closed = true;
    }

    if (settled) {
        nghttp2_nv hdrs[2];
        if (conn->ingress && (disp == PN_RELEASED || disp == PN_MODIFIED || disp == PN_REJECTED)) {
            if (disp == PN_RELEASED || disp == PN_MODIFIED) {
                uint8_t * error_msg = (uint8_t *)"Service Unavailable";
                hdrs[0].name = (uint8_t *)":status";
                hdrs[0].value = (uint8_t *)"503";
                hdrs[0].namelen = 7;
                hdrs[0].valuelen = 3;
                hdrs[0].flags = NGHTTP2_NV_FLAG_NONE;

                hdrs[1].name = (uint8_t *)":content-type";
                hdrs[1].value = (uint8_t *)"text/plain";
                hdrs[1].namelen = 13;
                hdrs[1].valuelen = 10;
                hdrs[1].flags = NGHTTP2_NV_FLAG_NONE;

                nghttp2_data_provider data_prd;
                data_prd.read_callback = error_read_callback;
                data_prd.source.ptr = error_msg;

                nghttp2_submit_response(stream_data->session_data->session, stream_data->stream_id, hdrs, 2, &data_prd);
                nghttp2_submit_goaway(stream_data->session_data->session, 0, stream_data->stream_id, NGHTTP2_CONNECT_ERROR, error_msg, 19);
            }
            else if (disp == PN_REJECTED) {
                uint8_t * error_msg = (uint8_t *)"Resource Unavailable";
                hdrs[0].name = (uint8_t *)":status";
                hdrs[0].value = (uint8_t *)"400";
                hdrs[0].namelen = 7;
                hdrs[0].valuelen = 3;
                hdrs[0].flags = NGHTTP2_NV_FLAG_NONE;

                hdrs[1].name = (uint8_t *)":content-type";
                hdrs[1].value = (uint8_t *)"text/plain";
                hdrs[1].namelen = 13;
                hdrs[1].valuelen = 10;
                hdrs[1].flags = NGHTTP2_NV_FLAG_NONE;

                nghttp2_data_provider data_prd;
                data_prd.read_callback = error_read_callback;
                data_prd.source.ptr = error_msg;

                nghttp2_submit_response(stream_data->session_data->session, stream_data->stream_id, hdrs, 2, &data_prd);
            }
        }

        if (!conn->ingress && (disp == PN_RELEASED || disp == PN_MODIFIED || disp == PN_REJECTED)) {
            //
            // On the server side connection, send a DATA frame with an END_STREAM flag thus closing the particular stream. We
            // don't want to close the entire connection like we did not the client side.
            //
            nghttp2_submit_data(conn->session_data->session, NGHTTP2_FLAG_END_STREAM, stream_data->stream_id, &conn->data_prd);
        }

        nghttp2_session_send(stream_data->session_data->session);

        qdr_delivery_set_context(dlv, 0);
        if (stream_data->in_dlv == dlv) {
            qd_log(http2_adaptor->log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] qdr_http_delivery_update, stream_data->in_dlv == dlv", stream_data->session_data->conn->conn_id, stream_data->stream_id);
        }
        else if (stream_data->out_dlv == dlv) {
            qd_log(http2_adaptor->log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] qdr_http_delivery_update, stream_data->out_dlv == dlv", stream_data->session_data->conn->conn_id, stream_data->stream_id);
        }

        if (stream_data->status == QD_STREAM_FULLY_CLOSED) {
            qd_log(http2_adaptor->log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] qdr_http_delivery_update, stream_data->status == QD_STREAM_FULLY_CLOSED", stream_data->session_data->conn->conn_id, stream_data->stream_id);
        }
        else {
            qd_log(http2_adaptor->log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] qdr_http_delivery_update, stream_data->status != QD_STREAM_FULLY_CLOSED", stream_data->session_data->conn->conn_id, stream_data->stream_id);
        }

        bool send_complete = stream_data->out_msg_send_complete;
        if (send_complete) {
            qd_log(http2_adaptor->log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] qdr_http_delivery_update, send_complete=true", stream_data->session_data->conn->conn_id, stream_data->stream_id);
        }
        else {
            qd_log(http2_adaptor->log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] qdr_http_delivery_update, send_complete=false", stream_data->session_data->conn->conn_id, stream_data->stream_id);
        }

        qdr_delivery_decref(http2_adaptor->core, dlv, "HTTP2 adaptor  - qdr_http_delivery_update");
        set_stream_data_delivery_flags(stream_data, dlv);

        if (send_complete && stream_data->status == QD_STREAM_FULLY_CLOSED) {
            qd_log(http2_adaptor->log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] qdr_http_delivery_update, stream_data->status == QD_STREAM_FULLY_CLOSED, calling free_http2_stream_data, send_complete(dlv)=%i", stream_data->session_data->conn->conn_id, stream_data->stream_id, stream_data->out_msg_send_complete);
            free_http2_stream_data(stream_data, false);
        }
        else {
            stream_data->disp_applied = true;
        }
    }
}


static void qdr_http_conn_close(void *context, qdr_connection_t *qdr_conn, qdr_error_t *error)
{
    if (qdr_conn) {
        qdr_http2_connection_t *http_conn = qdr_connection_get_context(qdr_conn);
        assert(http_conn);
        if (http_conn) {
            //
            // When the pn_raw_connection_close() is called, the
            // PN_RAW_CONNECTION_READ and PN_RAW_CONNECTION_WRITTEN events to be emitted so
            // the application can clean up buffers given to the raw connection. After that a
            // PN_RAW_CONNECTION_DISCONNECTED event will be emitted which will in turn call handle_disconnected().
            //
            http_conn->delete_egress_connections = true;
            pn_raw_connection_close(http_conn->pn_raw_conn);
        }
    }
}


static void qdr_http_conn_trace(void *context, qdr_connection_t *conn, bool trace)
{
}


static void qdr_http_first_attach(void *context, qdr_connection_t *conn, qdr_link_t *link,
                                 qdr_terminus_t *source, qdr_terminus_t *target,
                                 qd_session_class_t session_class)
{
}


static void qdr_copy_reply_to(qdr_http2_stream_data_t* stream_data, qd_iterator_t* reply_to)
{
    int length = qd_iterator_length(reply_to);
    stream_data->reply_to = malloc(length + 1);
    qd_iterator_strncpy(reply_to, stream_data->reply_to, length + 1);
}


static void qdr_http_second_attach(void *context, qdr_link_t *link,
                                  qdr_terminus_t *source, qdr_terminus_t *target)
{
    qdr_http2_stream_data_t *stream_data =  (qdr_http2_stream_data_t*)qdr_link_get_context(link);
    if (stream_data) {
        if (qdr_link_direction(link) == QD_OUTGOING && source->dynamic) {
            if (stream_data->session_data->conn->ingress) {
                qdr_copy_reply_to(stream_data, qdr_terminus_get_address(source));
                if (route_delivery(stream_data, qd_message_receive_complete(stream_data->message))) {
                    qd_log(http2_adaptor->log_source, QD_LOG_TRACE, "[C%"PRIu64"] Reply-to available now, delivery routed successfully", stream_data->session_data->conn->conn_id);
                }
                else {
                    qd_log(http2_adaptor->log_source, QD_LOG_TRACE, "[C%"PRIu64"] Reply-to available but delivery not routed (qdr_http_second_attach)", stream_data->session_data->conn->conn_id);
                }
            }
            qdr_link_flow(http2_adaptor->core, link, DEFAULT_CAPACITY, false);
        }
    }
}

static void qdr_http_activate(void *notused, qdr_connection_t *c)
{
    sys_mutex_lock(qd_server_get_activation_lock(http2_adaptor->core->qd->server));
    qdr_http2_connection_t* conn = (qdr_http2_connection_t*) qdr_connection_get_context(c);
    if (conn) {
        if (conn->pn_raw_conn) {
            qd_log(http2_adaptor->log_source, QD_LOG_TRACE, "[C%"PRIu64"] Activation triggered, calling pn_raw_connection_wake()", conn->conn_id);
            pn_raw_connection_wake(conn->pn_raw_conn);
        }
        else if (conn->activate_timer) {
            qd_timer_schedule(conn->activate_timer, 0);
            qd_log(http2_adaptor->log_source, QD_LOG_INFO, "[C%"PRIu64"] Activation triggered, no socket yet so scheduled timer", conn->conn_id);
        } else {
            qd_log(http2_adaptor->log_source, QD_LOG_ERROR, "[C%"PRIu64"] Cannot activate", conn->conn_id);
        }
    }
    sys_mutex_unlock(qd_server_get_activation_lock(http2_adaptor->core->qd->server));
}

static int qdr_http_push(void *context, qdr_link_t *link, int limit)
{
    return qdr_link_process_deliveries(http2_adaptor->core, link, limit);
}


static void http_connector_establish(qdr_http2_connection_t *conn)
{
    qd_log(http2_adaptor->log_source, QD_LOG_INFO, "[C%"PRIu64"] Connecting to: %s", conn->conn_id, conn->config->host_port);
    conn->pn_raw_conn = pn_raw_connection();
    pn_raw_connection_set_context(conn->pn_raw_conn, conn);
    pn_proactor_raw_connect(qd_server_proactor(conn->server), conn->pn_raw_conn, conn->config->host_port);
}


uint64_t handle_outgoing_http(qdr_http2_stream_data_t *stream_data)
{
    //stream_data->processing = true;
    qdr_http2_session_data_t *session_data = stream_data->session_data;
    qdr_http2_connection_t *conn = session_data->conn;

    qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"] Starting to handle_outgoing_http", conn->conn_id);
    if (stream_data->out_dlv) {
        qd_message_t *message = qdr_delivery_message(stream_data->out_dlv);

        if (stream_data->out_msg_send_complete) {
            qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] handle_outgoing_http send is already complete, returning " DLV_FMT, conn->conn_id, stream_data->stream_id, DLV_ARGS(stream_data->out_dlv));
            return 0;
        }

        if (!stream_data->out_msg_header_sent) {
            qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"] Header not sent yet", conn->conn_id);

            qd_iterator_t *group_id_itr = qd_message_field_iterator(message, QD_FIELD_GROUP_ID);
            stream_data->remote_site = (char*) qd_iterator_copy(group_id_itr);
            qd_iterator_free(group_id_itr);

            qd_iterator_t *app_properties_iter = qd_message_field_iterator(message, QD_FIELD_APPLICATION_PROPERTIES);
            qd_parsed_field_t *app_properties_fld = qd_parse(app_properties_iter);

            uint32_t count = qd_parse_sub_count(app_properties_fld);

            nghttp2_nv hdrs[count];

            for (uint32_t idx = 0; idx < count; idx++) {
                qd_parsed_field_t *key = qd_parse_sub_key(app_properties_fld, idx);
                qd_parsed_field_t *val = qd_parse_sub_value(app_properties_fld, idx);
                qd_iterator_t *key_raw = qd_parse_raw(key);
                qd_iterator_t *val_raw = qd_parse_raw(val);

                hdrs[idx].name = (uint8_t *)qd_iterator_copy(key_raw);
                hdrs[idx].value = (uint8_t *)qd_iterator_copy(val_raw);
                hdrs[idx].namelen = qd_iterator_length(key_raw);
                hdrs[idx].valuelen = qd_iterator_length(val_raw);
                hdrs[idx].flags = NGHTTP2_NV_FLAG_NONE;

                if (strcmp(METHOD, (const char *)hdrs[idx].name) == 0) {
                    stream_data->method = qd_strdup((const char *)hdrs[idx].value);
                }
                if (strcmp(STATUS, (const char *)hdrs[idx].name) == 0) {
                    stream_data->request_status = atoi((const char *)hdrs[idx].value);
                }
            }

            int stream_id = stream_data->session_data->conn->ingress?stream_data->stream_id: -1;

            create_settings_frame(conn);

            uint8_t flags = 0;
            stream_data->curr_stream_data_result = qd_message_next_stream_data(message, &stream_data->curr_stream_data);
            if (stream_data->curr_stream_data_result == QD_MESSAGE_STREAM_DATA_BODY_OK) {
                size_t payload_length = qd_message_stream_data_payload_length(stream_data->curr_stream_data);
                if (payload_length == 0) {
                    stream_data->next_stream_data_result = qd_message_next_stream_data(message, &stream_data->next_stream_data);
                    if (stream_data->next_stream_data_result == QD_MESSAGE_STREAM_DATA_NO_MORE) {
                        if (stream_data->next_stream_data) {
                            qd_message_stream_data_release(stream_data->next_stream_data);
                            stream_data->next_stream_data = 0;
                        }

                        qd_message_stream_data_release(stream_data->curr_stream_data);
                        stream_data->curr_stream_data = 0;
                        flags = NGHTTP2_FLAG_END_STREAM;
                        stream_data->out_msg_has_body = false;
                        qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"] Message has no body, sending NGHTTP2_FLAG_END_STREAM with nghttp2_submit_headers", conn->conn_id);
                    }
                }
            }

            stream_data->stream_id = nghttp2_submit_headers(session_data->session,
                                                            flags,
                                                            stream_id,
                                                            NULL,
                                                            hdrs,
                                                            count,
                                                            stream_data);

            if (stream_id != -1) {
                stream_data->stream_id = stream_id;
            }

            qd_log(http2_adaptor->log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] handle_outgoing_http, out_dlv before sending Outgoing headers "DLV_FMT, conn->conn_id, stream_data->stream_id, DLV_ARGS(stream_data->out_dlv));

            for (uint32_t idx = 0; idx < count; idx++) {
                qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] HTTP2 HEADER Outgoing [%s=%s]", conn->conn_id, stream_data->stream_id, (char *)hdrs[idx].name, (char *)hdrs[idx].value);
            }

            nghttp2_session_send(session_data->session);
            conn->client_magic_sent = true;
            qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] Headers submitted", conn->conn_id, stream_data->stream_id);

            qd_iterator_free(app_properties_iter);
            qd_parse_free(app_properties_fld);

            for (uint32_t idx = 0; idx < count; idx++) {
                free(hdrs[idx].name);
                free(hdrs[idx].value);
            }
        }
        else {
            qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] Headers already submitted, Proceeding with the body", conn->conn_id, stream_data->stream_id);
        }

        if (stream_data->out_msg_has_body) {
            if (stream_data->out_msg_header_sent) {
                qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] Stream was paused, resuming now", conn->conn_id, stream_data->stream_id);
                nghttp2_session_resume_data(session_data->session, stream_data->stream_id);
                nghttp2_session_send(session_data->session);
                qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] nghttp2_session_send - write_buffers done for resumed stream", conn->conn_id, stream_data->stream_id);
            }
            else {
                qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] Processing message body", conn->conn_id, stream_data->stream_id);
                conn->data_prd.source.ptr = stream_data;

                // TODO - Analyze the NGHTTP2_FLAG_END_STREAM flag
                int rv = 0;
                //if (qd_message_has_footer(qdr_delivery_message(stream_data->out_dlv))) {
                //    rv = nghttp2_submit_data(session_data->session, 0, stream_data->stream_id, &conn->data_prd);
                //}
                //else {
                    rv = nghttp2_submit_data(session_data->session, NGHTTP2_FLAG_END_STREAM, stream_data->stream_id, &conn->data_prd);
                //}
                if (rv != 0) {
                    qd_log(http2_adaptor->protocol_log_source, QD_LOG_ERROR, "[C%"PRIu64"][S%"PRId32"] Error submitting data rv=%i", conn->conn_id, stream_data->stream_id, rv);
                }
                else {
                    nghttp2_session_send(session_data->session);
                    qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] nghttp2_session_send - done", conn->conn_id, stream_data->stream_id);
                }
            }
        }
        else {
            qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] Message has no body", conn->conn_id, stream_data->stream_id);
        }
        stream_data->out_msg_header_sent = true;

        if (stream_data->out_msg_has_footer) {
            qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] Message has a footer", conn->conn_id, stream_data->stream_id);
            bool send_footer = false;
            if (stream_data->out_msg_has_body) {
                qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] stream_data->out_msg_has_body", conn->conn_id, stream_data->stream_id);
                if (stream_data->out_msg_body_sent) {
                    send_footer = true;
                    qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] send_footer = true", conn->conn_id, stream_data->stream_id);
                }
                else {
                    qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] send_footer = false", conn->conn_id, stream_data->stream_id);
                }
            }
            else {
                send_footer = true;
            }

            //
            // We have a footer and are ready to send it.
            //
            if (send_footer) {
                qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] Starting to send footer", conn->conn_id, stream_data->stream_id);
                // Send the properties in the footer as a HEADERS frame.
                qd_iterator_t     *footer_properties_iter = qd_message_stream_data_iterator(stream_data->curr_stream_data);
                qd_parsed_field_t *footer_properties_fld = qd_parse(footer_properties_iter);

                uint32_t count = qd_parse_sub_count(footer_properties_fld);

                nghttp2_nv hdrs[count];

                for (uint32_t idx = 0; idx < count; idx++) {
                    qd_parsed_field_t *key = qd_parse_sub_key(footer_properties_fld, idx);
                    qd_parsed_field_t *val = qd_parse_sub_value(footer_properties_fld, idx);
                    qd_iterator_t *key_raw = qd_parse_raw(key);
                    qd_iterator_t *val_raw = qd_parse_raw(val);

                    hdrs[idx].name = (uint8_t *)qd_iterator_copy(key_raw);
                    hdrs[idx].value = (uint8_t *)qd_iterator_copy(val_raw);
                    hdrs[idx].namelen = qd_iterator_length(key_raw);
                    hdrs[idx].valuelen = qd_iterator_length(val_raw);
                    hdrs[idx].flags = NGHTTP2_NV_FLAG_NONE;
                }

                nghttp2_submit_headers(session_data->session,
                        NGHTTP2_FLAG_END_STREAM,
                        stream_data->stream_id,
                        NULL,
                        hdrs,
                        count,
                        stream_data);

                for (uint32_t idx = 0; idx < count; idx++) {
                    qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] HTTP2 HEADER(footer) Outgoing [%s=%s]", conn->conn_id, stream_data->stream_id, (char *)hdrs[idx].name, (char *)hdrs[idx].value);
                }

                nghttp2_session_send(session_data->session);
                qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] Headers(from footer) submitted", conn->conn_id, stream_data->stream_id);

                qd_iterator_free(footer_properties_iter);
                qd_parse_free(footer_properties_fld);
                if (stream_data->curr_stream_data) {
                    qd_message_stream_data_release(stream_data->curr_stream_data);
                    stream_data->curr_stream_data = 0;
                }
                if (stream_data->next_stream_data) {
                    qd_message_stream_data_release(stream_data->next_stream_data);
                    stream_data->next_stream_data = 0;
                }

                for (uint32_t idx = 0; idx < count; idx++) {
                    free(hdrs[idx].name);
                    free(hdrs[idx].value);
                }
            }
        }
        else {
            qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] Message has no footer", conn->conn_id, stream_data->stream_id);
        }

        if (stream_data->out_msg_header_sent) {
            if (stream_data->out_msg_has_body) {
                if (stream_data->out_msg_body_sent) {
                    qd_message_set_send_complete(qdr_delivery_message(stream_data->out_dlv));
                    stream_data->out_msg_send_complete = true;
                    qd_log(http2_adaptor->log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] handle_outgoing_http, out_dlv send_complete "DLV_FMT , conn->conn_id, stream_data->stream_id, DLV_ARGS(stream_data->out_dlv));

                }
            }
            else {
                qd_message_set_send_complete(qdr_delivery_message(stream_data->out_dlv));
                stream_data->out_msg_send_complete = true;
                qd_log(http2_adaptor->log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] handle_outgoing_http, out_dlv send_complete "DLV_FMT, conn->conn_id, stream_data->stream_id, DLV_ARGS(stream_data->out_dlv));
            }
        }

        if (qd_message_send_complete(qdr_delivery_message(stream_data->out_dlv))) {
            advance_stream_status(stream_data);
            if (!stream_data->disp_updated && stream_data->status == QD_STREAM_FULLY_CLOSED) {
                qdr_delivery_remote_state_updated(http2_adaptor->core, stream_data->out_dlv, stream_data->out_dlv_local_disposition, true, 0, false);
                qd_log(http2_adaptor->log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] In handle_outgoing_http, qdr_delivery_remote_state_updated(stream_data->out_dlv)", conn->conn_id, stream_data->stream_id);
                stream_data->disp_updated = true;
                qdr_delivery_decref(http2_adaptor->core, stream_data->out_dlv, "HTTP2 adaptor out_dlv - handle_outgoing_http");
                set_stream_data_delivery_flags(stream_data, stream_data->out_dlv);
            }
        }
        qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"] Finished handle_outgoing_http", conn->conn_id);
    }
    else {
        qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"] No out_dlv, no handle_outgoing_http", conn->conn_id);
    }
    return 0;
}

static uint64_t qdr_http_deliver(void *context, qdr_link_t *link, qdr_delivery_t *delivery, bool settled)
{
    qdr_http2_stream_data_t *stream_data =  (qdr_http2_stream_data_t*)qdr_link_get_context(link);

    if (!stream_data)
        return 0;

    qdr_http2_connection_t *conn = stream_data->session_data->conn;

    if (link == stream_data->session_data->conn->stream_dispatcher) {
        //
        // Let's make an outbound connection to the configured connector.
        //
        qdr_http2_connection_t *conn = stream_data->session_data->conn;

        qdr_http2_stream_data_t *stream_data = create_http2_stream_data(conn->session_data, 0);
        if (!stream_data->out_dlv) {
            stream_data->out_dlv = delivery;
            qdr_delivery_incref(delivery, "egress out_dlv referenced by HTTP2 adaptor");
        }
        qdr_terminus_t *source = qdr_terminus(0);
        qdr_terminus_set_address(source, conn->config->address);

        // Receiving link.
        stream_data->out_link = qdr_link_first_attach(conn->qdr_conn,
                                                     QD_OUTGOING,
                                                     source,            // qdr_terminus_t   *source,
                                                     qdr_terminus(0),   // qdr_terminus_t   *target,
                                                     "http.egress.out", // const char       *name,
                                                     0,                 // const char       *terminus_addr,
                                                     true,
                                                     delivery,
                                                     &(stream_data->outgoing_id));
        qdr_link_set_context(stream_data->out_link, stream_data);
        qd_iterator_t *fld_iter = qd_message_field_iterator(qdr_delivery_message(delivery), QD_FIELD_REPLY_TO);
        stream_data->reply_to = (char *)qd_iterator_copy(fld_iter);
        qd_iterator_free(fld_iter);

        // Sender link.
        qdr_terminus_t *target = qdr_terminus(0);
        qdr_terminus_set_address(target, stream_data->reply_to);
        stream_data->in_link = qdr_link_first_attach(conn->qdr_conn,
                                                     QD_INCOMING,
                                                     qdr_terminus(0),  //qdr_terminus_t   *source,
                                                     target, //qdr_terminus_t   *target,
                                                     "http.egress.in",  //const char       *name,
                                                     0,                //const char       *terminus_addr,
                                                     false,
                                                     0,
                                                     &(stream_data->incoming_id));
        qdr_link_set_context(stream_data->in_link, stream_data);
        return QD_DELIVERY_MOVED_TO_NEW_LINK;
    }
    else if (stream_data) {
        if (conn->ingress) {
            if (!stream_data->out_dlv) {
                stream_data->out_dlv = delivery;
                qdr_delivery_incref(delivery, "ingress out_dlv referenced by HTTP2 adaptor");
            }
        }
        qd_log(http2_adaptor->log_source, QD_LOG_DEBUG, "[C%"PRIu64"][S%"PRId32"] qdr_http_deliver - call handle_outgoing_http", conn->conn_id, stream_data->stream_id);
        uint64_t disp = handle_outgoing_http(stream_data);
        if (stream_data->status == QD_STREAM_FULLY_CLOSED && disp == PN_ACCEPTED) {
            qd_log(http2_adaptor->log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] qdr_http_deliver - calling free_http2_stream_data", conn->conn_id, stream_data->stream_id);
            free_http2_stream_data(stream_data, false);
        }
        return disp;
    }
    return 0;
}


static int handle_incoming_http(qdr_http2_connection_t *conn)
{
    //
    // This fix is a for nodejs server (router acting as client).
    // This is what happens -
    // 1. nodejs sends a SETTINGS frame immediately after we open the connection. (this is legal)
    // 2. Router sends -
    //     2a. Client magic
    //     2b. SETTINGS frame with ack=true (here the router is responding to the SETTINGS frame from nodejs in step 1)
    //     2c. SETTINGS frame ack=false(this is the router's inital settings frame)
    //     2d. GET request
    // 3. Nodejs responds with GOAWAY. Not sure why
    // To remedy this problem, when nodejs sends the initial SETTINGS frame, we don't tell nghttp2 about it. So step 2c happens before step 2b and nodejs is now happy
    //
    if (!conn->ingress) {
        if (!conn->client_magic_sent) {
            return 0;
        }

    }

    pn_raw_buffer_t raw_buffers[READ_BUFFERS];
    size_t n;
    int count = 0;
    int rv = 0;

    if (!conn->pn_raw_conn)
        return 0;

    bool close_conn = false;

    while ( (n = pn_raw_connection_take_read_buffers(conn->pn_raw_conn, raw_buffers, READ_BUFFERS)) ) {
        for (size_t i = 0; i < n && raw_buffers[i].bytes; ++i) {
            qd_http2_buffer_t *buf = (qd_http2_buffer_t*) raw_buffers[i].context;
            DEQ_REMOVE(conn->granted_read_buffs, buf);
            uint32_t raw_buff_size = raw_buffers[i].size;
            qd_http2_buffer_insert(buf, raw_buff_size);
            count += raw_buff_size;

            if (raw_buff_size > 0 && !close_conn) {
                qd_log(http2_adaptor->log_source, QD_LOG_DEBUG, "[C%"PRIu64"] handle_incoming_http - Calling nghttp2_session_mem_recv qd_http2_buffer of size %"PRIu32" ", conn->conn_id, raw_buff_size);
                rv = nghttp2_session_mem_recv(conn->session_data->session, qd_http2_buffer_base(buf), qd_http2_buffer_size(buf));
                if (rv < 0) {
                    qd_log(http2_adaptor->protocol_log_source, QD_LOG_ERROR, "[C%"PRIu64"] Error in nghttp2_session_mem_recv rv=%i", conn->conn_id, rv);
                    if (rv == NGHTTP2_ERR_FLOODED) {
                        // Flooding was detected in this HTTP/2 session, and it must be closed. This is most likely caused by misbehavior of peer.
                        // If the client magic is bad, we need to close the connection.
                        qd_log(http2_adaptor->protocol_log_source, QD_LOG_ERROR, "[C%"PRIu64"] HTTP NGHTTP2_ERR_FLOODED", conn->conn_id);
                        nghttp2_submit_goaway(conn->session_data->session, 0, 0, NGHTTP2_PROTOCOL_ERROR, (uint8_t *)"Protocol Error", 14);
                    }
                    else if (rv == NGHTTP2_ERR_CALLBACK_FAILURE) {
                        qd_log(http2_adaptor->protocol_log_source, QD_LOG_ERROR, "[C%"PRIu64"] HTTP NGHTTP2_ERR_CALLBACK_FAILURE", conn->conn_id);
                        nghttp2_submit_goaway(conn->session_data->session, 0, 0, NGHTTP2_PROTOCOL_ERROR, (uint8_t *)"Internal Error", 14);
                    }
                    else if (rv == NGHTTP2_ERR_BAD_CLIENT_MAGIC) {
                        qd_log(http2_adaptor->protocol_log_source, QD_LOG_ERROR, "[C%"PRIu64"] HTTP2 Protocol error, NGHTTP2_ERR_BAD_CLIENT_MAGIC, closing connection", conn->conn_id);
                        nghttp2_submit_goaway(conn->session_data->session, 0, 0, NGHTTP2_PROTOCOL_ERROR, (uint8_t *)"Bad Client Magic", 16);
                    }
                    else {
                        nghttp2_submit_goaway(conn->session_data->session, 0, 0, NGHTTP2_PROTOCOL_ERROR, (uint8_t *)"Protocol Error", 14);
                    }
                    nghttp2_session_send(conn->session_data->session);

                    //
                    // An error was received from nghttp2, the connection needs to be closed.
                    //
                    close_conn = true;
                }
            }
            free_qd_http2_buffer_t(buf);
        }
    }

    if (close_conn) {
        pn_raw_connection_close(conn->pn_raw_conn);
    }
    else {
        grant_read_buffers(conn);
    }
    nghttp2_session_send(conn-> session_data->session);

    return count;
}

qdr_http2_connection_t *qdr_http_connection_ingress_accept(qdr_http2_connection_t* ingress_http_conn)
{
    ingress_http_conn->remote_address = get_address_string(ingress_http_conn->pn_raw_conn);
    qdr_connection_info_t *info = qdr_connection_info(false, //bool             is_encrypted,
                                                      false, //bool             is_authenticated,
                                                      true,  //bool             opened,
                                                      "",   //char            *sasl_mechanisms,
                                                      QD_INCOMING, //qd_direction_t   dir,
                                                      ingress_http_conn->remote_address,    //const char      *host,
                                                      "",    //const char      *ssl_proto,
                                                      "",    //const char      *ssl_cipher,
                                                      "",    //const char      *user,
                                                      "HttpAdaptor",    //const char      *container,
                                                      0,     //pn_data_t       *connection_properties,
                                                      0,     //int              ssl_ssf,
                                                      false, //bool             ssl,
                                                      "",                  // peer router version,
                                                      false);              // streaming links

    qdr_connection_t *conn = qdr_connection_opened(http2_adaptor->core,
                                                   http2_adaptor->adaptor,
                                                   true,
                                                   QDR_ROLE_NORMAL,
                                                   1,
                                                   qd_server_allocate_connection_id(ingress_http_conn->server),
                                                   0,
                                                   0,
                                                   false,
                                                   false,
                                                   250,
                                                   0,
                                                   0,
                                                   info,
                                                   0,
                                                   0);

    ingress_http_conn->qdr_conn = conn;
    ingress_http_conn->conn_id = conn->identity;
    qdr_connection_set_context(conn, ingress_http_conn);
    ingress_http_conn->connection_established = true;
    return ingress_http_conn;
}


static void restart_streams(qdr_http2_connection_t *http_conn)
{
    qdr_http2_stream_data_t *stream_data = DEQ_HEAD(http_conn->session_data->streams);
    if (!stream_data) {
        qd_log(http2_adaptor->log_source, QD_LOG_TRACE, "[C%"PRIu64"] In restart_streams, no stream_data, returning", http_conn->conn_id);
        return;
    }

    DEQ_REMOVE_HEAD(http_conn->session_data->streams);
    DEQ_INSERT_TAIL(http_conn->session_data->streams, stream_data);
    stream_data = DEQ_HEAD(http_conn->session_data->streams);
    qd_log(http2_adaptor->log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] In restart_streams swapped head and tail streams", http_conn->conn_id, stream_data->stream_id);
    while (stream_data) {
        if (stream_data->status == QD_STREAM_FULLY_CLOSED) {
            qd_log(http2_adaptor->log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] In restart_streams QD_STREAM_FULLY_CLOSED, not restarting stream", http_conn->conn_id, stream_data->stream_id);

            if (stream_data->out_dlv && !stream_data->disp_updated && !stream_data->out_dlv_decrefed && stream_data->status == QD_STREAM_FULLY_CLOSED ) {
                // A call to qdr_delivery_remote_state_updated will free the out_dlv
                qdr_delivery_remote_state_updated(http2_adaptor->core, stream_data->out_dlv, stream_data->out_dlv_local_disposition, true, 0, false);
                qd_log(http2_adaptor->log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] In restart_streams QD_STREAM_FULLY_CLOSED, qdr_delivery_remote_state_updated(stream_data->out_dlv)", http_conn->conn_id, stream_data->stream_id);
                stream_data->disp_updated = true;
            }
            qdr_http2_stream_data_t *next_stream_data = 0;
            next_stream_data = DEQ_NEXT(stream_data);
            if(stream_data->out_msg_send_complete && stream_data->disp_applied) {
                free_http2_stream_data(stream_data, false);
            }
            stream_data = next_stream_data;
        }
        else {
            if (stream_data->out_dlv_local_disposition != PN_ACCEPTED) {
                qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"][S%"PRId32"] Restarting stream in restart_streams()", http_conn->conn_id, stream_data->stream_id);
                handle_outgoing_http(stream_data);
            }
            stream_data = DEQ_NEXT(stream_data);
        }
    }
}


static void qdr_del_http2_connection_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    //
    // DISPATCH-1996: discard is true in the case where this action is called from qdr_core_free()
    // This means that the qdr_adaptors_finalize has already been called and the connection in question has already been freed.
    // No need to do anything now, if discard, just return.
    //
    if (discard)
        return;

    qdr_http2_connection_t *conn = (qdr_http2_connection_t*) action->args.general.context_1;
    free_qdr_http2_connection(conn, false);
}


static void close_connections(qdr_http2_connection_t* conn)
{
    qdr_connection_closed(conn->qdr_conn);
    qdr_connection_set_context(conn->qdr_conn, 0);
    conn->qdr_conn = 0;
    qdr_action_t *action = qdr_action(qdr_del_http2_connection_CT, "delete_http2_connection");
    action->args.general.context_1 = conn;
    qdr_action_enqueue(http2_adaptor->core, action);
}

static void clean_session_data(qdr_http2_connection_t* conn)
{
    free_all_connection_streams(conn, false);

    //
    // This closes the nghttp2 session. Next time when a new connection is opened, a new nghttp2 session
    // will be created by calling nghttp2_session_client_new
    //
    nghttp2_session_del(conn->session_data->session);
    conn->session_data->session = 0;

    //
    // Free all the buffers on this session. This session is closed and any unsent buffers should be freed.
    //
    qd_http2_buffer_t *buf = DEQ_HEAD(conn->session_data->buffs);
    qd_http2_buffer_t *curr_buf = 0;
    while (buf) {
        curr_buf = buf;
        DEQ_REMOVE_HEAD(conn->session_data->buffs);
        buf = DEQ_HEAD(conn->session_data->buffs);
        free_qd_http2_buffer_t(curr_buf);
    }
}


static void handle_disconnected(qdr_http2_connection_t* conn)
{
    sys_mutex_lock(qd_server_get_activation_lock(http2_adaptor->core->qd->server));

    if (conn->pn_raw_conn) {
        qd_log(http2_adaptor->log_source, QD_LOG_TRACE, "[C%"PRIu64"] Setting conn->pn_raw_conn=0", conn->conn_id);
        conn->pn_raw_conn = 0;
    }

    if (conn->ingress) {
        clean_session_data(conn);
        close_connections(conn);
    }
    else {
        if (conn->stream_dispatcher) {
            qdr_http2_stream_data_t *stream_data = qdr_link_get_context(conn->stream_dispatcher);
            qd_log(http2_adaptor->log_source, QD_LOG_TRACE, "[C%"PRIu64"] Detaching stream dispatcher link on egress connection, freed associated stream data", conn->conn_id);
            qdr_link_detach(conn->stream_dispatcher, QD_CLOSED, 0);
            qdr_link_set_context(conn->stream_dispatcher, 0);
            conn->stream_dispatcher = 0;
            if (stream_data) {
                qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"] Freeing stream_data (stream_dispatcher, handle_disconnected) (%lx)", conn->conn_id,  (long) stream_data);
                free_qdr_http2_stream_data_t(stream_data);
            }
            conn->stream_dispatcher_stream_data = 0;

        }
        if (conn->delete_egress_connections) {
            // The config has already been freed by the qd_http_connector_decref() function, set it to zero here
            conn->config = 0;
            // It is important that clean_session_data be called *after* the conn->config has been set to zero
            clean_session_data(conn);
            close_connections(conn);
        }
        else {
            clean_session_data(conn);
        }
    }
    sys_mutex_unlock(qd_server_get_activation_lock(http2_adaptor->core->qd->server));
}


static void egress_conn_ping_sender(void *context)
{
    qdr_http2_connection_t* conn = (qdr_http2_connection_t*) context;
    qd_log(http2_adaptor->log_source, QD_LOG_TRACE, "[C%"PRIu64"] Running egress_conn_ping_sender", conn->conn_id);

    //
    // We don't currently sent pings on the client connection.
    // We might do that if there is a need for it.
    //
    // For now, we send ping frames only on connections the router initiates
    // to http2 servers.
    //
    //
    if (!conn->connection_established || conn->ingress)
        return;

    time_t current = time(NULL);
    bool can_send_ping_frame = true;

    //
    // If there was incoming traffic less that PING_INTERVAL ago, no need to send a ping again
    //
    if (conn->last_pn_raw_conn_read !=0 && (current - conn->last_pn_raw_conn_read < PING_INTERVAL) && (conn->last_pn_raw_conn_read != conn->prev_ping)) {
        can_send_ping_frame = false;
    }

    //
    // If there was no traffic on the wire, send the next ping only if the previous ping was sent PING_INTERVAL seconds ago.
    //
    if (can_send_ping_frame && (current - conn->prev_ping) < PING_INTERVAL) {
        can_send_ping_frame = false;
    }

    if (can_send_ping_frame) {
        send_ping_frame(conn);
        qd_log(http2_adaptor->log_source, QD_LOG_TRACE, "[C%"PRIu64"] Sent PING frame", conn->conn_id);
        conn->prev_ping = current;
        if (conn->pn_raw_conn) {
            qd_log(http2_adaptor->log_source, QD_LOG_TRACE, "[C%"PRIu64"] egress_conn_ping_sender, calling pn_raw_connection_wake()", conn->conn_id);
            pn_raw_connection_wake(conn->pn_raw_conn);
        }
    }

    if (can_send_ping_frame) {
        //
        // A ping was just sent out, schedule next ping after PING_INTERVAL seconds
        //
        qd_timer_schedule(conn->ping_timer, PING_INTERVAL * 1000);
    }
    else {
        time_t time_since_last_read = current - conn->last_pn_raw_conn_read;
        qd_timer_schedule(conn->ping_timer, (PING_INTERVAL - time_since_last_read) * 1000);
    }
}


static void egress_conn_timer_handler(void *context)
{
    qdr_http2_connection_t* conn = (qdr_http2_connection_t*) context;

    if (conn->pn_raw_conn || conn->connection_established)
        return;

    qd_log(http2_adaptor->log_source, QD_LOG_INFO, "[C%"PRIu64"] Running egress_conn_timer_handler", conn->conn_id);

    if (!conn->ingress) {
        qd_log(http2_adaptor->log_source, QD_LOG_TRACE, "[C%"PRIu64"] - Egress_conn_timer_handler - Trying to establishing outbound connection", conn->conn_id);
        http_connector_establish(conn);
    }
}


static void create_stream_dispatcher_link(qdr_http2_connection_t *egress_http_conn)
{
    if (egress_http_conn->stream_dispatcher)
        return;

    qdr_terminus_t *source = qdr_terminus(0);
    qdr_terminus_set_address(source, egress_http_conn->config->address);
    egress_http_conn->stream_dispatcher = qdr_link_first_attach(egress_http_conn->qdr_conn,
                                                           QD_OUTGOING,
                                                           source,           //qdr_terminus_t   *source,
                                                           qdr_terminus(0),  //qdr_terminus_t   *target,
                                                           "stream_dispatcher", //const char       *name,
                                                           0,                //const char       *terminus_addr,
                                                           false,
                                                           0,
                                                           &(egress_http_conn->stream_dispatcher_id));

    // Create a dummy stream_data object and set that as context.
    qdr_http2_stream_data_t *stream_data = new_qdr_http2_stream_data_t();

    qd_log(http2_adaptor->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"] Created new stream_data for stream_dispatcher (%lx)", egress_http_conn->conn_id,  (long) stream_data);

    ZERO(stream_data);
    stream_data->session_data = egress_http_conn->session_data;
    qdr_link_set_context(egress_http_conn->stream_dispatcher, stream_data);

    // This is added specifically to deal with the shutdown leak of the dispatcher stream data.
    // The core frees all links before it calls adaptor final. so we cannot get the stream data from the qdr_link context.
    egress_http_conn->stream_dispatcher_stream_data = stream_data;
}


qdr_http2_connection_t *qdr_http_connection_egress(qd_http_connector_t *connector)
{
    qdr_http2_connection_t* egress_http_conn = new_qdr_http2_connection_t();
    ZERO(egress_http_conn);
    egress_http_conn->activate_timer = qd_timer(http2_adaptor->core->qd, egress_conn_timer_handler, egress_http_conn);
    egress_http_conn->ping_timer = qd_timer(http2_adaptor->core->qd, egress_conn_ping_sender, egress_http_conn);

    egress_http_conn->ingress = false;
    egress_http_conn->context.context = egress_http_conn;
    egress_http_conn->context.handler = &handle_connection_event;
    egress_http_conn->config = &(connector->config);
    egress_http_conn->server = connector->server;
    egress_http_conn->data_prd.read_callback = read_data_callback;

    egress_http_conn->session_data = new_qdr_http2_session_data_t();
    ZERO(egress_http_conn->session_data);
    DEQ_INIT(egress_http_conn->session_data->buffs);
    DEQ_INIT(egress_http_conn->session_data->streams);
    DEQ_INIT(egress_http_conn->granted_read_buffs);
    egress_http_conn->session_data->conn = egress_http_conn;

    sys_mutex_lock(http2_adaptor->lock);
    DEQ_INSERT_TAIL(http2_adaptor->connections, egress_http_conn);
    sys_mutex_unlock(http2_adaptor->lock);

    qdr_connection_info_t *info = qdr_connection_info(false, //bool             is_encrypted,
                                                      false, //bool             is_authenticated,
                                                      true,  //bool             opened,
                                                      "",   //char            *sasl_mechanisms,
                                                      QD_OUTGOING, //qd_direction_t   dir,
                                                      egress_http_conn->config->host_port,    //const char      *host,
                                                      "",    //const char      *ssl_proto,
                                                      "",    //const char      *ssl_cipher,
                                                      "",    //const char      *user,
                                                      "httpAdaptor",    //const char      *container,
                                                      0,     //pn_data_t       *connection_properties,
                                                      0,     //int              ssl_ssf,
                                                      false, //bool             ssl,
                                                      "",                  // peer router version,
                                                      false);              // streaming links

    qdr_connection_t *conn = qdr_connection_opened(http2_adaptor->core,
                                                   http2_adaptor->adaptor,
                                                   true,
                                                   QDR_ROLE_NORMAL,
                                                   1,
                                                   qd_server_allocate_connection_id(egress_http_conn->server),
                                                   0,
                                                   0,
                                                   false,
                                                   false,
                                                   250,
                                                   0,
                                                   0,
                                                   info,
                                                   0,
                                                   0);
    egress_http_conn->qdr_conn = conn;
    connector->ctx = conn;

    egress_http_conn->conn_id = conn->identity;
    qdr_connection_set_context(conn, egress_http_conn);
    create_stream_dispatcher_link(egress_http_conn);
    return egress_http_conn;
}

static void handle_connection_event(pn_event_t *e, qd_server_t *qd_server, void *context)
{
    qdr_http2_connection_t *conn = (qdr_http2_connection_t*) context;
    qd_log_source_t *log = http2_adaptor->log_source;
    switch (pn_event_type(e)) {
    case PN_RAW_CONNECTION_CONNECTED: {
        if (conn->ingress) {
            qdr_http_connection_ingress_accept(conn);
            send_settings_frame(conn);
            qd_log(log, QD_LOG_INFO, "[C%"PRIu64"] Accepted Ingress ((PN_RAW_CONNECTION_CONNECTED)) from %s", conn->conn_id, conn->remote_address);
        } else {
            if (!conn->session_data->session) {
                nghttp2_session_client_new(&conn->session_data->session, (nghttp2_session_callbacks *)http2_adaptor->callbacks, (void *)conn);
                send_settings_frame(conn);
                conn->client_magic_sent = true;
            }
            qd_log(log, QD_LOG_INFO, "[C%"PRIu64"] Connected Egress (PN_RAW_CONNECTION_CONNECTED)", conn->conn_id);
            conn->connection_established = true;
            create_stream_dispatcher_link(conn);
            qd_log(log, QD_LOG_TRACE, "[C%"PRIu64"] Created stream_dispatcher_link in PN_RAW_CONNECTION_CONNECTED", conn->conn_id);
            qdr_connection_process(conn->qdr_conn);
        }
        break;
    }
    case PN_RAW_CONNECTION_CLOSED_READ: {
        pn_raw_connection_close(conn->pn_raw_conn);
        qd_log(log, QD_LOG_TRACE, "[C%"PRIu64"] PN_RAW_CONNECTION_CLOSED_READ", conn->conn_id);
        break;
    }
    case PN_RAW_CONNECTION_CLOSED_WRITE: {
        qd_log(log, QD_LOG_TRACE, "[C%"PRIu64"] PN_RAW_CONNECTION_CLOSED_WRITE", conn->conn_id);
        break;
    }
    case PN_RAW_CONNECTION_DISCONNECTED: {
        if (conn->ingress) {
            qd_log(log, QD_LOG_TRACE, "[C%"PRIu64"] Ingress PN_RAW_CONNECTION_DISCONNECTED", conn->conn_id);
        }
        else {
            qd_log(log, QD_LOG_TRACE, "[C%"PRIu64"] Egress PN_RAW_CONNECTION_DISCONNECTED", conn->conn_id);
            conn->client_magic_sent = false;
            if (!conn->delete_egress_connections) {
                qd_log(log, QD_LOG_TRACE, "[C%"PRIu64"] Scheduling 2 second timer to reconnect to egress connection", conn->conn_id);
                qd_timer_schedule(conn->activate_timer, 2000);
            }
        }
        conn->connection_established = false;
        handle_disconnected(conn);
        break;
    }
    case PN_RAW_CONNECTION_NEED_WRITE_BUFFERS: {
        qd_log(log, QD_LOG_TRACE, "[C%"PRIu64"] PN_RAW_CONNECTION_NEED_WRITE_BUFFERS Need write buffers", conn->conn_id);
        break;
    }
    case PN_RAW_CONNECTION_NEED_READ_BUFFERS: {
        qd_log(log, QD_LOG_TRACE, "[C%"PRIu64"] PN_RAW_CONNECTION_NEED_READ_BUFFERS Need read buffers", conn->conn_id);
        grant_read_buffers(conn);
        break;
    }
    case PN_RAW_CONNECTION_WAKE: {
        qd_log(log, QD_LOG_TRACE, "[C%"PRIu64"] PN_RAW_CONNECTION_WAKE Wake-up", conn->conn_id);
        while (qdr_connection_process(conn->qdr_conn)) {}
        break;
    }
    case PN_RAW_CONNECTION_READ: {
        int read = handle_incoming_http(conn);
        if (read > 0) {
            if (!conn->ingress) {
                //
                // Save the time that we last received communication from the other side.
                // This is done so we can send a ping frame only when is there is no traffic for PING_INTERVAL seconds.
                //
                conn->last_pn_raw_conn_read = time(NULL);
            }
        }
        qd_log(log, QD_LOG_TRACE, "[C%"PRIu64"] PN_RAW_CONNECTION_READ Read %i bytes", conn->conn_id, read);
        break;
    }
    case PN_RAW_CONNECTION_WRITTEN: {
        pn_raw_buffer_t buffs[WRITE_BUFFERS];
        size_t n;
        size_t written = 0;
        if (conn->pn_raw_conn == 0) {
            qd_log(log, QD_LOG_TRACE, "[C%"PRIu64"] PN_RAW_CONNECTION_WRITTEN, No pn_raw_conn", conn->conn_id, written);
            break;
        }
        while ( (n = pn_raw_connection_take_written_buffers(conn->pn_raw_conn, buffs, WRITE_BUFFERS)) ) {
            for (size_t i = 0; i < n; ++i) {
                written += buffs[i].size;
                qd_http2_buffer_t *qd_http2_buff = (qd_http2_buffer_t *) buffs[i].context;
                assert(qd_http2_buff);
                if (qd_http2_buff != NULL) {
                    free_qd_http2_buffer_t(qd_http2_buff);
                }
            }
        }

        //
        // Send the first PING only after the first real data has been written on the egress connection.
        //
        if (written > 0 && !conn->ingress && !conn->first_pinged) {
            // Send a PING frame 4 seconds after opening an egress connection.
            qd_timer_schedule(conn->ping_timer, PING_INTERVAL * 1000);
            conn->first_pinged = true;
        }

        qd_log(log, QD_LOG_TRACE, "[C%"PRIu64"] PN_RAW_CONNECTION_WRITTEN Wrote %i bytes, DEQ_SIZE(session_data->buffs) = %zu", conn->conn_id, written, DEQ_SIZE(conn->session_data->buffs));
        restart_streams(conn);
        break;
    }
    default:
        break;
    }
}


static void handle_listener_event(pn_event_t *e, qd_server_t *qd_server, void *context) {
    qd_log_source_t *log = http2_adaptor->log_source;

    qd_http_listener_t *li = (qd_http_listener_t*) context;
    const char *host_port = li->config.host_port;

    switch (pn_event_type(e)) {
        case PN_LISTENER_OPEN: {
            qd_log(log, QD_LOG_NOTICE, "Listening on %s", host_port);
        }
        break;

        case PN_LISTENER_ACCEPT: {
            qd_log(log, QD_LOG_INFO, "Accepting HTTP connection on %s", host_port);
            qdr_http_connection_ingress(li);
        }
        break;

        case PN_LISTENER_CLOSE:
            qd_log(log, QD_LOG_INFO, "Closing HTTP connection on %s", host_port);
            break;

        default:
            break;
    }
}

/**
 * Delete connector via Management request
 */
void qd_http2_delete_connector(qd_dispatch_t *qd, qd_http_connector_t *connector)
{
    if (connector) {
        qd_log(http2_adaptor->log_source, QD_LOG_INFO, "Deleted HttpConnector for %s, %s:%s", connector->config.address, connector->config.host, connector->config.port);

        sys_mutex_lock(http2_adaptor->lock);
        DEQ_REMOVE(http2_adaptor->connectors, connector);
        sys_mutex_unlock(http2_adaptor->lock);
        //
        // Deleting a connector must delete the corresponding qdr_connection_t and qdr_http2_connection_t objects also.
        //
        if (connector->ctx)
            qdr_core_close_connection((qdr_connection_t  *)connector->ctx);
        qd_http_connector_decref(connector);
    }
}

/**
 * Delete listener via Management request
 */
void qd_http2_delete_listener(qd_dispatch_t *qd, qd_http_listener_t *li)
{
    if (li) {
        if (li->pn_listener) {
            pn_listener_close(li->pn_listener);
            li->pn_listener = 0;
        }
        sys_mutex_lock(http2_adaptor->lock);
        DEQ_REMOVE(http2_adaptor->listeners, li);
        sys_mutex_unlock(http2_adaptor->lock);

        qd_log(http2_adaptor->log_source, QD_LOG_INFO, "Deleted HttpListener for %s, %s:%s", li->config.address, li->config.host, li->config.port);
        qd_http_listener_decref(li);
    }
}


qd_http_listener_t *qd_http2_configure_listener(qd_dispatch_t *qd, const qd_http_bridge_config_t *config, qd_entity_t *entity)
{
    qd_http_listener_t *li = qd_http_listener(qd->server, &handle_listener_event);
    if (!li) {
        qd_log(http2_adaptor->log_source, QD_LOG_ERROR, "Unable to create http listener: no memory");
        return 0;
    }

    li->config = *config;
    DEQ_INSERT_TAIL(http2_adaptor->listeners, li);
    qd_log(http2_adaptor->log_source, QD_LOG_INFO, "Configured http2_adaptor listener on %s", (&li->config)->host_port);
    pn_proactor_listen(qd_server_proactor(li->server), li->pn_listener, li->config.host_port, BACKLOG);
    return li;
}


qd_http_connector_t *qd_http2_configure_connector(qd_dispatch_t *qd, const qd_http_bridge_config_t *config, qd_entity_t *entity)
{
    qd_http_connector_t *c = qd_http_connector(qd->server);
    if (!c) {
        qd_log(http2_adaptor->log_source, QD_LOG_ERROR, "Unable to create http connector: no memory");
        return 0;
    }
    c->config = *config;
    DEQ_ITEM_INIT(c);
    DEQ_INSERT_TAIL(http2_adaptor->connectors, c);
    qdr_http_connection_egress(c);
    return c;
}

static void qdr_http2_adaptor_final(void *adaptor_context)
{
    qd_log(http2_adaptor->log_source, QD_LOG_TRACE, "Shutting down HTTP2 Protocol adaptor");
    qdr_http2_adaptor_t *adaptor = (qdr_http2_adaptor_t*) adaptor_context;
    qdr_protocol_adaptor_free(adaptor->core, adaptor->adaptor);

    // Free all remaining connections.
    qdr_http2_connection_t *http_conn = DEQ_HEAD(adaptor->connections);
    while (http_conn) {
        if (http_conn->stream_dispatcher_stream_data) {
            qd_log(http2_adaptor->log_source, QD_LOG_INFO, "[C%"PRIu64"] Freeing stream_data (stream_dispatcher, qdr_http2_adaptor_final) (%lx)", http_conn->conn_id,  (long) http_conn->stream_dispatcher_stream_data);
            free_qdr_http2_stream_data_t(http_conn->stream_dispatcher_stream_data);
            http_conn->stream_dispatcher_stream_data = 0;
        }
        qd_log(http2_adaptor->log_source, QD_LOG_INFO, "[C%"PRIu64"] Freeing http2 connection (calling free_qdr_http2_connection)", http_conn->conn_id);
        free_qdr_http2_connection(http_conn, true);
        http_conn = DEQ_HEAD(adaptor->connections);
    }

    // Free all http listeners
    qd_http_listener_t *li = DEQ_HEAD(adaptor->listeners);
    while (li) {
        qd_http2_delete_listener(0, li);
        li = DEQ_HEAD(adaptor->listeners);
    }

    // Free all http connectors
    qd_http_connector_t *ct = DEQ_HEAD(adaptor->connectors);
    while (ct) {
        qd_http2_delete_connector(0, ct);
        ct = DEQ_HEAD(adaptor->connectors);
    }

    sys_mutex_free(adaptor->lock);
    nghttp2_session_callbacks_del(adaptor->callbacks);
    http2_adaptor =  NULL;
    free(adaptor);
}

/**
 * This initialization function will be invoked when the router core is ready for the protocol
 * adaptor to be created.  This function:
 *
 *   1) Registers the protocol adaptor with the router-core.
 *   2) Prepares the protocol adaptor to be configured.
 *   3) Registers nghttp2 callbacks
 */
static void qdr_http2_adaptor_init(qdr_core_t *core, void **adaptor_context)
{
    qdr_http2_adaptor_t *adaptor = NEW(qdr_http2_adaptor_t);
    adaptor->core    = core;
    adaptor->adaptor = qdr_protocol_adaptor(core,
                                            "http2",                // name
                                            adaptor,               // context
                                            qdr_http_activate,
                                            qdr_http_first_attach,
                                            qdr_http_second_attach,
                                            qdr_http_detach,
                                            qdr_http_flow,
                                            qdr_http_offer,
                                            qdr_http_drained,
                                            qdr_http_drain,
                                            qdr_http_push,
                                            qdr_http_deliver,
                                            qdr_http_get_credit,
                                            qdr_http_delivery_update,
                                            qdr_http_conn_close,
                                            qdr_http_conn_trace);
    adaptor->log_source = qd_log_source(QD_HTTP_LOG_SOURCE);
    adaptor->protocol_log_source = qd_log_source("PROTOCOL");
    adaptor->lock = sys_mutex();
    *adaptor_context = adaptor;
    DEQ_INIT(adaptor->listeners);
    DEQ_INIT(adaptor->connectors);
    DEQ_INIT(adaptor->connections);

    //
    // Register all nghttp2 callbacks.
    //
    nghttp2_session_callbacks *callbacks;
    nghttp2_session_callbacks_new(&callbacks);
    nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, on_frame_recv_callback);
    nghttp2_session_callbacks_set_on_begin_headers_callback(callbacks, on_begin_headers_callback);
    nghttp2_session_callbacks_set_on_header_callback(callbacks, on_header_callback);
    nghttp2_session_callbacks_set_on_stream_close_callback(callbacks, on_stream_close_callback);
    nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks, on_data_chunk_recv_callback);
    nghttp2_session_callbacks_set_send_data_callback(callbacks, snd_data_callback);
    nghttp2_session_callbacks_set_send_callback(callbacks, send_callback);
    nghttp2_session_callbacks_set_on_invalid_frame_recv_callback(callbacks, on_invalid_frame_recv_callback);

    adaptor->callbacks = callbacks;
    http2_adaptor = adaptor;
}

/**
 * Declare the adaptor so that it will self-register on process startup.
 */
QDR_CORE_ADAPTOR_DECLARE("http-adaptor", qdr_http2_adaptor_init, qdr_http2_adaptor_final)

#ifndef __http2_adaptor_h__
#define __http2_adaptor_h__ 1

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
#include "adaptors/http_common.h"
#include "server_private.h"

#include "qpid/dispatch/alloc.h"
#include "qpid/dispatch/atomic.h"
#include "qpid/dispatch/compose.h"
#include "qpid/dispatch/ctools.h"
#include "qpid/dispatch/log.h"
#include "qpid/dispatch/protocol_adaptor.h"
#include "qpid/dispatch/server.h"
#include "qpid/dispatch/threading.h"

#include <nghttp2/nghttp2.h>
#include <time.h>

size_t QD_HTTP2_BUFFER_SIZE = 16384;
size_t NUM_QD_BUFFERS_IN_ONE_HTTP2_BUFFER = 32;
size_t MAX_BUFFERS = 16;
size_t HTTP2_DATA_FRAME_HEADER_LENGTH = 9;


typedef struct qdr_http2_session_data_t qdr_http2_session_data_t;
typedef struct qdr_http2_stream_data_t  qdr_http2_stream_data_t;
typedef struct qdr_http2_connection_t   qdr_http2_connection_t;
typedef struct qd_http2_buffer_t        qd_http2_buffer_t;

/**
 * Stream status
 */
typedef enum {
    QD_STREAM_OPEN,
    QD_STREAM_HALF_CLOSED,
    QD_STREAM_FULLY_CLOSED
} qd_http2_stream_status_t;


DEQ_DECLARE(qdr_http2_stream_data_t, qd_http2_stream_data_list_t);
DEQ_DECLARE(qd_http2_buffer_t,       qd_http2_buffer_list_t);
DEQ_DECLARE(qdr_http2_connection_t,  qdr_http2_connection_list_t);

struct qdr_http2_session_data_t {
    qdr_http2_connection_t       *conn;       // Connection associated with the session_data
    nghttp2_session             *session;    // A pointer to the nghttp2s' session object
    qd_http2_stream_data_list_t  streams;    // A session can have many streams.
    qd_http2_buffer_list_t       buffs;      // Buffers for writing
};

struct qdr_http2_stream_data_t {
    qdr_http2_session_data_t *session_data;
    void                     *context;
    char                     *reply_to;
    char                     *remote_site; //for stats:
    char                     *method; //for stats, also used in the subject field of AMQP request message.
    char                     *request_status; //for stats, also used in the subject field of AMQP response message.
    qdr_delivery_t           *in_dlv;
    qdr_delivery_t           *out_dlv;
    uint64_t                  incoming_id;
    uint64_t                  outgoing_id;
    uint64_t                  out_dlv_local_disposition;
    qdr_link_t               *in_link;
    qdr_link_t               *out_link;
    qd_message_t             *message;
    qd_composed_field_t      *app_properties;
    qd_composed_field_t      *footer_properties;
    qd_buffer_list_t          body_buffers;
    qd_message_stream_data_t *curr_stream_data;
    qd_message_stream_data_t *next_stream_data;
    qd_message_stream_data_t *footer_stream_data;
    DEQ_LINKS(qdr_http2_stream_data_t);

    qd_message_stream_data_result_t  curr_stream_data_result;
    qd_message_stream_data_result_t  next_stream_data_result;
    int                            curr_stream_data_qd_buff_offset;
    int                            curr_stream_data_offset; // The offset within the qd_buffer so we can jump there.
    int 						   payload_handled;
    int                            in_link_credit;   // provided by router
    int32_t                        stream_id;
    size_t                         qd_buffers_to_send;
    qd_http2_stream_status_t       status;
    bool                     entire_footer_arrived;
    bool                     entire_header_arrived;
    bool                     out_msg_header_sent;
    bool                     out_msg_body_sent;
    bool                     use_footer_properties;
    bool                     full_payload_handled; // applies to the sending side.
    bool                     out_msg_has_body;
    bool                     out_msg_data_flag_eof;
    bool                     out_msg_has_footer;
    bool                     out_msg_send_complete; // we use this flag to save the send_complete flag because the delivery and message associated with this stream might have been freed.
    bool                     disp_updated;   // Has the disposition already been set on the out_dlv
    bool                     disp_applied;   // Has the disp been applied to the out_dlv. The stream is ready to be freed now.
    bool                     header_and_props_composed;  // true if the header and properties of the inbound message have already been composed so we don't have to do it again.
    bool                     stream_force_closed;
    bool                     in_dlv_decrefed;
    bool                     out_dlv_decrefed;
    bool                     body_data_added_to_msg;
    int                      bytes_in;
    int                      bytes_out;
    qd_timestamp_t           start;
    qd_timestamp_t           stop;
};

struct qdr_http2_connection_t {
    qd_handler_context_t     context;
    qdr_connection_t        *qdr_conn;
    pn_raw_connection_t     *pn_raw_conn;
    pn_raw_buffer_t          read_buffers[4];
    qd_timer_t              *activate_timer;
    qd_timer_t              *ping_timer;  // This timer is used to send a ping frame on the egress connection every 4 seconds.
    qd_http_bridge_config_t *config;
    qd_server_t             *server;
    uint64_t                 conn_id;
    qdr_http2_session_data_t *session_data;
    char                     *remote_address;
    qdr_link_t               *stream_dispatcher;
    qdr_http2_stream_data_t  *stream_dispatcher_stream_data;
    uint64_t                  stream_dispatcher_id;
    nghttp2_data_provider     data_prd;
    qd_http2_buffer_list_t    granted_read_buffs; //buffers for reading
    time_t                    prev_ping; // Time the previous PING frame was sent on egress connection.
    time_t                    last_pn_raw_conn_read;  // The last time a PN_RAW_CONNECTION_READ event was invoked with more than zero bytes on an egress connection.

    bool                      connection_established;
    bool                      grant_initial_buffers;
    bool                      ingress;
    bool                      timer_scheduled;
    bool                      client_magic_sent;
    bool                      woken_by_ping;
    bool                      first_pinged;
    bool                      delete_egress_connections;  // If set to true, the egress qdr_connection_t and qdr_http2_connection_t objects will be deleted
    bool                      goaway_received;
    sys_atomic_t 		      raw_closed_read;
    sys_atomic_t 			  raw_closed_write;
    bool                      q2_blocked;      // send a connection level WINDOW_UPDATE frame to tell the client to stop sending data.
    sys_atomic_t              q2_restart;      // signal to resume receive
    DEQ_LINKS(qdr_http2_connection_t);
 };

struct qd_http2_buffer_t {
    unsigned int  size;     ///< Size of data content
    unsigned char content[16393];   // 16k max content + 9 bytes for the HTTP2 header, 16384 + 9 = 16393
    DEQ_LINKS(qd_http2_buffer_t);
};


static inline unsigned char *qd_http2_buffer_base(const qd_http2_buffer_t *buf)
{
    return (unsigned char*) &buf->content[0];
}

/**
 * Return a pointer to the first unused byte in the buffer.
 * @param buf A pointer to an allocated buffer
 * @return A pointer to the first free octet in the buffer, the insert point for new data.
 */
static inline unsigned char *qd_http2_buffer_cursor(const qd_http2_buffer_t *buf)
{
    return ( (unsigned char*) &(buf->content[0]) ) + buf->size;
}

/**
 * Return remaining capacity at end of buffer.
 * @param buf A pointer to an allocated buffer
 * @return The number of octets in the buffer's free space, how many octets may be inserted.
 */
static inline size_t qd_http2_buffer_capacity(const qd_http2_buffer_t *buf)
{
    return QD_HTTP2_BUFFER_SIZE - buf->size;
}

/**
 * Return the size of the buffers data content.
 * @param buf A pointer to an allocated buffer
 * @return The number of octets of data in the buffer
 */
static inline size_t qd_http2_buffer_size(const qd_http2_buffer_t *buf)
{
    return buf->size;
}

/**
 * Notify the buffer that octets have been inserted at the buffer's cursor.  This will advance the
 * cursor by len octets.
 *
 * @param buf A pointer to an allocated buffer
 * @param len The number of octets that have been appended to the buffer
 */
static inline void qd_http2_buffer_insert(qd_http2_buffer_t *buf, size_t len)
{
    buf->size += len;
    assert(buf->size <= QD_HTTP2_BUFFER_SIZE + HTTP2_DATA_FRAME_HEADER_LENGTH);
}

ALLOC_DECLARE(qdr_http2_session_data_t);
ALLOC_DECLARE(qdr_http2_stream_data_t);
ALLOC_DECLARE(qdr_http2_connection_t);
ALLOC_DECLARE(qd_http2_buffer_t);


#endif // __http2_adaptor_h__

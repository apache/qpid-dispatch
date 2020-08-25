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
#include <qpid/dispatch/server.h>
#include <qpid/dispatch/threading.h>
#include <qpid/dispatch/compose.h>
#include <qpid/dispatch/atomic.h>
#include <qpid/dispatch/alloc.h>
#include <qpid/dispatch/ctools.h>
#include <qpid/dispatch/log.h>
#include <nghttp2/nghttp2.h>


// We already have a qd_http_listener_t defined in http-libwebsockets.c
// We will call this as qd_http_lsnr_t in order to avoid a clash.
// At a later point in time, we will handle websocket here as well
// and get rid of http-libwebsockets.c and rename this as qd_http_listener_t
typedef struct qdr_http2_session_data_t qdr_http2_session_data_t;
typedef struct qdr_http2_stream_data_t  qdr_http2_stream_data_t;
typedef struct qdr_http_connection_t    qdr_http_connection_t;
DEQ_DECLARE(qdr_http2_stream_data_t, qd_http2_stream_data_list_t);

struct qdr_http2_session_data_t {
    qdr_http_connection_t       *conn;       // Connection associated with the session_data
    nghttp2_session             *session;    // A pointer to the nghttp2s' session object
    qd_http2_stream_data_list_t  streams;    // A session can have many streams.
    qd_buffer_list_t             buffs;      // Buffers for writing
};

struct qdr_http2_stream_data_t {
    qdr_http2_session_data_t *session_data;
    char                     *reply_to;
    qdr_delivery_t           *in_dlv;
    qdr_delivery_t           *out_dlv;
    uint64_t                  incoming_id;
    uint64_t                  outgoing_id;
    uint64_t                  disposition;
    qdr_link_t               *in_link;
    qdr_link_t               *out_link;
    qd_message_t             *message;
    qd_composed_field_t      *app_properties;
    qd_composed_field_t      *body;
    qd_message_body_data_t   *curr_body_data;
    DEQ_LINKS(qdr_http2_stream_data_t);

    qd_message_body_data_result_t  curr_body_data_result;
    int                            curr_body_data_buff_offset;
    int                            body_data_buff_count;
    int32_t                        stream_id;

    bool                     entire_header_arrived; // true if all the headershave arrived, just before the start of the data frame or just before the END_STREAM.
    bool                     header_sent;
};

struct qdr_http_connection_t {
    qd_handler_context_t     context;
    qdr_connection_t        *qdr_conn;
    pn_raw_connection_t     *pn_raw_conn;
    pn_raw_buffer_t          read_buffers[4];
    qd_timer_t              *activate_timer;
    qd_http_bridge_config_t *config;
    qd_server_t             *server;
    uint64_t                 conn_id;

    //TODO - Code review - Change this to a struct.
    qdr_http2_session_data_t *session_data;
    char                    *remote_address;
    qdr_link_t              *stream_dispatcher;
    uint64_t                 stream_dispatcher_id;
    qdr_http2_stream_data_t *initial_stream;
    char                     *reply_to;
    nghttp2_data_provider    data_prd;
    bool                     connection_established;
    bool                     grant_initial_buffers;
    bool                     ingress;
 };

ALLOC_DECLARE(qdr_http2_session_data_t);
ALLOC_DECLARE(qdr_http2_stream_data_t);



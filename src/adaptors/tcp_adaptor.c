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

#include "tcp_adaptor.h"

#include "delivery.h"

#include "qpid/dispatch/alloc_pool.h"
#include "qpid/dispatch/ctools.h"
#include "qpid/dispatch/protocol_adaptor.h"

#include <proton/codec.h>
#include <proton/condition.h>
#include <proton/listener.h>
#include <proton/netaddr.h>
#include <proton/proactor.h>
#include <proton/raw_connection.h>

#include <inttypes.h>
#include <stdio.h>

// maximum amount of bytes to read from TCP client before backpressure
// activates.  Note that the actual number of read bytes may exceed this value
// by READ_BUFFERS * BUFFER_SIZE since we fetch up to READ_BUFFERs worth of
// buffers when calling pn_raw_connection_take_read_buffers()
//
// Assumptions: 1 Gbps, 1msec latency across a router, 3 hop router path
//   Effective GigEthernet throughput is 116MB/sec, or ~121635 bytes/msec.
//   For 3 hops routers with ~1msec latency gives a 6msec round trip
//   time. Ideally the window would be 1/2 full at most before the ACK
//   arrives:
//
const uint32_t TCP_MAX_CAPACITY = 121635 * 6 * 2;
const size_t TCP_BUFFER_SIZE = 16384*2;

ALLOC_DEFINE(qd_tcp_listener_t);
ALLOC_DEFINE(qd_tcp_connector_t);
ALLOC_DEFINE(qd_tcp_bridge_t);

#define WRITE_BUFFERS 12

#define LOCK   sys_mutex_lock
#define UNLOCK sys_mutex_unlock

typedef struct qdr_tcp_connection_t qdr_tcp_connection_t;

struct qdr_tcp_connection_t {
    qd_handler_context_t  context;
    char                 *reply_to;
    qdr_connection_t     *qdr_conn;
    uint64_t              conn_id;
    qdr_link_t           *incoming;
    uint64_t              incoming_id;
    qdr_link_t           *outgoing;
    uint64_t              outgoing_id;
    pn_raw_connection_t  *pn_raw_conn;
    sys_mutex_t          *activation_lock;
    qdr_delivery_t       *instream;
    qdr_delivery_t       *outstream;
    bool                  ingress;
    bool                  flow_enabled;
    bool                  incoming_started;
    bool                  egress_dispatcher;
    bool                  connector_closed;//only used if egress_dispatcher=true
    bool                  in_list;         // This connection is in the adaptor's connections list
    sys_atomic_t          raw_closed_read;   // proton event seen
    sys_atomic_t          raw_closed_write;  // proton event seen or write_close called
    bool                  raw_read_shutdown; // stream closed
    bool                  read_eos_seen;
    bool                  window_disabled;   // true: ignore unacked byte window
    qdr_delivery_t       *initial_delivery;
    qd_timer_t           *activate_timer;
    qd_tcp_bridge_t      *bridge;         // config and stats
    qd_server_t          *server;
    char                 *remote_address;
    char                 *global_id;
    uint64_t              bytes_in;       // read from raw conn
    uint64_t              bytes_out;      // written to raw conn
    uint64_t              bytes_unacked;  // not yet acked by outgoing tcp adaptor
    uint64_t              opened_time;
    uint64_t              last_in_time;
    uint64_t              last_out_time;

    qd_message_stream_data_t *previous_stream_data; // previous segment (received in full)
    qd_message_stream_data_t *outgoing_stream_data; // current segment
    size_t                  outgoing_body_bytes;  // bytes received from current segment
    int                     outgoing_body_offset; // buffer offset into current segment

    pn_raw_buffer_t         read_buffer;
    bool                    read_pending;
    pn_raw_buffer_t         write_buffer;
    bool                    write_pending;

    pn_raw_buffer_t         outgoing_buffs[WRITE_BUFFERS];
    int                     outgoing_buff_count;  // number of buffers with data
    int                     outgoing_buff_idx;    // first buffer with data

    sys_atomic_t            q2_restart;      // signal to resume receive
    bool                    q2_blocked;      // stop reading from raw conn

    DEQ_LINKS(qdr_tcp_connection_t);
};

DEQ_DECLARE(qdr_tcp_connection_t, qdr_tcp_connection_list_t);
ALLOC_DECLARE(qdr_tcp_connection_t);
ALLOC_DEFINE(qdr_tcp_connection_t);

typedef struct qdr_tcp_adaptor_t {
    qdr_core_t               *core;
    qdr_protocol_adaptor_t   *adaptor;
    qd_tcp_listener_list_t    listeners;
    qd_tcp_connector_list_t   connectors;
    qdr_tcp_connection_list_t connections;
    qd_bridge_config_list_t   bridges;
    qd_log_source_t          *log_source;
} qdr_tcp_adaptor_t;

static qdr_tcp_adaptor_t *tcp_adaptor;

static void qdr_add_tcp_connection_CT(qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_del_tcp_connection_CT(qdr_core_t *core, qdr_action_t *action, bool discard);

static void handle_disconnected(qdr_tcp_connection_t* conn);
static void free_qdr_tcp_connection(qdr_tcp_connection_t* conn);
static void free_bridge_config(qd_tcp_bridge_t *config);
static void qdr_tcp_open_server_side_connection(qdr_tcp_connection_t* tc);


// is the incoming byte window full
//
inline static bool read_window_full(const qdr_tcp_connection_t* conn)
{
    return !conn->window_disabled && conn->bytes_unacked >= TCP_MAX_CAPACITY;
}


static void allocate_tcp_buffer(pn_raw_buffer_t *buffer)
{
    buffer->bytes = malloc(TCP_BUFFER_SIZE);
    ZERO(buffer->bytes);
    buffer->capacity = TCP_BUFFER_SIZE;
    buffer->size = 0;
    buffer->offset = 0;
}

static void allocate_tcp_write_buffer(pn_raw_buffer_t *buffer)
{
    buffer->bytes = malloc(TCP_BUFFER_SIZE);
    ZERO(buffer->bytes);
    buffer->capacity = TCP_BUFFER_SIZE;
    buffer->size = 0;
    buffer->offset = 0;
}

static inline uint64_t qdr_tcp_conn_linkid(const qdr_tcp_connection_t *conn)
{
    assert(conn);
    return conn->instream ? conn->incoming_id : conn->outgoing_id;
}

static inline const char * qdr_tcp_connection_role_name(const qdr_tcp_connection_t *tc)
{
    assert(tc);
    return tc->ingress ? "listener" : "connector";
}

static const char * qdr_tcp_quadrant_id(const qdr_tcp_connection_t *tc, const qdr_link_t *link)
{
    if (tc->ingress)
        return link->link_direction == QD_INCOMING ? "(listener incoming)" : "(listener outgoing)";
    else
        return link->link_direction == QD_INCOMING ? "(connector incoming)" : "(connector outgoing)";
}

static void on_activate(void *context)
{
    qdr_tcp_connection_t* conn = (qdr_tcp_connection_t*) context;

    qd_log(tcp_adaptor->log_source, QD_LOG_DEBUG, "[C%"PRIu64"] on_activate", conn->conn_id);
    while (qdr_connection_process(conn->qdr_conn)) {}
    if (conn->egress_dispatcher && conn->connector_closed) {
        qdr_connection_set_context(conn->qdr_conn, 0);
        qdr_connection_closed(conn->qdr_conn);
        conn->qdr_conn = 0;
        free_qdr_tcp_connection(conn);
    }
}

static void grant_read_buffers(qdr_tcp_connection_t *conn)
{
    if (IS_ATOMIC_FLAG_SET(&conn->raw_closed_read) || conn->read_pending)
        return;

    conn->read_pending = true;
    qd_log(tcp_adaptor->log_source, QD_LOG_DEBUG,
        "[C%"PRIu64"][L%"PRIu64"] Calling pn_raw_connection_give_read_buffers() capacity=%i offset=%i",
           conn->conn_id, conn->incoming_id, conn->read_buffer.capacity, conn->read_buffer.offset);
    pn_raw_connection_give_read_buffers(conn->pn_raw_conn, &conn->read_buffer, 1);
}


// Per-message callback to resume receiving after Q2 is unblocked on the
// incoming link.
// This routine must be thread safe: the thread on which it is running
// is not an IO thread that owns the underlying pn_raw_conn.
//
void qdr_tcp_q2_unblocked_handler(const qd_alloc_safe_ptr_t context)
{
    qdr_tcp_connection_t *tc = (qdr_tcp_connection_t*)qd_alloc_deref_safe_ptr(&context);
    if (tc == 0) {
        // bad news.
        assert(false);
        return;
    }

    // prevent the tc from being deleted while running:
    LOCK(tc->activation_lock);

    if (tc->pn_raw_conn) {
        sys_atomic_set(&tc->q2_restart, 1);
        qd_log(tcp_adaptor->log_source, QD_LOG_DEBUG,
               "[C%"PRIu64"] q2 unblocked: call pn_raw_connection_wake()",
               tc->conn_id);
        pn_raw_connection_wake(tc->pn_raw_conn);
    }

    UNLOCK(tc->activation_lock);
}

// Extract buffers and their bytes from raw connection.
// * Add received byte count to connection stats
// * Return the count of bytes in the buffers list
static int handle_incoming_raw_read(qdr_tcp_connection_t *conn, qd_buffer_list_t *buffers)
{
    pn_raw_buffer_t raw_buffer;
    if (read_window_full(conn) || !pn_raw_connection_take_read_buffers(conn->pn_raw_conn, &raw_buffer, 1)) {
        return 0;
    }
    int result = raw_buffer.size;
    qd_log(tcp_adaptor->log_source, QD_LOG_DEBUG,
        "[C%"PRIu64"] pn_raw_connection_take_read_buffers() took buffer with %zu bytes",
        conn->conn_id, result);

    if (buffers) {
        qd_buffer_list_append(buffers, (uint8_t*) (raw_buffer.bytes + raw_buffer.offset), raw_buffer.size);
    }
    //reset buffer for further reads
    conn->read_buffer.size = 0;
    conn->read_buffer.offset = 0;
    conn->read_pending = false;
    if (result > 0) {
        // account for any incoming bytes just read

        conn->last_in_time = tcp_adaptor->core->uptime_ticks;
        conn->bytes_in      += result;
        LOCK(conn->bridge->stats_lock);
        conn->bridge->bytes_in += result;
        UNLOCK(conn->bridge->stats_lock);
        conn->bytes_unacked += result;
        if (read_window_full(conn)) {
            qd_log(tcp_adaptor->log_source, QD_LOG_TRACE,
                   "[C%"PRIu64"] TCP RX window CLOSED: bytes in=%"PRIu64" unacked=%"PRIu64,
                   conn->conn_id, conn->bytes_in, conn->bytes_unacked);
        }
    }
    return result;
}


// Fetch incoming raw incoming buffers from proton and pass them to a delivery.
// Create a new delivery if necessary.
// Return number of bytes read from raw connection
static int handle_incoming(qdr_tcp_connection_t *conn, const char *msg)
{
    qd_log_source_t *log = tcp_adaptor->log_source;

    qd_log(log, QD_LOG_TRACE,
           "[C%"PRIu64"][L%"PRIu64"] handle_incoming %s for %s connection. read_closed:%s, flow_enabled:%s",
           conn->conn_id, conn->incoming_id, msg,
           qdr_tcp_connection_role_name(conn),
           conn->raw_closed_read ? "T" : "F",
           conn->flow_enabled    ? "T" : "F");

    if (conn->raw_read_shutdown) {
        // Drain all read buffers that may still be in the raw connection
        qd_log(log, QD_LOG_TRACE,
            "[C%"PRIu64"][L%"PRIu64"] handle_incoming %s for %s connection. drain read buffers",
            conn->conn_id, conn->incoming_id, msg,
            qdr_tcp_connection_role_name(conn));
        handle_incoming_raw_read(conn, 0);
        return 0;
    }

    // Don't initiate an ingress stream message
    // if we don't yet have a reply-to address and credit.
    if (conn->ingress && !conn->reply_to) {
        qd_log(log, QD_LOG_DEBUG,
                "[C%"PRIu64"][L%"PRIu64"] Waiting for reply-to address before initiating %s ingress stream message",
                conn->conn_id, conn->incoming_id, qdr_tcp_connection_role_name(conn));
        return 0;
    }
    if (!conn->flow_enabled) {
        qd_log(log, QD_LOG_DEBUG,
                "[C%"PRIu64"][L%"PRIu64"] Waiting for credit before initiating %s ingress stream message",
                conn->conn_id, conn->incoming_id, qdr_tcp_connection_role_name(conn));
        return 0;
    }

    // Ensure existence of ingress stream message
    if (!conn->instream) {
        qd_message_t *msg = qd_message();

        qd_message_set_stream_annotation(msg, true);

        qd_composed_field_t *props = qd_compose(QD_PERFORMATIVE_PROPERTIES, 0);
        qd_compose_start_list(props);
        qd_compose_insert_null(props);                      // message-id
        qd_compose_insert_null(props);                      // user-id
        if (conn->ingress) {
            qd_compose_insert_string(props, conn->bridge->address); // to
            qd_compose_insert_string(props, conn->global_id);      // subject
            qd_compose_insert_string(props, conn->reply_to);       // reply-to
            qd_log(log, QD_LOG_DEBUG,
                   "[C%"PRIu64"][L%"PRIu64"] Initiating listener (ingress) stream incoming link for %s connection to: %s reply: %s",
                   conn->conn_id, conn->incoming_id, qdr_tcp_connection_role_name(conn),
                   conn->bridge->address, conn->reply_to);
        } else {
            qd_compose_insert_string(props, conn->reply_to);  // to
            qd_compose_insert_string(props, conn->global_id); // subject
            qd_compose_insert_null(props);                    // reply-to
            qd_log(log, QD_LOG_DEBUG,
                   "[C%"PRIu64"][L%"PRIu64"] Initiating connector (egress) stream incoming link for connection to: %s",
                   conn->conn_id, conn->incoming_id, conn->reply_to);
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

        qd_message_compose_2(msg, props, false);
        qd_compose_free(props);

        // set up message q2 unblocked callback handler
        qd_alloc_safe_ptr_t conn_sp = QD_SAFE_PTR_INIT(conn);
        qd_message_set_q2_unblocked_handler(msg, qdr_tcp_q2_unblocked_handler, conn_sp);

        conn->instream = qdr_link_deliver(conn->incoming, msg, 0, false, 0, 0, 0, 0);

        qd_log(log, QD_LOG_DEBUG,
               "[C%"PRIu64"][L%"PRIu64"][D%"PRIu32"] Initiating empty %s incoming stream message",
               conn->conn_id, conn->incoming_id, conn->instream->delivery_id,
               qdr_tcp_connection_role_name(conn));

        conn->incoming_started = true;
    }
    qdr_delivery_t *conn_instream = conn->instream;

    // Don't read from proton if in Q2 holdoff
    if (conn->q2_blocked) {
        qd_log(log, QD_LOG_DEBUG,
               DLV_FMT" handle_incoming q2_blocked for %s connection",
               DLV_ARGS(conn_instream),  qdr_tcp_connection_role_name(conn));
        return 0;
    }

    // Read all buffers available from proton.
    // Collect buffers for ingress; free empty buffers.
    qd_buffer_list_t buffers;
    DEQ_INIT(buffers);
    int count = handle_incoming_raw_read(conn, &buffers);

    // Grant more buffers to proton for reading if read side is still open
    grant_read_buffers(conn);

    // Push the bytes just read into the streaming message
    if (count > 0) {
        qd_message_stream_data_append(qdr_delivery_message(conn->instream), &buffers, &conn->q2_blocked);
        if (conn->q2_blocked) {
            // note: unit tests grep for this log!
            qd_log(log, QD_LOG_DEBUG,
                    DLV_FMT" %s client link blocked on Q2 limit",
                    DLV_ARGS(conn_instream), qdr_tcp_connection_role_name(conn));

        }
        qdr_delivery_continue(tcp_adaptor->core, conn->instream, false);
        qd_log(log, QD_LOG_TRACE,
                DLV_FMT" Continuing %s message with %i bytes",
                DLV_ARGS(conn_instream), qdr_tcp_connection_role_name(conn), count);
    } else {
        assert (DEQ_SIZE(buffers) == 0);
    }

    // Close the stream message if read side has closed
    if (IS_ATOMIC_FLAG_SET(&conn->raw_closed_read)) {
        qd_log(log, QD_LOG_DEBUG,
            DLV_FMT" close %s instream delivery",
            DLV_ARGS(conn_instream), qdr_tcp_connection_role_name(conn));
        qd_message_set_receive_complete(qdr_delivery_message(conn->instream));
        qdr_delivery_continue(tcp_adaptor->core, conn->instream, true);
        conn->raw_read_shutdown = true;
    }

    return count;
}


static void flush_outgoing_buffs(qdr_tcp_connection_t *conn)
{
    // Free any remaining stream data objects
    if (conn->outgoing_stream_data) {
        qd_message_stream_data_release_up_to(conn->outgoing_stream_data);
        conn->outgoing_stream_data = 0;
    } else if (conn->previous_stream_data) {
        qd_message_stream_data_release_up_to(conn->previous_stream_data);
        conn->previous_stream_data = 0;
    }
}


static void free_qdr_tcp_connection(qdr_tcp_connection_t* tc)
{
    free(tc->reply_to);
    free(tc->remote_address);
    free(tc->global_id);
    sys_atomic_destroy(&tc->q2_restart);
    sys_atomic_destroy(&tc->raw_closed_read);
    sys_atomic_destroy(&tc->raw_closed_write);
    if (tc->activate_timer) {
        qd_timer_free(tc->activate_timer);
    }
    sys_mutex_free(tc->activation_lock);
    free(tc->write_buffer.bytes);
    free(tc->read_buffer.bytes);
    //proactor will free the socket
    LOCK(tc->bridge->stats_lock);
    tc->bridge->connections_closed += 1;
    UNLOCK(tc->bridge->stats_lock);
    free_bridge_config(tc->bridge);
    free_qdr_tcp_connection_t(tc);
}

static void handle_disconnected(qdr_tcp_connection_t* conn)
{
    // release all message buffers since the deliveries will free the message
    // once we decref them.
    flush_outgoing_buffs(conn);

    if (conn->instream) {
        qd_log(tcp_adaptor->log_source, QD_LOG_DEBUG,
               "[C%"PRIu64"][L%"PRIu64"] handle_disconnected - close instream",
               conn->conn_id, conn->incoming_id);
        qd_message_set_receive_complete(qdr_delivery_message(conn->instream));
        qdr_delivery_continue(tcp_adaptor->core, conn->instream, true);
        qdr_delivery_decref(tcp_adaptor->core, conn->instream, "tcp-adaptor.handle_disconnected - instream");
    }
    if (conn->outstream) {
        qd_log(tcp_adaptor->log_source, QD_LOG_DEBUG,
               "[C%"PRIu64"][L%"PRIu64"] handle_disconnected - close outstream",
               conn->conn_id, conn->outgoing_id);
        qdr_delivery_decref(tcp_adaptor->core, conn->outstream, "tcp-adaptor.handle_disconnected - outstream");
    }
    if (conn->incoming) {
        qd_log(tcp_adaptor->log_source, QD_LOG_DEBUG,
               "[C%"PRIu64"][L%"PRIu64"] handle_disconnected - detach incoming",
               conn->conn_id, conn->incoming_id);
        qdr_link_detach(conn->incoming, QD_LOST, 0);
    }
    if (conn->outgoing) {
        qd_log(tcp_adaptor->log_source, QD_LOG_DEBUG,
               "[C%"PRIu64"][L%"PRIu64"] handle_disconnected - detach outgoing",
               conn->conn_id, conn->outgoing_id);
        qdr_link_detach(conn->outgoing, QD_LOST, 0);
    }
    if (conn->initial_delivery) {
        qdr_delivery_remote_state_updated(tcp_adaptor->core, conn->initial_delivery, PN_RELEASED, true, 0, false);
        qdr_delivery_decref(tcp_adaptor->core, conn->initial_delivery, "tcp-adaptor.handle_disconnected - initial_delivery");
        conn->initial_delivery = 0;
    }
    if (conn->qdr_conn) {
        qdr_connection_set_context(conn->qdr_conn, 0);
        qdr_connection_closed(conn->qdr_conn);
        conn->qdr_conn = 0;
    }
    free(conn->write_buffer.bytes);
    conn->write_buffer.bytes = 0;
    free(conn->read_buffer.bytes);
    conn->read_buffer.bytes = 0;

    //need to free on core thread to avoid deleting while in use by management agent
    qdr_action_t *action = qdr_action(qdr_del_tcp_connection_CT, "delete_tcp_connection");
    action->args.general.context_1 = conn;
    qdr_action_enqueue(tcp_adaptor->core, action);
}

static int read_message_body(qdr_tcp_connection_t *conn, qd_message_t *msg, pn_raw_buffer_t *buffers, int count)
{
    int used = 0;

    // Advance to next stream_data vbin segment if necessary.
    // Return early if no data to process or error
    if (conn->outgoing_stream_data == 0) {
        qd_message_stream_data_result_t stream_data_result = qd_message_next_stream_data(msg, &conn->outgoing_stream_data);
        if (stream_data_result == QD_MESSAGE_STREAM_DATA_BODY_OK) {
            // a new stream_data segment has been found
            conn->outgoing_body_bytes  = 0;
            conn->outgoing_body_offset = 0;
            // continue to process this segment
        } else if (stream_data_result == QD_MESSAGE_STREAM_DATA_INCOMPLETE) {
            return 0;
        } else {
            switch (stream_data_result) {
            case QD_MESSAGE_STREAM_DATA_NO_MORE:
                qd_log(tcp_adaptor->log_source, QD_LOG_INFO,
                       "[C%"PRIu64"] EOS", conn->conn_id);
                conn->read_eos_seen = true;
                break;
            case QD_MESSAGE_STREAM_DATA_INVALID:
                qd_log(tcp_adaptor->log_source, QD_LOG_ERROR,
                       "[C%"PRIu64"] Invalid body data for streaming message", conn->conn_id);
                break;
            default:
                break;
            }
            qd_message_set_send_complete(msg);
            return -1;
        }
    }

    // A valid stream_data is in place.
    // Try to get a buffer set from it.
    used = qd_message_stream_data_buffers(conn->outgoing_stream_data, buffers, conn->outgoing_body_offset, count);
    if (used > 0) {
        // Accumulate the lengths of the returned buffers.
        for (int i=0; i<used; i++) {
            conn->outgoing_body_bytes += buffers[i].size;
        }

        // Buffers returned should never exceed the stream_data payload length
        assert(conn->outgoing_body_bytes <= conn->outgoing_stream_data->payload.length);

        if (conn->outgoing_body_bytes == conn->outgoing_stream_data->payload.length) {
            // Erase the stream_data struct from the connection so that
            // a new one gets created on the next pass.
            conn->previous_stream_data = conn->outgoing_stream_data;
            conn->outgoing_stream_data = 0;
        } else {
            // Returned buffer set did not consume the entire stream_data segment.
            // Leave existing stream_data struct in place for use on next pass.
            // Add the number of returned buffers to the offset for the next pass.
            conn->outgoing_body_offset += used;
        }
    } else {
        // No buffers returned.
        // This sender has caught up with all data available on the input stream.
    }
    return used;
}


static bool copy_outgoing_buffs(qdr_tcp_connection_t *conn)
{
    // Send the outgoing buffs to pn_raw_conn.
    // Return true if all the buffers went out.
    bool result;

    if (conn->outgoing_buff_count == 0) {
        qd_log(tcp_adaptor->log_source, QD_LOG_DEBUG, "[C%"PRIu64"] No outgoing buffers to copy at present", conn->conn_id);
        result = true;
    } else if (conn->write_pending) {
        qd_log(tcp_adaptor->log_source, QD_LOG_DEBUG, "[C%"PRIu64"] Can't write, previous write still pending", conn->conn_id);
        result = false;
    } else {
        //copy small buffers into large one
        size_t used = conn->outgoing_buff_idx;
        while (used < conn->outgoing_buff_count && ((conn->write_buffer.size + conn->outgoing_buffs[used].size) <= conn->write_buffer.capacity)) {
            memcpy(conn->write_buffer.bytes + conn->write_buffer.size, conn->outgoing_buffs[used].bytes, conn->outgoing_buffs[used].size);
            conn->write_buffer.size += conn->outgoing_buffs[used].size;
            qd_log(tcp_adaptor->log_source, QD_LOG_DEBUG,
                   "[C%"PRIu64"] Copying buffer %i of %i with %i bytes (total=%i)", conn->conn_id, used+1, conn->outgoing_buff_count - conn->outgoing_buff_idx, conn->outgoing_buffs[used].size, conn->write_buffer.size);
            used++;
        }

        result = used == conn->outgoing_buff_count;

        if (result) {
            // set context only when stream data has just been consumed
            conn->write_buffer.context = (uintptr_t) conn->previous_stream_data;
            conn->previous_stream_data = 0;
        }

        conn->outgoing_buff_idx   += used;
        qd_log(tcp_adaptor->log_source, QD_LOG_DEBUG,
               "[C%"PRIu64"] Copied %i buffers, %i remain", conn->conn_id, used, conn->outgoing_buff_count - conn->outgoing_buff_idx);
    }
    return result;
}

static void handle_outgoing(qdr_tcp_connection_t *conn)
{
    if (conn->outstream) {
        if (IS_ATOMIC_FLAG_SET(&conn->raw_closed_write)) {
            // flush outgoing buffers and free attached stream_data objects
            flush_outgoing_buffs(conn);
            // give no more buffers to raw connection
            return;
        }
        qd_message_t *msg = qdr_delivery_message(conn->outstream);
        bool read_more_body = true;

        if (conn->outgoing_buff_count > 0) {
            // flush outgoing buffs that hold body data waiting to go out
            read_more_body = copy_outgoing_buffs(conn);
        }
        while (read_more_body) {
            ZERO(conn->outgoing_buffs);
            conn->outgoing_buff_idx   = 0;
            conn->outgoing_buff_count = read_message_body(conn, msg, conn->outgoing_buffs, WRITE_BUFFERS);

            if (conn->outgoing_buff_count > 0) {
                // Send the data just returned
                read_more_body = copy_outgoing_buffs(conn);
            } else {
                // The incoming stream has no new data to send
                break;
            }
        }

        if (conn->write_buffer.size && !conn->write_pending) {
            if (pn_raw_connection_write_buffers(conn->pn_raw_conn, &conn->write_buffer, 1)) {
                qd_log(tcp_adaptor->log_source, QD_LOG_DEBUG,
                       "[C%"PRIu64"] pn_raw_connection_write_buffers wrote %i bytes", conn->conn_id, conn->write_buffer.size);

                conn->write_pending = true;
            } else {
                qd_log(tcp_adaptor->log_source, QD_LOG_DEBUG,
                       "[C%"PRIu64"] pn_raw_connection_write_buffers could not write %i bytes", conn->conn_id, conn->write_buffer.size);
            }
        }

        if (conn->read_eos_seen) {
            qd_log(tcp_adaptor->log_source, QD_LOG_DEBUG,
                   "[C%"PRIu64"] handle_outgoing calling pn_raw_connection_write_close(). rcv_complete:%s, send_complete:%s",
                    conn->conn_id, qd_message_receive_complete(msg) ? "T" : "F", qd_message_send_complete(msg) ? "T" : "F");
            SET_ATOMIC_FLAG(&conn->raw_closed_write);
            pn_raw_connection_write_close(conn->pn_raw_conn);
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

static pn_data_t * qdr_tcp_conn_properties()
{
   // Return a new tcp connection properties map.
    pn_data_t *props = pn_data(0);
    pn_data_put_map(props);
    pn_data_enter(props);
    pn_data_put_symbol(props,
                       pn_bytes(strlen(QD_CONNECTION_PROPERTY_ADAPTOR_KEY),
                                       QD_CONNECTION_PROPERTY_ADAPTOR_KEY));
    pn_data_put_string(props,
                       pn_bytes(strlen(QD_CONNECTION_PROPERTY_TCP_ADAPTOR_VALUE),
                                       QD_CONNECTION_PROPERTY_TCP_ADAPTOR_VALUE));
    pn_data_exit(props);
    return props;
}

static void qdr_tcp_connection_ingress_accept(qdr_tcp_connection_t* tc)
{
    allocate_tcp_write_buffer(&tc->write_buffer);
    allocate_tcp_buffer(&tc->read_buffer);
    tc->remote_address = get_address_string(tc->pn_raw_conn);
    tc->global_id = get_global_id(tc->bridge->site_id, tc->remote_address);

    //
    // The qdr_connection_info() function makes its own copy of the passed in tcp_conn_properties.
    // So, we need to call pn_data_free(tcp_conn_properties).
    //
    pn_data_t *tcp_conn_properties = qdr_tcp_conn_properties();
    qdr_connection_info_t *info = qdr_connection_info(false,               // is_encrypted,
                                                      false,               // is_authenticated,
                                                      true,                // opened,
                                                      "",                  // *sasl_mechanisms,
                                                      QD_INCOMING,         // dir,
                                                      tc->remote_address,  // *host,
                                                      "",                  // *ssl_proto,
                                                      "",                  // *ssl_cipher,
                                                      "",                  // *user,
                                                      "TcpAdaptor",        // *container,
                                                      tcp_conn_properties, // *connection_properties,
                                                      0,                   // ssl_ssf,
                                                      false,               // ssl,
                                                      "",                  // peer router version,
                                                      false);              // streaming links
    pn_data_free(tcp_conn_properties);

    tc->conn_id = qd_server_allocate_connection_id(tc->server);
    qdr_connection_t *conn = qdr_connection_opened(tcp_adaptor->core,
                                                   tcp_adaptor->adaptor,
                                                   true,            // incoming
                                                   QDR_ROLE_NORMAL, // role
                                                   1,               // cost
                                                   tc->conn_id,     // management_id
                                                   0,               // label
                                                   0,               // remote_container_id
                                                   false,           // strip_annotations_in
                                                   false,           // strip_annotations_out
                                                   250,             // link_capacity
                                                   0,               // vhost
                                                   0,               // policy_spec
                                                   info,            // connection_info
                                                   0,               // context_binder
                                                   0);              // bind_token
    tc->qdr_conn = conn;
    qdr_connection_set_context(conn, tc);

    qdr_terminus_t *dynamic_source = qdr_terminus(0);
    qdr_terminus_set_dynamic(dynamic_source);
    qdr_terminus_t *target = qdr_terminus(0);
    qdr_terminus_set_address(target, tc->bridge->address);

    tc->outgoing = qdr_link_first_attach(conn,
                                         QD_OUTGOING,
                                         dynamic_source,    //qdr_terminus_t   *source,
                                         qdr_terminus(0),   //qdr_terminus_t   *target,
                                         "tcp.ingress.out", //const char       *name,
                                         0,                 //const char       *terminus_addr,
                                         false,
                                         NULL,
                                         &(tc->outgoing_id));
    qdr_link_set_context(tc->outgoing, tc);
    tc->incoming = qdr_link_first_attach(conn,
                                         QD_INCOMING,
                                         qdr_terminus(0),  //qdr_terminus_t   *source,
                                         target,           //qdr_terminus_t   *target,
                                         "tcp.ingress.in", //const char       *name,
                                         0,                //const char       *terminus_addr,
                                         false,
                                         NULL,
                                         &(tc->incoming_id));
    tc->opened_time = tcp_adaptor->core->uptime_ticks;
    qdr_link_set_context(tc->incoming, tc);

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
            qd_log(log, QD_LOG_INFO,
                   "[C%"PRIu64"] PN_RAW_CONNECTION_CONNECTED Listener ingress accepted to %s from %s (global_id=%s)",
                   conn->conn_id, conn->bridge->host_port, conn->remote_address, conn->global_id);
            break;
        } else {
            conn->remote_address = get_address_string(conn->pn_raw_conn);
            conn->opened_time = tcp_adaptor->core->uptime_ticks;
            qd_log(log, QD_LOG_INFO,
                   "[C%"PRIu64"] PN_RAW_CONNECTION_CONNECTED Connector egress connected to %s",
                   conn->conn_id, conn->remote_address);
            if (!!conn->initial_delivery) {
                qdr_tcp_open_server_side_connection(conn);
            }
            while (qdr_connection_process(conn->qdr_conn)) {}
            handle_outgoing(conn);
            break;
        }
    }
    case PN_RAW_CONNECTION_CLOSED_READ: {
        qd_log(log, QD_LOG_DEBUG, "[C%"PRIu64"][L%"PRIu64"] PN_RAW_CONNECTION_CLOSED_READ %s",
               conn->conn_id, conn->incoming_id, qdr_tcp_connection_role_name(conn));
        SET_ATOMIC_FLAG(&conn->raw_closed_read);
        LOCK(conn->activation_lock);
        conn->q2_blocked = false;
        UNLOCK(conn->activation_lock);
        handle_incoming(conn, "PNRC_CLOSED_READ");
        break;
    }
    case PN_RAW_CONNECTION_CLOSED_WRITE: {
        qd_log(log, QD_LOG_DEBUG,
               "[C%"PRIu64"] PN_RAW_CONNECTION_CLOSED_WRITE %s",
               conn->conn_id, qdr_tcp_connection_role_name(conn));
        SET_ATOMIC_FLAG(&conn->raw_closed_write);
        break;
    }
    case PN_RAW_CONNECTION_DISCONNECTED: {
        qd_log(log, QD_LOG_INFO,
               "[C%"PRIu64"] PN_RAW_CONNECTION_DISCONNECTED %s",
               conn->conn_id, qdr_tcp_connection_role_name(conn));
        LOCK(conn->activation_lock);
        conn->pn_raw_conn = 0;
        UNLOCK(conn->activation_lock);
        handle_disconnected(conn);
        break;
    }
    case PN_RAW_CONNECTION_NEED_WRITE_BUFFERS: {
        qd_log(log, QD_LOG_DEBUG,
               "[C%"PRIu64"] PN_RAW_CONNECTION_NEED_WRITE_BUFFERS %s",
               conn->conn_id, qdr_tcp_connection_role_name(conn));
        while (qdr_connection_process(conn->qdr_conn)) {}
        handle_outgoing(conn);
        break;
    }
    case PN_RAW_CONNECTION_NEED_READ_BUFFERS: {
        qd_log(log, QD_LOG_DEBUG,
               "[C%"PRIu64"] PN_RAW_CONNECTION_NEED_READ_BUFFERS %s",
               conn->conn_id, qdr_tcp_connection_role_name(conn));
        while (qdr_connection_process(conn->qdr_conn)) {}
        if (conn->incoming_started) {
            grant_read_buffers(conn);
            handle_incoming(conn, "PNRC_NEED_READ_BUFFERS");
        }
        break;
    }
    case PN_RAW_CONNECTION_WAKE: {
        qd_log(log, QD_LOG_DEBUG,
               "[C%"PRIu64"] PN_RAW_CONNECTION_WAKE %s",
               conn->conn_id, qdr_tcp_connection_role_name(conn));
        if (sys_atomic_set(&conn->q2_restart, 0)) {
            LOCK(conn->activation_lock);
            conn->q2_blocked = false;
            UNLOCK(conn->activation_lock);
            // note: unit tests grep for this log!
            qd_log(log, QD_LOG_TRACE,
                   "[C%"PRIu64"] %s client link unblocked from Q2 limit",
                   conn->conn_id, qdr_tcp_connection_role_name(conn));
            handle_incoming(conn, "PNRC_WAKE after Q2 unblock");
        }
        while (qdr_connection_process(conn->qdr_conn)) {}
        break;
    }
    case PN_RAW_CONNECTION_READ: {
        qd_log(log, QD_LOG_DEBUG,
               "[C%"PRIu64"] PN_RAW_CONNECTION_READ %s Event ",
               conn->conn_id, qdr_tcp_connection_role_name(conn));
        int read = 0;
        if (conn->incoming_started) {
            // Streaming message exists. Process read normally.
            read = handle_incoming(conn, "PNRC_READ");
        }
        qd_log(log, QD_LOG_DEBUG,
               "[C%"PRIu64"] PN_RAW_CONNECTION_READ Read %i bytes. Total read %"PRIu64" bytes",
               conn->conn_id, read, conn->bytes_in);
        while (qdr_connection_process(conn->qdr_conn)) {}
        break;
    }
    case PN_RAW_CONNECTION_WRITTEN: {
        pn_raw_buffer_t buff;
        if ( pn_raw_connection_take_written_buffers(conn->pn_raw_conn, &buff, 1) ) {
            size_t written = buff.size;
            if (buff.context) {
                qd_message_stream_data_release_up_to((qd_message_stream_data_t*) buff.context);
            }
            conn->write_pending = false;
            conn->write_buffer.size = 0;
            conn->write_buffer.offset = 0;
            conn->write_buffer.context = 0;
            conn->last_out_time = tcp_adaptor->core->uptime_ticks;
            conn->bytes_out += written;
            LOCK(conn->bridge->stats_lock);
            conn->bridge->bytes_out += written;
            UNLOCK(conn->bridge->stats_lock);

            if (written > 0) {
                // Tell the upstream to open its receive window.  Note: this update
                // is sent to the upstream (ingress) TCP adaptor. Since this update
                // is internal to the router network (never sent to the client) we
                // do not need to use the section_number (no section numbers in a
                // TCP stream!) and use section_offset only.
                //
                qd_delivery_state_t *dstate = qd_delivery_state();
                dstate->section_number = 0;
                dstate->section_offset = conn->bytes_out;
                qdr_delivery_remote_state_updated(tcp_adaptor->core, conn->outstream,
                                                  PN_RECEIVED,
                                                  false,  // settled
                                                  dstate,
                                                  false);
            }

            qd_log(log, QD_LOG_DEBUG,
                   "[C%"PRIu64"] PN_RAW_CONNECTION_WRITTEN %s pn_raw_connection_take_written_buffers wrote %zu bytes. Total written %"PRIu64" bytes",
                   conn->conn_id, qdr_tcp_connection_role_name(conn), written, conn->bytes_out);
            handle_outgoing(conn);
        }
        while (qdr_connection_process(conn->qdr_conn)) {}
        break;
    }
    default:
        qd_log(log, QD_LOG_DEBUG, "[C%"PRIu64"] Unexpected Event: %d", conn->conn_id, pn_event_type(e));
        break;
    }
}

static qdr_tcp_connection_t *qdr_tcp_connection_ingress(qd_tcp_listener_t* listener)
{
    qdr_tcp_connection_t* tc = new_qdr_tcp_connection_t();
    ZERO(tc);
    tc->activation_lock = sys_mutex();
    tc->ingress = true;
    tc->context.context = tc;
    tc->context.handler = &handle_connection_event;
    tc->bridge = listener->config;
    sys_atomic_inc(&tc->bridge->ref_count);
    tc->server = listener->server;
    sys_atomic_init(&tc->q2_restart, 0);
    sys_atomic_init(&tc->raw_closed_read, 0);
    sys_atomic_init(&tc->raw_closed_write, 0);

    LOCK(tc->bridge->stats_lock);
    tc->bridge->connections_opened +=1;
    UNLOCK(tc->bridge->stats_lock);

    tc->pn_raw_conn = pn_raw_connection();
    pn_raw_connection_set_context(tc->pn_raw_conn, tc);
    //the following call will cause a PN_RAW_CONNECTION_CONNECTED
    //event on another thread, which is where the rest of the
    //initialisation will happen, through a call to
    //qdr_tcp_connection_ingress_accept
    qd_log(tcp_adaptor->log_source, QD_LOG_INFO, "[C%"PRIu64"] call pn_listener_raw_accept()", tc->conn_id);
    pn_listener_raw_accept(listener->pn_listener, tc->pn_raw_conn);
    return tc;
}


static void qdr_tcp_open_server_side_connection(qdr_tcp_connection_t* tc)
{
    const char *host = tc->egress_dispatcher ? "egress-dispatch" : tc->bridge->host_port;
    qd_log(tcp_adaptor->log_source, QD_LOG_INFO, "[C%"PRIu64"] Opening server-side core connection %s", tc->conn_id, host);

    //
    // The qdr_connection_info() function makes its own copy of the passed in tcp_conn_properties.
    // So, we need to call pn_data_free(tcp_conn_properties)
    //
    pn_data_t *tcp_conn_properties = qdr_tcp_conn_properties();
    qdr_connection_info_t *info = qdr_connection_info(false,       //bool             is_encrypted,
                                                      false,       //bool             is_authenticated,
                                                      true,        //bool             opened,
                                                      "",          //char            *sasl_mechanisms,
                                                      QD_OUTGOING, //qd_direction_t   dir,
                                                      host,        //const char      *host,
                                                      "",          //const char      *ssl_proto,
                                                      "",          //const char      *ssl_cipher,
                                                      "",          //const char      *user,
                                                      "TcpAdaptor",//const char      *container,
                                                      tcp_conn_properties,// pn_data_t *connection_properties,
                                                      0,           //int              ssl_ssf,
                                                      false,       //bool             ssl,
                                                      "",          // peer router version,
                                                      false);      // streaming links
    pn_data_free(tcp_conn_properties);

    qdr_connection_t *conn = qdr_connection_opened(tcp_adaptor->core,
                                                   tcp_adaptor->adaptor,
                                                   false,           // incoming
                                                   QDR_ROLE_NORMAL, // role
                                                   1,               // cost
                                                   tc->conn_id,     // management_id
                                                   0,               // label
                                                   0,               // remote_container_id
                                                   false,           // strip_annotations_in
                                                   false,           // strip_annotations_out
                                                   250,             // link_capacity
                                                   0,               // vhost
                                                   0,               // policy_spec
                                                   info,            // connection_info
                                                   0,               // context_binder
                                                   0);              // bind_token
    tc->qdr_conn = conn;
    qdr_connection_set_context(conn, tc);

    qdr_terminus_t *source = qdr_terminus(0);
    qdr_terminus_set_address(source, tc->bridge->address);

    // This attach passes the ownership of the delivery from the core-side connection and link
    // to the adaptor-side outgoing connection and link.
    uint64_t i_conn_id = 0;
    uint64_t i_link_id = 0;
    if (!!tc->initial_delivery) {
        i_conn_id = tc->initial_delivery->conn_id;
        i_link_id = tc->initial_delivery->link_id;
    }
    tc->outgoing = qdr_link_first_attach(conn,
                                         QD_OUTGOING,
                                         source,           //qdr_terminus_t   *source,
                                         qdr_terminus(0),  //qdr_terminus_t   *target,
                                         "tcp.egress.out", //const char       *name,
                                         0,                //const char       *terminus_addr,
                                         !(tc->egress_dispatcher),
                                         tc->initial_delivery,
                                         &(tc->outgoing_id));
    if (!!tc->initial_delivery) {
        qd_log(tcp_adaptor->log_source, QD_LOG_DEBUG,
               DLV_FMT" initial_delivery ownership passed to "DLV_FMT,
               i_conn_id, i_link_id, tc->initial_delivery->delivery_id,
               tc->outgoing->conn_id, tc->outgoing->identity, tc->initial_delivery->delivery_id);
        qdr_delivery_decref(tcp_adaptor->core, tc->initial_delivery, "tcp-adaptor - passing initial_delivery into new link");
        tc->initial_delivery = 0;
    }
    qdr_link_set_context(tc->outgoing, tc);
}


static qdr_tcp_connection_t *qdr_tcp_connection_egress(qd_tcp_bridge_t *config, qd_server_t *server, qdr_delivery_t *initial_delivery)
{
    qdr_tcp_connection_t* tc = new_qdr_tcp_connection_t();
    ZERO(tc);
    tc->activation_lock = sys_mutex();
    if (initial_delivery) {
        tc->egress_dispatcher = false;
        tc->initial_delivery  = initial_delivery;
        qdr_delivery_incref(initial_delivery, "qdr_tcp_connection_egress - held initial delivery");
    } else {
        tc->activate_timer = qd_timer(tcp_adaptor->core->qd, on_activate, tc);
        tc->egress_dispatcher = true;
    }
    tc->ingress = false;
    tc->context.context = tc;
    tc->context.handler = &handle_connection_event;
    tc->bridge = config;
    sys_atomic_inc(&tc->bridge->ref_count);
    tc->server = server;
    sys_atomic_init(&tc->q2_restart, 0);
    sys_atomic_init(&tc->raw_closed_read, 0);
    sys_atomic_init(&tc->raw_closed_write, 0);
    tc->conn_id = qd_server_allocate_connection_id(tc->server);

    LOCK(tc->bridge->stats_lock);
    tc->bridge->connections_opened +=1;
    UNLOCK(tc->bridge->stats_lock);

    //
    // If this is the egress dispatcher, set up the core connection now.
    // Otherwise, set up a physical raw connection and wait until we are
    // running in that connection's context to set up the core
    // connection.
    //
    if (tc->egress_dispatcher)
        qdr_tcp_open_server_side_connection(tc);
    else {
        allocate_tcp_write_buffer(&tc->write_buffer);
        allocate_tcp_buffer(&tc->read_buffer);
        qd_log(tcp_adaptor->log_source, QD_LOG_INFO,
               "[C%"PRIu64"] call pn_proactor_raw_connect(). Egress connecting to: %s",
               tc->conn_id, tc->bridge->host_port);
        tc->pn_raw_conn = pn_raw_connection();
        pn_raw_connection_set_context(tc->pn_raw_conn, tc);
        pn_proactor_raw_connect(qd_server_proactor(tc->server), tc->pn_raw_conn, tc->bridge->host_port);
    }

    return tc;
}


static qd_tcp_bridge_t *qd_bridge_config()
{
    qd_tcp_bridge_t *bc = new_qd_tcp_bridge_t();
    if (!bc) return 0;
    ZERO(bc);
    sys_atomic_init(&bc->ref_count, 1);
    bc->stats_lock = sys_mutex();
    return bc;
}


static void free_bridge_config(qd_tcp_bridge_t *config)
{
    if (!config) return;
    if (sys_atomic_dec(&config->ref_count) > 1) return;

    qd_log(tcp_adaptor->log_source, QD_LOG_INFO,
           "Deleted TCP bridge configuation '%s' for address %s, %s, siteId %s. "
           "Connections opened:%"PRIu64", closed:%"PRIu64". Bytes in:%"PRIu64", out:%"PRIu64,
           config->name, config->address, config->host_port, config->site_id,
           config->connections_opened, config->connections_closed, config->bytes_in, config->bytes_out);
    free(config->name);
    free(config->address);
    free(config->host);
    free(config->port);
    free(config->site_id);
    free(config->host_port);

    sys_atomic_destroy(&config->ref_count);
    sys_mutex_free(config->stats_lock);
    free_qd_tcp_bridge_t(config);
}

#define CHECK() if (qd_error_code()) goto error

static qd_error_t load_bridge_config(qd_dispatch_t *qd, qd_tcp_bridge_t *config, qd_entity_t* entity, bool is_listener)
{
    qd_error_clear();

    config->name    = qd_entity_get_string(entity, "name");      CHECK();
    config->address = qd_entity_get_string(entity, "address");   CHECK();
    config->host    = qd_entity_get_string(entity, "host");      CHECK();
    config->port    = qd_entity_get_string(entity, "port");      CHECK();
    config->site_id = qd_entity_opt_string(entity, "siteId", 0); CHECK();

    int hplen = strlen(config->host) + strlen(config->port) + 2;
    config->host_port = malloc(hplen);
    snprintf(config->host_port, hplen, "%s:%s", config->host, config->port);

    return QD_ERROR_NONE;

 error:
    free_bridge_config(config);
    return qd_error_code();
}

static void log_tcp_bridge_config(qd_log_source_t *log, qd_tcp_bridge_t *c, const char *what) {
    qd_log(log, QD_LOG_INFO, "Configured %s for %s, %s:%s", what, c->address, c->host, c->port);
}

void qd_tcp_listener_decref(qd_tcp_listener_t* li)
{
    if (li && sys_atomic_dec(&li->ref_count) == 1) {
        sys_atomic_destroy(&li->ref_count);
        free_bridge_config(li->config);
        free_qd_tcp_listener_t(li);
    }
}

static void handle_listener_event(pn_event_t *e, qd_server_t *qd_server, void *context) {
    qd_log_source_t *log = tcp_adaptor->log_source;

    qd_tcp_listener_t *li = (qd_tcp_listener_t*) context;
    const char *host_port = li->config->host_port;

    switch (pn_event_type(e)) {

    case PN_LISTENER_OPEN: {
        qd_log(log, QD_LOG_NOTICE, "PN_LISTENER_OPEN Listening on %s", host_port);
        break;
    }

    case PN_LISTENER_ACCEPT: {
        qd_log(log, QD_LOG_INFO, "PN_LISTENER_ACCEPT Accepting TCP connection to %s", host_port);
        qdr_tcp_connection_ingress(li);
        break;
    }

    case PN_LISTENER_CLOSE:
        if (li->pn_listener) {
            pn_condition_t *cond = pn_listener_condition(li->pn_listener);
            if (pn_condition_is_set(cond)) {
                qd_log(log, QD_LOG_ERROR, "PN_LISTENER_CLOSE Listener error on %s: %s (%s)", host_port,
                       pn_condition_get_description(cond),
                       pn_condition_get_name(cond));
            } else {
                qd_log(log, QD_LOG_TRACE, "PN_LISTENER_CLOSE Listener closed on %s", host_port);
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
    li->config = qd_bridge_config();
    return li;
}

static const int BACKLOG = 50;  /* Listening backlog */

static bool tcp_listener_listen(qd_tcp_listener_t *li) {
   li->pn_listener = pn_listener();
    if (li->pn_listener) {
        pn_listener_set_context(li->pn_listener, &li->context);
        pn_proactor_listen(qd_server_proactor(li->server), li->pn_listener, li->config->host_port, BACKLOG);
        sys_atomic_inc(&li->ref_count); /* In use by proactor, PN_LISTENER_CLOSE will dec */
        /* Listen is asynchronous, log "listening" message on PN_LISTENER_OPEN event */
    } else {
        qd_log(tcp_adaptor->log_source, QD_LOG_CRITICAL, "Failed to create listener for %s",
               li->config->host_port);
     }
    return li->pn_listener;
}

qd_tcp_listener_t *qd_dispatch_configure_tcp_listener(qd_dispatch_t *qd, qd_entity_t *entity)
{
    qd_tcp_listener_t *li = qd_tcp_listener(qd->server);
    if (!li || load_bridge_config(qd, li->config, entity, true) != QD_ERROR_NONE) {
        qd_log(tcp_adaptor->log_source, QD_LOG_ERROR, "Unable to create tcp listener: %s", qd_error_message());
        qd_tcp_listener_decref(li);
        return 0;
    }
    DEQ_ITEM_INIT(li);
    DEQ_INSERT_TAIL(tcp_adaptor->listeners, li);
    log_tcp_bridge_config(tcp_adaptor->log_source, li->config, "TcpListener");
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
        qd_log(tcp_adaptor->log_source, QD_LOG_INFO,
               "Deleted TcpListener for %s, %s:%s",
               li->config->address, li->config->host, li->config->port);
        qd_tcp_listener_decref(li);
    }
}

qd_error_t qd_entity_refresh_tcpListener(qd_entity_t* entity, void *impl)
{
    qd_tcp_listener_t *listener = (qd_tcp_listener_t*)impl;

    LOCK(listener->config->stats_lock);
    uint64_t bi = listener->config->bytes_in;
    uint64_t bo = listener->config->bytes_out;
    uint64_t co = listener->config->connections_opened;
    uint64_t cc = listener->config->connections_closed;
    UNLOCK(listener->config->stats_lock);


    if (   qd_entity_set_long(entity, "bytesIn",           bi) == 0
        && qd_entity_set_long(entity, "bytesOut",          bo) == 0
        && qd_entity_set_long(entity, "connectionsOpened", co) == 0
        && qd_entity_set_long(entity, "connectionsClosed", cc) == 0)
    {
        return QD_ERROR_NONE;
    }
    return qd_error_code();
}

static qd_tcp_connector_t *qd_tcp_connector(qd_server_t *server)
{
    qd_tcp_connector_t *c = new_qd_tcp_connector_t();
    if (!c) return 0;
    ZERO(c);
    sys_atomic_init(&c->ref_count, 1);
    c->server = server;
    c->config = qd_bridge_config();

    return c;
}

void qd_tcp_connector_decref(qd_tcp_connector_t* c)
{
    if (c && sys_atomic_dec(&c->ref_count) == 1) {
        sys_atomic_destroy(&c->ref_count);
        free_bridge_config(c->config);
        free_qd_tcp_connector_t(c);
    }
}

qd_tcp_connector_t *qd_dispatch_configure_tcp_connector(qd_dispatch_t *qd, qd_entity_t *entity)
{
    qd_tcp_connector_t *c = qd_tcp_connector(qd->server);
    if (!c || load_bridge_config(qd, c->config, entity, true) != QD_ERROR_NONE) {
        qd_log(tcp_adaptor->log_source, QD_LOG_ERROR, "Unable to create tcp connector: %s", qd_error_message());
        qd_tcp_connector_decref(c);
        return 0;
    }
    DEQ_ITEM_INIT(c);
    DEQ_INSERT_TAIL(tcp_adaptor->connectors, c);
    log_tcp_bridge_config(tcp_adaptor->log_source, c->config, "TcpConnector");
    c->dispatcher = qdr_tcp_connection_egress(c->config, c->server, NULL);
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
        //deliveries out to live connections:
        qd_log(tcp_adaptor->log_source, QD_LOG_INFO,
               "Deleted TcpConnector for %s, %s:%s",
               ct->config->address, ct->config->host, ct->config->port);
        close_egress_dispatcher((qdr_tcp_connection_t*) ct->dispatcher);
        DEQ_REMOVE(tcp_adaptor->connectors, ct);
        qd_tcp_connector_decref(ct);
    }
}

qd_error_t qd_entity_refresh_tcpConnector(qd_entity_t* entity, void *impl)
{
    qd_tcp_connector_t *connector = (qd_tcp_connector_t*)impl;

    LOCK(connector->config->stats_lock);
    uint64_t bi = connector->config->bytes_in;
    uint64_t bo = connector->config->bytes_out;
    uint64_t co = connector->config->connections_opened;
    uint64_t cc = connector->config->connections_closed;
    UNLOCK(connector->config->stats_lock);


    if (   qd_entity_set_long(entity, "bytesIn",           bi) == 0
        && qd_entity_set_long(entity, "bytesOut",          bo) == 0
        && qd_entity_set_long(entity, "connectionsOpened", co) == 0
        && qd_entity_set_long(entity, "connectionsClosed", cc) == 0)
    {
        return QD_ERROR_NONE;
    }
    return qd_error_code();
}

static void qdr_tcp_first_attach(void *context, qdr_connection_t *conn, qdr_link_t *link,
                                 qdr_terminus_t *source, qdr_terminus_t *target,
                                 qd_session_class_t session_class)
{
    void *tcontext = qdr_connection_get_context(conn);
    if (tcontext) {
        qdr_tcp_connection_t* conn = (qdr_tcp_connection_t*) tcontext;
        qd_log(tcp_adaptor->log_source, QD_LOG_DEBUG,
               "[C%"PRIu64"][L%"PRIu64"] qdr_tcp_first_attach: NOOP",
               conn->conn_id, qdr_tcp_conn_linkid(conn));
    } else {
        qd_log(tcp_adaptor->log_source, QD_LOG_ERROR, "qdr_tcp_first_attach: no link context");
        assert(false);
    }
}

static void qdr_tcp_connection_copy_reply_to(qdr_tcp_connection_t* tc, qd_iterator_t* reply_to)
{
    tc->reply_to = (char*)  qd_iterator_copy(reply_to);
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
            qd_log(tcp_adaptor->log_source, QD_LOG_DEBUG,
                   "[C%"PRIu64"][L%"PRIu64"] %s qdr_tcp_second_attach",
                   tc->conn_id, tc->outgoing_id,
                   qdr_tcp_quadrant_id(tc, link));
            if (tc->ingress) {
                qdr_tcp_connection_copy_reply_to(tc, qdr_terminus_get_address(source));
                // for ingress, can start reading from socket once we have
                // a reply to address, as that is when we are able to send
                // out a message
                handle_incoming(tc, "qdr_tcp_second_attach");
            }
            qdr_link_flow(tcp_adaptor->core, link, 10, false);
        } else if (!tc->ingress) {
            qd_log(tcp_adaptor->log_source, QD_LOG_DEBUG,
                   "[C%"PRIu64"][L%"PRIu64"] %s qdr_tcp_second_attach",
                   tc->conn_id, tc->incoming_id,
                   qdr_tcp_quadrant_id(tc, link));
        }
    } else {
        qd_log(tcp_adaptor->log_source, QD_LOG_ERROR, "qdr_tcp_second_attach: no link context");
        assert(false);
    }
}


static void qdr_tcp_detach(void *context, qdr_link_t *link, qdr_error_t *error, bool first, bool close)
{
    qd_log(tcp_adaptor->log_source, QD_LOG_ERROR, "qdr_tcp_detach");
    assert(false);
}


static void qdr_tcp_flow(void *context, qdr_link_t *link, int credit)
{
    void* link_context = qdr_link_get_context(link);
    if (link_context) {
        qdr_tcp_connection_t* conn = (qdr_tcp_connection_t*) link_context;
        if (!conn->flow_enabled && credit > 0) {
            conn->flow_enabled = true;
            qd_log(tcp_adaptor->log_source, QD_LOG_DEBUG,
                   "[C%"PRIu64"][L%"PRIu64"] qdr_tcp_flow: Flow enabled, credit=%d",
                   conn->conn_id, conn->outgoing_id, credit);
            handle_incoming(conn, "qdr_tcp_flow");
        } else {
            qd_log(tcp_adaptor->log_source, QD_LOG_DEBUG,
                   "[C%"PRIu64"][L%"PRIu64"] qdr_tcp_flow: No action. enabled:%s, credit:%d",
                   conn->conn_id, qdr_tcp_conn_linkid(conn), conn->flow_enabled?"T":"F", credit);
        }
    } else {
        qd_log(tcp_adaptor->log_source, QD_LOG_ERROR, "qdr_tcp_flow: no link context");
        assert(false);
    }
}


static void qdr_tcp_offer(void *context, qdr_link_t *link, int delivery_count)
{
    void* link_context = qdr_link_get_context(link);
    if (link_context) {
        qdr_tcp_connection_t* conn = (qdr_tcp_connection_t*) link_context;
        qd_log(tcp_adaptor->log_source, QD_LOG_DEBUG,
               "[C%"PRIu64"][L%"PRIu64"] qdr_tcp_offer: NOOP",
               conn->conn_id, qdr_tcp_conn_linkid(conn));
    } else {
        qd_log(tcp_adaptor->log_source, QD_LOG_ERROR, "qdr_tcp_offer: no link context");
        assert(false);
    }

}


static void qdr_tcp_drained(void *context, qdr_link_t *link)
{
    void* link_context = qdr_link_get_context(link);
    if (link_context) {
        qdr_tcp_connection_t* conn = (qdr_tcp_connection_t*) link_context;
        qd_log(tcp_adaptor->log_source, QD_LOG_DEBUG,
               "[C%"PRIu64"][L%"PRIu64"] qdr_tcp_drained: NOOP",
               conn->conn_id, qdr_tcp_conn_linkid(conn));
    } else {
        qd_log(tcp_adaptor->log_source, QD_LOG_ERROR, "qdr_tcp_drained: no link context");
        assert(false);
    }
}


static void qdr_tcp_drain(void *context, qdr_link_t *link, bool mode)
{
    void* link_context = qdr_link_get_context(link);
    if (link_context) {
        qdr_tcp_connection_t* conn = (qdr_tcp_connection_t*) link_context;
        qd_log(tcp_adaptor->log_source, QD_LOG_DEBUG,
               "[C%"PRIu64"][L%"PRIu64"] qdr_tcp_drain: NOOP",
               conn->conn_id, qdr_tcp_conn_linkid(conn));
    } else {
        qd_log(tcp_adaptor->log_source, QD_LOG_ERROR, "qdr_tcp_drain: no link context");
        assert(false);
    }
}


static int qdr_tcp_push(void *context, qdr_link_t *link, int limit)
{
    void* link_context = qdr_link_get_context(link);
    if (link_context) {
        qdr_tcp_connection_t* conn = (qdr_tcp_connection_t*) link_context;
        qd_log(tcp_adaptor->log_source, QD_LOG_DEBUG,
               "[C%"PRIu64"][L%"PRIu64"] qdr_tcp_push",
               conn->conn_id, qdr_tcp_conn_linkid(conn));
        return qdr_link_process_deliveries(tcp_adaptor->core, link, limit);
    } else {
        qd_log(tcp_adaptor->log_source, QD_LOG_ERROR, "qdr_tcp_push: no link context");
        assert(false);
        return 0;
    }
}


static uint64_t qdr_tcp_deliver(void *context, qdr_link_t *link, qdr_delivery_t *delivery, bool settled)
{

    // @TODO(kgiusti): determine why this is necessary to prevent window full stall:
    qd_message_Q2_holdoff_disable(qdr_delivery_message(delivery));
    void* link_context = qdr_link_get_context(link);
    if (link_context) {
        qdr_tcp_connection_t* tc = (qdr_tcp_connection_t*) link_context;
        qd_log(tcp_adaptor->log_source, QD_LOG_DEBUG,
               DLV_FMT" qdr_tcp_deliver Delivery event", DLV_ARGS(delivery));
        if (tc->egress_dispatcher) {
            qd_log(tcp_adaptor->log_source, QD_LOG_DEBUG,
                   DLV_FMT" tcp_adaptor initiating egress connection", DLV_ARGS(delivery));
            qdr_tcp_connection_egress(tc->bridge, tc->server, delivery);
            return QD_DELIVERY_MOVED_TO_NEW_LINK;
        } else if (!tc->outstream) {
            tc->outstream = delivery;
            qdr_delivery_incref(delivery, "tcp_adaptor - new outstream");
            if (!tc->ingress) {
                //on egress, can only set up link for the reverse
                //direction once we receive the first part of the
                //message from client to server
                qd_message_t *msg = qdr_delivery_message(delivery);
                qd_iterator_t *f_iter = qd_message_field_iterator(msg, QD_FIELD_SUBJECT);
                qdr_tcp_connection_copy_global_id(tc, f_iter);
                qd_iterator_free(f_iter);
                f_iter = qd_message_field_iterator(msg, QD_FIELD_REPLY_TO);
                qdr_tcp_connection_copy_reply_to(tc, f_iter);
                qd_iterator_free(f_iter);
                qdr_terminus_t *target = qdr_terminus(0);
                qdr_terminus_set_address(target, tc->reply_to);
                tc->incoming = qdr_link_first_attach(tc->qdr_conn,
                                                     QD_INCOMING,
                                                     qdr_terminus(0),  //qdr_terminus_t   *source,
                                                     target,           //qdr_terminus_t   *target,
                                                     "tcp.egress.in",  //const char       *name,
                                                     0,                //const char       *terminus_addr,
                                                     false,
                                                     NULL,
                                                     &(tc->incoming_id));
                qd_log(tcp_adaptor->log_source, QD_LOG_DEBUG,
                       "[C%"PRIu64"][L%"PRIu64"] %s Created link to %s",
                       tc->conn_id, tc->incoming->identity,
                       qdr_tcp_quadrant_id(tc, tc->incoming), tc->reply_to);
                qdr_link_set_context(tc->incoming, tc);
                //add this connection to those visible through management now that we have the global_id
                qdr_action_t *action = qdr_action(qdr_add_tcp_connection_CT, "add_tcp_connection");
                action->args.general.context_1 = tc;
                qdr_action_enqueue(tcp_adaptor->core, action);

                handle_incoming(tc, "qdr_tcp_deliver");
            }
        }
        handle_outgoing(tc);
    } else {
        qd_log(tcp_adaptor->log_source, QD_LOG_ERROR, "qdr_tcp_deliver: no link context");
        assert(false);
    }
    return 0;
}


static int qdr_tcp_get_credit(void *context, qdr_link_t *link)
{
    void* link_context = qdr_link_get_context(link);
    if (link_context) {
        qdr_tcp_connection_t* conn = (qdr_tcp_connection_t*) link_context;
        qd_log(tcp_adaptor->log_source, QD_LOG_DEBUG,
               "[C%"PRIu64"][L%"PRIu64"] qdr_tcp_get_credit: NOOP",
               conn->conn_id, qdr_tcp_conn_linkid(conn));
    } else {
        qd_log(tcp_adaptor->log_source, QD_LOG_ERROR, "qdr_tcp_get_credit: no link context");
        assert(false);
    }
    return 10;
}


static void qdr_tcp_delivery_update(void *context, qdr_delivery_t *dlv, uint64_t disp, bool settled)
{
    void* link_context = qdr_link_get_context(qdr_delivery_link(dlv));
    if (link_context) {
        qdr_tcp_connection_t* tc = (qdr_tcp_connection_t*) link_context;
        qd_log(tcp_adaptor->log_source, QD_LOG_DEBUG,
               DLV_FMT" qdr_tcp_delivery_update: disp: %"PRIu64", settled: %s",
               DLV_ARGS(dlv), disp, settled ? "true" : "false");

        if (settled && disp == PN_RELEASED) {
            // When the connector is unable to connect to a tcp endpoint it will
            // release the message. We handle that here by closing the connection.
            // Half-closed status is signalled by read_eos_seen and is not
            // sufficient by itself to force a connection closure.
            qd_log(tcp_adaptor->log_source, QD_LOG_DEBUG,
                   DLV_FMT" qdr_tcp_delivery_update: call pn_raw_connection_close()",
                   DLV_ARGS(dlv));
            pn_raw_connection_close(tc->pn_raw_conn);
        }

        // handle read window updates

        const bool window_was_full = read_window_full(tc);
        tc->window_disabled = settled || tc->window_disabled;

        if (!tc->window_disabled) {

            if (disp == PN_RECEIVED) {
                //
                // the consumer of this TCP flow has updated its tx_sequence:
                //
                uint64_t ignore;
                qd_delivery_state_t *dstate = qdr_delivery_take_local_delivery_state(dlv, &ignore);

                if (!dstate) {
                    qd_log(tcp_adaptor->log_source, QD_LOG_ERROR,
                           "[C%"PRIu64"] BAD PN_RECEIVED - missing delivery-state!!", tc->conn_id);
                } else {
                    // note: the PN_RECEIVED is generated by the remote TCP
                    // adaptor, for simplicity we ignore the section_number since
                    // all we really need is a byte offset:
                    //
                    tc->bytes_unacked = tc->bytes_in - dstate->section_offset;
                    qd_delivery_state_free(dstate);
                }
            } else if (disp) {
                // terminal outcome: drain any pending receive data
                tc->window_disabled = true;
            }
        }

        if (window_was_full && !read_window_full(tc)) {
            // now that the window has opened fetch any outstanding read data
            handle_incoming(tc, "TCP RX window refresh");
        }
    } else {
        qd_log(tcp_adaptor->log_source, QD_LOG_ERROR, "qdr_tcp_delivery_update: no link context");
        assert(false);
    }
}


static void qdr_tcp_conn_close(void *context, qdr_connection_t *conn, qdr_error_t *error)
{
    void *tcontext = qdr_connection_get_context(conn);
    if (tcontext) {
        qdr_tcp_connection_t* conn = (qdr_tcp_connection_t*) tcontext;
        qd_log(tcp_adaptor->log_source, QD_LOG_DEBUG,
               "[C%"PRIu64"][L%"PRIu64"] qdr_tcp_conn_close: NOOP", conn->conn_id, qdr_tcp_conn_linkid(conn));
    } else {
        qd_log(tcp_adaptor->log_source, QD_LOG_ERROR, "qdr_tcp_conn_close: no connection context");
        assert(false);
    }
}


static void qdr_tcp_conn_trace(void *context, qdr_connection_t *conn, bool trace)
{
    void *tcontext = qdr_connection_get_context(conn);
    if (tcontext) {
        qdr_tcp_connection_t* conn = (qdr_tcp_connection_t*) tcontext;
        qd_log(tcp_adaptor->log_source, QD_LOG_DEBUG,
               "[C%"PRIu64"][L%"PRIu64"] qdr_tcp_conn_trace: NOOP",
               conn->conn_id, qdr_tcp_conn_linkid(conn));
    } else {
        qd_log(tcp_adaptor->log_source, QD_LOG_ERROR,
               "qdr_tcp_conn_trace: no connection context");
        assert(false);
    }
}

static void qdr_tcp_activate(void *notused, qdr_connection_t *c)
{
    void *context = qdr_connection_get_context(c);
    if (context) {
        qdr_tcp_connection_t* conn = (qdr_tcp_connection_t*) context;
        LOCK(conn->activation_lock);
        if (conn->pn_raw_conn && !(IS_ATOMIC_FLAG_SET(&conn->raw_closed_read) && IS_ATOMIC_FLAG_SET(&conn->raw_closed_write))) {
            qd_log(tcp_adaptor->log_source, QD_LOG_DEBUG,
                   "[C%"PRIu64"] qdr_tcp_activate: call pn_raw_connection_wake()", conn->conn_id);
            pn_raw_connection_wake(conn->pn_raw_conn);
            UNLOCK(conn->activation_lock);
        } else if (conn->activate_timer) {
            UNLOCK(conn->activation_lock);
            // On egress, the raw connection is only created once the
            // first part of the message encapsulating the
            // client->server half of the stream has been
            // received. Prior to that however a subscribing link (and
            // its associated connection must be setup), for which we
            // fake wakeup by using a timer.
            qd_log(tcp_adaptor->log_source, QD_LOG_DEBUG,
                   "[C%"PRIu64"] qdr_tcp_activate: schedule activate_timer", conn->conn_id);
            qd_timer_schedule(conn->activate_timer, 0);
        } else {
            UNLOCK(conn->activation_lock);
            qd_log(tcp_adaptor->log_source, QD_LOG_DEBUG,
                   "[C%"PRIu64"] qdr_tcp_activate: Cannot activate", conn->conn_id);
        }
    } else {
        qd_log(tcp_adaptor->log_source, QD_LOG_DEBUG, "qdr_tcp_activate: no connection context");
        // assert(false); This is routine. TODO: Is that a problem?
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
                                            qdr_tcp_activate,
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
    DEQ_INIT(adaptor->connections);
    *adaptor_context = adaptor;

    tcp_adaptor = adaptor;
}


static void qdr_tcp_adaptor_final(void *adaptor_context)
{
    qd_log(tcp_adaptor->log_source, QD_LOG_INFO, "Shutting down TCP protocol adaptor");
    qdr_tcp_adaptor_t *adaptor = (qdr_tcp_adaptor_t*) adaptor_context;

    qd_tcp_listener_t *tl = DEQ_HEAD(adaptor->listeners);
    while (tl) {
        qd_tcp_listener_t *next = DEQ_NEXT(tl);
        free_bridge_config(tl->config);
        free_qd_tcp_listener_t(tl);
        tl = next;
    }

    qd_tcp_connector_t *tr = DEQ_HEAD(adaptor->connectors);
    while (tr) {
        qd_tcp_connector_t *next = DEQ_NEXT(tr);
        free_bridge_config(tr->config);
        free_qdr_tcp_connection((qdr_tcp_connection_t*) tr->dispatcher);
        free_qd_tcp_connector_t(tr);
        tr = next;
    }

    qdr_tcp_connection_t *tc = DEQ_HEAD(adaptor->connections);
    while (tc) {
        qdr_tcp_connection_t *next = DEQ_NEXT(tc);
        free_qdr_tcp_connection(tc);
        tc = next;
    }

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
        qd_compose_insert_string(body, conn->bridge->address);
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
    qd_log(tcp_adaptor->log_source, QD_LOG_DEBUG,
           "query for first tcp connection (%i)", offset);
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
        qd_log(core->agent_log, QD_LOG_ERROR,
               "Error performing READ of %s: %s", TCP_CONNECTION_TYPE, query->status.description);
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
    if (!discard) {
        qdr_tcp_connection_t *conn = (qdr_tcp_connection_t*) action->args.general.context_1;
        DEQ_INSERT_TAIL(tcp_adaptor->connections, conn);
        conn->in_list = true;
        qd_log(tcp_adaptor->log_source, QD_LOG_DEBUG, "[C%"PRIu64"] qdr_add_tcp_connection_CT %s (%zu)",
            conn->conn_id, conn->bridge->host_port, DEQ_SIZE(tcp_adaptor->connections));
    }
}

static void qdr_del_tcp_connection_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    if (!discard) {
        qdr_tcp_connection_t *conn = (qdr_tcp_connection_t*) action->args.general.context_1;
        if (conn->in_list) {
            DEQ_REMOVE(tcp_adaptor->connections, conn);
            qd_log(tcp_adaptor->log_source, QD_LOG_DEBUG,
                   "[C%"PRIu64"] qdr_del_tcp_connection_CT %s deleted. bytes_in=%"PRIu64", bytes_out=%"PRId64", "
                   "opened_time=%"PRId64", last_in_time=%"PRId64", last_out_time=%"PRId64". Connections remaining %zu",
                   conn->conn_id, conn->bridge->host_port,
                   conn->bytes_in, conn->bytes_out, conn->opened_time, conn->last_in_time, conn->last_out_time,
                   DEQ_SIZE(tcp_adaptor->connections));
        }
        free_qdr_tcp_connection(conn);
    }
}

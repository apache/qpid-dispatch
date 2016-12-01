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

#include <qpid/dispatch/atomic.h>
#include <qpid/dispatch/amqp.h>
#include <qpid/dispatch/driver.h>
#include <qpid/dispatch/log.h>
#include <qpid/dispatch/threading.h>
#include <qpid/dispatch/timer.h>

#include <libwebsockets.h>

#include <assert.h>
#include <errno.h>

#include "http.h"

typedef struct qd_http_t {
    sys_mutex_t *lock;
    qd_dispatch_t *dispatch;
    qd_log_source_t *log;
    struct lws_context *context;
    qd_timer_t *timer;
} qd_http_t;

/* TODO aconway 2016-11-29: First cut serializes all access to libwebsockets.
 * LWS does have multi-thread facilities but it segregates file descriptors into
 * "serialization groups" which does not match well with dispatches current
 * and planned future threading strategies. Review when we refactor dispatch
 * to use the pn_proactor. At least 2 possibilities:
 *
 * - treat LWS as single-threaded IO code in the 'leader follower' model,
 *   analogous to how we handle libuv.
 * - work with LWS upstream to abstract out IO code so each LWS WSI can operate
 *   as a thread-independent unit, like the proton connection_driver.
 */

static __thread struct {
    qdpn_connector_t *connector; /* Set before each lws_service call */
} per_thread = { NULL };

typedef struct buffer_t { void *start; size_t size; size_t cap; } buffer_t;

/* Extra buffering per connection, stored in the lws_wsi_user() space. */
typedef struct buffers_t {
    buffer_t wtmp;    /* Temp buffer with pre-data header space required by LWS */
    buffer_t over;    /* Can't control LWS read size, buffer the overflow */
} buffers_t;

static void resize(buffer_t *b, size_t size) {
    /* FIXME aconway 2016-11-30: handle alloc failure */
    if (b->start == NULL || b->cap < size) {
        b->start = realloc(b->start, size);
        b->size = b->cap = size;
    }
    b->size = size;
}

/* Push as much as possible into the transport, store overflow in over. */
static void transport_push_max(pn_transport_t *t, pn_bytes_t buf, buffer_t *over) {
    ssize_t cap;
    while (buf.size > 0 && (cap = pn_transport_capacity(t)) > 0) {
        if (buf.size > cap) {
            pn_transport_push(t, buf.start, cap);
            buf.start += cap;
            buf.size -= cap;
        } else {
            pn_transport_push(t, buf.start, buf.size);
            buf.size = 0;
        }
    }
    if (buf.size > 0) {
        if (buf.size > over->cap) {
            resize(over, buf.size);
        }
        memmove(over->start, buf.start, buf.size);
    }
    over->size = buf.size;
}

static qd_http_t *qd_http_from_wsi(struct lws *wsi) {
    return (qd_http_t *)lws_context_user(lws_get_context(wsi));
}

static int callback_amqpws(struct lws *wsi, enum lws_callback_reasons reason,
                           void *user, void *in, size_t len)
{
    buffers_t *b = (buffers_t*)user;
    qd_http_t *h = qd_http_from_wsi(wsi);
    qdpn_connector_t *c = per_thread.connector;
    pn_transport_t *t = qdpn_connector_transport(c);
    const char *name = qdpn_connector_name(c);

    switch (reason) {

    case LWS_CALLBACK_ESTABLISHED: {
        qd_log(h->log, QD_LOG_DEBUG, "HTTP from %s upgraded to AMQP/WebSocket", name);
        memset(b, 0, sizeof(*b));
        break;
    }

    case LWS_CALLBACK_SERVER_WRITEABLE: {
        ssize_t size = pn_transport_pending(t);
        if (size < 0) {
            return -1;
        }
        if (size > 0) {
            pn_bytes_t wbuf = { size, pn_transport_head(t) };
            /* lws_write() demands LWS_PRE bytes of free space before the data */
            resize(&b->wtmp, wbuf.size + LWS_PRE);
            unsigned char *start = (unsigned char*)b->wtmp.start + LWS_PRE;
            memcpy(start, wbuf.start, wbuf.size);
            ssize_t wrote = lws_write(wsi, start, wbuf.size, LWS_WRITE_BINARY);
            if (wrote < 0) {
                pn_transport_close_head(t);
                return -1;
            } else {
                pn_transport_pop(t, (size_t)wrote);
            }
        }
        break;
    }

    case LWS_CALLBACK_RECEIVE: {
        if (pn_transport_capacity(t) < 0) {
            return -1;
        }
        assert(b->over.size == 0);
        transport_push_max(t, pn_bytes(len, in), &b->over);
        if (b->over.size > 0) {
            qd_log(h->log, QD_LOG_TRACE, "amqp/ws read buffered %z bytes on %s", name);
        }
        break;
    }

    case LWS_CALLBACK_WS_PEER_INITIATED_CLOSE: {
        qd_log(h->log, QD_LOG_DEBUG, "AMQP/WebSocket peer close from %s", name);
        pn_transport_close_tail(t);
        break;
    }

    case LWS_CALLBACK_CLOSED: {
        qd_log(h->log, QD_LOG_DEBUG, "AMQP/WebSocket from %s closed", name);
        qdpn_connector_mark_closed(c);
        break;
    }

    default:
        break;
    }
    return 0;
}

static int callback_http(struct lws *wsi, enum lws_callback_reasons reason, void *user,
                         void *in, size_t len)
{
    qdpn_connector_t *c = per_thread.connector;
    buffers_t *b = (buffers_t*)user;

    switch (reason) {
    case LWS_CALLBACK_ESTABLISHED: {
        memset(b, 0, sizeof(*b));
        break;
    }
    case LWS_CALLBACK_CLOSED: {
        qdpn_connector_mark_closed(c);
    }
    default:
        break;
    }
    return 0;
}

static void set_timer_lh(qd_http_t *h);

static void fire_timer(void *void_http) {
    qd_http_t *h = (qd_http_t*)void_http;
    sys_mutex_lock(h->lock);
    lws_service_fd(h->context, NULL);
    set_timer_lh(h);
    sys_mutex_unlock(h->lock);
}

static void set_timer_lh(qd_http_t *h) {
    if (!h->timer) {
        h->timer = qd_timer(h->dispatch, fire_timer, h);
    }
    qd_timer_schedule(h->timer, 1000);
}

void qd_http_connector_process(qdpn_connector_t *c) {
    per_thread.connector = c;  /* Pass to lws via thread-local storage */

    struct lws *wsi = (struct lws*)qdpn_connector_http(c);
    buffers_t *b = (buffers_t*)lws_wsi_user(wsi);
    qd_http_t * h = qd_http_from_wsi(wsi);
    pn_transport_t *t = qdpn_connector_transport(c);

    int flags =
        (qdpn_connector_activated(c, QDPN_CONNECTOR_READABLE) ? POLLIN : 0) |
        (qdpn_connector_activated(c, QDPN_CONNECTOR_WRITABLE) ? POLLOUT : 0);

    if (b && b->over.size) {         /* Consume last over-buffered read */
        transport_push_max(t, pn_bytes(b->over.size, b->over.start), &b->over);
        if (b->over.size) {         /* Don't let LIBWS read if we still are over */
            flags &= ~POLLIN;
        }
    }

    sys_mutex_lock(h->lock);
    struct lws_pollfd pfd = { qdpn_connector_get_fd(c), flags, flags };
    lws_service_fd(h->context, &pfd);
    set_timer_lh(h);
    sys_mutex_unlock(h->lock);

    if (pn_transport_capacity(t) > 0)
        qdpn_connector_activate(c, QDPN_CONNECTOR_READABLE);
    if (pn_transport_pending(t) > 0)
        qdpn_connector_activate(c, QDPN_CONNECTOR_WRITABLE);

    pn_timestamp_t now = qdpn_now(NULL);
    pn_timestamp_t next = pn_transport_tick(t, now);
    /* If we have overflow, re-process immediately after dispatch, otherwise at
     * next proton tick.
     */
    qdpn_connector_wakeup(c, (b && b->over.size) ? now : next);
}

qd_http_connector_t *qd_http_connector(qd_http_t *h, qdpn_connector_t *c) {
    struct lws* wsi = lws_adopt_socket(h->context, qdpn_connector_get_fd(c));
    return (qd_http_connector_t*)wsi;
}

static struct lws_protocols protocols[] = {
    /* first protocol must always be HTTP handler */
    {
        "http-only",		/* name */
        callback_http,		/* callback */
        sizeof(buffers_t),                      /* user data size */
    },
     /* "amqp" is the official oasis AMQP over WebSocket protocol name */
    {
        "amqp",
        callback_amqpws,
        sizeof(buffers_t),
    },
    /* "binary" is an alias for "amqp", for compatibility with clients designed
     * to work with a WebSocket proxy
     */
    {
        "binary",
        callback_amqpws,
        sizeof(buffers_t),
    },

};

qd_http_t *qd_http(qd_dispatch_t *d, qd_log_source_t *log) {
    qd_http_t *h = calloc(1, sizeof(qd_http_t));
    h->lock = sys_mutex();
    h->dispatch = d;
    h->log = log;
    lws_set_log_level(0, NULL);
    struct lws_context_creation_info info = {0};
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.gid = info.uid = -1;
    info.user = h;
    h->context = lws_create_context(&info);
    h->timer =  NULL;           /* Initialized later. */
    return h;
}

void qd_http_free(qd_http_t *h) {
    sys_mutex_free(h->lock);
    if (h->timer) qd_timer_free(h->timer);
    lws_context_destroy(h->context);
    free(h);
}

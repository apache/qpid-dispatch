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
#include "config.h"

/* Shared context for all HTTP connections.  */
struct qd_http_t {
    sys_mutex_t *lock;
    qd_dispatch_t *dispatch;
    qd_log_source_t *log;
    struct lws_context *context;
    qd_timer_t *timer;
    qdpn_connector_t **connectors; /* Indexed by file descriptor */
    size_t connectors_len;
};

static inline qdpn_connector_t *fd_connector(qd_http_t *h, int fd) {
    return (fd < h->connectors_len) ? h->connectors[fd] : NULL;
}

static inline qd_http_t *wsi_http(struct lws *wsi) {
    return (qd_http_t *)lws_context_user(lws_get_context(wsi));
}

static inline qdpn_connector_t *wsi_connector(struct lws *wsi) {
    return fd_connector(wsi_http(wsi), lws_get_socket_fd(wsi));
}

static inline int set_fd(qd_http_t *h, int fd, qdpn_connector_t *c) {
    if (fd >= h->connectors_len) {
        size_t len = h->connectors_len;
        h->connectors_len = (fd+1)*2;
        h->connectors = realloc(h->connectors, h->connectors_len*sizeof(qdpn_connector_t*));
        if (!h->connectors) return -1;
        memset(h->connectors + len, 0, h->connectors_len - len);
    }
    h->connectors[fd] = c;
    return 0;
}

/* Mark the qd connector closed, but leave the FD for LWS to clean up */
int mark_closed(struct lws *wsi) {
    qd_http_t *h = wsi_http(wsi);
    int fd = lws_get_socket_fd(wsi);
    qdpn_connector_t *c = fd_connector(h, fd);
    if (c) {
        qdpn_connector_mark_closed(c);
        return set_fd(h, fd, NULL);
    }
    return 0;
}

/* Push read data into the transport.
 * Return 0 on success, number of bytes un-pushed on failure.
 */
static int transport_push(pn_transport_t *t, pn_bytes_t buf) {
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
    return buf.size;
}

static int normal_close(struct lws *wsi, qdpn_connector_t *c, const char *msg) {
    lws_close_reason(wsi, LWS_CLOSE_STATUS_NORMAL, (unsigned char*)msg, strlen(msg));
    return -1;
}

static int unexpected_close(struct lws *wsi, qdpn_connector_t *c, const char *msg) {
    lws_close_reason(wsi, LWS_CLOSE_STATUS_UNEXPECTED_CONDITION, (unsigned char*)msg, strlen(msg));
    return -1;
}

/*
 * Callback for un-promoted HTTP connections, and low-level external poll operations.
 * Note main HTTP file serving is handled by the "mount" struct below.
 * Called with http lock held.
 */
static int callback_http(struct lws *wsi, enum lws_callback_reasons reason,
                         void *user, void *in, size_t len)
{
    switch (reason) {

    case LWS_CALLBACK_HTTP: {   /* Called if file mount can't find the file */
        lws_return_http_status(wsi, HTTP_STATUS_NOT_FOUND, "file not found");
        return -1;
    }

    case LWS_CALLBACK_CLOSED_HTTP:
        mark_closed(wsi);
        break;

        /* low-level 'protocol[0]' callbacks for all protocols   */
    case LWS_CALLBACK_DEL_POLL_FD: {
        if (mark_closed(wsi)) {
            lws_return_http_status(wsi, HTTP_STATUS_INTERNAL_SERVER_ERROR, "out of memory");
            return -1;
        }
        break;
    }

    case LWS_CALLBACK_CHANGE_MODE_POLL_FD: {
        struct lws_pollargs *p = (struct lws_pollargs*)in;
        qdpn_connector_t *c = wsi_connector(wsi);
        if (c) {
            if (p->events & POLLIN) qdpn_connector_activate(c, QDPN_CONNECTOR_READABLE);
            if (p->events & POLLOUT) qdpn_connector_activate(c, QDPN_CONNECTOR_WRITABLE);
        }
        break;
    }

    default:
        break;
    }

    return 0;
}

/* Buffer to allocate extra header space required by LWS.  */
typedef struct buffer_t { void *start; size_t size; size_t cap; } buffer_t;

/* Callbacks for promoted AMQP over WS connections.
 * Called with http lock held.
 */
static int callback_amqpws(struct lws *wsi, enum lws_callback_reasons reason,
                           void *user, void *in, size_t len)
{
    qd_http_t *h = wsi_http(wsi);
    qdpn_connector_t *c = wsi_connector(wsi);
    pn_transport_t *t = c ? qdpn_connector_transport(c) : NULL;
    const char *name = c ? qdpn_connector_name(c) : "<unknown>";

    switch (reason) {

    case LWS_CALLBACK_ESTABLISHED: {
        memset(user, 0, sizeof(buffer_t));
        qd_log(h->log, QD_LOG_TRACE, "HTTP from %s upgraded to AMQP/WebSocket", name);
        break;
    }

    case LWS_CALLBACK_SERVER_WRITEABLE: {
        ssize_t size;
        if (!t || (size = pn_transport_pending(t)) < 0) {
            return normal_close(wsi, c, "write-closed");
        }
        if (size > 0) {
            const void *start = pn_transport_head(t);
            /* lws_write() demands LWS_PRE bytes of free space before the data */
            size_t tmpsize = size + LWS_PRE;
            buffer_t *wtmp = (buffer_t*)user;
            if (wtmp->start == NULL || wtmp->cap < tmpsize) {
                wtmp->start = realloc(wtmp->start, tmpsize);
                wtmp->size = wtmp->cap = tmpsize;
            }
            if (wtmp->start == NULL) {
                return unexpected_close(wsi, c, "out-of-memory");
            }
            void *tmpstart = wtmp->start + LWS_PRE;
            memcpy(tmpstart, start, size);
            ssize_t wrote = lws_write(wsi, tmpstart, size, LWS_WRITE_BINARY);
            if (wrote < 0) {
                pn_transport_close_head(t);
                return normal_close(wsi, c, "write-error");
            } else {
                pn_transport_pop(t, (size_t)wrote);
            }
        }
        break;
    }

    case LWS_CALLBACK_RECEIVE: {
        if (!t || pn_transport_capacity(t) < 0) {
            return normal_close(wsi, c, "read-closed");
        }
        if (transport_push(t, pn_bytes(len, in))) {
            return unexpected_close(wsi, c, "read-overflow");
        }
        break;
    }

    case LWS_CALLBACK_WS_PEER_INITIATED_CLOSE:
        mark_closed(wsi);
        if (t) {
            pn_transport_close_tail(t);
        }

    case LWS_CALLBACK_CLOSED:
        mark_closed(wsi);
        break;

    default:
        break;
    }
    return 0;
}

/* Mount the console directory into URL space at /  */
static const struct lws_http_mount console_mount = {
    NULL,		/* linked-list pointer to next*/
    "/",		/* mountpoint in URL namespace on this vhost */
    QPID_CONSOLE_STAND_ALONE_INSTALL_DIR, /* where to go on the filesystem for that */
    "index.html",        /* default filename if none given */
    NULL,
    NULL,
    NULL,
    NULL,
    0,
    0,
    0,
    0,
    0,
    0,
    LWSMPRO_FILE,	/* mount type is a directory in a filesystem */
    1,                  /* strlen("/"), ie length of the mountpoint */
};

static void check_timer(void *void_http) {
    qd_http_t *h = (qd_http_t*)void_http;
    sys_mutex_lock(h->lock);
    /* Run LWS global timer and forced-service checks. */
    lws_service_fd(h->context, NULL);
    while (!lws_service_adjust_timeout(h->context, 1, 0)) {
        /* -1 timeout means just do forced service */
        lws_plat_service_tsi(h->context, -1, 0);
    }
    if (!h->timer) {
        h->timer = qd_timer(h->dispatch, check_timer, h);
    }
    qd_timer_cancel(h->timer);
    qd_timer_schedule(h->timer, 1000); /* LWS wants per-second wakeups */
    sys_mutex_unlock(h->lock);
}

void qd_http_connector_process(qdpn_connector_t *c) {
    qd_http_t * h = qdpn_listener_http(qdpn_connector_listener(c));
    sys_mutex_lock(h->lock);
    int fd = qdpn_connector_get_fd(c);
    struct lws *wsi = (struct lws*)qdpn_connector_http(c);
    /* Make sure we are still tracking this fd, could have been closed by timer */
    if (wsi) {
        pn_transport_t *t = qdpn_connector_transport(c);
        int flags =
            (qdpn_connector_activated(c, QDPN_CONNECTOR_READABLE) ? POLLIN : 0) |
            (qdpn_connector_activated(c, QDPN_CONNECTOR_WRITABLE) ? POLLOUT : 0);
        struct lws_pollfd pfd = { fd, flags, flags };
        if (pn_transport_pending(t) > 0) {
            lws_callback_on_writable(wsi);
        }
        lws_service_fd(h->context, &pfd);
        if (pn_transport_closed(t)) {
            mark_closed(wsi);   /* Don't let the server close the FD. */
        } else {
            if (pn_transport_capacity(t) > 0)
                qdpn_connector_activate(c, QDPN_CONNECTOR_READABLE);
            if (pn_transport_pending(t) > 0 || lws_partial_buffered(wsi))
                qdpn_connector_activate(c, QDPN_CONNECTOR_WRITABLE);
            qdpn_connector_wakeup(c, pn_transport_tick(t, qdpn_now(NULL)));
        }
    }
    sys_mutex_unlock(h->lock);
    check_timer(h);             /* Make sure the timer is running */
}

qd_http_connector_t *qd_http_connector(qd_http_t *h, qdpn_connector_t *c) {
    if (set_fd(h, qdpn_connector_get_fd(c), c)) {
        return NULL;
    }
    struct lws* wsi = lws_adopt_socket(h->context, qdpn_connector_get_fd(c));
    return (qd_http_connector_t*)wsi;
}

static struct lws_protocols protocols[] = {
    /* HTTP only protocol comes first */
    {
        "http-only",
        callback_http,
        0,
    },
     /* "amqp" is the official oasis AMQP over WebSocket protocol name */
    {
        "amqp",
        callback_amqpws,
        sizeof(buffer_t),
    },
    /* "binary" is an alias for "amqp", for compatibility with clients designed
     * to work with a WebSocket proxy
     */
    {
        "binary",
        callback_amqpws,
        sizeof(buffer_t),
    },
};

qd_http_t *qd_http(qd_dispatch_t *d, qd_log_source_t *log) {
    qd_http_t *h = calloc(1, sizeof(qd_http_t));
    if (!h) return NULL;
    h->lock = sys_mutex();
    h->dispatch = d;
    h->log = log;
    lws_set_log_level(0, NULL);

    struct lws_context_creation_info info = {0};
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.gid = info.uid = -1;
    info.user = h;
    info.mounts = &console_mount; /* Serve the console files */
    info.server_string = QD_CONNECTION_PROPERTY_PRODUCT_VALUE;
    h->context = lws_create_context(&info);
    h->timer = NULL;            /* Can't init timer here, server not initialized. */
    return h;
}

void qd_http_free(qd_http_t *h) {
    sys_mutex_free(h->lock);
    if (h->timer) qd_timer_free(h->timer);
    lws_context_destroy(h->context);
    free(h);
}

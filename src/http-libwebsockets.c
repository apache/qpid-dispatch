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
#include <qpid/dispatch/threading.h>
#include <qpid/dispatch/timer.h>

#include <libwebsockets.h>

#include <assert.h>
#include <errno.h>

#include "http.h"
#include "config.h"

typedef struct fd_data_t {
    qdpn_connector_t *connector;
    struct lws *wsi;
} fd_data_t;

struct qd_http_listener_t {
    sys_mutex_t *lock;
    qd_dispatch_t *dispatch;
    struct lws_context *context;
    qd_timer_t *timer;
    fd_data_t *fd;             /* indexed by file descriptor */
    size_t fd_len;
};

static inline fd_data_t *fd_data(qd_http_listener_t *hl, int fd) {
    fd_data_t *d = (fd < hl->fd_len) ? &hl->fd[fd] : NULL;
    return (d && d->connector && d->wsi) ? d : NULL;
}

static inline qdpn_connector_t *fd_connector(qd_http_listener_t *hl, int fd) {
    fd_data_t *d = fd_data(hl, fd);
    return d ? d->connector : NULL;
}

static inline qd_http_listener_t *wsi_http_listener(struct lws *wsi) {
    return (qd_http_listener_t*)lws_context_user(lws_get_context(wsi));
}

static inline qdpn_connector_t *wsi_connector(struct lws *wsi) {
    qd_http_listener_t *hl = wsi_http_listener(wsi);
    return fd_connector(hl, lws_get_socket_fd(wsi));
}

static inline fd_data_t *set_fd(qd_http_listener_t *hl, int fd, qdpn_connector_t *c, struct lws *wsi) {
    if (fd >= hl->fd_len) {
        size_t len = hl->fd_len;
        hl->fd_len = (fd+1)*2;
        hl->fd = realloc(hl->fd, hl->fd_len*sizeof(*hl->fd));
        if (!hl->fd) return NULL;
        memset(hl->fd + len, 0, sizeof(*hl->fd)*(hl->fd_len - len));
    }
    fd_data_t *d = &hl->fd[fd];
    d->connector = c;
    d->wsi = wsi;
    return d;
}

/* Mark the qd connector closed, but leave the FD for LWS to clean up */
int mark_closed(struct lws *wsi) {
    qd_http_listener_t *hl = wsi_http_listener(wsi);
    fd_data_t *d = fd_data(hl, lws_get_socket_fd(wsi));
    if (d) {
        qdpn_connector_mark_closed(d->connector);
        memset(d, 0, sizeof(*d));
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

static inline int normal_close(struct lws *wsi, const char *msg) {
    lws_close_reason(wsi, LWS_CLOSE_STATUS_NORMAL, (unsigned char*)msg, strlen(msg));
    return -1;
}

static inline int unexpected_close(struct lws *wsi, const char *msg) {
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
    qdpn_connector_t *c = wsi_connector(wsi);
    pn_transport_t *t = c ? qdpn_connector_transport(c) : NULL;

    switch (reason) {

    case LWS_CALLBACK_ESTABLISHED: {
        memset(user, 0, sizeof(buffer_t));
        break;
    }

    case LWS_CALLBACK_SERVER_WRITEABLE: {
        ssize_t size;
        if (!t || (size = pn_transport_pending(t)) < 0) {
            return normal_close(wsi, "write-closed");
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
                return unexpected_close(wsi, "out-of-memory");
            }
            void *tmpstart = wtmp->start + LWS_PRE;
            memcpy(tmpstart, start, size);
            ssize_t wrote = lws_write(wsi, tmpstart, size, LWS_WRITE_BINARY);
            if (wrote < 0) {
                pn_transport_close_head(t);
                return normal_close(wsi, "write-error");
            } else {
                pn_transport_pop(t, (size_t)wrote);
            }
        }
        break;
    }

    case LWS_CALLBACK_RECEIVE: {
        if (!t || pn_transport_capacity(t) < 0) {
            return normal_close(wsi, "read-closed");
        }
        if (transport_push(t, pn_bytes(len, in))) {
            return unexpected_close(wsi, "read-overflow");
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

static void check_timer(void *void_http_listener) {
    qd_http_listener_t *hl = (qd_http_listener_t*)void_http_listener;
    sys_mutex_lock(hl->lock);
    /* Run LWS global timer and forced-service checks. */
    lws_service_fd(hl->context, NULL);
    while (!lws_service_adjust_timeout(hl->context, 1, 0)) {
        /* -1 timeout means just do forced service */
        lws_plat_service_tsi(hl->context, -1, 0);
    }
    if (!hl->timer) {
        hl->timer = qd_timer(hl->dispatch, check_timer, hl);
    }
    qd_timer_cancel(hl->timer);
    qd_timer_schedule(hl->timer, 1000); /* LWS wants per-second wakeups */
    sys_mutex_unlock(hl->lock);
}

void qd_http_connector_process(qdpn_connector_t *c) {
    qd_http_listener_t * hl = qdpn_listener_http(qdpn_connector_listener(c));
    sys_mutex_lock(hl->lock);
    int fd = qdpn_connector_get_fd(c);
    fd_data_t *d = fd_data(hl, fd);
    /* Make sure we are still tracking this fd, could have been closed by timer */
    if (d) {
        pn_transport_t *t = qdpn_connector_transport(c);
        int flags =
            (qdpn_connector_activated(c, QDPN_CONNECTOR_READABLE) ? POLLIN : 0) |
            (qdpn_connector_activated(c, QDPN_CONNECTOR_WRITABLE) ? POLLOUT : 0);
        struct lws_pollfd pfd = { fd, flags, flags };
        if (pn_transport_pending(t) > 0) {
            lws_callback_on_writable(d->wsi);
        }
        lws_service_fd(hl->context, &pfd);
        d = fd_data(hl, fd);    /* We may have stopped tracking during service */
        if (pn_transport_closed(t)) {
            if (d) mark_closed(d->wsi);   /* Don't let the server close the FD. */
        } else {
            if (pn_transport_capacity(t) > 0)
                qdpn_connector_activate(c, QDPN_CONNECTOR_READABLE);
            if (pn_transport_pending(t) > 0 || (d && lws_partial_buffered(d->wsi)))
                qdpn_connector_activate(c, QDPN_CONNECTOR_WRITABLE);
            qdpn_connector_wakeup(c, pn_transport_tick(t, qdpn_now(NULL)));
        }
    }
    sys_mutex_unlock(hl->lock);
    check_timer(hl);             /* Make sure the timer is running */
}

void qd_http_listener_accept(qd_http_listener_t *hl, qdpn_connector_t *c) {
    struct lws* wsi = lws_adopt_socket(hl->context, qdpn_connector_get_fd(c));
    if (!wsi || !set_fd(hl, qdpn_connector_get_fd(c), c, wsi)) {
        pn_transport_t *t = qdpn_connector_transport(c);
        pn_condition_t *cond = pn_transport_condition(t);
        pn_condition_format(cond, "qpid-dispatch-router",
                           "Cannot enable HTTP support for %s", qdpn_connector_name(c));
        pn_transport_close_tail(t);
        pn_transport_close_head(t);
    }
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

/* FIXME aconway 2016-12-07: LWS context per listener, could reduce to per-ssl-profile */
qd_http_listener_t *qd_http_listener(qd_dispatch_t *d, const qd_server_config_t *config) {
    qd_http_listener_t *hl = calloc(1, sizeof(*hl));
    if (!hl) return NULL;
    hl->lock = sys_mutex();
    hl->dispatch = d;
    lws_set_log_level(0, NULL);

    struct lws_context_creation_info info = {0};
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.gid = info.uid = -1;
    info.user = hl;
    info.mounts = &console_mount; /* Serve the console files */
    info.server_string = QD_CONNECTION_PROPERTY_PRODUCT_VALUE;
    hl->context = lws_create_context(&info);

    /* FIXME aconway 2016-12-07: could use a single timer for all. */
    hl->timer = NULL;            /* Can't init timer here, server not initialized. */
    return hl;
}

void qd_http_listener_free(qd_http_listener_t *hl) {
    sys_mutex_free(hl->lock);
    if (hl->timer) qd_timer_free(hl->timer);
    lws_context_destroy(hl->context);
    free(hl);
}

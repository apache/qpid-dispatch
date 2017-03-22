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
#include <inttypes.h>

#include "http.h"
#include "server_private.h"
#include "config.h"

static qd_log_source_t* http_log;

static const char *CIPHER_LIST = "ALL:aNULL:!eNULL:@STRENGTH";

/* Associate file-descriptors, LWS instances and qdpn_connectors */
typedef struct fd_data_t {
    qdpn_connector_t *connector;
    struct lws *wsi;
} fd_data_t;

/* HTTP server state shared by all listeners  */
struct qd_http_server_t {
    qd_dispatch_t *dispatch;
    qd_log_source_t *log;
    sys_mutex_t *lock;         /* For now use LWS as a thread-unsafe library. */
    struct lws_context *context;
    qd_timer_t *timer;
    int vhost_id;               /* unique identifier for vhost name */
    fd_data_t *fd;              /* indexed by file descriptor */
    size_t fd_len;
};

/* Per-HTTP-listener */
struct qd_http_listener_t {
    qd_http_server_t *server;
    struct lws_vhost *vhost;
    struct lws_http_mount mount;
    char name[256];             /* vhost name */
};

/* Get wsi/connector associated with fd or NULL if nothing on record. */
static inline fd_data_t *fd_data(qd_http_server_t *s, int fd) {
    fd_data_t *d = (fd < s->fd_len) ? &s->fd[fd] : NULL;
    return (d && (d->connector || d->wsi)) ? d : NULL;
}

static inline qd_http_server_t *wsi_http_server(struct lws *wsi) {
    return (qd_http_server_t*)lws_context_user(lws_get_context(wsi));
}

static inline qdpn_connector_t *wsi_connector(struct lws *wsi) {
    fd_data_t *d = fd_data(wsi_http_server(wsi), lws_get_socket_fd(wsi));
    return d ? d->connector : NULL;
}

static inline fd_data_t *set_fd(qd_http_server_t *s, int fd, qdpn_connector_t *c, struct lws *wsi) {
    if (!s->fd || fd >= s->fd_len) {
        size_t oldlen = s->fd_len;
        s->fd_len = fd + 16;    /* Don't double, low-range FDs will be re-used first. */
        void *newfds = realloc(s->fd, s->fd_len*sizeof(*s->fd));
        if (!newfds) return NULL;
        s->fd = newfds;
        memset(s->fd + oldlen, 0, sizeof(*s->fd)*(s->fd_len - oldlen));
    }
    fd_data_t *d = &s->fd[fd];
    d->connector = c;
    d->wsi = wsi;
    return d;
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

    case LWS_CALLBACK_HTTP:     /* Called if file mount can't find the file */
        lws_return_http_status(wsi, HTTP_STATUS_NOT_FOUND, (char*)in);
        return -1;

    case LWS_CALLBACK_ADD_POLL_FD: {
        /* Record WSI against FD here, the connector will be recorded when lws_service returns. */
        set_fd(wsi_http_server(wsi), lws_get_socket_fd(wsi), 0, wsi);
        break;
    }
    case LWS_CALLBACK_DEL_POLL_FD: {
        fd_data_t *d = fd_data(wsi_http_server(wsi), lws_get_socket_fd(wsi));
        if (d) {
            /* Tell dispatch to forget this FD, but let LWS do the actual close() */
            if (d->connector) qdpn_connector_mark_closed(d->connector);
            memset(d, 0, sizeof(*d));
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

    /* NOTE: Not using LWS_CALLBACK_LOCK/UNLOCK_POLL as we are serializing all HTTP work for now. */

    default:
        break;
    }

    return 0;
}

/* Buffer to allocate extra header space required by LWS.  */
typedef struct buffer_t { char *start; size_t size; size_t cap; } buffer_t;

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
        qd_log(wsi_http_server(wsi)->log, QD_LOG_DEBUG,
               "Upgraded incoming HTTP connection from  %s[%"PRIu64"] to AMQP over WebSocket",
               qdpn_connector_name(c),
               qd_connection_connection_id((qd_connection_t*)qdpn_connector_context(c)));
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
        if (t) {
            pn_transport_close_tail(t);
        }

    case LWS_CALLBACK_CLOSED:
        break;

    default:
        break;
    }
    return 0;
}

static void check_timer(void *void_http_server) {
    qd_http_server_t *s = (qd_http_server_t*)void_http_server;
    /* Run LWS global timer and forced-service checks. */
    sys_mutex_lock(s->lock);
    lws_service_fd(s->context, NULL);
    while (!lws_service_adjust_timeout(s->context, 1, 0)) {
        /* -1 timeout means just do forced service */
        lws_plat_service_tsi(s->context, -1, 0);
    }
    if (!s->timer) {
        s->timer = qd_timer(s->dispatch, check_timer, s);
    }
    sys_mutex_unlock(s->lock);
    /* Timer is locked using server lock. */
    qd_timer_cancel(s->timer);
    qd_timer_schedule(s->timer, 1000); /* LWS wants per-second wakeups */
}

static qd_http_listener_t * qdpn_connector_http_listener(qdpn_connector_t* c) {
    qd_listener_t* ql = (qd_listener_t*)qdpn_listener_context(qdpn_connector_listener(c));
    return qd_listener_http(ql);
}

static void http_connector_process(qdpn_connector_t *c) {
    qd_http_listener_t *hl = qdpn_connector_http_listener(c);
    qd_http_server_t *s = hl->server;
    sys_mutex_lock(s->lock);
    int fd = qdpn_connector_get_fd(c);
    fd_data_t *d = fd_data(s, fd);
    /* Make sure we are still tracking this fd, could have been closed by timer */
    if (d) {
        pn_transport_t *t = qdpn_connector_transport(c);
        int flags =
            (qdpn_connector_hangup(c) ? POLLHUP : 0) |
            (qdpn_connector_activated(c, QDPN_CONNECTOR_READABLE) ? POLLIN : 0) |
            (qdpn_connector_activated(c, QDPN_CONNECTOR_WRITABLE) ? POLLOUT : 0);
        struct lws_pollfd pfd = { fd, flags, flags };
        if (pn_transport_pending(t) > 0) {
            lws_callback_on_writable(d->wsi);
        }
        lws_service_fd(s->context, &pfd);
        d = fd_data(s, fd);    /* We may have stopped tracking during service */
        if (pn_transport_capacity(t) > 0)
            qdpn_connector_activate(c, QDPN_CONNECTOR_READABLE);
        if (pn_transport_pending(t) > 0 || (d && lws_partial_buffered(d->wsi)))
            qdpn_connector_activate(c, QDPN_CONNECTOR_WRITABLE);
        pn_timestamp_t wake = pn_transport_tick(t, qdpn_now(NULL));
        if (wake) qdpn_connector_wakeup(c, wake);
    }
    sys_mutex_unlock(s->lock);
    check_timer(s);             /* Make sure the timer is running */
}

/* Dispatch closes a connector because it is HUP, socket_error or transport_closed()  */
static void http_connector_close(qdpn_connector_t *c) {
    int fd = qdpn_connector_get_fd(c);
    qd_http_server_t *s = qdpn_connector_http_listener(c)->server;
    sys_mutex_lock(s->lock);
    fd_data_t *d = fd_data(s, fd);
    if (d) {                    /* Only if we are still tracking fd */
        /* Shutdown but let LWS do the close(),  possibly in later timer */
        shutdown(qdpn_connector_get_fd(c), SHUT_RDWR);
        short flags = POLLIN|POLLOUT|POLLHUP;
        struct lws_pollfd pfd = { qdpn_connector_get_fd(c), flags, flags };
        lws_service_fd(s->context, &pfd);
        qdpn_connector_mark_closed(c);
        memset(d, 0 , sizeof(*d));
    }
    sys_mutex_unlock(s->lock);
}

static struct qdpn_connector_methods_t http_methods = {
    http_connector_process,
    http_connector_close
};

void qd_http_listener_accept(qd_http_listener_t *hl, qdpn_connector_t *c) {
    qd_http_server_t *s = hl->server;
    sys_mutex_lock(s->lock);
    int fd = qdpn_connector_get_fd(c);
    struct lws *wsi = lws_adopt_socket_vhost(hl->vhost, fd);
    fd_data_t *d = fd_data(s, fd);
    if (d) {          /* FD was adopted by LWS, so dispatch must not close it */
        qdpn_connector_set_methods(c, &http_methods);
        if (wsi) d->connector = c;
    }
    sys_mutex_unlock(s->lock);
    if (!wsi) {       /* accept failed, dispatch should forget the FD. */
        qdpn_connector_mark_closed(c);
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
    { NULL, NULL, 0, 0 } /* terminator */
};

static qd_log_level_t qd_level(int lll) {
    switch (lll) {
    case LLL_ERR: return QD_LOG_ERROR;
    case LLL_WARN: return QD_LOG_WARNING;
    case LLL_NOTICE: return QD_LOG_INFO;
    case LLL_INFO:return QD_LOG_DEBUG;
    case LLL_DEBUG: return QD_LOG_TRACE;
    default: return QD_LOG_NONE;
    }
}

static void emit_lws_log(int lll, const char *line)  {
    size_t  len = strlen(line);
    while (len > 1 && isspace(line[len-1]))
        --len;
    qd_log(http_log, qd_level(lll), "%.*s", len, line);
}

qd_http_server_t *qd_http_server(qd_dispatch_t *d, qd_log_source_t *log) {
    if (!http_log) http_log = qd_log_source("HTTP");
    qd_http_server_t *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->log = log;
    s->lock = sys_mutex();
    s->dispatch = d;
    int levels =
        (qd_log_enabled(log, QD_LOG_ERROR) ? LLL_ERR : 0) |
        (qd_log_enabled(log, QD_LOG_WARNING) ? LLL_WARN : 0) |
        (qd_log_enabled(log, QD_LOG_INFO) ? LLL_NOTICE : 0) |
        (qd_log_enabled(log, QD_LOG_DEBUG) ? LLL_INFO : 0) |
        (qd_log_enabled(log, QD_LOG_TRACE) ? LLL_DEBUG : 0);
    lws_set_log_level(levels, emit_lws_log);

    struct lws_context_creation_info info = {0};
    info.gid = info.uid = -1;
    info.user = s;
    info.server_string = QD_CONNECTION_PROPERTY_PRODUCT_VALUE;
    info.options = LWS_SERVER_OPTION_EXPLICIT_VHOSTS |
        LWS_SERVER_OPTION_SKIP_SERVER_CANONICAL_NAME |
        LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    info.max_http_header_pool = 32;
    info.timeout_secs = 1;
    s->context = lws_create_context(&info);
    if (!s->context) {
        free(s);
        return NULL;
    }
    return s;
}

void qd_http_server_free(qd_http_server_t *s) {
    sys_mutex_free(s->lock);
    lws_context_destroy(s->context);
    if (s->timer) qd_timer_free(s->timer);
    if (s->fd) free(s->fd);
    free(s);
}

qd_http_listener_t *qd_http_listener(qd_http_server_t *s, const qd_server_config_t *config) {
    qd_http_listener_t *hl = calloc(1, sizeof(*hl));
    if (!hl) return NULL;
    hl->server = s;

    struct lws_context_creation_info info = {0};

    struct lws_http_mount *m = &hl->mount;
    m->mountpoint = "/";    /* URL mount point */
    m->mountpoint_len = strlen(m->mountpoint); /* length of the mountpoint */
    m->origin = (config->http_root && *config->http_root) ? /* File system root */
        config->http_root : QPID_CONSOLE_STAND_ALONE_INSTALL_DIR;
    m->def = "index.html";  /* Default file name */
    m->origin_protocol = LWSMPRO_FILE; /* mount type is a directory in a filesystem */
    info.mounts = m;
    info.port = CONTEXT_PORT_NO_LISTEN_SERVER; /* Don't use LWS listener */
    info.protocols = protocols;
    info.keepalive_timeout = 1;
    info.ssl_cipher_list = CIPHER_LIST;
    info.options |= LWS_SERVER_OPTION_VALIDATE_UTF8;
    if (config->ssl_profile) {
        info.ssl_cert_filepath = config->ssl_certificate_file;
        info.ssl_private_key_filepath = config->ssl_private_key_file;
        info.ssl_private_key_password = config->ssl_password;
        info.ssl_ca_filepath = config->ssl_trusted_certificates;
        info.options |=
            LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT |
            (config->ssl_required ? 0 : LWS_SERVER_OPTION_ALLOW_NON_SSL_ON_SSL_PORT) |
            (config->requireAuthentication ? LWS_SERVER_OPTION_REQUIRE_VALID_OPENSSL_CLIENT_CERT : 0);
    }
    snprintf(hl->name, sizeof(hl->name), "vhost%x", s->vhost_id++);
    info.vhost_name = hl->name;
    hl->vhost = lws_create_vhost(s->context, &info);
    if (!hl->vhost) {
        free(hl);
        return NULL;
    }
    return hl;
}

void qd_http_listener_free(qd_http_listener_t *hl) {
    free(hl);
}

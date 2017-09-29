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
#include <qpid/dispatch/threading.h>
#include <qpid/dispatch/timer.h>

#include <proton/connection_driver.h>

#include <libwebsockets.h>

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>

#include "http.h"
#include "server_private.h"
#include "config.h"

static const char *CIPHER_LIST = "ALL:aNULL:!eNULL:@STRENGTH"; /* Default */

/* Log for LWS messages. For dispatch server messages use qd_http_server_t::log */
static qd_log_source_t* http_log;

static qd_log_level_t qd_level(int lll) {
    switch (lll) {
    case LLL_ERR: return QD_LOG_ERROR;
    case LLL_WARN: return QD_LOG_WARNING;
    /* LWS is noisy compared to dispatch on the informative levels, downgrade */
    case LLL_NOTICE: return QD_LOG_DEBUG;
    default: return QD_LOG_TRACE; /* Everything else to trace  */
    }
}

static void logger(int lll, const char *line)  {
    size_t  len = strlen(line);
    while (len > 1 && isspace(line[len-1])) { /* Strip trailing newline */
        --len;
    }
    qd_log(http_log, qd_level(lll), "%.*s", len, line);
}

static void log_init() {
    http_log = qd_log_source("HTTP");
    int levels = 0;
    for (int i = 0; i < LLL_COUNT; ++i) {
        int lll = 1<<i;
        levels |= qd_log_enabled(http_log, qd_level(lll)) ? lll : 0;
    }
    lws_set_log_level(levels, logger);
}

/* Intermediate write buffer: LWS needs extra header space on write.  */
typedef struct buffer_t {
    char *start;
    size_t size, cap;
} buffer_t;

/* Ensure size bytes in buffer, make buf empty if alloc fails */
static void buffer_set_size(buffer_t *buf, size_t size) {
    if (size > buf->cap) {
        buf->cap = (size > buf->cap * 2) ? size : buf->cap * 2;
        buf->start = realloc(buf->start, buf->cap);
    }
    if (buf->start) {
        buf->size = size;
    } else {
        buf->size = buf->cap = 0;
    }
}

/* AMQPWS connection: set as lws user data and qd_conn->context */
typedef struct connection_t {
    pn_connection_driver_t driver;
    qd_connection_t* qd_conn;
    buffer_t wbuf;   /* LWS requires allocated header space at start of buffer */
    struct lws *wsi;
} connection_t;

/* Navigating from WSI pointer to qd objects */
static qd_http_server_t *wsi_server(struct lws *wsi);
static qd_http_listener_t *wsi_listener(struct lws *wsi);
static qd_log_source_t *wsi_log(struct lws *wsi);


/* Declare LWS callbacks and protocol list */
static int callback_http(struct lws *wsi, enum lws_callback_reasons reason,
                         void *user, void *in, size_t len);
static int callback_amqpws(struct lws *wsi, enum lws_callback_reasons reason,
                           void *user, void *in, size_t len);

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
        sizeof(connection_t),
    },
    /* "binary" is an alias for "amqp", for compatibility with clients designed
     * to work with a WebSocket proxy
     */
    {
        "binary",
        callback_amqpws,
        sizeof(connection_t),
    },
    { NULL, NULL, 0, 0 } /* terminator */
};


static inline int unexpected_close(struct lws *wsi, const char *msg) {
    lws_close_reason(wsi, LWS_CLOSE_STATUS_UNEXPECTED_CONDITION,
                     (unsigned char*)msg, strlen(msg));
    char peer[64];
    lws_get_peer_simple(wsi, peer, sizeof(peer));
    qd_log(wsi_log(wsi), QD_LOG_ERROR, "Error on HTTP connection from %s: %s", peer, msg);
    return -1;
}

static int handle_events(connection_t* c) {
    if (!c->qd_conn) {
        return unexpected_close(c->wsi, "not-established");
    }
    pn_event_t *e;
    while ((e = pn_connection_driver_next_event(&c->driver))) {
        qd_connection_handle(c->qd_conn, e);
    }
    if (pn_connection_driver_write_buffer(&c->driver).size) {
        lws_callback_on_writable(c->wsi);
    }
    if (pn_connection_driver_finished(&c->driver)) {
        lws_close_reason(c->wsi, LWS_CLOSE_STATUS_NORMAL, NULL, 0);
        return -1;
    }
    return 0;
}

/* The server has a bounded, thread-safe queue for external work */
typedef struct work_t {
    enum { W_NONE, W_LISTEN, W_CLOSE, W_WAKE, W_STOP } type;
    void *value;
} work_t;

#define WORK_MAX 8              /* Just decouple threads, not a big buffer */

typedef struct work_queue_t {
    sys_mutex_t *lock;
    sys_cond_t *cond;
    work_t work[WORK_MAX];
    size_t head, len;          /* Ring buffer */
} work_queue_t;

/* HTTP Server runs in a single thread, communication from other threads via work_queue */
struct qd_http_server_t {
    qd_server_t *server;
    sys_thread_t *thread;
    work_queue_t work;
    qd_log_source_t *log;
    struct lws_context *context;
    pn_timestamp_t now;         /* Cache current time in thread_run */
    pn_timestamp_t next_tick;   /* Next requested tick service */
};

static void work_queue_destroy(work_queue_t *wq) {
    if (wq->lock) sys_mutex_free(wq->lock);
    if (wq->cond) sys_cond_free(wq->cond);
}

static void work_queue_init(work_queue_t *wq) {
    wq->lock = sys_mutex();
    wq->cond = sys_cond();
}

 /* Block till there is space */
static void work_push(qd_http_server_t *hs, work_t w) {
    work_queue_t *wq = &hs->work;
    sys_mutex_lock(wq->lock);
    while (wq->len == WORK_MAX) {
        lws_cancel_service(hs->context); /* Wake up the run thread to clear space */
        sys_cond_wait(wq->cond, wq->lock);
    }
    wq->work[(wq->head + wq->len) % WORK_MAX] = w;
    ++wq->len;
    sys_mutex_unlock(wq->lock);
    lws_cancel_service(hs->context); /* Wake up the run thread to handle my work */
}

/* Non-blocking, return { W_NONE, NULL } if empty */
static work_t work_pop(qd_http_server_t *hs) {
    work_t w = { W_NONE, NULL };
    work_queue_t *wq = &hs->work;
    sys_mutex_lock(wq->lock);
    if (wq->len > 0) {
        w = wq->work[wq->head];
        wq->head = (wq->head + 1) % WORK_MAX;
        --wq->len;
        sys_cond_signal(wq->cond);
    }
    sys_mutex_unlock(wq->lock);
    return w;
}

/* Each qd_http_listener_t is associated with an lws_vhost */
struct qd_http_listener_t {
    qd_listener_t *listener;
    qd_http_server_t *server;
    struct lws_vhost *vhost;
    struct lws_http_mount mount;
};

void qd_http_listener_free(qd_http_listener_t *hl) {
    if (!hl) return;
    if (hl->listener) {
        hl->listener->http = NULL;
        qd_listener_decref(hl->listener);
    }
    free(hl);
}

static qd_http_listener_t *qd_http_listener(qd_http_server_t *hs, qd_listener_t *li) {
    qd_http_listener_t *hl = calloc(1, sizeof(*hl));
    if (hl) {
        hl->server = hs;
        hl->listener = li;
        li->http = hl;
        sys_atomic_inc(&li->ref_count); /* Keep it around till qd_http_server_free() */
    } else {
        qd_log(hs->log, QD_LOG_CRITICAL, "No memory for HTTP listen on %s",
               li->config.host_port);
    }
    return hl;
}

static void listener_start(qd_http_listener_t *hl, qd_http_server_t *hs) {
    log_init();                 /* Update log flags at each listener */

    qd_server_config_t *config = &hl->listener->config;

    int port = qd_port_int(config->port);
    if (port < 0) {
        qd_log(hs->log, QD_LOG_ERROR, "HTTP listener %s has invalid port %s",
               config->host_port, config->port);
        goto error;
    }
    struct lws_http_mount *m = &hl->mount;
    m->mountpoint = "/";    /* URL mount point */
    m->mountpoint_len = strlen(m->mountpoint); /* length of the mountpoint */
    m->origin = (config->http_root && *config->http_root) ? /* File system root */
        config->http_root : QPID_CONSOLE_STAND_ALONE_INSTALL_DIR;
    m->def = "index.html";  /* Default file name */
    m->origin_protocol = LWSMPRO_FILE; /* mount type is a directory in a filesystem */

    struct lws_context_creation_info info = {0};
    info.mounts = m;
    info.port = port;
    info.protocols = protocols;
    info.keepalive_timeout = 1;
    info.ssl_cipher_list = CIPHER_LIST;
    info.options |= LWS_SERVER_OPTION_VALIDATE_UTF8;
    if (config->ssl_profile) {
        info.ssl_cert_filepath = config->ssl_certificate_file;
        info.ssl_private_key_filepath = config->ssl_private_key_file;
        info.ssl_private_key_password = config->ssl_password;
        info.ssl_ca_filepath = config->ssl_trusted_certificates;
        info.ssl_cipher_list = config->ciphers;

        info.options |=
            LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT |
            (config->ssl_required ? 0 : LWS_SERVER_OPTION_ALLOW_NON_SSL_ON_SSL_PORT) |
            (config->requireAuthentication ? LWS_SERVER_OPTION_REQUIRE_VALID_OPENSSL_CLIENT_CERT : 0);
    }
    info.vhost_name = hl->listener->config.host_port;
    hl->vhost = lws_create_vhost(hs->context, &info);
    if (hl->vhost) {
        /* Store hl pointer in vhost */
        void *vp = lws_protocol_vh_priv_zalloc(hl->vhost, &protocols[0], sizeof(hl));
        memcpy(vp, &hl, sizeof(hl));
        qd_log(hs->log, QD_LOG_NOTICE, "Listening for HTTP on %s", config->host_port);
        return;
    } else {
        qd_log(hs->log, QD_LOG_NOTICE, "Error listening for HTTP on %s", config->host_port);
        goto error;
    }
    return;

  error:
    if (hl->listener->exit_on_error) {
        qd_log(hs->log, QD_LOG_CRITICAL, "Shutting down, required listener failed %s",
               config->host_port);
        exit(1);
    }
    qd_http_listener_free(hl);
}

static void listener_close(qd_http_listener_t *hl, qd_http_server_t *hs) {
    /* TODO aconway 2017-04-13: can't easily stop listeners under libwebsockets */
    qd_log(hs->log, QD_LOG_ERROR, "Cannot close HTTP listener %s",
           hl->listener->config.host_port);
}

/*
 * LWS callback for un-promoted HTTP connections.
 * Note main HTTP file serving is handled by the "mount" struct below.
 */
static int callback_http(struct lws *wsi, enum lws_callback_reasons reason,
                         void *user, void *in, size_t len)
{
    switch (reason) {

    case LWS_CALLBACK_PROTOCOL_DESTROY:
        qd_http_listener_free(wsi_listener(wsi));
        return -1;

    case LWS_CALLBACK_HTTP: {
        /* Called if file mount can't find the file */
        lws_return_http_status(wsi, HTTP_STATUS_NOT_FOUND, (char*)in);
        return -1;
    }

    default:
        return 0;
    }
}

/* Wake up a connection managed by the http server thread */
static void connection_wake(qd_connection_t *qd_conn)
{
    connection_t *c = qd_conn->context;
    if (c && qd_conn->listener->http) {
        qd_http_server_t *hs = qd_conn->listener->http->server;
        work_t w = { W_WAKE, c };
        work_push(hs, w);
    }
}

/* Callbacks for promoted AMQP over WS connections. */
static int callback_amqpws(struct lws *wsi, enum lws_callback_reasons reason,
                           void *user, void *in, size_t len)
{
    qd_http_server_t *hs = wsi_server(wsi);
    connection_t *c = (connection_t*)user;

    switch (reason) {

    case LWS_CALLBACK_ESTABLISHED: {
        /* Upgrade accepted HTTP connection to AMQPWS */
        memset(c, 0, sizeof(*c));
        c->wsi = wsi;
        qd_http_listener_t *hl = wsi_listener(wsi);
        if (hl == NULL) {
            return unexpected_close(c->wsi, "cannot-upgrade");
        }
        c->qd_conn = qd_server_connection(hs->server, &hl->listener->config);
        if (c->qd_conn == NULL) {
            return unexpected_close(c->wsi, "out-of-memory");
        }
        c->qd_conn->context = c;
        c->qd_conn->wake = connection_wake;
        c->qd_conn->listener = hl->listener;
        lws_get_peer_simple(wsi, c->qd_conn->rhost, sizeof(c->qd_conn->rhost));
        int err = pn_connection_driver_init(&c->driver, c->qd_conn->pn_conn, NULL);
        if (err) {
            return unexpected_close(c->wsi, pn_code(err));
        }
        strncpy(c->qd_conn->rhost_port, c->qd_conn->rhost, sizeof(c->qd_conn->rhost_port));
        qd_log(hs->log, QD_LOG_DEBUG,
               "[%"PRIu64"] upgraded HTTP connection from %s to AMQPWS",
               qd_connection_connection_id(c->qd_conn), qd_connection_name(c->qd_conn));
        return handle_events(c);
    }

    case LWS_CALLBACK_SERVER_WRITEABLE: {
        if (handle_events(c)) return -1;
        pn_bytes_t dbuf = pn_connection_driver_write_buffer(&c->driver);
        if (dbuf.size) {
            /* lws_write() demands LWS_PRE bytes of free space before the data,
             * so we must copy from the driver's buffer to larger temporary wbuf
             */
            buffer_set_size(&c->wbuf, LWS_PRE + dbuf.size);
            if (c->wbuf.start == NULL) {
                return unexpected_close(c->wsi, "out-of-memory");
            }
            unsigned char* buf = (unsigned char*)c->wbuf.start + LWS_PRE;
            memcpy(buf, dbuf.start, dbuf.size);
            ssize_t wrote = lws_write(wsi, buf, dbuf.size, LWS_WRITE_BINARY);
            if (wrote < 0) {
                pn_connection_driver_write_close(&c->driver);
                return unexpected_close(c->wsi, "write-error");
            } else {
                pn_connection_driver_write_done(&c->driver, wrote);
            }
        }
        return handle_events(c);
    }

    case LWS_CALLBACK_RECEIVE: {
        while (len > 0) {
            if (handle_events(c)) return -1;
            pn_rwbytes_t dbuf = pn_connection_driver_read_buffer(&c->driver);
            if (dbuf.size == 0) {
                return unexpected_close(c->wsi, "unexpected-data");
            }
            size_t copy = (len < dbuf.size) ? len : dbuf.size;
            memcpy(dbuf.start, in, copy);
            pn_connection_driver_read_done(&c->driver, copy);
            len -= copy;
            in = (char*)in + copy;
        }
        return handle_events(c);
    }

    case LWS_CALLBACK_USER: {
        pn_timestamp_t next_tick = pn_transport_tick(c->driver.transport, hs->now);
        if (next_tick && next_tick > hs->now && next_tick < hs->next_tick) {
            hs->next_tick = next_tick;
        }
        return handle_events(c);
    }

    case LWS_CALLBACK_WS_PEER_INITIATED_CLOSE: {
        pn_connection_driver_read_close(&c->driver);
        return handle_events(c);
    }

    case LWS_CALLBACK_CLOSED: {
        if (c->driver.transport) {
            pn_connection_driver_close(&c->driver);
            handle_events(c);
        }
        pn_connection_driver_destroy(&c->driver);
        free(c->wbuf.start);
        return -1;
    }

    default:
        return 0;
    }
}

#define DEFAULT_TICK 1000

static void* http_thread_run(void* v) {
    qd_http_server_t *hs = v;
    qd_log(hs->log, QD_LOG_INFO, "HTTP server thread running");
    int result = 0;
    while(result >= 0) {
        /* Send a USER event to run transport ticks, may decrease hs->next_tick. */
        hs->now = qd_timer_now();
        hs->next_tick = hs->now + DEFAULT_TICK;
        lws_callback_all_protocol(hs->context, &protocols[1], LWS_CALLBACK_USER);
        lws_callback_all_protocol(hs->context, &protocols[2], LWS_CALLBACK_USER);
        pn_millis_t timeout = (hs->next_tick > hs->now) ? hs->next_tick - hs->now : 1;
        result = lws_service(hs->context, timeout);

        /* Process any work items on the queue */
        for (work_t w = work_pop(hs); w.type != W_NONE; w = work_pop(hs)) {
            switch (w.type) {
            case W_NONE:
                break;
            case W_STOP:
                result = -1;
                break;
            case W_LISTEN:
                listener_start((qd_http_listener_t*)w.value, hs);
                break;
            case W_CLOSE:
                listener_close((qd_http_listener_t*)w.value, hs);
                break;
            case W_WAKE: {
                connection_t *c = w.value;
                pn_collector_put(c->driver.collector, PN_OBJECT, c->driver.connection,
                                 PN_CONNECTION_WAKE);
                handle_events(c);
                break;
            }
            }
        }
    }
    qd_log(hs->log, QD_LOG_INFO, "HTTP server thread exit");
    return NULL;
}

void qd_http_server_free(qd_http_server_t *hs) {
    if (!hs) return;
    if (hs->thread) {
        /* Thread safe, stop via work queue then clean up */
        work_t work = { W_STOP, NULL };
        work_push(hs, work);
        sys_thread_join(hs->thread);
        sys_thread_free(hs->thread);
        hs->thread = NULL;
    }
    work_queue_destroy(&hs->work);
    if (hs->context) lws_context_destroy(hs->context);
    free(hs);
}

qd_http_server_t *qd_http_server(qd_server_t *s, qd_log_source_t *log) {
    log_init();
    qd_http_server_t *hs = calloc(1, sizeof(*hs));
    if (hs) {
        work_queue_init(&hs->work);
        struct lws_context_creation_info info = {0};
        info.gid = info.uid = -1;
        info.user = hs;
        info.server_string = QD_CONNECTION_PROPERTY_PRODUCT_VALUE;
        info.options = LWS_SERVER_OPTION_EXPLICIT_VHOSTS |
            LWS_SERVER_OPTION_SKIP_SERVER_CANONICAL_NAME |
            LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
        info.max_http_header_pool = 32;
        info.timeout_secs = 1;

        hs->context = lws_create_context(&info);
        hs->server = s;
        hs->log = log;              /* For messages from this file */
        if (!hs->context) {
            qd_log(hs->log, QD_LOG_CRITICAL, "No memory starting HTTP server");
            qd_http_server_free(hs);
            hs = NULL;
        }
    }
    return hs;
}

/* Thread safe calls that put items on work queue */

qd_http_listener_t *qd_http_server_listen(qd_http_server_t *hs, qd_listener_t *li)
{
    sys_mutex_lock(hs->work.lock);
    if (!hs->thread) {
        hs->thread = sys_thread(http_thread_run, hs);
    }
    bool ok = hs->thread;
    sys_mutex_unlock(hs->work.lock);
    if (!ok) return NULL;

    qd_http_listener_t *hl = qd_http_listener(hs, li);
    if (hl) {
        work_t w = { W_LISTEN, hl };
        work_push(hs, w);
    }
    return hl;
}

void qd_http_listener_close(qd_http_listener_t *hl)
{
    work_t w = { W_CLOSE, hl };
    work_push(hl->server, w);
}

static qd_http_server_t *wsi_server(struct lws *wsi) {
    return (qd_http_server_t*)lws_context_user(lws_get_context(wsi));
}

static qd_http_listener_t *wsi_listener(struct lws *wsi) {
    qd_http_listener_t *hl = NULL;
    struct lws_vhost *vhost = lws_get_vhost(wsi);
    if (vhost) {                /* Get qd_http_listener from vhost data */
        void *vp = lws_protocol_vh_priv_get(vhost, &protocols[0]);
        memcpy(&hl, vp, sizeof(hl));
    }
    return hl;
}

static qd_log_source_t *wsi_log(struct lws *wsi) {
    return wsi_server(wsi)->log;
}

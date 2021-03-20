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
#include <qpid/dispatch/protocol_adaptor.h>
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
static const char *IGNORED = "ignore-this-log-message";

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
    if (strstr(line, IGNORED)) return;
    size_t  len = strlen(line);
    while (len > 1 && isspace(line[len-1])) { /* Strip trailing newline */
        --len;
    }
    qd_log(http_log, qd_level(lll), "%.*s", (int)len, line);
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

typedef struct stats_request_state_t {
    bool callback_completed;
    bool wsi_deleted;
    qdr_global_stats_t stats;
    qd_http_server_t *server;
    struct lws *wsi;
} stats_request_state_t;

typedef struct stats_t {
    size_t current;
    bool headers_sent;
    stats_request_state_t *context;
} stats_t;

/* Navigating from WSI pointer to qd objects */
static qd_http_server_t *wsi_server(struct lws *wsi);
static qd_lws_listener_t *wsi_listener(struct lws *wsi);
static qd_log_source_t *wsi_log(struct lws *wsi);


/* Declare LWS callbacks and protocol list */
static int callback_http(struct lws *wsi, enum lws_callback_reasons reason,
                         void *user, void *in, size_t len);
static int callback_amqpws(struct lws *wsi, enum lws_callback_reasons reason,
                           void *user, void *in, size_t len);
static int callback_metrics(struct lws *wsi, enum lws_callback_reasons reason,
                               void *user, void *in, size_t len);
static int callback_healthz(struct lws *wsi, enum lws_callback_reasons reason,
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
    {
        "http",
        callback_metrics,
        sizeof(stats_t),
    },
    {
        "healthz",
        callback_healthz,
        sizeof(stats_t),
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
        if (c->qd_conn && e) {
            if (!qd_connection_handle(c->qd_conn, e)) {
                c->qd_conn = 0;  // connection closed
            }
        }
    }
    if (pn_connection_driver_write_buffer(&c->driver).size) {
        lws_callback_on_writable(c->wsi);
    }
    if (pn_connection_driver_write_closed(&c->driver)) {
        lws_close_reason(c->wsi, LWS_CLOSE_STATUS_NORMAL, NULL, 0);
        return -1;
    }
    return 0;
}

/* The server has a bounded, thread-safe queue for external work */
typedef struct work_t {
    enum { W_NONE, W_LISTEN, W_CLOSE, W_WAKE, W_STOP, W_HANDLE_STATS } type;
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
    qdr_core_t *core;
    sys_thread_t *thread;
    work_queue_t work;
    qd_log_source_t *log;
    struct lws_context *context;
    qd_timestamp_t now;         /* Cache current time in thread_run */
    qd_timestamp_t next_tick;   /* Next requested tick service */
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

/* Each qd_lws_listener_t is associated with an lws_vhost */
struct qd_lws_listener_t {
    qd_listener_t *listener;
    qd_http_server_t *server;
    struct lws_vhost *vhost;
    struct lws_http_mount mount;
    struct lws_http_mount metrics;
    struct lws_http_mount healthz;
};

void qd_lws_listener_free(qd_lws_listener_t *hl) {
    if (!hl) return;
    if (hl->listener) {
        hl->listener->http = NULL;
        qd_listener_decref(hl->listener);
    }
    free(hl);
}

static qd_lws_listener_t *qd_lws_listener(qd_http_server_t *hs, qd_listener_t *li) {
    qd_lws_listener_t *hl = calloc(1, sizeof(*hl));
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

/* Linked list: first entry on each line should point to next, last line should be the 
 * octet-stream default.
 */
static const struct lws_protocol_vhost_options mime_types[] = {
    { &mime_types[1], NULL, ".json", "application/json" },
    { &mime_types[2], NULL, ".woff2", "font/woff2" },
    { NULL, NULL, "*", "application/octet-stream" }
};

static void listener_start(qd_lws_listener_t *hl, qd_http_server_t *hs) {
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
    m->origin = (config->http_root_dir && *config->http_root_dir) ? /* File system root */
        config->http_root_dir : QPID_CONSOLE_STAND_ALONE_INSTALL_DIR;
    m->def = "index.html";  /* Default file name */
    m->origin_protocol = LWSMPRO_FILE; /* mount type is a directory in a filesystem */
    m->extra_mimetypes = mime_types;
    struct lws_http_mount *tail = m;
    if (config->metrics) {
        struct lws_http_mount *metrics = &hl->metrics;
        tail->mount_next = metrics;
        tail = metrics;
        metrics->mountpoint = "/metrics";
        metrics->mountpoint_len = strlen(metrics->mountpoint);
        metrics->origin_protocol = LWSMPRO_CALLBACK;
        metrics->protocol = "http";
        metrics->origin = IGNORED;
    }
    if (config->healthz) {
        struct lws_http_mount *healthz = &hl->healthz;
        tail->mount_next = healthz;
        healthz->mountpoint = "/healthz";
        healthz->mountpoint_len = strlen(healthz->mountpoint);
        healthz->origin_protocol = LWSMPRO_CALLBACK;
        healthz->protocol = "healthz";
        healthz->origin = IGNORED;
    }

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
        info.ssl_ca_filepath = config->ssl_trusted_certificate_db;
        info.ssl_cipher_list = config->ssl_ciphers;

        info.options |=
            LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT |
#if LWS_LIBRARY_VERSION_MAJOR > 3 || (LWS_LIBRARY_VERSION_MAJOR == 3 && LWS_LIBRARY_VERSION_MINOR >= 2)
            (config->ssl_required ? 0 : LWS_SERVER_OPTION_ALLOW_NON_SSL_ON_SSL_PORT | LWS_SERVER_OPTION_ALLOW_HTTP_ON_HTTPS_LISTENER) |
#else
            (config->ssl_required ? 0 : LWS_SERVER_OPTION_ALLOW_NON_SSL_ON_SSL_PORT) |
#endif
            ((config->requireAuthentication && info.ssl_ca_filepath) ? LWS_SERVER_OPTION_REQUIRE_VALID_OPENSSL_CLIENT_CERT : 0);
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
    qd_lws_listener_free(hl);
}

static void listener_close(qd_lws_listener_t *hl, qd_http_server_t *hs) {
    qd_server_config_t *config = &hl->listener->config;
    qd_log(hs->log, QD_LOG_NOTICE, "Stopped listening for HTTP on %s", config->host_port);
    lws_vhost_destroy(hl->vhost);
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
        qd_lws_listener_free(wsi_listener(wsi));
        break;
      default:
        break;
    }
    /* Do default HTTP handling for all the cases we don't care about. */
    return lws_callback_http_dummy(wsi, reason, user, in, len);
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

/**
 * Called on router worker thread
 */
static void handle_stats_results(void *context)
{
    stats_request_state_t* state = (stats_request_state_t*) context;
    if (state->wsi_deleted) {
        free(state);
    } else {
        qd_http_server_t *hs = state->server;
        if (hs) {
            work_t w = { W_HANDLE_STATS, state };
            work_push(hs, w);
        }
    }
}

/**
 * Called on http thread
 */
static void handle_stats_result_HT(stats_request_state_t* state)
{
    if (state->wsi_deleted) {
        free(state);
    } else {
        state->callback_completed = true;
        lws_callback_on_writable(state->wsi);
    }
}

typedef int (*int_metric) (qdr_global_stats_t *stats);
typedef struct metric_definition {
    const char* name;
    const char* type;
    int_metric value;
} metric_definition;

typedef struct allocator_metric_definition {
    const char* name;
    qd_alloc_stats_t *(*fn)(void);
} allocator_metric_definition;

static int stats_get_connections(qdr_global_stats_t *stats) { return stats->connections; }
static int stats_get_links(qdr_global_stats_t *stats) { return stats->links; }
static int stats_get_addrs(qdr_global_stats_t *stats) { return stats->addrs; }
static int stats_get_routers(qdr_global_stats_t *stats) { return stats->routers; }
static int stats_get_link_routes(qdr_global_stats_t *stats) { return stats->link_routes; }
static int stats_get_auto_links(qdr_global_stats_t *stats) { return stats->auto_links; }
static int stats_get_presettled_deliveries(qdr_global_stats_t *stats) { return stats->presettled_deliveries; }
static int stats_get_dropped_presettled_deliveries(qdr_global_stats_t *stats) { return stats->dropped_presettled_deliveries; }
static int stats_get_accepted_deliveries(qdr_global_stats_t *stats) { return stats->accepted_deliveries; }
static int stats_get_released_deliveries(qdr_global_stats_t *stats) { return stats->released_deliveries; }
static int stats_get_rejected_deliveries(qdr_global_stats_t *stats) { return stats->rejected_deliveries; }
static int stats_get_modified_deliveries(qdr_global_stats_t *stats) { return stats->modified_deliveries; }
static int stats_get_deliveries_ingress(qdr_global_stats_t *stats) { return stats->deliveries_ingress; }
static int stats_get_deliveries_egress(qdr_global_stats_t *stats) { return stats->deliveries_egress; }
static int stats_get_deliveries_transit(qdr_global_stats_t *stats) { return stats->deliveries_transit; }
static int stats_get_deliveries_ingress_route_container(qdr_global_stats_t *stats) { return stats->deliveries_ingress_route_container; }
static int stats_get_deliveries_egress_route_container(qdr_global_stats_t *stats) { return stats->deliveries_egress_route_container; }
static int stats_get_deliveries_delayed_1sec(qdr_global_stats_t *stats) { return stats->deliveries_delayed_1sec; }
static int stats_get_deliveries_delayed_10sec(qdr_global_stats_t *stats) { return stats->deliveries_delayed_10sec; }
static int stats_get_deliveries_stuck(qdr_global_stats_t *stats) { return stats->deliveries_stuck; }
static int stats_get_links_blocked(qdr_global_stats_t *stats) { return stats->links_blocked; }
static int stats_get_deliveries_redirected_to_fallback(qdr_global_stats_t *stats) { return stats->deliveries_redirected_to_fallback; }

qd_alloc_stats_t *alloc_stats_qd_bitmask_t(void);
qd_alloc_stats_t *alloc_stats_qd_buffer_t(void);
qd_alloc_stats_t *alloc_stats_qd_composed_field_t(void);
qd_alloc_stats_t *alloc_stats_qd_composite_t(void);
qd_alloc_stats_t *alloc_stats_qd_connection_t(void);
qd_alloc_stats_t *alloc_stats_qd_hash_handle_t(void);
qd_alloc_stats_t *alloc_stats_qd_hash_item_t(void);
qd_alloc_stats_t *alloc_stats_qd_iterator_t(void);
qd_alloc_stats_t *alloc_stats_qd_link_ref_t(void);
qd_alloc_stats_t *alloc_stats_qd_link_t(void);
qd_alloc_stats_t *alloc_stats_qd_listener_t(void);
qd_alloc_stats_t *alloc_stats_qd_log_entry_t(void);
qd_alloc_stats_t *alloc_stats_qd_management_context_t(void);
qd_alloc_stats_t *alloc_stats_qd_message_content_t(void);
qd_alloc_stats_t *alloc_stats_qd_message_t(void);
qd_alloc_stats_t *alloc_stats_qd_node_t(void);
qd_alloc_stats_t *alloc_stats_qd_parse_node_t(void);
qd_alloc_stats_t *alloc_stats_qd_parsed_field_t(void);
qd_alloc_stats_t *alloc_stats_qd_timer_t(void);
qd_alloc_stats_t *alloc_stats_qdr_action_t(void);
qd_alloc_stats_t *alloc_stats_qdr_address_config_t(void);
qd_alloc_stats_t *alloc_stats_qdr_address_t(void);
qd_alloc_stats_t *alloc_stats_qdr_connection_info_t(void);
qd_alloc_stats_t *alloc_stats_qdr_connection_t(void);
qd_alloc_stats_t *alloc_stats_qdr_connection_work_t(void);
qd_alloc_stats_t *alloc_stats_qdr_core_timer_t(void);
qd_alloc_stats_t *alloc_stats_qdr_delivery_cleanup_t(void);
qd_alloc_stats_t *alloc_stats_qdr_delivery_ref_t(void);
qd_alloc_stats_t *alloc_stats_qdr_delivery_t(void);
qd_alloc_stats_t *alloc_stats_qdr_field_t(void);
qd_alloc_stats_t *alloc_stats_qdr_general_work_t(void);
qd_alloc_stats_t *alloc_stats_qdr_link_ref_t(void);
qd_alloc_stats_t *alloc_stats_qdr_link_t(void);
qd_alloc_stats_t *alloc_stats_qdr_link_work_t(void);
qd_alloc_stats_t *alloc_stats_qdr_query_t(void);
qd_alloc_stats_t *alloc_stats_qdr_terminus_t(void);

static struct metric_definition metrics[] = {
    {"qdr_connections_total", "gauge", stats_get_connections},
    {"qdr_links_total", "gauge", stats_get_links},
    {"qdr_addresses_total", "gauge", stats_get_addrs},
    {"qdr_routers_total", "gauge", stats_get_routers},
    {"qdr_link_routes_total", "gauge", stats_get_link_routes},
    {"qdr_auto_links_total", "gauge", stats_get_auto_links},
    {"qdr_presettled_deliveries_total", "counter", stats_get_presettled_deliveries},
    {"qdr_dropped_presettled_deliveries_total", "counter", stats_get_dropped_presettled_deliveries},
    {"qdr_accepted_deliveries_total", "counter", stats_get_accepted_deliveries},
    {"qdr_released_deliveries_total", "counter", stats_get_released_deliveries},
    {"qdr_rejected_deliveries_total", "counter", stats_get_rejected_deliveries},
    {"qdr_modified_deliveries_total", "counter", stats_get_modified_deliveries},
    {"qdr_deliveries_ingress_total", "counter", stats_get_deliveries_ingress},
    {"qdr_deliveries_egress_total", "counter", stats_get_deliveries_egress},
    {"qdr_deliveries_transit_total", "counter", stats_get_deliveries_transit},
    {"qdr_deliveries_ingress_route_container_total", "counter", stats_get_deliveries_ingress_route_container},
    {"qdr_deliveries_egress_route_container_total", "counter", stats_get_deliveries_egress_route_container},
    {"qdr_deliveries_delayed_1sec_total", "counter", stats_get_deliveries_delayed_1sec},
    {"qdr_deliveries_delayed_10sec_total", "counter", stats_get_deliveries_delayed_10sec},
    {"qdr_deliveries_stuck_total", "gauge", stats_get_deliveries_stuck},
    {"qdr_links_blocked_total", "gauge", stats_get_links_blocked},
    {"qdr_deliveries_redirected_to_fallback_total", "counter", stats_get_deliveries_redirected_to_fallback}
};
static size_t metrics_length = sizeof(metrics)/sizeof(metrics[0]);

static struct allocator_metric_definition allocator_metrics[] = {
        {"qdr_allocator_qd_bitmask_t", alloc_stats_qd_bitmask_t},
        {"qdr_allocator_qd_buffer_t", alloc_stats_qd_buffer_t},
        {"qdr_allocator_qd_composed_field_t", alloc_stats_qd_composed_field_t},
        {"qdr_allocator_qd_composite_t", alloc_stats_qd_composite_t},
        {"qdr_allocator_qd_connection_t", alloc_stats_qd_connection_t},
        {"qdr_allocator_qd_hash_handle_t", alloc_stats_qd_hash_handle_t},
        {"qdr_allocator_qd_hash_item_t", alloc_stats_qd_hash_item_t},
        {"qdr_allocator_qd_iterator_t", alloc_stats_qd_iterator_t},
        {"qdr_allocator_qd_link_ref_t", alloc_stats_qd_link_ref_t},
        {"qdr_allocator_qd_link_t", alloc_stats_qd_link_t},
        {"qdr_allocator_qd_listener_t", alloc_stats_qd_listener_t},
        {"qdr_allocator_qd_log_entry_t", alloc_stats_qd_log_entry_t},
        {"qdr_allocator_qd_management_context_t", alloc_stats_qd_management_context_t},
        {"qdr_allocator_qd_message_content_t", alloc_stats_qd_message_content_t},
        {"qdr_allocator_qd_message_t", alloc_stats_qd_message_t},
        {"qdr_allocator_qd_node_t", alloc_stats_qd_node_t},
        {"qdr_allocator_qd_parse_node_t", alloc_stats_qd_parse_node_t},
        {"qdr_allocator_qd_parsed_field_t", alloc_stats_qd_parsed_field_t},
        {"qdr_allocator_qd_timer_t", alloc_stats_qd_timer_t},
        {"qdr_allocator_qdr_action_t", alloc_stats_qdr_action_t},
        {"qdr_allocator_qdr_address_config_t", alloc_stats_qdr_address_config_t},
        {"qdr_allocator_qdr_address_t", alloc_stats_qdr_address_t},
        {"qdr_allocator_qdr_connection_info_t", alloc_stats_qdr_connection_info_t},
        {"qdr_allocator_qdr_connection_t", alloc_stats_qdr_connection_t},
        {"qdr_allocator_qdr_connection_work_t", alloc_stats_qdr_connection_work_t},
        {"qdr_allocator_qdr_core_timer_t", alloc_stats_qdr_core_timer_t},
        {"qdr_allocator_qdr_delivery_cleanup_t", alloc_stats_qdr_delivery_cleanup_t},
        {"qdr_allocator_qdr_delivery_ref_t", alloc_stats_qdr_delivery_ref_t},
        {"qdr_allocator_qdr_delivery_t", alloc_stats_qdr_delivery_t},
        {"qdr_allocator_qdr_field_t", alloc_stats_qdr_field_t},
        {"qdr_allocator_qdr_general_work_t", alloc_stats_qdr_general_work_t},
        {"qdr_allocator_qdr_link_ref_t", alloc_stats_qdr_link_ref_t},
        {"qdr_allocator_qdr_link_t", alloc_stats_qdr_link_t},
        {"qdr_allocator_qdr_link_work_t", alloc_stats_qdr_link_work_t},
        {"qdr_allocator_qdr_query_t", alloc_stats_qdr_query_t},
        {"qdr_allocator_qdr_terminus_t", alloc_stats_qdr_terminus_t}
};
static size_t allocator_metrics_length = sizeof(allocator_metrics)/sizeof(allocator_metrics[0]);

#define ALLOC_DATA(S, F) ((allocator_field) {#F, (S!=NULL? S->F: 0)})

typedef struct allocator_field {
    const char* name;
    uint64_t value;
} allocator_field;

static bool write_stats(uint8_t **position, const uint8_t * const end, const char* name, const char* type, int value)
{
    //11 chars + type + 2*name + 20 chars for int
    // average metric name size is 30 bytes
    // average metric type size is 8 bytes
    // current number of metrics is 22
    // total metric buffer size = 22 * (11 + 8 + 2*30 + 20) = 2178
    size_t length = 11 + strlen(type) + strlen(name)*2 + 20;
    if (end - *position >= length) {
        *position += lws_snprintf((char*) *position, end - *position, "# TYPE %s %s\n", name, type);
        *position += lws_snprintf((char*) *position, end - *position, "%s %i\n", name, value);
        return true;
    } else {
        return false;
    }
}

static bool write_allocator_stats(uint8_t **position, const uint8_t * const end, const char* name, allocator_field field)
{
    // 30 chars (static) + 2*name + 2*field.name + 20 for int
    // average allocator metric name size is 54 bytes (name:field.name)
    // current number of metrics is 180
    // total allocator buffer size = 180 * (30 + 2*54 + 20) = 28440
    size_t length = 30 + strlen(name)*2 + strlen(field.name)*2 + 20;
    if (end - *position >= length) {
        *position += lws_snprintf((char*) *position, end - *position, "# TYPE %s:%s_bytes gauge\n", name, field.name);
        *position += lws_snprintf((char*) *position, end - *position, "%s:%s_bytes %"PRIu64"\n", name, field.name, field.value);
        return true;
    } else {
        return false;
    }
}

static bool write_metric(uint8_t **position, const uint8_t * const end, metric_definition* definition, qdr_global_stats_t* stats)
{
    return write_stats(position, end, definition->name, definition->type, definition->value(stats));
}

static bool write_allocator_metric(uint8_t **position, const uint8_t * const end, allocator_metric_definition* definition)
{
    qd_alloc_stats_t *allocator_stats = definition->fn();
    if (!write_allocator_stats(position, end, definition->name, ALLOC_DATA(allocator_stats, total_alloc_from_heap))) return false;
    if (!write_allocator_stats(position, end, definition->name, ALLOC_DATA(allocator_stats, total_free_to_heap))) return false;
    if (!write_allocator_stats(position, end, definition->name, ALLOC_DATA(allocator_stats, held_by_threads))) return false;
    if (!write_allocator_stats(position, end, definition->name, ALLOC_DATA(allocator_stats, batches_rebalanced_to_threads))) return false;
    if (!write_allocator_stats(position, end, definition->name, ALLOC_DATA(allocator_stats, batches_rebalanced_to_global))) return false;
    return true;
}

static int add_header_by_name(struct lws *wsi, const char* name, const char* value, uint8_t** position, uint8_t* end)
{
    return lws_add_http_header_by_name(wsi, (unsigned char*) name, (unsigned char*) value, strlen(value), position, end);
}

static int callback_metrics(struct lws *wsi, enum lws_callback_reasons reason,
                               void *user, void *in, size_t len)
{
    qd_http_server_t *hs = wsi_server(wsi);
    stats_t *stats = (stats_t*) user;
    // rationale for buffer size is explained at write_stats and write_allocator_stats
    uint8_t buffer[LWS_PRE + 30618];
    uint8_t *start = &buffer[LWS_PRE], *position = start, *end = &buffer[sizeof(buffer) - LWS_PRE - 1];

    switch (reason) {

    case LWS_CALLBACK_HTTP: {
        stats->context = NEW(stats_request_state_t);
        ZERO(stats->context);
        stats->context->wsi = wsi;
        stats->context->server = hs;
        //request stats from core thread
        qdr_request_global_stats(hs->core, &stats->context->stats, handle_stats_results, (void*) stats->context);
        return 0;
    }

    case LWS_CALLBACK_HTTP_WRITEABLE: {
        //encode stats into buffer
        if (!stats->headers_sent) {
            if (lws_add_http_header_status(wsi, HTTP_STATUS_OK, &position, end)
                || add_header_by_name(wsi, "content-type:", "text/plain", &position, end)
                || add_header_by_name(wsi, "connection:", "close", &position, end))
                return 1;
            if (lws_finalize_http_header(wsi, &position, end))
                return 1;
            stats->headers_sent = true;
        }

        while (stats->current < metrics_length) {
            if (write_metric(&position, end, &metrics[stats->current], &stats->context->stats)) {
                stats->current++;
                qd_log(hs->log, QD_LOG_DEBUG, "wrote metric %lu of %lu", stats->current, metrics_length);
            } else {
                qd_log(hs->log, QD_LOG_WARNING, "insufficient space in buffer");
                break;
            }
        }

        int alloc_cur = 0;
        while (alloc_cur < allocator_metrics_length) {
            if (write_allocator_metric(&position, end, &allocator_metrics[alloc_cur])) {
                qd_log(hs->log, QD_LOG_DEBUG, "wrote allocator metric %lu of %lu", alloc_cur, allocator_metrics_length);
                alloc_cur++;
            } else {
                qd_log(hs->log, QD_LOG_WARNING, "insufficient space in buffer");
                break;
            }
        }
        int n = (stats->current < metrics_length) || (alloc_cur < allocator_metrics_length) ? LWS_WRITE_HTTP : LWS_WRITE_HTTP_FINAL;

        //write buffer
        size_t available = position - start;
        if (lws_write(wsi, (unsigned char*) start, available, n) != available)
            return 1;
        if (n == LWS_WRITE_HTTP_FINAL) {
            if (lws_http_transaction_completed(wsi)) return -1;
        } else {
            lws_callback_on_writable(wsi);
        }
        return 0;
    }

    case LWS_CALLBACK_CLOSED: {
        stats->context->wsi_deleted = true;
        if (stats->context->callback_completed) {
            free(stats->context);
        }
    }

    default:
        return 0;
    }
}

static int callback_healthz(struct lws *wsi, enum lws_callback_reasons reason,
                               void *user, void *in, size_t len)
{
    qd_http_server_t *hs = wsi_server(wsi);
    stats_t *stats = (stats_t*) user;
    uint8_t buffer[LWS_PRE + 2048];
    uint8_t *start = &buffer[LWS_PRE], *position = start, *end = &buffer[sizeof(buffer) - LWS_PRE - 1];

    switch (reason) {

    case LWS_CALLBACK_HTTP: {
        stats->context = NEW(stats_request_state_t);
        ZERO(stats->context);
        stats->context->wsi = wsi;
        stats->context->server = hs;
        //make dummy request for stats (pass in null ptr); this still exercises the
        //path through core thread and back through callback on io thread which is
        //a reasonable initial liveness check
        qdr_request_global_stats(hs->core, 0, handle_stats_results, (void*) stats->context);
        return 0;
    }

    case LWS_CALLBACK_HTTP_WRITEABLE: {
        //encode stats into buffer
        if (!stats->headers_sent) {
            if (lws_add_http_header_status(wsi, HTTP_STATUS_OK, &position, end)
                || add_header_by_name(wsi, "content-type:", "text/plain", &position, end)
                || lws_add_http_header_content_length(wsi, 3, &position, end))
                return 1;
            if (lws_finalize_http_header(wsi, &position, end))
                return 1;
            stats->headers_sent = true;
        }
        position += lws_snprintf((char*) position, end - position, "OK\n");

        int n = LWS_WRITE_HTTP_FINAL;
        //write buffer
        size_t available = position - start;
	if (lws_write(wsi, (unsigned char*) start, available, n) != available)
            return 1;
        else if (lws_http_transaction_completed(wsi))
            return -1;
        else return 0;
    }

    case LWS_CALLBACK_CLOSED: {
        stats->context->wsi_deleted = true;
        if (stats->context->callback_completed) {
            free(stats->context);
        }
    }

    default:
        return 0;
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
        qd_lws_listener_t *hl = wsi_listener(wsi);
        if (hl == NULL || !hl->listener->config.websockets) {
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
                listener_start((qd_lws_listener_t*)w.value, hs);
                break;
            case W_CLOSE:
                listener_close((qd_lws_listener_t*)w.value, hs);
                break;
            case W_HANDLE_STATS:
                handle_stats_result_HT((stats_request_state_t*) w.value);
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

void qd_http_server_stop(qd_http_server_t *hs) {
    if (!hs) return;
    if (hs->thread) {
        /* Thread safe, stop via work queue then clean up */
        work_t work = { W_STOP, NULL };
        work_push(hs, work);
        sys_thread_join(hs->thread);
        sys_thread_free(hs->thread);
        hs->thread = NULL;
    }
}

void qd_http_server_free(qd_http_server_t *hs) {
    if (!hs) return;
    qd_http_server_stop(hs);
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
        hs->core = 0; // not yet available
        if (!hs->context) {
            qd_log(hs->log, QD_LOG_CRITICAL, "No memory starting HTTP server");
            qd_http_server_free(hs);
            hs = NULL;
        }
    }
    return hs;
}

/* Thread safe calls that put items on work queue */

qd_lws_listener_t *qd_http_server_listen(qd_http_server_t *hs, qd_listener_t *li)
{
    hs->core = qd_dispatch_router_core(qd_server_dispatch(hs->server));
    sys_mutex_lock(hs->work.lock);
    if (!hs->thread) {
        hs->thread = sys_thread(http_thread_run, hs);
    }
    bool ok = hs->thread;
    sys_mutex_unlock(hs->work.lock);
    if (!ok) return NULL;

    qd_lws_listener_t *hl = qd_lws_listener(hs, li);
    if (hl) {
        work_t w = { W_LISTEN, hl };
        work_push(hs, w);
    }
    return hl;
}

void qd_lws_listener_close(qd_lws_listener_t *hl)
{
    work_t w = { W_CLOSE, hl };
    work_push(hl->server, w);
}

static qd_http_server_t *wsi_server(struct lws *wsi) {
    return (qd_http_server_t*)lws_context_user(lws_get_context(wsi));
}

static qd_lws_listener_t *wsi_listener(struct lws *wsi) {
    qd_lws_listener_t *hl = NULL;
    struct lws_vhost *vhost = lws_get_vhost(wsi);
    if (vhost) {                /* Get qd_lws_listener from vhost data */
        void *vp = lws_protocol_vh_priv_get(vhost, &protocols[0]);
        memcpy(&hl, vp, sizeof(hl));
    }
    return hl;
}

static qd_log_source_t *wsi_log(struct lws *wsi) {
    return wsi_server(wsi)->log;
}

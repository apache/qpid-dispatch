#ifndef __http_common_h__
#define __http_common_h__
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
#include <qpid/dispatch/ctools.h>
#include <qpid/dispatch/alloc.h>
#include <qpid/dispatch/timer.h>

#include "delivery.h"
#include "entity.h"

#define QD_HTTP_LOG_SOURCE "HTTP_ADAPTOR"

typedef enum {
    VERSION_HTTP1,
    VERSION_HTTP2,
} qd_http_version_t;

typedef struct qd_http_bridge_config_t {
    char              *name;
    char              *host;
    char              *port;
    char              *address;
    char              *host_port;
    qd_http_version_t  version;
} qd_http_bridge_config_t;

void qd_http_free_bridge_config(qd_http_bridge_config_t *config);

typedef struct qd_http_lsnr_t qd_http_lsnr_t;
struct qd_http_lsnr_t {
    qd_http_bridge_config_t    config;
    qd_handler_context_t       context;
    sys_atomic_t               ref_count;
    qd_server_t               *server;
    pn_listener_t             *pn_listener;
    DEQ_LINKS(qd_http_lsnr_t);
};
DEQ_DECLARE(qd_http_lsnr_t, qd_http_lsnr_list_t);

qd_http_lsnr_t *qd_http_lsnr(qd_server_t *server, qd_server_event_handler_t handler);
void qd_http_listener_decref(qd_http_lsnr_t* li);

typedef struct qd_http_connector_t qd_http_connector_t;
struct qd_http_connector_t {
    qd_http_bridge_config_t       config;
    sys_atomic_t                  ref_count;
    qd_server_t                  *server;
    qd_timer_t                   *timer;
    long                          delay;
    struct qdr_http_connection_t *dispatcher;

    DEQ_LINKS(qd_http_connector_t);
};
DEQ_DECLARE(qd_http_connector_t, qd_http_connector_list_t);

qd_http_connector_t *qd_http_connector(qd_server_t *server);
void qd_http_connector_decref(qd_http_connector_t* c);



//
// Management Entity Interfaces (see HttpListenerEntity and HttpConnectorEntity in agent.py)
//

qd_http_lsnr_t *qd_dispatch_configure_http_lsnr(qd_dispatch_t *qd, qd_entity_t *entity);
void qd_dispatch_delete_http_listener(qd_dispatch_t *qd, void *impl);
qd_error_t qd_entity_refresh_httpListener(qd_entity_t* entity, void *impl);

qd_http_connector_t *qd_dispatch_configure_http_connector(qd_dispatch_t *qd, qd_entity_t *entity);
void qd_dispatch_delete_http_connector(qd_dispatch_t *qd, void *impl);
qd_error_t qd_entity_refresh_httpConnector(qd_entity_t* entity, void *impl);

//
// These functions are defined in their respective HTTP adaptors:
//

qd_http_lsnr_t *qd_http1_configure_listener(qd_dispatch_t *, const qd_http_bridge_config_t *, qd_entity_t *);
qd_http_lsnr_t *qd_http2_configure_listener(qd_dispatch_t *, const qd_http_bridge_config_t *, qd_entity_t *);

void qd_http1_delete_listener(qd_dispatch_t *, qd_http_lsnr_t *);
void qd_http2_delete_listener(qd_dispatch_t *, qd_http_lsnr_t *);

qd_http_connector_t *qd_http1_configure_connector(qd_dispatch_t *, const qd_http_bridge_config_t *, qd_entity_t *);
qd_http_connector_t *qd_http2_configure_connector(qd_dispatch_t *, const qd_http_bridge_config_t *, qd_entity_t *);

void qd_http1_delete_connector(qd_dispatch_t *, qd_http_connector_t *);
void qd_http2_delete_connector(qd_dispatch_t *, qd_http_connector_t *);


#endif // __http_common_h__

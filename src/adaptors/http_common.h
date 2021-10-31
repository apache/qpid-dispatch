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

#include "delivery.h"
#include "entity.h"

#include "qpid/dispatch/alloc.h"
#include "qpid/dispatch/atomic.h"
#include "qpid/dispatch/ctools.h"
#include "qpid/dispatch/timer.h"

#define QD_HTTP_LOG_SOURCE "HTTP_ADAPTOR"

typedef enum {
    VERSION_HTTP1,
    VERSION_HTTP2,
} qd_http_version_t;

typedef enum {
    QD_AGGREGATION_NONE,
    QD_AGGREGATION_JSON,
    QD_AGGREGATION_MULTIPART
} qd_http_aggregation_t;

typedef struct qd_http_bridge_config_t {
    char              *name;
    char              *host;
    char              *port;
    char              *address;
    char              *site;
    char              *host_override;
    char              *host_port;
    qd_http_version_t  version;
    bool                  event_channel;
    qd_http_aggregation_t aggregation;
} qd_http_bridge_config_t;

void qd_http_free_bridge_config(qd_http_bridge_config_t *config);

typedef struct qd_http_listener_t qd_http_listener_t;
struct qd_http_listener_t {
    qd_http_bridge_config_t    config;
    qd_handler_context_t       context;
    sys_atomic_t               ref_count;
    qd_server_t               *server;
    pn_listener_t             *pn_listener;
    DEQ_LINKS(qd_http_listener_t);
};
DEQ_DECLARE(qd_http_listener_t, qd_http_listener_list_t);

qd_http_listener_t *qd_http_listener(qd_server_t *server, qd_server_event_handler_t handler);
void qd_http_listener_decref(qd_http_listener_t* li);

typedef struct qd_http_connector_t qd_http_connector_t;
struct qd_http_connector_t {
    qd_http_bridge_config_t       config;
    sys_atomic_t                  ref_count;
    qd_server_t                  *server;
    qd_timer_t                   *timer;
    long                          delay;
    void                         *ctx;
    DEQ_LINKS(qd_http_connector_t);
};
DEQ_DECLARE(qd_http_connector_t, qd_http_connector_list_t);

qd_http_connector_t *qd_http_connector(qd_server_t *server);
void qd_http_connector_decref(qd_http_connector_t* c);



//
// Management Entity Interfaces (see HttpListenerEntity and HttpConnectorEntity in agent.py)
//

QD_EXPORT qd_http_listener_t *qd_dispatch_configure_http_listener(qd_dispatch_t *qd, qd_entity_t *entity);
QD_EXPORT void qd_dispatch_delete_http_listener(qd_dispatch_t *qd, void *impl);
qd_error_t qd_entity_refresh_httpListener(qd_entity_t* entity, void *impl);

QD_EXPORT qd_http_connector_t *qd_dispatch_configure_http_connector(qd_dispatch_t *qd, qd_entity_t *entity);
QD_EXPORT void qd_dispatch_delete_http_connector(qd_dispatch_t *qd, void *impl);
qd_error_t qd_entity_refresh_httpConnector(qd_entity_t* entity, void *impl);

// Management interfaces for retrieval of HttpRequestInfo entities
void qdra_http_request_info_get_first_CT(qdr_core_t *core, qdr_query_t *query, int offset);
void qdra_http_request_info_get_next_CT(qdr_core_t *core, qdr_query_t *query);
void qdra_http_request_info_get_CT(qdr_core_t          *core,
                                   qd_iterator_t       *name,
                                   qd_iterator_t       *identity,
                                   qdr_query_t         *query,
                                   const char          *qdr_http_request_info_columns[]);

#define QDR_HTTP_REQUEST_INFO_COLUMN_COUNT 11
extern const char *qdr_http_request_info_columns[QDR_HTTP_REQUEST_INFO_COLUMN_COUNT + 1];

void qd_http_record_request(qdr_core_t *core, const char * method, uint32_t status_code, const char *address, const char *host,
                            const char *local_site, const char *remote_site, bool ingress,
                            uint64_t bytes_in, uint64_t bytes_out, uint64_t latency);
char *qd_get_host_from_host_port(const char *host_port);

//
// These functions are defined in their respective HTTP adaptors:
//

qd_http_listener_t *qd_http1_configure_listener(qd_dispatch_t *, const qd_http_bridge_config_t *, qd_entity_t *);
qd_http_listener_t *qd_http2_configure_listener(qd_dispatch_t *, const qd_http_bridge_config_t *, qd_entity_t *);

void qd_http1_delete_listener(qd_dispatch_t *, qd_http_listener_t *);
void qd_http2_delete_listener(qd_dispatch_t *, qd_http_listener_t *);

qd_http_connector_t *qd_http1_configure_connector(qd_dispatch_t *, const qd_http_bridge_config_t *, qd_entity_t *);
qd_http_connector_t *qd_http2_configure_connector(qd_dispatch_t *, const qd_http_bridge_config_t *, qd_entity_t *);

void qd_http1_delete_connector(qd_dispatch_t *, qd_http_connector_t *);
void qd_http2_delete_connector(qd_dispatch_t *, qd_http_connector_t *);


#endif // __http_common_h__

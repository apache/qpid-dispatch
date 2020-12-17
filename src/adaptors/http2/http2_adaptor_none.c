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

#include "adaptors/http_common.h"

//
// Empty implementation of management related http2 functions.
// The nghttp2 library was not found, so no http2 functionality provided.
//

/**
 * Configure listener.
 */
qd_http_listener_t *qd_http2_configure_listener(qd_dispatch_t *qd, const qd_http_bridge_config_t *config, qd_entity_t *entity)
{
    qd_log_source_t  *log_source = qd_log_source(QD_HTTP_LOG_SOURCE);
    char *port = qd_entity_get_string(entity, "port");
    qd_log(log_source, QD_LOG_ERROR, "HTTP2 adaptor not activated due to missing nghttp2 library. Cannot open listener on port %s", port);
    free(port);
    return 0;
}

/**
 * Delete listener via Management request
 * Empty implementation.
 */
void qd_http2_delete_listener(qd_dispatch_t *qd, qd_http_listener_t *li)
{
    qd_log_source_t  *log_source = qd_log_source(QD_HTTP_LOG_SOURCE);
    qd_log(log_source, QD_LOG_ERROR, "HTTP2 adaptor not activated due to missing nghttp2 library. Cannot delete listeners");
}


/**
 * Configure connector
 */
qd_http_connector_t *qd_http2_configure_connector(qd_dispatch_t *qd, const qd_http_bridge_config_t *config, qd_entity_t *entity)
{
    qd_log_source_t  *log_source = qd_log_source(QD_HTTP_LOG_SOURCE);
    char *port = qd_entity_get_string(entity, "port");
    qd_log(log_source, QD_LOG_ERROR, "HTTP2 adaptor not activated due to missing nghttp2 library. Cannot open connector to port %s", port);
    free(port);
    return 0;
}


/**
 * Delete connector via Management request
 * Empty implementation.
 */
void qd_http2_delete_connector(qd_dispatch_t *qd, qd_http_connector_t *connector)
{
    qd_log_source_t  *log_source = qd_log_source(QD_HTTP_LOG_SOURCE);
    qd_log(log_source, QD_LOG_ERROR, "HTTP2 adaptor not activated due to missing nghttp2 library. Cannot delete connectors");

}

#ifndef QD_HTTP_H
#define QD_HTTP_H

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

typedef struct qd_http_listener_t qd_http_listener_t;
typedef struct qd_http_server_t qd_http_server_t;

struct qd_dispatch_t;
struct qd_log_source_t;
struct qd_server_config_t;
struct qdpn_connector_t;

qd_http_server_t *qd_http_server(struct qd_dispatch_t *dispatch, struct qd_log_source_t *log);
void qd_http_server_free(qd_http_server_t*);
qd_http_listener_t *qd_http_listener(struct qd_http_server_t *s,
                                     const struct qd_server_config_t *config);
void qd_http_listener_free(qd_http_listener_t *hl);
/* On error, qdpn_connector_closed(c) is true. */
void qd_http_listener_accept(qd_http_listener_t *hl, struct qdpn_connector_t *c);

#endif // QD_HTTP_H

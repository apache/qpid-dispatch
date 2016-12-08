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

#include <qpid/dispatch/dispatch.h>

typedef struct qd_http_t qd_http_t;
typedef struct qd_http_connector_t qd_http_connector_t;

qd_http_t *qd_http(qd_dispatch_t *d, qd_log_source_t *log);
void qd_http_free(qd_http_t *http);
qd_http_connector_t *qd_http_connector(qd_http_t *h, qdpn_connector_t *c);
void qd_http_connector_process(qdpn_connector_t *c);

#endif // QD_HTTP_H

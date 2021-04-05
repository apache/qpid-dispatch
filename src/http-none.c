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

#include "http.h"

#include "qpid/dispatch/log.h"

struct qd_dispatch_t;

/* No HTTP implementation available. */

qd_http_server_t *qd_http_server(struct qd_server_t *s, qd_log_source_t *log)
{
    qd_log(log, QD_LOG_WARNING, "HTTP support is not available");
    return 0;
}

void qd_http_server_free(qd_http_server_t *h) {}

void* qd_http_server_run(void* qd_http_server) { return 0; }

void qd_http_server_stop(qd_http_server_t *h) {}

qd_lws_listener_t *qd_http_server_listen(qd_http_server_t *s, struct qd_listener_t *li) { return 0; }

void qd_lws_listener_close(qd_lws_listener_t *hl) {}




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

typedef struct qd_lws_listener_t qd_lws_listener_t;
typedef struct qd_http_server_t qd_http_server_t;

struct qd_server_t;
struct qd_server_config_t;
struct qd_listener_t;
struct qd_log_source_t;

/* Create a HTTP server */
qd_http_server_t *qd_http_server(struct qd_server_t *server, struct qd_log_source_t *log);

/* Stop the HTTP server threads */
void qd_http_server_stop(qd_http_server_t*);

/* Free the HTTP server (stops threads if still running) */
void qd_http_server_free(qd_http_server_t*);

/* Listening for HTTP, thread safe. */
qd_lws_listener_t *qd_http_server_listen(qd_http_server_t *s, struct qd_listener_t *li);

/**
 * Closes an open http listener.
 */
void qd_lws_listener_close(qd_lws_listener_t *hl);

#endif // QD_HTTP_H

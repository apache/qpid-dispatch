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

#include <qpid/dispatch/log.h>
#include <qpid/dispatch/driver.h>
#include "http.h"

/* No HTTP implementation available. */

static qd_log_source_t *log = NULL;

void qd_http_connector_process(qdpn_connector_t *c) {}

qd_http_connector_t *qd_http_connector(qd_http_t *h, qdpn_connector_t *c) {

    return NULL;
}

qd_http_t *qd_http(qd_dispatch_t *d, qd_log_source_t *l) {
    log = l;
    qd_log(log, QD_LOG_WARNING, "HTTP support is not available");
    return NULL;
}

void qd_http_free(qd_http_t *h) {}

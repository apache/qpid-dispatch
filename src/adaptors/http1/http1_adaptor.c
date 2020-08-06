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

#include <qpid/dispatch/http1_lib.h>
#include <qpid/dispatch/protocol_adaptor.h>
#include <qpid/dispatch/message.h>
#include "adaptors/http_common.h"

#include <stdio.h>
#include <inttypes.h>


typedef struct qd_http1_adaptor_t {
    qdr_core_t               *core;
    qdr_protocol_adaptor_t   *adaptor;
    qd_http_lsnr_list_t       listeners;
    qd_http_connector_list_t  connectors;
    qd_log_source_t          *log;
} qd_http1_adaptor_t;

//static qd_http1_adaptor_t *http1_adaptor;

#define BUFF_BATCH 16


// dummy for now:
qd_http_lsnr_t *qd_http1_configure_listener(qd_dispatch_t *qd, const qd_http_bridge_config_t *config, qd_entity_t *entity)
{
    return 0;
}

void qd_http1_delete_listener(qd_dispatch_t *qd, qd_http_lsnr_t *listener) {}

qd_http_connector_t *qd_http1_configure_connector(qd_dispatch_t *qd, const qd_http_bridge_config_t *config, qd_entity_t *entity)
{
    return 0;
}

void qd_http1_delete_connector(qd_dispatch_t *qd, qd_http_connector_t *conn) {}


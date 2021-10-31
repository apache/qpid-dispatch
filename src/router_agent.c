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

#include "dispatch_private.h"

#include "qpid/dispatch.h"

#include <string.h>

const char *QD_ROUTER_TYPE = "router";

static const char *qd_router_mode_names[] = {
    "standalone",
    "interior",
    "edge",
    "endpoint"
};
ENUM_DEFINE(qd_router_mode, qd_router_mode_names);

QD_EXPORT qd_error_t qd_entity_refresh_router(qd_entity_t* entity, void *impl) {
    qd_dispatch_t *qd = (qd_dispatch_t*) impl;
    qd_router_t *router = qd->router;
    if (qd_entity_set_string(entity, "area", router->router_area) == 0 &&
        qd_entity_set_string(entity, "mode", qd_router_mode_name(router->router_mode)) == 0 &&
        qd_entity_set_long(entity, "addrCount", 0) == 0 &&
        qd_entity_set_long(entity, "linkCount", 0) == 0 &&
        qd_entity_set_long(entity, "nodeCount", 0) == 0
    )
        return QD_ERROR_NONE;
    return qd_error_code();
}


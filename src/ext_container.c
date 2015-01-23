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
#include "router_private.h"
#include "entity_cache.h"
#include <qpid/dispatch/ctools.h>
#include <qpid/dispatch/connection_manager.h>
#include <memory.h>
#include <stdio.h>

struct qd_external_container_t {
    DEQ_LINKS(qd_external_container_t);
    char *prefix;
    char *connector_name;
};

DEQ_DECLARE(qd_external_container_t, qd_external_container_list_t);

static qd_external_container_list_t ec_list = DEQ_EMPTY;

qd_external_container_t *qd_external_container(qd_router_t *router, const char *prefix, const char *connector_name)
{
    qd_external_container_t *ec = NEW(qd_external_container_t);

    if (ec) {
        DEQ_ITEM_INIT(ec);
        ec->prefix         = strdup(prefix);
        ec->connector_name = strdup(connector_name);
        DEQ_INSERT_TAIL(ec_list, ec);
    }

    return ec;
}


void qd_external_container_free(qd_external_container_t *ec)
{
    if (ec) {
        free(ec->prefix);
        free(ec->connector_name);
        DEQ_REMOVE(ec_list, ec);
        free(ec);
    }
}


void qd_external_container_free_all(void)
{
    qd_external_container_t *ec = DEQ_HEAD(ec_list);
    while (ec) {
        DEQ_REMOVE_HEAD(ec_list);
        qd_external_container_free(ec);
        ec = DEQ_HEAD(ec_list);
    }
}


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
#include "entity_cache.h"
#include <qpid/dispatch/ctools.h>
#include <qpid/dispatch/connection_manager.h>
#include <qpid/dispatch/timer.h>
#include <memory.h>
#include <stdio.h>

struct qd_external_container_t {
    DEQ_LINKS(qd_external_container_t);
    qd_dispatch_t *qd;
    char          *prefix;
    char          *connector_name;
    qd_timer_t    *timer;
};

DEQ_DECLARE(qd_external_container_t, qd_external_container_list_t);

static qd_external_container_list_t ec_list = DEQ_EMPTY;


static void qd_external_container_open_handler(void *context, qd_connection_t *conn)
{
    //const char *name = (char*) context;
}


static void qd_external_container_close_handler(void *context, qd_connection_t *conn)
{
    //const char *name = (char*) context;
}


static void qd_external_container_timer_handler(void *context)
{
    qd_external_container_t *ec = (qd_external_container_t*) context;
    qd_config_connector_t   *cc = qd_connection_manager_find_on_demand(ec->qd, ec->connector_name);
    if (cc) {
        qd_connection_manager_set_handlers(cc,
                                           qd_external_container_open_handler,
                                           qd_external_container_close_handler,
                                           (void*) ec->connector_name);
        qd_connection_manager_start_on_demand(ec->qd, cc);
    }
}


qd_external_container_t *qd_external_container(qd_dispatch_t *qd, const char *prefix, const char *connector_name)
{
    qd_external_container_t *ec = NEW(qd_external_container_t);

    if (ec) {
        DEQ_ITEM_INIT(ec);
        ec->qd             = qd;
        ec->prefix         = strdup(prefix);
        ec->connector_name = strdup(connector_name);
        ec->timer          = qd_timer(qd, qd_external_container_timer_handler, ec);
        DEQ_INSERT_TAIL(ec_list, ec);
        qd_timer_schedule(ec->timer, 0);
    }

    return ec;
}


void qd_external_container_free(qd_external_container_t *ec)
{
    if (ec) {
        free(ec->prefix);
        free(ec->connector_name);
        qd_timer_free(ec->timer);
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


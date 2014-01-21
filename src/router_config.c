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

#include <qpid/dispatch/python_embedded.h>
#include <qpid/dispatch.h>
#include <qpid/dispatch/config.h>
#include <qpid/dispatch/log.h>
#include "dispatch_private.h"
#include "router_private.h"

static const char *CONF_ADDRESS = "fixed-address";
static const char *MODULE       = "ROUTER";

void qd_router_configure(qd_router_t *router)
{
    if (!router->qd->config)
        return;

    int count = qd_config_item_count(router->qd->config, CONF_ADDRESS);

    router->config_addr_count = count;
    router->config_addrs      = NEW_ARRAY(qd_config_address_t, count);

    for (int idx = 0; idx < count; idx++) {
        const char *prefix = qd_config_item_value_string(router->qd->config, CONF_ADDRESS, idx, "prefix");
        const char *fanout = qd_config_item_value_string(router->qd->config, CONF_ADDRESS, idx, "fanout");
        const char *bias   = qd_config_item_value_string(router->qd->config, CONF_ADDRESS, idx, "bias");
        router->config_addrs[idx].prefix = (char*) malloc(strlen(prefix) + 1);
        if (prefix[0] == '/')
            strcpy(router->config_addrs[idx].prefix, &prefix[1]);
        else
            strcpy(router->config_addrs[idx].prefix, prefix);

        qd_address_semantics_t semantics = 0;
        if      (strcmp("multiple", fanout) == 0) semantics |= QD_FANOUT_MULTIPLE;
        else if (strcmp("single",   fanout) == 0) semantics |= QD_FANOUT_SINGLE;
        else
            assert(0);

        if      (strcmp("none",    bias) == 0) semantics |= QD_BIAS_NONE;
        else if (strcmp("closest", bias) == 0) semantics |= QD_BIAS_CLOSEST;
        else if (strcmp("spread",  bias) == 0) semantics |= QD_BIAS_SPREAD;
        else
            assert(0);

        router->config_addrs[idx].semantics = semantics;

        qd_log(MODULE, QD_LOG_INFO, "Configured Address: prefix=%s fanout=%s bias=%s", prefix, fanout, bias);
    }
}


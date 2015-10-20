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

#include "router_core_private.h"


qdr_core_t *qdr_core(void)
{
    qdr_core_t *core = NEW(qdr_core_t);
    ZERO(core);

    //
    // Set up the logging source for the router core
    //
    core->log = qd_log_source("ROUTER_CORE");

    //
    // Set up the threading support
    //
    core->cond    = sys_cond();
    core->lock    = sys_mutex();
    core->running = true;
    DEQ_INIT(core->action_list);
    core->thread  = sys_thread(router_core_thread, core);

    return core;
}


void qdr_core_free(qdr_core_t *core)
{
    //
    // Stop and join the thread
    //
    core->running = false;
    sys_cond_signal(core->cond);
    sys_thread_join(core->thread);

    //
    // Free the core resources
    //
    sys_thread_free(core->thread);
    sys_cond_free(core->cond);
    sys_mutex_free(core->lock);
    free(core);
}




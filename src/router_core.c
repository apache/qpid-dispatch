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
#include <qpid/dispatch/router_core.h>
#include <qpid/dispatch/threading.h>
#include <qpid/dispatch/log.h>
#include <memory.h>

/**
 * Creates a thread that is dedicated to managing and using the routing table.
 * The purpose of moving this function into one thread is to remove the widespread
 * lock contention that happens with synchrounous multi-threaded routing.
 *
 * This module owns, manages, and uses the router-link list and the address hash table
 */

struct qd_router_core_t {
    qd_log_source_t *log;
    sys_cond_t      *cond;
    sys_mutex_t     *lock;
    sys_thread_t    *thread;
    bool             running;
};


static void *router_core_thread(void *arg)
{
    qd_router_core_t *core = (qd_router_core_t*) arg;

    qd_log(core->log, QD_LOG_INFO, "Router Core thread running");

    sys_mutex_lock(core->lock);
    while (core->running) {
        sys_cond_wait(core->cond, core->lock);
        //
        // While there's work to do, do the work with the lock not held
        //
    }
    sys_mutex_unlock(core->lock);

    qd_log(core->log, QD_LOG_INFO, "Router Core thread exited");
    return 0;
}


qd_router_core_t *qd_router_core(void)
{
    qd_router_core_t *core = NEW(qd_router_core_t);
    memset(core, 0, sizeof(qd_router_core_t));

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
    core->thread  = sys_thread(router_core_thread, core);

    return core;
}


void qd_router_core_free(qd_router_core_t *core)
{
    core->running = false;
    sys_cond_signal(core->cond);
    sys_thread_join(core->thread);
    sys_thread_free(core->thread);
    sys_cond_free(core->cond);
    sys_mutex_free(core->lock);
    free(core);
}




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


/**
 * The following data structures are private to the core module.  They are defined
 * here to ensure their invisibiliy outside the core.
 */

typedef struct qdr_core_work_t {
    DEQ_LINKS(struct qdr_core_work_t);
} qdr_core_work_t;

ALLOC_DECLARE(qdr_core_work_t);
ALLOC_DEFINE(qdr_core_work_t);
DEQ_DECLARE(qdr_core_work_t, qdr_core_work_list_t);

struct qdr_core_t {
    qd_log_source_t      *log;
    sys_cond_t           *cond;
    sys_mutex_t          *lock;
    sys_thread_t         *thread;
    bool                  running;
    qdr_core_work_list_t  work_list;
};


static void router_core_do_work(qdr_core_t *core, qdr_core_work_t *work)
{
}


static void *router_core_thread(void *arg)
{
    qdr_core_t           *core = (qdr_core_t*) arg;
    qdr_core_work_list_t  work_list;
    qdr_core_work_t      *work;

    qd_log(core->log, QD_LOG_INFO, "Router Core thread running");
    while (core->running) {
        //
        // Use the lock only to protect the condition variable and the work list
        //
        sys_mutex_lock(core->lock);

        //
        // Block on the condition variable when there is no work to do
        //
        while (core->running && DEQ_IS_EMPTY(core->work_list))
            sys_cond_wait(core->cond, core->lock);

        //
        // Move the entire work list to a private list so we can process it without
        // holding the lock
        //
        DEQ_MOVE(core->work_list, work_list);
        sys_mutex_unlock(core->lock);

        //
        // Process and free all of the work items in the list
        //
        work = DEQ_HEAD(work_list);
        while (work) {
            DEQ_REMOVE_HEAD(work_list);

            if (core->running)
                router_core_do_work(core, work);

            free_qdr_core_work_t(work);
            work = DEQ_HEAD(work_list);
        }
    }

    qd_log(core->log, QD_LOG_INFO, "Router Core thread exited");
    return 0;
}


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
    DEQ_INIT(core->work_list);
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




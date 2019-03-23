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
#include "immediate_private.h"
#include "server_private.h"

#include <qpid/dispatch/threading.h>
#include <assert.h>

struct qd_immediate_t {
    qd_server_t *server;
    void (*handler)(void* context);
    void *context;
    bool armed;
};

/* Array rather than list for fast access and cache-coherence */
static qd_immediate_t immediates[256] = {{0}};
static size_t count = 0;
static sys_mutex_t *lock = NULL;
static bool visit_pending = false;
static size_t armed_count = 0;
static qd_server_t *qd_server;

void qd_immediate_initialize(void) {
    lock = sys_mutex();
}

void qd_immediate_finalize(void) {
    sys_mutex_free(lock);
    lock = 0;
}

qd_immediate_t *qd_immediate(qd_dispatch_t *qd, void (*handler)(void*), void* context) {
    sys_mutex_lock(lock);
    if (count >= sizeof(immediates)/sizeof(immediates[0])) {
        assert("exceeded max number of qd_immediate_t objects" == 0);
        return 0;
    }
    qd_immediate_t *i = &immediates[count++];
    i->server = qd ? qd->server : NULL;
    i->handler = handler;
    i->context = context;
    i->armed = false;
    if (!qd_server && i->server)
        qd_server = i->server;  // Remember server.
    sys_mutex_unlock(lock);
    return i;
}

void qd_immediate_arm(qd_immediate_t *i) {
    bool interrupt = false;
    sys_mutex_lock(lock);
    if (!i->armed) {
        i->armed = true;
        armed_count++;
    }
    if (!visit_pending) {
        visit_pending = true;
        interrupt = true;
    }
    sys_mutex_unlock(lock);
    if (interrupt && i->server) {
        qd_server_interrupt(i->server);
    }
}

void qd_immediate_schedule_visit(qd_server_t *s) {
    bool interrupt = false;
    sys_mutex_lock(lock);
    if (armed_count && !visit_pending) {
        visit_pending = true;
        interrupt = true;
    }
    sys_mutex_unlock(lock);
    if (interrupt) {
        qd_server_interrupt(s);
    }
}

void qd_immediate_disarm(qd_immediate_t *i) {
    sys_mutex_lock(lock);
    if (i->armed) {
      i->armed = false;
      armed_count--;
    }
    sys_mutex_unlock(lock);
}

void qd_immediate_set_armed(qd_immediate_t *i) {
    sys_mutex_lock(lock);
    if (!i->armed) {
      i->armed = true;
      armed_count++;
    }
    sys_mutex_unlock(lock);
}

void qd_immediate_free(qd_immediate_t *i) {
    /* Just disarm, its harmless to leave it in place. */
    qd_immediate_disarm(i);
}

void qd_immediate_visit() {
    bool interrupt = false;
    sys_mutex_lock(lock);
    for (qd_immediate_t *i = immediates; armed_count && i < immediates + count; ++i) {
        if (i->armed) {
            i->armed = false;
            armed_count--;
            sys_mutex_unlock(lock);
            i->handler(i->context);
            sys_mutex_lock(lock);
        }
    }
    if (armed_count > 0)
        interrupt = true;  /* A new arming while calling handlers.  Schedule a new visit. */
    else
        visit_pending = false;
    sys_mutex_unlock(lock);
    if (interrupt && qd_server) {
        qd_server_interrupt(qd_server);
    }
}

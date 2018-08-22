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

#include <stdio.h>
#include <qpid/dispatch/ctools.h>
#include "router_core_private.h"


ALLOC_DECLARE(qdr_core_timer_t);
ALLOC_DEFINE(qdr_core_timer_t);

qdr_core_timer_t *qdr_core_timer(qdr_core_t *core, qdr_timer_cb_t callback, void *timer_context)
{
    qdr_core_timer_t *timer = new_qdr_core_timer_t();
    if (!timer)
        return 0;

    timer->handler = callback;
    timer->context = timer_context;

    DEQ_ITEM_INIT(timer);

    return timer;
}


void qdr_core_timer_schedule(qdr_core_t *core, qdr_core_timer_t *timer, int delay)
{
    /* Invariant: time_before == total time up to but not including ptr */
    qdr_core_timer_t *ptr = DEQ_HEAD(core->scheduled_timers);
    int time_before = 0;
    while (ptr && time_before + ptr->delta_time < delay) {
        time_before += ptr->delta_time;
        ptr = ptr->next;
    }

    /* ptr is the first timer to exceed duration or NULL if we ran out */
    if (!ptr) {
        timer->delta_time = delay - time_before;
        DEQ_INSERT_TAIL(core->scheduled_timers, timer);
    } else {
        timer->delta_time = delay - time_before;
        ptr->delta_time -= timer->delta_time;
        ptr = ptr->prev;
        if (ptr)
            DEQ_INSERT_AFTER(core->scheduled_timers, timer, ptr);
        else
            DEQ_INSERT_HEAD(core->scheduled_timers, timer);
    }
    timer->scheduled = true;
}


void qdr_core_timer_cancel(qdr_core_t *core, qdr_core_timer_t *timer)
{
    if (timer->scheduled) {
        if (timer->next)
            timer->next->delta_time += timer->delta_time;
        DEQ_REMOVE(core->scheduled_timers, timer);
        timer->scheduled = false;
    }
}

void qdr_core_timer_visit(qdr_core_t *core)
{
    // The core timer is fired every second. so we just visit the head and process that timeer
    qdr_core_timer_t *timer = DEQ_HEAD(core->scheduled_timers);

    while(timer && timer->delta_time == 0 && timer->scheduled) {
        if (timer->handler)
            timer->handler(core, timer->context);
        DEQ_REMOVE_HEAD(core->scheduled_timers);
        timer = DEQ_NEXT(timer);
    }

    if (timer)
        timer->delta_time -= 1;

}


void qdr_core_timer_free(qdr_core_t *core, qdr_core_timer_t *timer)
{
    if (!timer)
        return;

    qdr_core_timer_cancel(core, timer);
    free_qdr_core_timer_t(timer);
}

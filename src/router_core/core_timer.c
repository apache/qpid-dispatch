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

#include "qpid/dispatch/ctools.h"

ALLOC_DEFINE(qdr_core_timer_t);

void qdr_process_tick_CT(qdr_core_t *core, qdr_action_t *action, bool discard);


void qdr_process_tick(qdr_core_t *core)
{
    qdr_action_t *action = qdr_action(qdr_process_tick_CT, "process_tick");
    qdr_action_enqueue(core, action);
}



qdr_core_timer_t *qdr_core_timer_CT(qdr_core_t *core, qdr_timer_cb_t callback, void *timer_context)
{
    qdr_core_timer_t *timer = new_qdr_core_timer_t();
    if (!timer)
        return 0;

    ZERO(timer);
    timer->handler = callback;
    timer->context = timer_context;
    DEQ_ITEM_INIT(timer);

    return timer;
}


void qdr_core_timer_schedule_CT(qdr_core_t *core, qdr_core_timer_t *timer, uint32_t delay)
{
    if (timer->scheduled)
        qdr_core_timer_cancel_CT(core, timer);

    qdr_core_timer_t *ptr         = DEQ_HEAD(core->scheduled_timers);
    uint32_t          time_before = 0;

    while (ptr && time_before + ptr->delta_time_seconds <= delay) {
        time_before += ptr->delta_time_seconds;
        ptr = DEQ_NEXT(ptr);
    }

    //
    // ptr is the first timer to exceed duration or NULL if we ran out
    //
    timer->delta_time_seconds = delay - time_before;
    timer->scheduled          = true;

    if (!ptr)
        DEQ_INSERT_TAIL(core->scheduled_timers, timer);
    else {
        ptr->delta_time_seconds -= timer->delta_time_seconds;
        ptr = DEQ_PREV(ptr);
        if (ptr)
            DEQ_INSERT_AFTER(core->scheduled_timers, timer, ptr);
        else
            DEQ_INSERT_HEAD(core->scheduled_timers, timer);
    }
}


void qdr_core_timer_cancel_CT(qdr_core_t *core, qdr_core_timer_t *timer)
{
    if (timer->scheduled) {
        timer->scheduled = false;
        if (DEQ_NEXT(timer)) {
            DEQ_NEXT(timer)->delta_time_seconds += timer->delta_time_seconds;
        }
        DEQ_REMOVE(core->scheduled_timers, timer);
    }
}


void qdr_core_timer_free_CT(qdr_core_t *core, qdr_core_timer_t *timer)
{
    if (!timer)
        return;

    qdr_core_timer_cancel_CT(core, timer);
    free_qdr_core_timer_t(timer);
}


void qdr_process_tick_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    if (discard)
        return;

    sys_atomic_inc(&core->uptime_ticks);

    qdr_core_timer_t *timer = DEQ_HEAD(core->scheduled_timers);
    qdr_core_timer_t *timer_next = 0;

    while (timer && timer->delta_time_seconds == 0) {
        assert(timer->scheduled);
        timer->scheduled = false;
        timer_next = DEQ_NEXT(timer);
        DEQ_REMOVE(core->scheduled_timers, timer);

        if (timer->handler)
            timer->handler(core, timer->context);

        timer = timer_next;
    }

    if (timer)
        timer->delta_time_seconds--;
}


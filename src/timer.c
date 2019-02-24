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
#include "timer_private.h"
#include "server_private.h"
#include <qpid/dispatch/ctools.h>
#include <qpid/dispatch/threading.h>
#include <qpid/dispatch/alloc.h>
#include <assert.h>
#include <stdio.h>
#include <time.h>

static sys_mutex_t     *lock = NULL;
static qd_timer_list_t  idle_timers = {0};
static qd_timer_list_t  scheduled_timers = {0};
/* Timers have relative delta_time measured from the previous timer.
 * The delta_time of the first timer on the queue is measured from timer_base.
 */
static qd_timestamp_t   time_base = 0;

ALLOC_DECLARE(qd_timer_t);
ALLOC_DEFINE(qd_timer_t);

/// For tests only
sys_mutex_t* qd_timer_lock() { return lock; }

//=========================================================================
// Private static functions
//=========================================================================

static void timer_cancel_LH(qd_timer_t *timer)
{
    if (timer->scheduled) {
        if (timer->next)
            timer->next->delta_time += timer->delta_time;
        DEQ_REMOVE(scheduled_timers, timer);
        DEQ_INSERT_TAIL(idle_timers, timer);
        timer->scheduled = false;
    }
}

/* Adjust timer's time_base and delays for the current time. */
static void timer_adjust_now_LH()
{
    qd_timestamp_t now = qd_timer_now();
    if (time_base != 0 && now > time_base) {
        qd_duration_t delta = now - time_base;
        /* Adjust timer delays by removing duration delta, starting from timer. */
        for (qd_timer_t *timer = DEQ_HEAD(scheduled_timers); delta > 0 && timer; timer = DEQ_NEXT(timer)) {
            if (timer->delta_time >= delta) {
                timer->delta_time -= delta;
                delta = 0;
            } else {
                delta -= timer->delta_time;
                timer->delta_time = 0; /* Ready to fire */
            }
        }
    }
    time_base = now;
}


//=========================================================================
// Public Functions from timer.h
//=========================================================================


qd_timer_t *qd_timer(qd_dispatch_t *qd, qd_timer_cb_t cb, void* context)
{
    qd_timer_t *timer = new_qd_timer_t();
    if (!timer)
        return 0;

    DEQ_ITEM_INIT(timer);

    timer->server     = qd ? qd->server : 0;
    timer->handler    = cb;
    timer->context    = context;
    timer->delta_time = 0;
    timer->scheduled  = false;
    sys_mutex_lock(lock);
    DEQ_INSERT_TAIL(idle_timers, timer);
    sys_mutex_unlock(lock);

    return timer;
}


void qd_timer_free(qd_timer_t *timer)
{
    if (!timer) return;
    sys_mutex_lock(lock);
    timer_cancel_LH(timer);
    DEQ_REMOVE(idle_timers, timer);
    sys_mutex_unlock(lock);
    free_qd_timer_t(timer);
}


qd_timestamp_t qd_timer_now() {
    struct timespec tv;
    clock_gettime(CLOCK_MONOTONIC, &tv);
    return ((qd_timestamp_t)tv.tv_sec) * 1000 + tv.tv_nsec / 1000000;
}


void qd_timer_schedule(qd_timer_t *timer, qd_duration_t duration)
{
    sys_mutex_lock(lock);
    timer_cancel_LH(timer);  // Timer is now on the idle list
    DEQ_REMOVE(idle_timers, timer);

    //
    // Find the insert point in the schedule.
    //
    timer_adjust_now_LH();   /* Adjust the timers for current time */

    /* Invariant: time_before == total time up to but not including ptr */
    qd_timer_t *ptr = DEQ_HEAD(scheduled_timers);
    qd_duration_t time_before = 0;
    while (ptr && time_before + ptr->delta_time < duration) {
        time_before += ptr->delta_time;
        ptr = ptr->next;
    }
    /* ptr is the first timer to exceed duration or NULL if we ran out */
    if (!ptr) {
        timer->delta_time = duration - time_before;
        DEQ_INSERT_TAIL(scheduled_timers, timer);
    } else {
        timer->delta_time = duration - time_before;
        ptr->delta_time -= timer->delta_time;
        ptr = ptr->prev;
        if (ptr)
            DEQ_INSERT_AFTER(scheduled_timers, timer, ptr);
        else
            DEQ_INSERT_HEAD(scheduled_timers, timer);
    }
    timer->scheduled = true;

    qd_timer_t *first = DEQ_HEAD(scheduled_timers);
    qd_server_timeout(first->server, first->delta_time);
    sys_mutex_unlock(lock);
}


void qd_timer_cancel(qd_timer_t *timer)
{
    sys_mutex_lock(lock);
    timer_cancel_LH(timer);
    sys_mutex_unlock(lock);
}


//=========================================================================
// Private Functions from timer_private.h
//=========================================================================


void qd_timer_initialize(sys_mutex_t *server_lock)
{
    lock = server_lock;
    DEQ_INIT(idle_timers);
    DEQ_INIT(scheduled_timers);
    time_base = 0;
}


void qd_timer_finalize(void)
{
    lock = 0;
}


/* Execute all timers that are ready and set up next timeout. */
void qd_timer_visit()
{
    sys_mutex_lock(lock);
    timer_adjust_now_LH();
    qd_timer_t *timer = DEQ_HEAD(scheduled_timers);
    while (timer && timer->delta_time == 0) {
        timer_cancel_LH(timer); /* Removes timer from scheduled_timers */
        sys_mutex_unlock(lock);
        timer->handler(timer->context); /* Call the handler outside the lock, may re-schedule */
        sys_mutex_lock(lock);
        timer = DEQ_HEAD(scheduled_timers);
    }
    qd_timer_t *first = DEQ_HEAD(scheduled_timers);
    if (first) {
        qd_server_timeout(first->server, first->delta_time);
    }
    sys_mutex_unlock(lock);
}

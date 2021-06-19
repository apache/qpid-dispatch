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

#include "qpid/dispatch/timer.h"

#include "dispatch_private.h"
#include "server_private.h"
#include "timer_private.h"

#include "qpid/dispatch/alloc.h"
#include "qpid/dispatch/ctools.h"
#include "qpid/dispatch/atomic.h"

#include <assert.h>
#include <stdio.h>
#include <time.h>


// timer state machine
//
// IDLE: initial state or state immediately after the callback finishes
// running.
//
// SCHEDULED: timer has been scheduled to run.  It is on the scheduled_timer
// list.  Valid next states are IDLE (if timer canceled), RUNNING, or DELETED.
//
// RUNNING: the timer callback is executing.  Valid next states are IDLE,
// SCHEDULED, BLOCKED or DELETED.  The address of the thread executing the
// callback is available in callback_thread
//
// BLOCKED: the callback is executing and another thread is blocked waiting for
// the callback to complete.  This state is used when cancelling or freeing a
// running timer.
//
// DELETED: final state.  There are no valid next states.
//
typedef enum {
    QD_TIMER_STATE_IDLE,
    QD_TIMER_STATE_SCHEDULED,
    QD_TIMER_STATE_RUNNING,
    QD_TIMER_STATE_BLOCKED,
    QD_TIMER_STATE_DELETED
} qd_timer_state_t;


struct qd_timer_t {
    DEQ_LINKS(qd_timer_t);
    qd_server_t      *server;
    qd_timer_cb_t     handler;
    void             *context;
    sys_cond_t       *condition;
    sys_atomic_t      ref_count; // referenced by user and when on scheduled list
    qd_timestamp_t    delta_time;
    qd_timer_state_t  state;
};

DEQ_DECLARE(qd_timer_t, qd_timer_list_t);

static sys_mutex_t     *lock = NULL;
static qd_timer_list_t  scheduled_timers = {0};

// thread currently running timer callbacks or 0 if callbacks are not running
static sys_thread_t    *callback_thread = 0;

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

// returns true if timer removed from scheduled list
static bool timer_cancel_LH(qd_timer_t *timer)
{
    if (timer->state == QD_TIMER_STATE_SCHEDULED) {
        if (timer->next)
            timer->next->delta_time += timer->delta_time;
        DEQ_REMOVE(scheduled_timers, timer);
        timer->state = QD_TIMER_STATE_IDLE;
        return true;
    }
    return false;
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


static void timer_decref_LH(qd_timer_t *timer)
{
    assert(sys_atomic_get(&timer->ref_count) > 0);
    if (sys_atomic_dec(&timer->ref_count) == 1) {
        assert(timer->state != QD_TIMER_STATE_SCHEDULED);
        sys_cond_free(timer->condition);
        sys_atomic_destroy(&timer->ref_count);
        free_qd_timer_t(timer);
    }
}


//=========================================================================
// Public Functions from timer.h
//=========================================================================


qd_timer_t *qd_timer(qd_dispatch_t *qd, qd_timer_cb_t cb, void* context)
{
    qd_timer_t *timer = new_qd_timer_t();
    if (!timer)
        return 0;

    sys_cond_t *cond = sys_cond();
    if (!cond) {
        free_qd_timer_t(timer);
        return 0;
    }

    DEQ_ITEM_INIT(timer);

    timer->server     = qd ? qd->server : 0;
    timer->handler    = cb;
    timer->context    = context;
    timer->delta_time = 0;
    timer->condition  = cond;
    timer->state      = QD_TIMER_STATE_IDLE;
    sys_atomic_init(&timer->ref_count, 1);

    return timer;
}


void qd_timer_free(qd_timer_t *timer)
{
    if (!timer) return;

    sys_mutex_lock(lock);

    assert(timer->state != QD_TIMER_STATE_DELETED);  // double free!!!

    if (timer->state == QD_TIMER_STATE_RUNNING) {
        if (sys_thread_self() != callback_thread) {
            // Another thread is running the callback (see qd_timer_visit())
            // Wait until the callback finishes
            timer->state = QD_TIMER_STATE_BLOCKED;
            sys_cond_wait(timer->condition, lock);
        }
    }

    // we can safely free the timer since the callback is not running

    if (timer_cancel_LH(timer)) {
        // removed from scheduled_timers, so drop ref_count
        assert(sys_atomic_get(&timer->ref_count) > 1);  // expect caller holds a ref_count
        timer_decref_LH(timer);
    }

    timer->state = QD_TIMER_STATE_DELETED;
    timer_decref_LH(timer);  // now drop caller ref_count
    sys_mutex_unlock(lock);
}


__attribute__((weak)) // permit replacement by dummy implementation in unit_tests
qd_timestamp_t qd_timer_now()
{
    struct timespec tv;
    clock_gettime(CLOCK_MONOTONIC, &tv);
    return ((qd_timestamp_t)tv.tv_sec) * 1000 + tv.tv_nsec / 1000000;
}

void qd_timer_schedule(qd_timer_t *timer, qd_duration_t duration)
{
    sys_mutex_lock(lock);

    assert(timer->state != QD_TIMER_STATE_DELETED);
    const bool was_scheduled = timer_cancel_LH(timer);

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

    timer->state = QD_TIMER_STATE_SCHEDULED;
    if (!was_scheduled) {
        // scheduled_timers list reference:
        sys_atomic_inc(&timer->ref_count);
    }

    qd_timer_t *first = DEQ_HEAD(scheduled_timers);
    qd_server_timeout(first->server, first->delta_time);
    sys_mutex_unlock(lock);
}


void qd_timer_cancel(qd_timer_t *timer)
{
    sys_mutex_lock(lock);

    if (timer->state == QD_TIMER_STATE_RUNNING) {
        assert(sys_thread_self() != callback_thread);  // cancel within callback not allowed
        timer->state = QD_TIMER_STATE_BLOCKED;
        sys_cond_wait(timer->condition, lock);
    }

    // timer may have been resheduled before wait returns
    const bool need_decref = timer_cancel_LH(timer);
    timer->state = QD_TIMER_STATE_IDLE;
    if (need_decref)  // was on scheduled list
        timer_decref_LH(timer);

    sys_mutex_unlock(lock);
}


//=========================================================================
// Private Functions from timer_private.h
//=========================================================================


void qd_timer_initialize(sys_mutex_t *server_lock)
{
    lock = server_lock;
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
    callback_thread = sys_thread_self();
    timer_adjust_now_LH();
    qd_timer_t *timer = DEQ_HEAD(scheduled_timers);
    while (timer && timer->delta_time == 0) {
        // Remove timer from scheduled_timers but keep ref_count
        assert(timer->state == QD_TIMER_STATE_SCHEDULED);
        // note: still holding scheduled_timers refcount
        timer_cancel_LH(timer);
        timer->state = QD_TIMER_STATE_RUNNING;
        sys_mutex_unlock(lock);

        /* The callback may reschedule or delete the timer while the lock is
         * dropped.  Attempting to delete the timer now will cause the caller to
         * block until the callback is done.
         */
        timer->handler(timer->context);

        sys_mutex_lock(lock);
        if (timer->state == QD_TIMER_STATE_BLOCKED) {
            sys_cond_signal(timer->condition);
            // expect blocked caller sets timer->state
        } else if (timer->state == QD_TIMER_STATE_RUNNING) {
            timer->state = QD_TIMER_STATE_IDLE;
        }

        // now drop scheduled_timers reference:
        timer_decref_LH(timer);
        timer = DEQ_HEAD(scheduled_timers);
    }
    qd_timer_t *first = DEQ_HEAD(scheduled_timers);
    if (first) {
        qd_server_timeout(first->server, first->delta_time);
    }
    callback_thread = 0;
    sys_mutex_unlock(lock);
}

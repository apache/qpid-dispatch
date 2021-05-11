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
#include "test_case.h"
#include "timer_private.h"

#include "qpid/dispatch/alloc.h"
#include "qpid/dispatch/atomic.h"
#include "qpid/dispatch/threading.h"
#include "qpid/dispatch/timer.h"

#include <stdio.h>
#include <sys/select.h>

// test the interaction of threads and timers
//
// This test cannot be part of the unit_test executable since unit_test
// overrides some of the timer-private functions and this test needs to
// exercise the "real" versions of those functions.


// overide qd_server_timeout - this test uses ticker_thread instead of proactor
// so this is not needed
void qd_server_timeout(qd_server_t *server, qd_duration_t duration)
{
}


// Timer thread - polls the timers every 10 msecs
//
static sys_mutex_t *ticker_lock = 0;
static bool done = false;
static void *ticker_thread(void *arg)
{
    struct timeval period;

    sys_mutex_lock(ticker_lock);
    while (!done) {
        // 10 msec sleep
        sys_mutex_unlock(ticker_lock);
        period.tv_sec = 0;
        period.tv_usec = 10 * 1000;
        select(0, 0, 0, 0, &period);
        qd_timer_visit();
        sys_mutex_lock(ticker_lock);
    }
    sys_mutex_unlock(ticker_lock);
    return 0;
}


// global event for synchronizing with timer callback
//
static struct event_t {
    sys_mutex_t *m;
    sys_cond_t  *c;
} event;

static void test_setup()
{
    event.m = sys_mutex();
    event.c = sys_cond();
}

static void test_cleanup()
{
    sys_mutex_free(event.m);
    event.m = 0;
    sys_cond_free(event.c);
    event.c = 0;
}


//
// simple set and expire test
//

static void test_simple_cb(void *context)
{
    int *iptr = (int *)context;

    sys_mutex_lock(event.m);  // block until test_simple waits
    *iptr = 2;
    sys_cond_signal(event.c);
    sys_mutex_unlock(event.m);
}

static int test_simple(const char *name)
{
    int i = 1;
    int result = 0;

    test_setup();

    qd_timer_t *t = qd_timer(0, test_simple_cb, (void *)&i);
    sys_mutex_lock(event.m);

    qd_timestamp_t start = qd_timer_now();
    qd_timer_schedule(t, 100);
    sys_cond_wait(event.c, event.m);   // wait for cb to finish
    qd_timestamp_t stop = qd_timer_now();
    if (i != 2) {
        fprintf(stderr, "%s failed: expected timer to run\n", name);
        result = 1;
    }

    if ((stop - start) < 100) {
        fprintf(stderr, "%s failed: expected timer ran too soon\n", name);
        result = 1;
    }
    sys_mutex_unlock(event.m);
    qd_timer_free(t);

    test_cleanup();
    fprintf(stderr, "Test test_simple: %s\n", result ? "FAILED" : "ok");
    return result;
}


//
// Test rescheduling from within the callback
//

static qd_timer_t *reschedule_timer;

static void test_reschedule_internal_cb(void *context)
{
    int *iptr = (int *)context;

    switch (*iptr) {
    case 1:  // first time in
        (*iptr) += 1;
        qd_timer_schedule(reschedule_timer, 100);
        break;
    case 2:
        (*iptr) += 1;
        sys_mutex_lock(event.m);
        sys_cond_signal(event.c);
        sys_mutex_unlock(event.m);
        break;
    }
}

static int test_reschedule_internal(const char *name)
{
    int i = 1;
    int result = 0;

    test_setup();
    reschedule_timer = qd_timer(0, test_reschedule_internal_cb, (void *)&i);
    sys_mutex_lock(event.m);
    qd_timestamp_t start = qd_timer_now();
    qd_timer_schedule(reschedule_timer, 100);
    sys_cond_wait(event.c, event.m);   // wait for cb to finish
    qd_timestamp_t stop = qd_timer_now();

    if (i != 3) {
        fprintf(stderr, "%s failed: expected timer to reschedule\n", name);
        result = 1;
    }

    if ((stop - start) < 200) {
        fprintf(stderr, "%s failed: timer expired too soon\n", name);
        result = 1;
    }
    sys_mutex_unlock(event.m);
    qd_timer_free(reschedule_timer);

    test_cleanup();
    fprintf(stderr, "Test reschedule_internal: %s\n", result ? "FAILED" : "ok");
    return result;
}


//
// Test freeing timer from within the callback
//

static qd_timer_t *free_timer;

static void test_free_internal_cb(void *context)
{
    sys_mutex_lock(event.m);
    qd_timer_free(free_timer);
    free_timer = 0;

    sys_cond_signal(event.c);
    sys_mutex_unlock(event.m);
}

static int test_free_internal(const char *name)
{
    int result = 0;

    test_setup();
    free_timer = qd_timer(0, test_free_internal_cb, 0);
    sys_mutex_lock(event.m);
    qd_timestamp_t start = qd_timer_now();
    qd_timer_schedule(free_timer, 100);
    sys_cond_wait(event.c, event.m);   // wait for cb to finish
    qd_timestamp_t stop = qd_timer_now();
    if ((stop - start) < 100) {
        fprintf(stderr, "%s failed: timer expired too soon\n", name);
        result = 1;
    }
    sys_mutex_unlock(event.m);
    if (free_timer != 0) {
        fprintf(stderr, "%s failed: timer free failed\n", name);
        result = 1;
    }

    test_cleanup();
    fprintf(stderr, "Test free_internal: %s\n", result ? "FAILED" : "ok");
    return result;
}


//
// Test freeing timer before it can run
//

static void test_prefree_cb(void *context)
{
    abort();  // should never run
}

static int test_prefree(const char *name)
{
    int result = 0;

    test_setup();
    qd_timer_t *t = qd_timer(0, test_prefree_cb, 0);
    qd_timestamp_t start = qd_timer_now();
    qd_timer_schedule(t, 1000 * 60);   // looong time
    qd_timer_free(t);
    qd_timestamp_t stop = qd_timer_now();
    if ((stop - start) > 1000 * 60) {
        fprintf(stderr, "%s failed: free blocked\n", name);
        result = 1;
    }

    test_cleanup();
    fprintf(stderr, "Test prefree: %s\n", result ? "FAILED" : "ok");
    return result;
}


//
// Test canceling timer before it can run
//

static void test_early_cancel_cb(void *context)
{
    abort();  // should never run
}

static int test_early_cancel(const char *name)
{
    int result = 0;

    test_setup();
    qd_timer_t *t = qd_timer(0, test_early_cancel_cb, 0);
    qd_timestamp_t start = qd_timer_now();
    qd_timer_schedule(t, 1000 * 60);   // looong time
    qd_timer_cancel(t);
    qd_timestamp_t stop = qd_timer_now();
    if ((stop - start) > 1000 * 60) {
        fprintf(stderr, "%s failed: free blocked\n", name);
        result = 1;
    }
    qd_timer_free(t);

    test_cleanup();
    fprintf(stderr, "Test early cancel: %s\n", result ? "FAILED" : "ok");
    return result;
}


//
// Test cancel then re-arm timer
//

static void test_rerun_cb(void *context)
{
    int *iptr = (int *)context;
    if (*iptr != 2) {
        abort();  // should never run
    }
    (*iptr) = 3;
    sys_mutex_lock(event.m);
    sys_cond_signal(event.c);
    sys_mutex_unlock(event.m);
}

static int test_rerun(const char *name)
{
    int result = 0;
    int i = 1;

    test_setup();
    qd_timer_t *t = qd_timer(0, test_rerun_cb, &i);
    qd_timestamp_t start = qd_timer_now();
    qd_timer_schedule(t, 1000 * 60);   // looong time
    qd_timer_cancel(t);
    qd_timestamp_t stop = qd_timer_now();
    if ((stop - start) > 1000 * 60) {
        fprintf(stderr, "%s failed: free blocked\n", name);
        result = 1;
    }

    sys_mutex_lock(event.m);

    i = 2;
    start = qd_timer_now();
    qd_timer_schedule(t, 100);
    sys_cond_wait(event.c, event.m);   // wait for cb to finish
    stop = qd_timer_now();
    if ((stop - start) < 100) {
        fprintf(stderr, "%s failed: expected timer ran too soon\n", name);
        result = 1;
    }
    if (i != 3) {
        fprintf(stderr, "%s failed: expected callback to finish\n", name);
        result = 1;
    }
    sys_mutex_unlock(event.m);
    qd_timer_free(t);

    test_cleanup();
    fprintf(stderr, "Test rerun: %s\n", result ? "FAILED" : "ok");
    return result;
}


//
// Test that canceling a running timer will block until callback completes
//

static void long_running_cb(void *context)
{
    struct timeval period = {.tv_sec = 0,
                             .tv_usec = 2000 * 1000}; // 2 sec

    // wake caller
    sys_mutex_lock(event.m);
    sys_cond_signal(event.c);
    sys_mutex_unlock(event.m);

    // sleep for 2 second, cancel should block for this
    select(0, 0, 0, 0, &period);
    sys_atomic_inc((sys_atomic_t *)context);
}

static int test_sync_cancel(const char *name)
{
    int result = 0;
    sys_atomic_t flag;
    sys_atomic_init(&flag, 1);

    test_setup();
    qd_timer_t *t = qd_timer(0, long_running_cb, (void *)&flag);

    sys_mutex_lock(event.m);
    qd_timer_schedule(t, 10);
    sys_cond_wait(event.c, event.m);   // wait for cb to start
    qd_timer_cancel(t);   // expected to block until cb finishes
    if (sys_atomic_get(&flag) != 2) {
        fprintf(stderr, "%s failed: callback still running\n", name);
        result = 1;
    }
    sys_mutex_unlock(event.m);
    qd_timer_free(t);

    test_cleanup();
    fprintf(stderr, "Test sync cancel: %s\n", result ? "FAILED" : "ok");
    return result;
}


//
// Test that freeing a running timer will block until callback completes
//

static int test_sync_free(const char *name)
{
    int result = 0;
    sys_atomic_t flag;
    sys_atomic_init(&flag, 1);

    test_setup();
    qd_timer_t *t = qd_timer(0, long_running_cb, (void *)&flag);

    sys_mutex_lock(event.m);
    qd_timer_schedule(t, 10);
    sys_cond_wait(event.c, event.m);   // wait for cb to start
    qd_timer_free(t);   // expected to block until cb finishes
    if (sys_atomic_get(&flag) != 2) {
        fprintf(stderr, "%s failed: callback still running\n", name);
        result = 1;
    }
    sys_mutex_unlock(event.m);

    test_cleanup();
    fprintf(stderr, "Test sync free: %s\n", result ? "FAILED" : "ok");
    return result;
}


int main(int argc, char *argv[])
{
    int result = 0;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <config-file>\n", argv[0]);
        exit(1);
    }

    // Call qd_dispatch() first initialize allocator used by other tests.
    qd_dispatch_t *qd = qd_dispatch(0, false);

    qd_dispatch_validate_config(argv[1]);
    if (qd_error_code()) {
        printf("Config failed: %s\n", qd_error_message());
        return 1;
    }

    qd_dispatch_load_config(qd, argv[1]);
    if (qd_error_code()) {
        printf("Config failed: %s\n", qd_error_message());
        return 1;
    }

    ticker_lock = sys_mutex();

    sys_thread_t *ticker = sys_thread(ticker_thread, 0);

    result = test_simple(argv[0]);
    result += test_reschedule_internal(argv[0]);
    result += test_free_internal(argv[0]);
    result += test_prefree(argv[0]);
    result += test_early_cancel(argv[0]);
    result += test_rerun(argv[0]);
    result += test_sync_cancel(argv[0]);
    result += test_sync_free(argv[0]);

    sys_mutex_lock(ticker_lock);
    done = true;
    sys_mutex_unlock(ticker_lock);

    sys_thread_join(ticker);
    sys_thread_free(ticker);

    qd_dispatch_free(qd);       // dispatch_free last.

    return result;
}

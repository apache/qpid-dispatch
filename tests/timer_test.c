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
#include "test_case.h"
#include "timer_private.h"

#include <stdio.h>

static unsigned long    fire_mask;
static unsigned long    fired;
static qd_duration_t    timeout;
static long             time_value;
static qd_timer_t      *timers[16];


/* Dummy out the now and timeout functions */
qd_timestamp_t qd_timer_now() {
    return time_value;
}

void qd_server_timeout(qd_server_t *server, qd_duration_t duration) {
    timeout = duration;
}

static void on_timer(void *context)
{
    fire_mask |= (unsigned long) context;
    ++fired;
}


static char* test_quiet(void *context)
{
    fire_mask = fired = 0;
    qd_timer_visit();
    qd_timer_visit();
    if (fired != 0 && fire_mask != 0)
        return "Expected zero timers fired";
    return 0;
}

static char* test_immediate(void *context)
{
    fire_mask = fired = 0;
    qd_timer_schedule(timers[0], 0);
    if (fired != 0)  return "Premature firing";
    qd_timer_visit();
    if (fired != 1)  return "Expected 1 firing";
    if (fire_mask != 1)  return "Incorrect fire mask";

    return 0;
}


static char* test_immediate_reschedule(void *context)
{
    fire_mask = fired = 0;
    qd_timer_schedule(timers[0], 0);
    qd_timer_schedule(timers[0], 0);
    qd_timer_visit();

    if (fired > 1) return "pass 1 - Too many firings";
    if (fire_mask != 1) return "pass 1 - Incorrect fire mask";

    fire_mask = fired = 0;
    qd_timer_schedule(timers[0], 0);
    qd_timer_schedule(timers[0], 0);
    qd_timer_visit();

    if (fired > 1) return "pass 2 - Too many firings";
    if (fire_mask != 1)  return "pass 2 - Incorrect fire mask";

    return 0;
}


static char* test_immediate_plus_delayed(void *context)
{
    fire_mask = fired = 0;
    qd_timer_schedule(timers[0], 0);
    qd_timer_schedule(timers[1], 5);
    qd_timer_visit();

    if (fired > 1) return "Too many firings";
    if (fire_mask != 1)  return "Incorrect fire mask 1";

    time_value += 8;
    qd_timer_visit();

    if (fired < 1) return "Delayed Failed to fire";
    if (fire_mask != 3)  return "Incorrect fire mask 3";

    return 0;
}


static char* test_single(void *context)
{
    fire_mask = fired = 0;

    qd_timer_schedule(timers[0], 2);
    if (timeout != 2) return "Incorrect timeout";
    qd_timer_visit();
    if (fired > 0) return "Premature firing";
    time_value++;
    qd_timer_visit();
    if (fired > 0) return "Premature firing 2";
    time_value++;
    qd_timer_visit();
    if (fire_mask != 1)  return "Incorrect fire mask";

    fire_mask = fired = 0;
    time_value++;
    qd_timer_visit();
    time_value++;
    qd_timer_visit();
    if (fired != 0) return "Spurious fires";

    return 0;
}


static char* test_two_inorder(void *context)
{
    fire_mask = fired = 0;

    qd_timer_schedule(timers[0], 2);
    if (timeout != 2) return "bad timeout 2";
    qd_timer_schedule(timers[1], 4);
    if (timeout != 2) return "bad timeout still 2";
    time_value += 2;
    qd_timer_visit();

    if (fire_mask & 2) return "Second fired prematurely";
    if (fire_mask != 1) return "Incorrect fire mask 1";

    time_value += 2;
    qd_timer_visit();

    if (fire_mask != 3)  return "Incorrect fire mask 3";

    return 0;
}


static char* test_two_reverse(void *context)
{
    fire_mask = fired = 0;

    qd_timer_schedule(timers[0], 4);
    qd_timer_schedule(timers[1], 2);
    time_value += 2;
    qd_timer_visit();

    if (fired < 1) return "First failed to fire";
    if (fired > 1) return "Second fired prematurely";
    if (fire_mask != 2) return "Incorrect fire mask 2";

    time_value += 2;
    qd_timer_visit();

    if (fired < 1) return "Second failed to fire";
    if (fire_mask != 3)  return "Incorrect fire mask 3";

    return 0;
}


static char* test_two_duplicate(void *context)
{
    fire_mask = fired = 0;

    qd_timer_schedule(timers[0], 2);
    qd_timer_schedule(timers[1], 2);
    time_value += 2;
    qd_timer_visit();
    if (fired != 2) return "Expected two firings";
    if (fire_mask != 3) return "Incorrect fire mask 3";

    fire_mask = fired = 0;
    time_value += 2;
    qd_timer_visit();
    if (fired > 0) return "Spurious timer fires";

    return 0;
}


static char* test_separated(void *context)
{
    fire_mask = fired = 0;
    qd_timer_schedule(timers[0], 2);
    qd_timer_schedule(timers[1], 4);
    time_value +=  2;
    qd_timer_visit();

    if (fired < 1) return "First failed to fire";
    if (fired > 1) return "Second fired prematurely";
    if (fire_mask != 1) return "Incorrect fire mask 1";

    fired = 0;
    qd_timer_schedule(timers[2], 2);
    qd_timer_schedule(timers[3], 4);
    time_value +=  2;
    qd_timer_visit();

    if (fired < 1) return "Second failed to fire";
    if (fired < 2) return "Third failed to fire";
    if (fire_mask != 7)  return "Incorrect fire mask 7";

    fired = 0;
    time_value +=  2;
    qd_timer_visit();
    if (fired < 1) return "Fourth failed to fire";
    if (fire_mask != 15) return "Incorrect fire mask 15";

    return 0;
}


static char* test_big(void *context)
{
    fire_mask = fired = 0;
    long durations[16] =
        { 5,  8,  7,  6,
         14, 10, 16, 15,
         11, 12,  9, 12,
          1,  2,  3,  4};
    unsigned long masks[18] = {
    0x1000,
    0x3000,
    0x7000,
    0xf000,
    0xf001,
    0xf009,
    0xf00d,
    0xf00f,
    0xf40f,
    0xf42f,
    0xf52f,
    0xff2f,
    0xff2f,
    0xff3f,
    0xffbf,
    0xffff,
    0xffff,
    0xffff
    };

    int i;
    for (i = 0; i < 16; i++)
        qd_timer_schedule(timers[i], durations[i]);
    for (i = 0; i < 18; i++) {
        ++time_value;
        qd_timer_visit();
        if (fire_mask != masks[i]) {
            static char error[100];
            sprintf(error, "Iteration %d: expected mask %04lx, got %04lx", i, masks[i], fire_mask);
            return error;
        }
    }

    return 0;
}


int timer_tests()
{
    char *test_group = "timer_tests";
    int result = 0;
    fire_mask = fired = 0;
    time_value = 1;

    timers[0]  = qd_timer(0, on_timer, (void*) 0x00000001);
    timers[1]  = qd_timer(0, on_timer, (void*) 0x00000002);
    timers[2]  = qd_timer(0, on_timer, (void*) 0x00000004);
    timers[3]  = qd_timer(0, on_timer, (void*) 0x00000008);
    timers[4]  = qd_timer(0, on_timer, (void*) 0x00000010);
    timers[5]  = qd_timer(0, on_timer, (void*) 0x00000020);
    timers[6]  = qd_timer(0, on_timer, (void*) 0x00000040);
    timers[7]  = qd_timer(0, on_timer, (void*) 0x00000080);
    timers[8]  = qd_timer(0, on_timer, (void*) 0x00000100);
    timers[9]  = qd_timer(0, on_timer, (void*) 0x00000200);
    timers[10] = qd_timer(0, on_timer, (void*) 0x00000400);
    timers[11] = qd_timer(0, on_timer, (void*) 0x00000800);
    timers[12] = qd_timer(0, on_timer, (void*) 0x00001000);
    timers[13] = qd_timer(0, on_timer, (void*) 0x00002000);
    timers[14] = qd_timer(0, on_timer, (void*) 0x00004000);
    timers[15] = qd_timer(0, on_timer, (void*) 0x00008000);

    TEST_CASE(test_quiet, 0);
    TEST_CASE(test_immediate, 0);
    TEST_CASE(test_immediate_reschedule, 0);
    TEST_CASE(test_immediate_plus_delayed, 0);
    TEST_CASE(test_single, 0);
    TEST_CASE(test_two_inorder, 0);
    TEST_CASE(test_two_reverse, 0);
    TEST_CASE(test_two_duplicate, 0);
    TEST_CASE(test_separated, 0);
    TEST_CASE(test_big, 0);

    int i;
    for (i = 0; i < 16; i++)
        qd_timer_free(timers[i]);

    return result;
}

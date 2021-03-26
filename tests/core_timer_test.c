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

#include "test_case.h"
#include <stdio.h>
#include <string.h>
#include "router_core/router_core_private.h"

void qdr_process_tick_CT(qdr_core_t *core, qdr_action_t *action, bool discard);


static int results[5];

static void callback(qdr_core_t *unused, void *context) {
    results[(long) context]++;
}


static char* test_core_timer(void *context)
{
    qdr_core_t *core = NEW(qdr_core_t);
    ZERO(core);

    qdr_core_timer_t *timers[5];

    for (long i = 0; i < 5; i++) {
        timers[i]  = qdr_core_timer_CT(core, callback, (void*) i);
        results[i] = 0;
    }

    qdr_core_timer_schedule_CT(core, timers[0], 5);
    qdr_core_timer_schedule_CT(core, timers[1], 10);
    qdr_core_timer_schedule_CT(core, timers[2], 1);
    qdr_core_timer_schedule_CT(core, timers[3], 15);
    qdr_core_timer_schedule_CT(core, timers[4], 0);

    //
    // Test the discard
    //
    qdr_process_tick_CT(core, 0, true);
    qdr_process_tick_CT(core, 0, true);
    qdr_process_tick_CT(core, 0, true);
    if (results[0] != 0 ||
        results[1] != 0 ||
        results[2] != 0 ||
        results[3] != 0 ||
        results[4] != 0)
        return "Received a callback on a discard tick events";

    //
    // Test zero-length timer
    //
    qdr_process_tick_CT(core, 0, false);
    if (results[0] != 0 ||
        results[1] != 0 ||
        results[2] != 0 ||
        results[3] != 0 ||
        results[4] != 1)
        return "Expected zero-length timer to fire once";
    
    //
    // Test 1-timer
    //
    qdr_process_tick_CT(core, 0, false);
    if (results[0] != 0 ||
        results[1] != 0 ||
        results[2] != 1 ||
        results[3] != 0 ||
        results[4] != 1)
        return "Expected timer(1) to fire once";

    //
    // Cancel 10-timer
    //
    qdr_core_timer_cancel_CT(core, timers[1]);
    
    //
    // Test 5-timer
    //
    qdr_process_tick_CT(core, 0, false);
    qdr_process_tick_CT(core, 0, false);
    qdr_process_tick_CT(core, 0, false);
    if (results[0] != 0 ||
        results[1] != 0 ||
        results[2] != 1 ||
        results[3] != 0 ||
        results[4] != 1)
        return "Expected timer(5) to not have fired yet";
    qdr_process_tick_CT(core, 0, false);
    if (results[0] != 1 ||
        results[1] != 0 ||
        results[2] != 1 ||
        results[3] != 0 ||
        results[4] != 1)
        return "Expected timer(5) to fire once";

    //
    // Test 15-timer
    //
    for (long i = 0; i < 9; i++)
        qdr_process_tick_CT(core, 0, false);
    if (results[0] != 1 ||
        results[1] != 0 ||
        results[2] != 1 ||
        results[3] != 0 ||
        results[4] != 1)
        return "Expected timer(15) and timer(10) to not have fired";
    qdr_process_tick_CT(core, 0, false);
    if (results[0] != 1 ||
        results[1] != 0 ||
        results[2] != 1 ||
        results[3] != 1 ||
        results[4] != 1)
        return "Expected timer(15) to fire once";

    //
    // Run with no timers for awhile
    //
    for (long i = 0; i < 100; i++)
        qdr_process_tick_CT(core, 0, false);
    if (results[0] != 1 ||
        results[1] != 0 ||
        results[2] != 1 ||
        results[3] != 1 ||
        results[4] != 1)
        return "Expected no timers to have fired when all are not scheduled";

    //
    // Re-schedule some timers at the same time
    //
    qdr_core_timer_schedule_CT(core, timers[0], 5);
    qdr_core_timer_schedule_CT(core, timers[1], 5);
    qdr_process_tick_CT(core, 0, false);
    qdr_process_tick_CT(core, 0, false);
    qdr_process_tick_CT(core, 0, false);
    qdr_process_tick_CT(core, 0, false);
    qdr_process_tick_CT(core, 0, false);
    if (results[0] != 1 ||
        results[1] != 0 ||
        results[2] != 1 ||
        results[3] != 1 ||
        results[4] != 1)
        return "Expected no timers to have fired while waiting for 5-timers";
    qdr_process_tick_CT(core, 0, false);
    if (results[0] != 2 ||
        results[1] != 1 ||
        results[2] != 1 ||
        results[3] != 1 ||
        results[4] != 1)
        return "Expected both 5-timers to have fired";

    //
    // Free up resources
    //
    for (long i = 0; i < 5; i++)
        qdr_core_timer_free_CT(core, timers[i]);
    free(core);

    return 0;
}


int core_timer_tests(void)
{
    int result = 0;
    char *test_group = "core_timer_tests";

    TEST_CASE(test_core_timer, 0);

    return result;
}


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

#define _GNU_SOURCE
#include "test_case.h"

#include "qpid/dispatch/threading.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define thread_count 10
static sys_thread_t *threads[thread_count] = {0};

static sys_mutex_t  *mutex = 0;
static sys_cond_t   *cond = 0;

static char         *result;


// for test_thread_id
//
void *thread_id_thread(void *arg)
{
    intptr_t index = (intptr_t) arg;
    assert(index < thread_count);

    sys_mutex_lock(mutex);

    // check if self corresponds to my index in threads[]
    if (!sys_thread_self()) {
        result = "sys_thread_self returned zero!";
    } else if (threads[index] != sys_thread_self()) {
        result = "sys_thread_self mismatch";
    }

    sys_mutex_unlock(mutex);

    return 0;
}


// ensure sys_thread_self is correct
//
static char *test_thread_id(void *context)
{
    mutex = sys_mutex();
    sys_mutex_lock(mutex);

    // start threads and retain their addresses
    //
    result = 0;
    memset(threads, 0, sizeof(threads));
    for (intptr_t i = 0; i < thread_count; ++i) {
        threads[i] = sys_thread(thread_id_thread, (void *)i);
    }

    sys_mutex_unlock(mutex);

    for (int i = 0; i < thread_count; ++i) {
        sys_thread_join(threads[i]);
        sys_thread_free(threads[i]);
    }

    //
    // test calling sys_thread_self() from the main context.  This context
    // was not created by sys_thread(), however a dummy non-zero value is returned.
    //
    sys_thread_t *main_id = sys_thread_self();
    if (!main_id) {
        result = "sys_thread_self() returned 0 for main thread";
    } else {
        for (int i = 0; i < thread_count; ++i) {
            if (threads[i] == main_id) {   // must be unique!
                result = "main thread sys_thread_self() not unique!";
                break;
            }
        }
    }

    sys_mutex_free(mutex);
    return result;
}



static int cond_count;


// run by test_condition
//
void *test_condition_thread(void *arg)
{
    int *test = (int *)arg;

    sys_mutex_lock(mutex);
    while (*test == 0) {
        // it is expected that cond_count will never be > 1 since the condition
        // is triggered only once
        cond_count += 1;
        sys_cond_wait(cond, mutex);
    }
    if (*test != 1) {
        result = "error expected *test to be 1";
    } else {
        *test += 1;
    }
    sys_mutex_unlock(mutex);

    return 0;
}


static char *test_condition(void *context)
{
    mutex = sys_mutex();
    cond = sys_cond();

    sys_mutex_lock(mutex);

    int test = 0;
    cond_count = 0;
    result = 0;
    sys_thread_t *thread = sys_thread(test_condition_thread, &test);

    sys_mutex_unlock(mutex);

    // let thread run and block on condition
    sleep(1);

    sys_mutex_lock(mutex);

    if (cond_count != 1) {
        result = "expected thread to wait on condition";
    }

    test = 1;

    sys_cond_signal(cond);
    sys_mutex_unlock(mutex);

    sys_thread_join(thread);
    sys_thread_free(thread);

    if (!result && test != 2) {
        result = "expected thread to increment test variable";
    }

    sys_cond_free(cond);
    sys_mutex_free(mutex);
    return result;
}


int thread_tests()
{
    int result = 0;
    char *test_group = "thread_tests";

    TEST_CASE(test_thread_id, 0);
    TEST_CASE(test_condition, 0);

    return result;
}


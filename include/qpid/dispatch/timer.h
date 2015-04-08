#ifndef __dispatch_timer_h__
#define __dispatch_timer_h__ 1
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

#include <qpid/dispatch/dispatch.h>
#include <qpid/dispatch/server.h>
#include <stdint.h>

/**@file
 * Server Timer Functions
 * 
 * @defgroup timer timer
 *
 * Server Timer Functions
 * @{
 */

typedef struct qd_timer_t qd_timer_t;
typedef int64_t qd_timestamp_t;

/**
 * Timer Callback
 *
 * Callback invoked after a timer's interval expires and the timer fires.
 *
 * @param context The context supplied in qd_timer
 */
typedef void (*qd_timer_cb_t)(void* context);


/**
 * Create a new timer object.
 * 
 * @param qd Pointer to the dispatch instance.
 * @param cb The callback function to be invoked when the timer expires.
 * @param context An opaque, user-supplied context to be passed into the callback.
 * @return A pointer to the new timer object or NULL if memory is exhausted.
 */
qd_timer_t *qd_timer(qd_dispatch_t *qd, qd_timer_cb_t cb, void* context);


/**
 * Free the resources for a timer object.  If the timer was scheduled, it will be canceled 
 * prior to freeing.  After this function returns, the callback will not be invoked for this
 * timer.
 *
 * @param timer Pointer to the timer object returned by qd_timer.
 */
void qd_timer_free(qd_timer_t *timer);


/**
 * Schedule a timer to fire in the future.
 *
 * Note that the timer callback will never be invoked synchronously during the execution
 * of qd_timer_schedule.  Even if the interval is immediate (0), the callback invocation will
 * be asynchronous and after the return of this function.
 *
 * @param timer Pointer to the timer object returned by qd_timer.
 * @param msec The minimum number of milliseconds of delay until the timer fires.
 *             If 0 is supplied, the timer will be scheduled to fire immediately.
 */
void qd_timer_schedule(qd_timer_t *timer, qd_timestamp_t msec);


/**
 * Attempt to cancel a scheduled timer.  Since the timer callback can be invoked on any
 * server thread, it is always possible that a last-second cancel attempt may arrive too late
 * to stop the timer from firing (i.e. the cancel is concurrent with the fire callback).
 *
 * @param timer Pointer to the timer object returned by qd_timer.
 */
void qd_timer_cancel(qd_timer_t *timer);

/**
 * @}
 */

#endif

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

#include "qpid/dispatch/dispatch.h"
#include "qpid/dispatch/server.h"

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
/** Absolute time stamp from monotonic clock source, milliseconds since arbitrary (fixed) instant */
typedef int64_t qd_timestamp_t;
/** Relative duration in milliseconds */
typedef int64_t qd_duration_t;

/**
 * Timer Callback
 *
 * Callback invoked after a timer's interval expires and the timer fires.  This
 * may be invoked on any server thread.
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
 * Free a timer.
 *
 * If the timer was scheduled, it will be canceled prior to freeing.  If the
 * callback is currently running on another server thread the caller will block
 * until the callback completes.  On return the caller is guaranteed that the
 * timer callback will never run again.  This may be called during the timer
 * callback - in that case the call will not block.
 *
 * Note Well:
 * Since this call may block until the timer callback completes, it is critical
 * that the caller MUST NOT be holding any lock that the timer callback takes.
 *
 * @param timer Pointer to the timer object returned by qd_timer.
 */
void qd_timer_free(qd_timer_t *timer);


/**
 * Schedule a timer to fire in the future.
 *
 * Note that the timer callback will never be invoked synchronously during the
 * execution of qd_timer_schedule.  Even if the interval is immediate (0), the
 * callback invocation will be asynchronous and after the return of this
 * function.  This may be called during the timer callback.
 *
 * @param timer Pointer to the timer object returned by qd_timer.
 * @param msec The minimum number of milliseconds of delay until the timer fires.
 *             If 0 is supplied, the timer will be scheduled to fire immediately.
 */
void qd_timer_schedule(qd_timer_t *timer, qd_duration_t msec);


/**
 * Cancel a scheduled timer.
 *
 * If the timer is scheduled it will be canceled and the callback will not be
 * invoked.  If the timer callback is currently executing on another thread
 * this call will block until the callback completes.  On return the caller is
 * guaranteed that the callback will not execute unless it is rescheduled using
 * qd_timer_schedule.  This call must not be invoked from within the callback
 * itself.
 *
 * Note Well:
 * Since this call may block until the timer callback completes, it is critical
 * that the caller MUST NOT be holding any lock that the timer callback takes.
 *
 * @param timer Pointer to the timer object returned by qd_timer.
 */
void qd_timer_cancel(qd_timer_t *timer);

/**
 * The current time.
 */
qd_timestamp_t qd_timer_now() ;

/**
 * @}
 */

#endif

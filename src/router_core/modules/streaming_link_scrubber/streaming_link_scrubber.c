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

#include "module.h"

#include "qpid/dispatch/ctools.h"

#include <inttypes.h>


/*
 * Release unused streaming links
 *
 * Periodically scan through the list of open connections checking for idle
 * streaming links.  If the connections idle streaming link pool is oversized
 * then release some of the unused links in the background.
 */

#define PROD_TIMER_INTERVAL    30
#define TEST_TIMER_INTERVAL    5
#define TEST_MAX_FREE_POOL     2
#define MAX_FREE_BATCH         10  // rate limit the link detach

static int timer_interval     = PROD_TIMER_INTERVAL;
static int max_free_pool_size = 128;

static void qdr_streaming_link_scrubber_CT(qdr_core_t *core, qdr_action_t *action, bool discard);

typedef struct tracker_t tracker_t;
struct tracker_t {
    qdr_core_t              *core;
    qdr_core_timer_t        *timer;
    qdr_connection_ref_t_sp  next_conn_ref;
};


/* Idle streaming link cleanup
 *
 * Check the size of the connections idle link free pool.  If the connection
 * has accumulated too many unused links start closing them
 */
static void idle_link_cleanup(qdr_core_t *core, qdr_connection_t *conn)
{
    qdr_link_list_t to_free = DEQ_EMPTY;

    qd_log(core->log, QD_LOG_DEBUG,
           "[C%"PRIu64"] Streaming link scrubber: scanning connection", conn->identity);

    const size_t pool_size = DEQ_SIZE(conn->streaming_link_pool);
    if (pool_size > max_free_pool_size) {
        size_t count = MIN(MAX_FREE_BATCH, pool_size - max_free_pool_size);

        // links are returned to the pool by inserting at the tail.  Thus the
        // links at head have been on the list the longest and are more likely
        // be candidates for cleanup (e.g. idle)
        while (count) {
            qdr_link_t *link = DEQ_HEAD(conn->streaming_link_pool);
            if (!qdr_link_is_idle_CT(link))
                break;
            DEQ_REMOVE_HEAD_N(STREAMING_POOL, conn->streaming_link_pool);
            DEQ_INSERT_TAIL_N(STREAMING_POOL, to_free, link);
            link->in_streaming_pool = false;
            count -= 1;
        }

    }

    if (DEQ_HEAD(to_free)) {
        qd_log(core->log, QD_LOG_DEBUG,
               "[C%"PRIu64"] Streaming link scrubber: found %d idle links", conn->identity, (int)DEQ_SIZE(to_free));

        while (DEQ_HEAD(to_free)) {
            qdr_link_t *link = DEQ_HEAD(to_free);
            DEQ_REMOVE_HEAD_N(STREAMING_POOL, to_free);
            qd_log(core->log, QD_LOG_DEBUG,
                   "[C%"PRIu64"][L%"PRIu64"] Streaming link scrubber: closing idle link %s",
                   link->conn->identity, link->identity, (link->name) ? link->name : "");
            qdr_link_outbound_detach_CT(core, link, 0, QDR_CONDITION_NONE, true);
        }
    }
}


static void timer_handler_CT(qdr_core_t *core, void *context)
{
    tracker_t            *tracker    = (tracker_t*) context;
    qdr_connection_ref_t *first_ref = DEQ_HEAD(core->streaming_connections);

    if (!!first_ref) {
        qd_log(core->log, QD_LOG_DEBUG, "Starting streaming link scrubber scan");
        set_safe_ptr_qdr_connection_ref_t(first_ref, &tracker->next_conn_ref);
        qdr_action_t *action = qdr_action(qdr_streaming_link_scrubber_CT, "streaming_link_scrubber");
        action->args.general.context_1 = tracker;
        qdr_action_background_enqueue(core, action);
    } else
        qdr_core_timer_schedule_CT(core, tracker->timer, timer_interval);
}


static void qdr_streaming_link_scrubber_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    if (discard)
        return;

    tracker_t            *tracker  = (tracker_t*) action->args.general.context_1;
    qdr_connection_ref_t *conn_ref = safe_deref_qdr_connection_ref_t(tracker->next_conn_ref);

    if (!!conn_ref) {
        idle_link_cleanup(core, conn_ref->conn);

        conn_ref = DEQ_NEXT(conn_ref);
        if (!!conn_ref) {
            //
            // There is another connection on the list.  Schedule another
            // background action to process the next connection.
            //
            set_safe_ptr_qdr_connection_ref_t(conn_ref, &tracker->next_conn_ref);
            action = qdr_action(qdr_streaming_link_scrubber_CT, "streaming_link_scrubber");
            action->args.general.context_1 = tracker;
            qdr_action_background_enqueue(core, action);
        } else
            //
            // We've come to the end of the list of open connections.  Set the
            // timer to start a new sweep after the interval.
            //
            qdr_core_timer_schedule_CT(core, tracker->timer, timer_interval);
    } else
        //
        // The connection we were provided is no longer valid.  It was probably
        // closed since the last time we came through this path.  Abort the
        // sweep and set the timer for a new one after the interval.
        //
        qdr_core_timer_schedule_CT(core, tracker->timer, timer_interval);
}


static bool qcm_streaming_link_scrubber_enable_CT(qdr_core_t *core)
{
    if (core->qd->test_hooks) {
        //
        // Test hooks are enabled, override the timing constants with the test values
        //
        timer_interval = TEST_TIMER_INTERVAL;
        max_free_pool_size = TEST_MAX_FREE_POOL;
    }

    return true;
}


static void qcm_streaming_link_scrubber_init_CT(qdr_core_t *core, void **module_context)
{
    tracker_t *tracker = NEW(tracker_t);
    ZERO(tracker);
    tracker->core  = core;
    tracker->timer = qdr_core_timer_CT(core, timer_handler_CT, tracker);
    qdr_core_timer_schedule_CT(core, tracker->timer, timer_interval);
    *module_context = tracker;

    qd_log(core->log, QD_LOG_INFO,
           "Streaming link scrubber: Scan interval: %d seconds, max free pool: %d links", timer_interval, max_free_pool_size);
}


static void qcm_streaming_link_scrubber_final_CT(void *module_context)
{
    tracker_t *tracker = (tracker_t*) module_context;
    qdr_core_timer_free_CT(tracker->core, tracker->timer);
    free(tracker);
}


QDR_CORE_MODULE_DECLARE("streaming_link_scrubber", qcm_streaming_link_scrubber_enable_CT, qcm_streaming_link_scrubber_init_CT, qcm_streaming_link_scrubber_final_CT)

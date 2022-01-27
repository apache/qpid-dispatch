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

#include "delivery.h"
#include "module.h"

#include "qpid/dispatch/ctools.h"

#include <inttypes.h>

#define PROD_TIMER_INTERVAL 30
#define PROD_STUCK_AGE      10

#define TEST_TIMER_INTERVAL 5
#define TEST_STUCK_AGE      3

static int timer_interval = PROD_TIMER_INTERVAL;
static int stuck_age      = PROD_STUCK_AGE;

static void action_handler_CT(qdr_core_t *core, qdr_action_t *action, bool discard);

typedef struct tracker_t tracker_t;

struct tracker_t {
    qdr_core_t       *core;
    qdr_core_timer_t *timer;
    qdr_link_t_sp     next_link;
};


static void check_delivery_CT(qdr_core_t *core, qdr_link_t *link, qdr_delivery_t *dlv)
{
    // DISPATCH-2036: ignore "infinitely long" streaming messages (like TCP
    // adaptor deliveries)
    if (dlv->msg && qd_message_is_streaming(dlv->msg)) {
        return;
    }

    if (!dlv->stuck && ((qdr_core_uptime_ticks(core) - link->core_ticks) > stuck_age)) {
        dlv->stuck = true;
        link->deliveries_stuck++;
        core->deliveries_stuck++;
        if (link->deliveries_stuck == 1)
            qd_log(core->log, QD_LOG_INFO,
                   "[C%"PRIu64"][L%"PRIu64"] "
                   "Stuck delivery: At least one delivery on this link has been undelivered/unsettled for more than %d seconds",
                   link->conn ? link->conn->identity : 0, link->identity, stuck_age);
    }
}


static void process_link_CT(qdr_core_t *core, qdr_link_t *link)
{
    qdr_delivery_t *dlv = DEQ_HEAD(link->undelivered);
    while (dlv) {
        check_delivery_CT(core, link, dlv);
        dlv = DEQ_NEXT(dlv);
    }

    dlv = DEQ_HEAD(link->unsettled);
    while (dlv) {
        check_delivery_CT(core, link, dlv);
        dlv = DEQ_NEXT(dlv);
    }

    if (!link->reported_as_blocked && link->zero_credit_time > 0 &&
        (qdr_core_uptime_ticks(core) - link->zero_credit_time > stuck_age)) {
        link->reported_as_blocked = true;
        core->links_blocked++;
        qd_log(core->log, QD_LOG_INFO,
               "[C%"PRIu64"][L%"PRIu64"] "
               "Link blocked with zero credit for %d seconds",
               link->conn ? link->conn->identity : 0, link->identity,
               qdr_core_uptime_ticks(core) - link->zero_credit_time);
    }
}


static void timer_handler_CT(qdr_core_t *core, void *context)
{
    tracker_t  *tracker    = (tracker_t*) context;
    qdr_link_t *first_link = DEQ_HEAD(core->open_links);

    qd_log(core->log, QD_LOG_DEBUG, "Stuck Delivery Detection: Starting detection cycle");

    if (!!first_link) {
        set_safe_ptr_qdr_link_t(first_link, &tracker->next_link);
        qdr_action_t *action = qdr_action(action_handler_CT, "detect_stuck_deliveries");
        action->args.general.context_1 = tracker;
        qdr_action_background_enqueue(core, action);
    } else
        qdr_core_timer_schedule_CT(core, tracker->timer, timer_interval);
}


static void action_handler_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    if (discard)
        return;

    tracker_t  *tracker = (tracker_t*) action->args.general.context_1;
    qdr_link_t *link    = safe_deref_qdr_link_t(tracker->next_link);

    if (!!link) {
        process_link_CT(core, link);
        qdr_link_t *next = DEQ_NEXT(link);
        if (!!next) {
            //
            // There is another link on the list.  Schedule another background action to process
            // the next link.
            //
            set_safe_ptr_qdr_link_t(next, &tracker->next_link);
            action = qdr_action(action_handler_CT, "detect_stuck_deliveries");
            action->args.general.context_1 = tracker;
            qdr_action_background_enqueue(core, action);
        } else
            //
            // We've come to the end of the list of open links.  Set the timer to start a new sweep
            // after the interval.
            //
            qdr_core_timer_schedule_CT(core, tracker->timer, timer_interval);
    } else
        //
        // The link we were provided is not valid.  It was probably closed since the last time we
        // came through this path.  Abort the sweep and set the timer for a new one after the interval.
        //
        qdr_core_timer_schedule_CT(core, tracker->timer, timer_interval);
}


static bool qdrc_delivery_tracker_enable_CT(qdr_core_t *core)
{
    if (core->qd->test_hooks) {
        //
        // Test hooks are enabled, override the timing constants with the test values
        //
        timer_interval = TEST_TIMER_INTERVAL;
        stuck_age      = TEST_STUCK_AGE;
    }

    return true;
}


static void qdrc_delivery_tracker_init_CT(qdr_core_t *core, void **module_context)
{
    tracker_t *tracker = NEW(tracker_t);
    ZERO(tracker);
    tracker->core  = core;
    tracker->timer = qdr_core_timer_CT(core, timer_handler_CT, tracker);
    qdr_core_timer_schedule_CT(core, tracker->timer, timer_interval);
    *module_context = tracker;

    qd_log(core->log, QD_LOG_INFO,
           "Stuck delivery detection: Scan interval: %d seconds, Delivery age threshold: %d seconds",
           timer_interval, stuck_age);
}


static void qdrc_delivery_tracker_final_CT(void *module_context)
{
    tracker_t *tracker = (tracker_t*) module_context;
    qdr_core_timer_free_CT(tracker->core, tracker->timer);
    free(tracker);
}


QDR_CORE_MODULE_DECLARE("stuck_delivery_detection", qdrc_delivery_tracker_enable_CT, qdrc_delivery_tracker_init_CT, qdrc_delivery_tracker_final_CT)

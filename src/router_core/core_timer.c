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

#include "router_core_private.h"
#include <stdio.h>

qdr_timer_work_t *qdr_timer_schedule(qdr_core_t *core, qdr_timer_cb_t callback, void *timer_context, int timer_delay)
{
    qdr_timer_work_t *timer_work = new_qdr_timer_work_t();
    ZERO(timer_work);
    timer_work->handler = callback;
    timer_work->timer_delay = timer_delay;
    timer_work->on_timer_context = timer_context;

    DEQ_INSERT_TAIL(core->timer_list, timer_work);

    return timer_work;
}

void qdr_timer_delete(qdr_core_t *core, qdr_timer_work_t *timer_work)
{
    if (DEQ_SIZE(core->timer_list) == 0)
        return;

    if (timer_work) {
        DEQ_REMOVE(core->timer_list, timer_work);
        free_qdr_timer_work_t(timer_work);
    }

}

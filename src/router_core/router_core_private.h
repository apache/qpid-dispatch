#ifndef qd_router_core_private
#define qd_router_core_private 1
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
#include <qpid/dispatch/router_core.h>
#include <qpid/dispatch/threading.h>
#include <qpid/dispatch/log.h>
#include <memory.h>

typedef struct {
    qd_buffer_list_t     buffers;
    qd_field_iterator_t *iterator;
} qdr_field_t;

qdr_field_t *qdr_field(const char *string);
void qdr_field_free(qdr_field_t *field);

typedef struct qdr_action_t qdr_action_t;
typedef void (*qdr_action_handler_t) (qdr_core_t *core, qdr_action_t *action);

struct qdr_action_t {
    DEQ_LINKS(qdr_action_t);
    qdr_action_handler_t action_handler;
    union {
        struct {
            int           link_maskbit;
            int           router_maskbit;
            int           nh_router_maskbit;
            qd_bitmask_t *router_set;
            qdr_field_t  *address;
            char          address_class;
            char          address_phase;
        } route_table;
    } args;
};

ALLOC_DECLARE(qdr_action_t);
DEQ_DECLARE(qdr_action_t, qdr_action_list_t);

struct qdr_core_t {
    qd_log_source_t   *log;
    sys_cond_t        *cond;
    sys_mutex_t       *lock;
    sys_thread_t      *thread;
    bool               running;
    qdr_action_list_t  action_list;

    void                 *rt_context;
    qdr_mobile_added_t    rt_mobile_added;
    qdr_mobile_removed_t  rt_mobile_removed;
    qdr_link_lost_t       rt_link_lost;
};

void *router_core_thread(void *arg);

#endif

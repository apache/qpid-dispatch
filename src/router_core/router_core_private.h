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
    qd_buffer_t         *buffer;
    qd_field_iterator_t *iterator;
} qdr_field_t;

typedef enum {
    QDR_ACTION_ADD_ROUTER,
    QDR_ACTION_DEL_ROUTER,
    QDR_ACTION_SET_LINK,
    QDR_ACTION_REMOVE_LINK,
    QDR_ACTION_SET_NEXT_HOP,
    QDR_ACTION_REMOVE_NEXT_HOP,
    QDR_ACTION_SET_VALID_ORIGINS,
    QDR_ACTION_MAP_DESTINATION,
    QDR_ACTION_UNMAP_DESTINATION,
    QDR_ACTION_SUBSCRIBE,
    QDR_ACTION_CONNECTION_OPENED,
    QDR_ACTION_CONNECTION_CLOSED,
    QDR_ACTION_LINK_FIRST_ATTACH,
    QDR_ACTION_LINK_SECOND_ATTACH,
    QDR_ACTION_LINK_DETACH,
    QDR_ACTION_DELIVER,
    QDR_ACTION_DELIVER_TO,
    QDR_ACTION_DISPOSITION_CHANGE,
    QDR_ACTION_FLOW_CHANGE,
    QDR_ACTION_MANAGE_CREATE,
    QDR_ACTION_MANAGE_DELETE,
    QDR_ACTION_MANAGE_READ,
    QDR_ACTION_MANAGE_GET_FIRST,
    QDR_ACTION_MANAGE_GET_NEXT
} qdr_action_type_t;

typedef struct qdr_action_t {
    DEQ_LINKS(struct qdr_action_t);
    qdr_action_type_t  action_type;
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
} qdr_action_t;

ALLOC_DECLARE(qdr_action_t);
DEQ_DECLARE(qdr_action_t, qdr_action_list_t);

struct qdr_core_t {
    qd_log_source_t   *log;
    sys_cond_t        *cond;
    sys_mutex_t       *lock;
    sys_thread_t      *thread;
    bool               running;
    qdr_action_list_t  action_list;
};

void *router_core_thread(void *arg);

#endif

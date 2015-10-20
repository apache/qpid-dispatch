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
//#include "route_tables.h"

static qdr_action_t *qdr_action(qdr_action_handler_t action_handler);
static void qdr_action_enqueue(qdr_core_t *core, qdr_action_t *action);

static void qdrh_add_router(qdr_core_t *core, qdr_action_t *action);
static void qdrh_del_router(qdr_core_t *core, qdr_action_t *action);
static void qdrh_set_link(qdr_core_t *core, qdr_action_t *action);
static void qdrh_remove_link(qdr_core_t *core, qdr_action_t *action);
static void qdrh_set_next_hop(qdr_core_t *core, qdr_action_t *action);
static void qdrh_remove_next_hop(qdr_core_t *core, qdr_action_t *action);
static void qdrh_set_valid_origins(qdr_core_t *core, qdr_action_t *action);
static void qdrh_map_destination(qdr_core_t *core, qdr_action_t *action);
static void qdrh_unmap_destination(qdr_core_t *core, qdr_action_t *action);


//==================================================================================
// Interface Functions
//==================================================================================

void qdr_core_add_router(qdr_core_t *core, const char *address, int router_maskbit)
{
    qdr_action_t *action = qdr_action(qdrh_add_router);
    action->args.route_table.router_maskbit = router_maskbit;
    action->args.route_table.address        = qdr_field(address);
    qdr_action_enqueue(core, action);
}


void qdr_core_del_router(qdr_core_t *core, int router_maskbit)
{
    qdr_action_t *action = qdr_action(qdrh_del_router);
    action->args.route_table.router_maskbit = router_maskbit;
    qdr_action_enqueue(core, action);
}


void qdr_core_set_link(qdr_core_t *core, int router_maskbit, int link_maskbit)
{
    qdr_action_t *action = qdr_action(qdrh_set_link);
    action->args.route_table.router_maskbit = router_maskbit;
    action->args.route_table.link_maskbit   = link_maskbit;
    qdr_action_enqueue(core, action);
}


void qdr_core_remove_link(qdr_core_t *core, int router_maskbit)
{
    qdr_action_t *action = qdr_action(qdrh_remove_link);
    action->args.route_table.router_maskbit = router_maskbit;
    qdr_action_enqueue(core, action);
}


void qdr_core_set_next_hop(qdr_core_t *core, int router_maskbit, int nh_router_maskbit)
{
    qdr_action_t *action = qdr_action(qdrh_set_next_hop);
    action->args.route_table.router_maskbit    = router_maskbit;
    action->args.route_table.nh_router_maskbit = nh_router_maskbit;
    qdr_action_enqueue(core, action);
}


void qdr_core_remove_next_hop(qdr_core_t *core, int router_maskbit)
{
    qdr_action_t *action = qdr_action(qdrh_remove_next_hop);
    action->args.route_table.router_maskbit = router_maskbit;
    qdr_action_enqueue(core, action);
}


void qdr_core_set_valid_origins(qdr_core_t *core, int router_maskbit, qd_bitmask_t *routers)
{
    qdr_action_t *action = qdr_action(qdrh_set_valid_origins);
    action->args.route_table.router_maskbit = router_maskbit;
    action->args.route_table.router_set     = routers;
    qdr_action_enqueue(core, action);
}


void qdr_core_map_destination(qdr_core_t *core, int router_maskbit, const char *address, char aclass, char phase)
{
    qdr_action_t *action = qdr_action(qdrh_map_destination);
    action->args.route_table.router_maskbit = router_maskbit;
    action->args.route_table.address        = qdr_field(address);
    action->args.route_table.address_phase  = phase;
    action->args.route_table.address_class  = aclass;
    qdr_action_enqueue(core, action);
}


void qdr_core_unmap_destination(qdr_core_t *core, int router_maskbit, const char *address, char aclass, char phase)
{
    qdr_action_t *action = qdr_action(qdrh_unmap_destination);
    action->args.route_table.router_maskbit = router_maskbit;
    action->args.route_table.address        = qdr_field(address);
    action->args.route_table.address_phase  = phase;
    action->args.route_table.address_class  = aclass;
    qdr_action_enqueue(core, action);
}

void qdr_core_route_table_handlers(qdr_core_t           *core, 
                                   void                 *context,
                                   qdr_mobile_added_t    mobile_added,
                                   qdr_mobile_removed_t  mobile_removed,
                                   qdr_link_lost_t       link_lost)
{
    core->rt_context        = context;
    core->rt_mobile_added   = mobile_added;
    core->rt_mobile_removed = mobile_removed;
    core->rt_link_lost      = link_lost;
}


//==================================================================================
// Internal Functions
//==================================================================================

static qdr_action_t *qdr_action(qdr_action_handler_t action_handler)
{
    qdr_action_t *action = new_qdr_action_t();
    ZERO(action);
    action->action_handler = action_handler;
    return action;
}

static void qdr_action_enqueue(qdr_core_t *core, qdr_action_t *action)
{
    sys_mutex_lock(core->lock);
    DEQ_INSERT_TAIL(core->action_list, action);
    sys_mutex_unlock(core->lock);
}


//==================================================================================
// In-Thread Functions
//==================================================================================

static void qdrh_add_router(qdr_core_t *core, qdr_action_t *action)
{
}


static void qdrh_del_router(qdr_core_t *core, qdr_action_t *action)
{
}


static void qdrh_set_link(qdr_core_t *core, qdr_action_t *action)
{
}


static void qdrh_remove_link(qdr_core_t *core, qdr_action_t *action)
{
}


static void qdrh_set_next_hop(qdr_core_t *core, qdr_action_t *action)
{
}


static void qdrh_remove_next_hop(qdr_core_t *core, qdr_action_t *action)
{
}


static void qdrh_set_valid_origins(qdr_core_t *core, qdr_action_t *action)
{
}


static void qdrh_map_destination(qdr_core_t *core, qdr_action_t *action)
{
}


static void qdrh_unmap_destination(qdr_core_t *core, qdr_action_t *action)
{
}



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

#include "credit.h"
#include <inttypes.h>
#include <limits.h>

//
// Core credit component.
//
// This component has the following responsibilities:
//
// For incoming normal links (from endpoints) that have a target address, maintain the
// correct credit loop for attached senders.  The credit given to a sender is
// controlled by the following rules:
//
//   1) If there are no consumers in the network, no credit is issued.
//   2) If there are consumers, at least one credit shall be issued to each sender for the address.
//   3) No more than link->capacity credits shall be issued to any sender.
//   4) The target credit to be issued is the total number of outgoing credits divided by
//      the total number of incoming links.  This number is then constrained by the rules
//      above.
//
// If an incoming link's credit window is to be expanded, the additional credit is issued
// immediately.
//
// If an incoming link's credit window is to be contracted to a non-zero value, the link
// stops issuing replacement credit until it achieves the new, smaller window.
//
// If an incoming link's credit window is to be set to zero, it shall issue a drain to the
// sender to revoke the outstanding credit.
//
//
// Fallback address handling:
//
// If an address has a fallback address, the flow stats for the fallback address are used
// in lieu of the primary address stats if the primary address has no destinations.  This
// means that changes to the fallback stats affect links to the primary address when the
// primary has no destinations.
//
// Conversely, if an address is a fallback for a primary address, its stats are ignored if
// the primary address has at least one destination.
//
//
// Internal destination handling:
//
// If an address is associated with an exchange, an internal endpoint, or a remote router,
// it is considered reachable and the credit loop shall be set at the link's capacity.
//

static qdrc_event_subscription_t *credit_event_sub;


static void qdrc_credit_set_link_flow_CT(qdr_core_t *core, qdr_link_t *link, int credit)
{
    //
    // Limit the credit issued to the link's capacity.
    //
    credit = MIN(credit, link->capacity);

    if (credit > link->credit_window) {
        //
        // We are increasing the credit for this link.  Consume residual and issue needed credit.
        //
        int diff = credit - link->credit_window;

        if (diff > link->credit_residual) {
            //
            // More credit is being added than there is residual credit for the link.  New credit must be issued.
            //
            diff -= link->credit_residual;
            link->credit_residual = 0;
            qdr_link_issue_credit_CT(core, link, diff, false);
        } else {
            //
            // There is sufficient residual credit on the link to cover this increase.  Simply reduce
            // the residual count.
            //
            link->credit_residual -= diff;
        }

        link->credit_window = credit;
    } else if (credit < link->credit_window) {
        //
        // We are decreasing the credit for this link.  Increase the residual to suppress the
        // replenishing of credit to get down to the new window.
        //
        link->credit_residual += link->credit_window - credit;
        link->credit_window = credit;
    }
}


static void qdrc_credit_flow_change_CT(void          *context,
                                       qdrc_event_t   event_type,
                                       qdr_address_t *addr)
{
    //
    // The flow data for an address has changed.  This function does not distinguish between
    // local and remote flow changes.  It simply uses the aggregate totals.
    //
    qdr_core_t *core = (qdr_core_t*) context;

    //
    // If this address has no local inlinks, there's no work to do so don't bother computing
    // the target credit.
    //
    if (DEQ_SIZE(addr->inlinks) == 0)
        return;

    //
    // Compute the target credit for this address to be used in each inlink.
    //
    int  target_credit;
    char addrClass = *((const char*) qd_hash_key_by_handle(addr->hash_handle));

    if (!!addr->exchange || DEQ_SIZE(addr->subscriptions) > 0 ||
        (addrClass == QD_ITER_HASH_PREFIX_ROUTER && qd_bitmask_cardinality(addr->rnodes) > 0)) {
        //
        // The address has destinations that don't participate in credit-based flow control.  Use
        // INT_MAX as the target.  This will be reduced to the capacity for each link it is applied to.
        //
        target_credit = INT_MAX; 
    } else {
        //
        // The more general case.  Compute the target based on the network-wide number of senders
        // getting a share of the network-wide number of issued receiver credits.
        //
        uint32_t total_out_credit = addr->local_out_credit + addr->remote_out_credit_total;
        if (total_out_credit == 0)
            target_credit = 0;
        else {
            target_credit = total_out_credit / (DEQ_SIZE(addr->inlinks) + addr->remote_inlinks_total);
            if (target_credit == 0)
                target_credit = 1;
        }
    }

    //
    // Apply the new target credit to each local inlink
    //
    qdr_link_ref_t *ref = DEQ_HEAD(addr->inlinks);
    while (ref) {
        qdrc_credit_set_link_flow_CT(core, ref->link, target_credit);
        ref = DEQ_NEXT(ref);
    }
}


void qdrc_credit_setup_CT(qdr_core_t *core)
{
    credit_event_sub = qdrc_event_subscribe_CT(core,
                                               QDRC_EVENT_ADDR_FLOW_LOCAL_CHANGE | QDRC_EVENT_ADDR_FLOW_REMOTE_CHANGE,
                                               0, // qdrc_connection_event_t on_conn_event,
                                               0, // qdrc_link_event_t       on_link_event,
                                               qdrc_credit_flow_change_CT,
                                               0, // qdrc_router_event_t     on_router_event,
                                               core);
}


void qdrc_credit_final_CT(qdr_core_t *core)
{
    qdrc_event_unsubscribe_CT(core, credit_event_sub);
    credit_event_sub = 0;
}


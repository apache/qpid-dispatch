#ifndef __delivery_h__
#define __delivery_h__ 1

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

#define QDR_DELIVERY_TAG_MAX 32

typedef enum {
    QDR_DELIVERY_NOWHERE = 0,
    QDR_DELIVERY_IN_UNDELIVERED,
    QDR_DELIVERY_IN_UNSETTLED,
    QDR_DELIVERY_IN_SETTLED
} qdr_delivery_where_t;


struct qdr_delivery_t {
    DEQ_LINKS(qdr_delivery_t);
    void                   *context;
    sys_atomic_t            ref_count;
    bool                    ref_counted;   /// Used to protect against ref count going 1 -> 0 -> 1
    qdr_link_t_sp           link_sp;       /// Safe pointer to the link
    qdr_delivery_t         *peer;          /// Use this peer if the delivery has one and only one peer.
    qdr_delivery_ref_t     *next_peer_ref;
    qd_message_t           *msg;
    qd_iterator_t          *to_addr;
    qd_iterator_t          *origin;
    uint64_t                disposition;         ///< local disposition, will be pushed to remote endpoint
    uint64_t                remote_disposition;  ///< disposition as set by remote endpoint
    uint64_t                mcast_disposition;   ///< temporary terminal disposition while multicast fwding
    uint32_t                ingress_time;
    pn_data_t              *extension_state;     ///< delivery-state in disposition performative
    qdr_error_t            *error;
    bool                    settled;
    bool                    presettled; /// Proton does not have a notion of pre-settled. This flag is introduced in Dispatch and should exclusively be used only to update management counters like presettled delivery counts on links etc. This flag DOES NOT represent the remote settlement state of the delivery.
    qdr_delivery_where_t    where;
    uint8_t                 tag[QDR_DELIVERY_TAG_MAX];
    int                     tag_length;
    qd_bitmask_t           *link_exclusion;
    qdr_address_t          *tracking_addr;
    int                     tracking_addr_bit;
    int                     ingress_index;
    qdr_link_work_t        *link_work;         ///< Delivery work item for this delivery
    qdr_subscription_ref_list_t subscriptions;
    qdr_delivery_ref_list_t peers;             /// Use this list if there if the delivery has more than one peer.
    bool                    multicast;         /// True if this delivery is targeted for a multicast address.
    bool                    via_edge;          /// True if this delivery arrived via an edge-connection.
    bool                    stuck;             /// True if this delivery was counted as stuck.
};

ALLOC_DECLARE(qdr_delivery_t);


///////////////////////////////////////////////////////////////////////////////
//                               Delivery API
///////////////////////////////////////////////////////////////////////////////


bool qdr_delivery_receive_complete(const qdr_delivery_t *delivery);
bool qdr_delivery_send_complete(const qdr_delivery_t *delivery);
bool qdr_delivery_oversize(const qdr_delivery_t *delivery);

void qdr_delivery_set_context(qdr_delivery_t *delivery, void *context);
void *qdr_delivery_get_context(const qdr_delivery_t *delivery);

void qdr_delivery_tag(const qdr_delivery_t *delivery, const char **tag, int *length);
bool qdr_delivery_tag_sent(const qdr_delivery_t *delivery);
void qdr_delivery_set_tag_sent(const qdr_delivery_t *delivery, bool tag_sent);

// note: access to _local_ endpoint disposition (not remote endpoint)
uint64_t qdr_delivery_disposition(const qdr_delivery_t *delivery);
void qdr_delivery_set_disposition(qdr_delivery_t *delivery, uint64_t disposition);

void qdr_delivery_set_aborted(const qdr_delivery_t *delivery, bool aborted);
bool qdr_delivery_is_aborted(const qdr_delivery_t *delivery);

qd_message_t *qdr_delivery_message(const qdr_delivery_t *delivery);
qdr_error_t *qdr_delivery_error(const qdr_delivery_t *delivery);
qdr_link_t *qdr_delivery_link(const qdr_delivery_t *delivery);
bool qdr_delivery_presettled(const qdr_delivery_t *delivery);

void qdr_delivery_incref(qdr_delivery_t *delivery, const char *label);

pn_data_t *qdr_delivery_extension_state(qdr_delivery_t *dlv);
void qdr_delivery_set_extension_state(qdr_delivery_t *dlv, uint64_t disposition, pn_data_t* disposition_data, bool update_disposition);
void qdr_delivery_write_extension_state(qdr_delivery_t *dlv, pn_delivery_t* pdlv, bool update_disposition);


//
// I/O thread only functions
//


/* release dlv and possibly schedule its deletion on the core thread */
void qdr_delivery_decref(qdr_core_t *core, qdr_delivery_t *delivery, const char *label);

/** Set the presettled flag on the delivery to true if it is not already true.
 * The presettled flag can only go from false to true and not vice versa.
 * This function should only be called when the delivery has been discarded and receive_complete flag is true in which case there
 * will be no thread contention.
**/
void qdr_delivery_set_presettled(qdr_delivery_t *delivery);

/* handles delivery disposition and settlement changes from the remote end of
 * the link, and schedules Core thread */
void qdr_delivery_remote_state_updated(qdr_core_t *core, qdr_delivery_t *delivery, uint64_t disp,
                                       bool settled, qdr_error_t *error, pn_data_t *ext_state, bool ref_given);

/* invoked when incoming message data arrives - schedule core thread */
qdr_delivery_t *qdr_deliver_continue(qdr_core_t *core, qdr_delivery_t *delivery, bool settled);


//
// CORE thread only functions
//


/* update settlement and/or disposition and schedule I/O processing */
void qdr_delivery_release_CT(qdr_core_t *core, qdr_delivery_t *delivery);
void qdr_delivery_reject_CT(qdr_core_t *core, qdr_delivery_t *delivery);
void qdr_delivery_failed_CT(qdr_core_t *core, qdr_delivery_t *delivery);
bool qdr_delivery_settled_CT(qdr_core_t *core, qdr_delivery_t *delivery);

/* add dlv to links list of updated deliveries and schedule I/O thread processing */
void qdr_delivery_push_CT(qdr_core_t *core, qdr_delivery_t *dlv);

/* optimized decref for core thread */
void qdr_delivery_decref_CT(qdr_core_t *core, qdr_delivery_t *delivery, const char *label);

/* peer delivery list management*/
void qdr_delivery_link_peers_CT(qdr_delivery_t *in_dlv, qdr_delivery_t *out_dlv);
void qdr_delivery_unlink_peers_CT(qdr_core_t *core, qdr_delivery_t *dlv, qdr_delivery_t *peer);

/* peer iterator - warning: not reentrant! */
qdr_delivery_t *qdr_delivery_first_peer_CT(qdr_delivery_t *dlv);
qdr_delivery_t *qdr_delivery_next_peer_CT(qdr_delivery_t *dlv);

/* schedules all peer deliveries with work for I/O processing */
void qdr_deliver_continue_peers_CT(qdr_core_t *core, qdr_delivery_t *in_dlv);

/* update the links counters with respect to its delivery */
void qdr_delivery_increment_counters_CT(qdr_core_t *core, qdr_delivery_t *delivery);

/**
 * multicast delivery state and settlement management
 */

// remote updated disposition/settlement for incoming delivery
void qdr_delivery_mcast_inbound_update_CT(qdr_core_t *core, qdr_delivery_t *in_dlv,
                                          uint64_t new_disp, bool settled);
// remote update disposition/settlement for outgoing delivery
void qdr_delivery_mcast_outbound_update_CT(qdr_core_t *core, qdr_delivery_t *in_dlv,
                                           qdr_delivery_t *out_peer,
                                           uint64_t new_disp, bool settled);
// number of unsettled peer (outbound) deliveries for in_dlv
int qdr_delivery_peer_count_CT(const qdr_delivery_t *in_dlv);


#endif // __delivery_h__

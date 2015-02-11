#ifndef __router_private_h__
#define __router_private_h__ 1
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

/**@file
 * Router Private type definitions
 *@internal
 */

#include <qpid/dispatch/enum.h>
#include <qpid/dispatch/router.h>
#include <qpid/dispatch/message.h>
#include <qpid/dispatch/bitmask.h>
#include <qpid/dispatch/hash.h>
#include <qpid/dispatch/log.h>
#include "dispatch_private.h"
#include "entity.h"

qd_error_t qd_router_python_setup(qd_router_t *router);
void qd_router_python_free(qd_router_t *router);
qd_error_t qd_pyrouter_tick(qd_router_t *router);
qd_error_t qd_router_configure_address(qd_router_t *router, qd_entity_t *entity);
qd_error_t qd_router_configure_waypoint(qd_router_t *router, qd_entity_t *entity);
qd_error_t qd_router_configure_lrp(qd_router_t *router, qd_entity_t *entity);

void qd_router_configure_free(qd_router_t *router);

typedef enum {
    QD_ROUTER_MODE_STANDALONE,  ///< Standalone router.  No routing protocol participation
    QD_ROUTER_MODE_INTERIOR,    ///< Interior router.  Full participation in routing protocol.
    QD_ROUTER_MODE_EDGE,        ///< Edge router.  No transit-router capability.
    QD_ROUTER_MODE_ENDPOINT     ///< No routing except for internal modules (agent, etc.).
} qd_router_mode_t;
ENUM_DECLARE(qd_router_mode);

typedef enum {
    QD_LINK_ENDPOINT,   ///< A link to a connected endpoint
    QD_LINK_WAYPOINT,   ///< A link to a configured waypoint
    QD_LINK_ROUTER,     ///< A link to a peer router in the same area
    QD_LINK_AREA        ///< A link to a peer router in a different area (area boundary)
} qd_link_type_t;
ENUM_DECLARE(qd_link_type);

typedef struct qd_routed_event_t {
    DEQ_LINKS(struct qd_routed_event_t);
    qd_delivery_t *delivery;
    qd_message_t  *message;
    bool           settle;
    uint64_t       disposition;
} qd_routed_event_t;

extern const char *QD_ROUTER_NODE_TYPE;
extern const char *QD_ROUTER_ADDRESS_TYPE;
extern const char *QD_ROUTER_LINK_TYPE;

ALLOC_DECLARE(qd_routed_event_t);
DEQ_DECLARE(qd_routed_event_t, qd_routed_event_list_t);


struct qd_router_link_t {
    DEQ_LINKS(qd_router_link_t);
    int                     mask_bit;        ///< Unique mask bit if this is an inter-router link
    qd_link_type_t          link_type;
    qd_direction_t          link_direction;
    qd_address_t           *owning_addr;     ///< [ref] Address record that owns this link
    qd_waypoint_t          *waypoint;        ///< [ref] Waypoint that owns this link
    qd_link_t              *link;            ///< [own] Link pointer
    qd_router_link_t       *connected_link;  ///< [ref] If this is a link-route, reference the connected link
    qd_router_link_ref_t   *ref;             ///< Pointer to a containing reference object
    char                   *target;          ///< Target address for incoming links
    qd_routed_event_list_t  event_fifo;      ///< FIFO of outgoing delivery/link events (no messages)
    qd_routed_event_list_t  msg_fifo;        ///< FIFO of outgoing message deliveries
};

ALLOC_DECLARE(qd_router_link_t);
DEQ_DECLARE(qd_router_link_t, qd_router_link_list_t);

struct qd_router_node_t {
    DEQ_LINKS(qd_router_node_t);
    qd_address_t     *owning_addr;
    int               mask_bit;
    qd_router_node_t *next_hop;   ///< Next hop node _if_ this is not a neighbor node
    qd_router_link_t *peer_link;  ///< Outgoing link _if_ this is a neighbor node
    uint32_t          ref_count;
    qd_bitmask_t     *valid_origins;
};

ALLOC_DECLARE(qd_router_node_t);
DEQ_DECLARE(qd_router_node_t, qd_router_node_list_t);

struct qd_router_ref_t {
    DEQ_LINKS(qd_router_ref_t);
    qd_router_node_t *router;
};

ALLOC_DECLARE(qd_router_ref_t);
DEQ_DECLARE(qd_router_ref_t, qd_router_ref_list_t);


struct qd_router_link_ref_t {
    DEQ_LINKS(qd_router_link_ref_t);
    qd_router_link_t *link;
};

ALLOC_DECLARE(qd_router_link_ref_t);
DEQ_DECLARE(qd_router_link_ref_t, qd_router_link_ref_list_t);


struct qd_lrp_t {
    DEQ_LINKS(qd_lrp_t);
    char               *prefix;
    qd_lrp_container_t *container;
};

DEQ_DECLARE(qd_lrp_t, qd_lrp_list_t);


struct qd_lrp_container_t {
    DEQ_LINKS(qd_lrp_container_t);
    qd_dispatch_t         *qd;
    qd_config_connector_t *cc;
    qd_lrp_list_t          lrps;
    qd_timer_t            *timer;
    qd_connection_t       *conn;
};

DEQ_DECLARE(qd_lrp_container_t, qd_lrp_container_list_t);


struct qd_router_lrp_ref_t {
    DEQ_LINKS(qd_router_lrp_ref_t);
    qd_lrp_t *lrp;
};

ALLOC_DECLARE(qd_router_lrp_ref_t);
DEQ_DECLARE(qd_router_lrp_ref_t, qd_router_lrp_ref_list_t);


struct qd_router_conn_t {
    int ref_count;
    int mask_bit;
};

ALLOC_DECLARE(qd_router_conn_t);


/** A router address */
struct qd_address_t {
    DEQ_LINKS(qd_address_t);
    qd_router_message_cb_t     handler;          ///< In-Process Consumer
    void                      *handler_context;  ///< In-Process Consumer context
    qd_router_lrp_ref_list_t   lrps;             ///< Local link-route destinations
    qd_router_link_ref_list_t  rlinks;           ///< Locally-Connected Consumers
    qd_router_ref_list_t       rnodes;           ///< Remotely-Connected Consumers
    qd_hash_handle_t          *hash_handle;      ///< Linkage back to the hash table entry
    qd_address_semantics_t     semantics;
    qd_address_t              *redirect;
    qd_address_t              *static_cc;
    qd_address_t              *dynamic_cc;
    bool                       toggle;
    bool                       waypoint;
    bool                       block_deletion;

    //
    // TODO - Add support for asynchronous address lookup:
    //  - Add a FIFO for routed_events holding the message and delivery
    //  - Add an indication that the address is awaiting a lookup response
    //

    /**@name Statistics */
    ///@{
    uint64_t deliveries_ingress;
    uint64_t deliveries_egress;
    uint64_t deliveries_transit;
    uint64_t deliveries_to_container;
    uint64_t deliveries_from_container;
    ///@}
};

ALLOC_DECLARE(qd_address_t);
DEQ_DECLARE(qd_address_t, qd_address_list_t);

/** Constructor for qd_address_t */
qd_address_t* qd_address();

struct qd_config_phase_t {
    DEQ_LINKS(qd_config_phase_t);
    char                    phase;
    qd_address_semantics_t  semantics;
};

DEQ_DECLARE(qd_config_phase_t, qd_config_phase_list_t);

struct qd_config_address_t {
    DEQ_LINKS(qd_config_address_t);
    char                   *prefix;
    char                    last_phase;
    qd_config_phase_list_t  phases;
};

DEQ_DECLARE(qd_config_address_t, qd_config_address_list_t);

/**
 * A waypoint is a point on a multi-phase route where messages can exit and re-enter the router.
 *
 * NOTE: a message received by a waypoint is first sent OUT and then received back IN.
 */
struct qd_waypoint_t {
    DEQ_LINKS(qd_waypoint_t);
    char                  *address;
    char                   in_phase;       ///< Phase for re-entering message.
    char                   out_phase;      ///< Phase for exiting message.
    char                  *connector_name; ///< On-demand connector name for outgoing messages.
    qd_config_connector_t *connector;      ///< Connector for outgoing messages.
    qd_connection_t       *connection;     ///< Connection for outgoing messages.
    qd_link_t             *in_link;        ///< Link for re-entering messages.
    qd_link_t             *out_link;       ///< Link for exiting messages.
    qd_address_t          *in_address;     ///< Address for re-entering messages.
    qd_address_t          *out_address;    ///< Address for exiting messages.
    bool                   connected;      ///< True if connected.
};

DEQ_DECLARE(qd_waypoint_t, qd_waypoint_list_t);


struct qd_router_t {
    qd_dispatch_t            *qd;
    qd_log_source_t          *log_source;
    qd_router_mode_t          router_mode;
    const char               *router_area;
    const char               *router_id;
    qd_node_t                *node;

    qd_address_list_t         addrs;
    qd_hash_t                *addr_hash;
    qd_address_t             *router_addr;
    qd_address_t             *routerma_addr;
    qd_address_t             *hello_addr;

    qd_router_link_list_t     links;
    qd_router_node_list_t     routers;
    qd_lrp_container_list_t   lrp_containers;
    qd_router_link_t        **out_links_by_mask_bit;
    qd_router_node_t        **routers_by_mask_bit;

    qd_bitmask_t             *neighbor_free_mask;
    sys_mutex_t              *lock;
    qd_timer_t               *timer;
    uint64_t                  dtag;

    qd_config_address_list_t  config_addrs;
    qd_waypoint_list_t        waypoints;
};



void qd_router_check_addr(qd_router_t *router, qd_address_t *addr, int was_local);
void qd_router_add_link_ref_LH(qd_router_link_ref_list_t *ref_list, qd_router_link_t *link);
void qd_router_del_link_ref_LH(qd_router_link_ref_list_t *ref_list, qd_router_link_t *link);

void qd_router_add_node_ref_LH(qd_router_ref_list_t *ref_list, qd_router_node_t *rnode);
void qd_router_del_node_ref_LH(qd_router_ref_list_t *ref_list, qd_router_node_t *rnode);

void qd_router_add_lrp_ref_LH(qd_router_lrp_ref_list_t *ref_list, qd_lrp_t *lrp);
void qd_router_del_lrp_ref_LH(qd_router_lrp_ref_list_t *ref_list, qd_lrp_t *lrp);

void qd_router_mobile_added(qd_router_t *router, qd_field_iterator_t *iter);
void qd_router_mobile_removed(qd_router_t *router, const char *addr);
void qd_router_link_lost(qd_router_t *router, int link_mask_bit);

qd_address_semantics_t router_semantics_for_addr(qd_router_t *router, qd_field_iterator_t *iter,
                                                 char in_phase, char *out_phase);
#endif

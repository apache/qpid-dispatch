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

typedef struct qd_router_link_t     qd_router_link_t;
typedef struct qd_router_node_t     qd_router_node_t;
typedef struct qd_router_ref_t      qd_router_ref_t;
typedef struct qd_router_link_ref_t qd_router_link_ref_t;
typedef struct qd_router_conn_t     qd_router_conn_t;

void qd_router_python_setup(qd_router_t *router);
void qd_pyrouter_tick(qd_router_t *router);
void qd_router_agent_setup(qd_router_t *router);
void qd_router_configure(qd_router_t *router);

typedef enum {
    QD_ROUTER_MODE_STANDALONE,  // Standalone router.  No routing protocol participation
    QD_ROUTER_MODE_INTERIOR,    // Interior router.  Full participation in routing protocol.
    QD_ROUTER_MODE_EDGE         // Edge router.  No routing protocol participation, access via other protocols.
} qd_router_mode_t;

typedef enum {
    QD_LINK_ENDPOINT,   // A link to a connected endpoint
    QD_LINK_ROUTER,     // A link to a peer router in the same area
    QD_LINK_AREA        // A link to a peer router in a different area (area boundary)
} qd_link_type_t;


typedef struct qd_routed_event_t {
    DEQ_LINKS(struct qd_routed_event_t);
    qd_delivery_t *delivery;
    qd_message_t  *message;
    bool           settle;
    uint64_t       disposition;
} qd_routed_event_t;

ALLOC_DECLARE(qd_routed_event_t);
DEQ_DECLARE(qd_routed_event_t, qd_routed_event_list_t);


struct qd_router_link_t {
    DEQ_LINKS(qd_router_link_t);
    int                     mask_bit;        // Unique mask bit if this is an inter-router link
    qd_link_type_t          link_type;
    qd_direction_t          link_direction;
    qd_address_t           *owning_addr;     // [ref] Address record that owns this link
    qd_link_t              *link;            // [own] Link pointer
    qd_router_link_t       *connected_link;  // [ref] If this is a link-route, reference the connected link
    qd_router_link_t       *peer_link;       // [ref] If this is a bidirectional link-route, reference the peer link
    qd_router_link_ref_t   *ref;             // Pointer to a containing reference object
    qd_routed_event_list_t  event_fifo;      // FIFO of outgoing delivery/link events (no messages)
    qd_routed_event_list_t  msg_fifo;        // FIFO of outgoing message deliveries
};

ALLOC_DECLARE(qd_router_link_t);
DEQ_DECLARE(qd_router_link_t, qd_router_link_list_t);

struct qd_router_node_t {
    DEQ_LINKS(qd_router_node_t);
    qd_address_t     *owning_addr;
    int               mask_bit;
    qd_router_node_t *next_hop;   // Next hop node _if_ this is not a neighbor node
    qd_router_link_t *peer_link;  // Outgoing link _if_ this is a neighbor node
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


struct qd_router_conn_t {
    int mask_bit;
};

ALLOC_DECLARE(qd_router_conn_t);


struct qd_address_t {
    DEQ_LINKS(qd_address_t);
    qd_router_message_cb_t     handler;          // In-Process Consumer
    void                      *handler_context;  // In-Process Consumer context
    qd_router_link_ref_list_t  rlinks;           // Locally-Connected Consumers
    qd_router_ref_list_t       rnodes;           // Remotely-Connected Consumers
    qd_hash_handle_t          *hash_handle;      // Linkage back to the hash table entry
    qd_address_semantics_t     semantics;
    qd_address_t              *redirect;
    qd_address_t              *static_cc;
    qd_address_t              *dynamic_cc;
    bool                       toggle;

    //
    // TODO - Add support for asynchronous address lookup:
    //  - Add a FIFO for routed_events holding the message and delivery
    //  - Add an indication that the address is awaiting a lookup response
    //

    //
    // Statistics
    //
    uint64_t deliveries_ingress;
    uint64_t deliveries_egress;
    uint64_t deliveries_transit;
    uint64_t deliveries_to_container;
    uint64_t deliveries_from_container;
};

ALLOC_DECLARE(qd_address_t);
DEQ_DECLARE(qd_address_t, qd_address_list_t);


typedef struct {
    char                   *prefix;
    qd_address_semantics_t  semantics;
} qd_config_address_t;


struct qd_router_t {
    qd_dispatch_t          *qd;
    qd_router_mode_t        router_mode;
    const char             *router_area;
    const char             *router_id;
    qd_node_t              *node;

    qd_address_list_t       addrs;
    qd_hash_t              *addr_hash;
    qd_address_t           *router_addr;
    qd_address_t           *hello_addr;

    qd_router_link_list_t   links;
    qd_router_node_list_t   routers;
    qd_router_link_t      **out_links_by_mask_bit;
    qd_router_node_t      **routers_by_mask_bit;

    qd_bitmask_t           *neighbor_free_mask;
    sys_mutex_t            *lock;
    qd_timer_t             *timer;
    uint64_t                dtag;

    qd_config_address_t    *config_addrs;
    int                     config_addr_count;

    PyObject               *pyRouter;
    PyObject               *pyTick;
    PyObject               *pyAdded;
    PyObject               *pyRemoved;

    qd_agent_class_t       *class_router;
    qd_agent_class_t       *class_link;
    qd_agent_class_t       *class_node;
    qd_agent_class_t       *class_address;
};



void qd_router_check_addr(qd_router_t *router, qd_address_t *addr, int was_local);
void qd_router_add_link_ref_LH(qd_router_link_ref_list_t *ref_list, qd_router_link_t *link);
void qd_router_del_link_ref_LH(qd_router_link_ref_list_t *ref_list, qd_router_link_t *link);

void qd_router_add_node_ref_LH(qd_router_ref_list_t *ref_list, qd_router_node_t *rnode);
void qd_router_del_node_ref_LH(qd_router_ref_list_t *ref_list, qd_router_node_t *rnode);

void qd_router_mobile_added(qd_router_t *router, qd_field_iterator_t *iter);
void qd_router_mobile_removed(qd_router_t *router, const char *addr);


#endif

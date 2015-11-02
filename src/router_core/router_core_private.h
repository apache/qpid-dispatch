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

/**
 * qdr_field_t - This type is used to pass variable-length fields (strings, etc.) into
 *               and out of the router-core thread.
 */
typedef struct {
    qd_buffer_list_t     buffers;
    qd_field_iterator_t *iterator;
} qdr_field_t;

qdr_field_t *qdr_field(const char *string);
void qdr_field_free(qdr_field_t *field);


/**
 * qdr_action_t - This type represents one work item to be performed by the router-core thread.
 */
typedef struct qdr_action_t qdr_action_t;
typedef void (*qdr_action_handler_t) (qdr_core_t *core, qdr_action_t *action, bool discard);

struct qdr_action_t {
    DEQ_LINKS(qdr_action_t);
    qdr_action_handler_t action_handler;
    union {
        //
        // Arguments for router control-plane actions
        //
        struct {
            int                     link_maskbit;
            int                     router_maskbit;
            int                     nh_router_maskbit;
            qd_bitmask_t           *router_set;
            qdr_field_t            *address;
            char                    address_class;
            char                    address_phase;
            qd_address_semantics_t  semantics;
        } route_table;

        //
        // Arguments for in-process subscriptions
        //
        struct {
            qdr_field_t            *address;
            qd_address_semantics_t  semantics;
            char                    address_class;
            char                    address_phase;
            qdr_receive_t           on_message;
            void                   *context;
        } subscribe;

        //
        // Arguments for management-agent actions
        //
        struct {
            qdr_query_t *query;
            int          offset;
        } agent;
    } args;
};

ALLOC_DECLARE(qdr_action_t);
DEQ_DECLARE(qdr_action_t, qdr_action_list_t);

#define QDR_AGENT_MAX_COLUMNS 64
#define QDR_AGENT_COLUMN_NULL (QDR_AGENT_MAX_COLUMNS + 1)

struct qdr_query_t {
    DEQ_LINKS(qdr_query_t);
    qd_router_entity_type_t  entity_type;
    void                    *context;
    int                      columns[QDR_AGENT_MAX_COLUMNS];
    qd_composed_field_t     *body;
    qdr_field_t             *next_key;
    int                      next_offset;
    bool                     more;
    const qd_amqp_error_t   *status;
};

ALLOC_DECLARE(qdr_query_t);
DEQ_DECLARE(qdr_query_t, qdr_query_list_t); 


typedef struct qdr_address_t     qdr_address_t;
typedef struct qdr_node_t        qdr_node_t;
typedef struct qdr_router_ref_t  qdr_router_ref_t;
typedef struct qdr_link_ref_t    qdr_link_ref_t;
typedef struct qdr_lrp_t         qdr_lrp_t;
typedef struct qdr_lrp_ref_t     qdr_lrp_ref_t;

struct qdr_node_t {
    DEQ_LINKS(qdr_node_t);
    qdr_address_t    *owning_addr;
    int               mask_bit;
    qdr_node_t       *next_hop;   ///< Next hop node _if_ this is not a neighbor node
    qdr_link_t       *peer_link;  ///< Outgoing link _if_ this is a neighbor node
    uint32_t          ref_count;
    qd_bitmask_t     *valid_origins;
};

ALLOC_DECLARE(qdr_node_t);
DEQ_DECLARE(qdr_node_t, qdr_node_list_t);


struct qdr_router_ref_t {
    DEQ_LINKS(qdr_router_ref_t);
    qdr_node_t *router;
};

ALLOC_DECLARE(qdr_router_ref_t);
DEQ_DECLARE(qdr_router_ref_t, qdr_router_ref_list_t);


struct qdr_link_t {
    DEQ_LINKS(qdr_link_t);
    int                       mask_bit;        ///< Unique mask bit if this is an inter-router link
    qd_link_type_t            link_type;
    qd_direction_t            link_direction;
    qdr_address_t            *owning_addr;     ///< [ref] Address record that owns this link
    //qd_waypoint_t            *waypoint;        ///< [ref] Waypoint that owns this link
    qd_link_t                *link;            ///< [own] Link pointer
    qdr_link_t               *connected_link;  ///< [ref] If this is a link-route, reference the connected link
    qdr_link_ref_t           *ref;             ///< Pointer to a containing reference object
    char                     *target;          ///< Target address for incoming links
    qd_routed_event_list_t    event_fifo;      ///< FIFO of outgoing delivery/link events (no messages)
    qd_routed_event_list_t    msg_fifo;        ///< FIFO of incoming or outgoing message deliveries
    qd_router_delivery_list_t deliveries;      ///< [own] outstanding unsettled deliveries
    bool                      strip_inbound_annotations;  ///<should the dispatch specific inbound annotations be stripped at the ingress router
    bool                      strip_outbound_annotations; ///<should the dispatch specific outbound annotations be stripped at the egress router
};

ALLOC_DECLARE(qdr_link_t);
DEQ_DECLARE(qdr_link_t, qdr_link_list_t);

struct qdr_link_ref_t {
    DEQ_LINKS(qdr_link_ref_t);
    qdr_link_t *link;
};

ALLOC_DECLARE(qdr_link_ref_t);
DEQ_DECLARE(qdr_link_ref_t, qdr_link_ref_list_t);


struct qdr_lrp_t {
    DEQ_LINKS(qdr_lrp_t);
    char               *prefix;
    bool                inbound;
    bool                outbound;
    qd_lrp_container_t *container;
};

DEQ_DECLARE(qdr_lrp_t, qdr_lrp_list_t);

struct qdr_lrp_ref_t {
    DEQ_LINKS(qdr_lrp_ref_t);
    qdr_lrp_t *lrp;
};

ALLOC_DECLARE(qdr_lrp_ref_t);
DEQ_DECLARE(qdr_lrp_ref_t, qdr_lrp_ref_list_t);


struct qdr_address_t {
    DEQ_LINKS(qdr_address_t);
    qd_router_message_cb_t     on_message;          ///< In-Process Message Consumer
    void                      *on_message_context;  ///< In-Process Consumer context
    qdr_lrp_ref_list_t         lrps;                ///< Local link-route destinations
    qdr_link_ref_list_t        rlinks;              ///< Locally-Connected Consumers
    qdr_router_ref_list_t      rnodes;              ///< Remotely-Connected Consumers
    qd_hash_handle_t          *hash_handle;         ///< Linkage back to the hash table entry
    qd_address_semantics_t     semantics;
    bool                       toggle;
    bool                       waypoint;
    bool                       block_deletion;
    qd_router_forwarder_t     *forwarder;

    /**@name Statistics */
    ///@{
    uint64_t deliveries_ingress;
    uint64_t deliveries_egress;
    uint64_t deliveries_transit;
    uint64_t deliveries_to_container;
    uint64_t deliveries_from_container;
    ///@}
};

ALLOC_DECLARE(qdr_address_t);
DEQ_DECLARE(qdr_address_t, qdr_address_list_t);

qdr_address_t *qdr_address(qd_address_semantics_t semantics);
qdr_address_t *qdr_add_local_address_CT(qdr_core_t *core, const char *addr, qd_address_semantics_t semantics);

void qdr_add_link_ref(qdr_link_ref_list_t *ref_list, qdr_link_t *link);
void qdr_del_link_ref(qdr_link_ref_list_t *ref_list, qdr_link_t *link);

void qdr_add_node_ref(qdr_router_ref_list_t *ref_list, qdr_node_t *rnode);
void qdr_del_node_ref(qdr_router_ref_list_t *ref_list, qdr_node_t *rnode);


struct qdr_core_t {
    qd_dispatch_t     *qd;
    qd_log_source_t   *log;
    sys_thread_t      *thread;
    bool               running;
    qdr_action_list_t  action_list;
    sys_cond_t        *action_cond;
    sys_mutex_t       *action_lock;

    //
    // Agent section
    //
    qdr_query_list_t       outgoing_query_list;
    sys_mutex_t           *query_lock;
    qd_timer_t            *agent_timer;
    qdr_manage_response_t  agent_response_handler;

    //
    // Route table section
    //
    void                 *rt_context;
    qdr_mobile_added_t    rt_mobile_added;
    qdr_mobile_removed_t  rt_mobile_removed;
    qdr_link_lost_t       rt_link_lost;

    const char *router_area;
    const char *router_id;

    qdr_address_list_t    addrs;
    qd_hash_t            *addr_hash;
    qdr_address_t        *router_addr;
    qdr_address_t        *routerma_addr;
    qdr_address_t        *hello_addr;

    qdr_link_list_t       links;
    qdr_node_list_t       routers;
    qdr_node_t          **routers_by_mask_bit;
    qdr_link_t          **out_links_by_mask_bit;
};

void *router_core_thread(void *arg);
void  qdr_route_table_setup_CT(qdr_core_t *core);
void  qdr_agent_setup_CT(qdr_core_t *core);
qdr_action_t *qdr_action(qdr_action_handler_t action_handler);
void qdr_action_enqueue(qdr_core_t *core, qdr_action_t *action);
void qdr_agent_enqueue_response_CT(qdr_core_t *core, qdr_query_t *query);

#endif

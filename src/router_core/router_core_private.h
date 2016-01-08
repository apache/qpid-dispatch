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

typedef struct qdr_address_t     qdr_address_t;
typedef struct qdr_node_t        qdr_node_t;
typedef struct qdr_router_ref_t  qdr_router_ref_t;
typedef struct qdr_link_ref_t    qdr_link_ref_t;
typedef struct qdr_lrp_t         qdr_lrp_t;
typedef struct qdr_lrp_ref_t     qdr_lrp_ref_t;
typedef struct qdr_forwarder_t   qdr_forwarder_t;

qdr_forwarder_t *qdr_forwarder_CT(qdr_core_t *core, qd_address_semantics_t semantics);
int qdr_forward_message_CT(qdr_core_t *core, qdr_address_t *addr, qd_message_t *msg, qdr_delivery_t *in_delivery,
                           bool exclude_inprocess, bool control);
void qdr_forward_attach_CT(qdr_core_t *core, qdr_forwarder_t *forwarder, qdr_link_t *in_link);

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
char *qdr_field_copy(qdr_field_t *field);

/**
 * qdr_action_t - This type represents one work item to be performed by the router-core thread.
 */
typedef struct qdr_action_t qdr_action_t;
typedef void (*qdr_action_handler_t) (qdr_core_t *core, qdr_action_t *action, bool discard);

struct qdr_action_t {
    DEQ_LINKS(qdr_action_t);
    qdr_action_handler_t  action_handler;
    const char           *label;
    union {
        //
        // Arguments for router control-plane actions
        //
        struct {
            int           link_maskbit;
            int           router_maskbit;
            int           nh_router_maskbit;
            qd_bitmask_t *router_set;
            qdr_field_t  *address;
        } route_table;

        //
        // Arguments for connection-level actions
        //
        struct {
            qdr_connection_t *conn;
            qdr_link_t       *link;
            qdr_delivery_t   *delivery;
            qd_message_t     *msg;
            qd_direction_t    dir;
            qdr_terminus_t   *source;
            qdr_terminus_t   *target;
            qdr_error_t      *error;
            qd_detach_type_t  dt;
        } connection;

        //
        // Arguments for in-process messaging
        //
        struct {
            qdr_field_t            *address;
            char                    address_class;
            char                    address_phase;
            qd_address_semantics_t  semantics;
            qdr_subscription_t     *subscription;
            qd_message_t           *message;
            bool                    exclude_inprocess;
            bool                    control;
        } io;

        //
        // Arguments for management-agent actions
        //
        struct {
            qdr_query_t             *query;
            int                      offset;
            qd_field_iterator_t     *identity;
            qd_field_iterator_t     *name;
            qd_parsed_field_t       *in_body;
        } agent;

    } args;
};

ALLOC_DECLARE(qdr_action_t);
DEQ_DECLARE(qdr_action_t, qdr_action_list_t);

#define QDR_AGENT_MAX_COLUMNS 64
#define QDR_AGENT_COLUMN_NULL (QDR_AGENT_MAX_COLUMNS + 1)

struct qdr_query_t {
    DEQ_LINKS(qdr_query_t);
    qdr_core_t              *core;
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


struct qdr_node_t {
    DEQ_LINKS(qdr_node_t);
    qdr_address_t    *owning_addr;
    int               mask_bit;
    qdr_node_t       *next_hop;           ///< Next hop node _if_ this is not a neighbor node
    qdr_link_t       *peer_control_link;  ///< Outgoing control link _if_ this is a neighbor node
    qdr_link_t       *peer_data_link;     ///< Outgoing data link _if_ this is a neighbor node
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


struct qdr_delivery_t {
    DEQ_LINKS(qdr_delivery_t);
    void                *context;
    qdr_link_t          *link;
    qdr_delivery_t      *peer;
    qd_message_t        *msg;
    qd_field_iterator_t *to_addr;
    qd_field_iterator_t *origin;
    uint64_t             disposition;
    bool                 settled;
};

ALLOC_DECLARE(qdr_delivery_t);
DEQ_DECLARE(qdr_delivery_t, qdr_delivery_list_t);


struct qdr_link_t {
    DEQ_LINKS(qdr_link_t);
    qdr_core_t          *core;
    void                *user_context;
    qdr_connection_t    *conn;            ///< [ref] Connection that owns this link
    qd_link_type_t       link_type;
    qd_direction_t       link_direction;
    char                *name;
    qdr_address_t       *owning_addr;     ///< [ref] Address record that owns this link
    qdr_link_t          *connected_link;  ///< [ref] If this is a link-route, reference the connected link
    qdr_link_ref_t      *ref;             ///< Pointer to a containing reference object (TODO - check this!)
    qdr_delivery_list_t  undelivered;     ///< Deliveries to be forwarded or sent
    qdr_delivery_list_t  unsettled;       ///< Unsettled deliveries
    bool                 strip_annotations_in;
    bool                 strip_annotations_out;
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

struct qdr_subscription_t {
    DEQ_LINKS(qdr_subscription_t);
    qdr_core_t    *core;
    qdr_address_t *addr;
    qdr_receive_t  on_message;
    void          *on_message_context;
};

DEQ_DECLARE(qdr_subscription_t, qdr_subscription_list_t);


struct qdr_address_t {
    DEQ_LINKS(qdr_address_t);
    qdr_subscription_list_t  subscriptions; ///< In-process message subscribers
    qdr_lrp_ref_list_t       lrps;          ///< Local link-route destinations
    qdr_link_ref_list_t      rlinks;        ///< Locally-Connected Consumers
    qdr_link_ref_list_t      inlinks;       ///< Locally-Connected Producers
    qdr_router_ref_list_t    rnodes;        ///< Remotely-Connected Consumers
    qd_hash_handle_t        *hash_handle;   ///< Linkage back to the hash table entry
    qdr_forwarder_t         *forwarder;
    bool                     toggle;
    bool                     waypoint;
    bool                     block_deletion;

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

qdr_address_t *qdr_address_CT(qdr_core_t *core, qd_address_semantics_t semantics);
qdr_address_t *qdr_add_local_address_CT(qdr_core_t *core, char aclass, const char *addr, qd_address_semantics_t semantics);

void qdr_add_link_ref(qdr_link_ref_list_t *ref_list, qdr_link_t *link);
void qdr_del_link_ref(qdr_link_ref_list_t *ref_list, qdr_link_t *link);

void qdr_add_node_ref(qdr_router_ref_list_t *ref_list, qdr_node_t *rnode);
void qdr_del_node_ref(qdr_router_ref_list_t *ref_list, qdr_node_t *rnode);


//
// General Work
//
// The following types are used to post work to the IO threads for
// non-connection-specific action.  These actions are serialized through
// a zero-delay timer and are processed by one thread at a time.  General
// actions occur in-order and are not run concurrently.
//
typedef struct qdr_general_work_t qdr_general_work_t;
typedef void (*qdr_general_work_handler_t) (qdr_core_t *core, qdr_general_work_t *work);

struct qdr_general_work_t {
    DEQ_LINKS(qdr_general_work_t);
    qdr_general_work_handler_t  handler;
    qdr_field_t                *field;
    int                         maskbit;
};

ALLOC_DECLARE(qdr_general_work_t);
DEQ_DECLARE(qdr_general_work_t, qdr_general_work_list_t);

qdr_general_work_t *qdr_general_work(qdr_general_work_handler_t handler);

//
// Connection Work
//
// The following types are used to post work to the IO threads for
// connection-specific action.  The actions for a particular connection
// are run in-order and are not concurrent.  Actions for different connections
// will run concurrently.
//
typedef enum {
    QDR_CONNECTION_WORK_FIRST_ATTACH,
    QDR_CONNECTION_WORK_SECOND_ATTACH,
    QDR_CONNECTION_WORK_DETACH
} qdr_connection_work_type_t;

typedef struct qdr_connection_work_t {
    DEQ_LINKS(struct qdr_connection_work_t);
    qdr_connection_work_type_t  work_type;
    qdr_link_t                 *link;
    qdr_terminus_t             *source;
    qdr_terminus_t             *target;
    qdr_error_t                *error;
} qdr_connection_work_t;

ALLOC_DECLARE(qdr_connection_work_t);
DEQ_DECLARE(qdr_connection_work_t, qdr_connection_work_list_t);


struct qdr_connection_t {
    DEQ_LINKS(qdr_connection_t);
    qdr_core_t                 *core;
    void                       *user_context;
    bool                        incoming;
    qdr_connection_role_t       role;
    const char                 *label;
    bool                        strip_annotations_in;
    bool                        strip_annotations_out;
    int                         mask_bit;
    qdr_link_list_t             links;
    qdr_connection_work_list_t  work_list;
    sys_mutex_t                *work_lock;
};

ALLOC_DECLARE(qdr_connection_t);
DEQ_DECLARE(qdr_connection_t, qdr_connection_list_t);


struct qdr_core_t {
    qd_dispatch_t     *qd;
    qd_log_source_t   *log;
    sys_thread_t      *thread;
    bool               running;
    qdr_action_list_t  action_list;
    sys_cond_t        *action_cond;
    sys_mutex_t       *action_lock;

    sys_mutex_t             *work_lock;
    qdr_general_work_list_t  work_list;
    qd_timer_t              *work_timer;

    qdr_connection_list_t open_connections;

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

    //
    // Connection section
    //
    void                      *user_context;
    qdr_connection_activate_t  activate_handler;
    qdr_link_first_attach_t    first_attach_handler;
    qdr_link_second_attach_t   second_attach_handler;
    qdr_link_detach_t          detach_handler;
    qdr_link_flow_t            flow_handler;

    const char *router_area;
    const char *router_id;

    qdr_address_list_t    addrs;
    qd_hash_t            *addr_hash;
    qdr_address_t        *hello_addr;
    qdr_address_t        *router_addr_L;
    qdr_address_t        *routerma_addr_L;
    qdr_address_t        *router_addr_T;
    qdr_address_t        *routerma_addr_T;

    qdr_node_list_t       routers;
    qd_bitmask_t         *neighbor_free_mask;
    qdr_node_t          **routers_by_mask_bit;
    qdr_link_t          **control_links_by_mask_bit;
    qdr_link_t          **data_links_by_mask_bit;

    qdr_forwarder_t      *forwarders[QD_SEMANTICS_LINK_BALANCED + 1];
};

typedef enum {
    PASSTHROUGH,
    TAP,
    BYPASS
} qdr_waypoint_mode_t;

void *router_core_thread(void *arg);
void  qdr_route_table_setup_CT(qdr_core_t *core);
void  qdr_agent_setup_CT(qdr_core_t *core);
void  qdr_forwarder_setup_CT(qdr_core_t *core);
qdr_action_t *qdr_action(qdr_action_handler_t action_handler, const char *label);
void qdr_action_enqueue(qdr_core_t *core, qdr_action_t *action);
void qdr_agent_enqueue_response_CT(qdr_core_t *core, qdr_query_t *query);

void qdr_post_mobile_added_CT(qdr_core_t *core, const char *address_hash);
void qdr_post_mobile_removed_CT(qdr_core_t *core, const char *address_hash);
void qdr_post_link_lost_CT(qdr_core_t *core, int link_maskbit);

void qdr_post_general_work_CT(qdr_core_t *core, qdr_general_work_t *work);
void qdr_check_addr_CT(qdr_core_t *core, qdr_address_t *addr, bool was_local);

void qdr_connection_activate_CT(qdr_core_t *core, qdr_connection_t *conn);

qdr_query_t *qdr_query(qdr_core_t              *core,
                       void                    *context,
                       qd_router_entity_type_t  type,
                       qd_parsed_field_t       *attribute_names,
                       qd_composed_field_t     *body);
#endif

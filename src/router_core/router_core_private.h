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

typedef struct qdr_address_t         qdr_address_t;
typedef struct qdr_address_config_t  qdr_address_config_t;
typedef struct qdr_node_t            qdr_node_t;
typedef struct qdr_router_ref_t      qdr_router_ref_t;
typedef struct qdr_link_ref_t        qdr_link_ref_t;
typedef struct qdr_forwarder_t       qdr_forwarder_t;
typedef struct qdr_link_route_t      qdr_link_route_t;
typedef struct qdr_auto_link_t       qdr_auto_link_t;
typedef struct qdr_conn_identifier_t qdr_conn_identifier_t;
typedef struct qdr_connection_ref_t  qdr_connection_ref_t;

qdr_forwarder_t *qdr_forwarder_CT(qdr_core_t *core, qd_address_treatment_t treatment);
int qdr_forward_message_CT(qdr_core_t *core, qdr_address_t *addr, qd_message_t *msg, qdr_delivery_t *in_delivery,
                           bool exclude_inprocess, bool control);
bool qdr_forward_attach_CT(qdr_core_t *core, qdr_address_t *addr, qdr_link_t *in_link, qdr_terminus_t *source,
                           qdr_terminus_t *target);

typedef enum {
    QDR_CONDITION_NO_ROUTE_TO_DESTINATION,
    QDR_CONDITION_ROUTED_LINK_LOST,
    QDR_CONDITION_FORBIDDEN,
    QDR_CONDITION_WRONG_ROLE,
    QDR_CONDITION_NONE
} qdr_condition_t;

/**
 * qdr_field_t - This type is used to pass variable-length fields (strings, etc.) into
 *               and out of the router-core thread.
 */
typedef struct {
    qd_buffer_list_t     buffers;
    qd_field_iterator_t *iterator;
} qdr_field_t;

qdr_field_t *qdr_field(const char *string);
qdr_field_t *qdr_field_from_iter(qd_field_iterator_t *iter);
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
            int           cost;
            qd_bitmask_t *router_set;
            qdr_field_t  *address;
        } route_table;

        //
        // Arguments for connection-level actions
        //
        struct {
            qdr_connection_t *conn;
            qdr_field_t      *connection_label;
            qdr_field_t      *container_id;
            qdr_link_t       *link;
            qdr_delivery_t   *delivery;
            qd_message_t     *msg;
            qd_direction_t    dir;
            qdr_terminus_t   *source;
            qdr_terminus_t   *target;
            qdr_error_t      *error;
            qd_detach_type_t  dt;
            int               credit;
            bool              drain;
            uint8_t           tag[32];
            int               tag_length;
        } connection;

        //
        // Arguments for delivery state updates
        //
        struct {
            qdr_delivery_t *delivery;
            uint64_t        disposition;
            bool            settled;
        } delivery;

        //
        // Arguments for in-process messaging
        //
        struct {
            qdr_field_t            *address;
            char                    address_class;
            char                    address_phase;
            qd_address_treatment_t  treatment;
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
    qd_amqp_error_t          status;
};

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
    int               cost;
};

ALLOC_DECLARE(qdr_node_t);
DEQ_DECLARE(qdr_node_t, qdr_node_list_t);


struct qdr_router_ref_t {
    DEQ_LINKS(qdr_router_ref_t);
    qdr_node_t *router;
};

ALLOC_DECLARE(qdr_router_ref_t);
DEQ_DECLARE(qdr_router_ref_t, qdr_router_ref_list_t);

typedef enum {
    QDR_DELIVERY_NOWHERE = 0,
    QDR_DELIVERY_IN_UNDELIVERED,
    QDR_DELIVERY_IN_UNSETTLED
} qdr_delivery_where_t;

struct qdr_delivery_t {
    DEQ_LINKS(qdr_delivery_t);
    void                *context;
    int                  ref_count;
    qdr_link_t          *link;
    qdr_delivery_t      *peer;
    qd_message_t        *msg;
    qd_field_iterator_t *to_addr;
    qd_field_iterator_t *origin;
    uint64_t             disposition;
    bool                 settled;
    qdr_delivery_where_t where;
    uint8_t              tag[32];
    int                  tag_length;
    qd_bitmask_t        *link_exclusion;
    qdr_address_t       *tracking_addr;
};

ALLOC_DECLARE(qdr_delivery_t);
DEQ_DECLARE(qdr_delivery_t, qdr_delivery_list_t);

typedef struct qdr_delivery_ref_t {
    DEQ_LINKS(struct qdr_delivery_ref_t);
    qdr_delivery_t *dlv;
} qdr_delivery_ref_t;

ALLOC_DECLARE(qdr_delivery_ref_t);
DEQ_DECLARE(qdr_delivery_ref_t, qdr_delivery_ref_list_t);

void qdr_add_delivery_ref(qdr_delivery_ref_list_t *list, qdr_delivery_t *dlv);
void qdr_del_delivery_ref(qdr_delivery_ref_list_t *list, qdr_delivery_ref_t *ref);

#define QDR_LINK_LIST_CLASS_ADDRESS    0
#define QDR_LINK_LIST_CLASS_DELIVERY   1
#define QDR_LINK_LIST_CLASS_FLOW       2
#define QDR_LINK_LIST_CLASS_CONNECTION 3
#define QDR_LINK_LIST_CLASSES          4

typedef enum {
    QDR_LINK_OPER_UP,
    QDR_LINK_OPER_DOWN,
    QDR_LINK_OPER_QUIESCING,
    QDR_LINK_OPER_IDLE
} qdr_link_oper_status_t;

struct qdr_link_t {
    DEQ_LINKS(qdr_link_t);
    qdr_core_t              *core;
    uint64_t                 identity;
    void                    *user_context;
    qdr_connection_t        *conn;               ///< [ref] Connection that owns this link
    qd_link_type_t           link_type;
    qd_direction_t           link_direction;
    char                    *name;
    int                      detach_count;       ///< 0, 1, or 2 depending on the state of the lifecycle
    qdr_address_t           *owning_addr;        ///< [ref] Address record that owns this link
    qdr_link_t              *connected_link;     ///< [ref] If this is a link-route, reference the connected link
    qdr_link_ref_t          *ref[QDR_LINK_LIST_CLASSES];  ///< Pointers to containing reference objects
    qdr_auto_link_t         *auto_link;          ///< [ref] Auto_link that owns this link
    qdr_delivery_list_t      undelivered;        ///< Deliveries to be forwarded or sent
    qdr_delivery_list_t      unsettled;          ///< Unsettled deliveries
    qdr_delivery_ref_list_t  updated_deliveries; ///< References to deliveries (in the unsettled list) with updates.
    bool                     admin_enabled;
    qdr_link_oper_status_t   oper_status;
    bool                     strip_annotations_in;
    bool                     strip_annotations_out;
    int                      capacity;
    int                      incremental_credit_CT;
    int                      incremental_credit;
    bool                     flow_started;   ///< for incoming, true iff initial credit has been granted
    bool                     drain_mode;
    bool                     drain_mode_changed;
    int                      credit_to_core; ///< Number of the available credits incrementally given to the core
    uint64_t                 total_deliveries;
};

ALLOC_DECLARE(qdr_link_t);
DEQ_DECLARE(qdr_link_t, qdr_link_list_t);

struct qdr_link_ref_t {
    DEQ_LINKS(qdr_link_ref_t);
    qdr_link_t *link;
};

ALLOC_DECLARE(qdr_link_ref_t);
DEQ_DECLARE(qdr_link_ref_t, qdr_link_ref_list_t);

void qdr_add_link_ref(qdr_link_ref_list_t *ref_list, qdr_link_t *link, int cls);
void qdr_del_link_ref(qdr_link_ref_list_t *ref_list, qdr_link_t *link, int cls);


struct qdr_connection_ref_t {
    DEQ_LINKS(qdr_connection_ref_t);
    qdr_connection_t *conn;
};

ALLOC_DECLARE(qdr_connection_ref_t);
DEQ_DECLARE(qdr_connection_ref_t, qdr_connection_ref_list_t);

void qdr_add_connection_ref(qdr_connection_ref_list_t *ref_list, qdr_connection_t *conn);
void qdr_del_connection_ref(qdr_connection_ref_list_t *ref_list, qdr_connection_t *conn);


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
    qdr_subscription_list_t    subscriptions; ///< In-process message subscribers
    qdr_connection_ref_list_t  conns;         ///< Local Connections for route-destinations
    qdr_link_ref_list_t        rlinks;        ///< Locally-Connected Consumers
    qdr_link_ref_list_t        inlinks;       ///< Locally-Connected Producers
    qd_bitmask_t              *rnodes;        ///< Bitmask of remote routers with connected consumers
    qd_hash_handle_t          *hash_handle;   ///< Linkage back to the hash table entry
    qd_address_treatment_t     treatment;
    qdr_forwarder_t           *forwarder;
    int                        ref_count;     ///< Number of link-routes + auto-links referencing this address
    bool                       block_deletion;
    bool                       local;
    uint32_t                   tracked_deliveries;
    uint64_t                   cost_epoch;

    //
    // State for "closest" treatment
    //
    qd_bitmask_t *closest_remotes;
    int           next_remote;

    //
    // State for "balanced" treatment
    //
    int *outstanding_deliveries;
    
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

qdr_address_t *qdr_address_CT(qdr_core_t *core, qd_address_treatment_t treatment);
qdr_address_t *qdr_add_local_address_CT(qdr_core_t *core, char aclass, const char *addr, qd_address_treatment_t treatment);

void qdr_add_node_ref(qdr_router_ref_list_t *ref_list, qdr_node_t *rnode);
void qdr_del_node_ref(qdr_router_ref_list_t *ref_list, qdr_node_t *rnode);

struct qdr_address_config_t {
    DEQ_LINKS(qdr_address_config_t);
    qd_hash_handle_t       *hash_handle;
    char                   *name;
    uint64_t                identity;
    qd_address_treatment_t  treatment;
    int                     in_phase;
    int                     out_phase;
};

ALLOC_DECLARE(qdr_address_config_t);
DEQ_DECLARE(qdr_address_config_t, qdr_address_config_list_t);


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
    int                         inter_router_cost;
    qdr_receive_t               on_message;
    void                       *on_message_context;
    qd_message_t               *msg;
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
    QDR_CONNECTION_WORK_FIRST_DETACH,
    QDR_CONNECTION_WORK_SECOND_DETACH
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
    int                         inter_router_cost;
    qdr_conn_identifier_t      *conn_id;
    bool                        strip_annotations_in;
    bool                        strip_annotations_out;
    int                         link_capacity;
    int                         mask_bit;
    uint64_t                    management_id; // A unique identifier for the qdr_connection_t copied over from qd_connection_t.
    qdr_connection_work_list_t  work_list;
    sys_mutex_t                *work_lock;
    qdr_link_ref_list_t         links;
    qdr_link_ref_list_t         links_with_deliveries;
    qdr_link_ref_list_t         links_with_credit;
};

ALLOC_DECLARE(qdr_connection_t);
DEQ_DECLARE(qdr_connection_t, qdr_connection_list_t);


struct qdr_link_route_t {
    DEQ_LINKS(qdr_link_route_t);
    DEQ_LINKS_N(REF, qdr_link_route_t);
    uint64_t                identity;
    char                   *name;
    qdr_address_t          *addr;
    qd_direction_t          dir;
    qdr_conn_identifier_t  *conn_id;
    qd_address_treatment_t  treatment;
    bool                    active;
};

ALLOC_DECLARE(qdr_link_route_t);
DEQ_DECLARE(qdr_link_route_t, qdr_link_route_list_t);


typedef enum {
    QDR_AUTO_LINK_STATE_INACTIVE,
    QDR_AUTO_LINK_STATE_ATTACHING,
    QDR_AUTO_LINK_STATE_FAILED,
    QDR_AUTO_LINK_STATE_ACTIVE,
    QDR_AUTO_LINK_STATE_QUIESCING,
    QDR_AUTO_LINK_STATE_IDLE
} qdr_auto_link_state_t;

struct qdr_auto_link_t {
    DEQ_LINKS(qdr_auto_link_t);
    DEQ_LINKS_N(REF, qdr_auto_link_t);
    uint64_t               identity;
    char                  *name;
    qdr_address_t         *addr;
    int                    phase;
    qd_direction_t         dir;
    qdr_conn_identifier_t *conn_id;
    qdr_link_t            *link;
    qdr_auto_link_state_t  state;
    char                  *last_error;
};

ALLOC_DECLARE(qdr_auto_link_t);
DEQ_DECLARE(qdr_auto_link_t, qdr_auto_link_list_t);


struct qdr_conn_identifier_t {
    qd_hash_handle_t      *hash_handle;
    qdr_connection_t      *open_connection;
    qdr_link_route_list_t  link_route_refs;
    qdr_auto_link_list_t   auto_link_refs;
};

ALLOC_DECLARE(qdr_conn_identifier_t);


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
    qdr_link_list_t       open_links;
    //
    // Agent section
    //
    qdr_query_list_t       outgoing_query_list;
    sys_mutex_t           *query_lock;
    qd_timer_t            *agent_timer;
    qdr_manage_response_t  agent_response_handler;
    qdr_subscription_t    *agent_subscription_mobile;
    qdr_subscription_t    *agent_subscription_local;

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
    qdr_link_offer_t           offer_handler;
    qdr_link_drained_t         drained_handler;
    qdr_link_drain_t           drain_handler;
    qdr_link_push_t            push_handler;
    qdr_link_deliver_t         deliver_handler;
    qdr_delivery_update_t      delivery_update_handler;

    qd_router_mode_t  router_mode;
    const char       *router_area;
    const char       *router_id;

    qdr_address_config_list_t  addr_config;
    qdr_auto_link_list_t       auto_links;
    qdr_link_route_list_t      link_routes;
    qd_hash_t                 *conn_id_hash;
    qdr_address_list_t         addrs;
    qd_hash_t                 *addr_hash;
    qdr_address_t             *hello_addr;
    qdr_address_t             *router_addr_L;
    qdr_address_t             *routerma_addr_L;
    qdr_address_t             *router_addr_T;
    qdr_address_t             *routerma_addr_T;

    qdr_node_list_t       routers;            ///< List of routers, in order of cost, from lowest to highest
    qd_bitmask_t         *neighbor_free_mask;
    qdr_node_t          **routers_by_mask_bit;
    qdr_link_t          **control_links_by_mask_bit;
    qdr_link_t          **data_links_by_mask_bit;
    uint64_t              cost_epoch;

    uint64_t              next_tag;

    uint64_t              next_identifier;
    sys_mutex_t          *id_lock;


    qdr_forwarder_t      *forwarders[QD_TREATMENT_LINK_BALANCED + 1];
};

void *router_core_thread(void *arg);
uint64_t qdr_identifier(qdr_core_t* core);
void qdr_management_agent_on_message(void *context, qd_message_t *msg, int link_id, int cost);
void  qdr_route_table_setup_CT(qdr_core_t *core);
void  qdr_agent_setup_CT(qdr_core_t *core);
void  qdr_forwarder_setup_CT(qdr_core_t *core);
qdr_action_t *qdr_action(qdr_action_handler_t action_handler, const char *label);
void qdr_action_enqueue(qdr_core_t *core, qdr_action_t *action);
void qdr_link_issue_credit_CT(qdr_core_t *core, qdr_link_t *link, int credit, bool drain);
void qdr_addr_start_inlinks_CT(qdr_core_t *core, qdr_address_t *addr);
void qdr_delivery_push_CT(qdr_core_t *core, qdr_delivery_t *dlv);
void qdr_delivery_release_CT(qdr_core_t *core, qdr_delivery_t *delivery);
void qdr_delivery_failed_CT(qdr_core_t *core, qdr_delivery_t *delivery);
bool qdr_delivery_settled_CT(qdr_core_t *core, qdr_delivery_t *delivery);
void qdr_agent_enqueue_response_CT(qdr_core_t *core, qdr_query_t *query);

void qdr_post_mobile_added_CT(qdr_core_t *core, const char *address_hash);
void qdr_post_mobile_removed_CT(qdr_core_t *core, const char *address_hash);
void qdr_post_link_lost_CT(qdr_core_t *core, int link_maskbit);

void qdr_post_general_work_CT(qdr_core_t *core, qdr_general_work_t *work);
void qdr_check_addr_CT(qdr_core_t *core, qdr_address_t *addr, bool was_local);

qdr_delivery_t *qdr_forward_new_delivery_CT(qdr_core_t *core, qdr_delivery_t *peer, qdr_link_t *link, qd_message_t *msg);
void qdr_forward_deliver_CT(qdr_core_t *core, qdr_link_t *link, qdr_delivery_t *dlv);
void qdr_connection_activate_CT(qdr_core_t *core, qdr_connection_t *conn);
qd_address_treatment_t qdr_treatment_for_address_CT(qdr_core_t *core, qd_field_iterator_t *iter, int *in_phase, int *out_phase);
qd_address_treatment_t qdr_treatment_for_address_hash_CT(qdr_core_t *core, qd_field_iterator_t *iter);

void qdr_connection_enqueue_work_CT(qdr_core_t            *core,
                                    qdr_connection_t      *conn,
                                    qdr_connection_work_t *work);

qdr_link_t *qdr_create_link_CT(qdr_core_t       *core,
                               qdr_connection_t *conn,
                               qd_link_type_t    link_type,
                               qd_direction_t    dir,
                               qdr_terminus_t   *source,
                               qdr_terminus_t   *target);

void qdr_link_outbound_detach_CT(qdr_core_t *core, qdr_link_t *link, qdr_error_t *error, qdr_condition_t condition);

qdr_query_t *qdr_query(qdr_core_t              *core,
                       void                    *context,
                       qd_router_entity_type_t  type,
                       qd_composed_field_t     *body);
#endif

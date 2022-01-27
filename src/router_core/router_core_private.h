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
#include "message_private.h"

#include "qpid/dispatch/atomic.h"
#include "qpid/dispatch/log.h"
#include "qpid/dispatch/protocol_adaptor.h"
#include "qpid/dispatch/threading.h"

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
typedef struct qdr_exchange          qdr_exchange_t;
typedef struct qdr_edge_t            qdr_edge_t;
typedef struct qdr_agent_t           qdr_agent_t;

ALLOC_DECLARE(qdr_address_t);
ALLOC_DECLARE(qdr_address_config_t);
ALLOC_DECLARE(qdr_node_t);
ALLOC_DECLARE(qdr_router_ref_t);
ALLOC_DECLARE(qdr_link_ref_t);
ALLOC_DECLARE(qdr_link_route_t);
ALLOC_DECLARE(qdr_auto_link_t);
ALLOC_DECLARE(qdr_conn_identifier_t);
ALLOC_DECLARE(qdr_connection_ref_t);

ALLOC_DECLARE(qdr_connection_t);
ALLOC_DECLARE(qdr_link_t);

#include "core_attach_address_lookup.h"
#include "core_events.h"
#include "core_link_endpoint.h"

qdr_forwarder_t *qdr_forwarder_CT(qdr_core_t *core, qd_address_treatment_t treatment);
int qdr_forward_message_CT(qdr_core_t *core, qdr_address_t *addr, qd_message_t *msg, qdr_delivery_t *in_delivery,
                           bool exclude_inprocess, bool control);
bool qdr_forward_attach_CT(qdr_core_t *core, qdr_address_t *addr, qdr_link_t *in_link, qdr_terminus_t *source,
                           qdr_terminus_t *target);
void qdr_forward_link_direct_CT(qdr_core_t       *core,
                                qdr_connection_t *conn,
                                qdr_link_t       *in_link,
                                qdr_terminus_t   *source,
                                qdr_terminus_t   *target,
                                char             *strip,
                                char             *insert);

typedef enum {
    QDR_CONDITION_NO_ROUTE_TO_DESTINATION,
    QDR_CONDITION_ROUTED_LINK_LOST,
    QDR_CONDITION_FORBIDDEN,
    QDR_CONDITION_WRONG_ROLE,
    QDR_CONDITION_COORDINATOR_PRECONDITION_FAILED,
    QDR_CONDITION_INVALID_LINK_EXPIRATION,
    QDR_CONDITION_NONE
} qdr_condition_t;

/**
 * qdr_field_t - This type is used to pass variable-length fields (strings, etc.) into
 *               and out of the router-core thread.
 */
typedef struct {
    qd_buffer_list_t  buffers;
    qd_iterator_t    *iterator;
} qdr_field_t;

qdr_field_t *qdr_field(const char *string);
qdr_field_t *qdr_field_from_iter(qd_iterator_t *iter);
qd_iterator_t *qdr_field_iterator(qdr_field_t *field);
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
            int           treatment_hint;
            qd_bitmask_t *router_set;
            qdr_field_t  *address;
        } route_table;

        //
        // Arguments for connection-level actions
        //
        struct {
            qdr_connection_t_sp  conn;
            qdr_field_t         *connection_label;
            qdr_field_t         *container_id;
            qdr_link_t_sp        link;
            qd_direction_t       dir;
            qdr_terminus_t      *source;
            qdr_terminus_t      *target;
            qdr_error_t         *error;
            qd_detach_type_t     dt;
            int                  credit;
            bool                 drain;
            bool                 enable_protocol_trace;
            qdr_delivery_t      *initial_delivery;
        } connection;

        //
        // Arguments for delivery state updates
        //
        struct {
            qdr_delivery_t      *delivery;
            qd_delivery_state_t *dstate;
            uint64_t             disposition;
            uint8_t              tag[32];
            int                  tag_length;
            bool                 settled;
            bool                 presettled;  // true if remote settles while msg is in flight
            bool                 more;  // true if there are more frames arriving, false otherwise
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
            qdr_field_t             *identity;
            qdr_field_t             *name;
            qd_parsed_field_t       *in_body;
            qd_buffer_list_t         body_buffers;
        } agent;

        //
        // Arguments for stats request actions
        //
        struct {
            qdr_global_stats_t             *stats;
            qdr_global_stats_handler_t     handler;
            void                           *context;
        } stats_request;

        //
        // Arguments for general use
        //
        struct {
            void *context_1;
            void *context_2;
            void *context_3;
            void *context_4;
        } general;

    } args;
};

ALLOC_DECLARE(qdr_action_t);
DEQ_DECLARE(qdr_action_t, qdr_action_list_t);

//
//
//
typedef struct qdr_delivery_cleanup_t qdr_delivery_cleanup_t;

struct qdr_delivery_cleanup_t {
    DEQ_LINKS(qdr_delivery_cleanup_t);
    qd_message_t  *msg;
    qd_iterator_t *iter;
};

ALLOC_DECLARE(qdr_delivery_cleanup_t);
DEQ_DECLARE(qdr_delivery_cleanup_t, qdr_delivery_cleanup_list_t);

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
    qdr_general_work_handler_t   handler;
    int                          maskbit;
    int                          inter_router_cost;
    qd_message_t                *msg;
    qdr_receive_t                on_message;
    void                        *on_message_context;
    uint64_t                     in_conn_id;
    uint64_t                     mobile_seq;
    const qd_policy_spec_t      *policy_spec;
    qdr_delivery_t              *delivery;
    qdr_delivery_cleanup_list_t  delivery_cleanup_list;
    qdr_global_stats_handler_t   stats_handler;
    void                        *context;
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
    QDR_CONNECTION_WORK_TRACING_ON,
    QDR_CONNECTION_WORK_TRACING_OFF

} qdr_connection_work_type_t;

typedef struct qdr_connection_work_t {
    DEQ_LINKS(struct qdr_connection_work_t);
    qdr_connection_work_type_t  work_type;
    qdr_link_t                 *link;
    qdr_terminus_t             *source;
    qdr_terminus_t             *target;
    qd_session_class_t          ssn_class;
} qdr_connection_work_t;

ALLOC_DECLARE(qdr_connection_work_t);
DEQ_DECLARE(qdr_connection_work_t, qdr_connection_work_list_t);
void qdr_connection_work_free_CT(qdr_connection_work_t *work);

//
// Link Work
//
// The following type is used to post link-specific work to the IO threads.
// This ensures that work related to a particular link (deliveries, disposition
// updates, flow updates, and detaches) are processed in-order.
//
// DELIVERY      - Push up to _value_ deliveries from the undelivered list to the
//                 link (outgoing links only).  Don't push more than there is
//                 available credit for.  If the full number of deliveries (_value_)
//                 cannot be pushed, don't consume this work item from the list.
//                 This link will be blocked until further credit is received.
// FLOW          - Push a flow update using _drain_action_ and _value_ for the
//                 number of incremental credits.
// FIRST_DETACH  - Issue a first detach on this link, using _error_ if there is an
//                 error condition.
// SECOND_DETACH - Issue a second detach on this link.
//
typedef enum {
    QDR_LINK_WORK_DELIVERY,
    QDR_LINK_WORK_FLOW,
    QDR_LINK_WORK_FIRST_DETACH,
    QDR_LINK_WORK_SECOND_DETACH
} qdr_link_work_type_t;

typedef enum {
    QDR_LINK_WORK_DRAIN_ACTION_NONE = 0,
    QDR_LINK_WORK_DRAIN_ACTION_SET,
    QDR_LINK_WORK_DRAIN_ACTION_CLEAR,
    QDR_LINK_WORK_DRAIN_ACTION_DRAINED
} qdr_link_work_drain_action_t;

typedef struct qdr_link_work_t {
    DEQ_LINKS(struct qdr_link_work_t);
    qdr_link_work_type_t          work_type;
    sys_atomic_t                  ref_count;
    qdr_error_t                  *error;
    int                           value;
    qdr_link_work_drain_action_t  drain_action;
    bool                          close_link;
    bool                          processing;
} qdr_link_work_t;

DEQ_DECLARE(qdr_link_work_t, qdr_link_work_list_t);

qdr_link_work_t *qdr_link_work(qdr_link_work_type_t type);
qdr_link_work_t *qdr_link_work_getref(qdr_link_work_t *work);
void qdr_link_work_release(qdr_link_work_t *work);


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
    uint64_t                 in_conn;  // or perhaps a pointer???
};

DEQ_DECLARE(qdr_query_t, qdr_query_list_t); 

struct qdr_node_t {
    DEQ_LINKS(qdr_node_t);
    qdr_address_t    *owning_addr;
    int               mask_bit;
    qdr_node_t       *next_hop;           ///< Next hop node _if_ this is not a neighbor node
    int               conn_mask_bit;      ///< qdr_connection_t->mask_bit inter-router conn if this is a neighbor node
    uint32_t          ref_count;
    qd_bitmask_t     *valid_origins;
    int               cost;
    uint64_t          mobile_seq;
    char             *wire_address_ma;    ///< The address of this router's mobile-sync agent in non-hashed form
    uint32_t          sync_mask;          ///< Bitmask for mobile-address-sync
};

DEQ_DECLARE(qdr_node_t, qdr_node_list_t);
void qdr_router_node_free(qdr_core_t *core, qdr_node_t *rnode);

struct qdr_router_ref_t {
    DEQ_LINKS(qdr_router_ref_t);
    qdr_node_t *router;
};

DEQ_DECLARE(qdr_router_ref_t, qdr_router_ref_list_t);

typedef struct qdr_delivery_ref_t {
    DEQ_LINKS(struct qdr_delivery_ref_t);
    qdr_delivery_t *dlv;
} qdr_delivery_ref_t;

ALLOC_DECLARE(qdr_delivery_ref_t);
DEQ_DECLARE(qdr_delivery_ref_t, qdr_delivery_ref_list_t);

struct qdr_subscription_t {
    DEQ_LINKS(qdr_subscription_t);
    qdr_core_t    *core;
    qdr_address_t *addr;
    qdr_receive_t  on_message;
    void          *on_message_context;
    bool           in_core;
};

DEQ_DECLARE(qdr_subscription_t, qdr_subscription_list_t);

typedef struct qdr_subscription_ref_t {
    DEQ_LINKS(struct qdr_subscription_ref_t);
    qdr_subscription_t *sub;
} qdr_subscription_ref_t;

ALLOC_DECLARE(qdr_subscription_ref_t);
DEQ_DECLARE(qdr_subscription_ref_t, qdr_subscription_ref_list_t);

void qdr_add_subscription_ref_CT(qdr_subscription_ref_list_t *list, qdr_subscription_t *sub);
void qdr_del_subscription_ref_CT(qdr_subscription_ref_list_t *list, qdr_subscription_ref_t *ref);

DEQ_DECLARE(qdr_delivery_t, qdr_delivery_list_t);

void qdr_add_delivery_ref_CT(qdr_delivery_ref_list_t *list, qdr_delivery_t *dlv);
void qdr_del_delivery_ref(qdr_delivery_ref_list_t *list, qdr_delivery_ref_t *ref);

#define QDR_LINK_LIST_CLASS_ADDRESS    0
#define QDR_LINK_LIST_CLASS_WORK       1
#define QDR_LINK_LIST_CLASS_CONNECTION 2
#define QDR_LINK_LIST_CLASS_LOCAL      3
#define QDR_LINK_LIST_CLASSES          4

typedef enum {
    QDR_LINK_OPER_UP,
    QDR_LINK_OPER_DOWN,
    QDR_LINK_OPER_QUIESCING,
    QDR_LINK_OPER_IDLE
} qdr_link_oper_status_t;

#define QDR_LINK_RATE_DEPTH 5

struct qdr_link_t {
    DEQ_LINKS(qdr_link_t);
    qdr_core_t              *core;
    uint64_t                 identity;
    void                    *user_context;
    void                    *edge_context;       ///< Opaque context to be used for edge-related purposes
    qdr_connection_t        *conn;               ///< [ref] Connection that owns this link
    qd_link_type_t           link_type;
    qd_direction_t           link_direction;
    qdr_link_work_list_t     work_list;
    char                    *name;
    char                    *disambiguated_name;
    char                    *terminus_addr;
    int                      attach_count;       ///< 1 or 2 depending on the state of the lifecycle
    int                      detach_count;       ///< 0, 1, or 2 depending on the state of the lifecycle
    qdr_address_t           *owning_addr;        ///< [ref] Address record that owns this link
    int                      phase;
    qdr_link_t              *connected_link;     ///< [ref] If this is a link-route, reference the connected link
    qdrc_endpoint_t         *core_endpoint;      ///< [ref] Set if this link terminates on an in-core endpoint
    qdr_link_ref_t          *ref[QDR_LINK_LIST_CLASSES];  ///< Pointers to containing reference objects
    qdr_auto_link_t         *auto_link;          ///< [ref] Auto_link that owns this link
    qdr_delivery_list_t      undelivered;        ///< Deliveries to be forwarded or sent
    qdr_delivery_list_t      unsettled;          ///< Unsettled deliveries
    qdr_delivery_list_t      settled;            ///< Settled deliveries
    qdr_delivery_ref_list_t  updated_deliveries; ///< References to deliveries (in the unsettled list) with updates.
    qdr_link_oper_status_t   oper_status;
    int                      capacity;
    int                      credit_to_core;    ///< Number of the available credits incrementally given to the core
    int                      credit_pending;    ///< Number of credits to be issued once consumers are available
    int                      credit_stored;     ///< Number of credits given to the link before it was ready to process them.
    int                      credit_reported;   ///< Number of credits to expose to management
    uint32_t                 zero_credit_time;  ///< Number of core ticks when credit last went to zero
    bool                     reported_as_blocked; ///< The fact that this link has been blocked with zero credit has been logged
    bool                     admin_enabled;
    bool                     strip_annotations_in;
    bool                     strip_annotations_out;
    bool                     drain_mode;
    bool                     stalled_outbound;  ///< Indicates that this link is stalled on outbound buffer backpressure
    bool                     detach_received;   ///< True on core receipt of inbound attach
    bool                     detach_send_done;  ///< True once the detach has been sent by the I/O thread
    bool                     edge;              ///< True if this link is in an edge-connection
    bool                     processing;        ///< True if an IO thread is currently handling this link
    bool                     ready_to_free;     ///< True if the core thread wanted to clean up the link but it was processing
    bool                     fallback;          ///< True if this link is attached to a fallback destination for an address
    bool                     streaming;         ///< True if this link can be reused for streaming msgs
    bool                     in_streaming_pool; ///< True if this link is in the connections standby pool STREAMING_POOL
    bool                     terminus_survives_disconnect;
    bool                     no_route;          ///< True if this link is to not receive routed deliveries
    bool                     no_route_bound;    // Has the no_route link been bound ? Has the link's owning address been set for no_route links ?
    char                    *strip_prefix;
    char                    *insert_prefix;

    uint64_t  total_deliveries;
    uint64_t  presettled_deliveries;
    uint64_t  dropped_presettled_deliveries;
    uint64_t  accepted_deliveries;
    uint64_t  rejected_deliveries;
    uint64_t  released_deliveries;
    uint64_t  modified_deliveries;
    uint64_t  deliveries_delayed_1sec;
    uint64_t  deliveries_delayed_10sec;
    uint64_t  deliveries_stuck;
    uint64_t  settled_deliveries[QDR_LINK_RATE_DEPTH];
    uint64_t *ingress_histogram;
    uint8_t   priority;
    uint8_t   rate_cursor;
    uint32_t  core_ticks;
    uint64_t  conn_id;

    DEQ_LINKS_N(STREAMING_POOL, qdr_link_t);
};
DEQ_DECLARE(qdr_link_t, qdr_link_list_t);

struct qdr_link_ref_t {
    DEQ_LINKS(qdr_link_ref_t);
    qdr_link_t *link;
};

DEQ_DECLARE(qdr_link_ref_t, qdr_link_ref_list_t);

void qdr_add_link_ref(qdr_link_ref_list_t *ref_list, qdr_link_t *link, int cls);
bool qdr_del_link_ref(qdr_link_ref_list_t *ref_list, qdr_link_t *link, int cls);
void move_link_ref(qdr_link_t *link, int from_cls, int to_cls);


struct qdr_connection_ref_t {
    DEQ_LINKS(qdr_connection_ref_t);
    qdr_connection_t *conn;
};

DEQ_DECLARE(qdr_connection_ref_t, qdr_connection_ref_list_t);

void qdr_add_connection_ref(qdr_connection_ref_list_t *ref_list, qdr_connection_t *conn);
void qdr_del_connection_ref(qdr_connection_ref_list_t *ref_list, qdr_connection_t *conn);

struct qdr_address_t {
    DEQ_LINKS(qdr_address_t);
    qdr_address_config_t      *config;
    qdr_subscription_list_t    subscriptions; ///< In-process message subscribers
    qdr_connection_ref_list_t  conns;         ///< Local Connections for route-destinations
    qdr_link_ref_list_t        rlinks;        ///< Locally-Connected Consumers
    qdr_link_ref_list_t        inlinks;       ///< Locally-Connected Producers
    qd_bitmask_t              *rnodes;        ///< Bitmask of remote routers with connected consumers
    qd_hash_handle_t          *hash_handle;   ///< Linkage back to the hash table entry
    qdrc_endpoint_desc_t      *core_endpoint; ///< [ref] Set if this address is bound to an in-core endpoint
    void                      *core_endpoint_context;
    qdr_link_t                *edge_inlink;   ///< [ref] In-link from connected Interior router (on edge router)
    qdr_link_t                *edge_outlink;  ///< [ref] Out-link to connected Interior router (on edge router)
    qd_address_treatment_t     treatment;
    qdr_forwarder_t           *forwarder;
    int                        ref_count;     ///< Number of entities referencing this address
    bool                       local;
    bool                       router_control_only; ///< If set, address is only for deliveries arriving on a control link
    uint32_t                   tracked_deliveries;
    uint64_t                   cost_epoch;

    //
    // State for mobile-address synchronization
    //
    DEQ_LINKS_N(SYNC_ADD, qdr_address_t);
    DEQ_LINKS_N(SYNC_DEL, qdr_address_t);
    uint32_t sync_mask;

    //
    // State for tracking fallback destinations for undeliverable deliveries
    //
    qdr_address_t *fallback;     ///< Pointer to this address's fallback destination
    qdr_address_t *fallback_for; ///< Pointer to the address that this is a fallback for

    //
    // State for "closest" treatment
    //
    qd_bitmask_t *closest_remotes;
    int           next_remote;

    //
    // State for "balanced" treatment, indexed by inter-router connection mask bit
    //
    int *outstanding_deliveries;

    //
    // State for "exchange" treatment
    //
    qdr_exchange_t *exchange;  // weak ref

    //
    // State for "link balanced" treatment
    //
    char *add_prefix;
    char *del_prefix;

    /**@name Statistics */
    ///@{
    uint64_t deliveries_ingress;
    uint64_t deliveries_egress;
    uint64_t deliveries_transit;
    uint64_t deliveries_to_container;
    uint64_t deliveries_from_container;
    uint64_t deliveries_egress_route_container;
    uint64_t deliveries_ingress_route_container;
    uint64_t deliveries_redirected;

    ///@}

    int priority;
};

DEQ_DECLARE(qdr_address_t, qdr_address_list_t);

qdr_address_t *qdr_address_CT(qdr_core_t *core, qd_address_treatment_t treatment, qdr_address_config_t *config);
qdr_address_t *qdr_add_local_address_CT(qdr_core_t *core, char aclass, const char *addr, qd_address_treatment_t treatment);
qdr_address_t *qdr_add_mobile_address_CT(qdr_core_t *core, const char* prefix, const char *addr, qd_address_treatment_t treatment, bool edge);
void qdr_core_remove_address(qdr_core_t *core, qdr_address_t *addr);
void qdr_core_bind_address_link_CT(qdr_core_t *core, qdr_address_t *addr, qdr_link_t *link);
void qdr_core_unbind_address_link_CT(qdr_core_t *core, qdr_address_t *addr, qdr_link_t *link);
void qdr_core_bind_address_conn_CT(qdr_core_t *core, qdr_address_t *addr, qdr_connection_t *conn);
void qdr_core_unbind_address_conn_CT(qdr_core_t *core, qdr_address_t *addr, qdr_connection_t *conn);
void qdr_setup_fallback_address_CT(qdr_core_t *core, qdr_address_t *addr);

struct qdr_address_config_t {
    DEQ_LINKS(qdr_address_config_t);
    char                   *name;
    uint64_t                identity;
    uint32_t                ref_count;
    char                   *pattern;
    bool                    fallback;
    bool                    is_prefix;
    qd_address_treatment_t  treatment;
    int                     in_phase;
    int                     out_phase;
    int                     priority;
    qd_hash_handle_t        *hash_handle;
};

DEQ_DECLARE(qdr_address_config_t, qdr_address_config_list_t);
void qdr_core_remove_address_config(qdr_core_t *core, qdr_address_config_t *addr);
bool qdr_is_addr_treatment_multicast(qdr_address_t *addr);

//
// Connection Information
//
// This record is used to give the core thread access to the details
// of a connection's configuration.
//

struct qdr_connection_info_t {
    char                       *container;
    char                       *sasl_mechanisms;
    char                       *host;
    char                       *ssl_proto;
    char                       *ssl_cipher;
    char                       *user;
    bool                        is_authenticated;
    bool                        is_encrypted;
    bool                        opened;
    bool                        streaming_links;  // will allow streaming links
    qd_direction_t              dir;
    qdr_connection_role_t       role;
    pn_data_t                  *connection_properties;
    bool                        ssl;
    int                         ssl_ssf; //ssl strength factor
    char                       *version; // if role is router or edge
};

ALLOC_DECLARE(qdr_connection_info_t);

DEQ_DECLARE(qdr_link_route_t, qdr_link_route_list_t);


struct qdr_connection_t {
    DEQ_LINKS(qdr_connection_t);
    DEQ_LINKS_N(ACTIVATE, qdr_connection_t);
    qdr_protocol_adaptor_t     *protocol_adaptor;
    uint64_t                    identity;
    qdr_core_t                 *core;
    bool                        incoming;
    bool                        in_activate_list;
    bool                        closed; // This bit is used in the case where a client is trying to force close this connection.
    uint8_t                     next_pri;  // for incoming inter-router data links
    qdr_connection_role_t       role;
    int                         inter_router_cost;
    qdr_conn_identifier_t      *conn_id;
    qdr_conn_identifier_t      *alt_conn_id;
    bool                        strip_annotations_in;
    bool                        strip_annotations_out;
    int                         link_capacity;
    int                         mask_bit;  ///< set only if inter-router connection
    qdr_connection_work_list_t  work_list;
    sys_mutex_t                *work_lock;
    qdr_link_ref_list_t         links;
    qdr_link_ref_list_t         links_with_work[QDR_N_PRIORITIES];
    char                       *tenant_space;
    int                         tenant_space_len;
    qdr_connection_info_t      *connection_info;
    void                       *user_context; /* Updated from IO thread, use work_lock */
    qdr_link_route_list_t       conn_link_routes;  // connection scoped link routes
    qd_conn_oper_status_t       oper_status;
    qd_conn_admin_status_t      admin_status;
    qdr_error_t                *error;
    uint32_t                    conn_uptime; // Timestamp which can be used to calculate the number of seconds this connection has been up and running.
    uint32_t                    last_delivery_time; // Timestamp which can be used to calculate the number of seconds since the last delivery arrived on this connection.
    bool                        enable_protocol_trace; // Has trace level logging been turned on for this connection.
    bool                        has_streaming_links;   ///< one or more of this connection's links are for streaming messages
    qdr_link_list_t             streaming_link_pool;   ///< pool of links available for streaming messages
    const qd_policy_spec_t     *policy_spec;
};

DEQ_DECLARE(qdr_connection_t, qdr_connection_list_t);

#define QDR_IS_LINK_ROUTE_PREFIX(p) ((p) == QD_ITER_HASH_PREFIX_LINKROUTE_ADDR_IN || (p) == QD_ITER_HASH_PREFIX_LINKROUTE_ADDR_OUT)
#define QDR_IS_LINK_ROUTE(p) ((p) == QD_ITER_HASH_PREFIX_LINKROUTE_PATTERN_IN || (p) == QD_ITER_HASH_PREFIX_LINKROUTE_PATTERN_OUT || QDR_IS_LINK_ROUTE_PREFIX(p))
#define QDR_LINK_ROUTE_DIR(p) (((p) == QD_ITER_HASH_PREFIX_LINKROUTE_ADDR_IN || (p) == QD_ITER_HASH_PREFIX_LINKROUTE_PATTERN_IN) ? QD_INCOMING : QD_OUTGOING)
#define QDR_LINK_ROUTE_HASH(dir, is_prefix) \
    (((dir) == QD_INCOMING)                 \
     ? ((is_prefix) ? QD_ITER_HASH_PREFIX_LINKROUTE_ADDR_IN  : QD_ITER_HASH_PREFIX_LINKROUTE_PATTERN_IN)    \
     : ((is_prefix) ? QD_ITER_HASH_PREFIX_LINKROUTE_ADDR_OUT : QD_ITER_HASH_PREFIX_LINKROUTE_PATTERN_OUT))

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
    bool                    is_prefix;
    char                   *pattern;
    char                   *add_prefix;
    char                   *del_prefix;
    qdr_connection_t       *parent_conn;
    qd_hash_handle_t       *hash_handle;
};

void qdr_core_delete_link_route(qdr_core_t *core, qdr_link_route_t *lr);
void qdr_core_delete_auto_link (qdr_core_t *core,  qdr_auto_link_t *al);

// Core timer related field/data structures
typedef void (*qdr_timer_cb_t)(qdr_core_t *core, void* context);
typedef qdr_address_t * (*qdr_edge_conn_addr_t) (void *context);

typedef struct qdr_core_timer_t {
    DEQ_LINKS(struct qdr_core_timer_t);
    qdr_timer_cb_t  handler;
    void           *context;
    uint32_t        delta_time_seconds;
    bool            scheduled;
} qdr_core_timer_t;

ALLOC_DECLARE(qdr_core_timer_t);
DEQ_DECLARE(qdr_core_timer_t, qdr_core_timer_list_t);


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
    char                  *external_addr;
    const char            *internal_addr;
    int                    phase;
    int                    retry_attempts;
    qd_direction_t         dir;
    qdr_conn_identifier_t *conn_id;
    qdr_link_t            *link;
    qdr_auto_link_state_t  state;
    qdr_core_timer_t      *retry_timer; // If the auto link attach fails or gets disconnected, this timer retries the attach.
    char                  *last_error;
    bool                   fallback;   // True iff this auto-link attaches to a fallback destination for an address.
    qd_hash_handle_t      *hash_handle;
};

DEQ_DECLARE(qdr_auto_link_t, qdr_auto_link_list_t);


struct qdr_conn_identifier_t {
    qd_hash_handle_t          *connection_hash_handle;
    qd_hash_handle_t          *container_hash_handle;
    qdr_connection_ref_list_t  connection_refs;
    qdr_link_route_list_t      link_route_refs;
    qdr_auto_link_list_t       auto_link_refs;
};

DEQ_DECLARE(qdr_exchange_t, qdr_exchange_list_t);

typedef struct qdr_priority_sheaf_t {
    qdr_link_t *links[QDR_N_PRIORITIES];
    int count;
} qdr_priority_sheaf_t;


struct qdr_protocol_adaptor_t {
    DEQ_LINKS(qdr_protocol_adaptor_t);
    const char *name;

    //
    // Callbacks
    //
    void                     *user_context;
    qdr_connection_activate_t activate_handler;
    qdr_link_first_attach_t   first_attach_handler;
    qdr_link_second_attach_t  second_attach_handler;
    qdr_link_detach_t         detach_handler;
    qdr_link_flow_t           flow_handler;
    qdr_link_offer_t          offer_handler;
    qdr_link_drained_t        drained_handler;
    qdr_link_drain_t          drain_handler;
    qdr_link_push_t           push_handler;
    qdr_link_deliver_t        deliver_handler;
    qdr_link_get_credit_t     get_credit_handler;
    qdr_delivery_update_t     delivery_update_handler;
    qdr_connection_close_t    conn_close_handler;
    qdr_connection_trace_t    conn_trace_handler;
};

DEQ_DECLARE(qdr_protocol_adaptor_t, qdr_protocol_adaptor_list_t);


struct qdr_core_t {
    qd_dispatch_t     *qd;
    qd_log_source_t   *log;
    qd_log_source_t   *agent_log;
    sys_thread_t      *thread;

    qdr_action_list_t  action_list_background;  /// Actions processed only when the action_list is empty
    qdr_action_list_t  action_list;
    sys_cond_t        *action_cond;
    sys_mutex_t       *action_lock;
    bool               running;
    bool               sleeping;

    sys_mutex_t             *work_lock;
    qdr_core_timer_list_t    scheduled_timers;
    qdr_general_work_list_t  work_list;
    qd_timer_t              *work_timer;
    sys_atomic_t             uptime_ticks;

    qdr_protocol_adaptor_list_t  protocol_adaptors;
    qdr_connection_list_t        open_connections;
    qdr_connection_t            *active_edge_connection;
    qdr_connection_list_t        connections_to_activate;
    qdr_link_list_t              open_links;
    qdr_connection_ref_list_t    streaming_connections;

    qdrc_attach_addr_lookup_t  addr_lookup_handler;
    void                      *addr_lookup_context;

    qdr_agent_t               *mgmt_agent;

    //
    // Route table section
    //
    void                    *rt_context;
    qdr_set_mobile_seq_t     rt_set_mobile_seq;
    qdr_set_my_mobile_seq_t  rt_set_my_mobile_seq;
    qdr_link_lost_t          rt_link_lost;

    //
    // Events section
    //
    qdrc_event_subscription_list_t conn_event_subscriptions;
    qdrc_event_subscription_list_t link_event_subscriptions;
    qdrc_event_subscription_list_t addr_event_subscriptions;
    qdrc_event_subscription_list_t router_event_subscriptions;

    qd_router_mode_t  router_mode;
    const char       *router_area;
    const char       *router_id;
    int               worker_thread_count;

    qdr_address_config_list_t  addr_config;
    // Hash to hold names of address configs, link routes and auto links.
    // address config prefix = 'C', auto link prefix = 'A', link route prefix = 'L'
    qd_hash_t                 *addr_lr_al_hash;
    qdr_auto_link_list_t       auto_links;
    qdr_link_route_list_t      link_routes;
    qd_hash_t                 *conn_id_hash;
    qdr_address_list_t         addrs;
    qd_hash_t                 *addr_hash;
    qd_parse_tree_t           *addr_parse_tree;
    qd_parse_tree_t           *link_route_tree[2];   // QD_INCOMING, QD_OUTGOING
    qdr_address_t             *hello_addr;
    qdr_address_t             *router_addr_L;
    qdr_address_t             *routerma_addr_L;
    qdr_address_t             *router_addr_T;
    qdr_address_t             *routerma_addr_T;

    qdr_node_list_t       routers;            ///< List of routers, in order of cost, from lowest to highest
    qd_bitmask_t         *neighbor_free_mask;        ///< bits available for new conns (qd_connection_t->mask_bit values)
    qdr_node_t          **routers_by_mask_bit;       ///< indexed by qdr_node_t->mask_bit
    qdr_connection_t    **rnode_conns_by_mask_bit;   ///< inter-router conns indexed by conn->mask_bit
    qdr_link_t          **control_links_by_mask_bit; ///< indexed by qdr_node_t->link_mask_bit, qdr_connection_t->mask_bit
    qdr_priority_sheaf_t *data_links_by_mask_bit;    ///< indexed by qdr_node_t->link_mask_bit, qdr_connection_t->mask_bit
    uint64_t              cost_epoch;

    uint64_t              next_tag;

    uint64_t              next_identifier;
    sys_mutex_t          *id_lock;

    qdr_exchange_list_t   exchanges;
    qdr_forwarder_t      *forwarders[QD_TREATMENT_LINK_BALANCED + 1];

    qdr_delivery_cleanup_list_t  delivery_cleanup_list;  ///< List of delivery cleanup items to be processed in an IO thread

    // Overall delivery counters
    uint64_t presettled_deliveries;
    uint64_t dropped_presettled_deliveries;
    uint64_t accepted_deliveries;
    uint64_t rejected_deliveries;
    uint64_t released_deliveries;
    uint64_t modified_deliveries;
    uint64_t deliveries_ingress;
    uint64_t deliveries_egress;
    uint64_t deliveries_transit;
    uint64_t deliveries_egress_route_container;
    uint64_t deliveries_ingress_route_container;
    uint64_t deliveries_delayed_1sec;
    uint64_t deliveries_delayed_10sec;
    uint64_t deliveries_stuck;
    uint64_t deliveries_redirected;
    uint32_t links_blocked;

    qdr_edge_conn_addr_t          edge_conn_addr;
    void                         *edge_context;
};

struct qdr_terminus_t {
    qdr_field_t            *address;
    pn_durability_t         durability;
    pn_expiry_policy_t      expiry_policy;
    pn_seconds_t            timeout;
    bool                    dynamic;
    bool                    coordinator;
    pn_distribution_mode_t  distribution_mode;
    pn_data_t              *properties;
    pn_data_t              *filter;
    pn_data_t              *outcomes;
    pn_data_t              *capabilities;
};

ALLOC_DECLARE(qdr_terminus_t);

void *router_core_thread(void *arg);
uint64_t qdr_identifier(qdr_core_t* core);
uint64_t qdr_management_agent_on_message(void *context, qd_message_t *msg, int link_id, int cost,
                                         uint64_t in_conn_id, const qd_policy_spec_t *policy_spec, qdr_error_t **error);
void  qdr_route_table_setup_CT(qdr_core_t *core);
qdr_agent_t *qdr_agent(qdr_core_t *core);
void qdr_agent_setup_subscriptions(qdr_agent_t *agent, qdr_core_t *core);
void qdr_agent_free(qdr_agent_t *agent);
void  qdr_forwarder_setup_CT(qdr_core_t *core);
qdr_action_t *qdr_action(qdr_action_handler_t action_handler, const char *label);
void qdr_action_enqueue(qdr_core_t *core, qdr_action_t *action);
void qdr_action_background_enqueue(qdr_core_t *core, qdr_action_t *action);
void qdr_link_issue_credit_CT(qdr_core_t *core, qdr_link_t *link, int credit, bool drain);
void qdr_drain_inbound_undelivered_CT(qdr_core_t *core, qdr_link_t *link, qdr_address_t *addr);
void qdr_addr_start_inlinks_CT(qdr_core_t *core, qdr_address_t *addr);

/**
 * Returns true if the passed in address is a mobile address, false otherwise
 * If the first character of the address_key (obtained using its hash_handle) is M, the address is mobile.
 */
bool qdr_address_is_mobile_CT(qdr_address_t *addr);

void qdr_forward_on_message_CT(qdr_core_t *core, qdr_subscription_t *sub, qdr_link_t *link, qd_message_t *msg, qdr_delivery_t *in_dlv);
void qdr_in_process_send_to_CT(qdr_core_t *core, qd_iterator_t *address, qd_message_t *msg, bool exclude_inprocess, bool control);
void qdr_agent_enqueue_response_CT(qdr_core_t *core, qdr_query_t *query);

void qdr_post_set_mobile_seq_CT(qdr_core_t *core, int router_maskbit, uint64_t mobile_seq);
void qdr_post_set_my_mobile_seq_CT(qdr_core_t *core, uint64_t mobile_seq);
void qdr_post_link_lost_CT(qdr_core_t *core, int link_maskbit);

void qdr_post_general_work_CT(qdr_core_t *core, qdr_general_work_t *work);
void qdr_check_addr_CT(qdr_core_t *core, qdr_address_t *addr);
bool qdr_is_addr_treatment_multicast(qdr_address_t *addr);
qdr_delivery_t *qdr_forward_new_delivery_CT(qdr_core_t *core, qdr_delivery_t *peer, qdr_link_t *link, qd_message_t *msg);
void qdr_forward_deliver_CT(qdr_core_t *core, qdr_link_t *link, qdr_delivery_t *dlv);
void qdr_connection_free(qdr_connection_t *conn);
void qdr_connection_activate_CT(qdr_core_t *core, qdr_connection_t *conn);
void qdr_close_connection_CT(qdr_core_t *core, qdr_connection_t *conn);
qdr_link_t *qdr_connection_new_streaming_link_CT(qdr_core_t *core, qdr_connection_t *conn);
qdr_address_config_t *qdr_config_for_address_CT(qdr_core_t *core, qdr_connection_t *conn, qd_iterator_t *iter);
qd_address_treatment_t qdr_treatment_for_address_hash_CT(qdr_core_t *core, qd_iterator_t *iter, qdr_address_config_t **addr_config);
qd_address_treatment_t qdr_treatment_for_address_hash_with_default_CT(qdr_core_t *core, qd_iterator_t *iter, qd_address_treatment_t default_treatment, qdr_address_config_t **addr_config);
qdr_edge_t *qdr_edge(qdr_core_t *);
void qdr_edge_free(qdr_edge_t *);
void qdr_edge_connection_opened(qdr_edge_t *edge, qdr_connection_t *conn);
void qdr_edge_connection_closed(qdr_edge_t *edge);
void qdr_link_cleanup_deliveries_CT(qdr_core_t *core, qdr_connection_t *conn, qdr_link_t *link, bool on_shutdown);

void qdr_connection_enqueue_work_CT(qdr_core_t            *core,
                                    qdr_connection_t      *conn,
                                    qdr_connection_work_t *work);
void qdr_link_enqueue_work_CT(qdr_core_t      *core,
                              qdr_link_t      *conn,
                              qdr_link_work_t *work);

qdr_link_t *qdr_create_link_CT(qdr_core_t        *core,
                               qdr_connection_t  *conn,
                               qd_link_type_t     link_type,
                               qd_direction_t     dir,
                               qdr_terminus_t    *source,
                               qdr_terminus_t    *target,
                               qd_session_class_t ssn_class,
                               uint8_t priority);

void qdr_link_outbound_detach_CT(qdr_core_t *core, qdr_link_t *link, qdr_error_t *error, qdr_condition_t condition, bool close);
void qdr_link_outbound_second_attach_CT(qdr_core_t *core, qdr_link_t *link, qdr_terminus_t *source, qdr_terminus_t *target);
bool qdr_link_is_idle_CT(const qdr_link_t *link);
qdr_terminus_t *qdr_terminus_router_control(void);  ///< new terminus for router control links
qdr_terminus_t *qdr_terminus_router_data(void);  ///< new terminus for router links

qdr_query_t *qdr_query(qdr_core_t              *core,
                       void                    *context,
                       qd_router_entity_type_t  type,
                       qd_composed_field_t     *body,
                       uint64_t                 conn_id);

void qdr_modules_init(qdr_core_t *core);
void qdr_adaptors_init(qdr_core_t *core);
void qdr_modules_finalize(qdr_core_t *core);
void qdr_adaptors_finalize(qdr_core_t *core);

/**
 * Create a new timer which will only be used inside the core thread.
 *
 * @param core Pointer to the core object returned by qd_core()
 * @callback Callback function to be invoked when timer fires.
 * @timer_context Context to be used when firing callback
 */
qdr_core_timer_t *qdr_core_timer_CT(qdr_core_t *core, qdr_timer_cb_t callback, void *timer_context);


/**
 * Schedules a core timer with a delay. The timer will fire after "delay" seconds
 * @param core Pointer to the core object returned by qd_core()
 * @param timer Timer object that needs to be scheduled.
 * @param delay The number of seconds to wait before firing the timer
 */
void qdr_core_timer_schedule_CT(qdr_core_t *core, qdr_core_timer_t *timer, uint32_t delay);

/**
 * Cancels an already scheduled timeer. This does not free the timer. It is the responsibility of the person who
 * created the timer to free it.
 * @param core Pointer to the core object returned by qd_core()
 * @param timer Timer object that needs to be scheduled.
 *
 */
void qdr_core_timer_cancel_CT(qdr_core_t *core, qdr_core_timer_t *timer);

/**
 * Cancels the timer if it is scheduled and and free it.
 * @param core Pointer to the core object returned by qd_core()
 * @param timer Timer object that needs to be scheduled.
 */
void qdr_core_timer_free_CT(qdr_core_t *core, qdr_core_timer_t *timer);

/**
 * Clears the sheaf of priority links in a connection.
 * Call this when a connection is being closed, when the mask-bit
 * for that sheaf is being returned to the core for re-use.
 * @param core Pointer to the core object returned by qd_core()
 * @param n uint8_t index for the sheaf to be reset prior to re-use.
 */
void qdr_reset_sheaf(qdr_core_t *core, uint8_t n);

/**
 * Run in an IO thread.
 *
 * Records Proton's view of the link's available credit and tracks it for management and
 * logging.
 */
void qdr_record_link_credit(qdr_core_t *core, qdr_link_t *link);

/**
 * Access core uptime
 */
static inline uint32_t qdr_core_uptime_ticks(qdr_core_t *core)
{
    return sys_atomic_get(&core->uptime_ticks);
}

#endif

#ifndef __agent_private_h__
#define __agent_private_h__

#include "schema_enum.h"
#include "ctools.h"

typedef struct qd_management_work_item_t {
    DEQ_LINKS(struct qd_management_work_item_t);
    int                  operation;      //Is this a CREATE, READ, UPDATE, DELETE or QUERY
    int                  entity_type;    // Is this a listener or connector or address.... etc.
    int                  count;
    int                  offset;
    void                *ctx;
    qd_field_iterator_t *reply_to;
    qd_field_iterator_t *correlation_id;
    qd_field_iterator_t *identity_iter;
    qd_field_iterator_t *name_iter;
    qd_parsed_field_t   *in_body;
} qd_management_work_item_t;

DEQ_DECLARE(qd_management_work_item_t, qd_management_work_list_t);

typedef struct qd_entity_type_handler_t {
    qd_schema_entity_type_t  entity_type;
    void                    *ctx;
    qd_agent_handler_t       create_handler;
    qd_agent_handler_t       read_handler;
    qd_agent_handler_t       update_handler;
    qd_agent_handler_t       delete_handler;
    qd_agent_handler_t       query_handler;
} qd_entity_type_handler_t;

struct qd_agent_t {
    qd_management_work_list_t  work_queue;
    sys_mutex_t               *lock;
    qd_log_source_t           *log_source;
    qd_entity_type_handler_t  *handlers[QD_SCHEMA_ENTITY_TYPE_ENUM_COUNT];
};

struct qd_agent_request_t {
    qd_buffer_list_t        *list; // A buffer chain holding all the relevant information for the CRUDQ operations.
    void                    *ctx;
    int                      count;
    int                      offset;
    qd_field_iterator_t     *identity_iter;
    qd_field_iterator_t     *name_iter;
    qd_field_iterator_t     *reply_to;
    qd_field_iterator_t     *correlation_id;
    qd_router_entity_type_t  entity_type;
    qd_parsed_field_t       *in_body;
};

#endif

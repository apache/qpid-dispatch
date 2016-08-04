#ifndef __agent_private_h__
#define __agent_private_h__

#include <Python.h>
#include <qpid/dispatch/ctools.h>
#include <qpid/dispatch/timer.h>
#include <qpid/dispatch/threading.h>
#include <qpid/dispatch/log.h>
#include "agent.h"
#include "schema_enum.h"

typedef struct qd_management_work_item_t {
    DEQ_LINKS(struct qd_management_work_item_t);
    qd_agent_request_t *request;
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

typedef struct {
    PyObject_HEAD
    qd_agent_t *agent;
} AgentAdapter;

struct qd_agent_t {
    qd_dispatch_t             *qd;
    qd_timer_t                *timer;
    AgentAdapter              *adapter;
    char                      *address;
    const char                *config_file;
    qd_management_work_list_t  work_queue;
    sys_mutex_t               *lock;
    qd_log_source_t           *log_source;
    qd_entity_type_handler_t  *handlers[QD_SCHEMA_ENTITY_TYPE_ENUM_COUNT];
};

struct qd_agent_request_t {
    qd_buffer_list_t            *buffer_list; // A buffer chain holding all the relevant information for the CRUDQ operations.
    void                        *ctx;
    int                          count;
    int                          offset;
    qd_schema_entity_type_t      entity_type;
    qd_schema_entity_operation_t operation;
};



#endif

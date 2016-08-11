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

#include <Python.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <qpid/dispatch/python_embedded.h>
//#include <qpid/dispatch/compose.h>

#include "agent.h"
#include "agent_private.h"
#include "schema_enum.h"
#include "compose_private.h"

#define MANAGEMENT_INTERNAL_MODULE "qpid_dispatch_internal.management.agent"
#define MANAGEMENT_MODULE "qpid_dispatch.management"

static PyObject *qd_post_management_request(PyObject *self,
                                            PyObject *args,
                                            PyObject *keywds)

{
    int operation;    //Is this a CREATE, READ, UPDATE, DELETE or QUERY
    int entity_type;  // Is this a listener or connector or address.... etc.
    int count = 0;        // used for queries only
    int offset = 0;       //used for queries only
    PyObject *cid      = 0;
    PyObject *reply_to = 0;
    PyObject *name     = 0;
    PyObject *identity = 0;
    PyObject *body     = 0;

    static char *kwlist[] = {"cid", "reply_to",  "name", "identity", "body", "operation", "entity_type", "count", "offset", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, keywds, "OOOOOiiii", kwlist, &cid, &reply_to, &name, &identity, &body, &operation, &entity_type, &count, &offset))
        return 0;

    qd_composed_field_t *field = qd_compose_subfield(0);

    qd_py_to_composed(cid, field);
    qd_py_to_composed(reply_to, field);
    qd_py_to_composed(name, field);
    qd_py_to_composed(identity, field);
    qd_py_to_composed(body, field);

    qd_buffer_list_t *buffers = qd_compose_buffers(field);

    //
    // Create a request and add it to the work_queue
    //
    qd_agent_request_t *request = NEW(qd_agent_request_t);
    request->buffer_list = buffers;
    request->count = count;
    request->entity_type = entity_type;
    request->operation = operation;

    AgentAdapter *adapter = ((AgentAdapter*) self);
    //request->ctx = adapter->agent->handlers[entity_type]->ctx;
    qd_management_work_item_t *work_item = NEW(qd_management_work_item_t);
    work_item->request = request;
    //
    // Add work item to the work item list after locking the work item list
    //
    sys_mutex_lock(adapter->agent->lock);
    DEQ_INSERT_TAIL(adapter->agent->work_queue, work_item);
    sys_mutex_unlock(adapter->agent->lock);

    //create_handler(request);

    //
    // TODO - Kick off processing of the work queue
    //
    //qd_timer_schedule(adapter->agent->timer, 0);

    return Py_None;
}

/**
 * Declare all the methods in the AgentAdapter.
 * post_management_request is the name of the method that the python side would call and qd_post_management_request is the C implementation
 * of the function.
 */
static PyMethodDef AgentAdapter_functions[] = {
    //{"post_management_request", (PyCFunction)qd_post_management_request, METH_VARARGS|METH_KEYWORDS, "Posts a management request to a work queue"},
    {"post_management_request", (PyCFunction)qd_post_management_request, METH_VARARGS|METH_KEYWORDS, "Posts a management request to a work queue"},
    {0, 0, 0, 0} // <-- Not sure why we need this
};

static PyTypeObject AgentAdapterType = {
    PyObject_HEAD_INIT(0)
    0,                              /* ob_size*/
    MANAGEMENT_INTERNAL_MODULE ".AgentAdapter",  /* tp_name*/
    sizeof(AgentAdapter),    /* tp_basicsize*/
    0,                              /* tp_itemsize*/
    0,                              /* tp_dealloc*/
    0,                              /* tp_print*/
    0,                              /* tp_getattr*/
    0,                              /* tp_setattr*/
    0,                              /* tp_compare*/
    0,                              /* tp_repr*/
    0,                              /* tp_as_number*/
    0,                              /* tp_as_sequence*/
    0,                              /* tp_as_mapping*/
    0,                              /* tp_hash */
    0,                              /* tp_call*/
    0,                              /* tp_str*/
    0,                              /* tp_getattro*/
    0,                              /* tp_setattro*/
    0,                              /* tp_as_buffer*/
    Py_TPFLAGS_DEFAULT,             /* tp_flags*/
    "Agent request Adapter",        /* tp_doc */
    0,                              /* tp_traverse */
    0,                              /* tp_clear */
    0,                              /* tp_richcompare */
    0,                              /* tp_weaklistoffset */
    0,                              /* tp_iter */
    0,                              /* tp_iternext */
    AgentAdapter_functions,  /* tp_methods */
    0,                              /* tp_members */
    0,                              /* tp_getset */
    0,                              /* tp_base */
    0,                              /* tp_dict */
    0,                              /* tp_descr_get */
    0,                              /* tp_descr_set */
    0,                              /* tp_dictoffset */
    0,                              /* tp_init */
    0,                              /* tp_alloc */
    0,                              /* tp_new */
    0,                              /* tp_free */
    0,                              /* tp_is_gc */
    0,                              /* tp_bases */
    0,                              /* tp_mro */
    0,                              /* tp_cache */
    0,                              /* tp_subclasses */
    0,                              /* tp_weaklist */
    0,                              /* tp_del */
    0                               /* tp_version_tag */
};


/*static void process_work_queue(void *context)
{
    qd_agent_t *agent = (qd_agent_t *)context;
    qd_management_work_item_t *work_item = DEQ_HEAD(agent->work_queue);

    //TODO - The following works well with core but no corresponding functions for non-core
    while(work_item) {
            qd_agent_request_t *request = work_item->request;
            qd_entity_type_handler_t *handler = agent->handlers[request->entity_type];
            switch (request->operation) {
                case QD_SCHEMA_ENTITY_OPERATION_READ:
                    handler->read_handler(request->ctx,request);
                    break;
                case QD_SCHEMA_ENTITY_OPERATION_DELETE:
                    handler->delete_handler(request->ctx, request);
                    break;
                case QD_SCHEMA_ENTITY_OPERATION_CREATE:
                    handler->create_handler(request->ctx, request);
                    break;
                case QD_SCHEMA_ENTITY_OPERATION_UPDATE:
                    handler->update_handler(request->ctx, request);
                    break;
                case QD_SCHEMA_ENTITY_OPERATION_QUERY:
                    handler->query_handler(request->ctx, request);
                    break;
                case QD_SCHEMA_ENTITY_OPERATION_ENUM_COUNT:
                    break;
            }

            work_item = DEQ_NEXT(work_item);
    }
}*/

qd_agent_t* qd_agent(qd_dispatch_t *qd, char *address, const char *config_path)
{
    //
    // Create a new instance of AgentAdapterType
    //
    AgentAdapterType.tp_new = PyType_GenericNew;
    PyType_Ready(&AgentAdapterType);

    // Load the qpid_dispatch_internal.management Python module
    PyObject *module = PyImport_ImportModule(MANAGEMENT_INTERNAL_MODULE);

    if (!module) {
        qd_error_py();
        //qd_log(log_source, QD_LOG_CRITICAL, "Cannot load dispatch extension module '%s'", MANAGEMENT_INTERNAL_MODULE);
        abort();
    }


    PyTypeObject *agentAdapterType = &AgentAdapterType;
    Py_INCREF(agentAdapterType);

    //Use the "AgentAdapter" name to add the AgentAdapterType to the management
    PyModule_AddObject(module, "AgentAdapter", (PyObject*) &AgentAdapterType);
    PyObject *adapterType     = PyObject_GetAttrString(module, "AgentAdapter");
    PyObject *adapterInstance = PyObject_CallObject(adapterType, 0);

    //
    //Instantiate the new agent and return it
    //
    qd_agent_t *agent = NEW(qd_agent_t);
    agent->adapter = ((AgentAdapter*) adapterInstance);
    agent->qd = qd;
    agent->address = address;
    agent->config_file = config_path;
    agent->log_source = qd_log_source("AGENT");
    //agent->timer = qd_timer(qd, process_work_queue, agent);
    DEQ_INIT(agent->work_queue);
    agent->lock = sys_mutex();
    AgentAdapter *adapter = ((AgentAdapter*) adapterInstance);
    adapter->agent = agent;

    //
    // Initialize the handlers to zeros
    //
    for (int i=0; i < QD_SCHEMA_ENTITY_TYPE_ENUM_COUNT; i++)
        agent->handlers[i] = 0;

    Py_DECREF(agentAdapterType);
    Py_DECREF(module);

    //TODO - This is a test
    qd_agent_start(agent);

    return agent;
}


qd_error_t qd_agent_start(qd_agent_t *agent)
{
    // Load the qpid_dispatch_internal.management Python module
    PyObject *module = PyImport_ImportModule(MANAGEMENT_INTERNAL_MODULE);

    char *class = "ManagementAgent";

    //
    //Instantiate the ManagementAgent class found in qpid_dispatch_internal/management/agent.py
    //
    PyObject* pClass = PyObject_GetAttrString(module, class); QD_ERROR_PY_RET();

    //
    // Constructor Arguments for ManagementAgent
    //
    PyObject* pArgs = PyTuple_New(3);

   // arg 0: management address $management
   PyObject *address = PyString_FromString(agent->address);
   PyTuple_SetItem(pArgs, 0, address);

   // arg 1: adapter instance
   PyTuple_SetItem(pArgs, 1, (PyObject*)agent->adapter);

   // arg 2: config file location
   PyObject *config_file = PyString_FromString((char *)agent->config_file);
   PyTuple_SetItem(pArgs, 2, config_file);

   //
   // Instantiate the ManagementAgent class
   //
   PyObject* pyManagementInstance = PyInstance_New(pClass, pArgs, 0); QD_ERROR_PY_RET();
   if (!pyManagementInstance) {
       qd_log(agent->log_source, QD_LOG_CRITICAL, "Cannot create instance of Python class '%s.%s'", MANAGEMENT_INTERNAL_MODULE, class);
   }
   Py_DECREF(pArgs);
   Py_DECREF(pClass);
   return qd_error_code();
}


void qd_agent_register_handlers(qd_agent_t *agent,
                                void *ctx,
                                qd_schema_entity_type_t entity_type,
                                qd_agent_handler_t create_handler,
                                qd_agent_handler_t read_handler,
                                qd_agent_handler_t update_handler,
                                qd_agent_handler_t delete_handler,
                                qd_agent_handler_t query_handler)
{
    qd_entity_type_handler_t *entity_handler = NEW(qd_entity_type_handler_t);
    entity_handler->ctx            = ctx;
    entity_handler->entity_type    = entity_type;
    entity_handler->delete_handler = delete_handler;
    entity_handler->update_handler = update_handler;
    entity_handler->query_handler  = query_handler;
    entity_handler->create_handler = create_handler;
    entity_handler->read_handler   = read_handler;

    //Store the entity_handler in the appropriate cell of the handler array indexed by the enum qd_schema_entity_type_t
    agent->handlers[entity_type] = entity_handler;
}

qd_buffer_list_t *get_request_buffers(qd_agent_request_t *request)
{
    return request->buffer_list;
}

qd_schema_entity_type_t get_request_entity_type(qd_agent_request_t *request)
{
    return request->entity_type;
}

void  *get_request_context(qd_agent_request_t *request)
{
    return request->ctx;
}

int get_request_count(qd_agent_request_t *request)
{
    return request->count;
}

int get_request_offset(qd_agent_request_t *request)
{
    return request->offset;
}

void qd_agent_free(qd_agent_t *agent)
{

}

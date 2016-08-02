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

#include <qpid/dispatch/python_embedded.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

#include "agent.h"
#include "agent_private.h"
#include "schema_enum.h"

#define MANAGEMENT_MODULE "qpid_dispatch_internal.management"

typedef struct {
    PyObject_HEAD
    qd_agent_t *agent;

} AgentRequestAdapter;

/**
 * Declare all the methods in the AgentRequestAdapter.
 * post_management_request is the name of the method that the python side would call and qd_post_management_request is the C implementation
 * of the function.
 */
static PyMethodDef AgentRequestAdapter_functions[] = {
    {"post_management_request", qd_post_management_request, METH_VARARGS, "Posts a management request to a work queue"},
    {0, 0, 0, 0} // <-- Not sure why we need this
};

static PyTypeObject AgentRequestAdapterType = {
    PyObject_HEAD_INIT(0)
    0,                              /* ob_size*/
    MANAGEMENT_MODULE ".AgentRequestAdapter",  /* tp_name*/
    sizeof(AgentRequestAdapter),    /* tp_basicsize*/
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
    AgentRequestAdapter_functions,  /* tp_methods */
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

PyObject* qd_agent_init(char *agentClass, char *address, PyObject *pythonManagementModule, const char *config_path)
{
    // Create a new instance of AgentRequestAdapterType
    AgentRequestAdapterType.tp_new = PyType_GenericNew;
    PyType_Ready(&AgentRequestAdapterType);

    // Load the qpid_dispatch_internal.management Python module

    if (!pythonManagementModule) {
        qd_error_py();
        qd_log(log_source, QD_LOG_CRITICAL, "Cannot load dispatch extension module '%s'", MANAGEMENT_MODULE);
        abort();
    }

    PyTypeObject *agentRequestAdapterType = &AgentRequestAdapterType;
    Py_INCREF(agentRequestAdapterType);

    //Use the "AgentRequestAdapter" name to add the AgentRequestAdapterType to the management
    PyModule_AddObject(pythonManagementModule, "AgentRequestAdapter", (PyObject*) &AgentRequestAdapterType);
    // Now we have added AgentRequestAdapter to the qpid_dispatch_internal.management python module

    PyObject *adapterType     = PyObject_GetAttrString(pythonManagementModule, "AgentRequestAdapter");
    PyObject * adapterInstance = PyObject_CallObject(adapterType, 0);
    adapter = ((AgentRequestAdapter*) adapterInstance);
    ((AgentRequestAdapter*) adapterInstance)->log_source = qd_log_source("AGENT");
    qd_management_work_list_t  work_queue = 0;
    DEQ_INIT(work_queue);
    ((AgentRequestAdapter*) adapterInstance)->work_queue = work_queue;
    ((AgentRequestAdapter*) adapterInstance)->lock = sys_mutex();
    initialize_handlers(((AgentRequestAdapter*) adapterInstance)->handlers);

    //Instantiate the ManagementAgent class found in qpid_dispatch_internal/management/agent.py
    PyObject* pClass = PyObject_GetAttrString(pythonManagementModule, agentClass);

    //
    // Constructor Arguments for ManagementAgent
    //
    PyObject* pArgs = PyTuple_New(3);

   // arg 0: management address $management
   PyObject *address = PyString_FromString(address);
   PyTuple_SetItem(pArgs, 0, address);

   // arg 1: adapter instance
   PyTuple_SetItem(pArgs, 1, adapterInstance);

   // arg 2: config file location
   PyObject *config_file = PyString_FromString(config_path);
   PyTuple_SetItem(pArgs, 2, config_file);

   //
   // Instantiate the ManagementAgent class
   //
   PyObject* pyManagementInstance = PyInstance_New(pClass, pArgs, 0);
   if (pyManagementInstance) {}
   Py_DECREF(pArgs);
   Py_DECREF(adapterType);
   Py_DECREF(pythonManagementModule);

   //TODO - should I return an adapter or an instance of the entire management agent object?
   return adapterInstance;
}

/***
 * Adds a management
 */
static PyObject *qd_post_management_request(PyObject *self, //TODO - Do we need so many arguments or can I just pass a list with everything in it?
                                            PyObject *arg1, // Operation(CRUDQ) to be performed.
                                            PyObject *arg2, // Entity type
                                            PyObject *arg3, // count
                                            PyObject *arg4, // offset
                                            PyObject *arg5, // Correlation-id
                                            PyObject *arg6, // Reply to
                                            PyObject *arg7, // Name
                                            PyObject *arg8, // identity
                                            PyObject *arg9) // Request body
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

    if (!PyArg_ParseTuple(arg1, "i", &operation))
        return 0;
    if (!PyArg_ParseTuple(arg2, "i", &entity_type))
        return 0;
    if (!PyArg_ParseTuple(arg3, "i", &count))
        return 0;
    if (!PyArg_ParseTuple(arg4, "i", &offset))
        return 0;
    if (!PyArg_ParseTuple(arg3, "o", &cid))
        return 0;
    if (!PyArg_ParseTuple(arg4, "o", &reply_to))
        return 0;
    if (!PyArg_ParseTuple(arg5, "o", &name))
        return 0;
    if (!PyArg_ParseTuple(arg6, "o", &identity))
        return 0;
    if (!PyArg_ParseTuple(arg7, "o", &body))
        return 0;

    //
    // correlation id
    //
    qd_composed_field_t *cid_field = qd_compose_subfield(0);
    qd_py_to_composed(body, cid_field);
    qd_buffer_list_t cid_buffers = qd_compose_buffers(cid_field);
    // TODO - this is not correct. what if the buffer length is more than 512?
    qd_buffer_t buffer = DEQ_HEAD(cid_buffers);
    qd_field_iterator_t cid_iter = qd_address_iterator_buffer(buffer, 0, qd_buffer_list_length(cid_buffers), ITER_VIEW_ALL);


    qd_composed_field_t *reply_to_field = qd_compose_subfield(0);
    qd_py_to_composed(body, reply_to_field);
    qd_buffer_list_t reply_to_buffers = qd_compose_buffers(reply_to_field);
    // TODO - this is not correct. what if the buffer length is more than 512?
    qd_field_iterator_t reply_to_iter = qd_address_iterator_buffer(DEQ_HEAD(reply_to_buffers), 0, qd_buffer_list_length(reply_to_buffers), ITER_VIEW_ALL);

    qd_composed_field_t *identity_field = qd_compose_subfield(0);
    qd_py_to_composed(body, identity_field);
    qd_buffer_list_t identity_buffers = qd_compose_buffers(identity_field);
    // TODO - this is not correct. what if the buffer length is more than 512?
    qd_field_iterator_t identity_iter = qd_address_iterator_buffer(DEQ_HEAD(identity_buffers), 0, qd_buffer_list_length(identity_buffers), ITER_VIEW_ALL);

    qd_composed_field_t *name_field = qd_compose_subfield(0);
    qd_py_to_composed(body, name_field);
    qd_buffer_list_t name_buffers = qd_compose_buffers(name_field);
    // TODO - this is not correct. what if the buffer length is more than 512?
    qd_field_iterator_t name_iter = qd_address_iterator_buffer(DEQ_HEAD(name_buffers), 0, qd_buffer_list_length(name_buffers), ITER_VIEW_ALL);


    qd_composed_field_t *body_field = qd_compose_subfield(0);
    qd_py_to_composed(body, body_field);
    qd_buffer_list_t body_buffers = qd_compose_buffers(body_field);
    // TODO - this is not correct. what if the buffer length is more than 512?
    qd_field_iterator_t body_iter = qd_address_iterator_buffer(DEQ_HEAD(body_buffers), 0, qd_buffer_list_length(body_buffers), ITER_VIEW_ALL);


    qd_entity_type_handler_t handler = qd_agent_handler_for_type(entity_type);

    //
    // Create a work item (qd_management_work_item_t)
    //
    qd_management_work_item_t work_item = NEW(qd_management_work_item_t);
    work_item->count          = count;
    work_item->offset         = offset;
    work_item->operation      = operation;
    work_item->entity_type    = entity_type;
    work_item->ctx            = handler->ctx;
    work_item->reply_to       = reply_to_iter;
    work_item->correlation_id = cid_iter;
    work_item->identity_iter  = identity_iter;
    work_item->name_iter      = name_iter;
    work_item->in_body        = body_iter;

    //
    // Add work item to the work item list after locking the work item list
    //
    sys_mutex_lock(adapter->lock);
    DEQ_INSERT_TAIL(adapter->work_queue, work_item);
    sys_mutex_unlock(adapter->lock);

    //
    // TODO - Kick off processing of the work queue
    //
    return Py_None;
}


void qd_register_handlers(void *ctx,
                          PyObject *pyAdapter,
                          qd_schema_entity_type_t entity_type,
                          qd_agent_handler_t create_handler,
                          qd_agent_handler_t read_handler,
                          qd_agent_handler_t update_handler,
                          qd_agent_handler_t delete_handler,
                          qd_agent_handler_t query_handler)
{
    AgentRequestAdapter* adapter = ((AgentRequestAdapter*) pyAdapter);
    qd_entity_type_handler_t entity_handler = NEW(qd_entity_type_handler_t);
    entity_handler->delete_handler = delete_handler;
    entity_handler->update_handler = update_handler;
    entity_handler->query_handler  = query_handler;
    entity_handler->create_handler = create_handler;
    entity_handler->read_handler   = read_handler;

    //Store the entity_handler in the appropriate cell of the handler array index by the enum qd_schema_entity_type_t
    adapter->handlers[entity_type] = entity_handler;

}

static qd_entity_type_handler_t *qd_agent_handler_for_type(qd_schema_entity_type_t entity_type, AgentRequestAdapter* adapter)
{
    return adapter->handlers[entity_type];
}

static void initialize_handlers(AgentRequestAdapter* adapter)
{
    for (int i=0; i < QD_SCHEMA_ENTITY_TYPE_ENUM_COUNT; i++)
    {
            adapter->handlers[i] = 0;
    }
}


static process_work_queue(qd_management_work_list_t  work_queue, AgentRequestAdapter* adapter)
{
    qd_management_work_item_t work_item = DEQ_HEAD(work_queue);

    qd_entity_type_handler_t handler = qd_agent_handler_for_type(work_item->entity_type, adapter);

    //TODO - The following works well with core but no corresponding functions for non-core
    while(work_item) {
            switch (work_item->operation) {
                case QD_SCHEMA_ENTITY_OPERATION_READ:
                    handler->read_handler(work_item->ctx,
                                          work_item->reply_to,
                                          work_item->correlation_id,
                                          work_item->entity_type,
                                          work_item->operation,
                                          work_item->identity_iter,
                                          work_item->name_iter);
                    break;
                case QD_SCHEMA_ENTITY_OPERATION_DELETE:
                    handler->delete_handler(work_item->ctx,
                                            work_item->reply_to,
                                            work_item->correlation_id,
                                            work_item->entity_type,
                                            work_item->operation,
                                            work_item->identity_iter,
                                            work_item->name_iter);
                    break;
                case QD_SCHEMA_ENTITY_OPERATION_CREATE:
                    handler->create_handler(work_item->ctx,
                                            work_item->reply_to,
                                            work_item->correlation_id,
                                            work_item->entity_type,
                                            work_item->operation,
                                            work_item->name_iter,
                                            work_item->in_body);
                    break;
                case QD_SCHEMA_ENTITY_OPERATION_UPDATE:
                    handler->update_handler(work_item->ctx,
                                            work_item->reply_to,
                                            work_item->correlation_id,
                                            work_item->entity_type,
                                            work_item->operation,
                                            work_item->identity_iter,
                                            work_item->name_iter,
                                            work_item->in_body);
                    break;
                case QD_SCHEMA_ENTITY_OPERATION_QUERY:
                    handler->query_handler(work_item->ctx,
                                            work_item->reply_to,
                                            work_item->correlation_id,
                                            work_item->entity_type,
                                            work_item->operation,
                                            work_item->count,
                                            work_item->offset,
                                            work_item->in_body);
                    break;
            }

            work_item = DEQ_NEXT(work_item);
    }
}


PyObject* qd_agent_adapter_finalize(PyObject *adapter)
{

}

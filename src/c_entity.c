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
#include <qpid/dispatch/threading.h>
#include <qpid/dispatch/ctools.h>
#include <structmember.h>
#include "c_entity.h"
#include "entity_private.h"
#include "dispatch_private.h"
#include "router_private.h"

#include <stdbool.h>
#include <pthread.h>


typedef enum { REMOVE=0, ADD=1 }  action_t;

typedef struct entity_event_t {
    DEQ_LINKS(struct entity_event_t);
    action_t action;
    const char *type;
    void *object;
} entity_event_t;

DEQ_DECLARE(entity_event_t, entity_event_list_t);

static entity_event_t *entity_event(action_t action, const char *type, void *object) {
    entity_event_t *event = NEW(entity_event_t);
    DEQ_ITEM_INIT(event);
    event->action = action;
    event->type = type;
    event->object = object;
    return event;
}

static sys_mutex_t *event_lock = 0;
static entity_event_list_t  event_list;

void qd_c_entity_initialize(void) {
    event_lock = sys_mutex();
    DEQ_INIT(event_list);
}

static void push_event(action_t action, const char *type, void *object) {
    if (!event_lock) return;    /* Unit tests don't call qd_c_entity_initialize */
    sys_mutex_lock(event_lock);
    entity_event_t *event = entity_event(action, type, object);
    DEQ_INSERT_TAIL(event_list, event);
    sys_mutex_unlock(event_lock);
}

void qd_c_entity_add(const char *type, void *object) { push_event(ADD, type, object); }

void qd_c_entity_remove(const char *type, void *object) { push_event(REMOVE, type, object); }

// Get events in the add/remove cache into a python list of (action, type, pointer)
// Locks the entity cache so entities can be updated safely (prevent entities from being deleted.)
// Do not processs any entities if return error code != 0
// Must call qd_c_entity_update_end when done, regardless of error code.
qd_error_t qd_c_entity_update_begin(PyObject *list) {
    if (!event_lock) return QD_ERROR_NONE;    /* Unit tests don't call qd_c_entity_initialize */
    qd_error_clear();
    sys_mutex_lock(event_lock);
    entity_event_t *event = DEQ_HEAD(event_list);
    while (event) {
        PyObject *tuple = Py_BuildValue("(isl)", (int)event->action, event->type, (long)event->object);
        if (!tuple) { qd_error_py(); break; }
        int err = PyList_Append(list, tuple);
        Py_DECREF(tuple);
        if (err) { qd_error_py(); break; }
        DEQ_REMOVE_HEAD(event_list);
        free(event);
        event = DEQ_HEAD(event_list);
    }
    return qd_error_code();
}

void qd_c_entity_update_end() {
    sys_mutex_unlock(event_lock);
}

const char *QD_ALLOCATOR_TYPE = "allocator";
const char *QD_CONNECTION_TYPE = "connection";
const char *QD_ROUTER_TYPE = "router";
const char *QD_ROUTER_NODE_TYPE = "router.node";
const char *QD_ROUTER_ADDRESS_TYPE = "router.address";
const char *QD_ROUTER_LINK_TYPE = "router.link";

const char *QD_ALLOCATOR_TYPE_LONG = "org.apache.qpid.dispatch.allocator";
const char *QD_CONNECTION_TYPE_LONG = "org.apache.qpid.dispatch.connection";
const char *QD_ROUTER_TYPE_LONG = "org.apache.qpid.dispatch.router";
const char *QD_ROUTER_NODE_TYPE_LONG = "org.apache.qpid.dispatch.router.node";
const char *QD_ROUTER_ADDRESS_TYPE_LONG = "org.apache.qpid.dispatch.router.address";
const char *QD_ROUTER_LINK_TYPE_LONG = "org.apache.qpid.dispatch.router.link";

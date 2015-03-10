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
#include <qpid/dispatch.h>
#include "dispatch_private.h"
#include "router_private.h"
#include "waypoint_private.h"
#include "entity_cache.h"

static qd_address_semantics_t router_addr_semantics = QD_FANOUT_SINGLE | QD_BIAS_CLOSEST | QD_CONGESTION_DROP | QD_DROP_FOR_SLOW_CONSUMERS | QD_BYPASS_VALID_ORIGINS;

static qd_log_source_t *log_source = 0;
static PyObject        *pyRouter   = 0;
static PyObject        *pyTick     = 0;
static PyObject        *pyAdded    = 0;
static PyObject        *pyRemoved  = 0;
static PyObject        *pyLinkLost = 0;

typedef struct {
    PyObject_HEAD
    qd_router_t *router;
} RouterAdapter;


static PyObject *qd_add_router(PyObject *self, PyObject *args)
{
    RouterAdapter *adapter = (RouterAdapter*) self;
    qd_router_t   *router  = adapter->router;
    const char    *address;
    int            router_maskbit;
    char          *error = 0;

    if (!PyArg_ParseTuple(args, "si", &address, &router_maskbit))
        return 0;

    do {
        if (router_maskbit >= qd_bitmask_width() || router_maskbit < 0) {
            error = "Router bit mask out of range";
            break;
        }

        sys_mutex_lock(router->lock);
        if (router->routers_by_mask_bit[router_maskbit] != 0) {
            sys_mutex_unlock(router->lock);
            error = "Adding router over already existing router";
            break;
        }

        //
        // Hash lookup the address to ensure there isn't an existing router address.
        //
        qd_field_iterator_t *iter = qd_address_iterator_string(address, ITER_VIEW_ADDRESS_HASH);
        qd_address_t        *addr;

        qd_hash_retrieve(router->addr_hash, iter, (void**) &addr);
        assert(addr == 0);

        //
        // Create an address record for this router and insert it in the hash table.
        // This record will be found whenever a "foreign" topological address to this
        // remote router is looked up.
        //
        addr = qd_address(router_addr_semantics);
        qd_hash_insert(router->addr_hash, iter, addr, &addr->hash_handle);
        DEQ_INSERT_TAIL(router->addrs, addr);
        qd_entity_cache_add(QD_ROUTER_ADDRESS_TYPE, addr);
        qd_field_iterator_free(iter);

        //
        // Create a router-node record to represent the remote router.
        //
        qd_router_node_t *rnode = new_qd_router_node_t();
        DEQ_ITEM_INIT(rnode);
        rnode->owning_addr   = addr;
        rnode->mask_bit      = router_maskbit;
        rnode->next_hop      = 0;
        rnode->peer_link     = 0;
        rnode->ref_count     = 0;
        rnode->valid_origins = qd_bitmask(0);

        DEQ_INSERT_TAIL(router->routers, rnode);

        //
        // Link the router record to the address record.
        //
        qd_router_add_node_ref_LH(&addr->rnodes, rnode);

        //
        // Link the router record to the router address records.
        //
        qd_router_add_node_ref_LH(&router->router_addr->rnodes, rnode);
        qd_router_add_node_ref_LH(&router->routerma_addr->rnodes, rnode);

        //
        // Add the router record to the mask-bit index.
        //
        router->routers_by_mask_bit[router_maskbit] = rnode;

        sys_mutex_unlock(router->lock);
    } while (0);

    if (error) {
        PyErr_SetString(PyExc_Exception, error);
        return 0;
    }

    Py_INCREF(Py_None);
    return Py_None;
}


static PyObject* qd_del_router(PyObject *self, PyObject *args)
{
    RouterAdapter *adapter = (RouterAdapter*) self;
    qd_router_t   *router  = adapter->router;
    int router_maskbit;
    char *error = 0;

    if (!PyArg_ParseTuple(args, "i", &router_maskbit))
        return 0;

    do {
        if (router_maskbit >= qd_bitmask_width() || router_maskbit < 0) {
            error = "Router bit mask out of range";
            break;
        }

        sys_mutex_lock(router->lock);
        if (router->routers_by_mask_bit[router_maskbit] == 0) {
            sys_mutex_unlock(router->lock);
            error = "Deleting nonexistent router";
            break;
        }

        qd_router_node_t *rnode = router->routers_by_mask_bit[router_maskbit];
        qd_address_t     *oaddr = rnode->owning_addr;
        assert(oaddr);

        qd_entity_cache_remove(QD_ROUTER_ADDRESS_TYPE, oaddr);

        //
        // Unlink the router node from the address record
        //
        qd_router_del_node_ref_LH(&oaddr->rnodes, rnode);

        //
        // While the router node has a non-zero reference count, look for addresses
        // to unlink the node from.
        //
        qd_address_t *addr = DEQ_HEAD(router->addrs);
        while (addr && rnode->ref_count > 0) {
            qd_router_del_node_ref_LH(&addr->rnodes, rnode);
            addr = DEQ_NEXT(addr);
        }
        assert(rnode->ref_count == 0);

        //
        // Free the router node and the owning address records.
        //
        qd_bitmask_free(rnode->valid_origins);
        DEQ_REMOVE(router->routers, rnode);
        free_qd_router_node_t(rnode);

        qd_hash_remove_by_handle(router->addr_hash, oaddr->hash_handle);
        DEQ_REMOVE(router->addrs, oaddr);
        qd_hash_handle_free(oaddr->hash_handle);
        router->routers_by_mask_bit[router_maskbit] = 0;
        free_qd_address_t(oaddr);

        sys_mutex_unlock(router->lock);
    } while(0);

    if (error) {
        PyErr_SetString(PyExc_Exception, error);
        return 0;
    }

    Py_INCREF(Py_None);
    return Py_None;
}


static PyObject* qd_set_link(PyObject *self, PyObject *args)
{
    RouterAdapter *adapter = (RouterAdapter*) self;
    qd_router_t   *router  = adapter->router;
    int            router_maskbit;
    int            link_maskbit;
    char          *error = 0;

    if (!PyArg_ParseTuple(args, "ii", &router_maskbit, &link_maskbit))
        return 0;

    do {
        if (link_maskbit >= qd_bitmask_width() || link_maskbit < 0) {
            error = "Link bit mask out of range";
            break;
        }

        sys_mutex_lock(router->lock);
        if (router->out_links_by_mask_bit[link_maskbit] == 0) {
            sys_mutex_unlock(router->lock);
            error = "Adding neighbor router with invalid link reference";
            break;
        }

        //
        // Add the peer_link reference to the router record.
        //
        qd_router_node_t *rnode = router->routers_by_mask_bit[router_maskbit];
        rnode->peer_link = router->out_links_by_mask_bit[link_maskbit];

        sys_mutex_unlock(router->lock);
    } while (0);

    if (error) {
        PyErr_SetString(PyExc_Exception, error);
        return 0;
    }

    Py_INCREF(Py_None);
    return Py_None;
}


static PyObject* qd_remove_link(PyObject *self, PyObject *args)
{
    RouterAdapter *adapter = (RouterAdapter*) self;
    qd_router_t   *router  = adapter->router;
    int            router_maskbit;
    char          *error = 0;

    if (!PyArg_ParseTuple(args, "i", &router_maskbit))
        return 0;

    do {
        sys_mutex_lock(router->lock);
        qd_router_node_t *rnode = router->routers_by_mask_bit[router_maskbit];
        rnode->peer_link = 0;
        sys_mutex_unlock(router->lock);
    } while (0);

    if (error) {
        PyErr_SetString(PyExc_Exception, error);
        return 0;
    }

    Py_INCREF(Py_None);
    return Py_None;
}


static PyObject* qd_set_next_hop(PyObject *self, PyObject *args)
{
    RouterAdapter *adapter = (RouterAdapter*) self;
    qd_router_t   *router  = adapter->router;
    int            router_maskbit;
    int            next_hop_maskbit;
    char          *error = 0;

    if (!PyArg_ParseTuple(args, "ii", &router_maskbit, &next_hop_maskbit))
        return 0;

    do {
        if (router_maskbit >= qd_bitmask_width() || router_maskbit < 0) {
            error = "Router bit mask out of range";
            break;
        }

        if (next_hop_maskbit >= qd_bitmask_width() || next_hop_maskbit < 0) {
            error = "Next Hop bit mask out of range";
            break;
        }

        sys_mutex_lock(router->lock);
        if (router->routers_by_mask_bit[router_maskbit] == 0) {
            sys_mutex_unlock(router->lock);
            error = "Router Not Found";
            break;
        }

        if (router->routers_by_mask_bit[next_hop_maskbit] == 0) {
            sys_mutex_unlock(router->lock);
            error = "Next Hop Not Found";
            break;
        }

        if (router_maskbit != next_hop_maskbit) {
            qd_router_node_t *rnode = router->routers_by_mask_bit[router_maskbit];
            rnode->next_hop = router->routers_by_mask_bit[next_hop_maskbit];
        }
        sys_mutex_unlock(router->lock);
    } while (0);

    if (error) {
        PyErr_SetString(PyExc_Exception, error);
        return 0;
    }

    Py_INCREF(Py_None);
    return Py_None;
}


static PyObject* qd_remove_next_hop(PyObject *self, PyObject *args)
{
    RouterAdapter *adapter = (RouterAdapter*) self;
    qd_router_t   *router  = adapter->router;
    int            router_maskbit;
    char          *error = 0;

    if (!PyArg_ParseTuple(args, "i", &router_maskbit))
        return 0;

    do {
        if (router_maskbit >= qd_bitmask_width() || router_maskbit < 0) {
            error = "Router bit mask out of range";
            break;
        }

        sys_mutex_lock(router->lock);
        if (router->routers_by_mask_bit[router_maskbit] == 0) {
            sys_mutex_unlock(router->lock);
            error = "Router Not Found";
            break;
        }

        qd_router_node_t *rnode = router->routers_by_mask_bit[router_maskbit];
        rnode->next_hop = 0;

        sys_mutex_unlock(router->lock);
    } while (0);

    if (error) {
        PyErr_SetString(PyExc_Exception, error);
        return 0;
    }

    Py_INCREF(Py_None);
    return Py_None;
}


static PyObject* qd_set_valid_origins(PyObject *self, PyObject *args)
{
    RouterAdapter *adapter = (RouterAdapter*) self;
    qd_router_t   *router  = adapter->router;
    int            router_maskbit;
    PyObject      *origin_list;
    Py_ssize_t     idx;
    char          *error = 0;

    if (!PyArg_ParseTuple(args, "iO", &router_maskbit, &origin_list))
        return 0;

    do {
        if (router_maskbit >= qd_bitmask_width() || router_maskbit < 0) {
            error = "Router bit mask out of range";
            break;
        }

        if (!PyList_Check(origin_list)) {
            error = "Expected List as argument 2";
            break;
        }

        sys_mutex_lock(router->lock);
        if (router->routers_by_mask_bit[router_maskbit] == 0) {
            sys_mutex_unlock(router->lock);
            error = "Router Not Found";
            break;
        }

        Py_ssize_t        origin_count = PyList_Size(origin_list);
        qd_router_node_t *rnode        = router->routers_by_mask_bit[router_maskbit];
        int               maskbit;

        for (idx = 0; idx < origin_count; idx++) {
            maskbit = PyInt_AS_LONG(PyList_GetItem(origin_list, idx));

            if (maskbit >= qd_bitmask_width() || maskbit < 0) {
                error = "Origin bit mask out of range";
                break;
            }

            if (router->routers_by_mask_bit[maskbit] == 0) {
                error = "Origin router Not Found";
                break;
            }
        }

        if (error == 0) {
            qd_bitmask_clear_all(rnode->valid_origins);
            qd_bitmask_set_bit(rnode->valid_origins, 0);  // This router is a valid origin for all destinations
            for (idx = 0; idx < origin_count; idx++) {
                maskbit = PyInt_AS_LONG(PyList_GetItem(origin_list, idx));
                qd_bitmask_set_bit(rnode->valid_origins, maskbit);
            }
        }

        sys_mutex_unlock(router->lock);
    } while (0);

    if (error) {
        PyErr_SetString(PyExc_Exception, error);
        return 0;
    }

    Py_INCREF(Py_None);
    return Py_None;
}


static PyObject* qd_map_destination(PyObject *self, PyObject *args)
{
    RouterAdapter       *adapter = (RouterAdapter*) self;
    qd_router_t         *router  = adapter->router;
    char                 phase;
    char                 unused;
    const char          *addr_string;
    int                  maskbit;
    qd_address_t        *addr;
    qd_field_iterator_t *iter;

    if (!PyArg_ParseTuple(args, "csi", &phase, &addr_string, &maskbit))
        return 0;

    if (maskbit >= qd_bitmask_width() || maskbit < 0) {
        PyErr_SetString(PyExc_Exception, "Router bit mask out of range");
        return 0;
    }

    if (router->routers_by_mask_bit[maskbit] == 0) {
        PyErr_SetString(PyExc_Exception, "Router Not Found");
        return 0;
    }

    iter = qd_address_iterator_string(addr_string, ITER_VIEW_ALL);

    sys_mutex_lock(router->lock);
    qd_hash_retrieve(router->addr_hash, iter, (void**) &addr);
    if (!addr) {
        addr = qd_address(router_semantics_for_addr(router, iter, phase, &unused));
        qd_hash_insert(router->addr_hash, iter, addr, &addr->hash_handle);
        DEQ_ITEM_INIT(addr);
        DEQ_INSERT_TAIL(router->addrs, addr);
        qd_entity_cache_add(QD_ROUTER_ADDRESS_TYPE, addr);
    }
    qd_field_iterator_free(iter);

    qd_router_node_t *rnode = router->routers_by_mask_bit[maskbit];
    qd_router_add_node_ref_LH(&addr->rnodes, rnode);

    //
    // If the address has an associated waypoint, notify the waypoint module of the changes.
    //
    if (addr->waypoint)
        qd_waypoint_address_updated_LH(router->qd, addr);

    sys_mutex_unlock(router->lock);
    Py_INCREF(Py_None);
    return Py_None;
}


static PyObject* qd_unmap_destination(PyObject *self, PyObject *args)
{
    RouterAdapter *adapter = (RouterAdapter*) self;
    qd_router_t   *router  = adapter->router;
    const char    *addr_string;
    int            maskbit;
    qd_address_t  *addr;

    if (!PyArg_ParseTuple(args, "si", &addr_string, &maskbit))
        return 0;

    if (maskbit >= qd_bitmask_width() || maskbit < 0) {
        PyErr_SetString(PyExc_Exception, "Router bit mask out of range");
        return 0;
    }

    if (router->routers_by_mask_bit[maskbit] == 0) {
        PyErr_SetString(PyExc_Exception, "Router Not Found");
        return 0;
    }

    qd_router_node_t    *rnode = router->routers_by_mask_bit[maskbit];
    qd_field_iterator_t *iter  = qd_address_iterator_string(addr_string, ITER_VIEW_ALL);

    sys_mutex_lock(router->lock);
    qd_hash_retrieve(router->addr_hash, iter, (void**) &addr);
    qd_field_iterator_free(iter);

    if (!addr) {
        PyErr_SetString(PyExc_Exception, "Address Not Found");
        sys_mutex_unlock(router->lock);
        return 0;
    }

    qd_router_del_node_ref_LH(&addr->rnodes, rnode);

    //
    // If the address has an associated waypoint, notify the waypoint module of the changes.
    //
    if (addr->waypoint)
        qd_waypoint_address_updated_LH(router->qd, addr);

    sys_mutex_unlock(router->lock);

    qd_router_check_addr(router, addr, 0);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* qd_get_agent(PyObject *self, PyObject *args) {
    RouterAdapter *adapter = (RouterAdapter*) self;
    PyObject *agent = adapter->router->qd->agent;
    if (agent) {
        Py_INCREF(agent);
        return agent;
    }
    Py_RETURN_NONE;
}

static PyMethodDef RouterAdapter_methods[] = {
    {"add_router",          qd_add_router,        METH_VARARGS, "A new remote/reachable router has been discovered"},
    {"del_router",          qd_del_router,        METH_VARARGS, "We've lost reachability to a remote router"},
    {"set_link",            qd_set_link,          METH_VARARGS, "Set the link for a neighbor router"},
    {"remove_link",         qd_remove_link,       METH_VARARGS, "Remove the link for a neighbor router"},
    {"set_next_hop",        qd_set_next_hop,      METH_VARARGS, "Set the next hop for a remote router"},
    {"remove_next_hop",     qd_remove_next_hop,   METH_VARARGS, "Remove the next hop for a remote router"},
    {"set_valid_origins",   qd_set_valid_origins, METH_VARARGS, "Set the valid origins for a remote router"},
    {"map_destination",     qd_map_destination,   METH_VARARGS, "Add a newly discovered destination mapping"},
    {"unmap_destination",   qd_unmap_destination, METH_VARARGS, "Delete a destination mapping"},
    {"get_agent",           qd_get_agent,         METH_VARARGS, "Get the management agent"},
    {0, 0, 0, 0}
};

static PyTypeObject RouterAdapterType = {
    PyObject_HEAD_INIT(0)
    0,                         /* ob_size*/
    "dispatch.RouterAdapter",  /* tp_name*/
    sizeof(RouterAdapter),     /* tp_basicsize*/
    0,                         /* tp_itemsize*/
    0,                         /* tp_dealloc*/
    0,                         /* tp_print*/
    0,                         /* tp_getattr*/
    0,                         /* tp_setattr*/
    0,                         /* tp_compare*/
    0,                         /* tp_repr*/
    0,                         /* tp_as_number*/
    0,                         /* tp_as_sequence*/
    0,                         /* tp_as_mapping*/
    0,                         /* tp_hash */
    0,                         /* tp_call*/
    0,                         /* tp_str*/
    0,                         /* tp_getattro*/
    0,                         /* tp_setattro*/
    0,                         /* tp_as_buffer*/
    Py_TPFLAGS_DEFAULT,        /* tp_flags*/
    "Dispatch Router Adapter", /* tp_doc */
    0,                         /* tp_traverse */
    0,                         /* tp_clear */
    0,                         /* tp_richcompare */
    0,                         /* tp_weaklistoffset */
    0,                         /* tp_iter */
    0,                         /* tp_iternext */
    RouterAdapter_methods,     /* tp_methods */
    0,                         /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    0,                         /* tp_init */
    0,                         /* tp_alloc */
    0,                         /* tp_new */
    0,                         /* tp_free */
    0,                         /* tp_is_gc */
    0,                         /* tp_bases */
    0,                         /* tp_mro */
    0,                         /* tp_cache */
    0,                         /* tp_subclasses */
    0,                         /* tp_weaklist */
    0,                         /* tp_del */
    0                          /* tp_version_tag */
};

qd_error_t qd_router_python_setup(qd_router_t *router)
{
    qd_error_clear();
    log_source = qd_log_source("ROUTER");

    //
    // If we are not operating as an interior router, don't start the
    // router module.
    //
    if (router->router_mode != QD_ROUTER_MODE_INTERIOR)
        return QD_ERROR_NONE;

    PyObject *pDispatchModule = qd_python_module();
    RouterAdapterType.tp_new = PyType_GenericNew;
    PyType_Ready(&RouterAdapterType);
    QD_ERROR_PY_RET();

    PyTypeObject *raType = &RouterAdapterType;
    Py_INCREF(raType);
    PyModule_AddObject(pDispatchModule, "RouterAdapter", (PyObject*) &RouterAdapterType);

    //
    // Attempt to import the Python Router module
    //
    PyObject* pId;
    PyObject* pArea;
    PyObject* pMaxRouters;
    PyObject* pModule;
    PyObject* pClass;
    PyObject* pArgs;

    pModule = PyImport_ImportModule("qpid_dispatch_internal.router"); QD_ERROR_PY_RET();
    pClass = PyObject_GetAttrString(pModule, "RouterEngine");
    Py_DECREF(pModule);
    QD_ERROR_PY_RET();

    PyObject *adapterType     = PyObject_GetAttrString(pDispatchModule, "RouterAdapter");  QD_ERROR_PY_RET();
    PyObject *adapterInstance = PyObject_CallObject(adapterType, 0); QD_ERROR_PY_RET();

    ((RouterAdapter*) adapterInstance)->router = router;

    //
    // Constructor Arguments for RouterEngine
    //
    pArgs = PyTuple_New(4);

    // arg 0: adapter instance
    PyTuple_SetItem(pArgs, 0, adapterInstance);

    // arg 1: router_id
    pId = PyString_FromString(router->router_id);
    PyTuple_SetItem(pArgs, 1, pId);

    // arg 2: area_id
    pArea = PyString_FromString(router->router_area);
    PyTuple_SetItem(pArgs, 2, pArea);

    // arg 3: max_routers
    pMaxRouters = PyInt_FromLong((long) qd_bitmask_width());
    PyTuple_SetItem(pArgs, 3, pMaxRouters);

    //
    // Instantiate the router
    //
    pyRouter = PyInstance_New(pClass, pArgs, 0);
    Py_DECREF(pArgs);
    Py_DECREF(adapterType);
    QD_ERROR_PY_RET();

    pyTick = PyObject_GetAttrString(pyRouter, "handleTimerTick"); QD_ERROR_PY_RET();
    pyAdded = PyObject_GetAttrString(pyRouter, "addressAdded"); QD_ERROR_PY_RET();
    pyRemoved = PyObject_GetAttrString(pyRouter, "addressRemoved"); QD_ERROR_PY_RET();
    pyLinkLost = PyObject_GetAttrString(pyRouter, "linkLost"); QD_ERROR_PY_RET();
    return qd_error_code();
}

void qd_router_python_free(qd_router_t *router) {
    // empty
}


qd_error_t qd_pyrouter_tick(qd_router_t *router)
{
    qd_error_clear();
    qd_error_t err = QD_ERROR_NONE;

    PyObject *pArgs;
    PyObject *pValue;

    if (pyTick && router->router_mode == QD_ROUTER_MODE_INTERIOR) {
        qd_python_lock_state_t lock_state = qd_python_lock();
        pArgs  = PyTuple_New(0);
        pValue = PyObject_CallObject(pyTick, pArgs);
        Py_DECREF(pArgs);
        Py_XDECREF(pValue);
        err = qd_error_py();
        qd_python_unlock(lock_state);
    }
    return err;
}


void qd_router_mobile_added(qd_router_t *router, qd_field_iterator_t *iter)
{
    PyObject *pArgs;
    PyObject *pValue;

    if (pyAdded && router->router_mode == QD_ROUTER_MODE_INTERIOR) {
        qd_address_iterator_reset_view(iter, ITER_VIEW_ADDRESS_HASH);
        char *address = (char*) qd_field_iterator_copy(iter);

        qd_python_lock_state_t lock_state = qd_python_lock();
        pArgs = PyTuple_New(1);
        PyTuple_SetItem(pArgs, 0, PyString_FromString(address));
        pValue = PyObject_CallObject(pyAdded, pArgs);
        qd_error_py();
        Py_DECREF(pArgs);
        Py_XDECREF(pValue);
        qd_python_unlock(lock_state);

        free(address);
    }
}


void qd_router_mobile_removed(qd_router_t *router, const char *address)
{
    PyObject *pArgs;
    PyObject *pValue;

    if (pyRemoved && router->router_mode == QD_ROUTER_MODE_INTERIOR) {
        qd_python_lock_state_t lock_state = qd_python_lock();
        pArgs = PyTuple_New(1);
        PyTuple_SetItem(pArgs, 0, PyString_FromString(address));
        pValue = PyObject_CallObject(pyRemoved, pArgs);
        qd_error_py();
        Py_DECREF(pArgs);
        Py_XDECREF(pValue);
        qd_python_unlock(lock_state);
    }
}


void qd_router_link_lost(qd_router_t *router, int link_mask_bit)
{
    PyObject *pArgs;
    PyObject *pValue;

    if (pyRemoved && router->router_mode == QD_ROUTER_MODE_INTERIOR) {
        qd_python_lock_state_t lock_state = qd_python_lock();
        pArgs = PyTuple_New(1);
        PyTuple_SetItem(pArgs, 0, PyInt_FromLong((long) link_mask_bit));
        pValue = PyObject_CallObject(pyLinkLost, pArgs);
        qd_error_py();
        Py_DECREF(pArgs);
        Py_XDECREF(pValue);
        qd_python_unlock(lock_state);
    }
}


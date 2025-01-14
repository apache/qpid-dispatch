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

#include "python_private.h"
#include "qpid/dispatch/python_embedded.h"

#include "dispatch_private.h"
#include "pythoncapi_compat.h"
#include "router_private.h"

#include "qpid/dispatch.h"

#include <stdlib.h>
#include <string.h>

static qd_log_source_t *log_source       = 0;
static PyObject        *pyRouter         = 0;
static PyObject        *pyTick           = 0;
static PyObject        *pySetMobileSeq   = 0;
static PyObject        *pySetMyMobileSeq = 0;
static PyObject        *pyLinkLost       = 0;

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

    if (!PyArg_ParseTuple(args, "si", &address, &router_maskbit))
        return 0;

    qdr_core_add_router(router->router_core, address, router_maskbit);
    qd_tracemask_add_router(router->tracemask, address, router_maskbit);

    Py_INCREF(Py_None);
    return Py_None;
}


static PyObject* qd_del_router(PyObject *self, PyObject *args)
{
    RouterAdapter *adapter = (RouterAdapter*) self;
    qd_router_t   *router  = adapter->router;
    int router_maskbit;

    if (!PyArg_ParseTuple(args, "i", &router_maskbit))
        return 0;

    qdr_core_del_router(router->router_core, router_maskbit);
    qd_tracemask_del_router(router->tracemask, router_maskbit);

    Py_INCREF(Py_None);
    return Py_None;
}


static PyObject* qd_set_link(PyObject *self, PyObject *args)
{
    RouterAdapter *adapter = (RouterAdapter*) self;
    qd_router_t   *router  = adapter->router;
    int            router_maskbit;
    int            link_maskbit;

    if (!PyArg_ParseTuple(args, "ii", &router_maskbit, &link_maskbit))
        return 0;

    qdr_core_set_link(router->router_core, router_maskbit, link_maskbit);
    qd_tracemask_set_link(router->tracemask, router_maskbit, link_maskbit);

    Py_INCREF(Py_None);
    return Py_None;
}


static PyObject* qd_remove_link(PyObject *self, PyObject *args)
{
    RouterAdapter *adapter = (RouterAdapter*) self;
    qd_router_t   *router  = adapter->router;
    int            router_maskbit;

    if (!PyArg_ParseTuple(args, "i", &router_maskbit))
        return 0;

    qdr_core_remove_link(router->router_core, router_maskbit);
    qd_tracemask_remove_link(router->tracemask, router_maskbit);

    Py_INCREF(Py_None);
    return Py_None;
}


static PyObject* qd_set_next_hop(PyObject *self, PyObject *args)
{
    RouterAdapter *adapter = (RouterAdapter*) self;
    qd_router_t   *router  = adapter->router;
    int            router_maskbit;
    int            next_hop_maskbit;

    if (!PyArg_ParseTuple(args, "ii", &router_maskbit, &next_hop_maskbit))
        return 0;

    qdr_core_set_next_hop(router->router_core, router_maskbit, next_hop_maskbit);

    Py_INCREF(Py_None);
    return Py_None;
}


static PyObject* qd_remove_next_hop(PyObject *self, PyObject *args)
{
    RouterAdapter *adapter = (RouterAdapter*) self;
    qd_router_t   *router  = adapter->router;
    int            router_maskbit;

    if (!PyArg_ParseTuple(args, "i", &router_maskbit))
        return 0;

    qdr_core_remove_next_hop(router->router_core, router_maskbit);

    Py_INCREF(Py_None);
    return Py_None;
}


static PyObject* qd_set_cost(PyObject *self, PyObject *args)
{
    RouterAdapter *adapter = (RouterAdapter*) self;
    qd_router_t   *router  = adapter->router;
    int            router_maskbit;
    int            cost;

    if (!PyArg_ParseTuple(args, "ii", &router_maskbit, &cost))
        return 0;

    qdr_core_set_cost(router->router_core, router_maskbit, cost);

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

        Py_ssize_t    origin_count = PyList_Size(origin_list);
        qd_bitmask_t *core_bitmask = qd_bitmask(0);
        int           maskbit;

        for (idx = 0; idx < origin_count; idx++) {
            PyObject *pi = PyList_GetItem(origin_list, idx);
            assert(QD_PY_INT_CHECK(pi));
            maskbit = (int)QD_PY_INT_2_INT64(pi);
            if (maskbit >= qd_bitmask_width() || maskbit < 0) {
                error = "Origin bit mask out of range";
                break;
            }
        }

        if (error == 0) {
            qd_bitmask_set_bit(core_bitmask, 0);  // This router is a valid origin for all destinations
            for (idx = 0; idx < origin_count; idx++) {
                PyObject *pi = PyList_GetItem(origin_list, idx);
                assert(QD_PY_INT_CHECK(pi));
                maskbit = (int)QD_PY_INT_2_INT64(pi);
                qd_bitmask_set_bit(core_bitmask, maskbit);
            }
        } else {
            qd_bitmask_free(core_bitmask);
            break;
        }

        qdr_core_set_valid_origins(router->router_core, router_maskbit, core_bitmask);
    } while (0);

    if (error) {
        PyErr_SetString(PyExc_Exception, error);
        return 0;
    }

    Py_INCREF(Py_None);
    return Py_None;
}


static PyObject* qd_set_radius(PyObject *self, PyObject *args)
{
    RouterAdapter *adapter = (RouterAdapter*) self;
    qd_router_t   *router  = adapter->router;

    if (!PyArg_ParseTuple(args, "i", &router->topology_radius))
        return 0;

    Py_INCREF(Py_None);
    return Py_None;
}


static PyObject* qd_flush_destinations(PyObject *self, PyObject *args)
{
    RouterAdapter *adapter = (RouterAdapter*) self;
    qd_router_t   *router  = adapter->router;
    int            maskbit;

    if (!PyArg_ParseTuple(args, "i", &maskbit))
        return 0;

    if (maskbit >= qd_bitmask_width() || maskbit < 0) {
        PyErr_SetString(PyExc_Exception, "Router bit mask out of range");
        return 0;
    }

    qdr_core_flush_destinations(router->router_core, maskbit);

    Py_INCREF(Py_None);
    return Py_None;
}


static PyObject* qd_mobile_seq_advanced(PyObject *self, PyObject *args)
{
    RouterAdapter *adapter = (RouterAdapter*) self;
    qd_router_t   *router  = adapter->router;
    int            maskbit;

    if (!PyArg_ParseTuple(args, "i", &maskbit))
        return 0;

    if (maskbit >= qd_bitmask_width() || maskbit < 0) {
        PyErr_SetString(PyExc_Exception, "Router bit mask out of range");
        return 0;
    }

    qdr_core_mobile_seq_advanced(router->router_core, maskbit);

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
    {"add_router",          qd_add_router,          METH_VARARGS, "A new remote/reachable router has been discovered"},
    {"del_router",          qd_del_router,          METH_VARARGS, "We've lost reachability to a remote router"},
    {"set_link",            qd_set_link,            METH_VARARGS, "Set the link for a neighbor router"},
    {"remove_link",         qd_remove_link,         METH_VARARGS, "Remove the link for a neighbor router"},
    {"set_next_hop",        qd_set_next_hop,        METH_VARARGS, "Set the next hop for a remote router"},
    {"remove_next_hop",     qd_remove_next_hop,     METH_VARARGS, "Remove the next hop for a remote router"},
    {"set_cost",            qd_set_cost,            METH_VARARGS, "Set the cost to reach a remote router"},
    {"set_valid_origins",   qd_set_valid_origins,   METH_VARARGS, "Set the valid origins for a remote router"},
    {"set_radius",          qd_set_radius,          METH_VARARGS, "Set the current topology radius"},
    {"flush_destinations",  qd_flush_destinations,  METH_VARARGS, "Remove all mapped destinations from a router"},
    {"mobile_seq_advanced", qd_mobile_seq_advanced, METH_VARARGS, "Mobile sequence for a router moved ahead of the local value"},
    {"get_agent",           qd_get_agent,           METH_VARARGS, "Get the management agent"},
    {0, 0, 0, 0}
};

static PyTypeObject RouterAdapterType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "dispatch.RouterAdapter",
    .tp_basicsize = sizeof(RouterAdapter),
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_doc = "Dispatch Router Adapter",
    .tp_methods = RouterAdapter_methods,
    .tp_new = PyType_GenericNew,
};


static void qd_router_set_mobile_seq(void *context, int router_mask_bit, uint64_t mobile_seq)
{
    qd_router_t *router = (qd_router_t*) context;
    PyObject    *pArgs;
    PyObject    *pValue;

    if (pySetMobileSeq && router->router_mode == QD_ROUTER_MODE_INTERIOR) {
        qd_python_lock_state_t lock_state = qd_python_lock();
        pArgs = PyTuple_New(2);
        PyTuple_SetItem(pArgs, 0, PyLong_FromLong((long) router_mask_bit));
        PyTuple_SetItem(pArgs, 1, PyLong_FromLong((long) mobile_seq));
        pValue = PyObject_CallObject(pySetMobileSeq, pArgs);
        qd_error_py();
        Py_DECREF(pArgs);
        Py_XDECREF(pValue);
        qd_python_unlock(lock_state);
    }
}


static void qd_router_set_my_mobile_seq(void *context, uint64_t mobile_seq)
{
    qd_router_t *router = (qd_router_t*) context;
    PyObject    *pArgs;
    PyObject    *pValue;

    if (pySetMobileSeq && router->router_mode == QD_ROUTER_MODE_INTERIOR) {
        qd_python_lock_state_t lock_state = qd_python_lock();
        pArgs = PyTuple_New(1);
        PyTuple_SetItem(pArgs, 0, PyLong_FromLong((long) mobile_seq));
        pValue = PyObject_CallObject(pySetMyMobileSeq, pArgs);
        qd_error_py();
        Py_DECREF(pArgs);
        Py_XDECREF(pValue);
        qd_python_unlock(lock_state);
    }
}


static void qd_router_link_lost(void *context, int link_mask_bit)
{
    qd_router_t *router = (qd_router_t*) context;
    PyObject    *pArgs;
    PyObject    *pValue;

    if (pyLinkLost && router->router_mode == QD_ROUTER_MODE_INTERIOR) {
        qd_python_lock_state_t lock_state = qd_python_lock();
        pArgs = PyTuple_New(1);
        PyTuple_SetItem(pArgs, 0, PyLong_FromLong((long) link_mask_bit));
        pValue = PyObject_CallObject(pyLinkLost, pArgs);
        qd_error_py();
        Py_DECREF(pArgs);
        Py_XDECREF(pValue);
        qd_python_unlock(lock_state);
    }
}


qd_error_t qd_router_python_setup(qd_router_t *router)
{
    qd_error_clear();
    log_source = qd_log_source("ROUTER");

    qdr_core_route_table_handlers(router->router_core,
                                  router,
                                  qd_router_set_mobile_seq,
                                  qd_router_set_my_mobile_seq,
                                  qd_router_link_lost);

    //
    // If we are not operating as an interior router, don't start the
    // router module.
    //
    if (router->router_mode != QD_ROUTER_MODE_INTERIOR)
        return QD_ERROR_NONE;

    PyObject *pDispatchModule = qd_python_module();
    PyType_Ready(&RouterAdapterType);
    QD_ERROR_PY_RET();

    PyModule_AddType(pDispatchModule, (PyTypeObject*) &RouterAdapterType);

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
    pId = PyUnicode_FromString(router->router_id);
    PyTuple_SetItem(pArgs, 1, pId);

    // arg 2: area_id
    pArea = PyUnicode_FromString(router->router_area);
    PyTuple_SetItem(pArgs, 2, pArea);

    // arg 3: max_routers
    pMaxRouters = PyLong_FromLong((long) qd_bitmask_width());
    PyTuple_SetItem(pArgs, 3, pMaxRouters);

    //
    // Instantiate the router
    //
    pyRouter = PyObject_CallObject(pClass, pArgs);
    Py_DECREF(pClass);
    Py_DECREF(pArgs);
    Py_DECREF(adapterType);
    QD_ERROR_PY_RET();

    pyTick           = PyObject_GetAttrString(pyRouter, "handleTimerTick"); QD_ERROR_PY_RET();
    pySetMobileSeq   = PyObject_GetAttrString(pyRouter, "setMobileSeq"); QD_ERROR_PY_RET();
    pySetMyMobileSeq = PyObject_GetAttrString(pyRouter, "setMyMobileSeq"); QD_ERROR_PY_RET();
    pyLinkLost       = PyObject_GetAttrString(pyRouter, "linkLost"); QD_ERROR_PY_RET();
    return qd_error_code();
}

void qd_router_python_free(qd_router_t *router) {
    Py_XDECREF(pyRouter);
    Py_XDECREF(pyTick);
    Py_XDECREF(pySetMobileSeq);
    Py_XDECREF(pySetMyMobileSeq);
    Py_XDECREF(pyLinkLost);
//    qd_python_finalize();
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


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
#include <qpid/dispatch/log.h>
#include <qpid/dispatch/amqp.h>
#include <qpid/dispatch/alloc.h>
#include <qpid/dispatch/router.h>


//===============================================================================
// Control Functions
//===============================================================================

static qd_dispatch_t *dispatch   = 0;
static uint32_t       ref_count  = 0;
static sys_mutex_t   *lock       = 0;
static char          *log_module = "PYTHON";
static PyObject      *dispatch_module = 0;
static PyObject      *dispatch_python_pkgdir = 0;

static qd_address_semantics_t py_semantics = {false, QD_FORWARD_MULTICAST};

static void qd_python_setup(void);


void qd_python_initialize(qd_dispatch_t *qd,
                          const char    *python_pkgdir)
{
    dispatch = qd;
    lock = sys_mutex();
    if (python_pkgdir)
        dispatch_python_pkgdir = PyString_FromString(python_pkgdir);
}


void qd_python_finalize(void)
{
    assert(ref_count == 0);
    sys_mutex_free(lock);
}


void qd_python_start(void)
{
    sys_mutex_lock(lock);
    if (ref_count == 0) {
        Py_Initialize();
        qd_python_setup();
        qd_log(log_module, LOG_TRACE, "Embedded Python Interpreter Initialized");
    }
    ref_count++;
    sys_mutex_unlock(lock);
}


void qd_python_stop(void)
{
    sys_mutex_lock(lock);
    ref_count--;
    if (ref_count == 0) {
        Py_DECREF(dispatch_module);
        dispatch_module = 0;
        Py_Finalize();
        qd_log(log_module, LOG_TRACE, "Embedded Python Interpreter Shut Down");
    }
    sys_mutex_unlock(lock);
}


PyObject *qd_python_module(void)
{
    assert(dispatch_module);
    return dispatch_module;
}


//===============================================================================
// Data Conversion Functions
//===============================================================================

static PyObject *parsed_to_py_string(qd_parsed_field_t *field)
{
    switch (qd_parse_tag(field)) {
    case QD_AMQP_VBIN8:
    case QD_AMQP_VBIN32:
    case QD_AMQP_STR8_UTF8:
    case QD_AMQP_STR32_UTF8:
    case QD_AMQP_SYM8:
    case QD_AMQP_SYM32:
        break;
    default:
        return Py_None;
    }

#define SHORT_BUF 1024
    uint8_t short_buf[SHORT_BUF];
    PyObject *result;
    qd_field_iterator_t *raw = qd_parse_raw(field);
    qd_field_iterator_reset(raw);
    uint32_t length = qd_field_iterator_remaining(raw);
    uint8_t *buffer = short_buf;
    uint8_t *ptr;
    int alloc = 0;

    if (length > SHORT_BUF) {
        alloc = 1;
        buffer = (uint8_t*) malloc(length);
    }

    ptr = buffer;
    while (!qd_field_iterator_end(raw))
        *(ptr++) = qd_field_iterator_octet(raw);
    result = PyString_FromStringAndSize((char*) buffer, ptr - buffer);
    if (alloc)
        free(buffer);

    return result;
}


void qd_py_to_composed(PyObject *value, qd_composed_field_t *field)
{
    if      (PyBool_Check(value))
        qd_compose_insert_bool(field, PyInt_AS_LONG(value) ? 1 : 0);

    //else if (PyFloat_Check(value))
    //    qd_compose_insert_double(field, PyFloat_AS_DOUBLE(value));

    else if (PyInt_Check(value))
        qd_compose_insert_long(field, (int64_t) PyInt_AS_LONG(value));

    else if (PyLong_Check(value))
        qd_compose_insert_long(field, (int64_t) PyLong_AsLongLong(value));

    else if (PyString_Check(value))
        qd_compose_insert_string(field, PyString_AS_STRING(value));

    else if (PyDict_Check(value)) {
        Py_ssize_t  iter = 0;
        PyObject   *key;
        PyObject   *val;
        qd_compose_start_map(field);
        while (PyDict_Next(value, &iter, &key, &val)) {
            qd_py_to_composed(key, field);
            qd_py_to_composed(val, field);
        }
        qd_compose_end_map(field);
    }

    else if (PyList_Check(value)) {
        Py_ssize_t count = PyList_Size(value);
        qd_compose_start_list(field);
        for (Py_ssize_t idx = 0; idx < count; idx++) {
            PyObject *item = PyList_GetItem(value, idx);
            qd_py_to_composed(item, field);
        }
        qd_compose_end_list(field);
    }

    else if (PyTuple_Check(value)) {
        Py_ssize_t count = PyTuple_Size(value);
        qd_compose_start_list(field);
        for (Py_ssize_t idx = 0; idx < count; idx++) {
            PyObject *item = PyTuple_GetItem(value, idx);
            qd_py_to_composed(item, field);
        }
        qd_compose_end_list(field);
    }
}


PyObject *qd_field_to_py(qd_parsed_field_t *field)
{
    PyObject *result = Py_None;
    uint8_t   tag    = qd_parse_tag(field);

    switch (tag) {
    case QD_AMQP_NULL:
        result = Py_None;
        break;

    case QD_AMQP_BOOLEAN:
    case QD_AMQP_TRUE:
    case QD_AMQP_FALSE:
        result = qd_parse_as_uint(field) ? Py_True : Py_False;
        break;

    case QD_AMQP_UBYTE:
    case QD_AMQP_USHORT:
    case QD_AMQP_UINT:
    case QD_AMQP_SMALLUINT:
    case QD_AMQP_UINT0:
        result = PyInt_FromLong((long) qd_parse_as_uint(field));
        break;

    case QD_AMQP_ULONG:
    case QD_AMQP_SMALLULONG:
    case QD_AMQP_ULONG0:
    case QD_AMQP_TIMESTAMP:
        result = PyLong_FromUnsignedLongLong((unsigned PY_LONG_LONG) qd_parse_as_ulong(field));
        break;

    case QD_AMQP_BYTE:
    case QD_AMQP_SHORT:
    case QD_AMQP_INT:
    case QD_AMQP_SMALLINT:
        result = PyInt_FromLong((long) qd_parse_as_int(field));
        break;

    case QD_AMQP_LONG:
    case QD_AMQP_SMALLLONG:
        result = PyLong_FromUnsignedLongLong((unsigned PY_LONG_LONG) qd_parse_as_long(field));
        break;

    case QD_AMQP_FLOAT:
    case QD_AMQP_DOUBLE:
    case QD_AMQP_DECIMAL32:
    case QD_AMQP_DECIMAL64:
    case QD_AMQP_DECIMAL128:
    case QD_AMQP_UTF32:
    case QD_AMQP_UUID:
        break;

    case QD_AMQP_VBIN8:
    case QD_AMQP_VBIN32:
    case QD_AMQP_STR8_UTF8:
    case QD_AMQP_STR32_UTF8:
    case QD_AMQP_SYM8:
    case QD_AMQP_SYM32:
        result = parsed_to_py_string(field);
        break;

    case QD_AMQP_LIST0:
    case QD_AMQP_LIST8:
    case QD_AMQP_LIST32: {
        uint32_t count = qd_parse_sub_count(field);
        result = PyList_New(count);
        for (uint32_t idx = 0; idx < count; idx++) {
            qd_parsed_field_t *sub = qd_parse_sub_value(field, idx);
            PyObject *pysub = qd_field_to_py(sub);
            if (pysub == 0)
                return 0;
            PyList_SetItem(result, idx, pysub);
        }
        break;
    }
    case QD_AMQP_MAP8:
    case QD_AMQP_MAP32: {
        uint32_t count = qd_parse_sub_count(field);
        result = PyDict_New();
        for (uint32_t idx = 0; idx < count; idx++) {
            qd_parsed_field_t *key = qd_parse_sub_key(field, idx);
            qd_parsed_field_t *val = qd_parse_sub_value(field, idx);
            PyObject *pykey = parsed_to_py_string(key);
            PyObject *pyval = qd_field_to_py(val);
            if (pyval == 0)
                return 0;
            PyDict_SetItem(result, pykey, pyval);
            Py_DECREF(pykey);
            Py_DECREF(pyval);
        }
        break;
    }
    case QD_AMQP_ARRAY8:
    case QD_AMQP_ARRAY32:
        break;
    }

    return result;
}


//===============================================================================
// Logging Object
//===============================================================================

typedef struct {
    PyObject_HEAD
    PyObject *module_name;
} LogAdapter;


static int LogAdapter_init(LogAdapter *self, PyObject *args, PyObject *kwds)
{
    const char *text;
    if (!PyArg_ParseTuple(args, "s", &text))
        return -1;

    self->module_name = PyString_FromString(text);
    return 0;
}


static void LogAdapter_dealloc(LogAdapter* self)
{
    Py_XDECREF(self->module_name);
    self->ob_type->tp_free((PyObject*)self);
}


static PyObject* qd_python_log(PyObject *self, PyObject *args)
{
    int level;
    const char* text;

    if (!PyArg_ParseTuple(args, "is", &level, &text))
        return 0;

    LogAdapter *self_ptr = (LogAdapter*) self;
    char       *logmod   = PyString_AS_STRING(self_ptr->module_name);

    qd_log(logmod, level, text);

    Py_INCREF(Py_None);
    return Py_None;
}


static PyMethodDef LogAdapter_methods[] = {
    {"log", qd_python_log, METH_VARARGS, "Emit a Log Line"},
    {0, 0, 0, 0}
};

static PyMethodDef empty_methods[] = {
  {0, 0, 0, 0}
};

static PyTypeObject LogAdapterType = {
    PyObject_HEAD_INIT(0)
    0,                         /* ob_size*/
    "dispatch.LogAdapter",     /* tp_name*/
    sizeof(LogAdapter),        /* tp_basicsize*/
    0,                         /* tp_itemsize*/
    (destructor)LogAdapter_dealloc, /* tp_dealloc*/
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
    "Dispatch Log Adapter",    /* tp_doc */
    0,                         /* tp_traverse */
    0,                         /* tp_clear */
    0,                         /* tp_richcompare */
    0,                         /* tp_weaklistoffset */
    0,                         /* tp_iter */
    0,                         /* tp_iternext */
    LogAdapter_methods,        /* tp_methods */
    0,                         /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)LogAdapter_init, /* tp_init */
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


//===============================================================================
// Message IO Object
//===============================================================================

typedef struct {
    PyObject_HEAD
    PyObject       *handler;
    PyObject       *handler_rx_call;
    qd_dispatch_t  *qd;
    Py_ssize_t      addr_count;
    qd_address_t  **addrs;
} IoAdapter;


static void qd_io_rx_handler(void *context, qd_message_t *msg, int link_id)
{
    IoAdapter *self = (IoAdapter*) context;

    //
    // Parse the message through the body and exit if the message is not well formed.
    //
    if (!qd_message_check(msg, QD_DEPTH_BODY))
        return;

    //
    // Get an iterator for the application-properties.  Exit if the message has none.
    //
    qd_field_iterator_t *ap = qd_message_field_iterator(msg, QD_FIELD_APPLICATION_PROPERTIES);
    if (ap == 0)
        return;

    //
    // Try to get a map-view of the application-properties.
    //
    qd_parsed_field_t *ap_map = qd_parse(ap);
    if (ap_map == 0 || !qd_parse_ok(ap_map) || !qd_parse_is_map(ap_map)) {
        qd_field_iterator_free(ap);
        qd_parse_free(ap_map);
        return;
    }

    //
    // Get an iterator for the body.  Exit if the message has none.
    //
    qd_field_iterator_t *body = qd_message_field_iterator(msg, QD_FIELD_BODY);
    if (body == 0) {
        qd_field_iterator_free(ap);
        qd_parse_free(ap_map);
        return;
    }

    //
    // Try to get a map-view of the body.
    //
    qd_parsed_field_t *body_map = qd_parse(body);
    if (body_map == 0 || !qd_parse_ok(body_map) || !qd_parse_is_map(body_map)) {
        qd_field_iterator_free(ap);
        qd_field_iterator_free(body);
        qd_parse_free(ap_map);
        qd_parse_free(body_map);
        return;
    }

    sys_mutex_lock(lock);
    PyObject *pAP   = qd_field_to_py(ap_map);
    PyObject *pBody = qd_field_to_py(body_map);

    PyObject *pArgs = PyTuple_New(3);
    PyTuple_SetItem(pArgs, 0, pAP);
    PyTuple_SetItem(pArgs, 1, pBody);
    PyTuple_SetItem(pArgs, 2, PyInt_FromLong((long) link_id));

    PyObject *pValue = PyObject_CallObject(self->handler_rx_call, pArgs);
    Py_DECREF(pArgs);
    if (pValue) {
        Py_DECREF(pValue);
    }
    sys_mutex_unlock(lock);

    qd_field_iterator_free(ap);
    qd_field_iterator_free(body);
    qd_parse_free(ap_map);
    qd_parse_free(body_map);
}


static int IoAdapter_init(IoAdapter *self, PyObject *args, PyObject *kwds)
{
    PyObject *addrs;
    if (!PyArg_ParseTuple(args, "OO", &self->handler, &addrs))
        return -1;

    self->handler_rx_call = PyObject_GetAttrString(self->handler, "receive");
    if (!self->handler_rx_call || !PyCallable_Check(self->handler_rx_call))
        return -1;

    if (!PyTuple_Check(addrs))
        return -1;

    Py_INCREF(self->handler);
    Py_INCREF(self->handler_rx_call);
    self->qd         = dispatch;
    self->addr_count = PyTuple_Size(addrs);
    self->addrs      = NEW_PTR_ARRAY(qd_address_t, self->addr_count);
    for (Py_ssize_t idx = 0; idx < self->addr_count; idx++)
        self->addrs[idx] = qd_router_register_address(self->qd,
                                                      PyString_AS_STRING(PyTuple_GetItem(addrs, idx)),
                                                      qd_io_rx_handler, &py_semantics, self);
    return 0;
}


static void IoAdapter_dealloc(IoAdapter* self)
{
    for (Py_ssize_t idx = 0; idx < self->addr_count; idx++)
        qd_router_unregister_address(self->addrs[idx]);
    free(self->addrs);
    Py_DECREF(self->handler);
    Py_DECREF(self->handler_rx_call);
    self->ob_type->tp_free((PyObject*)self);
}


static PyObject* qd_python_send(PyObject *self, PyObject *args)
{
    IoAdapter           *ioa   = (IoAdapter*) self;
    qd_composed_field_t *field = 0;
    const char          *address;
    PyObject            *app_properties;
    PyObject            *body;

    if (!PyArg_ParseTuple(args, "sOO", &address, &app_properties, &body))
        return 0;

    field = qd_compose(QD_PERFORMATIVE_DELIVERY_ANNOTATIONS, field);
    qd_compose_start_map(field);

    qd_compose_insert_string(field, QD_DA_INGRESS);
    qd_compose_insert_string(field, qd_router_id(ioa->qd));

    qd_compose_insert_string(field, QD_DA_TRACE);
    qd_compose_start_list(field);
    qd_compose_insert_string(field, qd_router_id(ioa->qd));
    qd_compose_end_list(field);

    qd_compose_end_map(field);

    field = qd_compose(QD_PERFORMATIVE_PROPERTIES, field);
    qd_compose_start_list(field);
    qd_compose_insert_null(field);            // message-id
    qd_compose_insert_null(field);            // user-id
    qd_compose_insert_string(field, address); // to
    qd_compose_end_list(field);

    field = qd_compose(QD_PERFORMATIVE_APPLICATION_PROPERTIES, field);
    qd_py_to_composed(app_properties, field);

    field = qd_compose(QD_PERFORMATIVE_BODY_AMQP_VALUE, field);
    qd_py_to_composed(body, field);

    qd_message_t *msg = qd_message();
    qd_message_compose_2(msg, field);
    qd_router_send2(ioa->qd, address, msg);
    qd_message_free(msg);
    qd_compose_free(field);

    Py_INCREF(Py_None);
    return Py_None;
}


static PyMethodDef IoAdapter_methods[] = {
    {"send", qd_python_send, METH_VARARGS, "Send a Message"},
    {0, 0, 0, 0}
};


static PyTypeObject IoAdapterType = {
    PyObject_HEAD_INIT(0)
    0,                         /* ob_size*/
    "dispatch.IoAdapter",      /* tp_name*/
    sizeof(IoAdapter),         /* tp_basicsize*/
    0,                         /* tp_itemsize*/
    (destructor)IoAdapter_dealloc, /* tp_dealloc*/
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
    "Dispatch IO Adapter",     /* tp_doc */
    0,                         /* tp_traverse */
    0,                         /* tp_clear */
    0,                         /* tp_richcompare */
    0,                         /* tp_weaklistoffset */
    0,                         /* tp_iter */
    0,                         /* tp_iternext */
    IoAdapter_methods,         /* tp_methods */
    0,                         /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)IoAdapter_init,  /* tp_init */
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


//===============================================================================
// Initialization of Modules and Types
//===============================================================================

static void qd_register_log_constant(PyObject *module, const char *name, uint32_t value)
{
    PyObject *const_object = PyInt_FromLong((long) value);
    Py_INCREF(const_object);
    PyModule_AddObject(module, name, const_object);
}


static void qd_python_setup(void)
{
    LogAdapterType.tp_new = PyType_GenericNew;
    IoAdapterType.tp_new  = PyType_GenericNew;
    if ((PyType_Ready(&LogAdapterType) < 0) || (PyType_Ready(&IoAdapterType) < 0)) {
        PyErr_Print();
        qd_log(log_module, LOG_ERROR, "Unable to initialize Adapters");
        assert(0);
    } else {
        PyObject *m = Py_InitModule3("dispatch", empty_methods, "Dispatch Adapter Module");

        //
        // Append sys.path to include location of Dispatch libraries
        //
        if (dispatch_python_pkgdir) {
            PyObject *sys_path = PySys_GetObject("path");
            PyList_Append(sys_path, dispatch_python_pkgdir);
        }

        //
        // Add LogAdapter
        //
        PyTypeObject *laType = &LogAdapterType;
        Py_INCREF(laType);
        PyModule_AddObject(m, "LogAdapter", (PyObject*) &LogAdapterType);

        qd_register_log_constant(m, "LOG_TRACE",    LOG_TRACE);
        qd_register_log_constant(m, "LOG_DEBUG",    LOG_DEBUG);
        qd_register_log_constant(m, "LOG_INFO",     LOG_INFO);
        qd_register_log_constant(m, "LOG_NOTICE",   LOG_NOTICE);
        qd_register_log_constant(m, "LOG_WARNING",  LOG_WARNING);
        qd_register_log_constant(m, "LOG_ERROR",    LOG_ERROR);
        qd_register_log_constant(m, "LOG_CRITICAL", LOG_CRITICAL);

        //
        PyTypeObject *ioaType = &IoAdapterType;
        Py_INCREF(ioaType);
        PyModule_AddObject(m, "IoAdapter", (PyObject*) &IoAdapterType);

        Py_INCREF(m);
        dispatch_module = m;
    }
}

void qd_python_lock(void)
{
    sys_mutex_lock(lock);
}

void qd_python_unlock(void)
{
    sys_mutex_unlock(lock);
}


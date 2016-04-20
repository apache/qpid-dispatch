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

#include "entity_cache.h"
#include <qpid/dispatch/python_embedded.h>
#include <qpid/dispatch/threading.h>
#include <qpid/dispatch/log.h>
#include <qpid/dispatch/error.h>
#include <qpid/dispatch/amqp.h>
#include "alloc.h"
#include <qpid/dispatch/router.h>
#include <qpid/dispatch/error.h>


#define DISPATCH_MODULE "qpid_dispatch_internal.dispatch"

//===============================================================================
// Control Functions
//===============================================================================

static qd_dispatch_t   *dispatch   = 0;
static sys_mutex_t     *ilock      = 0;
static bool             lock_held  = false;
static qd_log_source_t *log_source = 0;
static PyObject        *dispatch_module = 0;
static PyObject        *message_type = 0;
static PyObject        *dispatch_python_pkgdir = 0;

static void qd_python_setup(void);


void qd_python_initialize(qd_dispatch_t *qd, const char *python_pkgdir)
{
    log_source = qd_log_source("PYTHON");
    dispatch = qd;
    ilock = sys_mutex();
    if (python_pkgdir)
        dispatch_python_pkgdir = PyString_FromString(python_pkgdir);

    qd_python_lock_state_t ls = qd_python_lock();
    Py_Initialize();
    qd_python_setup();
    qd_python_unlock(ls);
}


void qd_python_finalize(void)
{
    sys_mutex_free(ilock);
    Py_DECREF(dispatch_module);
    dispatch_module = 0;
    PyGC_Collect();
    Py_Finalize();
}


PyObject *qd_python_module(void)
{
    assert(dispatch_module);
    return dispatch_module;
}


void qd_python_check_lock(void)
{
    assert(lock_held);
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
        Py_RETURN_NONE;
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


qd_error_t qd_py_to_composed(PyObject *value, qd_composed_field_t *field)
{
    qd_python_check_lock();
    qd_error_clear();
    if (value == Py_None) {
        qd_compose_insert_null(field);
    }
    else if (PyBool_Check(value)) {
        qd_compose_insert_bool(field, PyInt_AS_LONG(value) ? 1 : 0);
    }
    else if (PyInt_Check(value)) {
        qd_compose_insert_long(field, (int64_t) PyInt_AS_LONG(value));
    }
    else if (PyLong_Check(value)) {
        qd_compose_insert_long(field, (int64_t) PyLong_AsLongLong(value));
    }
    else if (PyString_Check(value) || PyUnicode_Check(value)) {
        qd_compose_insert_string(field, PyString_AsString(value));
    }
    else if (PyDict_Check(value)) {
        Py_ssize_t  iter = 0;
        PyObject   *key;
        PyObject   *val;
        qd_compose_start_map(field);
        while (PyDict_Next(value, &iter, &key, &val)) {
            qd_py_to_composed(key, field); QD_ERROR_RET();
            qd_py_to_composed(val, field); QD_ERROR_RET();
        }
        QD_ERROR_PY_RET();
        qd_compose_end_map(field);
    }

    else if (PyList_Check(value)) {
        Py_ssize_t count = PyList_Size(value);
        if (count == 0)
            qd_compose_empty_list(field);
        else {
            qd_compose_start_list(field);
            for (Py_ssize_t idx = 0; idx < count; idx++) {
                PyObject *item = PyList_GetItem(value, idx); QD_ERROR_PY_RET();
                qd_py_to_composed(item, field); QD_ERROR_RET();
            }
            qd_compose_end_list(field);
        }
    }

    else if (PyTuple_Check(value)) {
        Py_ssize_t count = PyTuple_Size(value);
        if (count == 0)
            qd_compose_empty_list(field);
        else {
            qd_compose_start_list(field);
            for (Py_ssize_t idx = 0; idx < count; idx++) {
                PyObject *item = PyTuple_GetItem(value, idx); QD_ERROR_PY_RET();
                qd_py_to_composed(item, field); QD_ERROR_RET();
            }
            qd_compose_end_list(field);
        }
    }
    else {
        PyObject *type=0, *typestr=0, *repr=0;
        if ((type = PyObject_Type(value)) &&
            (typestr = PyObject_Str(type)) &&
            (repr = PyObject_Repr(value)))
            qd_error(QD_ERROR_TYPE, "Can't compose object of type %s: %s",
                     PyString_AsString(typestr), PyString_AsString(repr));
        else
            qd_error(QD_ERROR_TYPE, "Can't compose python object of unknown type");

        Py_XDECREF(type);
        Py_XDECREF(typestr);
        Py_XDECREF(repr);
    }
    return qd_error_code();
}

void qd_py_attr_to_composed(PyObject *object, const char *attr, qd_composed_field_t *field)
{
    qd_python_check_lock();
    PyObject *value = PyObject_GetAttrString(object, attr);
    if (value) {
        qd_py_to_composed(value, field);
        Py_DECREF(value);
    }
    else {
        qd_error_py();
    }
}

PyObject *qd_field_to_py(qd_parsed_field_t *field)
{
    qd_python_check_lock();
    PyObject *result = 0;
    uint8_t   tag    = qd_parse_tag(field);
    switch (tag) {
      case QD_AMQP_NULL:
        Py_INCREF(Py_None);
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
        result = PyLong_FromLongLong((PY_LONG_LONG)qd_parse_as_long(field));
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
    if (!result)
        Py_RETURN_NONE;
    return result;
}


//===============================================================================
// Logging Object
//===============================================================================

typedef struct {
    PyObject_HEAD
    PyObject *module_name;
    qd_log_source_t *log_source;
} LogAdapter;


static int LogAdapter_init(LogAdapter *self, PyObject *args, PyObject *kwds)
{
    const char *text;
    if (!PyArg_ParseTuple(args, "s", &text))
        return -1;

    self->module_name = PyString_FromString(text);
    self->log_source  = qd_log_source(text);
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
    const char *text;
    const char *file;
    int line;

    if (!PyArg_ParseTuple(args, "issi", &level, &text, &file, &line))
        return 0;

    LogAdapter *self_ptr = (LogAdapter*) self;
    //char       *logmod   = PyString_AS_STRING(self_ptr->module_name);

    qd_log_impl(self_ptr->log_source, level, file, line, "%s", text);

    Py_INCREF(Py_None);
    return Py_None;
}


static PyMethodDef LogAdapter_methods[] = {
    {"log", qd_python_log, METH_VARARGS, "Emit a Log Line"},
    {0, 0, 0, 0}
};

static PyTypeObject LogAdapterType = {
    PyObject_HEAD_INIT(0)
    0,                         /* ob_size*/
    DISPATCH_MODULE ".LogAdapter",  /* tp_name*/
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
    PyObject           *handler;
    qd_dispatch_t      *qd;
    qdr_core_t         *core;
    qdr_subscription_t *sub;
} IoAdapter;

// Parse an iterator to a python object.
static PyObject *py_iter_parse(qd_field_iterator_t *iter)
{
    qd_parsed_field_t *parsed=0;
    if (iter && (parsed = qd_parse(iter))) {
        if (!qd_parse_ok(parsed)) {
            qd_error(QD_ERROR_MESSAGE, qd_parse_error(parsed));
            qd_parse_free(parsed);
            return 0;
        }
        PyObject *value = qd_field_to_py(parsed);
        qd_parse_free(parsed);
        if (!value) qd_error_py();
        return value;
    }
    qd_error(QD_ERROR_MESSAGE, "Failed to parse message field");
    return 0;
}

// Copy a string value from an iterator as a python object.
static PyObject *py_iter_copy(qd_field_iterator_t *iter)
{
    unsigned char *bytes = 0;
    PyObject *value = 0;
    (void)(iter && (bytes = qd_field_iterator_copy(iter)) && (value = PyString_FromString((char*)bytes)));
    if (bytes) free(bytes);
    return value;
}

// Copy a message field, using to_py to a python object attribute.
static qd_error_t iter_to_py_attr(qd_field_iterator_t *iter,
                                  PyObject* (*to_py)(qd_field_iterator_t *),
                                  PyObject *obj, const char *attr)
{
    qd_error_clear();
    if (iter) {
        PyObject *value = to_py(iter);
        qd_field_iterator_free(iter);
        if (value) {
            PyObject_SetAttrString(obj, attr, value);
            Py_DECREF(value);
        }
        else {
            qd_error_py();      /* In case there were python errors. */
            qd_error(QD_ERROR_MESSAGE, "Can't convert message field %s", attr);
        }
    }
    return qd_error_code();
}

static void qd_io_rx_handler(void *context, qd_message_t *msg, int link_id, int inter_router_cost)
{
    IoAdapter *self = (IoAdapter*) context;

    //
    // Parse the message through the body and exit if the message is not well formed.
    //
    if (!qd_message_check(msg, QD_DEPTH_BODY))
        return;

    // This is called from non-python threads so we need to acquire the GIL to use python APIS.
    qd_python_lock_state_t lock_state = qd_python_lock();
    PyObject *py_msg = PyObject_CallFunction(message_type, NULL);
    if (!py_msg) {
        qd_error_py();
        qd_python_unlock(lock_state);
        return;
    }
    iter_to_py_attr(qd_message_field_iterator(msg, QD_FIELD_TO), py_iter_copy, py_msg, "address");
    iter_to_py_attr(qd_message_field_iterator(msg, QD_FIELD_REPLY_TO), py_iter_copy, py_msg, "reply_to");
    // Note: correlation ID requires _typed()
    iter_to_py_attr(qd_message_field_iterator_typed(msg, QD_FIELD_CORRELATION_ID), py_iter_parse, py_msg, "correlation_id");
    iter_to_py_attr(qd_message_field_iterator(msg, QD_FIELD_APPLICATION_PROPERTIES), py_iter_parse, py_msg, "properties");
    iter_to_py_attr(qd_message_field_iterator(msg, QD_FIELD_BODY), py_iter_parse, py_msg, "body");

    PyObject *value = PyObject_CallFunction(self->handler, "Oll", py_msg, link_id, inter_router_cost);

    Py_DECREF(py_msg);
    Py_XDECREF(value);
    qd_error_py();
    qd_python_unlock(lock_state);
}


static int IoAdapter_init(IoAdapter *self, PyObject *args, PyObject *kwds)
{
    PyObject *addr;
    char aclass    = 'L';
    char phase     = '0';
    int  treatment = QD_TREATMENT_ANYCAST_CLOSEST;
    if (!PyArg_ParseTuple(args, "OO|cci", &self->handler, &addr, &aclass, &phase, &treatment))
        return -1;
    if (!PyCallable_Check(self->handler)) {
        PyErr_SetString(PyExc_TypeError, "IoAdapter.__init__ handler is not callable");
        return -1;
    }
    if (treatment == QD_TREATMENT_ANYCAST_BALANCED) {
        PyErr_SetString(PyExc_TypeError, "IoAdapter: ANYCAST_BALANCED is not supported for in-process subscriptions");
        return -1;
    }
    Py_INCREF(self->handler);
    self->qd   = dispatch;
    self->core = qd_router_core(self->qd);
    const char *address = PyString_AsString(addr);
    if (!address) return -1;
    qd_error_clear();
    self->sub = qdr_core_subscribe(self->core, address, aclass, phase, treatment, qd_io_rx_handler, self);
    if (qd_error_code()) {
        PyErr_SetString(PyExc_RuntimeError, qd_error_message());
        return -1;
    }
    return 0;
}

static void IoAdapter_dealloc(IoAdapter* self)
{
    qdr_core_unsubscribe(self->sub);
    Py_DECREF(self->handler);
    self->ob_type->tp_free((PyObject*)self);
}

static qd_error_t compose_python_message(qd_composed_field_t **field, PyObject *message,
                                         qd_dispatch_t* qd) {
    *field = qd_compose(QD_PERFORMATIVE_PROPERTIES, *field);
    qd_compose_start_list(*field);
    qd_compose_insert_null(*field);                                 // message-id
    qd_compose_insert_null(*field);                                 // user-id
    qd_py_attr_to_composed(message, "address", *field); QD_ERROR_RET(); // to
    qd_compose_insert_null(*field);                                 // subject
    qd_compose_insert_null(*field);                                 // reply-to
    qd_py_attr_to_composed(message, "correlation_id", *field); QD_ERROR_RET(); // correlation-id
    qd_compose_end_list(*field);

    *field = qd_compose(QD_PERFORMATIVE_APPLICATION_PROPERTIES, *field);  QD_ERROR_RET();
    qd_py_attr_to_composed(message, "properties", *field); QD_ERROR_RET();

    *field = qd_compose(QD_PERFORMATIVE_BODY_AMQP_VALUE, *field); QD_ERROR_RET();
    qd_py_attr_to_composed(message, "body", *field); QD_ERROR_RET();
    return qd_error_code();
}

static PyObject *qd_python_send(PyObject *self, PyObject *args)
{
    qd_error_clear();
    IoAdapter           *ioa   = (IoAdapter*) self;
    qd_composed_field_t *field = 0;
    PyObject *message = 0;
    int       no_echo = 1;
    int       control = 0;

    if (!PyArg_ParseTuple(args, "O|ii", &message, &no_echo, &control))
        return 0;

    if (compose_python_message(&field, message, ioa->qd) == QD_ERROR_NONE) {
        qd_message_t *msg = qd_message();
        qd_message_compose_2(msg, field);

        qd_composed_field_t *ingress = qd_compose_subfield(0);
        qd_compose_insert_string(ingress, qd_router_id(ioa->qd));

        qd_composed_field_t *trace = qd_compose_subfield(0);
        qd_compose_start_list(trace);
        qd_compose_insert_string(trace, qd_router_id(ioa->qd));
        qd_compose_end_list(trace);

        qd_message_set_ingress_annotation(msg, ingress);
        qd_message_set_trace_annotation(msg, trace);

        PyObject *address = PyObject_GetAttrString(message, "address");
        if (address) {
            qdr_send_to2(ioa->core, msg, PyString_AsString(address), (bool) no_echo, (bool) control);
            Py_DECREF(address);
        }
        qd_compose_free(field);
        qd_message_free(msg);
        Py_RETURN_NONE;
    }
    if (!PyErr_Occurred())
        PyErr_SetString(PyExc_RuntimeError, qd_error_message());
    return 0;
}


static PyMethodDef IoAdapter_methods[] = {
    {"send", qd_python_send, METH_VARARGS, "Send a Message"},
    {0, 0, 0, 0}
};


static PyTypeObject IoAdapterType = {
    PyObject_HEAD_INIT(0)
    0,                         /* ob_size*/
    DISPATCH_MODULE ".IoAdapter",  /* tp_name*/
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

static void qd_register_constant(PyObject *module, const char *name, uint32_t value)
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
        qd_error_py();
        qd_log(log_source, QD_LOG_CRITICAL, "Unable to initialize Adapters");
        abort();
    } else {
        //
        // Append sys.path to include location of Dispatch libraries
        //
        if (dispatch_python_pkgdir) {
            PyObject *sys_path = PySys_GetObject("path");
            PyList_Append(sys_path, dispatch_python_pkgdir);
        }

        // Import the initial dispatch module (we will add C extensions to it)
        PyObject *m = PyImport_ImportModule(DISPATCH_MODULE);
        if (!m) {
            qd_error_py();
            qd_log(log_source, QD_LOG_CRITICAL, "Cannot load dispatch extension module '%s'", DISPATCH_MODULE);
            abort();
        }

        //
        // Add LogAdapter
        //
        PyTypeObject *laType = &LogAdapterType;
        Py_INCREF(laType);
        PyModule_AddObject(m, "LogAdapter", (PyObject*) &LogAdapterType);

        qd_register_constant(m, "LOG_TRACE",    QD_LOG_TRACE);
        qd_register_constant(m, "LOG_DEBUG",    QD_LOG_DEBUG);
        qd_register_constant(m, "LOG_INFO",     QD_LOG_INFO);
        qd_register_constant(m, "LOG_NOTICE",   QD_LOG_NOTICE);
        qd_register_constant(m, "LOG_WARNING",  QD_LOG_WARNING);
        qd_register_constant(m, "LOG_ERROR",    QD_LOG_ERROR);
        qd_register_constant(m, "LOG_CRITICAL", QD_LOG_CRITICAL);

        qd_register_constant(m, "LOG_STACK_LIMIT", 8); /* Limit stack traces for logging. */

        PyTypeObject *ioaType = &IoAdapterType;
        Py_INCREF(ioaType);
        PyModule_AddObject(m, "IoAdapter", (PyObject*) &IoAdapterType);

        qd_register_constant(m, "TREATMENT_MULTICAST_FLOOD",  QD_TREATMENT_MULTICAST_FLOOD);
        qd_register_constant(m, "TREATMENT_MULTICAST_ONCE",   QD_TREATMENT_MULTICAST_ONCE);
        qd_register_constant(m, "TREATMENT_ANYCAST_CLOSEST",  QD_TREATMENT_ANYCAST_CLOSEST);
        qd_register_constant(m, "TREATMENT_ANYCAST_BALANCED", QD_TREATMENT_ANYCAST_BALANCED);

        Py_INCREF(m);
        dispatch_module = m;
    }

    // Get the router.message.Message class.
    PyObject *message_module =
        PyImport_ImportModule("qpid_dispatch_internal.router.message");
    if (message_module) {
        message_type = PyObject_GetAttrString(message_module, "Message");
        Py_DECREF(message_module);
    }
    if (!message_type) {
        qd_error_py();
        return;
    }
}

qd_python_lock_state_t qd_python_lock(void)
{
    sys_mutex_lock(ilock);
    lock_held = true;
    return 0;
}

void qd_python_unlock(qd_python_lock_state_t lock_state)
{
    lock_held = false;
    sys_mutex_unlock(ilock);
}

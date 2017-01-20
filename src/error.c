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


/* Make sure we get the XSI compliant strerror_r from string.h not the GNU one. */
#define _POSIX_C_SOURCE 200112L
#undef _GNU_SOURCE
#include <string.h>

#include <Python.h>
#include <qpid/dispatch/error.h>
#include <qpid/dispatch/enum.h>
#include <qpid/dispatch/log.h>
#include <qpid/dispatch/python_embedded.h>
#include <stdarg.h>
#include <stdio.h>
#include <errno.h>
#include "log_private.h"
#include "aprintf.h"

static const char *qd_error_names[] = {
 "No Error",
 "Not found",
 "Already exists",
 "Allocation",
 "Invalid message",
 "Python",
 "Configuration",
 "Type",
 "Value",
 "Run Time",
 "System"
};
ENUM_DEFINE(qd_error, qd_error_names);

#define ERROR_MAX QD_LOG_TEXT_MAX
const int QD_ERROR_MAX = ERROR_MAX;

/* Thread local data. */
static __thread struct {
    char error_message[ERROR_MAX];
    qd_error_t error_code;
} ts = {{0}, 0};

static qd_log_source_t* log_source = 0;

void qd_error_initialize() {
    log_source = qd_log_source("ERROR");
}

qd_error_t qd_error_vimpl(qd_error_t code, const char *file, int line, const char *fmt, va_list ap) {
    ts.error_code = code;
    if (code) {
        char *begin = ts.error_message;
        char *end = begin + ERROR_MAX;
        (void)aprintf;
        const char* name = qd_error_name(code);
        if (name)
            aprintf(&begin, end, "%s: ", name);
        else
            aprintf(&begin, end, "%d: ", code);
        vaprintf(&begin, end, fmt, ap);
        // NOTE: Use the file/line from the qd_error macro, not this line in error.c
        qd_log_impl(log_source, QD_LOG_ERROR, file, line, "%s", qd_error_message());
        return code;
    }
    else
        qd_error_clear();
    return 0;
}

qd_error_t qd_error_impl(qd_error_t code, const char *file, int line, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    qd_error_t err = qd_error_vimpl(code, file, line, fmt, ap);
    va_end(ap);
    return err;
}

qd_error_t qd_error_clear() {
    ts.error_code = 0;
    snprintf(ts.error_message, ERROR_MAX, "No Error");
    return QD_ERROR_NONE;
}

const char* qd_error_message() {
    return ts.error_message;
}

qd_error_t qd_error_code() {
    return ts.error_code;
}

static void py_set_item(PyObject *dict, const char* name, PyObject *value) {
    PyObject *py_name = PyString_FromString(name);
    PyDict_SetItem(dict, py_name, value);
    Py_DECREF(py_name);
}

static void log_trace_py(PyObject *type, PyObject *value, PyObject* trace, qd_log_level_t level,
                         const char *file, int line)
{
    if (!qd_log_enabled(log_source, level)) return;
    if (!(type && value && trace)) return;

    PyObject *module = PyImport_ImportModule("traceback");
    if (!module) return;

    PyObject *globals = PyDict_New();
    py_set_item(globals, "traceback", module);
    Py_DECREF(module);

    PyObject *locals  = PyDict_New();
    py_set_item(locals, "type", type);
    py_set_item(locals, "value", value);
    py_set_item(locals, "trace", trace);

    PyObject *result = PyRun_String(
        "''.join(traceback.format_exception(type, value, trace))", Py_eval_input, globals, locals);
    Py_DECREF(globals);
    Py_DECREF(locals);


    if (result) {
        const char* trace = PyString_AsString(result);
        if (strlen(trace) < QD_LOG_TEXT_MAX) {
            qd_log_impl(log_source, level, file, line, "%s", trace);
        } else {
            // Keep as much of the the tail of the trace as we can.
            const char *tail = trace;
            while (tail && strlen(tail) > QD_LOG_TEXT_MAX) {
                tail = strchr(tail, '\n');
                if (tail) ++tail;
            }
            qd_log_impl(log_source, level, file, line, "Traceback truncated:\n%s", tail ? tail : "");
        }
        Py_DECREF(result);
    }
}

qd_error_t qd_error_py_impl(const char *file, int line) {
    qd_python_check_lock();
    if (PyErr_Occurred()) {
        PyObject *type, *value, *trace;
        PyErr_Fetch(&type, &value, &trace); /* Note clears the python error indicator */

        PyObject *py_type_name = type ? PyObject_GetAttrString(type, "__name__") : NULL;
        const char *type_name = py_type_name ? PyString_AsString(py_type_name) : NULL;

        PyObject *py_value_str = value ? PyObject_Str(value) : NULL;
        const char *value_str = py_value_str ? PyString_AsString(py_value_str) : NULL;
        if (!value_str) value_str = "Unknown";

        PyErr_Clear(); /* Ignore errors while we're trying to build the values. */
        if (type_name)
            qd_error_impl(QD_ERROR_PYTHON, file, line, "%s: %s", type_name, value_str);
        else
            qd_error_impl(QD_ERROR_PYTHON, file, line, "%s", value_str);
        Py_XDECREF(py_value_str);
        Py_XDECREF(py_type_name);

        log_trace_py(type, value, trace, QD_LOG_ERROR, file, line);

        Py_XDECREF(type);
        Py_XDECREF(value);
        Py_XDECREF(trace);
    } else {
        qd_error_clear();
    }
    return qd_error_code();
}

qd_error_t qd_error_errno_impl(int errnum, const char *file, int line, const char *fmt, ...) {
    if (errnum) {
        ts.error_code = QD_ERROR_SYSTEM;
        char *begin = ts.error_message;
        char *end = begin + sizeof(ts.error_message);
        va_list arglist;
        va_start(arglist, fmt);
        vaprintf(&begin, end, fmt, arglist);
        va_end(arglist);
        aprintf(&begin, end, ": ", errnum);
        (void)strerror_r(errnum, begin, end - begin);
        qd_log_impl(log_source, QD_LOG_ERROR, file, line, "%s", ts.error_message);
        return qd_error_code();
    }
    else
        return qd_error_clear();
}

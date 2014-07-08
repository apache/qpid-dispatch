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
#include <qpid/dispatch/error.h>
#include <qpid/dispatch/log.h>
#include <stdarg.h>
#include <stdio.h>
#include "static_assert.h"
#include "log_private.h"

static const char *error_names[] = {
 "No Error",
 "Not found",
 "Already exists",
 "Allocation",
 "Invalid message",
 "Python",
 "Configuration"
};

STATIC_ASSERT(sizeof(error_names)/sizeof(error_names[0]) == QD_ERROR_ENUM_COUNT,
	      error_names_wrong_size);

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

qd_error_t qd_error(qd_error_t code, const char *fmt, ...) {
    ts.error_code = code;
    if (code) {
	int i = 0;
	if (code < QD_ERROR_ENUM_COUNT)
	    i = snprintf(ts.error_message, ERROR_MAX,"%s: ", error_names[code]);
	else
	    i = snprintf(ts.error_message, ERROR_MAX, "%d: ", code);
	va_list arglist;
	va_start(arglist, fmt);
	vsnprintf(ts.error_message+i, ERROR_MAX-i, fmt, arglist);
	va_end(arglist);
	qd_log(log_source, QD_LOG_ERROR, "%s", qd_error_message());
	return code;
    }
    else
	qd_error_clear();
    return 0;
}

void qd_error_clear() {
    ts.error_code = 0;
    snprintf(ts.error_message, ERROR_MAX, "No Error");
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

static void log_trace_py(PyObject *type, PyObject *value, PyObject* trace, qd_log_level_t level) {
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
	"'\\n'.join(traceback.format_exception(type, value, trace))", Py_eval_input, globals, locals);
    Py_DECREF(globals);
    Py_DECREF(locals);

    if (result) {
	const char* trace = PyString_AsString(result);
	if (strlen(trace) < QD_LOG_TEXT_MAX) {
	    qd_log(log_source, level, "%s", trace);
	} else {
	    // Keep as much of the the tail of the trace as we can.
	    const char *tail = trace;
	    while (tail && strlen(tail) > QD_LOG_TEXT_MAX) {
		tail = strchr(tail, '\n');
		if (tail) ++tail;
	    }
	    qd_log(log_source, level, "Traceback truncated:\n%s", tail ? tail : "");
	}
	Py_DECREF(result);
    }

}

qd_error_t qd_error_py() {
    if (PyErr_Occurred()) {
	PyObject *type, *value, *trace;
	PyErr_Fetch(&type, &value, &trace); /* Note clears the python error indicator */

	PyObject *type_name = type ? PyObject_GetAttrString(type, "__name__") : NULL;
	PyErr_Print();
	PyObject *value_str = value ? PyObject_Str(value) : NULL;
	const char* str = value_str ? PyString_AsString(value_str) : "Unknown";
	if (type_name)
	    qd_error(QD_ERROR_PYTHON, "%s: %s", PyString_AsString(type_name), str);
	else
	    qd_error(QD_ERROR_PYTHON, "%s", str);
	Py_XDECREF(value_str);
	Py_XDECREF(type_name);

	log_trace_py(type, value, trace, QD_LOG_ERROR);

	Py_XDECREF(type);
	Py_XDECREF(value);
	Py_XDECREF(trace);
    } else {
	qd_error_clear();
    }
    return qd_error_code();
}

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
#include "dispatch_private.h"
#include "entity_private.h"

struct qd_entity_t {
    PyObject py_object;
};

void qd_entity_free(qd_entity_t* entity) {
    Py_XDECREF(entity);
}

static PyObject* qd_entity_get_py(qd_entity_t* entity, const char* attribute) {
    PyObject *py_key = PyString_FromString(attribute);
    if (!py_key) return NULL;   /* Don't set qd_error, caller will set if needed. */
    PyObject *value = PyObject_GetItem((PyObject*)entity, py_key);
    Py_DECREF(py_key);
    return value;
}

bool qd_entity_has(qd_entity_t* entity, const char *attribute) {
    PyObject *value = qd_entity_get_py(entity, attribute);
    Py_XDECREF(value);
    PyErr_Clear();              /* Ignore errors */
    return value;
}

char *qd_entity_string(qd_entity_t *entity, const char* attribute) {
    qd_error_clear();
    PyObject *py_obj = qd_entity_get_py(entity, attribute);
    PyObject *py_str = py_obj ? PyObject_Str(py_obj) : NULL;
    char* str = py_str ? PyString_AsString(py_str) : NULL;
    Py_XDECREF(py_obj);
    Py_XDECREF(py_str);
    if (str) return strdup(str);
    qd_error_py();
    return NULL;
}

long qd_entity_long(qd_entity_t *entity, const char* attribute) {
    qd_error_clear();
    PyObject *py_obj = qd_entity_get_py(entity, attribute);
    long result = py_obj ? PyInt_AsLong(py_obj) : -1;
    Py_XDECREF(py_obj);
    qd_error_py();
    return result;
}

bool qd_entity_bool(qd_entity_t *entity, const char* attribute) {
    qd_error_clear();
    PyObject *py_obj = qd_entity_get_py(entity, attribute);
    bool result = py_obj ? PyObject_IsTrue(py_obj) : false;
    Py_XDECREF(py_obj);
    qd_error_py();
    return result;
}


char *qd_entity_opt_string(qd_entity_t *entity, const char* attribute, const char* default_value)
{
    if (qd_entity_has(entity, attribute))
	return qd_entity_string(entity, attribute);
    else
	return default_value ? strdup(default_value) : NULL;
}

long qd_entity_opt_long(qd_entity_t *entity, const char* attribute, long default_value) {
    if (qd_entity_has(entity, attribute)) {
	long result = qd_entity_long(entity, attribute);
	if (!qd_error_code())
	    return result;
    }
    return default_value;
}

bool qd_entity_opt_bool(qd_entity_t *entity, const char* attribute, bool default_value) {
    if (qd_entity_has(entity, attribute)) {
	bool result = qd_entity_bool(entity, attribute);
	if (!qd_error_code())
	    return result;
    }
    return default_value;
}

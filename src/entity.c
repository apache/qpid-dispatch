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

#include "python_private.h"  // must be first!
#include "qpid/dispatch/python_embedded.h"

#include "entity.h"

#include "dispatch_private.h"

#include "qpid/dispatch/error.h"

struct qd_entity_t {
    PyObject py_object;      /* Any object supporting __set/get_item__, e.g. dict. */
};

static PyObject* qd_entity_get_py(qd_entity_t* entity, const char* attribute) {
    PyObject *py_key = PyUnicode_FromString(attribute);
    if (!py_key) return NULL;   /* Don't set qd_error, caller will set if needed. */
    PyObject *value = PyObject_GetItem((PyObject*)entity, py_key);
    Py_DECREF(py_key);
    return value;
}

bool qd_entity_has(qd_entity_t* entity, const char *attribute) {
    PyObject *value = qd_entity_get_py(entity, attribute);
    Py_XDECREF(value);
    PyErr_Clear();              /* Ignore errors */
    return value != NULL;
}

char *qd_entity_get_string(qd_entity_t *entity, const char* attribute) {
    qd_error_clear();
    PyObject *py_obj = qd_entity_get_py(entity, attribute);
    char *str = py_string_2_c(py_obj);
    Py_XDECREF(py_obj);
    if (!str) qd_error_py();
    return str;
}

long qd_entity_get_long(qd_entity_t *entity, const char* attribute) {
    qd_error_clear();
    PyObject *py_obj = qd_entity_get_py(entity, attribute);
    if (py_obj && !PyLong_Check(py_obj)) {
        // 2.6 PyLong_AsLong fails to 'cast' non-long types
        // so we have to manually cast it first:
        PyObject *py_tmp = PyNumber_Long(py_obj);
        Py_XDECREF(py_obj);
        py_obj = py_tmp;
    }
    long result = py_obj ? PyLong_AsLong(py_obj) : -1;
    Py_XDECREF(py_obj);
    qd_error_py();
    return result;
}

bool qd_entity_get_bool(qd_entity_t *entity, const char* attribute) {
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
        return qd_entity_get_string(entity, attribute);
    else
        return default_value ? strdup(default_value) : NULL;
}

long qd_entity_opt_long(qd_entity_t *entity, const char* attribute, long default_value) {
    if (qd_entity_has(entity, attribute)) {
        long result = qd_entity_get_long(entity, attribute);
        if (!qd_error_code())
            return result;
    }
    return default_value;
}

bool qd_entity_opt_bool(qd_entity_t *entity, const char* attribute, bool default_value) {
    if (qd_entity_has(entity, attribute)) {
        bool result = qd_entity_get_bool(entity, attribute);
        if (!qd_error_code())
            return result;
    }
    return default_value;
}


pn_data_t *qd_entity_opt_map(qd_entity_t *entity, const char *attribute)
{
    if (!qd_entity_has(entity, attribute))
        return NULL;

    PyObject *py_obj = qd_entity_get_py(entity, attribute);
    assert(py_obj); // qd_entity_has() indicates py_obj != NULL

    if (!PyDict_Check(py_obj)) {
        qd_error(QD_ERROR_CONFIG, "Invalid type: map expected");
        Py_XDECREF(py_obj);
        return NULL;
    }

    pn_data_t *pn_map = pn_data(0);
    if (!pn_map) {
        qd_error(QD_ERROR_ALLOC, "Map allocation failure");
        Py_XDECREF(py_obj);
        return NULL;
    }

    qd_error_t rc = qd_py_to_pn_data(py_obj, pn_map);
    Py_XDECREF(py_obj);

    if (rc != QD_ERROR_NONE) {
        qd_error(QD_ERROR_ALLOC, "Failed to convert python map");
        pn_data_free(pn_map);
        return NULL;
    }

    return pn_map;
}


/**
 * Set a value for an entity attribute. If py_value == NULL then clear the attribute.
 * If the attribute exists and is a list, append this value to the list.
 *
 * NOTE: This function will Py_XDECREF(py_value).
 */
qd_error_t qd_entity_set_py(qd_entity_t* entity, const char* attribute, PyObject* py_value) {
    qd_error_clear();

    int result = 0;
    PyObject *py_key = PyUnicode_FromString(attribute);
    if (py_key) {
        if (py_value == NULL) {     /* Delete the attribute */
            result = PyObject_DelItem((PyObject*)entity, py_key);
            PyErr_Clear();          /* Ignore error if it isn't there. */
        }
        else {
            PyObject *old = PyObject_GetItem((PyObject*)entity, py_key);
            PyErr_Clear();          /* Ignore error if it isn't there. */
            if (old && PyList_Check(old)) /* Add to list */
                result = PyList_Append(old, py_value);
            else                    /* Set attribute */
                result = PyObject_SetItem((PyObject*)entity, py_key, py_value);
            Py_XDECREF(old);
        }
    }
    Py_XDECREF(py_key);
    Py_XDECREF(py_value);
    return (py_key == NULL || result < 0) ? qd_error_py() : QD_ERROR_NONE;
}

qd_error_t qd_entity_set_string(qd_entity_t *entity, const char* attribute, const char *value) {
    return qd_entity_set_py(entity, attribute, value ? PyUnicode_FromString(value) : 0);
}

qd_error_t qd_entity_set_longp(qd_entity_t *entity, const char* attribute, const long *value) {
    return qd_entity_set_py(entity, attribute, value ? PyLong_FromLong(*value) : 0);
}

qd_error_t qd_entity_set_boolp(qd_entity_t *entity, const char *attribute, const bool *value) {
    return qd_entity_set_py(entity, attribute, value ? PyBool_FromLong(*value) : 0);
}

qd_error_t qd_entity_set_long(qd_entity_t *entity, const char* attribute, long value) {
    return qd_entity_set_longp(entity, attribute, &value);
}

qd_error_t qd_entity_set_bool(qd_entity_t *entity, const char *attribute, bool value) {
    return qd_entity_set_boolp(entity, attribute, &value);
}

qd_error_t qd_entity_clear(qd_entity_t *entity, const char *attribute) {
    return qd_entity_set_py(entity, attribute, 0);
}

#define CHECK(err) if (err) return qd_error_code()

qd_error_t qd_entity_set_list(qd_entity_t *entity, const char *attribute) {
    CHECK(qd_entity_clear(entity, attribute));
    return qd_entity_set_py(entity, attribute, PyList_New(0));
}


qd_error_t qd_entity_set_map(qd_entity_t *entity, const char *attribute) {
    //CHECK(qd_entity_clear(entity, attribute));
    return qd_entity_set_py(entity, attribute, PyDict_New());
}

qd_error_t qd_entity_set_map_key_value_int(qd_entity_t *entity, const char *attribute, const char *key, int value)
{
    if (!key)
        return  QD_ERROR_VALUE;

    PyObject *py_key = PyUnicode_FromString(key);
    PyObject *py_value = PyLong_FromLong(value);
    PyObject *py_attribute = PyUnicode_FromString(attribute);

    qd_error_t ret = QD_ERROR_NONE;

    if (PyDict_Contains((PyObject*)entity, py_attribute) == 1) {
        PyObject* dict = PyDict_GetItem((PyObject*)entity, py_attribute);
        if (PyDict_SetItem(dict, py_key, py_value) < 0)
            ret = QD_ERROR_PYTHON;
    }
    else
        ret = QD_ERROR_VALUE;

    Py_XDECREF(py_key);
    Py_XDECREF(py_value);
    Py_XDECREF(py_attribute);

    return ret;
}

qd_error_t qd_entity_set_map_key_value_string(qd_entity_t *entity, const char *attribute, const char *key, const char *value)
{
    if (!key)
        return  QD_ERROR_VALUE;

    PyObject *py_key = PyUnicode_FromString(key);
    PyObject *py_value = PyUnicode_FromString(value);
    PyObject *py_attribute = PyUnicode_FromString(attribute);

    qd_error_t ret = QD_ERROR_NONE;

    if (PyDict_Contains((PyObject*)entity, py_attribute) == 1) {
        PyObject* dict = PyDict_GetItem((PyObject*)entity, py_attribute);
        if (PyDict_SetItem(dict, py_key, py_value) < 0)
            ret = QD_ERROR_PYTHON;
    }
    else
        ret = QD_ERROR_VALUE;

    Py_XDECREF(py_key);
    Py_XDECREF(py_value);
    Py_XDECREF(py_attribute);

    return ret;
}


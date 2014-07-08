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
#include "dispatch_private.h"
#include <qpid/dispatch/alloc.h>
#include <qpid/dispatch/log.h>

const char *MANAGEMENT_MODULE = "qpid_dispatch_internal.management";
const char *CONFIG_CLASS = "QdConfig";

static qd_log_source_t *log_source = 0;

struct qd_config_t {
    PyObject *pModule;
    PyObject *pClass;
    PyObject *pObject;
};

ALLOC_DECLARE(qd_config_t);
ALLOC_DEFINE(qd_config_t);

void qd_config_initialize(void)
{
    log_source = qd_log_source("CONFIG");
    qd_python_start();
}


void qd_config_finalize(void)
{
    qd_python_stop();
}


qd_config_t *qd_config(void)
{
    qd_error_clear();
    qd_config_t *config = new_qd_config_t();
    memset(config, 0, sizeof(*config));
    //
    // Load the Python management module and get a reference to the config class.
    //
    if ((config->pModule = PyImport_ImportModule(MANAGEMENT_MODULE)) &&
	(config->pClass = PyObject_GetAttrString(config->pModule, CONFIG_CLASS)) &&
	(config->pObject = PyObject_CallFunction(config->pClass, NULL)))
    {
	return config;
    } else {
	qd_error_py();		/* Log python error & set qd_error */
	qd_config_free(config);
	return 0;
    }
}


qd_error_t qd_config_read(qd_config_t *config, const char *filepath)
{
    qd_error_clear();
    PyObject *pMethod;
    PyObject *pPath;
    PyObject *pArgs;
    PyObject *pResult;

    assert(config);
    if (!config)
	return qd_error(QD_ERROR_CONFIG, "No configuration object");

    pMethod = PyObject_GetAttrString(config->pObject, "load");
    if (!pMethod || !PyCallable_Check(pMethod)) {
	Py_XDECREF(pMethod);
        qd_error_py();
        return qd_error(QD_ERROR_CONFIG, "No callable 'load'");
    }
    pArgs = PyTuple_New(1);
    pPath = PyString_FromString(filepath);
    PyTuple_SetItem(pArgs, 0, pPath);
    pResult = PyObject_CallObject(pMethod, pArgs);
    Py_DECREF(pArgs);
    if (pResult) {
        Py_DECREF(pResult);
    } else {
        return qd_error_py();
    }
    Py_DECREF(pMethod);
    return QD_ERROR_NONE;
}


void qd_config_free(qd_config_t *config)
{
    if (config) {
        Py_XDECREF(config->pClass);
        Py_XDECREF(config->pModule);
        Py_XDECREF(config->pObject);
        free_qd_config_t(config);
    }
}


int qd_config_item_count(const qd_dispatch_t *dispatch, const char *section)
{
    PyErr_Clear();
    PyObject *result =
	PyObject_CallMethod(dispatch->config->pObject, "section_count", "(s)", section);
    if (qd_error_py()) return -1;
    long count = PyInt_AsLong(result);
    if (qd_error_py()) return -1;
    Py_DECREF(result);
    return count;
}


static PyObject *item_value(const qd_dispatch_t *dispatch, const char *section, int index, const char* key)
{
    return PyObject_CallMethod(dispatch->config->pObject, "value", "(sis)", section, index, key);
}


bool qd_config_item_exists(const qd_dispatch_t *dispatch, const char *section, int index, const char* key)
{
    PyObject *value = item_value(dispatch, section, index, key);
    if (!value) qd_error_py();
    bool exists = value && (value != Py_None);
    Py_XDECREF(value);
    return exists;
}

char *qd_config_item_value_string(const qd_dispatch_t *dispatch, const char *section, int index, const char* key)
{
    PyObject *value = item_value(dispatch, section, index, key);
    if (value && value != Py_None) {
	PyObject *value_str = PyObject_Str(value);
	Py_DECREF(value);
	if (value_str) {
	    char* result = strdup(PyString_AsString(value_str));
	    Py_DECREF(value_str);
	    return result;
	}
    }
    qd_error_py();
    return 0;
}


uint32_t qd_config_item_value_int(const qd_dispatch_t *dispatch, const char *section, int index, const char* key)
{
    PyObject *value = item_value(dispatch, section, index, key);
    if (value && value != Py_None) {
	long result = PyLong_AsLong(value);
	Py_DECREF(value);
	return result;
    }
    qd_error_py();
    return 0;
}


int qd_config_item_value_bool(const qd_dispatch_t *dispatch, const char *section, int index, const char* key)
{
    PyObject *value = item_value(dispatch, section, index, key);
    if (value) {
	bool result = PyObject_IsTrue(value);
        Py_DECREF(value);
	return result;
    }
    qd_error_py();
    return 0;
}

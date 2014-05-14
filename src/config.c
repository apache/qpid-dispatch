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

#define PYTHON_MODULE "qpid_dispatch_internal.config"

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
    qd_config_t *config = new_qd_config_t();

    //
    // Load the Python configuration module and get a reference to the config class.
    //
    PyObject *pName = PyString_FromString(PYTHON_MODULE);
    config->pModule = PyImport_Import(pName);
    Py_DECREF(pName);

    if (!config->pModule) {
        PyErr_Print();
        free_qd_config_t(config);
        qd_log(log_source, QD_LOG_ERROR, "Unable to load configuration module: %s", PYTHON_MODULE);
        return 0;
    }

    config->pClass = PyObject_GetAttrString(config->pModule, "DispatchConfig");
    if (!config->pClass || !PyClass_Check(config->pClass)) {
        PyErr_Print();
        Py_DECREF(config->pModule);
        free_qd_config_t(config);
        qd_log(log_source, QD_LOG_ERROR, "Problem with configuration module: Missing DispatchConfig class");
        return 0;
    }

    //
    // Instantiate the DispatchConfig class
    //
    PyObject *pArgs = PyTuple_New(0);
    config->pObject = PyInstance_New(config->pClass, pArgs, 0);
    Py_DECREF(pArgs);

    if (config->pObject == 0) {
        PyErr_Print();
        Py_DECREF(config->pModule);
        free_qd_config_t(config);
        return 0;
    }

    return config;
}


void qd_config_read(qd_config_t *config, const char *filepath)
{
    PyObject *pMethod;
    PyObject *pPath;
    PyObject *pArgs;
    PyObject *pResult;

    if (!config)
        return;

    pMethod = PyObject_GetAttrString(config->pObject, "read_file");
    if (!pMethod || !PyCallable_Check(pMethod)) {
        qd_log(log_source, QD_LOG_ERROR, "Problem with configuration module: No callable 'read_file'");
        if (pMethod) {
            Py_DECREF(pMethod);
        }
        return;
    }

    pArgs = PyTuple_New(1);
    pPath = PyString_FromString(filepath);
    PyTuple_SetItem(pArgs, 0, pPath);
    pResult = PyObject_CallObject(pMethod, pArgs);
    Py_DECREF(pArgs);
    if (pResult) {
        Py_DECREF(pResult);
    } else {
#ifndef NDEBUG
        PyErr_Print();
#endif
        qd_log(log_source, QD_LOG_CRITICAL, "Configuration Failed, Exiting");
        exit(1);
    }
    Py_DECREF(pMethod);
}


void qd_config_extend(qd_config_t *config, const char *text)
{
    PyRun_SimpleString(text);
}


void qd_config_free(qd_config_t *config)
{
    if (config) {
        Py_DECREF(config->pClass);
        Py_DECREF(config->pModule);
        free_qd_config_t(config);
    }
}


int qd_config_item_count(const qd_dispatch_t *dispatch, const char *section)
{
    const qd_config_t *config = dispatch->config;
    PyObject *pSection;
    PyObject *pMethod;
    PyObject *pArgs;
    PyObject *pResult;
    int       result = 0;

    if (!config)
        return 0;

    pMethod = PyObject_GetAttrString(config->pObject, "item_count");
    if (!pMethod || !PyCallable_Check(pMethod)) {
        qd_log(log_source, QD_LOG_ERROR, "Problem with configuration module: No callable 'item_count'");
        if (pMethod) {
            Py_DECREF(pMethod);
        }
        return 0;
    }

    pSection = PyString_FromString(section);
    pArgs    = PyTuple_New(1);
    PyTuple_SetItem(pArgs, 0, pSection);
    pResult = PyObject_CallObject(pMethod, pArgs);
    Py_DECREF(pArgs);
    if (pResult && PyInt_Check(pResult))
        result = (int) PyInt_AsLong(pResult);
    if (pResult) {
        Py_DECREF(pResult);
    }
    Py_DECREF(pMethod);

    return result;
}


static PyObject *item_value(const qd_dispatch_t *dispatch, const char *section, int index, const char* key, const char* method)
{
    const qd_config_t *config = dispatch->config;
    PyObject *pSection;
    PyObject *pIndex;
    PyObject *pKey;
    PyObject *pMethod;
    PyObject *pArgs;
    PyObject *pResult;

    if (!config)
        return 0;

    pMethod = PyObject_GetAttrString(config->pObject, method);
    if (!pMethod || !PyCallable_Check(pMethod)) {
        qd_log(log_source, QD_LOG_ERROR, "Problem with configuration module: No callable '%s'", method);
        if (pMethod) {
            Py_DECREF(pMethod);
        }
        return 0;
    }

    pSection = PyString_FromString(section);
    pIndex   = PyInt_FromLong((long) index);
    pKey     = PyString_FromString(key);
    pArgs    = PyTuple_New(3);
    PyTuple_SetItem(pArgs, 0, pSection);
    PyTuple_SetItem(pArgs, 1, pIndex);
    PyTuple_SetItem(pArgs, 2, pKey);
    pResult = PyObject_CallObject(pMethod, pArgs);
    Py_DECREF(pArgs);
    Py_DECREF(pMethod);

    return pResult;
}


bool qd_config_item_exists(const qd_dispatch_t *dispatch, const char *section, int index, const char* key)
{
    PyObject *pResult = item_value(dispatch, section, index, key, "value_string");
    bool exists = pResult;
    if (pResult) {
        Py_DECREF(pResult);
    }
    return exists;
}

const char *qd_config_item_value_string(const qd_dispatch_t *dispatch, const char *section, int index, const char* key)
{
    PyObject *pResult = item_value(dispatch, section, index, key, "value_string");
    char     *value   = 0;

    if (pResult && PyString_Check(pResult)) {
        Py_ssize_t size = PyString_Size(pResult);
        value = (char*) malloc(size + 1);
        strncpy(value, PyString_AsString(pResult), size + 1);
    }

    if (pResult) {
        Py_DECREF(pResult);
    }

    return value;
}


uint32_t qd_config_item_value_int(const qd_dispatch_t *dispatch, const char *section, int index, const char* key)
{
    PyObject *pResult = item_value(dispatch, section, index, key, "value_int");
    uint32_t  value   = 0;

    if (pResult && PyLong_Check(pResult))
        value = (uint32_t) PyLong_AsLong(pResult);

    if (pResult) {
        Py_DECREF(pResult);
    }

    return value;
}


int qd_config_item_value_bool(const qd_dispatch_t *dispatch, const char *section, int index, const char* key)
{
    PyObject *pResult = item_value(dispatch, section, index, key, "value_bool");
    int       value   = 0;

    if (pResult && pResult != Py_None)
        value = 1;

    if (pResult) {
        Py_DECREF(pResult);
    }

    return value;
}


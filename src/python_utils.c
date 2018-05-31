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


// Convert a Python string type object to a C null terminated string.  Caller
// must free the returned string buffer.  Returns NULL if python object is not
// a string-ish type to start with (i.e. expects string type, error if not)
char *py_string_2_c(PyObject *py_str)
{
    char *str = NULL;
    if (!py_str) return NULL;

    if (PyUnicode_Check(py_str)) {
        // python 3 str OR python 2 unicode type
        PyObject *ref = PyUnicode_AsUTF8String(py_str);
        if (ref) {
            // now a bytes object
            str = strdup(PyBytes_AS_STRING(ref));
            Py_DECREF(ref);
        }
    } else if (PyBytes_Check(py_str)) {
        // python 2 str
        str = strdup(PyBytes_AS_STRING(py_str));
    }
    return str;
}

// Convert the string representation of an arbitrary Python type object to a
// null terminated C string.  Equivalent to calling 'str(o)' in Python.  Caller
// must free the returned string buffer.
char *py_obj_2_c_string(PyObject *py_obj)
{
    char *str = NULL;
    PyObject *tmp = NULL;

    if (!py_obj) return NULL;

    // first convert to a python string object
    if (PyUnicode_Check(py_obj) || PyBytes_Check(py_obj)) {
        // A python string type - no need to call str(py_obj)
        tmp = py_obj;
        Py_INCREF(tmp);  // for decref below
    } else {
        // create a new object via str(py_obj);
        tmp = PyObject_Str(py_obj);
    }
    str = py_string_2_c(tmp);
    Py_XDECREF(tmp);
    return str;
}


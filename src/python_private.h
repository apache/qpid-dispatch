#ifndef __python_internal_h__
#define __python_internal_h__ 1
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

#include "qpid/dispatch/message.h"

#include <stdint.h>

#if PY_MAJOR_VERSION <= 2
// deal with the two integer types in Python2
#define QD_PY_INT_CHECK(PO) (PyInt_Check(PO) || PyLong_Check(PO))
#define QD_PY_INT_2_INT64(PO) (PyLong_Check(PO) ? \
                               (int64_t) PyLong_AsLongLong(PO) : \
                               (int64_t) PyInt_AS_LONG(PO))
#else  // Python3
#define QD_PY_INT_CHECK(PO) (PyLong_Check(PO))
#define QD_PY_INT_2_INT64(PO) ((int64_t)PyLong_AsLongLong(PO))
#endif

// Convert a Python string type to a C string.  The resulting string may be
// UTF-8 encoded.  Caller must free returned string buffer.  Returns NULL on
// failure
char *py_string_2_c(PyObject *py_str);

// Convert the string representation of an arbitrary Python type object to a
// null terminated C string.  Equivalent to calling 'str(o)' in Python.  The
// resulting string may be UTF-8 encoded. Caller must free the returned string
// buffer.
char *py_obj_2_c_string(PyObject *py_obj);

void qd_json_msgs_init(PyObject **msgs);
void qd_json_msgs_done(PyObject *msgs);
void qd_json_msgs_append(PyObject *msgs, qd_message_t *msg);
char *qd_json_msgs_string(PyObject *msgs);

#endif

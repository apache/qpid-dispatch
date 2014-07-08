#ifndef __dispatch_error_h__
#define __dispatch_error_h__ 1
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

/** @file
 * Thread-safe error handling mechansim for dispatch.
 *
 * Functions that detect an error should return qd_error_t.
 *
 * Implementation of such functions should first call qd_error_clear()
 * to clear any previous error and on detecting an error do:
 *     return qd_error(code, "printf format", ...)
 *
 * The caller can check the error code and message with
 * qd_error_code() and qd_error_message().
 *
 *
 */
// NOTE: If you modify this enum, you must update error_names in error.c
typedef enum {
    QD_ERROR_NONE = 0,
    QD_ERROR_NOT_FOUND,
    QD_ERROR_ALREADY_EXISTS,
    QD_ERROR_ALLOC,
    QD_ERROR_MESSAGE,           ///< Error parsing a message.
    QD_ERROR_PYTHON,            ///< Error from python code.
    QD_ERROR_CONFIG,            ///< Error in configuration

    QD_ERROR_COUNT              ///< Not an error, marks the end of the enum
} qd_error_t;

/**
 * Store thread-local error code and message.
 *@param code Error code. If 0 this is equivalent to calling qd_error_clear()
 *@param fmt printf-stlye format.
 *@return code
 */
qd_error_t qd_error(qd_error_t code, const char *fmt, ...);

/**
 * Clear thread-local error code and message.
 */
void qd_error_clear();

/**
 * @return Thread local error message. Includes text for error code.
 */
const char* qd_error_message();

/**
 *@return Thread local error code
 */
qd_error_t qd_error_code();

/** Maximum length of a qd_error_message, useful for temporary buffers. */
extern const int QD_ERROR_MAX;

/**
 * Check for a python error.
 *
 * If there is a python error, call qd_error(QD_ERROR_PYTHON, <python-msg>),
 * clear the python error indicator, log the python error and stack trace and
 * return QD_ERROR_PYTHON.
 *
 * If there is no python error, call qd_error_clear() and return QD_ERROR_NONE.
 *
 * @return QD_ERROR_PYTHON or QD_ERROR_NONE.
 */
qd_error_t qd_error_py();

#endif

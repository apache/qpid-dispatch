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

#include "qpid/dispatch/enum.h"

#include <qpid/dispatch/internal/export.h>

#include <stdarg.h>

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
    QD_ERROR_NONE,
    QD_ERROR_NOT_FOUND,
    QD_ERROR_ALREADY_EXISTS,
    QD_ERROR_ALLOC,
    QD_ERROR_MESSAGE,           ///< Error parsing a message.
    QD_ERROR_PYTHON,            ///< Error from python code.
    QD_ERROR_CONFIG,            ///< Error in configuration
    QD_ERROR_TYPE,              ///< Value of inappropriate type.
    QD_ERROR_VALUE,             ///< Invalid value.
    QD_ERROR_RUNTIME,           ///< Run-time failure.
    QD_ERROR_SYSTEM             ///< System error from errno
} qd_error_t;
ENUM_DECLARE(qd_error);

/**
 * Store thread-local error code and message.
 *@param code Error code. If 0 this is equivalent to calling qd_error_clear()
 *@param ... printf-stlye format and arguments.
 *@return code
 */
#define qd_error(code, ...) qd_error_impl(code, __FILE__, __LINE__, __VA_ARGS__)

/**
 * Like qd_error but takes a va_list of format arguments
 */
#define qd_verror(code, fmt, ap) qd_error_vimpl(code, __FILE__, __LINE__, fmt, ap)

qd_error_t qd_error_impl(qd_error_t code, const char *file, int line, const char *fmt, ...);
qd_error_t qd_error_vimpl(qd_error_t code, const char *file, int line, const char *fmt, va_list ap);

/**
 * Clear thread-local error code and message.
 *@return QD_ERROR_NONE
 */
qd_error_t qd_error_clear();

/**
 * @return Thread local error message. Includes text for error code.
 */
QD_EXPORT const char* qd_error_message();

/**
 *@return Thread local error code
 */
QD_EXPORT qd_error_t qd_error_code();

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
#define qd_error_py() qd_error_py_impl(__FILE__, __LINE__)

qd_error_t qd_error_py_impl(const char *file, int line);

#define QD_ERROR_RET() do { if (qd_error_code()) return qd_error_code(); } while(0)
#define QD_ERROR_BREAK() if (qd_error_code()) break;
#define QD_ERROR_PY_RET() do { if (qd_error_py()) return qd_error_code(); } while(0)

/**
 * Check for an errno error.
 *
 * If errnum is non-0, set error code QD_ERROR_SYSTEM with a message including the errno text.
 * Otherwise, call qd_error_clear() and return QD_ERROR_NONE.
 */
#define qd_error_errno(errnum, ...) qd_error_errno_impl(errnum, __FILE__, __LINE__, __VA_ARGS__)

qd_error_t qd_error_errno_impl(int errnum, const char *file, int line, const char *fmt, ...);

#endif

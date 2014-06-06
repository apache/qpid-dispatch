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
 "Invalid message"
};

STATIC_ASSERT(sizeof(error_names)/sizeof(error_names[0]) == QD_ERROR_COUNT, error_names_wrong_size);

#define ERROR_MAX QD_LOG_TEXT_MAX
const int QD_ERROR_MAX = ERROR_MAX;
static __thread char error_message[ERROR_MAX];
static __thread qd_error_t error_code = 0;
static qd_log_source_t* log_source = 0;

void qd_error_initialize() {
    log_source = qd_log_source("ERROR");
}

qd_error_t qd_error(qd_error_t code, const char *fmt, ...) {
    error_code = code;
    if (code) {
	int i = 0;
	if (code < QD_ERROR_COUNT)
	    i = snprintf(error_message, ERROR_MAX,"%s: ", error_names[code]);
	else
	    i = snprintf(error_message, ERROR_MAX, "%d: ", code);
	va_list arglist;
	va_start(arglist, fmt);
	vsnprintf(error_message+i, ERROR_MAX-i, fmt, arglist);
	va_end(arglist);
	qd_log(log_source, QD_LOG_TRACE, "%s", qd_error_message());
	return code;
    }
    else
	qd_error_clear();
    return 0;
}

void qd_error_clear() {
    error_code = 0;
    error_message[0] = '\0';
}

const char* qd_error_message() {
    return error_message;
}

qd_error_t qd_error_code() {
    return error_code;
}

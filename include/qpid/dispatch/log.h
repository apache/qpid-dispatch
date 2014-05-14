#ifndef __dispatch_log_h__
#define __dispatch_log_h__ 1
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
#include <stdbool.h>

/** Logging levels */
typedef enum {
    QD_LOG_NONE     =0x00000000,
    QD_LOG_TRACE    =0x00000001,
    QD_LOG_DEBUG    =0x00000002,
    QD_LOG_INFO     =0x00000004,
    QD_LOG_NOTICE   =0x00000008,
    QD_LOG_WARNING  =0x00000010,
    QD_LOG_ERROR    =0x00000020,
    QD_LOG_CRITICAL =0x00000040,
} qd_log_level_t;

typedef struct qd_log_source_t qd_log_source_t;

qd_log_source_t* qd_log_source(const char *module);

/**@internal*/
bool qd_log_enabled(qd_log_source_t *source, int level);
/**@internal*/
void qd_log_impl(qd_log_source_t *source, int level, const char *file, int line, const char *fmt, ...);

/** Log a message
 * Note: does not evaluate the format args unless the log message is enabled.
 * @param s qd_log_source_t* source of log message.
 * @param c qd_log_level_t log level of message
 * @param f printf style format string ...
 */
#define qd_log(s, c, f, ...) \
    do { if (qd_log_enabled(s,c)) qd_log_impl(s, c, __FILE__, __LINE__, f , ##__VA_ARGS__); } while(0)

/** Set the mask to enable log levels, see qd_log_level_t */
void qd_log_set_mask(int mask);

#endif

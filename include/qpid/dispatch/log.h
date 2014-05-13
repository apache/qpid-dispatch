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

#define QD_LOG_NONE     0x00000000
#define QD_LOG_TRACE    0x00000001
#define QD_LOG_DEBUG    0x00000002
#define QD_LOG_INFO     0x00000004
#define QD_LOG_NOTICE   0x00000008
#define QD_LOG_WARNING  0x00000010
#define QD_LOG_ERROR    0x00000020
#define QD_LOG_CRITICAL 0x00000040

typedef struct qd_log_source_t qd_log_source_t;

qd_log_source_t* qd_log_source(const char *module);

void qd_log_impl(qd_log_source_t *source, int level, const char *file, int line, const char *fmt, ...);
#define qd_log(s, c, f, ...) qd_log_impl(s, c, __FILE__, __LINE__, f , ##__VA_ARGS__)

#endif

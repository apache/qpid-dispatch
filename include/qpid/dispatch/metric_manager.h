#ifndef __dispatch_metric_manager_h__
#define __dispatch_metric_manager_h__ 1
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

#include <qpid/dispatch/metric.h>
#include <qpid/dispatch/buffer.h>

/**@file
 * Server Metric Manager
 * 
 * @defgroup metric metric_manager
 *
 * Server Metric Manager Functions
 * @{
 */
typedef struct qd_metric_manager_t qd_metric_manager_t;

qd_metric_manager_t *qd_metric_manager();
void qd_metric_manager_free(qd_metric_manager_t *manager);
void qd_metric_manager_export(qd_metric_manager_t *manager, qd_buffer_list_t *buffers);
qd_metric_t *qd_metric_manager_register(qd_metric_manager_t *manager, const char *name, const char *desc, qd_metric_type_t type);

/**
 * @}
 */

#endif

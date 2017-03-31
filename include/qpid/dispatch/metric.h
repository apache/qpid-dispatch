#ifndef __dispatch_metric_h__
#define __dispatch_metric_h__ 1
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

#include <stdint.h>

/**@file
 * Server Metric Functions
 * 
 * @defgroup metric metric
 *
 * Server Metric Functions
 * @{
 */

typedef struct qd_metric_t           qd_metric_t;
typedef struct qd_metric_counter_t   qd_metric_counter_t;
typedef struct qd_metric_gauge_t     qd_metric_gauge_t;

typedef enum {
    QD_METRIC_TYPE_GAUGE = 1,
    QD_METRIC_TYPE_COUNTER
} qd_metric_type_t;

struct qd_metric_t {
    const char * name;
    const char * description;
    qd_metric_type_t type;
    double value;
};

#define QD_METRIC_INIT(parent, n, d, t) do { \
    (parent)->n.name = #n;                   \
    (parent)->n.description = (d);           \
    (parent)->n.type = (t);                  \
    (parent)->n.value = 0;                   \
} while (0)

#define QD_METRIC_INC(m)      (m)->value++
#define QD_METRIC_INC_N(m, n) (m)->value += (n)
#define QD_METRIC_DEC(m)      (m)->value--
#define QD_METRIC_DEC_N(m, n)   (m)->value -= (n)
#define QD_METRIC_SET(m, n)   (m)->value = (n)

/**
 * @}
 */

#endif

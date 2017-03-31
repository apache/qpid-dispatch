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

/**@file
 * Server Metric Functions
 * 
 * @defgroup metric metric
 *
 * Server Metric Functions
 * @{
 */

typedef struct qd_metric_t            qd_metric_t;
typedef struct qd_metric_labelset_t   qd_metric_labelset_t;

typedef enum {
    QD_METRIC_TYPE_GAUGE = 1,
    QD_METRIC_TYPE_COUNTER
} qd_metric_type_t;

qd_metric_t * qd_metric(const char * name, const char * description, qd_metric_type_t type);
void          qd_metric_inc(qd_metric_t *, qd_metric_labelset_t * labels, double increment);
void          qd_metric_dec(qd_metric_t *, qd_metric_labelset_t * labels, double decrement);
void          qd_metric_set(qd_metric_t *, qd_metric_labelset_t * labels, double value);

#define QD_METRIC_INC(m)      qd_metric_inc((m), NULL, 1)
#define QD_METRIC_INC_N(m, n) qd_metric_inc((m), NULL, (n))
#define QD_METRIC_DEC(m)      qd_metric_dec((m), NULL, 1)
#define QD_METRIC_DEC_N(m, n) qd_metric_dec((m), NULL, (n))
#define QD_METRIC_SET(m, n)   qd_metric_set((m), NULL, (n))

/**
 * @}
 */

#endif

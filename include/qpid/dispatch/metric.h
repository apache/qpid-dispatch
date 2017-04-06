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

typedef struct qd_metric_t         qd_metric_t;
typedef struct qd_metric_label_t   qd_metric_label_t;

typedef enum {
    QD_METRIC_TYPE_GAUGE = 1,
    QD_METRIC_TYPE_COUNTER
} qd_metric_type_t;

struct qd_metric_label_t {
    const char * key;
    const char * value;
};

qd_metric_t * qd_metric(const char *name, const char *description, qd_metric_type_t type);
void          qd_metric_free(qd_metric_t *metric);
void          qd_metric_inc(qd_metric_t *metric, double increment, const qd_metric_label_t labels[], unsigned int num_labels);
void          qd_metric_dec(qd_metric_t *metric, double decrement, const qd_metric_label_t labels[], unsigned int num_labels);
void          qd_metric_set(qd_metric_t *metric, double value, const qd_metric_label_t labels[], unsigned int num_labels);

#define QD_METRIC_INC(m)      qd_metric_inc((m), 1, NULL, 0)
#define QD_METRIC_INC_N(m, n) qd_metric_inc((m), (n), NULL, 0)
#define QD_METRIC_INC_L1_N(m, n, label_key, label_value) do { \
    qd_metric_label_t labels[] = {                          \
        {                                                   \
            .key = (label_key),                             \
            .value = (label_value)                          \
        }                                                   \
    };                                                      \
    qd_metric_inc((m), (n), labels, 1);                     \
} while (0)
#define QD_METRIC_INC_L1(m, label_key, label_value) QD_METRIC_INC_L1_N(m, 1, label_key, label_value)

#define QD_METRIC_DEC(m)      qd_metric_dec((m), 1, NULL, 0)
#define QD_METRIC_DEC_N(m, n) qd_metric_dec((m), (n), NULL, 0)
#define QD_METRIC_DEC_L1_N(m, n, label_key, label_value) do { \
    qd_metric_label_t labels[] = {                          \
        {                                                   \
            .key = (label_key),                             \
            .value = (label_value)                          \
        }                                                   \
    };                                                      \
    qd_metric_dec((m), (n), labels, 1);                     \
} while (0)

#define QD_METRIC_DEC_L1(m, label_key, label_value) QD_METRIC_DEC_L1_N(m, 1, label_key, label_value)

#define QD_METRIC_SET(m, n)   qd_metric_set((m), (n), NULL, 0)

/**
 * @}
 */

#endif

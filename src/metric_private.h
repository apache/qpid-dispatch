#ifndef __metric_private_h__
#define __metric_private_h__ 1
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
#include <qpid/dispatch/ctools.h>

typedef struct qd_metric_value_t qd_metric_value_t;

// TODO: Hash labels for quick lookup
struct qd_metric_value_t {
    DEQ_LINKS(qd_metric_value_t);
    qd_metric_label_t *labels;
    double value;
};

DEQ_DECLARE(qd_metric_value_t, qd_metric_value_list_t);

struct qd_metric_t {
    const char                 *name;
    const char                 *description;
    qd_metric_type_t            type;
    qd_metric_value_list_t      values;
};

#endif

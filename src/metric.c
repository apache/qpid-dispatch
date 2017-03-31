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

#include "metric_private.h"
#include "alloc.h"

ALLOC_DECLARE(qd_metric_t);
ALLOC_DEFINE(qd_metric_t);

qd_metric_t *
qd_metric(const char *name, const char *description, qd_metric_type_t type)
{
    qd_metric_t *metric = new_qd_metric_t();
    if (!metric)
        return 0;

    metric->name = name;
    metric->description = description;
    metric->type = type;
    DEQ_INIT(metric->values);

    return metric;
}

void
qd_metric_inc(qd_metric_t *metric, double increment, qd_metric_label_t labels[], unsigned int num_labels)
{
}

void
qd_metric_dec(qd_metric_t *metric, double decrement, qd_metric_label_t labels[], unsigned int num_labels)
{
}

void
qd_metric_set(qd_metric_t *metric, double value, qd_metric_label_t labels[], unsigned int num_labels)
{
}

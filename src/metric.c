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
#include <stdio.h>
#include <qpid/dispatch/buffer.h>

ALLOC_DECLARE(qd_metric_t);
ALLOC_DEFINE(qd_metric_t);
ALLOC_DECLARE(qd_metric_value_t);
ALLOC_DEFINE(qd_metric_value_t);

#define MIN(a, b) (a) < (b) ? (a) : (b)

static void
qd_metric_insert(qd_metric_t *metric, double initial_value, const qd_metric_label_t labels[], unsigned int num_labels)
{
    // Add new metric
    qd_metric_value_t *value = new_qd_metric_value_t();
    DEQ_ITEM_INIT(value);
    value->value = initial_value;
    value->num_labels = num_labels;
    value->labels = malloc(sizeof(qd_metric_label_t) * num_labels);
    memset(value->labels, 0, sizeof(qd_metric_label_t) * num_labels);
    for (int i = 0; i < num_labels; i++) {
        value->labels[i].key = strdup(labels[i].key);
        value->labels[i].value = strdup(labels[i].value);
    }
    DEQ_INSERT_TAIL(metric->values, value);
}

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
    DEQ_ITEM_INIT(metric);

    return metric;
}

void
qd_metric_free(qd_metric_t *metric)
{
    if (!metric) return;

    qd_metric_value_t * value = DEQ_HEAD(metric->values);
    while (value != NULL) {
        for (int i = 0; i < value->num_labels; i++) {
            // Casting is OK, we have allocated them
            free((char *)value->labels[i].key);
            free((char *)value->labels[i].value);
        }
        if (value->num_labels > 0) {
            free(value->labels);
        }
        DEQ_REMOVE_HEAD(metric->values);
        free_qd_metric_value_t(value);
        value = DEQ_HEAD(metric->values);
    }
    free_qd_metric_t(metric);
}

static int
qd_metric_label_cmp(const qd_metric_label_t *la, const qd_metric_label_t *lb, unsigned int num_labels)
{
    int retval = 0;
    for (int i = 0; i < num_labels; i++) {
        retval = strncmp(la[i].key, lb[i].key, MIN(strlen(la[i].key), strlen(lb[i].key)));
        if (retval != 0) {
            return retval;
        }

        retval = strncmp(la[i].value, lb[i].value, MIN(strlen(la[i].value), strlen(lb[i].value)));
        if (retval != 0) {
            return retval;
        }
    }
    return 0;
}

void
qd_metric_inc(qd_metric_t *metric, double increment, const qd_metric_label_t labels[], unsigned int num_labels)
{
    qd_metric_value_t * value = DEQ_HEAD(metric->values);
    while (value != NULL) {
        if (value->num_labels == num_labels && qd_metric_label_cmp(value->labels, labels, num_labels) == 0) {
            value->value += increment;
            return;
        }
        value = DEQ_NEXT(value);
    }

    qd_metric_insert(metric, increment, labels, num_labels);
}

void
qd_metric_dec(qd_metric_t *metric, double decrement, const qd_metric_label_t labels[], unsigned int num_labels)
{
    qd_metric_value_t * value = DEQ_HEAD(metric->values);
    while (value != NULL) {
        if (value->num_labels == num_labels && qd_metric_label_cmp(value->labels, labels, num_labels) == 0) {
            value->value -= decrement;
            return;
        }
        value = DEQ_NEXT(value);
    }
    qd_metric_insert(metric, decrement, labels, num_labels);
}

void
qd_metric_set(qd_metric_t *metric, double numeric_value, const qd_metric_label_t labels[], unsigned int num_labels)
{
    qd_metric_value_t * value = DEQ_HEAD(metric->values);
    while (value != NULL) {
        if (value->num_labels == num_labels && qd_metric_label_cmp(value->labels, labels, num_labels) == 0) {
            value->value = numeric_value;
            return;
        }
        value = DEQ_NEXT(value);
    }
    qd_metric_insert(metric, numeric_value, labels, num_labels);
}

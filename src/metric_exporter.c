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

#include "metric_exporter_private.h"
#include <stdio.h>

#define MIN(a, b) (a) < (b) ? (a) : (b)

#if 0
static void
write_string(qd_buffer_list_t *buffers, const char *str, unsigned long long len)
{
    qd_buffer_t * buf = DEQ_TAIL(*buffers);
    while (len > 0) {
        if (buf == NULL) {
            buf = qd_buffer();
            DEQ_INSERT_TAIL(*buffers, buf);
        }
        unsigned char * p = qd_buffer_cursor(buf);
        unsigned long long to_copy = MIN(len, qd_buffer_capacity(buf));
        memcpy(p, str, to_copy);
        qd_buffer_insert(buf, to_copy);
        str += to_copy;
        len -= to_copy;
        if (len > 0) {
            buf = qd_buffer();
            DEQ_INSERT_TAIL(*buffers, buf);
        }
    }
}

typedef enum {
    METRIC_TYPE_GAUGE = 1,
    METRIC_TYPE_COUNTER
} metric_type_t;

static const char *
type_to_string(metric_type_t type)
{
    switch (type) {
    case METRIC_TYPE_GAUGE:
        return "gauge";
    case METRIC_TYPE_COUNTER:
        return "counter";
    default:
        return "unknown";
    }
}

static void
metric_write(qd_metric_t *metric, qd_buffer_list_t *buffers)
{
    qd_metric_value_t * value = DEQ_HEAD(metric->values);

    char buf[256];
    snprintf(buf, sizeof(buf), "# HELP %s %s\n", metric->name, metric->description);
    write_string(buffers, buf, strlen(buf));
    snprintf(buf, sizeof(buf), "# TYPE %s %s\n", metric->name, type_to_string(metric->type));
    write_string(buffers, buf, strlen(buf));

    while (value != NULL) {
        write_string(buffers, metric->name, strlen(metric->name));
        if (value->num_labels >= 1 && value->labels[0].key != NULL) {
            write_string(buffers, "{", 1);
            for (int i = 0; i < value->num_labels; i++) {
                write_string(buffers, value->labels[i].key, strlen(value->labels[i].key));
                write_string(buffers, "=\"", 2);
                write_string(buffers, value->labels[i].value, strlen(value->labels[i].value));
                write_string(buffers, "\"", 1);
                if (i < value->num_labels - 1) {
                    write_string(buffers, ",", 1);
                }
            }
            write_string(buffers, "}", 1);
        }
        write_string(buffers, " ", 1);

        snprintf(buf, sizeof(buf), "%f\n", value->value);
        write_string(buffers, buf, strlen(buf));
        value = DEQ_NEXT(value);
    }
}
#endif

void
metric_export_prometheus(qd_dispatch_t *dispatch, qd_buffer_list_t *buffers)
{
#if 0
    qdr_manage_handler(core, qd_manage_response_handler);
    ctx->query = qdr_manage_query(core, ctx, entity_type, attribute_names_parsed_field, field);
#endif
}

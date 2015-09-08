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

#include <stdio.h>

#include <qpid/dispatch/amqp.h>

#include "annotation_private.h"
#include "message_private.h"

// create a buffer chain holding the outgoing message annotations section
bool compose_message_annotations(qd_message_pvt_t *msg, qd_buffer_list_t *out)
{
    if (!DEQ_IS_EMPTY(msg->ma_to_override) ||
        !DEQ_IS_EMPTY(msg->ma_trace) ||
        !DEQ_IS_EMPTY(msg->ma_ingress)) {

        qd_composed_field_t *out_ma = qd_compose(QD_PERFORMATIVE_MESSAGE_ANNOTATIONS, 0);
        qd_compose_start_map(out_ma);

        if (!DEQ_IS_EMPTY(msg->ma_to_override)) {
            qd_compose_insert_symbol(out_ma, QD_MA_TO);
            qd_compose_insert_buffers(out_ma, &msg->ma_to_override);
        }

        if (!DEQ_IS_EMPTY(msg->ma_trace)) {
            qd_compose_insert_symbol(out_ma, QD_MA_TRACE);
            qd_compose_insert_buffers(out_ma, &msg->ma_trace);
        }

        if (!DEQ_IS_EMPTY(msg->ma_ingress)) {
            qd_compose_insert_symbol(out_ma, QD_MA_INGRESS);
            qd_compose_insert_buffers(out_ma, &msg->ma_ingress);
        }

        qd_compose_end_map(out_ma);

        qd_compose_take_buffers(out_ma, out);
        qd_compose_free(out_ma);
        return true;
    }

    return false;
}

qd_parsed_field_t *qd_message_message_annotations(qd_message_t *in_msg)
{
    qd_message_pvt_t     *msg     = (qd_message_pvt_t*) in_msg;
    qd_message_content_t *content = msg->content;

    if (content->parsed_message_annotations)
        return content->parsed_message_annotations;

    qd_field_iterator_t *ma = qd_message_field_iterator(in_msg, QD_FIELD_MESSAGE_ANNOTATION);
    if (ma == 0)
        return 0;

    content->parsed_message_annotations = qd_parse(ma);
    if (content->parsed_message_annotations == 0 ||
        !qd_parse_ok(content->parsed_message_annotations) ||
        !qd_parse_is_map(content->parsed_message_annotations)) {
        qd_field_iterator_free(ma);
        qd_parse_free(content->parsed_message_annotations);
        content->parsed_message_annotations = 0;
        return 0;
    }

    qd_field_iterator_free(ma);
    return content->parsed_message_annotations;
}

qd_field_iterator_t *router_annotate_message(qd_router_t       *router,
                                             qd_parsed_field_t *in_ma,
                                             qd_message_t      *msg,
                                             int               *drop,
                                             const char        *to_override,
                                             char *node_id,
                                             bool strip_inbound_annotations)
{
    qd_field_iterator_t *ingress_iter = 0;

    qd_parsed_field_t *trace   = 0;
    qd_parsed_field_t *ingress = 0;
    qd_parsed_field_t *to      = 0;

    if (in_ma && !strip_inbound_annotations) {
        uint32_t count = qd_parse_sub_count(in_ma);
        bool done = false;

        for (uint32_t idx = 0; idx < count && !done; idx++) {
            qd_parsed_field_t *sub  = qd_parse_sub_key(in_ma, idx);
            if (!sub) continue;
            qd_field_iterator_t *iter = qd_parse_raw(sub);
            if (!iter) continue;

            if (qd_field_iterator_equal(iter, (unsigned char *)QD_MA_TRACE)) {
                trace = qd_parse_sub_value(in_ma, idx);
            } else if (qd_field_iterator_equal(iter, (unsigned char *)QD_MA_INGRESS)) {
                ingress = qd_parse_sub_value(in_ma, idx);
            } else if (qd_field_iterator_equal(iter, (unsigned char *)QD_MA_TO)) {
                to = qd_parse_sub_value(in_ma, idx);
            }
            done = trace && ingress && to;
        }
    }

    //
    // QD_MA_TRACE:
    // If there is a trace field, append this router's ID to the trace.
    // If the router ID is already in the trace the msg has looped.
    //
    qd_composed_field_t *trace_field = qd_compose_subfield(0);
    qd_compose_start_list(trace_field);
    if (trace) {
        if (qd_parse_is_list(trace)) {
            uint32_t idx = 0;
            qd_parsed_field_t *trace_item = qd_parse_sub_value(trace, idx);
            while (trace_item) {
                qd_field_iterator_t *iter = qd_parse_raw(trace_item);
                if (qd_field_iterator_equal(iter, (unsigned char*) node_id)) {
                    *drop = 1;
                    return 0;  // no further processing necessary
                }
                qd_field_iterator_reset(iter);
                qd_compose_insert_string_iterator(trace_field, iter);
                idx++;
                trace_item = qd_parse_sub_value(trace, idx);
            }
        }
    }
    qd_compose_insert_string(trace_field, node_id);
    qd_compose_end_list(trace_field);
    qd_message_set_trace_annotation(msg, trace_field);

    //
    // QD_MA_TO:
    // The supplied to override takes precedence over any existing
    // value.
    //
    if (to_override) {  // takes precedence over existing value
        qd_composed_field_t *to_field = qd_compose_subfield(0);
        qd_compose_insert_string(to_field, to_override);
        qd_message_set_to_override_annotation(msg, to_field);
    } else if (to) {
        qd_composed_field_t *to_field = qd_compose_subfield(0);
        qd_compose_insert_string_iterator(to_field, qd_parse_raw(to));
        qd_message_set_to_override_annotation(msg, to_field);
    }

    //
    // QD_MA_INGRESS:
    // If there is no ingress field, annotate the ingress as
    // this router else keep the original field.
    //
    qd_composed_field_t *ingress_field = qd_compose_subfield(0);
    if (ingress && qd_parse_is_scalar(ingress)) {
        ingress_iter = qd_parse_raw(ingress);
        qd_compose_insert_string_iterator(ingress_field, ingress_iter);
    } else
        qd_compose_insert_string(ingress_field, node_id);
    qd_message_set_ingress_annotation(msg, ingress_field);

    //
    // Return the iterator to the ingress field _if_ it was present.
    // If we added the ingress, return NULL.
    //
    return ingress_iter;
}

void qd_message_set_trace_annotation(qd_message_t *in_msg, qd_composed_field_t *trace_field)
{
    qd_message_pvt_t *msg = (qd_message_pvt_t*) in_msg;
    qd_buffer_list_free_buffers(&msg->ma_trace);
    qd_compose_take_buffers(trace_field, &msg->ma_trace);
    qd_compose_free(trace_field);
}

void qd_message_set_to_override_annotation(qd_message_t *in_msg, qd_composed_field_t *to_field)
{
    qd_message_pvt_t *msg = (qd_message_pvt_t*) in_msg;
    qd_buffer_list_free_buffers(&msg->ma_to_override);
    qd_compose_take_buffers(to_field, &msg->ma_to_override);
    qd_compose_free(to_field);
}

void qd_message_set_ingress_annotation(qd_message_t *in_msg, qd_composed_field_t *ingress_field)
{
    qd_message_pvt_t *msg = (qd_message_pvt_t*) in_msg;
    qd_buffer_list_free_buffers(&msg->ma_ingress);
    qd_compose_take_buffers(ingress_field, &msg->ma_ingress);
    qd_compose_free(ingress_field);
}

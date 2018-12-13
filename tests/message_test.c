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

#include "test_case.h"
#include <stdio.h>
#include <string.h>
#include "message_private.h"
#include <qpid/dispatch/iterator.h>
#include <qpid/dispatch/amqp.h>
#include <proton/message.h>

static char buffer[10000];

static size_t flatten_bufs(qd_message_content_t *content)
{
    char        *cursor = buffer;
    qd_buffer_t *buf    = DEQ_HEAD(content->buffers);

    while (buf) {
        memcpy(cursor, qd_buffer_base(buf), qd_buffer_size(buf));
        cursor += qd_buffer_size(buf);
        buf = buf->next;
    }

    return (size_t) (cursor - buffer);
}


static void set_content(qd_message_content_t *content, size_t len)
{
    char        *cursor = buffer;
    qd_buffer_t *buf;

    while (len > (size_t) (cursor - buffer)) {
        buf = qd_buffer();
        size_t segment   = qd_buffer_capacity(buf);
        size_t remaining = len - (size_t) (cursor - buffer);
        if (segment > remaining)
            segment = remaining;
        memcpy(qd_buffer_base(buf), cursor, segment);
        cursor += segment;
        qd_buffer_insert(buf, segment);
        DEQ_INSERT_TAIL(content->buffers, buf);
    }
}


static void set_content_bufs(qd_message_content_t *content, int nbufs)
{
    for (; nbufs > 0; nbufs--) {
        qd_buffer_t *buf = qd_buffer();
        size_t segment   = qd_buffer_capacity(buf);
        qd_buffer_insert(buf, segment);
        DEQ_INSERT_TAIL(content->buffers, buf);
    }
}


static char* test_send_to_messenger(void *context)
{
    qd_message_t         *msg     = qd_message();
    qd_message_content_t *content = MSG_CONTENT(msg);
    qd_message_compose_1(msg, "test_addr_0", 0);
    qd_buffer_t *buf = DEQ_HEAD(content->buffers);
    if (buf == 0) return "Expected a buffer in the test message";

    pn_message_t *pn_msg = pn_message();
    size_t len = flatten_bufs(content);
    int result = pn_message_decode(pn_msg, buffer, len);
    if (result != 0) return "Error in pn_message_decode";

    if (strcmp(pn_message_get_address(pn_msg), "test_addr_0") != 0)
        return "Address mismatch in received message";

    pn_message_free(pn_msg);
    qd_message_free(msg);

    return 0;
}


static char* test_receive_from_messenger(void *context)
{
    pn_message_t *pn_msg = pn_message();
    pn_message_set_address(pn_msg, "test_addr_1");

    size_t       size = 10000;
    int result = pn_message_encode(pn_msg, buffer, &size);
    if (result != 0) {
        pn_message_free(pn_msg);
        return "Error in pn_message_encode";
    }

    qd_message_t         *msg     = qd_message();
    qd_message_content_t *content = MSG_CONTENT(msg);

    set_content(content, size);

    int valid = qd_message_check(msg, QD_DEPTH_ALL);
    if (!valid) {
        pn_message_free(pn_msg);
        qd_message_free(msg);
        return "qd_message_check returns 'invalid'";
    }

    qd_iterator_t *iter = qd_message_field_iterator(msg, QD_FIELD_TO);
    if (iter == 0) {
        pn_message_free(pn_msg);
        qd_message_free(msg);
        return "Expected an iterator for the 'to' field";
    }

    if (!qd_iterator_equal(iter, (unsigned char*) "test_addr_1")) {
        qd_iterator_free(iter);
        pn_message_free(pn_msg);
        qd_message_free(msg);
        return "Mismatched 'to' field contents";
    }
    qd_iterator_free(iter);

    ssize_t  test_len = (size_t)qd_message_field_length(msg, QD_FIELD_TO);
    if (test_len != 11) {
        pn_message_free(pn_msg);
        qd_message_free(msg);
        return "Incorrect field length";
    }

    char test_field[100];
    size_t hdr_length;
    test_len = qd_message_field_copy(msg, QD_FIELD_TO, test_field, &hdr_length);
    if (test_len - hdr_length != 11) {
        pn_message_free(pn_msg);
        qd_message_free(msg);
        return "Incorrect length returned from field_copy";
    }

    if (test_len < 0) {
        pn_message_free(pn_msg);
        qd_message_free(msg);
        return "test_len cannot be less than zero";
    }
    test_field[test_len] = '\0';
    if (strcmp(test_field + hdr_length, "test_addr_1") != 0) {
        pn_message_free(pn_msg);
        qd_message_free(msg);
        return "Incorrect field content returned from field_copy";
    }

    pn_message_free(pn_msg);
    qd_message_free(msg);

    return 0;
}


// load a few interesting message properties and validate
static char* test_message_properties(void *context)
{
    pn_atom_t id = {.type = PN_STRING,
                    .u.as_bytes.start = "messageId",
                    .u.as_bytes.size = 9};
    pn_atom_t cid = {.type = PN_STRING,
                     .u.as_bytes.start = "correlationId",
                     .u.as_bytes.size = 13};
    const char *subject = "A Subject";
    pn_message_t *pn_msg = pn_message();
    pn_message_set_id(pn_msg, id);
    pn_message_set_subject(pn_msg, subject);
    pn_message_set_correlation_id(pn_msg, cid);

    size_t       size = 10000;
    int result = pn_message_encode(pn_msg, buffer, &size);
    pn_message_free(pn_msg);

    if (result != 0) return "Error in pn_message_encode";

    qd_message_t         *msg     = qd_message();
    qd_message_content_t *content = MSG_CONTENT(msg);

    set_content(content, size);

    qd_iterator_t *iter = qd_message_field_iterator(msg, QD_FIELD_CORRELATION_ID);
    if (!iter) {
        qd_message_free(msg);
        return "Expected iterator for the 'correlation-id' field";
    }
    if (qd_iterator_length(iter) != 13) {
        qd_iterator_free(iter);
        qd_message_free(msg);
        return "Bad length for correlation-id";
    }
    if (!qd_iterator_equal(iter, (const unsigned char *)"correlationId")) {
        qd_iterator_free(iter);
        qd_message_free(msg);
        return "Invalid correlation-id";
    }
    qd_iterator_free(iter);

    iter = qd_message_field_iterator(msg, QD_FIELD_SUBJECT);
    if (!iter) {
        qd_iterator_free(iter);
        qd_message_free(msg);
        return "Expected iterator for the 'subject' field";
    }
    if (!qd_iterator_equal(iter, (const unsigned char *)subject)) {
        qd_iterator_free(iter);
        qd_message_free(msg);
        return "Bad value for subject";
    }
    qd_iterator_free(iter);

    iter = qd_message_field_iterator(msg, QD_FIELD_MESSAGE_ID);
    if (!iter) {
        qd_message_free(msg);
        return "Expected iterator for the 'message-id' field";
    }
    if (qd_iterator_length(iter) != 9) {
        qd_iterator_free(iter);
        qd_message_free(msg);
        return "Bad length for message-id";
    }
    if (!qd_iterator_equal(iter, (const unsigned char *)"messageId")) {
        qd_iterator_free(iter);
        qd_message_free(msg);
        return "Invalid message-id";
    }
    qd_iterator_free(iter);

    iter = qd_message_field_iterator(msg, QD_FIELD_TO);
    if (iter) {
        qd_iterator_free(iter);
        qd_message_free(msg);
        return "Expected no iterator for the 'to' field";
    }
    qd_iterator_free(iter);

    qd_message_free(msg);

    return 0;
}


static char* test_check_multiple(void *context)
{
    pn_message_t *pn_msg = pn_message();
    pn_message_set_address(pn_msg, "test_addr_2");

    size_t size = 10000;
    int result = pn_message_encode(pn_msg, buffer, &size);
    pn_message_free(pn_msg);
    if (result != 0) return "Error in pn_message_encode";

    qd_message_t         *msg     = qd_message();
    qd_message_content_t *content = MSG_CONTENT(msg);

    set_content(content, size);

    int valid = qd_message_check(msg, QD_DEPTH_DELIVERY_ANNOTATIONS);
    if (!valid) {
        qd_message_free(msg);
        return "qd_message_check returns 'invalid' for DELIVERY_ANNOTATIONS";
    }

    valid = qd_message_check(msg, QD_DEPTH_BODY);
    if (!valid) {
        qd_message_free(msg);
        return "qd_message_check returns 'invalid' for BODY";
    }

    valid = qd_message_check(msg, QD_DEPTH_PROPERTIES);
    if (!valid) {
        qd_message_free(msg);
        return "qd_message_check returns 'invalid' for PROPERTIES";
    }

    qd_message_free(msg);

    return 0;
}


static char* test_send_message_annotations(void *context)
{
    qd_message_t         *msg     = qd_message();
    qd_message_content_t *content = MSG_CONTENT(msg);

    qd_composed_field_t *trace = qd_compose_subfield(0);
    qd_compose_start_list(trace);
    qd_compose_insert_string(trace, "Node1");
    qd_compose_insert_string(trace, "Node2");
    qd_compose_end_list(trace);
    qd_message_set_trace_annotation(msg, trace);

    qd_composed_field_t *to_override = qd_compose_subfield(0);
    qd_compose_insert_string(to_override, "to/address");
    qd_message_set_to_override_annotation(msg, to_override);

    qd_composed_field_t *ingress = qd_compose_subfield(0);
    qd_compose_insert_string(ingress, "distress");
    qd_message_set_ingress_annotation(msg, ingress);

    qd_message_compose_1(msg, "test_addr_0", 0);
    qd_buffer_t *buf = DEQ_HEAD(content->buffers);
    if (buf == 0) return "Expected a buffer in the test message";

    pn_message_t *pn_msg = pn_message();
    size_t len = flatten_bufs(content);
    int result = pn_message_decode(pn_msg, buffer, len);
    if (result != 0) return "Error in pn_message_decode";

    pn_data_t *ma = pn_message_annotations(pn_msg);
    if (!ma) return "Missing message annotations";
    pn_data_rewind(ma);
    pn_data_next(ma);
    if (pn_data_type(ma) != PN_MAP) return "Invalid message annotation type";
    if (pn_data_get_map(ma) != QD_MA_N_KEYS * 2) return "Invalid map length";
    pn_data_enter(ma);
    for (int i = 0; i < QD_MA_N_KEYS; i++) {
        pn_data_next(ma);
        if (pn_data_type(ma) != PN_SYMBOL) return "Bad map index";
        pn_bytes_t sym = pn_data_get_symbol(ma);
        if (!strncmp(QD_MA_PREFIX, sym.start, sym.size)) {
            pn_data_next(ma);
            sym = pn_data_get_string(ma);
        } else if (!strncmp(QD_MA_INGRESS, sym.start, sym.size)) {
            pn_data_next(ma);
            sym = pn_data_get_string(ma);
            if (strncmp("distress", sym.start, sym.size)) return "Bad ingress";
            //fprintf(stderr, "[%.*s]\n", (int)sym.size, sym.start);
        } else if (!strncmp(QD_MA_TO, sym.start, sym.size)) {
            pn_data_next(ma);
            sym = pn_data_get_string(ma);
            if (strncmp("to/address", sym.start, sym.size)) return "Bad to override";
            //fprintf(stderr, "[%.*s]\n", (int)sym.size, sym.start);
        } else if (!strncmp(QD_MA_TRACE, sym.start, sym.size)) {
            pn_data_next(ma);
            if (pn_data_type(ma) != PN_LIST) return "List not found";
            pn_data_enter(ma);
            pn_data_next(ma);
            sym = pn_data_get_string(ma);
            if (strncmp("Node1", sym.start, sym.size)) return "Bad trace entry";
            //fprintf(stderr, "[%.*s]\n", (int)sym.size, sym.start);
            pn_data_next(ma);
            sym = pn_data_get_string(ma);
            if (strncmp("Node2", sym.start, sym.size)) return "Bad trace entry";
            //fprintf(stderr, "[%.*s]\n", (int)sym.size, sym.start);
            pn_data_exit(ma);
        } else return "Unexpected map key";
    }

    pn_message_free(pn_msg);
    qd_message_free(msg);

    return 0;
}


static char* test_q2_input_holdoff_sensing(void *context)
{
    if (QD_QLIMIT_Q2_LOWER >= QD_QLIMIT_Q2_UPPER)
        return "QD_LIMIT_Q2 lower limit is bigger than upper limit";

    for (int nbufs=1; nbufs<QD_QLIMIT_Q2_UPPER + 1; nbufs++) {
        qd_message_t         *msg     = qd_message();
        qd_message_content_t *content = MSG_CONTENT(msg);

        set_content_bufs(content, nbufs);
        if (qd_message_Q2_holdoff_should_block(msg) != (nbufs >= QD_QLIMIT_Q2_UPPER)) {
            qd_message_free(msg);
            return "qd_message_holdoff_would_block was miscalculated";
        }
        if (qd_message_Q2_holdoff_should_unblock(msg) != (nbufs < QD_QLIMIT_Q2_LOWER)) {
            qd_message_free(msg);
            return "qd_message_holdoff_would_unblock was miscalculated";
        }

        qd_message_free(msg);
    }
    return 0;
}


int message_tests(void)
{
    int result = 0;
    char *test_group = "message_tests";

    TEST_CASE(test_send_to_messenger, 0);
    TEST_CASE(test_receive_from_messenger, 0);
    TEST_CASE(test_message_properties, 0);
    TEST_CASE(test_check_multiple, 0);
    TEST_CASE(test_send_message_annotations, 0);
    TEST_CASE(test_q2_input_holdoff_sensing, 0);

    return result;
}


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

#include "message_private.h"
#include "test_case.h"

#include "qpid/dispatch/amqp.h"
#include "qpid/dispatch/iterator.h"

#include <proton/message.h>
#include <proton/raw_connection.h>

#include <stdio.h>
#include <string.h>

#define FLAT_BUF_SIZE (100000)
static unsigned char buffer[FLAT_BUF_SIZE];

// given a buffer list, copy the data it contains into a single contiguous
// buffer.
static size_t flatten_bufs(const qd_buffer_list_t *content)
{
    unsigned char *cursor = buffer;
    qd_buffer_t *buf      = DEQ_HEAD((*content));

    while (buf) {
        // if this asserts you need a bigger buffer!
        assert(((size_t) (cursor - buffer)) + qd_buffer_size(buf) < FLAT_BUF_SIZE);
        memcpy(cursor, qd_buffer_base(buf), qd_buffer_size(buf));
        cursor += qd_buffer_size(buf);
        buf = DEQ_NEXT(buf);
    }

    return (size_t) (cursor - buffer);
}


static void set_content(qd_message_content_t *content, unsigned char *buffer, size_t len)
{
    qd_buffer_list_t blist = DEQ_EMPTY;

    qd_buffer_list_append(&blist, buffer, len);
    DEQ_APPEND(content->buffers, blist);
    SET_ATOMIC_FLAG(&content->receive_complete);
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

    qd_composed_field_t *header = qd_compose(QD_PERFORMATIVE_HEADER, 0);
    qd_compose_start_list(header);
    qd_compose_insert_bool(header, true);  // durable
    qd_compose_end_list(header);

    qd_composed_field_t *props = qd_compose(QD_PERFORMATIVE_PROPERTIES, 0);
    qd_compose_start_list(props);
    qd_compose_insert_null(props);          // message-id
    qd_compose_insert_null(props);          // user-id
    qd_compose_insert_string(props, "test_addr_0");    // to
    qd_compose_end_list(props);

    qd_message_compose_3(msg, header, props, true);
    qd_compose_free(header);
    qd_compose_free(props);

    qd_buffer_t *buf = DEQ_HEAD(content->buffers);
    if (buf == 0) {
        qd_message_free(msg);
        return "Expected a buffer in the test message";
    }

    pn_message_t *pn_msg = pn_message();
    size_t len = flatten_bufs(&content->buffers);
    int result = pn_message_decode(pn_msg, (char *)buffer, len);
    if (result != 0) {
        pn_message_free(pn_msg);
        qd_message_free(msg);
        return "Error in pn_message_decode";
    }

    if (!pn_message_is_durable(pn_msg)) {
        pn_message_free(pn_msg);
        qd_message_free(msg);
        return "Durable flag not set";
    }

    if (strcmp(pn_message_get_address(pn_msg), "test_addr_0") != 0) {
        pn_message_free(pn_msg);
        qd_message_free(msg);
        return "Address mismatch in received message";
    }

    pn_message_free(pn_msg);
    qd_message_free(msg);

    return 0;
}


static char* test_receive_from_messenger(void *context)
{
    pn_message_t *pn_msg = pn_message();
    pn_message_set_address(pn_msg, "test_addr_1");

    size_t       size = 10000;
    int result = pn_message_encode(pn_msg, (char *)buffer, &size);
    if (result != 0) {
        pn_message_free(pn_msg);
        return "Error in pn_message_encode";
    }

    qd_message_t         *msg     = qd_message();
    qd_message_content_t *content = MSG_CONTENT(msg);

    set_content(content, buffer, size);

    if (qd_message_check_depth(msg, QD_DEPTH_ALL) != QD_MESSAGE_DEPTH_OK) {
        pn_message_free(pn_msg);
        qd_message_free(msg);
        return "qd_message_check_depth returns 'invalid'";
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
    int result = pn_message_encode(pn_msg, (char *)buffer, &size);
    pn_message_free(pn_msg);

    if (result != 0) return "Error in pn_message_encode";

    qd_message_t         *msg     = qd_message();
    qd_message_content_t *content = MSG_CONTENT(msg);

    set_content(content, buffer, size);

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


// run qd_message_check_depth against different legal AMQP message
//
static char* _check_all_depths(qd_message_t *msg)
{
    static const qd_message_depth_t depths[] = {
        // yep: purposely out of order
        QD_DEPTH_MESSAGE_ANNOTATIONS,
        QD_DEPTH_DELIVERY_ANNOTATIONS,
        QD_DEPTH_PROPERTIES,
        QD_DEPTH_HEADER,
        QD_DEPTH_APPLICATION_PROPERTIES,
        QD_DEPTH_BODY
    };
    static const int n_depths = 6;

    static char err[1024];

    for (int i = 0; i < n_depths; ++i) {
        if (qd_message_check_depth(msg, depths[i]) != QD_MESSAGE_DEPTH_OK) {
            snprintf(err, 1023,
                     "qd_message_check_depth returned 'invalid' for section 0x%X", (unsigned int)depths[i]);
            err[1023] = 0;
            return err;
        }
    }
    return 0;
}


static char* test_check_multiple(void *context)
{
    // case 1: a minimal encoded message
    //
    pn_message_t *pn_msg = pn_message();

    size_t size = 10000;
    int result = pn_message_encode(pn_msg, (char *)buffer, &size);
    pn_message_free(pn_msg);
    if (result != 0) return "Error in pn_message_encode";

    qd_message_t         *msg     = qd_message();
    qd_message_content_t *content = MSG_CONTENT(msg);

    set_content(content, buffer, size);
    char *rc = _check_all_depths(msg);
    qd_message_free(msg);
    if (rc) return rc;

    // case 2: minimal, with address field in header
    //
    pn_msg = pn_message();
    pn_message_set_address(pn_msg, "test_addr_2");
    size = 10000;
    result = pn_message_encode(pn_msg, (char *)buffer, &size);
    pn_message_free(pn_msg);
    if (result != 0) return "Error in pn_message_encode";
    msg = qd_message();
    set_content(MSG_CONTENT(msg), buffer, size);
    rc = _check_all_depths(msg);
    qd_message_free(msg);
    if (rc) return rc;

    // case 3: null body
    //
    pn_msg = pn_message();
    pn_data_t *body = pn_message_body(pn_msg);
    pn_data_put_null(body);
    size = 10000;
    result = pn_message_encode(pn_msg, (char *)buffer, &size);
    pn_message_free(pn_msg);
    if (result != 0) return "Error in pn_message_encode";
    msg = qd_message();
    set_content(MSG_CONTENT(msg), buffer, size);
    rc = _check_all_depths(msg);
    qd_message_free(msg);
    if (rc) return rc;

    // case 4: minimal legal AMQP 1.0 message (as defined by the standard)
    // A single body field with a null value
    const unsigned char null_body[] = {0x00, 0x53, 0x77, 0x40};
    size = sizeof(null_body);
    memcpy(buffer, null_body, size);
    msg = qd_message();
    set_content(MSG_CONTENT(msg), buffer, size);
    rc = _check_all_depths(msg);
    qd_message_free(msg);
    return rc;
}


// Create a proton message containing router-specific annotations.
// Ensure the annotations are properly parsed.
//
static char* test_parse_message_annotations(void *context)
{
    char *error = 0;

    pn_message_t *pn_msg = pn_message();
    pn_message_set_durable(pn_msg, true);
    pn_message_set_address(pn_msg, "test_addr_0");
    pn_data_t *pn_ma = pn_message_annotations(pn_msg);
    pn_data_clear(pn_ma);
    pn_data_put_map(pn_ma);
    pn_data_enter(pn_ma);

    pn_data_put_symbol(pn_ma, pn_bytes(strlen(QD_MA_INGRESS), QD_MA_INGRESS));
    pn_data_put_string(pn_ma, pn_bytes(strlen("distress"), "distress"));

    pn_data_put_symbol(pn_ma, pn_bytes(strlen(QD_MA_TRACE), QD_MA_TRACE));
    pn_data_put_list(pn_ma);
    pn_data_enter(pn_ma);
    pn_data_put_string(pn_ma, pn_bytes(strlen("Node1"), "Node1"));
    pn_data_put_string(pn_ma, pn_bytes(strlen("Node2"), "Node2"));
    pn_data_exit(pn_ma);

    pn_data_put_symbol(pn_ma, pn_bytes(strlen(QD_MA_TO), QD_MA_TO));
    pn_data_put_string(pn_ma, pn_bytes(strlen("to/address"), "to/address"));

    pn_data_put_symbol(pn_ma, pn_bytes(strlen(QD_MA_PHASE), QD_MA_PHASE));
    pn_data_put_int(pn_ma, 9);

    pn_data_put_symbol(pn_ma, pn_bytes(strlen(QD_MA_STREAM), QD_MA_STREAM));
    pn_data_put_int(pn_ma, 1);
    pn_data_exit(pn_ma);

    // convert the proton message to a dispatch message

    qd_message_t         *msg     = qd_message();
    qd_message_content_t *content = MSG_CONTENT(msg);
    size_t                    len = FLAT_BUF_SIZE;

    pn_message_encode(pn_msg, (char*) buffer, &len);
    qd_buffer_list_t blist = DEQ_EMPTY;
    qd_buffer_list_append(&blist, buffer, len);
    DEQ_MOVE(blist, content->buffers);

    // now parse the sections:
    if (qd_message_check_depth(msg, QD_DEPTH_PROPERTIES) != QD_MESSAGE_DEPTH_OK) {
        error = "Failed to validate message";
        goto exit;
    }

    error = (char*) qd_message_parse_annotations(msg);
    if (error) {
        goto exit;
    }

    // validate sections parsed correctly:

    qd_parsed_field_t *pf_trace = qd_message_get_trace(msg);
    if (!pf_trace) {
        error = "TRACE not found!";
        goto exit;
    }
    if (qd_parse_sub_count(pf_trace) != 2
        || !qd_iterator_equal(qd_parse_raw(qd_parse_sub_value(pf_trace, 0)),
                              (const unsigned char*) "Node1")
        || !qd_iterator_equal(qd_parse_raw(qd_parse_sub_value(pf_trace, 1)),
                              (const unsigned char*) "Node2")) {
        error = "Invalid trace list";
        goto exit;
    }

    qd_parsed_field_t *pf_to = qd_message_get_to_override(msg);
    if (!pf_to) {
        error = "TO override not found!";
        goto exit;
    }
    if (!qd_iterator_equal(qd_parse_raw(pf_to), (const unsigned char*) "to/address")) {
        error = "Invalid TO override!";
        goto exit;
    }

    qd_parsed_field_t *pf_ingress = qd_message_get_ingress_router(msg);
    if (!pf_ingress) {
        error = "INGRESS not found!";
        goto exit;
    }
    if (!qd_iterator_equal(qd_parse_raw(pf_ingress), (const unsigned char*) "distress")) {
        error = "Invalid ingress override!";
        goto exit;
    }

    if (!qd_message_is_streaming(msg)) {
        error = "streaming flag not parsed!";
        goto exit;
    }

    if (qd_message_get_phase_annotation(msg) != 9) {
        error = "phase not parsed!";
        goto exit;
    }


exit:

    pn_message_free(pn_msg);
    qd_message_free(msg);
    return error;
}


static char* test_q2_input_holdoff_sensing(void *context)
{
    if (QD_QLIMIT_Q2_LOWER >= QD_QLIMIT_Q2_UPPER)
        return "QD_LIMIT_Q2 lower limit is bigger than upper limit";

    for (int nbufs=1; nbufs<QD_QLIMIT_Q2_UPPER + 1; nbufs++) {
        qd_message_t         *msg     = qd_message();
        qd_message_content_t *content = MSG_CONTENT(msg);

        set_content_bufs(content, nbufs);
        if (_Q2_holdoff_should_block_LH(content) != (nbufs >= QD_QLIMIT_Q2_UPPER)) {
            qd_message_free(msg);
            return "qd_message_holdoff_would_block was miscalculated";
        }
        if (_Q2_holdoff_should_unblock_LH(content) != (nbufs < QD_QLIMIT_Q2_LOWER)) {
            qd_message_free(msg);
            return "qd_message_holdoff_would_unblock was miscalculated";
        }

        qd_message_free(msg);
    }
    return 0;
}


// verify that message check does not incorrectly validate a message section
// that has not been completely received.
//
static char *test_incomplete_annotations(void *context)
{
    const char big_string[] =
        "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
        "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
        "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
        "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
        "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
        "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
        "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
        "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
        "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
        "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789";

    char *result = 0;
    qd_message_t *msg = 0;
    pn_message_t *out_message = pn_message();

    pn_data_t *body = pn_message_body(out_message);
    pn_data_clear(body);
    pn_data_put_list(body);
    pn_data_enter(body);
    pn_data_put_long(body, 1);
    pn_data_put_long(body, 2);
    pn_data_put_long(body, 3);
    pn_data_exit(body);

    // Add a bunch 'o user message annotations
    pn_data_t *annos = pn_message_annotations(out_message);
    pn_data_clear(annos);
    pn_data_put_map(annos);
    pn_data_enter(annos);

    pn_data_put_symbol(annos, pn_bytes(strlen("my-key"), "my-key"));
    pn_data_put_string(annos, pn_bytes(strlen("my-data"), "my-data"));

    pn_data_put_symbol(annos, pn_bytes(strlen("my-other-key"), "my-other-key"));
    pn_data_put_string(annos, pn_bytes(strlen("my-other-data"), "my-other-data"));

    // embedded map
    pn_data_put_symbol(annos, pn_bytes(strlen("my-map"), "my-map"));
    pn_data_put_map(annos);
    pn_data_enter(annos);
    pn_data_put_symbol(annos, pn_bytes(strlen("my-map-key1"), "my-map-key1"));
    pn_data_put_char(annos, 'X');
    pn_data_put_symbol(annos, pn_bytes(strlen("my-map-key2"), "my-map-key2"));
    pn_data_put_byte(annos, 0x12);
    pn_data_put_symbol(annos, pn_bytes(strlen("my-map-key3"), "my-map-key3"));
    pn_data_put_string(annos, pn_bytes(strlen("Are We Not Men?"), "Are We Not Men?"));
    pn_data_put_symbol(annos, pn_bytes(strlen("my-last-key"), "my-last-key"));
    pn_data_put_binary(annos, pn_bytes(sizeof(big_string), big_string));
    pn_data_exit(annos);

    pn_data_put_symbol(annos, pn_bytes(strlen("my-ulong"), "my-ulong"));
    pn_data_put_ulong(annos, 0xDEADBEEFCAFEBEEF);

    // embedded list
    pn_data_put_symbol(annos, pn_bytes(strlen("my-list"), "my-list"));
    pn_data_put_list(annos);
    pn_data_enter(annos);
    pn_data_put_string(annos, pn_bytes(sizeof(big_string), big_string));
    pn_data_put_double(annos, 3.1415);
    pn_data_put_short(annos, 1966);
    pn_data_exit(annos);

    pn_data_put_symbol(annos, pn_bytes(strlen("my-bool"), "my-bool"));
    pn_data_put_bool(annos, false);

    pn_data_exit(annos);

    // now encode it

    size_t encode_len = sizeof(buffer);
    int rc = pn_message_encode(out_message, (char *)buffer, &encode_len);
    if (rc) {
        if (rc == PN_OVERFLOW)
            result = "Error: sizeof(buffer) in message_test.c too small - update it!";
        else
            result = "Error encoding message";
        goto exit;
    }

    assert(encode_len > 100);  // you broke the test!

    // Verify that the message check fails unless the entire annotations are
    // present.  First copy in only the first 100 bytes: enough for the MA
    // section header but not the whole section

    msg = qd_message();
    qd_message_content_t *content = MSG_CONTENT(msg);
    set_content(content, buffer, 100);
    CLEAR_ATOMIC_FLAG(&content->receive_complete);   // more data coming!
    if (qd_message_check_depth(msg, QD_DEPTH_MESSAGE_ANNOTATIONS) != QD_MESSAGE_DEPTH_INCOMPLETE) {
        result = "Error: incomplete message was not detected!";
        goto exit;
    }

    // now complete the message
    set_content(content, &buffer[100], encode_len - 100);
    if (qd_message_check_depth(msg, QD_DEPTH_MESSAGE_ANNOTATIONS) != QD_MESSAGE_DEPTH_OK) {
        result = "Error: expected message to be valid!";
    }

exit:

    if (out_message) pn_message_free(out_message);
    if (msg) qd_message_free(msg);

    return result;
}


static char *test_check_weird_messages(void *context)
{
    char *result = 0;
    qd_message_t *msg = qd_message();

    // case 1:
    // delivery annotations with empty map
    unsigned char da_map[] = {0x00, 0x80,
                              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x71,
                              0xc1, 0x01, 0x00};
    // first test an incomplete pattern:
    set_content(MSG_CONTENT(msg), da_map, 4);
    CLEAR_ATOMIC_FLAG(&(MSG_CONTENT(msg)->receive_complete));
    qd_message_depth_status_t mc = qd_message_check_depth(msg, QD_DEPTH_DELIVERY_ANNOTATIONS);
    if (mc != QD_MESSAGE_DEPTH_INCOMPLETE) {
        result = "Expected INCOMPLETE status";
        goto exit;
    }

    // full pattern, but no tag
    set_content(MSG_CONTENT(msg), &da_map[4], 6);
    CLEAR_ATOMIC_FLAG(&(MSG_CONTENT(msg)->receive_complete));
    mc = qd_message_check_depth(msg, QD_DEPTH_DELIVERY_ANNOTATIONS);
    if (mc != QD_MESSAGE_DEPTH_INCOMPLETE) {
        result = "Expected INCOMPLETE status";
        goto exit;
    }

    // add tag, but incomplete field:
    set_content(MSG_CONTENT(msg), &da_map[10], 1);
    CLEAR_ATOMIC_FLAG(&(MSG_CONTENT(msg)->receive_complete));
    mc = qd_message_check_depth(msg, QD_DEPTH_DELIVERY_ANNOTATIONS);
    if (mc != QD_MESSAGE_DEPTH_INCOMPLETE) {
        result = "Expected INCOMPLETE status";
        goto exit;
    }

    // and finish up
    set_content(MSG_CONTENT(msg), &da_map[11], 2);
    mc = qd_message_check_depth(msg, QD_DEPTH_DELIVERY_ANNOTATIONS);
    if (mc != QD_MESSAGE_DEPTH_OK) {
        result = "Expected OK status";
        goto exit;
    }

    // case 2: negative test - detect invalid tag
    unsigned char bad_hdr[] = {0x00, 0x53, 0x70, 0xC1};  // 0xc1 == map, not list!
    qd_message_free(msg);
    msg = qd_message();
    set_content(MSG_CONTENT(msg), bad_hdr, sizeof(bad_hdr));
    CLEAR_ATOMIC_FLAG(&(MSG_CONTENT(msg)->receive_complete));
    mc = qd_message_check_depth(msg, QD_DEPTH_DELIVERY_ANNOTATIONS); // looking _past_ header!
    if (mc != QD_MESSAGE_DEPTH_INVALID) {
        result = "Bad tag not detected!";
        goto exit;
    }

    // case 3: check the valid body types
    unsigned char body_bin[] = {0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x75,
                                0xA0, 0x03, 0x00, 0x01, 0x02};
    qd_message_free(msg);
    msg = qd_message();
    set_content(MSG_CONTENT(msg), body_bin, sizeof(body_bin));
    mc = qd_message_check_depth(msg, QD_DEPTH_ALL); // looking _past_ header!
    if (mc != QD_MESSAGE_DEPTH_OK) {
        result = "Expected OK bin body";
        goto exit;
    }

    unsigned char body_seq[] = {0x00, 0x53, 0x76, 0x45};
    qd_message_free(msg);
    msg = qd_message();
    set_content(MSG_CONTENT(msg), body_seq, sizeof(body_seq));
    mc = qd_message_check_depth(msg, QD_DEPTH_BODY);
    if (mc != QD_MESSAGE_DEPTH_OK) {
        result = "Expected OK seq body";
        goto exit;
    }

    unsigned char body_value[] = {0x00, 0x53, 0x77, 0x51, 0x99};
    qd_message_free(msg);
    msg = qd_message();
    set_content(MSG_CONTENT(msg), body_value, sizeof(body_value));
    mc = qd_message_check_depth(msg, QD_DEPTH_BODY);
    if (mc != QD_MESSAGE_DEPTH_OK) {
        result = "Expected OK value body";
        goto exit;
    }

exit:
    qd_message_free(msg);
    return result;
}

//
// Testing protocol adapter 'stream_data' interfaces
//

static qd_message_t *stream_data_generate_message(char *s_chunk_size, char *s_n_chunks, bool flatten)
{
    // Fill a message with n_chunks of vbin chunk_size body data.

    int   chunk_size = atoi(s_chunk_size);
    int   n_chunks   = atoi(s_n_chunks);

    // Add message headers

    qd_composed_field_t *header = qd_compose(QD_PERFORMATIVE_HEADER, 0);
    qd_compose_start_list(header);
    qd_compose_insert_bool(header, 0);     // durable
    qd_compose_insert_null(header);        // priority
    qd_compose_end_list(header);

    qd_composed_field_t *props = qd_compose(QD_PERFORMATIVE_PROPERTIES, 0);
    qd_compose_start_list(props);
    qd_compose_insert_null(props);          // message-id
    qd_compose_insert_null(props);          // user-id
    qd_compose_insert_string(props, "whom-it-may-concern");    // to
    qd_compose_end_list(props);

    qd_message_t *msg = qd_message();
    qd_message_compose_3(msg, header, props, false);
    qd_compose_free(header);
    qd_compose_free(props);

    // Generate the chunks. Each chunk is wrapped in a BODY_DATA section. Each
    // body section resides in its own buffer list.  This creates a sparse body
    // buffer chain which will exercise buffer boundary checking.

    unsigned char *buf2 = (unsigned char *)malloc(chunk_size);
    qd_buffer_list_t body = DEQ_EMPTY;

    for (int j=0; j<n_chunks; j++) {
        qd_buffer_list_t     tmp = DEQ_EMPTY;
        qd_composed_field_t *field = qd_compose(QD_PERFORMATIVE_BODY_DATA, 0);

        memset(buf2, j+1, chunk_size);
        qd_compose_insert_binary(field, (const uint8_t*) buf2, chunk_size);
        qd_compose_take_buffers(field, &tmp);
        DEQ_APPEND(body, tmp);
        qd_compose_free(field);
    }

    if (!flatten) {
        DEQ_APPEND(MSG_CONTENT(msg)->buffers, body);
    } else {
        // compact the separate body buffer chains into the smallest buffer
        // chain possible
        size_t flat_size = flatten_bufs(&body);
        qd_buffer_list_append(&(MSG_CONTENT(msg)->buffers), buffer, flat_size);
        qd_buffer_list_free_buffers(&body);
    }

    free(buf2);
    return msg;
}

static void free_stream_data_list(qd_message_t *msg_in)
{
    // DISPATCH-1800 - this should not be required here
    qd_message_pvt_t *msg = (qd_message_pvt_t *)msg_in;
    qd_message_stream_data_t *bd = DEQ_HEAD(msg->stream_data_list);
    while (bd) {
        qd_message_stream_data_t *next = DEQ_NEXT(bd);
        free_qd_message_stream_data_t(bd);
        bd = next;
    }

}

static char *check_stream_data(char *s_chunk_size, char *s_n_chunks, bool flatten)
{
    // Fill a message with n chunks of vbin chunk_size body data.
    // Then test by retrieving n chunks from a message copy and verifing.
    //
    // 'flatten' messes with message buffers after they have been composed.
    // * Not flattened means that vbin headers stand alone in separate buffers and
    //   vbin data always starts in the first byte of a new buffer. This is the
    //   buffer condition when a message is forwarded between adaptors on a single
    //   router. The receiver and sender have two messages but share message content.
    // * Flattened means that vbin headers and vbin data are packed into the buffer
    //   list. This is the buffer condition when a message is forwarded between
    //   routers and the receiver is handling the vbin segments.

    int   chunk_size = atoi(s_chunk_size);
    int   n_chunks   = atoi(s_n_chunks);

    char *result     = 0;
    int   received;     // got this much of chunk_size chunk

    // Set the original message content

    qd_message_t *msg = stream_data_generate_message(s_chunk_size, s_n_chunks, flatten);

    // check the chunks
    // Define the number of raw buffers to be extracted on each loop
#define N_PN_RAW_BUFFS (2)

    qd_message_t *copy = qd_message_copy(msg);
    qd_message_stream_data_t *stream_data;

    for (int j=0; j<n_chunks; j++) {
        received = 0; // this chunk received size in bytes.

        // Set up the next_stream_data snapshot
        qd_message_stream_data_result_t stream_data_result = qd_message_next_stream_data(copy, &stream_data);

        if (stream_data_result == QD_MESSAGE_STREAM_DATA_BODY_OK) {
            // check stream_data payload length
            if (stream_data->payload.length != chunk_size) {
                printf("********** check_stream_data: BUFFER_SIZE=%zu, pn-buf-array-size:%d, "
                    "chunk_size:%s, n_chunks:%s, payload length error : %zu \n",
                    BUFFER_SIZE, N_PN_RAW_BUFFS, s_chunk_size, s_n_chunks, stream_data->payload.length);
                fflush(stdout);
                result = "qd_message_next_stream_data returned wrong payload length.";
                break;
            }

            // Loop to extract the body data
            //  * verify content
            //  * verify body data length

            // buffs        - body data is extracted through this array of raw buffers
            pn_raw_buffer_t buffs[N_PN_RAW_BUFFS];

            // used_buffers - Number of qd_buffers in content buffer chain consumed so far.
            //                This number must increase as dictated by qd_message_stream_data_buffers()
            //                when vbin segments are consumed from the current stream_data chunk.
            //                A single vbin segment may consume 0, 1, or many qd_buffers.
            size_t used_buffers = 0;

            while (received < chunk_size) {
                ZERO(buffs);
                size_t n_used = qd_message_stream_data_buffers(stream_data, buffs, used_buffers, N_PN_RAW_BUFFS);
                if (n_used > 0) {
                    for (size_t ii=0; ii<n_used; ii++) {
                        char e_char = (char)(j + 1);   // expected char in payload
                        // Verify the content of the bufffer
                        for (uint32_t idx=0; idx < buffs[ii].size; idx++) {
                            char actual = buffs[ii].bytes[buffs[ii].offset + idx];
                            if (e_char != actual) {
                                printf("********** check_stream_data: BUFFER_SIZE=%zu, pn-buf-array-size:%d, "
                                    "chunk_size:%s, n_chunks:%s, verify error at index %d, expected:%d, actual:%d \n",
                                    BUFFER_SIZE, N_PN_RAW_BUFFS, s_chunk_size, s_n_chunks, received + idx, e_char,
                                    actual);
                                fflush(stdout);
                                result = "verify error";
                            }
                        }
                        received += buffs[ii].size;
                    }
                    used_buffers += n_used;
                    if (!!result) break;
                } else {
                    printf("********** check_stream_data: BUFFER_SIZE=%zu, pn-buf-array-size:%d, "
                        "chunk_size:%s, n_chunks:%s, received %d bytes (not enough) \n",
                        BUFFER_SIZE, N_PN_RAW_BUFFS, s_chunk_size, s_n_chunks, received);
                    fflush(stdout);
                    result = "Did not receive enough data";
                    break;
                }
                if (received > chunk_size) {
                    printf("********** check_stream_data: BUFFER_SIZE=%zu, pn-buf-array-size:%d, "
                        "chunk_size:%s, n_chunks:%s, received %d bytes (too many) \n",
                        BUFFER_SIZE, N_PN_RAW_BUFFS, s_chunk_size, s_n_chunks, received);
                    result = "Received too much data";
                    break;
                }
            }
            // successful check

        } else if (stream_data_result == QD_MESSAGE_STREAM_DATA_INCOMPLETE) {
            result = "DATA_INCOMPLETE"; break;
        } else {
            switch (stream_data_result) {
            case QD_MESSAGE_STREAM_DATA_NO_MORE:
                result = "EOS"; break;
            case QD_MESSAGE_STREAM_DATA_INVALID:
                result = "Invalid body data for streaming message"; break;
            default:
                result = "result: default"; break;
            }
        }
    }

    free_stream_data_list(msg);
    qd_message_free(msg);
    if (!!copy) {
        free_stream_data_list(copy);
        qd_message_free(copy);
    }
    return result;
}

static char *test_check_stream_data(void * context)
{
    char *result = 0;

#define N_CHUNK_SIZES (10)
    char *chunk_sizes[N_CHUNK_SIZES] = {"1", "10", "100", "510", "511", "512", "513", "1023", "1024", "1025"};

#define N_N_CHUNKS (4)
    char *n_chunks[N_N_CHUNKS]       = {"1", "2", "10", "25"};

    for (int i=0; i<N_CHUNK_SIZES; i++) {
        for (int j=0; j<N_N_CHUNKS; j++) {
            result = check_stream_data(chunk_sizes[i], n_chunks[j], false);
            if (!!result) {
                printf("test_check_stream_data: chunk_size:%s, n_chunks:%s, flatten:%s, result:%s   \n",
                       chunk_sizes[i], n_chunks[j], "false", result);
                fflush(stdout);
                return result;
            }
            result = check_stream_data(chunk_sizes[i], n_chunks[j], true);
            if (!!result) {
                printf("test_check_stream_data: chunk_size:%s, n_chunks:%s, flatten:%s, result:%s   \n",
                       chunk_sizes[i], n_chunks[j], "true", result);
                fflush(stdout);
                return result;
            }
        }
    }
    return result;
}


// for testing Q2 unblock callback
static void q2_unblocked_handler(qd_alloc_safe_ptr_t context)
{
    int *iptr = (int*) context.ptr;
    (*iptr) += 1;
}


// Verify that qd_message_stream_data_append() will break up a long binary data
// field in order to avoid triggering Q2.  Ensure all stream_data buffers are
// freed when done.
//
static char *test_check_stream_data_append(void * context)
{
    char *result = 0;
    qd_message_t *msg = 0;
    qd_message_t *out_msg = 0;
    int unblock_called = 0;

    // generate a buffer list of binary data large enough to trigger Q2
    //
    const int body_bufct = (QD_QLIMIT_Q2_UPPER * 3) + 5;
    qd_buffer_list_t bin_data = DEQ_EMPTY;
    for (int i = 0; i < body_bufct; ++i) {
        qd_buffer_t *buffy = qd_buffer();
        qd_buffer_insert(buffy, qd_buffer_capacity(buffy));
        DEQ_INSERT_TAIL(bin_data, buffy);
    }

    // simulate building a message as an adaptor would:

    qd_composed_field_t *field = qd_compose(QD_PERFORMATIVE_HEADER, 0);
    qd_compose_start_list(field);
    qd_compose_insert_bool(field, 0);     // durable
    qd_compose_insert_null(field);        // priority
    qd_compose_end_list(field);

    field = qd_compose(QD_PERFORMATIVE_PROPERTIES, field);
    qd_compose_start_list(field);
    qd_compose_insert_ulong(field, 666);    // message-id
    qd_compose_insert_null(field);                 // user-id
    qd_compose_insert_string(field, "/whereevah"); // to
    qd_compose_insert_string(field, "my-subject");  // subject
    qd_compose_insert_string(field, "/reply-to");   // reply-to
    qd_compose_end_list(field);

    msg = qd_message_compose(field, 0, 0, false);

    qd_alloc_safe_ptr_t unblock_arg = {0};
    unblock_arg.ptr = (void*) &unblock_called;
    qd_message_set_q2_unblocked_handler(msg, q2_unblocked_handler, unblock_arg);

    // snapshot the message buffer count to use as a baseline
    const size_t base_bufct = DEQ_SIZE(MSG_CONTENT(msg)->buffers);

    bool blocked;
    int depth = qd_message_stream_data_append(msg, &bin_data, &blocked);
    if (depth <= body_bufct) {
        // expected to add extra buffer(s) for meta-data
        result = "append length is incorrect";
        goto exit;
    }

    // expected that the append has triggered Q2 blocking:
    if (!blocked) {
        result = "expected Q2 block event did not occur!";
        goto exit;
    }

    // And while we're at it, stuff in a footer
    field = qd_compose(QD_PERFORMATIVE_FOOTER, 0);
    qd_compose_start_map(field);
    qd_compose_insert_symbol(field, "Key1");
    qd_compose_insert_string(field, "Value1");
    qd_compose_insert_symbol(field, "Key2");
    qd_compose_insert_string(field, "Value2");
    qd_compose_end_map(field);
    qd_message_extend(msg, field, 0);
    qd_compose_free(field);

    qd_message_set_receive_complete(msg);

    // "forward" the message
    out_msg = qd_message_copy(msg);

    // walk the data streams...
    int bd_count = 0;
    int body_buffers = 0;
    qd_message_stream_data_t *stream_data = 0;
    bool done = false;
    int footer_found = 0;
    while (!done) {
        switch (qd_message_next_stream_data(out_msg, &stream_data)) {
        case QD_MESSAGE_STREAM_DATA_INCOMPLETE:
        case QD_MESSAGE_STREAM_DATA_INVALID:
            result = "Next body data failed to get next body data";
            goto exit;
        case QD_MESSAGE_STREAM_DATA_NO_MORE:
            done = true;
            break;
        case QD_MESSAGE_STREAM_DATA_FOOTER_OK:
            bd_count += 1;
            footer_found += 1;
            qd_message_stream_data_release(stream_data);
            break;
        case QD_MESSAGE_STREAM_DATA_BODY_OK:
            bd_count += 1;
            // qd_message_stream_data_append() breaks the buffer list up into
            // smaller lists that are no bigger than QD_QLIMIT_Q2_LOWER buffers
            // long
            body_buffers += qd_message_stream_data_buffer_count(stream_data);
            if (qd_message_stream_data_buffer_count(stream_data) >= QD_QLIMIT_Q2_LOWER) {
                result = "Body data list length too long!";
                goto exit;
            }
            qd_message_stream_data_release(stream_data);
            break;
        }
    }

    // verify:

    if (body_bufct != body_buffers) {
        result = "Not all body data buffers were decoded!";
        goto exit;
    }

    if (footer_found != 1) {
        result = "I ordered a side of 'footer' with that message!";
        goto exit;
    }

    // +2 for 1 extra 5 buffers and 1 for footer
    if (bd_count != (body_bufct / QD_QLIMIT_Q2_LOWER) + 2) {
        result = "Unexpected count of body data sections!";
        goto exit;
    }

    // expect: free all the body and footer buffers except for the very last
    // buffer.  Remember kids: perfect is good, but done is better.
    if (DEQ_SIZE(MSG_CONTENT(out_msg)->buffers) != base_bufct + 1) {
        result = "Possible buffer leak detected!";
        goto exit;
    }

    // and Q2 should be unblocked
    if (qd_message_is_Q2_blocked(msg)) {
        result = "Q2 expected to be unblocked!";
        goto exit;
    }

    if (unblock_called != 1) {
        result = "Q2 unblock handler not called!";
        goto exit;
    }


exit:
    qd_message_free(msg);
    qd_message_free(out_msg);
    return result;
}


// Verify that decoding streaming body data across two
// "outgoing" messages works
static char *test_check_stream_data_fanout(void *context)
{
    char *result = 0;
    qd_message_t *in_msg = 0;
    qd_message_t *out_msg1 = 0;
    qd_message_t *out_msg2 = 0;

    // simulate building a message as an adaptor would:

    qd_composed_field_t *header = qd_compose(QD_PERFORMATIVE_HEADER, 0);
    qd_compose_start_list(header);
    qd_compose_insert_bool(header, 0);     // durable
    qd_compose_insert_null(header);        // priority
    qd_compose_end_list(header);

    qd_composed_field_t *props = qd_compose(QD_PERFORMATIVE_PROPERTIES, 0);
    qd_compose_start_list(props);
    qd_compose_insert_ulong(props, 666);    // message-id
    qd_compose_insert_null(props);                 // user-id
    qd_compose_insert_string(props, "/whereevah"); // to
    qd_compose_insert_string(props, "my-subject");  // subject
    qd_compose_insert_string(props, "/reply-to");   // reply-to
    qd_compose_end_list(props);

    in_msg = qd_message_compose(header, props, 0, false);

    // snapshot the message buffer count to use as a baseline
    const size_t base_bufct = DEQ_SIZE(MSG_CONTENT(in_msg)->buffers);

    // construct a couple of body data sections, cheek-to-jowl in a buffer
    // chain
#define sd_count  5
    qd_composed_field_t *field = qd_compose(QD_PERFORMATIVE_BODY_DATA, 0);
    memset(buffer, '1', 99);
    qd_compose_insert_binary(field, buffer, 99);

    field = qd_compose(QD_PERFORMATIVE_BODY_DATA, field);
    memset(buffer, '2', 1);
    qd_compose_insert_binary(field, buffer, 1);

    field = qd_compose(QD_PERFORMATIVE_BODY_DATA, field);
    memset(buffer, '3', 1);
    qd_compose_insert_binary(field, buffer, 1);

    field = qd_compose(QD_PERFORMATIVE_BODY_DATA, field);
    memset(buffer, '4', 1001);
    qd_compose_insert_binary(field, buffer, 1001);

    field = qd_compose(QD_PERFORMATIVE_BODY_DATA, field);
    memset(buffer, '5', 1001);
    qd_compose_insert_binary(field, buffer, 5);

    qd_message_extend(in_msg, field, 0);
    qd_compose_free(field);

    qd_message_set_receive_complete(in_msg);

    // "fan out" the message
    out_msg1 = qd_message_copy(in_msg);
    qd_message_add_fanout(out_msg1);
    out_msg2 = qd_message_copy(in_msg);
    qd_message_add_fanout(out_msg2);

    // walk the data streams for both messages:
    qd_message_stream_data_t *out_sd1[sd_count] = {0};
    qd_message_stream_data_t *out_sd2[sd_count] = {0};

    qd_message_stream_data_t *stream_data = 0;
    bool done = false;
    int index = 0;
    while (!done) {
        switch (qd_message_next_stream_data(out_msg1, &stream_data)) {
        case QD_MESSAGE_STREAM_DATA_NO_MORE:
            done = true;
            break;
        case QD_MESSAGE_STREAM_DATA_BODY_OK:
            out_sd1[index++] = stream_data;
            break;
        default:
            result = "Next body data failed to get next body data";
            goto exit;
        }
    }
    if (index != sd_count) {
        result = "wrong stream data count out1";
        goto exit;
    }

    index = 0;
    done = false;
    while (!done) {
        switch (qd_message_next_stream_data(out_msg2, &stream_data)) {
        case QD_MESSAGE_STREAM_DATA_NO_MORE:
            done = true;
            break;
        case QD_MESSAGE_STREAM_DATA_BODY_OK:
            out_sd2[index++] = stream_data;
            break;
        default:
            result = "Next body data failed to get next body data";
            goto exit;
        }
    }
    if (index != sd_count) {
        result = "wrong stream data count out2";
        goto exit;
    }

    // now free each one in opposite order (evil, yes?)
    for (index = 0; index < sd_count; ++index) {
        qd_message_stream_data_release(out_sd1[index]);
        qd_message_stream_data_release(out_sd2[(sd_count - 1) - index]);
    }

    // expect: all but the last body buffer is freed:
    if (DEQ_SIZE(MSG_CONTENT(out_msg1)->buffers) != base_bufct + 1
        || DEQ_SIZE(MSG_CONTENT(out_msg2)->buffers) != base_bufct + 1) {
        result = "Possible buffer leak detected!";
        goto exit;
    }

exit:
    qd_message_free(in_msg);
    qd_message_free(out_msg1);
    qd_message_free(out_msg2);
    return result;
}


// Verify that decoding a message that has only a footer (no body data)
// messages works
static char *test_check_stream_data_footer(void *context)
{
    char *result = 0;
    qd_message_t *in_msg = 0;
    qd_message_t *out_msg1 = 0;
    qd_message_t *out_msg2 = 0;

    // simulate building a message as an adaptor would:

    qd_composed_field_t *field = qd_compose(QD_PERFORMATIVE_HEADER, 0);
    qd_compose_start_list(field);
    qd_compose_insert_bool(field, 0);     // durable
    qd_compose_insert_null(field);        // priority
    qd_compose_end_list(field);
    field = qd_compose(QD_PERFORMATIVE_PROPERTIES, field);
    qd_compose_start_list(field);
    qd_compose_insert_ulong(field, 666);    // message-id
    qd_compose_insert_null(field);                 // user-id
    qd_compose_insert_string(field, "/whereevah"); // to
    qd_compose_insert_string(field, "my-subject");  // subject
    qd_compose_insert_string(field, "/reply-to");   // reply-to
    qd_compose_end_list(field);

    in_msg = qd_message_compose(field, 0, 0, false);

    // snapshot the message buffer count to use as a baseline
    const size_t base_bufct = DEQ_SIZE(MSG_CONTENT(in_msg)->buffers);

    // Append a footer
    bool q2_blocked;
    field = qd_compose(QD_PERFORMATIVE_FOOTER, 0);
    qd_compose_start_map(field);
    qd_compose_insert_symbol(field, "Key1");
    qd_compose_insert_string(field, "Value1");
    qd_compose_insert_symbol(field, "Key2");
    qd_compose_insert_string(field, "Value2");
    qd_compose_end_map(field);
    qd_message_extend(in_msg, field, &q2_blocked);
    qd_compose_free(field);

    // this small message should not have triggered Q2
    assert(DEQ_SIZE(MSG_CONTENT(in_msg)->buffers) < QD_QLIMIT_Q2_UPPER);
    if (q2_blocked) {
        result = "Unexpected Q2 block on message extend";
        goto exit;
    }

    qd_message_set_receive_complete(in_msg);

    // "fan out" the message
    out_msg1 = qd_message_copy(in_msg);
    qd_message_add_fanout(out_msg1);
    out_msg2 = qd_message_copy(in_msg);
    qd_message_add_fanout(out_msg2);

    qd_message_stream_data_t *stream_data = 0;
    bool done = false;
    bool footer = false;
    while (!done) {
        switch (qd_message_next_stream_data(out_msg1, &stream_data)) {
        case QD_MESSAGE_STREAM_DATA_NO_MORE:
            done = true;
            break;
        case QD_MESSAGE_STREAM_DATA_FOOTER_OK:
            footer = true;
            qd_message_stream_data_release(stream_data);
            break;
        case QD_MESSAGE_STREAM_DATA_BODY_OK:
            result = "Unexpected body data present";
            goto exit;
        default:
            result = "Next body data failed to get next body data";
            goto exit;
        }
    }
    if (!footer) {
        result = "No footer found in out_msg1";
        goto exit;
    }

    done = false;
    footer = false;
    while (!done) {
        switch (qd_message_next_stream_data(out_msg2, &stream_data)) {
        case QD_MESSAGE_STREAM_DATA_NO_MORE:
            done = true;
            break;
        case QD_MESSAGE_STREAM_DATA_FOOTER_OK:
            footer = true;
            qd_message_stream_data_release(stream_data);
            break;
        case QD_MESSAGE_STREAM_DATA_BODY_OK:
            result = "Unexpected body data present";
            goto exit;
        default:
            result = "Next body data failed to get next body data";
            goto exit;
        }
    }
    if (!footer) {
        result = "No footer found in out_msg2";
        goto exit;
    }

    // expect: all but the last body buffer is freed:
    if (DEQ_SIZE(MSG_CONTENT(out_msg1)->buffers) != base_bufct + 1
        || DEQ_SIZE(MSG_CONTENT(out_msg2)->buffers) != base_bufct + 1) {
        result = "Possible buffer leak detected!";
        goto exit;
    }

exit:
    qd_message_free(in_msg);
    qd_message_free(out_msg1);
    qd_message_free(out_msg2);
    return result;
}


static char *test_q2_callback_on_disable(void *context)
{
    char *result = 0;
    qd_message_t *msg = 0;
    int unblock_called = 0;

    // first test: ensure calling disable without being in Q2 does not invoke the
    // handler:

    qd_composed_field_t *field = qd_compose(QD_PERFORMATIVE_HEADER, 0);
    qd_compose_start_list(field);
    qd_compose_insert_bool(field, 0);     // durable
    qd_compose_insert_null(field);        // priority
    qd_compose_end_list(field);
    field = qd_compose(QD_PERFORMATIVE_PROPERTIES, field);
    qd_compose_start_list(field);
    qd_compose_insert_ulong(field, 666);    // message-id
    qd_compose_insert_null(field);                 // user-id
    qd_compose_insert_string(field, "/whereevah"); // to
    qd_compose_insert_string(field, "my-subject");  // subject
    qd_compose_insert_string(field, "/reply-to");   // reply-to
    qd_compose_end_list(field);

    msg = qd_message_compose(field, 0, 0, false);

    qd_alloc_safe_ptr_t unblock_arg = {0};
    unblock_arg.ptr = (void*) &unblock_called;
    qd_message_set_q2_unblocked_handler(msg, q2_unblocked_handler, unblock_arg);


    qd_message_Q2_holdoff_disable(msg);

    if (unblock_called != 0) {
        result = "Unexpected call to Q2 unblock handler!";
        goto exit;
    }

    qd_message_free(msg);

    // now try it again with a message with Q2 active

    field = qd_compose(QD_PERFORMATIVE_HEADER, 0);
    qd_compose_start_list(field);
    qd_compose_insert_bool(field, 0);     // durable
    qd_compose_insert_null(field);        // priority
    qd_compose_end_list(field);
    field = qd_compose(QD_PERFORMATIVE_PROPERTIES, field);
    qd_compose_start_list(field);
    qd_compose_insert_ulong(field, 666);    // message-id
    qd_compose_insert_null(field);                 // user-id
    qd_compose_insert_string(field, "/whereevah"); // to
    qd_compose_insert_string(field, "my-subject");  // subject
    qd_compose_insert_string(field, "/reply-to");   // reply-to
    qd_compose_end_list(field);

    msg = qd_message_compose(field, 0, 0, false);
    unblock_arg.ptr = (void*) &unblock_called;
    qd_message_set_q2_unblocked_handler(msg, q2_unblocked_handler, unblock_arg);

    // grow message until Q2 activates

    bool blocked = false;
    uint8_t data[1000] = {0};
    while (!blocked) {
        qd_buffer_list_t bin_data = DEQ_EMPTY;
        qd_buffer_list_append(&bin_data, data, sizeof(data));
        qd_message_stream_data_append(msg, &bin_data, &blocked);
    }

    // now ensure callback is made

    qd_message_Q2_holdoff_disable(msg);

    if (unblock_called != 1) {
        result = "Failed to invoke unblock handler";
        goto exit;
    }


exit:
    qd_message_free(msg);
    return result;
}


// Ensure that the Q2 calculation does not include header buffers.  Header
// buffers are held until the message is freed, so they should not be a factor
// in flow control (DISPATCH-2191).
//
static char *test_q2_ignore_headers(void *context)
{
    char *result = 0;
    qd_message_t *msg = qd_message();
    qd_message_content_t *content = MSG_CONTENT(msg);

    // create a message and add a bunch of headers.  Put each header in its own
    // buffer to increase the buffer count.

    qd_composed_field_t *field = qd_compose(QD_PERFORMATIVE_HEADER, 0);
    qd_compose_start_list(field);
    qd_compose_insert_bool(field, 0);     // durable
    qd_compose_insert_null(field);        // priority
    qd_compose_end_list(field);
    qd_buffer_list_t field_buffers;
    qd_compose_take_buffers(field, &field_buffers);
    qd_compose_free(field);
    content->buffers = field_buffers;

    const qd_amqp_performative_t plist[3] = {
        QD_PERFORMATIVE_DELIVERY_ANNOTATIONS,
        QD_PERFORMATIVE_MESSAGE_ANNOTATIONS,
        QD_PERFORMATIVE_APPLICATION_PROPERTIES};

    for (int i = 0; i < 3; ++i) {
        field = qd_compose(plist[i], 0);
        qd_compose_start_map(field);
        qd_compose_insert_symbol(field, "Key");
        qd_compose_insert_string(field, "Value");
        qd_compose_end_map(field);
        qd_compose_take_buffers(field, &field_buffers);
        qd_compose_free(field);
        DEQ_APPEND(content->buffers, field_buffers);
    }

    // validate the message - this will mark the buffers that contain header
    // data
    if (qd_message_check_depth(msg, QD_DEPTH_APPLICATION_PROPERTIES) != QD_MESSAGE_DEPTH_OK) {
        result = "Unexpected depth check failure";
        goto exit;
    }

    const size_t header_ct = DEQ_SIZE(content->buffers);
    assert(header_ct);
    assert(!_Q2_holdoff_should_block_LH(content));

    // Now append buffers until Q2 blocks
    while (!_Q2_holdoff_should_block_LH(content)) {
        qd_buffer_t *buffy = qd_buffer();
        qd_buffer_insert(buffy, qd_buffer_capacity(buffy));
        DEQ_INSERT_TAIL(content->buffers, buffy);
    }

    // expect: block occurs when length == QD_QLIMIT_Q2_UPPER + header_ct
    if (DEQ_SIZE(content->buffers) != QD_QLIMIT_Q2_UPPER + header_ct) {
        result = "Wrong buffer length for Q2 activate!";
        goto exit;
    }

    // now remove buffers until Q2 is relieved

    while (!_Q2_holdoff_should_unblock_LH(content)) {
        qd_buffer_t *buffy = DEQ_TAIL(content->buffers);
        DEQ_REMOVE_TAIL(content->buffers);
        qd_buffer_free(buffy);
    }

    // expect: Q2 deactivates when list length < QD_QDLIMIT_Q2_LOWER + header_ct
    if (DEQ_SIZE(content->buffers) != (QD_QLIMIT_Q2_LOWER + header_ct) - 1) {
        result = "Wrong buffer length for Q2 deactivate!";
        goto exit;
    }

exit:

    qd_message_free(msg);
    return result;
}


// verify that a locally generated message containing message annotations can
// be correctly parsed
static char *test_local_message_compose(void * context)
{
    char *result = 0;
    qd_composed_field_t *header = qd_compose(QD_PERFORMATIVE_HEADER, 0);
    qd_compose_start_list(header);
    qd_compose_insert_bool(header, true);  // durable
    qd_compose_end_list(header);

    qd_composed_field_t *da = qd_compose(QD_PERFORMATIVE_DELIVERY_ANNOTATIONS, 0);
    qd_compose_start_map(da);
    qd_compose_insert_symbol(da, "key1");
    qd_compose_insert_string(da, "value1");
    qd_compose_end_map(da);

    qd_composed_field_t *ma = qd_compose(QD_PERFORMATIVE_MESSAGE_ANNOTATIONS, 0);
    qd_compose_start_map(ma);

    qd_compose_insert_symbol(ma, "User Key 1");
    qd_compose_insert_string(ma, "User Value 1");

    qd_compose_insert_symbol(ma, "User Key 2");
    qd_compose_insert_string(ma, "User Value 2");

    qd_compose_insert_symbol(ma, QD_MA_INGRESS);
    qd_compose_insert_string(ma, "0/InRouter");

    qd_compose_insert_symbol(ma, QD_MA_TRACE);
    qd_compose_start_list(ma);
    qd_compose_insert_string(ma, "1/Router");
    qd_compose_insert_string(ma, "2/Router");
    qd_compose_end_list(ma);

    qd_compose_insert_symbol(ma, QD_MA_TO);
    qd_compose_insert_string(ma, "address1");

    qd_compose_insert_symbol(ma, QD_MA_PHASE);
    qd_compose_insert_int(ma, 7);

    qd_compose_insert_symbol(ma, QD_MA_STREAM);
    qd_compose_insert_int(ma, 1);

    qd_compose_end_map(ma);

    qd_message_t *msg = qd_message_compose(header, da, ma, true);
    qd_message_content_t *content = MSG_CONTENT(msg);

    // verify that the internals of the content have been properly initialized.
    // It should appear as if the message has arrived from proton like any
    // other message.

    if (!content->section_message_header.parsed) {
        result = "Header section not parsed";
        goto exit;
    }

    if (!content->section_delivery_annotation.parsed) {
        result = "Delivery Annotation section not parsed";
        goto exit;
    }

    if (!content->section_message_annotation.parsed || !content->ma_parsed) {
        result = "Message Annotation section not parsed";
        goto exit;
    }

    if (content->ma_user_count != 4) {
        result = "failed to find user message annotations";
        goto exit;
    }

    if (content->ma_user_annotations.remaining != 52) {
        result = "wrong length of user annotations";
        goto exit;
    }

    if (!content->ma_pf_ingress
        || !qd_iterator_equal(qd_parse_raw(content->ma_pf_ingress),
                              (const unsigned char*) "0/InRouter")) {
        result = "ingress MA not correct";
        goto exit;
    }

    if (!content->ma_pf_to_override
        || !qd_iterator_equal(qd_parse_raw(content->ma_pf_to_override),
                              (const unsigned char*) "address1")) {
        result = "to-override MA not correct";
        goto exit;
    }

    if (!content->ma_pf_trace
        || qd_parse_sub_count(content->ma_pf_trace) != 2
        || !qd_iterator_equal(qd_parse_raw(qd_parse_sub_value(content->ma_pf_trace, 0)),
                              (const unsigned char*) "1/Router")
        || !qd_iterator_equal(qd_parse_raw(qd_parse_sub_value(content->ma_pf_trace, 1)),
                              (const unsigned char*) "2/Router")) {
        result = "Invalid trace list";
        goto exit;
    }

    if (((qd_message_pvt_t *)msg)->ma_phase != 7) {
        result = "incorrect phase MA";
        goto exit;
    }

    if (!((qd_message_pvt_t *)msg)->ma_streaming) {
        result = "incorrect streaming MA";
        goto exit;
    }

exit:

    qd_message_free(msg);
    return result;
}

int message_tests(void)
{
    int result = 0;
    char *test_group = "message_tests";

    TEST_CASE(test_send_to_messenger, 0);
    TEST_CASE(test_receive_from_messenger, 0);
    TEST_CASE(test_message_properties, 0);
    TEST_CASE(test_check_multiple, 0);
    TEST_CASE(test_parse_message_annotations, 0);
    TEST_CASE(test_q2_input_holdoff_sensing, 0);
    TEST_CASE(test_incomplete_annotations, 0);
    TEST_CASE(test_check_weird_messages, 0);
    TEST_CASE(test_check_stream_data, 0);
    TEST_CASE(test_check_stream_data_append, 0);
    TEST_CASE(test_check_stream_data_fanout, 0);
    TEST_CASE(test_check_stream_data_footer, 0);
    TEST_CASE(test_q2_callback_on_disable, 0);
    TEST_CASE(test_q2_ignore_headers, 0);
    TEST_CASE(test_local_message_compose, 0);

    return result;
}


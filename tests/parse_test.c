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

#define _GNU_SOURCE
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <inttypes.h>
#include "test_case.h"
#include <qpid/dispatch.h>
#include <qpid/dispatch/trace_mask.h>

struct fs_vector_t {
    const char *data;
    int         length;
    uint8_t     expected_tag;
    int         check_uint;
    int         check_ulong;
    int         check_int;
    int         check_long;
    uint64_t    expected_ulong;
    int64_t     expected_long;
} fs_vectors[] = {
{"\x40",                 1, QD_AMQP_NULL,       0, 0, 0, 0, 0, 0},           // 0
{"\x41",                 1, QD_AMQP_TRUE,       1, 0, 0, 0, 1, 0},           // 1
{"\x42",                 1, QD_AMQP_FALSE,      1, 0, 0, 0, 0, 0},           // 2
{"\x56\x00",             2, QD_AMQP_BOOLEAN,    1, 0, 0, 0, 0, 0},           // 3
{"\x56\x01",             2, QD_AMQP_BOOLEAN,    1, 0, 0, 0, 1, 0},           // 4
{"\x50\x45",             2, QD_AMQP_UBYTE,      1, 0, 0, 0, 0x45, 0},        // 5
{"\x60\x02\x04",         3, QD_AMQP_USHORT,     1, 0, 0, 0, 0x0204, 0},      // 6
{"\x70\x01\x02\x03\x04", 5, QD_AMQP_UINT,       1, 0, 0, 0, 0x01020304, 0},  // 7
{"\x52\x06",             2, QD_AMQP_SMALLUINT,  1, 0, 0, 0, 6, 0},           // 8
{"\x43",                 1, QD_AMQP_UINT0,      1, 0, 0, 0, 0, 0},           // 9
{"\x80\x01\x02\x03\x04\x05\x06\x07\x08",
                         9, QD_AMQP_ULONG,      0, 1, 0, 0, 0x0102030405060708, 0},  // 10
{"\x53\x08",             2, QD_AMQP_SMALLULONG, 0, 1, 0, 0, 0x08, 0},                // 11
{"\x44",                 1, QD_AMQP_ULONG0,     0, 1, 0, 0, 0, 0},                   // 12
{"\x71\x01\x02\x03\x04", 5, QD_AMQP_INT,        0, 0, 1, 0, 0, 0x01020304},          // 13
{"\x54\x02",             2, QD_AMQP_SMALLINT,   0, 0, 1, 0, 0, 2},                   // 14
{"\x81\x01\x02\x03\x04\x05\x06\x07\x08",
                         9, QD_AMQP_LONG,       0, 0, 0, 1, 0, 0x0102030405060708},  // 15
{"\x55\x08",             2, QD_AMQP_SMALLLONG,  0, 0, 0, 1, 0, 0x08},                // 16
{"\x45",                 1, QD_AMQP_LIST0,      0, 0, 0, 0, 0, 0},                   // 17
{0, 0, 0, 0, 0}
};


static char *test_parser_fixed_scalars(void *context)
{
    int idx = 0;
    static char error[1024];

    while (fs_vectors[idx].data) {
        qd_iterator_t *field  = qd_iterator_binary(fs_vectors[idx].data,
                                                   fs_vectors[idx].length, ITER_VIEW_ALL);
        qd_parsed_field_t *parsed = qd_parse(field);

        qd_iterator_t *typed_iter = qd_parse_typed(parsed);

        int length = qd_iterator_length(typed_iter);

        if (length != fs_vectors[idx].length)
            return "Length of typed iterator does not match actual length";

        if (!qd_parse_ok(parsed)) return "Unexpected Parse Error";
        if (qd_parse_tag(parsed) != fs_vectors[idx].expected_tag) {
            sprintf(error, "(%d) Tag: Expected %02x, Got %02x", idx,
                    fs_vectors[idx].expected_tag, qd_parse_tag(parsed));
            return error;
        }
        if (fs_vectors[idx].check_uint &&
            qd_parse_as_uint(parsed) != fs_vectors[idx].expected_ulong) {
            sprintf(error, "(%d) UINT: Expected %"PRIx64", Got %"PRIx32, idx,
                    fs_vectors[idx].expected_ulong, qd_parse_as_uint(parsed));
            return error;
        }
        if (fs_vectors[idx].check_ulong &&
            qd_parse_as_ulong(parsed) != fs_vectors[idx].expected_ulong) {
            sprintf(error, "(%d) ULONG: Expected %"PRIx64", Got %"PRIx64, idx,
                    fs_vectors[idx].expected_ulong, qd_parse_as_ulong(parsed));
            return error;
        }
        if (fs_vectors[idx].check_int &&
            qd_parse_as_int(parsed) != fs_vectors[idx].expected_long) {
            sprintf(error, "(%d) INT: Expected %"PRIx64", Got %"PRIx32, idx,
                    fs_vectors[idx].expected_long, qd_parse_as_int(parsed));
            return error;
        }
        if (fs_vectors[idx].check_long &&
            qd_parse_as_long(parsed) != fs_vectors[idx].expected_long) {
            sprintf(error, "(%d) LONG: Expected %"PRIx64", Got %"PRIx64, idx,
                    fs_vectors[idx].expected_long, qd_parse_as_long(parsed));
            return error;
        }
        idx++;

        qd_iterator_free(field);
        qd_parse_free(parsed);
    }

    return 0;
}


static char *test_map(void *context)
{
    static char error[1000];
    const char *data =
        "\xd1\x00\x00\x00\x2d\x00\x00\x00\x06"    // map32, 6 items
        "\xa3\x05\x66irst\xa1\x0evalue_of_first"  // (23) "first":"value_of_first"
        "\xa3\x06second\x52\x20"                  // (10) "second":32
        "\xa3\x05third\x41";                      // (8)  "third":true
    int data_len = 50;

    qd_iterator_t     *data_iter = qd_iterator_binary(data, data_len, ITER_VIEW_ALL);
    qd_parsed_field_t *field     = qd_parse(data_iter);

    if (!qd_parse_ok(field)) {
        snprintf(error, 1000, "Parse failed: %s", qd_parse_error(field));
        qd_iterator_free(data_iter);
        qd_parse_free(field);
        return error;
    }

    if (!qd_parse_is_map(field)) {
        qd_iterator_free(data_iter);
        qd_parse_free(field);
        return "Expected field to be a map";
    }

    uint32_t count = qd_parse_sub_count(field);
    if (count != 3) {
        snprintf(error, 1000, "Expected sub-count==3, got %"PRIu32, count);
        qd_iterator_free(data_iter);
        qd_parse_free(field);
        return error;
    }

    qd_parsed_field_t *key_field  = qd_parse_sub_key(field, 0);
    qd_iterator_t     *key_iter   = qd_parse_raw(key_field);
    qd_iterator_t     *typed_iter = qd_parse_typed(key_field);
    if (!qd_iterator_equal(key_iter, (unsigned char*) "first")) {
        unsigned char     *result   = qd_iterator_copy(key_iter);
        snprintf(error, 1000, "First key: expected 'first', got '%s'", result);
        free (result);
        return error;
    }

    if (!qd_iterator_equal(typed_iter, (unsigned char*) "\xa3\x05\x66irst"))
        return "Incorrect typed iterator on first-key";

    qd_parsed_field_t *val_field = qd_parse_sub_value(field, 0);
    qd_iterator_t     *val_iter  = qd_parse_raw(val_field);
    typed_iter = qd_parse_typed(val_field);
    if (!qd_iterator_equal(val_iter, (unsigned char*) "value_of_first")) {
        unsigned char     *result   = qd_iterator_copy(val_iter);
        snprintf(error, 1000, "First value: expected 'value_of_first', got '%s'", result);
        free (result);
        return error;
    }

    if (!qd_iterator_equal(typed_iter, (unsigned char*) "\xa1\x0evalue_of_first"))
        return "Incorrect typed iterator on first-key";

    key_field = qd_parse_sub_key(field, 1);
    key_iter  = qd_parse_raw(key_field);
    if (!qd_iterator_equal(key_iter, (unsigned char*) "second")) {
        unsigned char     *result   = qd_iterator_copy(key_iter);
        snprintf(error, 1000, "Second key: expected 'second', got '%s'", result);
        free (result);
        return error;
    }

    val_field = qd_parse_sub_value(field, 1);
    if (qd_parse_as_uint(val_field) != 32) {
        snprintf(error, 1000, "Second value: expected 32, got %"PRIu32, qd_parse_as_uint(val_field));
        return error;
    }

    key_field = qd_parse_sub_key(field, 2);
    key_iter  = qd_parse_raw(key_field);
    if (!qd_iterator_equal(key_iter, (unsigned char*) "third")) {
        unsigned char     *result   = qd_iterator_copy(key_iter);
        snprintf(error, 1000, "Third key: expected 'third', got '%s'", result);
        free (result);
        return error;
    }

    val_field = qd_parse_sub_value(field, 2);
    if (!qd_parse_as_bool(val_field)) {
        snprintf(error, 1000, "Third value: expected true");
        return error;
    }

    qd_iterator_free(data_iter);
    qd_parse_free(field);
    return 0;
}


struct err_vector_t {
    const char *data;
    int         length;
    const char *expected_error;
} err_vectors[] = {
{"",                 0, "Insufficient Data to Determine Tag"},  // 0
{"\x21",             1, "Invalid Tag - No Length Information"}, // 1
//{"\x56",             1, "w"},           // 2
{"\xa0",             1, "Insufficient Data to Determine Length"},        // 3
{"\xb0",             1, "Insufficient Data to Determine Length"},        // 4
{"\xb0\x00",         2, "Insufficient Data to Determine Length"},        // 5
{"\xb0\x00\x00",     3, "Insufficient Data to Determine Length"},        // 6
{"\xb0\x00\x00\x00", 4, "Insufficient Data to Determine Length"},        // 7
{"\xc0\x04",         2, "Insufficient Data to Determine Count"},         // 8
{"\xd0\x00\x00\x00\x00\x00\x00\x00\x01",  9, "Insufficient Length to Determine Count"}, // 9
{0, 0, 0}
};

static char *test_parser_errors(void *context)
{
    int idx = 0;
    static char error[1024];

    while (err_vectors[idx].data) {
        qd_iterator_t *field  = qd_iterator_binary(err_vectors[idx].data,
                                                   err_vectors[idx].length, ITER_VIEW_ALL);
        qd_parsed_field_t *parsed = qd_parse(field);
        if (qd_parse_ok(parsed)) {
            sprintf(error, "(%d) Unexpected Parse Success", idx);
            return error;
        }
        if (strcmp(qd_parse_error(parsed), err_vectors[idx].expected_error) != 0) {
            sprintf(error, "(%d) Error: Expected %s, Got %s", idx,
                    err_vectors[idx].expected_error, qd_parse_error(parsed));
            return error;
        }
        qd_parse_free(parsed);
        qd_iterator_free(field);
        idx++;
    }

    return 0;
}


static char *test_tracemask(void *context)
{
    qd_bitmask_t    *bm = NULL;
    qd_tracemask_t  *tm = qd_tracemask();
    qd_buffer_list_t list;
    static char      error[1024];

    qd_iterator_set_address("0", "ROUTER");

    qd_tracemask_add_router(tm, "amqp:/_topo/0/Router.A", 0);
    qd_tracemask_add_router(tm, "amqp:/_topo/0/Router.B", 1);
    qd_tracemask_add_router(tm, "amqp:/_topo/0/Router.C", 2);
    qd_tracemask_add_router(tm, "amqp:/_topo/0/Router.D", 3);
    qd_tracemask_add_router(tm, "amqp:/_topo/0/Router.E", 4);
    qd_tracemask_add_router(tm, "amqp:/_topo/0/Router.F", 5);

    qd_tracemask_set_link(tm, 0, 4);
    qd_tracemask_set_link(tm, 3, 10);
    qd_tracemask_set_link(tm, 4, 3);
    qd_tracemask_set_link(tm, 5, 2);

    qd_composed_field_t *comp = qd_compose_subfield(0);
    qd_compose_start_list(comp);
    qd_compose_insert_string(comp, "0/Router.A");
    qd_compose_insert_string(comp, "0/Router.D");
    qd_compose_insert_string(comp, "0/Router.E");
    qd_compose_end_list(comp);

    DEQ_INIT(list);
    qd_compose_take_buffers(comp, &list);
    qd_compose_free(comp);

    int length = 0;
    qd_buffer_t *buf = DEQ_HEAD(list);
    while (buf) {
        length += qd_buffer_size(buf);
        buf = DEQ_NEXT(buf);
    }

    qd_iterator_t     *iter = qd_iterator_buffer(DEQ_HEAD(list), 0, length, ITER_VIEW_ALL);
    qd_parsed_field_t *pf   = qd_parse(iter);
    qd_iterator_free(iter);

    int ingress = -1;

    bm = qd_tracemask_create(tm, pf, &ingress);
    if (qd_bitmask_cardinality(bm) != 3) {
        sprintf(error, "Expected cardinality of 3, got %d", qd_bitmask_cardinality(bm));
        return error;
    }
    if (ingress != 0) {
        sprintf(error, "(A) Expected ingress index of 0, got %d", ingress);
        return error;
    }
    int total = 0;
    int bit, c;
    for (QD_BITMASK_EACH(bm, bit, c)) {
        total += bit;
    }
    if (total != 17) {
        sprintf(error, "Expected total bit value of 17, got %d", total);
        return error;
    }

    qd_bitmask_free(bm);
    qd_tracemask_del_router(tm, 3);
    qd_tracemask_remove_link(tm, 0);

    ingress = -1;
    bm = qd_tracemask_create(tm, pf, &ingress);
    qd_parse_free(pf);
    if (qd_bitmask_cardinality(bm) != 1) {
        sprintf(error, "Expected cardinality of 1, got %d", qd_bitmask_cardinality(bm));
        return error;
    }
    if (ingress != 0) {
        sprintf(error, "(B) Expected ingress index of 0, got %d", ingress);
        return error;
    }

    total = 0;
    for (QD_BITMASK_EACH(bm, bit, c)) {
        total += bit;
    }
    if (total != 3) {
        sprintf(error, "Expected total bit value of 3, got %d", total);
        return error;
    }

    qd_tracemask_free(tm);
    qd_bitmask_free(bm);
    for (qd_buffer_t *buf = DEQ_HEAD(list); buf; buf = DEQ_HEAD(list)) {
        DEQ_REMOVE_HEAD(list);
        qd_buffer_free(buf);
    }
    return 0;
}


int parse_tests()
{
    int result = 0;
    char *test_group = "parse_tests";

    TEST_CASE(test_parser_fixed_scalars, 0);
    TEST_CASE(test_map, 0);
    TEST_CASE(test_parser_errors, 0);
    TEST_CASE(test_tracemask, 0);

    return result;
}


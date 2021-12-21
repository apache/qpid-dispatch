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
#include "test_case.h"

#include "qpid/dispatch.h"
#include "qpid/dispatch/trace_mask.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

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
{"\x70\xff\xff\xff\xff", 5, QD_AMQP_UINT,       1, 0, 0, 0, UINT32_MAX, 0},          // 18

{"\x71\x7f\xff\xff\xff", 5, QD_AMQP_INT,        0, 0, 1, 0, 0, INT32_MAX},           // 19
{"\x71\x7f\xff\xff\xff", 5, QD_AMQP_INT,        0, 0, 0, 1, 0, INT32_MAX},           // 20
{"\x71\x80\x00\x00\x00", 5, QD_AMQP_INT,        0, 0, 1, 0, 0, INT32_MIN},           // 21
{"\x71\x80\x00\x00\x00", 5, QD_AMQP_INT,        0, 0, 0, 1, 0, INT32_MIN},           // 22

{"\x51\x7f",             2, QD_AMQP_BYTE,       0, 0, 1, 0, 0, INT8_MAX},            // 23
{"\x51\x7f",             2, QD_AMQP_BYTE,       0, 0, 0, 1, 0, INT8_MAX},            // 24
{"\x51\x80",             2, QD_AMQP_BYTE,       0, 0, 1, 0, 0, INT8_MIN},            // 25
{"\x51\x80",             2, QD_AMQP_BYTE,       0, 0, 0, 1, 0, INT8_MIN},            // 26

{"\x61\x7f\xff",         3, QD_AMQP_SHORT,      0, 0, 1, 0, 0, INT16_MAX},           // 27
{"\x61\x7f\xff",         3, QD_AMQP_SHORT,      0, 0, 0, 1, 0, INT16_MAX},           // 28
{"\x61\x80\x00",         3, QD_AMQP_SHORT,      0, 0, 1, 0, 0, INT16_MIN},           // 29
{"\x61\x80\x00",         3, QD_AMQP_SHORT,      0, 0, 0, 1, 0, INT16_MIN},           // 30

{"\x54\x7f",             2, QD_AMQP_SMALLINT,   0, 0, 1, 0, 0, INT8_MAX},            // 31
{"\x54\x7f",             2, QD_AMQP_SMALLINT,   0, 0, 0, 1, 0, INT8_MAX},            // 32
{"\x54\x80",             2, QD_AMQP_SMALLINT,   0, 0, 1, 1, 0, INT8_MIN},            // 33
{"\x54\x80",             2, QD_AMQP_SMALLINT,   0, 0, 0, 1, 0, INT8_MIN},            // 34

{"\x55\x7f",             2, QD_AMQP_SMALLLONG,  0, 0, 1, 0, 0, INT8_MAX},            // 35
{"\x55\x7f",             2, QD_AMQP_SMALLLONG,  0, 0, 0, 1, 0, INT8_MAX},            // 36
{"\x55\x80",             2, QD_AMQP_SMALLLONG,  0, 0, 1, 0, 0, INT8_MIN},            // 37
{"\x55\x80",             2, QD_AMQP_SMALLLONG,  0, 0, 0, 1, 0, INT8_MIN},            // 38

{"\x80\xff\xff\xff\xff\xff\xff\xff\xff",
                         9, QD_AMQP_ULONG,      0, 1, 0, 0, UINT64_MAX, 0},          // 39
{"\x81\x7f\xff\xff\xff\xff\xff\xff\xff",
                         9, QD_AMQP_LONG,       0, 0, 0, 1, 0, INT64_MAX},           // 40
{"\x81\x80\x00\x00\x00\x00\x00\x00\x00",
                         9, QD_AMQP_LONG,       0, 0, 0, 1, 0, INT64_MIN},           // 41
{0, 0, 0, 0, 0}
};


static char *test_parser_fixed_scalars(void *context)
{
    int idx = 0;
    qd_iterator_t *field = NULL;
    qd_parsed_field_t *parsed = NULL;
    static char error[1024];
    qd_buffer_list_t buflist = DEQ_EMPTY;

    error[0] = 0;

    while (fs_vectors[idx].data) {
        qd_buffer_list_append(&buflist, (const uint8_t *)fs_vectors[idx].data, fs_vectors[idx].length);
        field = qd_iterator_buffer(DEQ_HEAD(buflist), 0, fs_vectors[idx].length, ITER_VIEW_ALL);
        parsed = qd_parse(field);

        qd_iterator_t *typed_iter = qd_parse_typed(parsed);

        int length = qd_iterator_length(typed_iter);

        if (length != fs_vectors[idx].length) {
            strcpy(error, "Length of typed iterator does not match actual length");
            break;
        }

        if (!qd_parse_ok(parsed)) {
            strcpy(error, "Unexpected Parse Error");
            break;
        }
        if (qd_parse_tag(parsed) != fs_vectors[idx].expected_tag) {
            sprintf(error, "(%d) Tag: Expected %02x, Got %02x", idx,
                    fs_vectors[idx].expected_tag, qd_parse_tag(parsed));
            break;
        }
        if (fs_vectors[idx].check_uint &&
            qd_parse_as_uint(parsed) != fs_vectors[idx].expected_ulong) {
            sprintf(error, "(%d) UINT: Expected %"PRIx64", Got %"PRIx32, idx,
                    fs_vectors[idx].expected_ulong, qd_parse_as_uint(parsed));
            break;
        }
        if (fs_vectors[idx].check_ulong &&
            qd_parse_as_ulong(parsed) != fs_vectors[idx].expected_ulong) {
            sprintf(error, "(%d) ULONG: Expected %"PRIx64", Got %"PRIx64, idx,
                    fs_vectors[idx].expected_ulong, qd_parse_as_ulong(parsed));
            break;
        }
        if (fs_vectors[idx].check_int &&
            qd_parse_as_int(parsed) != fs_vectors[idx].expected_long) {
            sprintf(error, "(%d) INT: Expected %"PRIx64", Got %"PRIx32, idx,
                    fs_vectors[idx].expected_long, qd_parse_as_int(parsed));
            break;
        }
        if (fs_vectors[idx].check_long &&
            qd_parse_as_long(parsed) != fs_vectors[idx].expected_long) {
            sprintf(error, "(%d) LONG: Expected %"PRIx64", Got %"PRIx64, idx,
                    fs_vectors[idx].expected_long, qd_parse_as_long(parsed));
            break;
        }
        idx++;
        qd_iterator_free(field);
        field = 0;
        qd_parse_free(parsed);
        parsed = 0;
        qd_buffer_list_free_buffers(&buflist);
    }

    qd_iterator_free(field);
    qd_parse_free(parsed);
    qd_buffer_list_free_buffers(&buflist);
    return *error ? error : 0;
}

static char *test_integer_conversion(void *context)
{
    const struct fs_vector_t {
        const char *data;
        int         length;
        uint8_t     parse_as;
        bool        expect_fail;
        int64_t     expected_int;
        uint64_t    expected_uint;
    } fs_vectors[] = {
        // can successfully convert 64 bit values that are valid in the 32bit range
        {"\x80\x00\x00\x00\x00\xff\xff\xff\xff", 9, QD_AMQP_UINT, false, 0,         UINT32_MAX},
        {"\x80\x00\x00\x00\x00\x00\x00\x00\x00", 9, QD_AMQP_UINT, false, 0,         0},
        {"\x80\x00\x00\x00\x00\x00\x00\x00\x01", 9, QD_AMQP_UINT, false, 0,         1},
        {"\x81\x00\x00\x00\x00\x7f\xff\xff\xff", 9, QD_AMQP_INT,  false, INT32_MAX, 0},
        {"\x81\xFF\xFF\xFF\xFF\x80\x00\x00\x00", 9, QD_AMQP_INT,  false, INT32_MIN, 0},
        {"\x81\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF", 9, QD_AMQP_INT,  false, -1,        0},

        // signed/unsigned conversions
        {"\x70\x7F\xFF\xFF\xFF",                 5, QD_AMQP_INT,  false, INT32_MAX, 0},
        {"\x71\x7F\xFF\xFF\xFF",                 5, QD_AMQP_UINT, false, 0,         INT32_MAX},
        {"\x80\x7F\xFF\xFF\xFF\xFF\xFF\xFF\xFF", 9, QD_AMQP_LONG, false, INT64_MAX, 0},
        {"\x81\x7F\xFF\xFF\xFF\xFF\xFF\xFF\xFF", 9, QD_AMQP_ULONG, false,0,         INT64_MAX},
        {"\x50\x7F",                             2, QD_AMQP_INT,  false, INT8_MAX,  0},
        {"\x60\x7F\xFF",                         3, QD_AMQP_INT,  false, INT16_MAX,  0},
        {"\x53\x7F",                             2, QD_AMQP_INT,  false, INT8_MAX,  0},
        {"\x55\x7F",                             2, QD_AMQP_UINT, false, 0,         INT8_MAX},
        {"\x51\x7F",                             2, QD_AMQP_UINT, false, 0,         INT8_MAX},
        {"\x61\x7F\xFF",                         3, QD_AMQP_UINT, false, 0,         INT16_MAX},

        // strings
        {"\xa1\x02 1",                           4, QD_AMQP_UINT, false, 0,         1},
        {"\xa1\x02-1",                           4, QD_AMQP_INT,  false, -1,        0},

        {"\xa1\x14" "18446744073709551615",     22, QD_AMQP_ULONG,false, 0,         UINT64_MAX},
        {"\xa1\x14" "-9223372036854775808",     22, QD_AMQP_LONG, false, INT64_MIN, 0},
        {"\xa1\x13" "9223372036854775807",      21, QD_AMQP_LONG, false, INT64_MAX, 0},
        {"\xa3\x13" "9223372036854775807",      21, QD_AMQP_LONG, false, INT64_MAX, 0},

        // cannot convert 64 bit values that are outside the 32bit range as int32
        {"\x80\x00\x00\x00\x01\x00\x00\x00\x00", 9, QD_AMQP_UINT, true,  0, 0},
        {"\x81\x00\x00\x00\x00\x80\x00\x00\x00", 9, QD_AMQP_INT,  true,  0, 0},
        {"\x81\xFF\xFF\xFF\xFF\x7F\xFF\xFF\xFF", 9, QD_AMQP_INT,  true,  0, 0},

        // bad signed/unsigned conversions
        {"\x80\x80\x00\x00\x00\x00\x00\x00\x00", 9, QD_AMQP_LONG,  true, 0, 0},
        {"\x81\x80\x00\x00\x00\x00\x00\x00\x00", 9, QD_AMQP_ULONG, true, 0, 0},
        {"\x70\x80\x00\x00\x00",                 5, QD_AMQP_LONG,  true, 0, 0},
        {"\x71\x80\x00\x00\x00",                 5, QD_AMQP_ULONG, true, 0, 0},
        {"\x55\x80",                             2, QD_AMQP_UINT,  true, 0, 0},
        {"\x51\x80",                             2, QD_AMQP_UINT,  true, 0, 0},
        {"\x54\x80",                             2, QD_AMQP_UINT,  true, 0, 0},
        {"\x61\x80\x00",                         3, QD_AMQP_UINT,  true, 0, 0},
        {"\x53\x80",                             2, QD_AMQP_INT,   true, 0, 0},
        {"\x52\x80",                             2, QD_AMQP_INT,   true, 0, 0},
        {"\x50\x80",                             2, QD_AMQP_LONG,  true, 0, 0},
        {"\x60\x80\x00",                         3, QD_AMQP_LONG,  true, 0, 0},
        {NULL},
    };

    qd_buffer_list_t buflist = DEQ_EMPTY;
    char *error = NULL;

    for (int i = 0; fs_vectors[i].data && !error; ++i) {
        qd_buffer_list_append(&buflist, (const uint8_t *)fs_vectors[i].data, fs_vectors[i].length);
        qd_iterator_t *data_iter = qd_iterator_buffer(DEQ_HEAD(buflist), 0, fs_vectors[i].length, ITER_VIEW_ALL);
        qd_parsed_field_t *field = qd_parse(data_iter);

        if (!qd_parse_ok(field)) {
            error = "unexpected parse error";
            qd_iterator_free(data_iter);
            qd_parse_free(field);
            break;
        }

        bool equal = false;
        switch (fs_vectors[i].parse_as) {
        case QD_AMQP_UINT:
        {
            uint32_t tmp = qd_parse_as_uint(field);
            equal = (tmp == fs_vectors[i].expected_uint);
            break;
        }
        case QD_AMQP_ULONG:
        {
            uint64_t tmp = qd_parse_as_ulong(field);
            equal = (tmp == fs_vectors[i].expected_uint);
            break;
        }
        case QD_AMQP_INT:
        {

            int32_t tmp = qd_parse_as_int(field);
            equal = (tmp == fs_vectors[i].expected_int);
            break;
        }
        case QD_AMQP_LONG:
        {
            int64_t tmp = qd_parse_as_long(field);
            equal = (tmp == fs_vectors[i].expected_int);
            break;
        }
        }

        if (!qd_parse_ok(field)) {
            if (!fs_vectors[i].expect_fail) {
                error = "unexpected conversion/parse error";
            }
        } else if (fs_vectors[i].expect_fail) {
            error = "Conversion did not fail as expected";
        } else if (!equal) {
            error = "unexpected converted value";
        }

        qd_iterator_free(data_iter);
        qd_parse_free(field);
        qd_buffer_list_free_buffers(&buflist);
    }

    qd_buffer_list_free_buffers(&buflist);
    return error;
}


static char *test_map(void *context)
{
    qd_iterator_t     *val_iter = 0;
    qd_iterator_t     *typed_iter = 0;
    qd_iterator_t     *key_iter = 0;
    static char error[1000] = "";
    const uint8_t data[] =
        "\xd1\x00\x00\x00\x43\x00\x00\x00\x08"    // map32, 8 items
        "\xa3\x05\x66irst\xa1\x0evalue_of_first"  // (23) "first":"value_of_first"
        "\xa3\x06second\x52\x20"                  // (10) "second":32
        "\xa3\x05third\x41"                       // (8)  "third":true
        "\x80\x00\x00\x00\x00\x00\x00\x80\x00"    // (9) 32768:
        "\xb0\x00\x00\x00\x08"                    // (5+8) vbin "!+!+!+!+"
        "\x21\x2b\x21\x2b\x21\x2b\x21\x2b";

    qd_buffer_list_t buflist = DEQ_EMPTY;
    qd_buffer_list_append(&buflist, data, sizeof(data));

    qd_iterator_t     *data_iter = qd_iterator_buffer(DEQ_HEAD(buflist), 0, sizeof(data), ITER_VIEW_ALL);
    qd_parsed_field_t *field     = qd_parse(data_iter);
    qd_iterator_free(data_iter);

    if (!qd_parse_ok(field)) {
        snprintf(error, 1000, "Parse failed: %s", qd_parse_error(field));
        goto exit;
    }

    if (!qd_parse_is_map(field)) {
        snprintf(error, sizeof(error), "Expected field to be a map");
        goto exit;
    }

    uint32_t count = qd_parse_sub_count(field);
    if (count != 4) {
        snprintf(error, 1000, "Expected sub-count==4, got %"PRIu32, count);
        goto exit;
    }

    // Validate "first":"value_of_first"

    qd_parsed_field_t *key_field  = qd_parse_sub_key(field, 0);
    key_iter   = qd_parse_raw(key_field);
    typed_iter = qd_parse_typed(key_field);
    if (!qd_iterator_equal(key_iter, (unsigned char*) "first")) {
        unsigned char     *result   = qd_iterator_copy(key_iter);
        snprintf(error, 1000, "First key: expected 'first', got '%s'", result);
        free (result);
        goto exit;
    }

    if (!qd_iterator_equal(typed_iter, (unsigned char*) "\xa3\x05\x66irst"))
        return "Incorrect typed iterator on first-key";

    qd_parsed_field_t *val_field = qd_parse_sub_value(field, 0);
    val_iter  = qd_parse_raw(val_field);
    typed_iter = qd_parse_typed(val_field);
    if (!qd_iterator_equal(val_iter, (unsigned char*) "value_of_first")) {
        unsigned char     *result   = qd_iterator_copy(val_iter);
        snprintf(error, 1000, "First value: expected 'value_of_first', got '%s'", result);
        free (result);
        goto exit;
    }

    if (!qd_iterator_equal(typed_iter, (unsigned char*) "\xa1\x0evalue_of_first"))
        return "Incorrect typed iterator on first-key";

    // Validate "second:32"

    key_field = qd_parse_sub_key(field, 1);
    key_iter  = qd_parse_raw(key_field);
    if (!qd_iterator_equal(key_iter, (unsigned char*) "second")) {
        unsigned char     *result   = qd_iterator_copy(key_iter);
        snprintf(error, 1000, "Second key: expected 'second', got '%s'", result);
        free (result);
        goto exit;
    }

    val_field = qd_parse_sub_value(field, 1);
    if (qd_parse_as_uint(val_field) != 32) {
        snprintf(error, 1000, "Second value: expected 32, got %"PRIu32, qd_parse_as_uint(val_field));
        goto exit;
    }

    // Validate "third":true

    key_field = qd_parse_sub_key(field, 2);
    key_iter  = qd_parse_raw(key_field);
    if (!qd_iterator_equal(key_iter, (unsigned char*) "third")) {
        unsigned char     *result   = qd_iterator_copy(key_iter);
        snprintf(error, 1000, "Third key: expected 'third', got '%s'", result);
        free (result);
        goto exit;
    }

    val_field = qd_parse_sub_value(field, 2);
    if (!qd_parse_as_bool(val_field)) {
        snprintf(error, 1000, "Third value: expected true");
        goto exit;
    }

    // Validate 32768:"!+!+!+!+"

    uint8_t octet;
    uint8_t ncopy_buf[8];

    key_field = qd_parse_sub_key(field, 3);
    typed_iter = qd_parse_typed(key_field);

    octet = qd_iterator_octet(typed_iter);
    if (octet != (uint8_t)0x80) {
        snprintf(error, sizeof(error), "4th Key not ulong type");
        goto exit;
    }
    if (qd_iterator_ncopy_octets(typed_iter, ncopy_buf, 8) != 8) {
        snprintf(error, sizeof(error), "4th Key incorrect length");
        goto exit;
    }
    if (memcmp(ncopy_buf, "\x00\x00\x00\x00\x00\x00\x80\x00", 8) != 0) {
        snprintf(error, sizeof(error), "4th key encoding value incorrect");
        goto exit;
    }
    if (qd_parse_as_ulong(key_field) != 32768) {
        snprintf(error, sizeof(error), "4th key value not 32768");
        goto exit;
    }

    val_field = qd_parse_sub_value(field, 3);
    val_iter = qd_parse_raw(val_field);
    typed_iter = qd_parse_typed(val_field);

    octet = qd_iterator_octet(typed_iter);
    if (octet != (uint8_t)0xb0) {
        snprintf(error, sizeof(error), "4th Value not vbin32 type: 0x%X", (unsigned int)octet);
        goto exit;
    }
    if (qd_iterator_ncopy_octets(typed_iter, ncopy_buf, 4) != 4) {
        snprintf(error, sizeof(error), "4th Value incorrect length");
        goto exit;
    }
    if (memcmp(ncopy_buf, "\x00\x00\x00\x08", 4) != 0) {
        snprintf(error, sizeof(error), "4th Value encoding incorrect");
        goto exit;
    }

    if (qd_iterator_octet(val_iter) != '!' || qd_iterator_octet(val_iter) != '+') {
        snprintf(error, sizeof(error), "4th Value [0-1] incorrect");
        goto exit;
    }
    if (qd_iterator_ncopy_octets(typed_iter, ncopy_buf, 4) != 4) {
        snprintf(error, sizeof(error), "4th Value sub-copy failed");
        goto exit;
    }
    if (memcmp(ncopy_buf, "!+!+", 4) != 0) {
        snprintf(error, sizeof(error), "4th Value sub-copy incorrect");
        goto exit;
    }
    if (qd_iterator_octet(val_iter) != '!' || qd_iterator_octet(val_iter) != '+') {
        snprintf(error, sizeof(error), "4th Value [6-7] incorrect");
        goto exit;
    }


exit:
    qd_parse_free(field);
    qd_buffer_list_free_buffers(&buflist);

    return error[0] ? error : 0;
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
    qd_buffer_list_t buflist = DEQ_EMPTY;
    static char error[1024];

    while (err_vectors[idx].data) {
        if (err_vectors[idx].length) {
            qd_buffer_list_append(&buflist, (const uint8_t *)err_vectors[idx].data, err_vectors[idx].length);
        } else {
            qd_buffer_t *tmp = qd_buffer();
            DEQ_INSERT_HEAD(buflist, tmp);
        }
        qd_iterator_t *field  = qd_iterator_buffer(DEQ_HEAD(buflist), 0, err_vectors[idx].length, ITER_VIEW_ALL);
        qd_parsed_field_t *parsed = qd_parse(field);
        if (qd_parse_ok(parsed)) {
            qd_parse_free(parsed);
            qd_iterator_free(field);
            sprintf(error, "(%d) Unexpected Parse Success", idx);
            qd_buffer_list_free_buffers(&buflist);
            return error;
        }
        if (strcmp(qd_parse_error(parsed), err_vectors[idx].expected_error) != 0) {
            sprintf(error, "(%d) Error: Expected %s, Got %s", idx,
                    err_vectors[idx].expected_error, qd_parse_error(parsed));
            qd_parse_free(parsed);
            qd_iterator_free(field);
            qd_buffer_list_free_buffers(&buflist);
            return error;
        }
        qd_parse_free(parsed);
        qd_iterator_free(field);
        qd_buffer_list_free_buffers(&buflist);
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

    error[0] = 0;
    qd_iterator_set_address(false, "0", "ROUTER");

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
        goto cleanup;
    }
    if (ingress != 0) {
        sprintf(error, "(A) Expected ingress index of 0, got %d", ingress);
        goto cleanup;
    }
    int total = 0;
    int bit, c;
    for (QD_BITMASK_EACH(bm, bit, c)) {
        total += bit;
    }
    if (total != 17) {
        sprintf(error, "Expected total bit value of 17, got %d", total);
        goto cleanup;
    }

    qd_bitmask_free(bm);
    bm = 0;
    qd_tracemask_del_router(tm, 3);
    qd_tracemask_remove_link(tm, 0);

    ingress = -1;
    bm = qd_tracemask_create(tm, pf, &ingress);
    qd_parse_free(pf);
    pf = 0;
    if (qd_bitmask_cardinality(bm) != 1) {
        sprintf(error, "Expected cardinality of 1, got %d", qd_bitmask_cardinality(bm));
        goto cleanup;
    }
    if (ingress != 0) {
        sprintf(error, "(B) Expected ingress index of 0, got %d", ingress);
        goto cleanup;
    }

    total = 0;
    for (QD_BITMASK_EACH(bm, bit, c)) {
        total += bit;
    }
    if (total != 3) {
        sprintf(error, "Expected total bit value of 3, got %d", total);
        // fallthrough
    }

cleanup:
    qd_parse_free(pf);
    qd_tracemask_free(tm);
    qd_bitmask_free(bm);
    for (qd_buffer_t *buf = DEQ_HEAD(list); buf; buf = DEQ_HEAD(list)) {
        DEQ_REMOVE_HEAD(list);
        qd_buffer_free(buf);
    }
    return *error ? error : 0;
}


int parse_tests()
{
    int result = 0;
    char *test_group = "parse_tests";

    TEST_CASE(test_parser_fixed_scalars, 0);
    TEST_CASE(test_map, 0);
    TEST_CASE(test_parser_errors, 0);
    TEST_CASE(test_tracemask, 0);
    TEST_CASE(test_integer_conversion, 0);

    return result;
}


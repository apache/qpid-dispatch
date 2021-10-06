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
#include "qpid/dispatch/buffer.h"
#include "qpid/dispatch/amqp.h"

#include "test_case.h"

#include <stdio.h>
#include <string.h>


static void fill_buffer(qd_buffer_list_t *list,
                        const unsigned char *data,
                        int length)
{
    DEQ_INIT(*list);
    while (length > 0) {
        qd_buffer_t *buf = qd_buffer();
        size_t count = qd_buffer_capacity(buf);
        if (length < count) count = length;
        memcpy(qd_buffer_cursor(buf),
               data, count);
        qd_buffer_insert(buf, count);
        DEQ_INSERT_TAIL(*list, buf);
        data += count;
        length -= count;
    }
}

static int compare_buffer(const qd_buffer_list_t *list,
                          const unsigned char *data,
                          int length)
{
    qd_buffer_t *buf = DEQ_HEAD(*list);
    while (buf && length > 0) {
        size_t count = qd_buffer_size(buf);
        if (length < count) count = length;
        if (memcmp(qd_buffer_base(buf), data, count))
            return 0;
        length -= count;
        data += count;
        buf = DEQ_NEXT(buf);
    }
    return !buf && length == 0;
}


static const char pattern[] = "This piggy went 'wee wee wee' all the way home!";
static const int pattern_len = sizeof(pattern);

static char *test_buffer_list_clone(void *context)
{
    qd_buffer_list_t list;
    fill_buffer(&list, (unsigned char *)pattern, pattern_len);
    if (qd_buffer_list_length(&list) != pattern_len) return "Invalid fill?";

    qd_buffer_list_t copy;
    unsigned int len = qd_buffer_list_clone(&copy, &list);
    if (len != pattern_len) return "Copy failed";

    // 'corrupt' source buffer list:
    *qd_buffer_base(DEQ_HEAD(list)) = (unsigned char)'X';
    qd_buffer_list_free_buffers(&list);
    if (!DEQ_IS_EMPTY(list)) return "List should be empty!";

    // ensure copy is un-molested:
    if (!compare_buffer(&copy, (unsigned char *)pattern, pattern_len)) return "Buffer list corrupted";

    qd_buffer_list_free_buffers(&list);
    qd_buffer_list_free_buffers(&copy);
    return 0;
}


static char *test_buffer_list_append(void *context)
{
    qd_buffer_list_t list;
    qd_buffer_t *buf = qd_buffer();
    size_t buffer_size = qd_buffer_capacity(buf);
    qd_buffer_free(buf);

    DEQ_INIT(list);

    qd_buffer_list_append(&list, (uint8_t*) "", 0);
    if (DEQ_SIZE(list) > 0) return "Buffer list should be empty";

    qd_buffer_list_append(&list, (uint8_t*) "ABCDEFGHIJ", 10);
    if (DEQ_SIZE(list) != (10 / buffer_size) + ((10 % buffer_size) ? 1 : 0)) return "Incorrect buffer count for size 10";

    qd_buffer_list_append(&list, (uint8_t*) "KLMNOPQRSTUVWXYZ", 16);
    if (DEQ_SIZE(list) != (26 / buffer_size) + ((26 % buffer_size) ? 1 : 0)) return "Incorrect buffer count for size 26";

    size_t list_len = 0;
    buf = DEQ_HEAD(list);
    while (buf) {
        list_len += qd_buffer_size(buf);
        buf = DEQ_NEXT(buf);
    }
    if (list_len != 26) {
        static char error[100];
        sprintf(error, "Incorrect accumulated buffer size: %zu", list_len);
        return error;
    }

    qd_buffer_list_free_buffers(&list);

    return 0;
}


static char *test_buffer_field(void *context)
{
    char *result = 0;
    static const uint8_t data1[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    static const uint8_t data2[10] = {0xF9, 0xF8, 0xF7, 0xF6, 0xF5, 0xF4, 0xF3, 0xF2, 0xF1, 0xF0};
    qd_buffer_list_t list;
    qd_buffer_field_t bfield;

    DEQ_INIT(list);

    // test buffer list (2000 octets):
    for (int i = 0; i < 100; ++i) {
        qd_buffer_list_t tmp;
        DEQ_INIT(tmp);
        fill_buffer(&tmp, (unsigned char *) data1, 10);
        qd_buffer_t *b = qd_buffer();
        DEQ_INSERT_TAIL(tmp, b);  // empty buffer
        DEQ_APPEND(list, tmp);
        fill_buffer(&tmp, (unsigned char *) data2, 10);
        DEQ_APPEND(list, tmp);
    }

    // verify octet read

    bfield.head = DEQ_HEAD(list);
    bfield.cursor = qd_buffer_base(bfield.head);
    bfield.length = 2000;

    int total_octets = 0;
    size_t expected_length = 2000;
    uint8_t next_octet = 0;
    uint8_t octet = 0xFF;
    while (qd_buffer_field_octet(&bfield, &octet)) {
        total_octets += 1;
        expected_length -= 1;

        if (bfield.length != expected_length) {
            result = "octet length not updated";
            goto exit;
        }
        if (octet != next_octet) {
            result = "Unexpected next octet";
            goto exit;
        }
        if (next_octet == 0x09)
            next_octet = 0xF9;
        else if (next_octet == 0xF0)
            next_octet = 0;
        else if (next_octet < 0x09)
            next_octet += 1;
        else
            next_octet -= 1;
    }

    if (total_octets != 2000 || bfield.length != 0) {
        result = "Next octet wrong length";
        goto exit;
    }

    // verify advance

    bfield.head = DEQ_HEAD(list);
    bfield.cursor = qd_buffer_base(bfield.head);
    bfield.length = 2000;

    size_t amount = qd_buffer_field_advance(&bfield, 2);
    if (amount != 2) {
        result = "advance 2 failed";
        goto exit;
    }

    if (!qd_buffer_field_octet(&bfield, &octet) || octet != 2) {
        result = "expected to advance to '2'";
        goto exit;
    }

    amount = qd_buffer_field_advance(&bfield, 1995);
    if (amount != 1995) {
        result = "advance 1995 failed";
        goto exit;
    }

    if (bfield.length != 2) {
        result = "expected 2 last octets";
        goto exit;
    }

    if (!qd_buffer_field_octet(&bfield, &octet) || octet != 0xF1) {
        result = "expected to advance to '0xF1'";
        goto exit;
    }

    amount = qd_buffer_field_advance(&bfield, 3);
    if (amount != 1 || bfield.length != 0) {
        result = "failed to advance to end of field";
        goto exit;
    }

    // verify memcpy

    bfield.head = DEQ_HEAD(list);
    bfield.cursor = qd_buffer_base(bfield.head);
    bfield.length = 2000;

    uint8_t dest[10];
    amount = qd_buffer_field_memcpy(&bfield, dest, 5);
    if (amount != 5) {
        result = "failed to memcpy 5";
        goto exit;
    }
    if (memcmp(dest, data1, 5)) {
        result = "memcpy 5 failed";
        goto exit;
    }
    amount = qd_buffer_field_memcpy(&bfield, dest, 10);
    if (amount != 10) {
        result = "failed to memcpy 10";
        goto exit;
    }
    if (memcmp(dest, &data1[5], 5) || memcmp(&dest[5], &data2[0], 5)) {
        result = "memcpy 10 failed";
        goto exit;
    }
    amount = qd_buffer_field_advance(&bfield, 1980);
    if (amount != 1980) {
        result = "advance 1980 failed";
        goto exit;
    }
    amount = qd_buffer_field_memcpy(&bfield, dest, 10);
    if (amount != 5) {
        result = "memcpy expected 5 failed";
        goto exit;
    }
    if (memcmp(dest, &data2[5], 5) || bfield.length != 0) {
        result = "memcpy at end failed";
        goto exit;
    }

    // verify equal

    bfield.head = DEQ_HEAD(list);
    bfield.cursor = qd_buffer_base(bfield.head);
    bfield.length = 2000;

    const uint8_t pattern[] = "\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\xF9\xF8\xF7\xF6\xF5\xF4\xF3\xF2\xF1\xF0";
    const uint8_t pattern_bad[] = "\xF9\xF8\xF7\xF6\xF5\xF4\xF3\xF2\xF1\xF0\xAA";
    if (qd_buffer_field_equal(&bfield, (uint8_t*) "\x00\x01\x03", 3)) {
        result = "expected equal 3 to fail";
        goto exit;
    }
    if (bfield.length != 2000) {
        result = "do not advance on failed equal";
        goto exit;
    }
    if (!qd_buffer_field_equal(&bfield, pattern, 20)) {
        result = "expected pattern match";
        goto exit;
    }
    if (bfield.length != 1980) {
        result = "match did not advance";
        goto exit;
    }
    (void)qd_buffer_field_advance(&bfield, 1960);
    if (!qd_buffer_field_equal(&bfield, pattern, 10)) {
        result = "expected sub pattern match";
        goto exit;
    }
    if (qd_buffer_field_equal(&bfield, pattern_bad, 11)) {
        result = "did not expect sub pattern match";
        goto exit;
    }
    if (bfield.length != 10) {
        result = "mismatch advanced";
        goto exit;
    }
    if (!qd_buffer_field_equal(&bfield, &pattern[10], 10)) {
        result = "expected end sub pattern match";
        goto exit;
    }


exit:
    qd_buffer_list_free_buffers(&list);
    return result;
}


static char *test_amqp_field(void *context)
{
    char *result = 0;
    qd_buffer_list_t list;
    qd_buffer_field_t bfield;
    qd_amqp_field_t amqpf;

    // AMQP 32 map to parse

    const struct {
        const char *value;
        size_t length;
    } test_map[] = {
        {"\xD1\x00\x00\x00\x5C", 5},                  // map32, length 92 octets
        {"\x00\x00\x00\x06", 4},                      // count
        {"\xA3\x04", 2},                              // sym8, 4 octets
        {"Key1", 4},                                  // key value
        {"\x71\x00\x00\x00\x01", 5},                  // int32 value 1
        {"\xB3\x00\x00", 3},                          // long symbol, 2 bytes of length
        {"\x00\x11", 2},                              // remaining length
        {"ABCDEF", 6},                                // start of key
        {"GHIJKLMNOPR", 11},                          // key remainder
        {"\xD0", 1},                                  // start of list32
        {"\x00\x00\x00\x2E", 4},                      // length (4 octets)
        {"\x00\x00\x00\x08", 4},                      // count  (4 octets)

        // list data: 42 octets
        {"\x40", 1},                                  // null (1 octet)
        {"\x42", 1},                                  // bool false (1 octet)
        {"\x50\x22", 2},                              // octet (2 octet)
        {"\x60\x00\x02", 3},                          // short (3 octet)
        {"\x70\x00\x00\x00\x03", 5},                  // uint32 (5 octet)
        {"\x81\x00\x00\x00\x00\x00\x00\x00\xFF", 9},  // int64 (9 octets)
        {"\x94\x00\x00\x00\x00\x00\x00\x00\x00", 9},  // decimal128 first half (9 octets)
        {"\x10\x10\x10\x10\x10\x10\x10\x10", 8},      // decimal128 2nd half (8 octets)
        {"\xC0\x02\x01\x40", 4},                      // list8 one null entry (4 octets)

        // end of list data: last key,value map entry:
        {"\x44", 1},                                  // map key ulong 0
        {"\xA1\x01\x20", 3},                          // str8, single space
        {0,0}
    };

    // to validate each field in the above map
#define MAP_COUNT 6
    const qd_amqp_field_t expected_map_fields[MAP_COUNT] = {
        {
            .tag = QD_AMQP_SYM8,
            .size = 4
        },
        {
            .tag = QD_AMQP_INT,
            .size = 4
        },
        {
            .tag = QD_AMQP_SYM32,
            .size = 17
        },
#define MAP_LIST_INDEX 3
        {
            .tag = QD_AMQP_LIST32,
            .size = 46,
            .count = 8
        },
        {
            .tag = QD_AMQP_ULONG0,
        },
        {
            .tag = QD_AMQP_STR8_UTF8,
            .size = 1
        }
    };

#define LIST_COUNT 8
    const qd_amqp_field_t expected_list_fields[LIST_COUNT] = {
        {
            .tag = QD_AMQP_NULL
        },
        {
            .tag = QD_AMQP_FALSE
        },
        {
            .tag = QD_AMQP_UBYTE,
            .size = 1
        },
        {
            .tag = QD_AMQP_USHORT,
            .size = 2
        },
        {
            .tag = QD_AMQP_UINT,
            .size = 4
        },
        {
            .tag = QD_AMQP_LONG,
            .size = 8
        },
        {
            .tag = QD_AMQP_DECIMAL128,
            .size = 16
        },
        {
            .tag = QD_AMQP_LIST8,
            .size = 2,
            .count = 1
        },
    };

    // build up test data
    DEQ_INIT(list);
    size_t map_len = 0;
    for (int i = 0; test_map[i].length; ++i) {
        qd_buffer_list_t tmp;
        DEQ_INIT(tmp);
        fill_buffer(&tmp, (unsigned char *) test_map[i].value, test_map[i].length);
        map_len += test_map[i].length;
        DEQ_APPEND(list, tmp);
    }

    bfield.head = DEQ_HEAD(list);
    bfield.cursor = qd_buffer_base(bfield.head);
    bfield.length = map_len;

    // parse out the top-level map

    size_t amount = qd_buffer_field_get_amqp_data(&bfield, &amqpf);
    if (amount != map_len) {
        result = "expected entire map to be skipped";
        goto exit;
    }
    if (amqpf.tag != QD_AMQP_MAP32
        || amqpf.size != 92
        || amqpf.count != 6
        || amqpf.data.length != 88) {
        result = "AMQP Map failed to parse";
        goto exit;
    }

    if (bfield.length != 0) {
        result = "bfield length not zero";
        goto exit;
    }

    // rebase bfield to the contents of the map
    // and proceed to decode each element
    bfield = amqpf.data;
    int map_index = 0;
    while (qd_buffer_field_get_amqp_data(&bfield, &amqpf)) {

        if (expected_map_fields[map_index].tag != amqpf.tag
            || expected_map_fields[map_index].size != amqpf.size
            || expected_map_fields[map_index].count != amqpf.count) {

            result = "failed to parse map entry";
            goto exit;
        }

        // parse into the LIST32 map value;
        if (map_index == MAP_LIST_INDEX) {
            // amqpf contains the inner list32, validate it
            qd_buffer_field_t bfield_list = amqpf.data;
            qd_amqp_field_t amqpf_list;
            int list_index = 0;
            while (qd_buffer_field_get_amqp_data(&bfield_list, &amqpf_list)) {
                if (expected_list_fields[list_index].tag != amqpf_list.tag
                    || expected_list_fields[list_index].size != amqpf_list.size
                    || expected_list_fields[list_index].count != amqpf_list.count) {

                    result = "failed to parse list entry";
                    goto exit;
                }

                list_index += 1;
            }

            if (list_index != LIST_COUNT) {
                result = "incorrect list count";
                goto exit;
            }

            if (bfield_list.length) {
                result = "failed to consume full list";
                goto exit;
            }
        }

        map_index += 1;
    }

    if (map_index != MAP_COUNT) {
        result = "incorrect map field count";
        goto exit;
    }

    if (bfield.length != 0) {
        result = "failed to consume full map";
        goto exit;
    }


exit:
    qd_buffer_list_free_buffers(&list);
    return result;
}

int buffer_tests()
{
    int result = 0;
    char *test_group = "buffer_tests";

    TEST_CASE(test_buffer_list_clone, 0);
    TEST_CASE(test_buffer_list_append, 0);
    TEST_CASE(test_buffer_field, 0);
    TEST_CASE(test_amqp_field, 0);

    return result;
}


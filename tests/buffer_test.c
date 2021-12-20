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
#include "buffer_field_api.h"

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
    qd_buffer_list_t other_list;
    qd_buffer_field_t bfield;

    DEQ_INIT(list);
    DEQ_INIT(other_list);

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

    bfield.buffer = DEQ_HEAD(list);
    bfield.cursor = qd_buffer_base(bfield.buffer);
    bfield.remaining = 2000;

    int total_octets = 0;
    size_t expected_length = 2000;
    uint8_t next_octet = 0;
    uint8_t octet = 0xFF;
    while (qd_buffer_field_octet(&bfield, &octet)) {
        total_octets += 1;
        expected_length -= 1;

        if (bfield.remaining != expected_length) {
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

    if (total_octets != 2000 || bfield.remaining != 0) {
        result = "Next octet wrong length";
        goto exit;
    }

    // verify advance

    bfield.buffer = DEQ_HEAD(list);
    bfield.cursor = qd_buffer_base(bfield.buffer);
    bfield.remaining = 2000;

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

    if (bfield.remaining != 2) {
        result = "expected 2 last octets";
        goto exit;
    }

    if (!qd_buffer_field_octet(&bfield, &octet) || octet != 0xF1) {
        result = "expected to advance to '0xF1'";
        goto exit;
    }

    amount = qd_buffer_field_advance(&bfield, 3);
    if (amount != 1 || bfield.remaining != 0) {
        result = "failed to advance to end of field";
        goto exit;
    }

    // verify ncopy

    bfield.buffer = DEQ_HEAD(list);
    bfield.cursor = qd_buffer_base(bfield.buffer);
    bfield.remaining = 2000;

    uint8_t dest[10];
    amount = qd_buffer_field_ncopy(&bfield, dest, 5);
    if (amount != 5) {
        result = "failed to ncopy 5";
        goto exit;
    }
    if (memcmp(dest, data1, 5)) {
        result = "ncopy 5 failed";
        goto exit;
    }
    amount = qd_buffer_field_ncopy(&bfield, dest, 10);
    if (amount != 10) {
        result = "failed to ncopy 10";
        goto exit;
    }
    if (memcmp(dest, &data1[5], 5) || memcmp(&dest[5], &data2[0], 5)) {
        result = "ncopy 10 failed";
        goto exit;
    }
    amount = qd_buffer_field_advance(&bfield, 1980);
    if (amount != 1980) {
        result = "advance 1980 failed";
        goto exit;
    }
    amount = qd_buffer_field_ncopy(&bfield, dest, 10);
    if (amount != 5) {
        result = "ncopy expected 5 failed";
        goto exit;
    }
    if (memcmp(dest, &data2[5], 5) || bfield.remaining != 0) {
        result = "ncopy at end failed";
        goto exit;
    }

    // verify equal

    bfield.buffer = DEQ_HEAD(list);
    bfield.cursor = qd_buffer_base(bfield.buffer);
    bfield.remaining = 2000;

    const uint8_t pattern[] = "\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\xF9\xF8\xF7\xF6\xF5\xF4\xF3\xF2\xF1\xF0";
    const uint8_t pattern_bad[] = "\xF9\xF8\xF7\xF6\xF5\xF4\xF3\xF2\xF1\xF0\xAA";
    if (qd_buffer_field_equal(&bfield, (uint8_t*) "\x00\x01\x03", 3)) {
        result = "expected equal 3 to fail";
        goto exit;
    }
    if (bfield.remaining != 2000) {
        result = "do not advance on failed equal";
        goto exit;
    }
    if (!qd_buffer_field_equal(&bfield, pattern, 20)) {
        result = "expected pattern match";
        goto exit;
    }
    if (bfield.remaining != 1980) {
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
    if (bfield.remaining != 10) {
        result = "mismatch advanced";
        goto exit;
    }
    if (!qd_buffer_field_equal(&bfield, &pattern[10], 9 )) {
        result = "expected end sub pattern match";
        goto exit;
    }

    if (!qd_buffer_field_octet(&bfield, &octet) || octet != 0xF0) {
        result = "failed to octet read the extra trailing octet in the pattern";
    }

    // verify buffer list append

    bfield.buffer = DEQ_HEAD(list);
    bfield.cursor = qd_buffer_base(bfield.buffer);
    bfield.remaining = 2000;

    qd_buffer_field_t saved_bfield = bfield;
    qd_buffer_t *bptr = 0;

    qd_buffer_list_append_field(&other_list, &bfield);
    if (bfield.remaining) {
        result = "expected to append 2000 octets";
        goto exit;
    }
    bptr = DEQ_HEAD(other_list);
    uint32_t cmp_count = 0;
    while (bptr) {
        if (!qd_buffer_field_equal(&saved_bfield, qd_buffer_base(bptr), qd_buffer_size(bptr))) {
            result = "expected list and buffers to be equal";
            goto exit;
        }
        cmp_count += qd_buffer_size(bptr);
        bptr = DEQ_NEXT(bptr);
    }

    if (saved_bfield.remaining != 0) {
        result = "expected saved_bfield to be empty";
        goto exit;
    }

    if (cmp_count != 2000) {
        result = "did not compare 2000 octets";
        goto exit;
    }

    qd_buffer_list_free_buffers(&other_list);

    const char *append_str = "abcdefghijklmnopqrstuvwxyz";
    qd_buffer_list_append(&other_list, (const uint8_t *)append_str, strlen(append_str));

    bfield.buffer = DEQ_HEAD(other_list);
    bfield.cursor = qd_buffer_base(bfield.buffer);
    bfield.remaining = strlen(append_str);

    if (!qd_buffer_field_equal(&bfield, (const uint8_t*) append_str, strlen(append_str))) {
        result = "expected to equal append_str";
        goto exit;
    }

exit:
    qd_buffer_list_free_buffers(&list);
    qd_buffer_list_free_buffers(&other_list);
    return result;
}


int buffer_tests()
{
    int result = 0;
    char *test_group = "buffer_tests";

    TEST_CASE(test_buffer_list_clone, 0);
    TEST_CASE(test_buffer_list_append, 0);
    TEST_CASE(test_buffer_field, 0);

    return result;
}


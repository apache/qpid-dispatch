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
#include "compose_private.h"
#include "test_case.h"

#include "qpid/dispatch.h"

#include <inttypes.h>
#include <stdio.h>

static char *vector0 =
    "\x00\x53\x77"                             // amqp-value
    "\xd0\x00\x00\x01\x26\x00\x00\x00\x0a"     // list32 with ten items
    "\xd1\x00\x00\x00\x18\x00\x00\x00\x04"     // map32 with two pairs
    "\xa1\x06key001"                           // str8-utf8
    "\x52\x0a"                                 // smalluint
    "\xa1\x06key002"                           // str8-utf8
    "\x52\x0b"                                 // smalluint
    "\xd1\x00\x00\x00\x18\x00\x00\x00\x04"     // map32 with two pairs
    "\xa1\x06key001"                           // str8-utf8
    "\x52\x14"                                 // smalluint
    "\xa1\x06key002"                           // str8-utf8
    "\x52\x15"                                 // smalluint
    "\xd1\x00\x00\x00\x18\x00\x00\x00\x04"     // map32 with two pairs
    "\xa1\x06key001"                           // str8-utf8
    "\x52\x14"                                 // smalluint
    "\xa1\x06key002"                           // str8-utf8
    "\x52\x15"                                 // smalluint
    "\xd1\x00\x00\x00\x18\x00\x00\x00\x04"     // map32 with two pairs
    "\xa1\x06key001"                           // str8-utf8
    "\x52\x14"                                 // smalluint
    "\xa1\x06key002"                           // str8-utf8
    "\x52\x15"                                 // smalluint
    "\xd1\x00\x00\x00\x18\x00\x00\x00\x04"     // map32 with two pairs
    "\xa1\x06key001"                           // str8-utf8
    "\x52\x14"                                 // smalluint
    "\xa1\x06key002"                           // str8-utf8
    "\x52\x15"                                 // smalluint
    "\xd1\x00\x00\x00\x18\x00\x00\x00\x04"     // map32 with two pairs
    "\xa1\x06key001"                           // str8-utf8
    "\x52\x14"                                 // smalluint
    "\xa1\x06key002"                           // str8-utf8
    "\x52\x15"                                 // smalluint
    "\xd1\x00\x00\x00\x18\x00\x00\x00\x04"     // map32 with two pairs
    "\xa1\x06key001"                           // str8-utf8
    "\x52\x14"                                 // smalluint
    "\xa1\x06key002"                           // str8-utf8
    "\x52\x15"                                 // smalluint
    "\xd1\x00\x00\x00\x18\x00\x00\x00\x04"     // map32 with two pairs
    "\xa1\x06key001"                           // str8-utf8
    "\x52\x14"                                 // smalluint
    "\xa1\x06key002"                           // str8-utf8
    "\x52\x15"                                 // smalluint
    "\xd1\x00\x00\x00\x18\x00\x00\x00\x04"     // map32 with two pairs
    "\xa1\x06key001"                           // str8-utf8
    "\x52\x14"                                 // smalluint
    "\xa1\x06key002"                           // str8-utf8
    "\x52\x15"                                 // smalluint
    "\xd1\x00\x00\x00\x18\x00\x00\x00\x04"     // map32 with two pairs
    "\xa1\x06key001"                           // str8-utf8
    "\x52\x14"                                 // smalluint
    "\xa1\x06key002"                           // str8-utf8
    "\x52\x15"                                 // smalluint
    ;

static int vector0_length = 302;

static char *test_compose_list_of_maps(void *context)
{
    qd_composed_field_t *field = qd_compose(QD_PERFORMATIVE_BODY_AMQP_VALUE, 0);

    qd_compose_start_list(field);

    qd_compose_start_map(field);
    qd_compose_insert_string(field, "key001");
    qd_compose_insert_uint(field, 10);
    qd_compose_insert_string(field, "key002");
    qd_compose_insert_uint(field, 11);
    qd_compose_end_map(field);

    for (int j = 0; j < 9; j++) {
        qd_compose_start_map(field);
        qd_compose_insert_string(field, "key001");
        qd_compose_insert_uint(field, 20);
        qd_compose_insert_string(field, "key002");
        qd_compose_insert_uint(field, 21);
        qd_compose_end_map(field);
    }

    qd_compose_end_list(field);

    qd_buffer_t *buf = DEQ_HEAD(field->buffers);

    if (qd_buffer_size(buf) != vector0_length) return "Incorrect Length of Buffer";

    char *left  = vector0;
    char *right = (char*) qd_buffer_base(buf);
    int   idx;

    for (idx = 0; idx < vector0_length; idx++) {
        if (*left != *right) return "Pattern Mismatch";
        left++;
        right++;
    }

    qd_compose_free(field);
    return 0;
}

static char *vector1 =
    "\x00\x53\x71"                             // delivery annotations
    "\xd1\x00\x00\x00\x3d\x00\x00\x00\x04"     // map32 with two item pairs
    "\xa3\x06key001"                           // sym8
    "\x52\x0a"                                 // smalluint
    "\xa3\x06key002"                           // sym8
    "\xd0\x00\x00\x00\x22\x00\x00\x00\x04"     // list32 with four items
    "\xa1\x05item1"                            // str8-utf8
    "\xa1\x05item2"                            // str8-utf8
    "\xa1\x05item3"                            // str8-utf8
    "\xd0\x00\x00\x00\x04\x00\x00\x00\x00"     // list32 empty
    ;

static int vector1_length = 69;

static char *test_compose_insert_empty_string(void *context)
{
    char *empty_string = "\x00\x53\x75\xa1\x00";
    qd_composed_field_t *field = qd_compose(QD_PERFORMATIVE_BODY_DATA, 0);
    qd_compose_insert_string(field, 0);

    qd_buffer_t *buf = DEQ_HEAD(field->buffers);

    char *right = (char*) qd_buffer_base(buf);

    for (int idx = 0; idx < qd_buffer_size(buf); idx++) {
        if (empty_string[idx] != right[idx])
            return "Pattern Mismatch";
    }
    qd_compose_free(field);
    return 0;
}

static char *test_compose_insert_empty_symbol(void *context)
{
    char *empty_string = "\x00\x53\x75\xa3\x00";
    qd_composed_field_t *field = qd_compose(QD_PERFORMATIVE_BODY_DATA, 0);
    qd_compose_insert_symbol(field, 0);

    qd_buffer_t *buf = DEQ_HEAD(field->buffers);

    char *right = (char*) qd_buffer_base(buf);

    for (int idx = 0; idx < qd_buffer_size(buf); idx++) {
        if (empty_string[idx] != right[idx])
            return "Pattern Mismatch";
    }
    qd_compose_free(field);
    return 0;
}

static char *test_compose_insert_empty_binary(void *context)
{
    char *empty_string = "\x00\x53\x75\xa0\x00";
    qd_composed_field_t *field = qd_compose(QD_PERFORMATIVE_BODY_DATA, 0);
    qd_compose_insert_binary(field, 0, 0);

    qd_buffer_t *buf = DEQ_HEAD(field->buffers);

    char *right = (char*) qd_buffer_base(buf);

    for (int idx = 0; idx < qd_buffer_size(buf); idx++) {
        if (empty_string[idx] != right[idx])
            return "Pattern Mismatch";
    }
    qd_compose_free(field);
    return 0;
}

static char *test_compose_nested_composites(void *context)
{
    qd_composed_field_t *field = qd_compose(QD_PERFORMATIVE_DELIVERY_ANNOTATIONS, 0);

    qd_compose_start_map(field);

    qd_compose_insert_symbol(field, "key001");
    qd_compose_insert_uint(field, 10);

    qd_compose_insert_symbol(field, "key002");
    qd_compose_start_list(field);

    qd_compose_insert_string(field, "item1");
    qd_compose_insert_string(field, "item2");
    qd_compose_insert_string(field, "item3");

    qd_compose_start_list(field);
    qd_compose_end_list(field);

    qd_compose_end_list(field);
    qd_compose_end_map(field);

    qd_buffer_t *buf = DEQ_HEAD(field->buffers);

    if (qd_buffer_size(buf) != vector1_length) return "Incorrect Length of Buffer";

    char *left  = vector1;
    char *right = (char*) qd_buffer_base(buf);
    int   idx;

    for (idx = 0; idx < vector1_length; idx++) {
        if (*left != *right) return "Pattern Mismatch";
        left++;
        right++;
    }

    qd_compose_free(field);
    return 0;
}

static char *vector2 =
    "\x00\x53\x73"                             // properties
    "\xd0\x00\x00\x00\x83\x00\x00\x00\x1c"     // list32 with 28 items
    "\x40"                                     // null
    "\x42"                                     // false
    "\x41"                                     // true
    "\x43"                                     // uint0
    "\x52\x01"                                 // smalluint
    "\x52\xff"                                 // smalluint
    "\x70\x00\x00\x01\x00"                     // uint
    "\x70\x10\x00\x00\x00"                     // uint
    "\x44"                                     // ulong0
    "\x53\x01"                                 // smallulong
    "\x53\xff"                                 // smallulong
    "\x80\x00\x00\x00\x00\x00\x00\x01\x00"     // ulong
    "\x80\x00\x00\x00\x00\x20\x00\x00\x00"     // ulong
    "\x54\x00"                                 // smallint
    "\x54\x01"                                 // smallint
    "\x54\xff"                                 // smallint
    "\x71\x00\x00\x00\xff"                     // int
    "\x71\x00\x00\x01\x00"                     // int
    "\x55\x00"                                 // smalllong
    "\x55\x01"                                 // smalllong
    "\x55\xff"                                 // smalllong
    "\x81\x00\x00\x00\x00\x00\x00\x00\xff"     // long
    "\x81\x00\x00\x00\x00\x00\x00\x01\x00"     // long
    "\x83\x00\x11\x22\x33\x44\x55\x66\x77"     // timestamp
    "\x98\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f\x00"  // uuid
    "\xa0\x02\x00\x11"                         // vbin8
    "\xa1\x06string"                           // str8-utf8
    "\xa3\x06symbol"                           // sym8
    ;

static int vector2_length = 139;

static char *test_compose_scalars(void *context)
{
    qd_composed_field_t *field = qd_compose(QD_PERFORMATIVE_PROPERTIES, 0);

    qd_compose_start_list(field);

    qd_compose_insert_null(field);

    qd_compose_insert_bool(field, 0);
    qd_compose_insert_bool(field, 1);

    qd_compose_insert_uint(field, 0);
    qd_compose_insert_uint(field, 1);
    qd_compose_insert_uint(field, 255);
    qd_compose_insert_uint(field, 256);
    qd_compose_insert_uint(field, 0x10000000);

    qd_compose_insert_ulong(field, 0);
    qd_compose_insert_ulong(field, 1);
    qd_compose_insert_ulong(field, 255);
    qd_compose_insert_ulong(field, 256);
    qd_compose_insert_ulong(field, 0x20000000);

    qd_compose_insert_int(field, 0);
    qd_compose_insert_int(field, 1);
    qd_compose_insert_int(field, -1);
    qd_compose_insert_int(field, 255);
    qd_compose_insert_int(field, 256);

    qd_compose_insert_long(field, 0);
    qd_compose_insert_long(field, 1);
    qd_compose_insert_long(field, -1);
    qd_compose_insert_long(field, 255);
    qd_compose_insert_long(field, 256);

    qd_compose_insert_timestamp(field, 0x0011223344556677);
    qd_compose_insert_uuid(field, (uint8_t*) "\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f\x00");
    qd_compose_insert_binary(field, (uint8_t*) "\x00\x11", 2);
    qd_compose_insert_string(field, "string");
    qd_compose_insert_symbol(field, "symbol");

    qd_compose_end_list(field);

    qd_buffer_t *buf = DEQ_HEAD(field->buffers);

    if (qd_buffer_size(buf) != vector2_length) return "Incorrect Length of Buffer";

    char *left  = vector2;
    char *right = (char*) qd_buffer_base(buf);
    int   idx;

    for (idx = 0; idx < vector2_length; idx++) {
        if (*left != *right) return "Pattern Mismatch";
        left++;
        right++;
    }

    qd_compose_free(field);
    return 0;
}


// verify composition via a set of sub-fields
static char *vector3 =
    "\x00\x53\x72"                              // message annotations
    "\xD1\x00\x00\x00\x1A\x00\x00\x00\x04"      // map32, 26 bytes, 4 fields
    "\xA1\x04Key1"                              // str8
    "\x70\x00\x00\x03\xE7"                      // uint 999
    "\xA1\x04Key2"                              // str8
    "\x70\x00\x00\x03\x78";                     // uint 888
static int vector3_length = 34;

static char *test_compose_subfields(void *context)
{
    qd_composed_field_t *sub1 = qd_compose_subfield(0);
    qd_compose_insert_string(sub1, "Key1");
    qd_composed_field_t *sub2 = qd_compose_subfield(0);
    qd_compose_insert_uint(sub2, 999);

    qd_composed_field_t *sub3 = qd_compose_subfield(0);
    qd_compose_insert_string(sub3, "Key2");

    //
    qd_composed_field_t *field = qd_compose(QD_PERFORMATIVE_MESSAGE_ANNOTATIONS, 0);
    qd_compose_start_map(field);
    qd_compose_insert_buffers(field, &sub1->buffers);
    if (!DEQ_IS_EMPTY(sub1->buffers)) {
        qd_compose_free(sub1);
        qd_compose_free(sub2);
        qd_compose_free(sub3);
        return "Buffer chain ownership not transferred!";
    }
    qd_compose_free(sub1);
    qd_compose_insert_buffers(field, &sub2->buffers);
    qd_compose_free(sub2);

    qd_compose_insert_buffers(field, &sub3->buffers);
    qd_compose_free(sub3);
    qd_compose_insert_uint(field, 888);
    qd_compose_end_map(field);

    qd_buffer_list_t list;
    qd_compose_take_buffers(field, &list);
    if (!DEQ_IS_EMPTY(field->buffers)) return "Buffer list not removed!";

    qd_compose_free(field);

    if (qd_buffer_list_length(&list) != vector3_length)
        return "Improper encoded length";

    unsigned char *src = (unsigned char *)vector3;
    qd_buffer_t *buf = DEQ_HEAD(list);
    while (buf) {
        unsigned char *c = qd_buffer_base(buf);
        while (c != (qd_buffer_base(buf) + qd_buffer_size(buf))) {
            if (*c != *src) return "Pattern Mismatch";
            c++;
            src++;
        }
        buf = DEQ_NEXT(buf);
    }

    qd_buffer_list_free_buffers(&list);

    return 0;
}


int compose_tests()
{
    int result = 0;
    char *test_group = "compose_tests";

    TEST_CASE(test_compose_list_of_maps, 0);
    TEST_CASE(test_compose_nested_composites, 0);
    TEST_CASE(test_compose_insert_empty_symbol, 0);
    TEST_CASE(test_compose_insert_empty_string, 0);
    TEST_CASE(test_compose_insert_empty_binary, 0);
    TEST_CASE(test_compose_scalars, 0);
    TEST_CASE(test_compose_subfields, 0);

    return result;
}


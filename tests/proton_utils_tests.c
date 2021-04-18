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
#include "qpid/dispatch/proton_utils.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    const char     *encoded;
    size_t          size;
    const char     *expected;
} as_string_vector_t;

as_string_vector_t vectors0[] = {
    {"\x40",                 1, 0},          // null
    {"\x56\x00",             2, "false"},    // boolean
    {"\x56\x01",             2, "true"},     // boolean
    {"\x41",                 1, "true"},     // boolean.true
    {"\x42",                 1, "false"},    // boolean.false
    {"\x50\x55",             2, "85"},       // ubyte
    {"\x51\x55",             2, "85"},       // byte
    {"\x60\x11\x55",         3, "4437"},     // ushort
    {"\x61\x11\x55",         3, "4437"},     // short
    {"\x70\x00\x11\x22\x33", 5, "1122867"},  // uint
    {"\x52\x55",             2, "85"},       // smalluint
    {"\x43",                 1, "0"},        // uint0
    {"\x71\x00\x11\x22\x33", 5, "1122867"},  // int
    {"\x53\x55",             2, "85"},       // smallulong
    {"\x55\x55",             2, "85"},       // smalllong
    {"\x98\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f", 17, "00010203-0405-0607-0809-0a0b0c0d0e0f"}, // uuid
    {"\xa0\x04GB\x01D",      6, 0},          // vbin8
    {"\xa0\x04GBCD",         6, "GBCD"},     // vbin8
    {"\xa1\x04HBCD",         6, "HBCD"},     // str8-utf8
    {"\xa3\x04IBCD",         6, "IBCD"},     // sym8
    {"\x45",                 1, 0},          // list0
    {0, 0, 0}
};

#define MAX_ERROR 1000
static char error[MAX_ERROR];

static char *test_data_as_string(void *context)
{
    as_string_vector_t *vector = vectors0;

    while (vector->encoded) {
        pn_data_t *data = pn_data(0);
        pn_data_decode(data, vector->encoded, vector->size);
        char *result = qdpn_data_as_string(data);
        pn_data_free(data);

        if (result || vector->expected) {
            if ((result == 0 || vector->expected == 0) && result != vector->expected) {
                snprintf(error, MAX_ERROR, "Expected '%s', got '%s'", vector->expected, result);
                free(result);
                return error;
            }

            if (strcmp(result, vector->expected) != 0) {
                snprintf(error, MAX_ERROR, "Expected '%s', got '%s'", vector->expected, result);
                free(result);
                return error;
            }
        }
        free(result);
        vector++;
    }

    return 0;
}


int proton_utils_tests(void)
{
    int result = 0;
    char *test_group = "proton_utils_tests";

    TEST_CASE(test_data_as_string, 0);

    return result;
}


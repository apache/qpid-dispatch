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

#include "qdr_doctest.h"

extern "C" {
#include <../src/router_core/router_core_private.h>
#include <../src/terminus_private.h>
#include <inttypes.h>
#include <qpid/dispatch/router_core.h>
#include <stdint.h>
#include <stdio.h>
}

TEST_CASE("test_safe_snprintf") {
    const int   OUTPUT_SIZE = 128;
    const char *TEST_MESSAGE = "something";
    const int   LEN = strlen(TEST_MESSAGE);

    size_t len;
    char   output[OUTPUT_SIZE];

    SUBCASE("valid_inputs") {
        SUBCASE("") {
            len = safe_snprintf(output, LEN + 10, TEST_MESSAGE);
            CHECK(LEN == len);
            CHECK(output == TEST_MESSAGE);
        }

        SUBCASE("") {
            len = safe_snprintf(output, LEN + 1, TEST_MESSAGE);
            CHECK(LEN == len);
            CHECK(output == TEST_MESSAGE);
        }

        SUBCASE("") {
            len = safe_snprintf(output, LEN, TEST_MESSAGE);
            CHECK(LEN - 1 == len);
            CHECK(output == "somethin");
        }

        SUBCASE("") {
            len = safe_snprintf(output, 0, TEST_MESSAGE);
            CHECK(0 == len);
        }

        SUBCASE("") {
            output[0] = 'a';
            len = safe_snprintf(output, 1, TEST_MESSAGE);
            CHECK(0 == len);
            CHECK('\0' == output[0]);
        }

        SUBCASE("") {
            len = safe_snprintf(output, (int)-1, TEST_MESSAGE);
            CHECK(0 == len);
        }
    }
}

TEST_CASE("test_qdr_terminus_format") {
    SUBCASE("coordinator") {
        const int   SIZE = 128;
        const char *EXPECTED = "{<coordinator>}";
        const int   EXPECTED_LEN = strlen(EXPECTED);

        size_t size = SIZE;
        char   output[SIZE];

        qdr_terminus_t t;
        t.coordinator = true;

        qdr_terminus_format(&t, output, &size);
        CHECK(output == EXPECTED);
        CHECK(size == SIZE - EXPECTED_LEN);
    }

    SUBCASE("empty") {
        char   output[3];
        size_t size = 3;
        output[2] = 'A';

        SUBCASE("") {
            qdr_terminus_format(NULL, output, &size);

            CHECK(output == "{}");
            CHECK(size == 1);
        }
    }
}

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
#include <string.h>
#include <stdint.h>
#include "test_case.h"
#include <qpid/dispatch/router_core.h>

typedef struct {
    qdr_router_version_t version;
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
    bool   expected;
} test_data_t;

static const test_data_t data[] = {
    {
        .version = {.major = 10,
                    .minor = 11,
                    .patch = 12},
        .major = 10,
        .minor = 11,
        .patch = 12,
        .expected = true,
    },
    {
        .version = {.major = 10,
                    .minor = 11,
                    .patch = 12},
        .major = 10,
        .minor = 11,
        .patch = 13,
        .expected = false,
    },
    {
        .version = {.major = 10,
                    .minor = 11,
                    .patch = 12},
        .major = 10,
        .minor = 12,
        .patch = 0,
        .expected = false,
    },
    {
        .version = {.major = 10,
                    .minor = 11,
                    .patch = 12},
        .major = 11,
        .minor = 0,
        .patch = 0,
        .expected = false,
    },
    {
        .version = {.major = 10,
                    .minor = 11,
                    .patch = 12},
        .major = 10,
        .minor = 11,
        .patch = 11,
        .expected = true,
    },
    {
        .version = {.major = 10,
                    .minor = 11,
                    .patch = 12},
        .major = 10,
        .minor = 10,
        .patch = 13,
        .expected = true,
    },
    {
        .version = {.major = 10,
                    .minor = 11,
                    .patch = 12},
        .major = 9,
        .minor = 12,
        .patch = 13,
        .expected = true,
    },
    {
        .version = {.major = 10,
                    .minor = 11,
                    .patch = 12},
        .major = 0,
        .minor = 0,
        .patch = 0,
        .expected = true,
    },
};
const int data_count = (sizeof(data)/sizeof(data[0]));

static char buffer[100];

static char *test_version_compare(void *context)
{

    const test_data_t *p = data;
    for (int i = 0; i < data_count; ++i) {

        if (QDR_ROUTER_VERSION_AT_LEAST(p->version, p->major, p->minor, p->patch) != p->expected) {
            snprintf(buffer, sizeof(buffer), "At least failed: %u.%u.%u / %u.%u.%u e=%s\n",
                     p->version.major, p->version.minor, p->version.patch,
                     p->major, p->minor, p->patch, p->expected ? "true" : "false");
            return buffer;
        }
        if (QDR_ROUTER_VERSION_LESS_THAN(p->version, p->major, p->minor, p->patch) == p->expected) {
            snprintf(buffer, sizeof(buffer), "Less than failed: %u.%u.%u / %u.%u.%u e=%s\n",
                     p->version.major, p->version.minor, p->version.patch,
                     p->major, p->minor, p->patch, p->expected ? "true" : "false");
            return buffer;
        }
        ++p;
    }

    return 0;
}


int version_tests()
{
    int result = 0;
    char *test_group = "version_tests";

    TEST_CASE(test_version_compare, 0);

    return result;
}


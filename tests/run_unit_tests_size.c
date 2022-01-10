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

#include "qpid/dispatch/alloc.h"
#include "qpid/dispatch/buffer.h"
#include "qpid/dispatch/iterator.h"
#include "qpid/dispatch/router.h"

void qd_log_initialize(void);
void qd_log_finalize(void);
void qd_error_initialize();
void qd_router_id_initialize(const char *, const char *);
void qd_router_id_finalize(void);


int message_tests();
int field_tests();
int parse_tests();
int buffer_tests();

// validate router id constructor/encoder
//
static int router_id_tests(void)
{
    int result = 0;
    const char *id;
    const uint8_t *encoded_id;
    size_t len = 0;
    char boundary_id[257];

    qd_router_id_initialize("0", "shortId");
    id = qd_router_id();
    if (strcmp(id, "0/shortId") != 0) {
        fprintf(stderr, "Invalid shortId (%s)\n", id);
        result = 1;
        goto exit;
    }

    encoded_id = qd_router_id_encoded(&len);
    if (len != strlen(id) + 2) {
        fprintf(stderr, "shortId encode failed - bad len\n");
        result = 1;
        goto exit;
    }
    if (encoded_id[0] != QD_AMQP_STR8_UTF8
        || encoded_id[1] != strlen(id)
        || memcmp(&encoded_id[2], id, strlen(id)) != 0) {

        fprintf(stderr, "shortId encode failed - bad format\n");
        result = 1;
        goto exit;
    }

    qd_router_id_finalize();

    //
    // this ID will be exactly 255 chars long (STR8).
    //

    memset(boundary_id, 'B', 253);
    boundary_id[253] = 0;

    qd_router_id_initialize("0", boundary_id);
    id = qd_router_id();
    assert(strlen(id) == 255);
    if (strncmp(id, "0/", 2) != 0 || strcmp(&id[2], boundary_id) != 0) {
        fprintf(stderr, "Invalid boundary 255 id (%s)\n", id);
        result = 1;
        goto exit;
    }

    encoded_id = qd_router_id_encoded(&len);
    if (len != strlen(id) + 2) {
        fprintf(stderr, "bounary 255 encode failed - bad len\n");
        result = 1;
        goto exit;
    }
    if (encoded_id[0] != QD_AMQP_STR8_UTF8
        || encoded_id[1] != strlen(id)
        || memcmp(&encoded_id[2], id, strlen(id)) != 0) {

        fprintf(stderr, "boundary encode failed - bad format\n");
        result = 1;
        goto exit;
    }

    qd_router_id_finalize();

    //
    // this ID will be exactly 256 chars long (STR32).
    //

    memset(boundary_id, 'B', 254);
    boundary_id[255] = 0;

    qd_router_id_initialize("0", boundary_id);
    id = qd_router_id();
    assert(strlen(id) == 256);
    if (strncmp(id, "0/", 2) != 0 || strcmp(&id[2], boundary_id) != 0) {
        fprintf(stderr, "Invalid boundary 256 id (%s)\n", id);
        result = 1;
        goto exit;
    }

    encoded_id = qd_router_id_encoded(&len);
    if (len != strlen(id) + 5) {
        fprintf(stderr, "bounary 256 encode failed - bad len\n");
        result = 1;
        goto exit;
    }
    if (encoded_id[0] != QD_AMQP_STR32_UTF8
        || qd_parse_uint32_decode(&encoded_id[1]) != strlen(id)
        || memcmp(&encoded_id[5], id, strlen(id)) != 0) {

        fprintf(stderr, "boundary 256 encode failed - bad format\n");
        result = 1;
        goto exit;
    }


exit:
    qd_router_id_finalize();
    return result;
}

int main(int argc, char** argv)
{
    size_t buffer_size = 512;
    int result = 0;

    if (argc > 1) {
        buffer_size = atoi(argv[1]);
        if (buffer_size < 1)
            return 1;
    }

    qd_alloc_initialize();
    qd_log_initialize();
    qd_error_initialize();
    qd_buffer_set_size(buffer_size);

    result += router_id_tests();
    qd_router_id_initialize("0", "UnitTestRouter");

    result += message_tests();
    result += field_tests();
    result += parse_tests();
    result += buffer_tests();

    qd_log_finalize();
    qd_alloc_finalize();
    qd_iterator_finalize();
    qd_router_id_finalize();

    return result;
}


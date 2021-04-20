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

#include "qpid/dispatch.h"
#include "qpid/dispatch/buffer.h"

#include <stdio.h>

int tool_tests(void);
int timer_tests(qd_dispatch_t*);
int core_timer_tests(void);
int alloc_tests(void);
int compose_tests(void);
int policy_tests(void);
int failoverlist_tests(void);
int parse_tree_tests(void);
int proton_utils_tests(void);
int version_tests(void);
int hash_tests(void);
int thread_tests(void);


int main(int argc, char** argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <config-file>\n", argv[0]);
        exit(1);
    }
    int result = 0;

    // Call qd_dispatch() first initialize allocator used by other tests.
    qd_dispatch_t *qd = qd_dispatch(0, false);

    qd_dispatch_validate_config(argv[1]);
    if (qd_error_code()) {
        printf("Config failed: %s\n", qd_error_message());
        return 1;
    }

    qd_dispatch_load_config(qd, argv[1]);
    if (qd_error_code()) {
        printf("Config failed: %s\n", qd_error_message());
        return 1;
    }
    result += timer_tests(qd);
    result += tool_tests();
    result += compose_tests();
    result += alloc_tests();
    result += policy_tests();
    result += failoverlist_tests();
    result += parse_tree_tests();
    result += proton_utils_tests();
    result += core_timer_tests();
    result += hash_tests();
    result += thread_tests();

    qd_dispatch_free(qd);       // dispatch_free last.

    return result;
}

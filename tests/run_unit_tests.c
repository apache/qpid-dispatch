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

#include <qpid/dispatch.h>
#include <qpid/dispatch/buffer.h>
#include <stdio.h>

int tool_tests(void);
int timer_tests(void);
int alloc_tests(void);
int server_tests(qd_dispatch_t *qd);
int parse_tests(void);
int compose_tests(void);

int main(int argc, char** argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <config-file>\n", argv[0]);
        exit(1);
    }

    qd_dispatch_t *qd = qd_dispatch(0);
    qd_dispatch_load_config(qd, argv[1]);

    int result = 0;
    result += tool_tests();
    result += alloc_tests();
    result += server_tests(qd);
    result += parse_tests();
    result += compose_tests();
    qd_dispatch_free(qd);
    result += timer_tests();
    return result;
}

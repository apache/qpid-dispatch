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
#include <stdio.h>
#include <string.h>
#include "test_case.h"
#include <qpid/dispatch/buffer.h>


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

    return 0;
}


int buffer_tests()
{
    int result = 0;

    TEST_CASE(test_buffer_list_clone, 0);

    return result;
}


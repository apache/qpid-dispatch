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

#include "qpid/dispatch/buffer.h"

#include "qpid/dispatch/alloc.h"

#include <stdint.h>
#include <string.h>


size_t BUFFER_SIZE     = 512;

ALLOC_DECLARE(qd_buffer_t);
ALLOC_DEFINE_CONFIG(qd_buffer_t, sizeof(qd_buffer_t), &BUFFER_SIZE, 0);


void qd_buffer_set_size(size_t size)
{
    BUFFER_SIZE = size;
}


qd_buffer_t *qd_buffer(void)
{
    qd_buffer_t *buf = new_qd_buffer_t();

    DEQ_ITEM_INIT(buf);
    buf->size   = 0;
    sys_atomic_init(&buf->bfanout, 0);
    return buf;
}


void qd_buffer_free(qd_buffer_t *buf)
{
    if (!buf) return;
    sys_atomic_destroy(&buf->bfanout);
    free_qd_buffer_t(buf);
}


unsigned int qd_buffer_list_clone(qd_buffer_list_t *dst, const qd_buffer_list_t *src)
{
    uint32_t len = 0;
    DEQ_INIT(*dst);
    qd_buffer_t *buf = DEQ_HEAD(*src);
    while (buf) {
        size_t to_copy = qd_buffer_size(buf);
        unsigned char *src = qd_buffer_base(buf);
        len += to_copy;
        while (to_copy) {
            qd_buffer_t *newbuf = qd_buffer();
            size_t count = qd_buffer_capacity(newbuf);
            // default buffer capacity may have changed,
            // so don't assume it will fit:
            if (count > to_copy) count = to_copy;
            memcpy(qd_buffer_cursor(newbuf), src, count);
            qd_buffer_insert(newbuf, count);
            DEQ_INSERT_TAIL(*dst, newbuf);
            src += count;
            to_copy -= count;
        }
        buf = DEQ_NEXT(buf);
    }
    return len;
}


void qd_buffer_list_free_buffers(qd_buffer_list_t *list)
{
    qd_buffer_t *buf = DEQ_HEAD(*list);
    while (buf) {
        DEQ_REMOVE_HEAD(*list);
        qd_buffer_free(buf);
        buf = DEQ_HEAD(*list);
    }
}


unsigned int qd_buffer_list_length(const qd_buffer_list_t *list)
{
    unsigned int len = 0;
    qd_buffer_t *buf = DEQ_HEAD(*list);
    while (buf) {
        len += qd_buffer_size(buf);
        buf = DEQ_NEXT(buf);
    }
    return len;
}


void qd_buffer_list_append(qd_buffer_list_t *buflist, const uint8_t *data, size_t len)
{
    //
    // If len is zero, there's no work to do.
    //
    if (len == 0)
        return;

    //
    // If the buffer list is empty and there's some data, add one empty buffer before we begin.
    //
    if (DEQ_SIZE(*buflist) == 0) {
        qd_buffer_t *buf = qd_buffer();
        DEQ_INSERT_TAIL(*buflist, buf);
    }

    qd_buffer_t *tail = DEQ_TAIL(*buflist);

    while (len > 0) {
        size_t to_copy = MIN(len, qd_buffer_capacity(tail));
        if (to_copy > 0) {
            memcpy(qd_buffer_cursor(tail), data, to_copy);
            qd_buffer_insert(tail, to_copy);
            data += to_copy;
            len  -= to_copy;
        }
        if (len > 0) {
            tail = qd_buffer();
            DEQ_INSERT_TAIL(*buflist, tail);
        }
    }
}

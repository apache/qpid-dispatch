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

#include <qpid/dispatch/buffer.h>
#include <qpid/dispatch/alloc.h>

#include <stdint.h>
#include <string.h>


size_t BUFFER_SIZE     = 512;
static int size_locked = 0;

ALLOC_DECLARE(qd_buffer_t);
ALLOC_DEFINE_CONFIG(qd_buffer_t, sizeof(qd_buffer_t), &BUFFER_SIZE, 0);


void qd_buffer_set_size(size_t size)
{
    assert(!size_locked);
    BUFFER_SIZE = size;
}


qd_buffer_t *qd_buffer(void)
{
    size_locked = 1;
    qd_buffer_t *buf = new_qd_buffer_t();

    DEQ_ITEM_INIT(buf);
    buf->size   = 0;
    sys_atomic_init(&buf->bfanout, 0);
    return buf;
}


void qd_buffer_free(qd_buffer_t *buf)
{
    if (!buf) return;
    free_qd_buffer_t(buf);
}


unsigned char *qd_buffer_base(qd_buffer_t *buf)
{
    return (unsigned char*) &buf[1];
}


unsigned char *qd_buffer_cursor(qd_buffer_t *buf)
{
    return ((unsigned char*) &buf[1]) + buf->size;
}


size_t qd_buffer_capacity(qd_buffer_t *buf)
{
    return BUFFER_SIZE - buf->size;
}


size_t qd_buffer_size(qd_buffer_t *buf)
{
    return buf->size;
}


void qd_buffer_insert(qd_buffer_t *buf, size_t len)
{
    buf->size += len;
    assert(buf->size <= BUFFER_SIZE);
}

void qd_buffer_add_fanout(qd_buffer_t *buf)
{
    sys_atomic_inc(&buf->bfanout);
}

size_t qd_buffer_fanout(qd_buffer_t *buf)
{
    return buf->bfanout;
}


unsigned char *qd_buffer_at(qd_buffer_t *buf, size_t len)
{
    // If the len is greater than the buffer size, we might point to some garbage.
    // We dont want that to happen, so do the assert.
    assert(len <= BUFFER_SIZE);
    return ((unsigned char*) &buf[1]) + len;
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

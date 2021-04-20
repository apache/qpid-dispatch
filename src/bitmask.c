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

#include "qpid/dispatch/bitmask.h"

#include "qpid/dispatch/alloc.h"

#include <assert.h>
#include <stdint.h>

#define QD_BITMASK_LONGS 2
#define QD_BITMASK_BITS  (QD_BITMASK_LONGS * 64)

struct qd_bitmask_t {
    uint64_t array[QD_BITMASK_LONGS];
    int      first_set;
    int      cardinality;
};

ALLOC_DECLARE(qd_bitmask_t);
ALLOC_DEFINE(qd_bitmask_t);

#define MASK_INDEX(num)  (num / 64)
#define MASK_ONEHOT(num) (((uint64_t) 1) << (num % 64))
#define FIRST_NONE    -1
#define FIRST_UNKNOWN -2


int qd_bitmask_width()
{
    return QD_BITMASK_BITS;
}


qd_bitmask_t *qd_bitmask(int initial)
{
    qd_bitmask_t *b = new_qd_bitmask_t();
    if (initial)
        qd_bitmask_set_all(b);
    else
        qd_bitmask_clear_all(b);
    return b;
}


void qd_bitmask_free(qd_bitmask_t *b)
{
    if (!b) return;
    free_qd_bitmask_t(b);
}


void qd_bitmask_set_all(qd_bitmask_t *b)
{
    for (int i = 0; i < QD_BITMASK_LONGS; i++)
        b->array[i] = 0xFFFFFFFFFFFFFFFF;
    b->first_set   = 0;
    b->cardinality = QD_BITMASK_BITS;
}


void qd_bitmask_clear_all(qd_bitmask_t *b)
{
    for (int i = 0; i < QD_BITMASK_LONGS; i++)
        b->array[i] = 0;
    b->first_set   = FIRST_NONE;
    b->cardinality = 0;
}


int qd_bitmask_set_bit(qd_bitmask_t *b, int bitnum)
{
    int old_value = 1;
    assert(bitnum < QD_BITMASK_BITS);
    if ((b->array[MASK_INDEX(bitnum)] & MASK_ONEHOT(bitnum)) == 0) {
        old_value = 0;
        b->cardinality++;
    }

    b->array[MASK_INDEX(bitnum)] |= MASK_ONEHOT(bitnum);
    if (b->first_set > bitnum || b->first_set < 0)
        b->first_set = bitnum;

    return old_value;
}


int qd_bitmask_clear_bit(qd_bitmask_t *b, int bitnum)
{
    int old_value = 0;
    assert(bitnum < QD_BITMASK_BITS);
    if (b->array[MASK_INDEX(bitnum)] & MASK_ONEHOT(bitnum)) {
        old_value = 1;
        b->cardinality--;
    }

    b->array[MASK_INDEX(bitnum)] &= ~(MASK_ONEHOT(bitnum));
    if (b->first_set == bitnum)
        b->first_set = FIRST_UNKNOWN;

    return old_value;
}


int qd_bitmask_value(qd_bitmask_t *b, int bitnum)
{
    return (b->array[MASK_INDEX(bitnum)] & MASK_ONEHOT(bitnum)) ? 1 : 0;
}


int qd_bitmask_first_set(qd_bitmask_t *b, int *bitnum)
{
    //
    // If the passed in qd_bitmask_t *b pointer is null, we want to avoid a crash by returning immediately.
    //
    if (!b)
        return 0;

    if (b->first_set == FIRST_UNKNOWN) {
        b->first_set = FIRST_NONE;
        for (int i = 0; i < QD_BITMASK_LONGS; i++)
            if (b->array[i]) {
                for (int j = 0; j < 64; j++)
                    if ((((uint64_t) 1) << j) & b->array[i]) {
                        b->first_set = i * 64 + j;
                        break;
                    }
                break;
            }
    }

    if (b->first_set == FIRST_NONE)
        return 0;
    *bitnum = b->first_set;
    return 1;
}


int qd_bitmask_cardinality(const qd_bitmask_t *b)
{
    return b->cardinality;
}


bool qd_bitmask_valid_bit_value(int bitnum)
{
    return (bitnum >= 0 && bitnum < QD_BITMASK_BITS);
}


int _qdbm_start(qd_bitmask_t *b)
{
    int v;
    if (qd_bitmask_first_set(b, &v))
        return v;
    return -1;
}


void _qdbm_next(qd_bitmask_t *b, int *v)
{
    if (*v == QD_BITMASK_BITS - 1) {
        *v = -1;
        return;
    }

    int      idx  = MASK_INDEX(*v);
    uint64_t bits = MASK_ONEHOT(*v);
    int      next = *v;

    while (1) {
        next++;
        if (bits & 0x8000000000000000LL) {
            if (++idx == QD_BITMASK_LONGS) {
                *v = -1;
                return;
            }
            bits = 1;
        } else
            bits = bits << 1;

        if (b->array[idx] & bits) {
            *v = next;
            return;
        }
    }
}


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

#include "qpid/dispatch/trace_mask.h"

#include "qpid/dispatch/alloc.h"
#include "qpid/dispatch/hash.h"
#include "qpid/dispatch/iterator.h"
#include "qpid/dispatch/threading.h"

typedef struct {
    qd_hash_handle_t *hash_handle;
    int               maskbit;
    int               link_maskbit;
} qdtm_router_t;

ALLOC_DECLARE(qdtm_router_t);
ALLOC_DEFINE(qdtm_router_t);

struct qd_tracemask_t {
    sys_rwlock_t   *lock;
    qd_hash_t      *hash;
    qdtm_router_t **router_by_mask_bit;
};


qd_tracemask_t *qd_tracemask(void)
{
    qd_tracemask_t *tm = NEW(qd_tracemask_t);
    tm->lock               = sys_rwlock();
    tm->hash               = qd_hash(8, 1, 0);
    tm->router_by_mask_bit = NEW_PTR_ARRAY(qdtm_router_t, qd_bitmask_width());

    for (int i = 0; i < qd_bitmask_width(); i++)
        tm->router_by_mask_bit[i] = 0;
    return tm;
}


void qd_tracemask_free(qd_tracemask_t *tm)
{
    for (int i = 0; i < qd_bitmask_width(); i++) {
        if (tm->router_by_mask_bit[i])
            qd_tracemask_del_router(tm, i);
    }
    free(tm->router_by_mask_bit);

    qd_hash_free(tm->hash);
    sys_rwlock_free(tm->lock);
    free(tm);
}


void qd_tracemask_add_router(qd_tracemask_t *tm, const char *address, int maskbit)
{
    qd_iterator_t *iter = qd_iterator_string(address, ITER_VIEW_ADDRESS_HASH);
    sys_rwlock_wrlock(tm->lock);
    assert(maskbit < qd_bitmask_width() && tm->router_by_mask_bit[maskbit] == 0);
    if (maskbit < qd_bitmask_width() && tm->router_by_mask_bit[maskbit] == 0) {
        qdtm_router_t *router = new_qdtm_router_t();
        router->maskbit = maskbit;
        router->link_maskbit = -1;
        qd_hash_insert(tm->hash, iter, router, &router->hash_handle);
        tm->router_by_mask_bit[maskbit] = router;
    }
    sys_rwlock_unlock(tm->lock);
    qd_iterator_free(iter);
}


void qd_tracemask_del_router(qd_tracemask_t *tm, int maskbit)
{
    sys_rwlock_wrlock(tm->lock);
    assert(maskbit < qd_bitmask_width() && tm->router_by_mask_bit[maskbit] != 0);
    if (maskbit < qd_bitmask_width() && tm->router_by_mask_bit[maskbit] != 0) {
        qdtm_router_t *router = tm->router_by_mask_bit[maskbit];
        qd_hash_remove_by_handle(tm->hash, router->hash_handle);
        qd_hash_handle_free(router->hash_handle);
        tm->router_by_mask_bit[maskbit] = 0;
        free_qdtm_router_t(router);
    }
    sys_rwlock_unlock(tm->lock);
}


void qd_tracemask_set_link(qd_tracemask_t *tm, int router_maskbit, int link_maskbit)
{
    sys_rwlock_wrlock(tm->lock);
    assert(router_maskbit < qd_bitmask_width() && link_maskbit < qd_bitmask_width() &&
           tm->router_by_mask_bit[router_maskbit] != 0);
    if (router_maskbit < qd_bitmask_width() && link_maskbit < qd_bitmask_width() &&
        tm->router_by_mask_bit[router_maskbit] != 0) {
        qdtm_router_t *router = tm->router_by_mask_bit[router_maskbit];
        router->link_maskbit = link_maskbit;
    }
    sys_rwlock_unlock(tm->lock);
}


void qd_tracemask_remove_link(qd_tracemask_t *tm, int router_maskbit)
{
    sys_rwlock_wrlock(tm->lock);
    assert(router_maskbit < qd_bitmask_width() && tm->router_by_mask_bit[router_maskbit] != 0);
    if (router_maskbit < qd_bitmask_width() && tm->router_by_mask_bit[router_maskbit] != 0) {
        qdtm_router_t *router = tm->router_by_mask_bit[router_maskbit];
        router->link_maskbit = -1;
    }
    sys_rwlock_unlock(tm->lock);
}


qd_bitmask_t *qd_tracemask_create(qd_tracemask_t *tm, qd_parsed_field_t *tracelist, int *ingress_index)
{
    qd_bitmask_t *bm    = qd_bitmask(0);
    int           idx   = 0;
    bool          first = true;

    assert(qd_parse_is_list(tracelist));

    sys_rwlock_rdlock(tm->lock);
    qd_parsed_field_t *item   = qd_parse_sub_value(tracelist, idx);
    qdtm_router_t     *router = 0;
    while (item) {
        qd_iterator_t *iter = qd_parse_raw(item);
        qd_iterator_reset_view(iter, ITER_VIEW_NODE_HASH);
        qd_hash_retrieve(tm->hash, iter, (void*) &router);
        if (router) {
            if (router->link_maskbit >= 0)
                qd_bitmask_set_bit(bm, router->link_maskbit);
            if (first)
                *ingress_index = router->maskbit;
        }
        idx++;
        item = qd_parse_sub_value(tracelist, idx);
        first = false;
    }
    sys_rwlock_unlock(tm->lock);
    return bm;
}


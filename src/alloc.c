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

#include <Python.h>
#include <qpid/dispatch/alloc.h>
#include <qpid/dispatch/ctools.h>
#include <qpid/dispatch/log.h>
#include <memory.h>
#include <inttypes.h>
#include <stdio.h>
#include "entity.h"
#include "entity_cache.h"

#define QD_MEMORY_DEBUG 1

const char *QD_ALLOCATOR_TYPE = "allocator";

typedef struct qd_alloc_type_t qd_alloc_type_t;
typedef struct qd_alloc_item_t qd_alloc_item_t;

struct qd_alloc_type_t {
    DEQ_LINKS(qd_alloc_type_t);
    qd_alloc_type_desc_t *desc;
};

DEQ_DECLARE(qd_alloc_type_t, qd_alloc_type_list_t);

#define PATTERN_FRONT 0xdeadbeef
#define PATTERN_BACK  0xbabecafe

struct qd_alloc_item_t {
    DEQ_LINKS(qd_alloc_item_t);
#ifdef QD_MEMORY_DEBUG
    qd_alloc_type_desc_t *desc;
    uint32_t              header;
#endif
};

DEQ_DECLARE(qd_alloc_item_t, qd_alloc_item_list_t);


struct qd_alloc_pool_t {
    DEQ_LINKS(qd_alloc_pool_t);
    qd_alloc_item_list_t free_list;
};

qd_alloc_config_t qd_alloc_default_config_big   = {16,  32, 0};
qd_alloc_config_t qd_alloc_default_config_small = {64, 128, 0};
#define BIG_THRESHOLD 256

static sys_mutex_t          *init_lock = 0;
static qd_alloc_type_list_t  type_list;
static char *debug_dump = 0;

static void qd_alloc_init(qd_alloc_type_desc_t *desc)
{
    sys_mutex_lock(init_lock);

    if (!desc->global_pool) {
        desc->total_size = desc->type_size;
        if (desc->additional_size)
            desc->total_size += *desc->additional_size;

        if (desc->config == 0)
            desc->config = desc->total_size > BIG_THRESHOLD ?
                &qd_alloc_default_config_big : &qd_alloc_default_config_small;

        assert (desc->config->local_free_list_max >= desc->config->transfer_batch_size);

        desc->global_pool = NEW(qd_alloc_pool_t);
        DEQ_INIT(desc->global_pool->free_list);
        desc->lock = sys_mutex();
        DEQ_INIT(desc->tpool_list);
        desc->stats = NEW(qd_alloc_stats_t);
        memset(desc->stats, 0, sizeof(qd_alloc_stats_t));

        qd_alloc_type_t *type_item = NEW(qd_alloc_type_t);
        DEQ_ITEM_INIT(type_item);
        type_item->desc = desc;
        DEQ_INSERT_TAIL(type_list, type_item);

        desc->header  = PATTERN_FRONT;
        desc->trailer = PATTERN_BACK;
        qd_entity_cache_add(QD_ALLOCATOR_TYPE, type_item);
    }

    sys_mutex_unlock(init_lock);
}


void *qd_alloc(qd_alloc_type_desc_t *desc, qd_alloc_pool_t **tpool)
{
    int idx;

    //
    // If the descriptor is not initialized, set it up now.
    //
    if (desc->trailer != PATTERN_BACK)
        qd_alloc_init(desc);

    //
    // If this is the thread's first pass through here, allocate the
    // thread-local pool for this type.
    //
    if (*tpool == 0) {
        *tpool = NEW(qd_alloc_pool_t);
        DEQ_ITEM_INIT(*tpool);
        DEQ_INIT((*tpool)->free_list);
        sys_mutex_lock(desc->lock);
        DEQ_INSERT_TAIL(desc->tpool_list, *tpool);
        sys_mutex_unlock(desc->lock);
    }

    qd_alloc_pool_t *pool = *tpool;

    //
    // Fast case: If there's an item on the local free list, take it off the
    // list and return it.  Since everything we've touched is thread-local,
    // there is no need to acquire a lock.
    //
    qd_alloc_item_t *item = DEQ_HEAD(pool->free_list);
    if (item) {
        DEQ_REMOVE_HEAD(pool->free_list);
#ifdef QD_MEMORY_DEBUG
        item->desc   = desc;
        item->header = PATTERN_FRONT;
        *((uint32_t*) ((void*) &item[1] + desc->total_size))= PATTERN_BACK;
#endif
        return &item[1];
    }

    //
    // The local free list is empty, we need to either rebalance a batch
    // of items from the global list or go to the heap to get new memory.
    //
    sys_mutex_lock(desc->lock);
    if (DEQ_SIZE(desc->global_pool->free_list) >= desc->config->transfer_batch_size) {
        //
        // Rebalance a full batch from the global free list to the thread list.
        //
        desc->stats->batches_rebalanced_to_threads++;
        desc->stats->held_by_threads += desc->config->transfer_batch_size;
        for (idx = 0; idx < desc->config->transfer_batch_size; idx++) {
            item = DEQ_HEAD(desc->global_pool->free_list);
            DEQ_REMOVE_HEAD(desc->global_pool->free_list);
            DEQ_INSERT_TAIL(pool->free_list, item);
        }
    } else {
        //
        // Allocate a full batch from the heap and put it on the thread list.
        //
        for (idx = 0; idx < desc->config->transfer_batch_size; idx++) {
            item = (qd_alloc_item_t*) malloc(sizeof(qd_alloc_item_t) + desc->total_size
#ifdef QD_MEMORY_DEBUG
                                             + sizeof(uint32_t)
#endif
                                             );
            if (item == 0)
                break;
            DEQ_ITEM_INIT(item);
            DEQ_INSERT_TAIL(pool->free_list, item);
            desc->stats->held_by_threads++;
            desc->stats->total_alloc_from_heap++;
        }
    }
    sys_mutex_unlock(desc->lock);

    item = DEQ_HEAD(pool->free_list);
    if (item) {
        DEQ_REMOVE_HEAD(pool->free_list);
#ifdef QD_MEMORY_DEBUG
        item->desc = desc;
        item->header = PATTERN_FRONT;
        *((uint32_t*) ((void*) &item[1] + desc->total_size))= PATTERN_BACK;
#endif
        return &item[1];
    }

    return 0;
}


void qd_dealloc(qd_alloc_type_desc_t *desc, qd_alloc_pool_t **tpool, void *p)
{
    if (!p) return;
    qd_alloc_item_t *item = ((qd_alloc_item_t*) p) - 1;
    int              idx;

#ifdef QD_MEMORY_DEBUG
    assert (desc->header  == PATTERN_FRONT);
    assert (desc->trailer == PATTERN_BACK);
    assert (item->header  == PATTERN_FRONT);
    assert (*((uint32_t*) (p + desc->total_size)) == PATTERN_BACK);
    assert (item->desc == desc);  // Check for double-free
    item->desc = 0;
#endif

    //
    // If this is the thread's first pass through here, allocate the
    // thread-local pool for this type.
    //
    if (*tpool == 0) {
        *tpool = NEW(qd_alloc_pool_t);
        DEQ_ITEM_INIT(*tpool);
        DEQ_INIT((*tpool)->free_list);
        sys_mutex_lock(desc->lock);
        DEQ_INSERT_TAIL(desc->tpool_list, *tpool);
        sys_mutex_unlock(desc->lock);
    }

    qd_alloc_pool_t *pool = *tpool;

    DEQ_INSERT_TAIL(pool->free_list, item);

    if (DEQ_SIZE(pool->free_list) <= desc->config->local_free_list_max)
        return;

    //
    // We've exceeded the maximum size of the local free list.  A batch must be
    // rebalanced back to the global list.
    //
    sys_mutex_lock(desc->lock);
    desc->stats->batches_rebalanced_to_global++;
    desc->stats->held_by_threads -= desc->config->transfer_batch_size;
    for (idx = 0; idx < desc->config->transfer_batch_size; idx++) {
        item = DEQ_HEAD(pool->free_list);
        DEQ_REMOVE_HEAD(pool->free_list);
        DEQ_INSERT_TAIL(desc->global_pool->free_list, item);
    }

    //
    // If there's a global_free_list size limit, remove items until the limit is
    // not exceeded.
    //
    if (desc->config->global_free_list_max != 0) {
        while (DEQ_SIZE(desc->global_pool->free_list) > desc->config->global_free_list_max) {
            item = DEQ_HEAD(desc->global_pool->free_list);
            DEQ_REMOVE_HEAD(desc->global_pool->free_list);
            free(item);
            desc->stats->total_free_to_heap++;
        }
    }

    sys_mutex_unlock(desc->lock);
}


void qd_alloc_initialize(void)
{
    init_lock = sys_mutex();
    DEQ_INIT(type_list);
}


void qd_alloc_finalize(void)
{
    //
    // Note that the logging facility is already finalized by the time this is called.
    // We will dump debugging information into debug_dump if specified.
    //
    // The assumption coming into this finalizer is that all allocations have been
    // released.  Any non-released objects shall be flagged.
    //

    //
    // Note: By the time we get here, the server threads have been joined and there is
    //       only the main thread remaining.  There is therefore no reason to be 
    //       concerned about locking.
    //

    qd_alloc_item_t *item;
    qd_alloc_type_t *type_item = DEQ_HEAD(type_list);

    FILE *dump_file = 0;
    if (debug_dump) {
        dump_file = fopen(debug_dump, "w");
        free(debug_dump);
    }

    while (type_item) {
        qd_entity_cache_remove(QD_ALLOCATOR_TYPE, type_item);
        qd_alloc_type_desc_t *desc = type_item->desc;

        //
        // Reclaim the items on the global free pool
        //
        item = DEQ_HEAD(desc->global_pool->free_list);
        while (item) {
            DEQ_REMOVE_HEAD(desc->global_pool->free_list);
            free(item);
            desc->stats->total_free_to_heap++;
            item = DEQ_HEAD(desc->global_pool->free_list);
        }
        free(desc->global_pool);
        desc->global_pool = 0;

        //
        // Reclaim the items on thread pools
        //
        qd_alloc_pool_t *tpool = DEQ_HEAD(desc->tpool_list);
        while (tpool) {
            item = DEQ_HEAD(tpool->free_list);
            while (item) {
                DEQ_REMOVE_HEAD(tpool->free_list);
                free(item);
                desc->stats->total_free_to_heap++;
                item = DEQ_HEAD(tpool->free_list);
            }

            DEQ_REMOVE_HEAD(desc->tpool_list);
            free(tpool);
            tpool = DEQ_HEAD(desc->tpool_list);
        }

        //
        // Check the stats for lost items
        //
        if (dump_file && desc->stats->total_free_to_heap < desc->stats->total_alloc_from_heap)
            fprintf(dump_file,
                    "alloc.c: Items of type '%s' remain allocated at shutdown: %"PRId64"\n", 
                    desc->type_name,
                    desc->stats->total_alloc_from_heap - desc->stats->total_free_to_heap);

        //
        // Reclaim the descriptor components
        //
        free(desc->stats);
        sys_mutex_free(desc->lock);
        desc->lock = 0;
        desc->trailer = 0;

        DEQ_REMOVE_HEAD(type_list);
        free(type_item);
        type_item = DEQ_HEAD(type_list);
    }

    sys_mutex_free(init_lock);
    if (dump_file) fclose(dump_file);
}


qd_error_t qd_entity_refresh_allocator(qd_entity_t* entity, void *impl) {
    qd_alloc_type_t *alloc_type = (qd_alloc_type_t*) impl;
    if (qd_entity_set_string(entity, "typeName", alloc_type->desc->type_name) == 0 &&
        qd_entity_set_long(entity, "typeSize", alloc_type->desc->total_size) == 0 &&
        qd_entity_set_long(entity, "transferBatchSize", alloc_type->desc->config->transfer_batch_size) == 0 &&
        qd_entity_set_long(entity, "localFreeListMax", alloc_type->desc->config->local_free_list_max) == 0 &&
        qd_entity_set_long(entity, "globalFreeListMax", alloc_type->desc->config->global_free_list_max) == 0 &&
        qd_entity_set_long(entity, "totalAllocFromHeap", alloc_type->desc->stats->total_alloc_from_heap) == 0 &&
        qd_entity_set_long(entity, "totalFreeToHeap", alloc_type->desc->stats->total_free_to_heap) == 0 &&
        qd_entity_set_long(entity, "heldByThreads", alloc_type->desc->stats->held_by_threads) == 0 &&
        qd_entity_set_long(entity, "batchesRebalancedToThreads", alloc_type->desc->stats->batches_rebalanced_to_threads) == 0 &&
        qd_entity_set_long(entity, "batchesRebalancedToGlobal", alloc_type->desc->stats->batches_rebalanced_to_global) == 0)
        return QD_ERROR_NONE;
    return qd_error_code();
}

void qd_alloc_debug_dump(const char *file) {
    debug_dump = file ? strdup(file) : 0;
}

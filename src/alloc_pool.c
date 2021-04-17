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

#include "qpid/dispatch/alloc_pool.h"

#include "config.h"
#include "entity.h"
#include "entity_cache.h"
#include "qd_asan_interface.h"

#include "qpid/dispatch/alloc.h"
#include "qpid/dispatch/ctools.h"
#include "qpid/dispatch/log.h"

#include <inttypes.h>
#include <memory.h>
#include <stdio.h>

#ifdef QD_MEMORY_DEBUG
#include "log_private.h"

#include <execinfo.h>
#endif

const char *QD_ALLOCATOR_TYPE = "allocator";

typedef struct qd_alloc_type_t          qd_alloc_type_t;
typedef struct qd_alloc_item_t          qd_alloc_item_t;
typedef struct qd_alloc_chunk_t         qd_alloc_chunk_t;
typedef struct qd_alloc_linked_stack_t  qd_alloc_linked_stack_t;

#define PATTERN_FRONT 0xdeadbeef
#define PATTERN_BACK  0xbabecafe
#define STACK_DEPTH   10

struct qd_alloc_item_t {
    uintmax_t             sequence;  // uintmax_t ensures proper alignment of following data
#ifdef QD_MEMORY_DEBUG
    qd_alloc_type_desc_t *desc;
    DEQ_LINKS(qd_alloc_item_t);
    void                 *backtrace[STACK_DEPTH];
    int                   backtrace_size;
    struct timeval        timestamp;
    uint32_t              header;
#endif
};

#ifdef QD_MEMORY_DEBUG
DEQ_DECLARE(qd_alloc_item_t, qd_alloc_item_list_t);
#endif

struct qd_alloc_type_t {
    DEQ_LINKS(qd_alloc_type_t);
    qd_alloc_type_desc_t *desc;
#ifdef QD_MEMORY_DEBUG
    qd_alloc_item_list_t  allocated;
#endif
};

DEQ_DECLARE(qd_alloc_type_t, qd_alloc_type_list_t);


// Leak Suppressions
// A list of items that are known to leak.
//
// Be a Good Citizen when adding unexpected leaks to this list and include the
// corresponding JIRA in a comment!
//
// When nirvana is reached and we have fixed all of these then please remove
// -DQD_MEMORY_DEBUG=1 from RuntimeChecks.cmake and .travis.yml when doing
// RUNTIME=asan testing
//
#if QD_MEMORY_STATS
static const char *leaking_types[] = {
    "qd_hash_handle_t",       // DISPATCH-1696

    // system_tests_edge_router (centos7)
    // DISPATCH-1699

    "qd_message_t",
    "qd_message_content_t",
    "qdr_delivery_t",
    "qd_delivery_state_t", // DISPATCH-2082: See comments in JIRA
    "qd_link_ref_t",

    // system_tests_priority (centos7)
    // DISPATCH-1700

    "qd_iterator_t",
    "qdr_action_t",
    "qdr_field_t",
    "qd_buffer_t",
    "qd_bitmask_t",

    "qd_parsed_field_t",  // DISPATCH-1701
    "qdr_delivery_ref_t", // DISPATCH-1702

    0};
#endif

//128 has been chosen because many CPUs arch use an
//adjacent line prefetching optimization that load
//2*cache line bytes in batch
#define CHUNK_SIZE 128/sizeof(void*)

struct qd_alloc_chunk_t {
    qd_alloc_chunk_t     *prev;                 //do not use DEQ_LINKS here: field position could affect access cost
    qd_alloc_item_t      *items[CHUNK_SIZE];
    qd_alloc_chunk_t     *next;
};

struct qd_alloc_linked_stack_t {
    //the base
    qd_alloc_chunk_t     *top_chunk;
    uint32_t              top;                  //qd_alloc_item* top_item = top_chunk->items[top+1] <-> top > 0
    uint64_t              size;
    qd_alloc_chunk_t      base_chunk;
};


static inline void init_stack(qd_alloc_linked_stack_t *stack)
{
    stack->top_chunk = &stack->base_chunk;
    stack->top_chunk->next = NULL;
    stack->top = 0;
    stack->size = 0;
}

static inline void prev_chunk_stack(qd_alloc_linked_stack_t *const stack)
{
    const uint32_t chunk_size = CHUNK_SIZE;
    assert(stack->top == 0);
    assert(stack->size != 0);
    assert(stack->top_chunk != &stack->base_chunk);
    qd_alloc_chunk_t *prev = stack->top_chunk->prev;
    //TODO(franz):  stack->top_chunk could be passed externally and walked its nexts
    //              to recycle the last chunk.
    //              Just need to pay attention to null out released_chunk->prev->next
    //              to make it unreachable from the stack
    stack->top_chunk = prev;
    stack->top = chunk_size;
}

static inline qd_alloc_item_t *pop_stack(qd_alloc_linked_stack_t *const stack)
{
    if (stack->top == 0) {
        if (stack->size == 0) {
            assert(stack->top_chunk == &stack->base_chunk);
            return NULL;
        }
        prev_chunk_stack(stack);
    }
    assert(stack->top > 0);
    stack->top--;
    assert(stack->top < CHUNK_SIZE);
    assert(stack->size > 0);
    stack->size--;
    qd_alloc_item_t *item = stack->top_chunk->items[stack->top];
    assert(item != NULL);
    return item;
}

static inline void free_stack_chunks(qd_alloc_linked_stack_t *stack)
{
    assert(stack->size == 0);
    //the assumption here is that next is always correctly set
    qd_alloc_chunk_t *chunk = stack->base_chunk.next;
    while (chunk != NULL) {
        qd_alloc_chunk_t *next = chunk->next;
        free(chunk);
        chunk = next;
    }
}

static inline bool next_chunk_stack(qd_alloc_linked_stack_t *const stack)
{
    assert(stack->top == CHUNK_SIZE);
    qd_alloc_chunk_t *top = stack->top_chunk->next;
    if (top == NULL) {
        top = NEW(qd_alloc_chunk_t);
        if (top == NULL) {
            return false;
        }
        stack->top_chunk->next = top;
        top->prev = stack->top_chunk;
        top->next = NULL;
    }
    assert(top->prev == stack->top_chunk);
    assert(stack->top_chunk->next == top);
    stack->top_chunk = top;
    stack->top = 0;
    return true;
}

static inline bool push_stack(qd_alloc_linked_stack_t *stack, qd_alloc_item_t *item)
{
    const uint32_t chunk_size = CHUNK_SIZE;
    if (stack->top == chunk_size) {
        if (!next_chunk_stack(stack)) {
            return false;
        }
    }
    assert(stack->top < chunk_size);
    stack->size++;
    stack->top_chunk->items[stack->top] = item;
    stack->top++;
    return true;
}

static inline int unordered_move_stack(qd_alloc_linked_stack_t *from, qd_alloc_linked_stack_t *to, uint32_t length)
{
    length = from->size < length ? from->size : length;
    if (length == 0) {
        return 0;
    }
    uint32_t remaining = length;
    const uint32_t chunk_size = CHUNK_SIZE;
    while (remaining > 0) {
        //top will tell us how much data we could memcpy
        uint32_t to_copy = remaining;
        if (from->top == 0) {
            prev_chunk_stack(from);
        }
        to_copy = from->top < to_copy ? from->top : to_copy;
        if (to->top == chunk_size) {
            if (!next_chunk_stack(to)) {
                return length - remaining;
            }
        }
        uint32_t remaining_to = chunk_size - to->top;
        to_copy = remaining_to < to_copy ? remaining_to : to_copy;
        from->top -= to_copy;
        memcpy(&to->top_chunk->items[to->top], &from->top_chunk->items[from->top], to_copy * sizeof(qd_alloc_item_t *));
        to->top += to_copy;
        to->size += to_copy;
        from->size -= to_copy;
        remaining -= to_copy;
    }
    return length;
}

struct qd_alloc_pool_t {
    DEQ_LINKS(qd_alloc_pool_t);
    qd_alloc_linked_stack_t free_list;
};

qd_alloc_config_t qd_alloc_default_config_big   = {16,  32, -1};
qd_alloc_config_t qd_alloc_default_config_small = {64, 128, -1};
qd_alloc_config_t qd_alloc_default_config_asan  = { 1,   0,  0};
#define BIG_THRESHOLD 2000

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

        desc->config = &qd_alloc_default_config_asan;
//        if (desc->config == 0)
//            desc->config = desc->total_size > BIG_THRESHOLD ?
//                &qd_alloc_default_config_big : &qd_alloc_default_config_small;
//
//        assert (desc->config->local_free_list_max >= desc->config->transfer_batch_size);

        desc->global_pool = NEW(qd_alloc_pool_t);
        DEQ_ITEM_INIT(desc->global_pool);
        init_stack(&desc->global_pool->free_list);
        desc->lock = sys_mutex();
        DEQ_INIT(desc->tpool_list);
#if QD_MEMORY_STATS
        desc->stats = NEW(qd_alloc_stats_t);
        ZERO(desc->stats);
#endif

        qd_alloc_type_t *type_item = NEW(qd_alloc_type_t);
        DEQ_ITEM_INIT(type_item);
        type_item->desc = desc;
        desc->debug = type_item;
#ifdef QD_MEMORY_DEBUG
        DEQ_INIT(type_item->allocated);
#endif
        DEQ_INSERT_TAIL(type_list, type_item);

        desc->header  = PATTERN_FRONT;
        desc->trailer = PATTERN_BACK;
        qd_entity_cache_add(QD_ALLOCATOR_TYPE, type_item);
    }

    sys_mutex_unlock(init_lock);
}


/* coverity[+alloc] */
void *qd_alloc(qd_alloc_type_desc_t *desc, qd_alloc_pool_t **tpool)
{
    int idx;

    //
    // If the descriptor is not initialized, set it up now.
    //
    if (desc->header != PATTERN_FRONT)
        qd_alloc_init(desc);

    //
    // If this is the thread's first pass through here, allocate the
    // thread-local pool for this type.
    //
    if (*tpool == 0) {
        NEW_CACHE_ALIGNED(qd_alloc_pool_t, *tpool);
        DEQ_ITEM_INIT(*tpool);
        init_stack(&(*tpool)->free_list);
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
    qd_alloc_item_t *item = pop_stack(&pool->free_list);
    if (item) {
        ASAN_UNPOISON_MEMORY_REGION(&item[1], desc->total_size);
#ifdef QD_MEMORY_DEBUG
        item->desc   = desc;
        item->backtrace_size = backtrace(item->backtrace, STACK_DEPTH);
        gettimeofday(&item->timestamp, NULL);
        qd_alloc_type_t *qtype = (qd_alloc_type_t*) desc->debug;
        sys_mutex_lock(desc->lock);
        DEQ_INSERT_TAIL(qtype->allocated, item);
        sys_mutex_unlock(desc->lock);
        item->header = PATTERN_FRONT;
        const uint32_t pb = PATTERN_BACK;
        memcpy((char*) &item[1] + desc->total_size, &pb, sizeof(pb));
        QD_MEMORY_FILL(&item[1], QD_MEMORY_INIT, desc->total_size);
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
        const int moved = unordered_move_stack(&desc->global_pool->free_list, &pool->free_list,
                                               desc->config->transfer_batch_size);
        assert(moved == desc->config->transfer_batch_size);
#if QD_MEMORY_STATS
        desc->stats->batches_rebalanced_to_threads++;
        desc->stats->held_by_threads += moved;
#endif
    } else {
        //
        // Allocate a full batch from the heap and put it on the thread list.
        //
        //TODO(franz):
        //  -   would be better to allocate in batches == transfer_batch_size
        //      and put a small (== sizeof(transfer_batch_size)) ref_count to help the final free
        //  -   could be beneficial directly to delink a chunk?
        for (idx = 0; idx < desc->config->transfer_batch_size; idx++) {
            size_t size = sizeof(qd_alloc_item_t) + desc->total_size
#ifdef QD_MEMORY_DEBUG
                                                  + sizeof(uint32_t)
#endif
                ;
            ALLOC_CACHE_ALIGNED(size, item);
            if (item == 0)
                break;
#ifdef QD_MEMORY_DEBUG
            DEQ_ITEM_INIT(item);
#endif
            if (!push_stack(&pool->free_list, item)) {
                free(item);
                break;
            }
            item->sequence = 0;
            ASAN_POISON_MEMORY_REGION(&item[1], desc->total_size);
#if QD_MEMORY_STATS
            desc->stats->held_by_threads++;
            desc->stats->total_alloc_from_heap++;
#endif
        }
    }
    sys_mutex_unlock(desc->lock);

    item = pop_stack(&pool->free_list);
    if (item) {
        ASAN_UNPOISON_MEMORY_REGION(&item[1], desc->total_size);
#ifdef QD_MEMORY_DEBUG
        item->desc = desc;
        item->backtrace_size = backtrace(item->backtrace, STACK_DEPTH);
        gettimeofday(&item->timestamp, NULL);
        qd_alloc_type_t *qtype = (qd_alloc_type_t*) desc->debug;
        sys_mutex_lock(desc->lock);
        DEQ_INSERT_TAIL(qtype->allocated, item);
        sys_mutex_unlock(desc->lock);
        item->header = PATTERN_FRONT;
        const uint32_t pb = PATTERN_BACK;
        memcpy((char*) &item[1] + desc->total_size, &pb, sizeof(pb));
        QD_MEMORY_FILL(&item[1], QD_MEMORY_INIT, desc->total_size);
#endif
        return &item[1];
    }

    return 0;
}


/* coverity[+free : arg-2] */
void qd_dealloc(qd_alloc_type_desc_t *desc, qd_alloc_pool_t **tpool, char *p)
{
    if (!p) return;
    qd_alloc_item_t *item = ((qd_alloc_item_t*) p) - 1;

#ifdef QD_MEMORY_DEBUG
    assert (desc->header  == PATTERN_FRONT);
    assert (desc->trailer == PATTERN_BACK);
    assert (item->header  == PATTERN_FRONT);
    const uint32_t pb = PATTERN_BACK;
    (void)pb;  // prevent unused warning
    assert (memcmp(p + desc->total_size, &pb, sizeof(pb)) == 0);
    assert (item->desc == desc);  // Check for double-free
    qd_alloc_type_t *qtype = (qd_alloc_type_t*) desc->debug;
    sys_mutex_lock(desc->lock);
    DEQ_REMOVE(qtype->allocated, item);
    sys_mutex_unlock(desc->lock);
    item->desc = 0;
    QD_MEMORY_FILL(p, QD_MEMORY_FREE, desc->total_size);
#endif
    ASAN_POISON_MEMORY_REGION(p, desc->total_size);

    //
    // If this is the thread's first pass through here, allocate the
    // thread-local pool for this type.
    //
    if (*tpool == 0) {
        *tpool = NEW(qd_alloc_pool_t);
        DEQ_ITEM_INIT(*tpool);
        init_stack(&(*tpool)->free_list);
        sys_mutex_lock(desc->lock);
        DEQ_INSERT_TAIL(desc->tpool_list, *tpool);
        sys_mutex_unlock(desc->lock);
    }

    qd_alloc_pool_t *pool = *tpool;

    item->sequence++;
    if (!push_stack(&pool->free_list, item)) {
        free(item);
    }

    if (DEQ_SIZE(pool->free_list) < desc->config->local_free_list_max)
        return;

    //
    // We've exceeded the maximum size of the local free list.  A batch must be
    // rebalanced back to the global list.
    //
    sys_mutex_lock(desc->lock);
    const int moved = unordered_move_stack(&pool->free_list, &desc->global_pool->free_list,
                                           desc->config->transfer_batch_size);
    assert(moved == desc->config->transfer_batch_size);
#if QD_MEMORY_STATS
    desc->stats->batches_rebalanced_to_global++;
    desc->stats->held_by_threads -= moved;
#endif
    //
    // If there's a global_free_list size limit, remove items until the limit is
    // not exceeded.
    //
    if (desc->config->global_free_list_max != -1) {
        while (DEQ_SIZE(desc->global_pool->free_list) > desc->config->global_free_list_max) {
            item = pop_stack(&desc->global_pool->free_list);
            free(item);
#if QD_MEMORY_STATS
            desc->stats->total_free_to_heap++;
#endif
        }
    }

    sys_mutex_unlock(desc->lock);
}

// use-after-free; no way to check if memory is freed before
// accessing it from `qd_alloc_deref_safe_ptr`
ATTRIBUTE_NO_SANITIZE_ADDRESS
uint32_t qd_alloc_sequence(void *p)
{
    if (!p)
        return 0;

    qd_alloc_item_t *item = ((qd_alloc_item_t*) p) - 1;
#ifdef QD_MEMORY_DEBUG
    // ensure p actually points to an alloc pool item
    assert(item->header == PATTERN_FRONT);
#endif
    return item->sequence;
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
#ifdef QD_MEMORY_DEBUG
    const char *last_leak = 0;
#endif

    FILE *dump_file = 0;
    if (debug_dump) {
        dump_file = fopen(debug_dump, "w");
    }
#if QD_MEMORY_DEBUG
    // For memory debug builds make it very hard to ignore leaks
    else {
        dump_file = stderr;
    }
#endif

    while (type_item) {
        qd_entity_cache_remove(QD_ALLOCATOR_TYPE, type_item);
        qd_alloc_type_desc_t *desc = type_item->desc;

        //
        // Reclaim the items on the global free pool
        //
        item = pop_stack(&desc->global_pool->free_list);
        while (item) {
            free(item);
#if QD_MEMORY_STATS
            desc->stats->total_free_to_heap++;
#endif
            item = pop_stack(&desc->global_pool->free_list);
        }
        free_stack_chunks(&desc->global_pool->free_list);
        free(desc->global_pool);
        desc->global_pool = 0;

        //
        // Reclaim the items on thread pools
        //
        qd_alloc_pool_t *tpool = DEQ_HEAD(desc->tpool_list);
        while (tpool) {
            item = pop_stack(&tpool->free_list);
            while (item) {
                free(item);
#if QD_MEMORY_STATS
                desc->stats->total_free_to_heap++;
#endif
                item = pop_stack(&tpool->free_list);
            }
            DEQ_REMOVE_HEAD(desc->tpool_list);
            free_stack_chunks(&tpool->free_list);
            free(tpool);
            tpool = DEQ_HEAD(desc->tpool_list);
        }

        //
        // Check the stats for lost items
        //
#if QD_MEMORY_STATS
        if (dump_file && desc->stats->total_free_to_heap < desc->stats->total_alloc_from_heap) {
            bool suppressed = false;
            for (int i = 0; leaking_types[i]; ++i) {
                if (strcmp(desc->type_name, leaking_types[i]) == 0) {
                    suppressed = true;
                    break;
                }
            }
            fprintf(dump_file,
                    "alloc.c: Items of type '%s' remain allocated at shutdown: %"PRId64"%s\n",
                    desc->type_name,
                    desc->stats->total_alloc_from_heap - desc->stats->total_free_to_heap,
                    suppressed ? " (SUPPRESSED)" : "");

#ifdef QD_MEMORY_DEBUG
            qd_alloc_type_t *qtype = (qd_alloc_type_t*) desc->debug;
            qd_alloc_item_t *item = DEQ_HEAD(qtype->allocated);
            char buf[100];
            while (item) {
                DEQ_REMOVE_HEAD(qtype->allocated);
                char **strings = backtrace_symbols(item->backtrace, item->backtrace_size);

                if (!suppressed) {
                    // DISPATCH-1795: avoid output noise by only printing
                    // backtraces for leaks that are not suppressed
                    qd_log_formatted_time(&item->timestamp, buf, 100);
                    fprintf(dump_file, "Leak: %s type: %s address: %p\n",
                            buf, desc->type_name, (void *)(&item[1]));
                    for (size_t i = 0; i < item->backtrace_size; i++)
                        fprintf(dump_file, "%s\n", strings[i]);
                    fprintf(dump_file, "\n");
                    last_leak = desc->type_name;
                }
                free(strings);

                // free the item to prevent ASAN from also reporting this leak.
                // Since this is a custom heap ASAN will dump the first
                // malloc() of the object - not the last time it was allocated
                // from the pool.
                free(item);
                item = DEQ_HEAD(qtype->allocated);
            }
#endif
        }
#endif

        //
        // Reclaim the descriptor components
        //
#if QD_MEMORY_STATS
        free(desc->stats);
#endif
        sys_mutex_free(desc->lock);
        desc->lock = 0;
        desc->trailer = 0;

        DEQ_REMOVE_HEAD(type_list);
        free(type_item);
        type_item = DEQ_HEAD(type_list);
    }

    sys_mutex_free(init_lock);
    if (debug_dump) {
        fclose(dump_file);
        free(debug_dump);
    }

#ifdef QD_MEMORY_DEBUG
    if (last_leak) {
        fprintf(stderr, "ERROR: Aborted due to unexpected alloc pool leak of type '%s'\n", last_leak);
        fflush(0);
        abort();
    }
#endif
}


qd_error_t qd_entity_refresh_allocator(qd_entity_t* entity, void *impl) {
    qd_alloc_type_t *alloc_type = (qd_alloc_type_t*) impl;
    if (qd_entity_set_string(entity, "typeName", alloc_type->desc->type_name) == 0 &&
        qd_entity_set_long(entity, "typeSize", alloc_type->desc->total_size) == 0 &&
        qd_entity_set_long(entity, "transferBatchSize", alloc_type->desc->config->transfer_batch_size) == 0 &&
        qd_entity_set_long(entity, "localFreeListMax", alloc_type->desc->config->local_free_list_max) == 0 &&
        qd_entity_set_long(entity, "globalFreeListMax", alloc_type->desc->config->global_free_list_max) == 0
#if QD_MEMORY_STATS
        && qd_entity_set_long(entity, "totalAllocFromHeap", alloc_type->desc->stats->total_alloc_from_heap) == 0 &&
        qd_entity_set_long(entity, "totalFreeToHeap", alloc_type->desc->stats->total_free_to_heap) == 0 &&
        qd_entity_set_long(entity, "heldByThreads", alloc_type->desc->stats->held_by_threads) == 0 &&
        qd_entity_set_long(entity, "batchesRebalancedToThreads", alloc_type->desc->stats->batches_rebalanced_to_threads) == 0 &&
        qd_entity_set_long(entity, "batchesRebalancedToGlobal", alloc_type->desc->stats->batches_rebalanced_to_global) == 0
#endif
        )
        return QD_ERROR_NONE;
    return qd_error_code();
}

void qd_alloc_debug_dump(const char *file) {
    debug_dump = file ? strdup(file) : 0;
}

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



#include <stdio.h>
#include <string.h>
#include "alloc.h"
#include <qpid/dispatch/hash.h>
#include <qpid/dispatch/ctools.h>

typedef struct qd_hash_item_t {
    DEQ_LINKS(struct qd_hash_item_t);
    unsigned char *key;
    union {
        void       *val;
        const void *val_const;
    } v;
} qd_hash_item_t;

ALLOC_DECLARE(qd_hash_item_t);
ALLOC_DEFINE(qd_hash_item_t);
DEQ_DECLARE(qd_hash_item_t, items_t);


typedef struct bucket_t {
    items_t items;
} bucket_t;


struct qd_hash_t {
    bucket_t     *buckets;
    unsigned int  bucket_count;
    unsigned int  bucket_mask;
    int           batch_size;
    size_t        size;
    int           is_const;
};


struct qd_hash_handle_t {
    bucket_t    *bucket;
    qd_hash_item_t *item;
};

ALLOC_DECLARE(qd_hash_handle_t);
ALLOC_DEFINE(qd_hash_handle_t);


qd_hash_t *qd_hash(int bucket_exponent, int batch_size, int value_is_const)
{
    int i;
    qd_hash_t *h = NEW(qd_hash_t);

    if (!h)
        return 0;

    h->bucket_count = 1 << bucket_exponent;
    h->bucket_mask  = h->bucket_count - 1;
    h->batch_size   = batch_size;
    h->size         = 0;
    h->is_const     = value_is_const;
    h->buckets = NEW_ARRAY(bucket_t, h->bucket_count);
    for (i = 0; i < h->bucket_count; i++) {
        DEQ_INIT(h->buckets[i].items);
    }

    return h;
}

//remove the given item from the given bucket of the given hash
//return the key if non-null key pointer given, otherwise, free the memory
static void qd_hash_internal_remove_item(qd_hash_t *h, bucket_t *bucket, qd_hash_item_t *item, unsigned char **key) {
    if (key)
        *key = item->key;
    else
        free(item->key);
    DEQ_REMOVE(bucket->items, item);
    free_qd_hash_item_t(item);
    h->size--;
}

void qd_hash_free(qd_hash_t *h)
{
    if (!h) return;
    qd_hash_item_t *item;
    int             idx;

    for (idx = 0; idx < h->bucket_count; idx++) {
        item = DEQ_HEAD(h->buckets[idx].items);
        while (item) {
            qd_hash_internal_remove_item(h, &h->buckets[idx], item, 0);
            item = DEQ_HEAD(h->buckets[idx].items);
        }
    }
    free(h->buckets);
    free(h);
}


size_t qd_hash_size(qd_hash_t *h)
{
    return h ? h->size : 0;
}


static qd_hash_item_t *qd_hash_internal_insert(qd_hash_t *h, qd_iterator_t *key, int *exists, qd_hash_handle_t **handle)
{
    unsigned long   idx  = qd_iterator_hash_view(key) & h->bucket_mask;
    qd_hash_item_t *item = DEQ_HEAD(h->buckets[idx].items);

    while (item) {
        if (qd_iterator_equal(key, item->key))
            break;
        item = DEQ_NEXT(item);
    }

    if (item) {
        *exists = 1;
        if (handle)
            *handle = 0;
        return item;
    }

    item = new_qd_hash_item_t();
    if (!item)
        return 0;

    DEQ_ITEM_INIT(item);
    item->key = qd_iterator_copy(key);

    DEQ_INSERT_TAIL(h->buckets[idx].items, item);
    h->size++;
    *exists = 0;

    //
    // If a pointer to a handle-pointer was supplied, create a handle for this item.
    //
    if (handle) {
        *handle = new_qd_hash_handle_t();
        (*handle)->bucket = &h->buckets[idx];
        (*handle)->item   = item;
    }

    return item;
}


qd_error_t qd_hash_insert(qd_hash_t *h, qd_iterator_t *key, void *val, qd_hash_handle_t **handle)
{
    int             exists = 0;
    qd_hash_item_t *item   = qd_hash_internal_insert(h, key, &exists, handle);

    if (!item)
        return QD_ERROR_ALLOC;

    if (exists)
        return QD_ERROR_ALREADY_EXISTS;

    item->v.val = val;

    return QD_ERROR_NONE;
}


qd_error_t qd_hash_insert_const(qd_hash_t *h, qd_iterator_t *key, const void *val, qd_hash_handle_t **handle)
{
    assert(h->is_const);

    int             exists = 0;
    qd_hash_item_t *item  = qd_hash_internal_insert(h, key, &exists, handle);

    if (!item)
        return QD_ERROR_ALLOC;

    if (exists)
        return QD_ERROR_ALREADY_EXISTS;

    item->v.val_const = val;

    return QD_ERROR_NONE;
}


static qd_hash_item_t *qd_hash_internal_retrieve_with_hash(qd_hash_t *h, uint32_t hash, qd_iterator_t *key)
{
    uint32_t        idx  = hash & h->bucket_mask;
	qd_hash_item_t *item = DEQ_HEAD(h->buckets[idx].items);

	while (item) {
		if (qd_iterator_equal(key, item->key))
			break;
		item = DEQ_NEXT(item);
	}

	return item;
}


static qd_hash_item_t *qd_hash_internal_retrieve(qd_hash_t *h, qd_iterator_t *key)
{
    uint32_t hash = qd_iterator_hash_view(key);
    return qd_hash_internal_retrieve_with_hash(h, hash, key);
}


void qd_hash_retrieve_prefix(qd_hash_t *h, qd_iterator_t *iter, void **val)
{
	//Hash individual segments by iterating thru the octets in the iterator.
	qd_iterator_hash_view_segments(iter);

	uint32_t hash = 0;

	qd_hash_item_t *item;
	while (qd_iterator_next_segment(iter, &hash)) {
		item = qd_hash_internal_retrieve_with_hash(h, hash, iter);
		if (item)
			break;
	}

	if (item)
		*val = item->v.val;
	else
		*val = 0;
}


void qd_hash_retrieve_prefix_const(qd_hash_t *h, qd_iterator_t *iter, const void **val)
{
    assert(h->is_const);

    //Hash individual segments by iterating thru the octets in the iterator.
    qd_iterator_hash_view_segments(iter);

    uint32_t hash = 0;

    qd_hash_item_t *item;

    while (qd_iterator_next_segment(iter, &hash)) {
        item = qd_hash_internal_retrieve_with_hash(h, hash, iter);
        if (item)
            break;
    }

    if (item)
        *val = item->v.val_const;
    else
        *val = 0;
}


qd_error_t qd_hash_retrieve(qd_hash_t *h, qd_iterator_t *key, void **val)
{
    qd_hash_item_t *item = qd_hash_internal_retrieve(h, key);
    if (item)
        *val = item->v.val;
    else
        *val = 0;

    return QD_ERROR_NONE;
}


qd_error_t qd_hash_retrieve_const(qd_hash_t *h, qd_iterator_t *key, const void **val)
{
    assert(h->is_const);

    qd_hash_item_t *item = qd_hash_internal_retrieve(h, key);
    if (item)
        *val = item->v.val_const;
    else
        *val = 0;

    return QD_ERROR_NONE;
}


qd_error_t qd_hash_remove(qd_hash_t *h, qd_iterator_t *key)
{
    //the retrieve function will re-apply the bucket_mask, but that is ok
    //we apply it here because we need the bucket index to do the remove
    uint32_t        idx  = qd_iterator_hash_view(key) & h->bucket_mask;
    qd_hash_item_t *item = qd_hash_internal_retrieve_with_hash(h, idx, key);
    if (!item)
        return QD_ERROR_NOT_FOUND;

    qd_hash_internal_remove_item(h, &h->buckets[idx], item, 0);
    return QD_ERROR_NONE;
}


void qd_hash_handle_free(qd_hash_handle_t *handle)
{
    if (handle)
        free_qd_hash_handle_t(handle);
}


const unsigned char *qd_hash_key_by_handle(const qd_hash_handle_t *handle)
{
    if (handle)
        return handle->item->key;
    return 0;
}


qd_error_t qd_hash_remove_by_handle(qd_hash_t *h, qd_hash_handle_t *handle)
{
    unsigned char *key   = 0;
    qd_error_t     error = qd_hash_remove_by_handle2(h, handle, &key);
    if (key)
        free(key);
    return error;
}


qd_error_t qd_hash_remove_by_handle2(qd_hash_t *h, qd_hash_handle_t *handle, unsigned char **key)
{
    if (!handle)
        return QD_ERROR_NOT_FOUND;
    qd_hash_internal_remove_item(h, handle->bucket, handle->item, key);
    return QD_ERROR_NONE;
}

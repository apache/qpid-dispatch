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

#include "qpid/dispatch/hash.h"

#include "qpid/dispatch/alloc.h"
#include "qpid/dispatch/ctools.h"

#include <string.h>

typedef struct qd_hash_item_t {
    DEQ_LINKS(struct qd_hash_item_t);
    unsigned char *key;
    union {
        void       *val;
        const void *val_const;
    } v;
    qd_hash_handle_t *handle;
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


static bucket_t *qd_hash_get_bucket_iter(qd_hash_t *h, qd_iterator_t *key)
{
    uint32_t idx = qd_iterator_hash_view(key) & h->bucket_mask;
    return &h->buckets[idx];
}


static bucket_t *qd_hash_get_bucket_str(qd_hash_t *h, const unsigned char *key)
{
    uint32_t hash = HASH_INIT;
    while (*key) {
        hash = HASH_COMPUTE(hash, *key++);
    }

    return &h->buckets[(hash & h->bucket_mask)];
}


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
    if (key) {
        *key = item->key;
    }
    else {
        free(item->key);
        item->key = 0;
    }
    DEQ_REMOVE(bucket->items, item);

    //
    // We are going to free this item, so we will set the
    // item pointer on the hash_handle to zero so nobody with
    // access to the hash_handle can ever try to get to this freed item.
    // The item and the hash_handle can be freed independent of one another.
    //
    if (item->handle) {
        item->handle->item   = 0;
    }
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


// ownership of key is transfered to the qd_hash_item_t
static qd_hash_item_t *qd_hash_internal_insert(qd_hash_t *h, bucket_t *bucket, unsigned char *key, int *exists, qd_hash_handle_t **handle)
{
    qd_hash_item_t *item = DEQ_HEAD(bucket->items);

    while (item) {
        if (strcmp((const char *) key, (const char *) item->key) == 0)
            break;
        item = DEQ_NEXT(item);
    }

    if (item) {
        *exists = 1;
        if (handle) {
            //
            // If the item already exists, we return the item and also return a zero hash handle.
            // This means that there is ever only one hash handle for an item.
            //
            *handle = 0;
        }
        return item;
    }

    item = new_qd_hash_item_t();
    if (!item)
        return 0;
    item->handle = 0;

    DEQ_ITEM_INIT(item);
    item->key = key;

    DEQ_INSERT_TAIL(bucket->items, item);
    h->size++;
    *exists = 0;

    //
    // If a pointer to a handle-pointer was supplied, create a handle for this item.
    //
    if (handle) {
        *handle = new_qd_hash_handle_t();
        (*handle)->bucket = bucket;
        (*handle)->item   = item;

        //
        // There is ever only one hash_handle that points to an item.
        // We will store that hash_handle in the item itself because
        // when the item is freed, the item pointer on its associated hash_handle will
        // be set to zero so that nobody can try to access the item via the handle after
        // the item is freed.
        //
        item->handle = *handle;
    }

    return item;
}


qd_error_t qd_hash_insert(qd_hash_t *h, qd_iterator_t *key, void *val, qd_hash_handle_t **handle)
{
    int       exists = 0;
    bucket_t *bucket = qd_hash_get_bucket_iter(h, key);
    unsigned char *k = qd_iterator_copy(key);

    if (!k)
        return QD_ERROR_ALLOC;

    qd_hash_item_t *item   = qd_hash_internal_insert(h, bucket, k, &exists, handle);

    if (!item) {
        free(k);
        return QD_ERROR_ALLOC;
    }

    if (exists) {
        free(k);
        return QD_ERROR_ALREADY_EXISTS;
    }

    item->v.val = val;

    return QD_ERROR_NONE;
}


qd_error_t qd_hash_insert_const(qd_hash_t *h, qd_iterator_t *key, const void *val, qd_hash_handle_t **handle)
{
    assert(h->is_const);

    int       exists = 0;
    bucket_t *bucket = qd_hash_get_bucket_iter(h, key);
    unsigned char *k = qd_iterator_copy(key);

    if (!k)
        return QD_ERROR_ALLOC;

    qd_hash_item_t *item  = qd_hash_internal_insert(h, bucket, k, &exists, handle);

    if (!item) {
        free(k);
        return QD_ERROR_ALLOC;
    }

    if (exists) {
        free(k);
        return QD_ERROR_ALREADY_EXISTS;
    }

    item->v.val_const = val;

    return QD_ERROR_NONE;
}


qd_error_t qd_hash_insert_str(qd_hash_t *h, const unsigned char *key, void *val, qd_hash_handle_t **handle)
{
    int       exists = 0;
    bucket_t *bucket = qd_hash_get_bucket_str(h, key);
    unsigned char *k = (unsigned char *) strdup((const char *) key);

    if (!k)
        return QD_ERROR_ALLOC;

    qd_hash_item_t *item   = qd_hash_internal_insert(h, bucket, k, &exists, handle);

    if (!item) {
        free(k);
        return QD_ERROR_ALLOC;
    }

    if (exists) {
        free(k);
        return QD_ERROR_ALREADY_EXISTS;
    }

    item->v.val = val;

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

	qd_hash_item_t *item = 0;
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

    qd_hash_item_t *item = 0;
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
    if (!key) {
        *val = 0;
        return QD_ERROR_NONE;
    }

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


static qd_hash_item_t *qd_hash_internal_get_item_str(qd_hash_t *h, bucket_t *bucket, const unsigned char *key)
{
	qd_hash_item_t *item = DEQ_HEAD(bucket->items);

	while (item) {
		if (strcmp((const char *) key, (const char *) item->key) == 0) {
            return item;
        }
		item = DEQ_NEXT(item);
	}

	return 0;
}


qd_error_t qd_hash_retrieve_str(qd_hash_t *h, const unsigned char *key, void **val)
{
	qd_hash_item_t *item = qd_hash_internal_get_item_str(h,
                                                         qd_hash_get_bucket_str(h, key),
                                                         key);
    if (item) {
        *val = item->v.val;
	} else {
        *val = 0;
    }
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


qd_error_t qd_hash_remove_str(qd_hash_t *h, const unsigned char *key)
{
    bucket_t     *bucket = qd_hash_get_bucket_str(h, key);
    qd_hash_item_t *item = qd_hash_internal_get_item_str(h, bucket, key);
    if (!item)
        return QD_ERROR_NOT_FOUND;

    qd_hash_internal_remove_item(h, bucket, item, 0);
    return QD_ERROR_NONE;
}


void qd_hash_handle_free(qd_hash_handle_t *handle)
{
    if (handle) {
        //
        // The hash handle is being freed. if the handle points to an item, make sure to zero out the
        // item's handle so it cannot be dereferenced later.
        //
        if (handle->item && handle->item->handle) {
            handle->item->handle = 0;
        }
        free_qd_hash_handle_t(handle);
    }
}


const unsigned char *qd_hash_key_by_handle(const qd_hash_handle_t *handle)
{
    if (handle && handle->item)
        return handle->item->key;
    return 0;
}


qd_error_t qd_hash_remove_by_handle(qd_hash_t *h, qd_hash_handle_t *handle)
{
    if (!handle)
        return QD_ERROR_NONE;

    unsigned char *key   = 0;
    qd_error_t     error = qd_hash_remove_by_handle2(h, handle, &key);
    if (key)
        free(key);

    return error;
}


qd_error_t qd_hash_remove_by_handle2(qd_hash_t *h, qd_hash_handle_t *handle, unsigned char **key)
{
    //
    // If the handle is not supplied or if the supplied handle has no item, we don't want to proceed
    // removing the item by handle.
    //
    if (!handle || !handle->item)
        return QD_ERROR_NOT_FOUND;
    qd_hash_internal_remove_item(h, handle->bucket, handle->item, key);
    return QD_ERROR_NONE;
}

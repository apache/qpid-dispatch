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

#include <qpid/dispatch/hash.h>
#include <qpid/dispatch/ctools.h>
#include <qpid/dispatch/alloc.h>
#include <stdio.h>
#include <string.h>

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


// djb2 hash algorithm
static unsigned long qd_hash_function(qd_field_iterator_t *iter)
{
    unsigned long hash = 5381;
    int c;

    qd_field_iterator_reset(iter);
    while (!qd_field_iterator_end(iter)) {
        c = (int) qd_field_iterator_octet(iter);
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    }

    return hash;
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


void qd_hash_free(qd_hash_t *h)
{
    if (!h) return;
    qd_hash_item_t *item;
    int             idx;

    for (idx = 0; idx < h->bucket_count; idx++) {
        item = DEQ_HEAD(h->buckets[idx].items);
        while (item) {
            free(item->key);
            free_qd_hash_item_t(item);
            DEQ_REMOVE_HEAD(h->buckets[idx].items);
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


static qd_hash_item_t *qd_hash_internal_insert(qd_hash_t *h, qd_field_iterator_t *key, int *exists, qd_hash_handle_t **handle)
{
    unsigned long   idx  = qd_hash_function(key) & h->bucket_mask;
    qd_hash_item_t *item = DEQ_HEAD(h->buckets[idx].items);

    while (item) {
        if (qd_field_iterator_equal(key, item->key))
            break;
        item = item->next;
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
    item->key = qd_field_iterator_copy(key);

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


qd_error_t qd_hash_insert(qd_hash_t *h, qd_field_iterator_t *key, void *val, qd_hash_handle_t **handle)
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


qd_error_t qd_hash_insert_const(qd_hash_t *h, qd_field_iterator_t *key, const void *val, qd_hash_handle_t **handle)
{
    assert(h->is_const);

    int             error = 0;
    qd_hash_item_t *item  = qd_hash_internal_insert(h, key, &error, handle);

    if (item)
        item->v.val_const = val;
    return error;
}


static qd_hash_item_t *qd_hash_internal_retrieve(qd_hash_t *h, qd_field_iterator_t *key)
{
    unsigned long   idx  = qd_hash_function(key) & h->bucket_mask;
    qd_hash_item_t *item = DEQ_HEAD(h->buckets[idx].items);

    while (item) {
        if (qd_field_iterator_equal(key, item->key))
            break;
        item = item->next;
    }

    return item;
}


qd_error_t qd_hash_retrieve(qd_hash_t *h, qd_field_iterator_t *key, void **val)
{
    qd_hash_item_t *item = qd_hash_internal_retrieve(h, key);
    if (item)
        *val = item->v.val;
    else
        *val = 0;

    return QD_ERROR_NONE;
}


qd_error_t qd_hash_retrieve_const(qd_hash_t *h, qd_field_iterator_t *key, const void **val)
{
    assert(h->is_const);

    qd_hash_item_t *item = qd_hash_internal_retrieve(h, key);
    if (item)
        *val = item->v.val_const;
    else
        *val = 0;

    return QD_ERROR_NONE;
}


qd_error_t qd_hash_remove(qd_hash_t *h, qd_field_iterator_t *key)
{
    unsigned long   idx  = qd_hash_function(key) & h->bucket_mask;
    qd_hash_item_t *item = DEQ_HEAD(h->buckets[idx].items);

    while (item) {
        if (qd_field_iterator_equal(key, item->key))
            break;
        item = item->next;
    }

    if (item) {
        free(item->key);
        DEQ_REMOVE(h->buckets[idx].items, item);
        free_qd_hash_item_t(item);
        h->size--;
        return QD_ERROR_NONE;
    }

    return QD_ERROR_NOT_FOUND;
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
    *key = handle->item->key;
    DEQ_REMOVE(handle->bucket->items, handle->item);
    free_qd_hash_item_t(handle->item);
    h->size--;
    return QD_ERROR_NONE;
}

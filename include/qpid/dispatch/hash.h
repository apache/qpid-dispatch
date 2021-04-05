#ifndef __dispatch_hash_h__
#define __dispatch_hash_h__ 1
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

/**@file
 * Hash table.
 */

#include "qpid/dispatch/error.h"
#include "qpid/dispatch/iterator.h"

#include <stdlib.h>

#define HASH_INIT ((uint32_t) 5381)
#define HASH_COMPUTE(HASH, OCTET) ((((HASH)<<5) + (HASH)) + (uint32_t)(OCTET))  // hash * 33 + octet

typedef struct qd_hash_t        qd_hash_t;
typedef struct qd_hash_handle_t qd_hash_handle_t;

qd_hash_t *qd_hash(int bucket_exponent, int batch_size, int value_is_const);
void qd_hash_free(qd_hash_t *h);

size_t qd_hash_size(qd_hash_t *h);

qd_error_t qd_hash_insert(qd_hash_t *h, qd_iterator_t *key, void *val, qd_hash_handle_t **handle);
qd_error_t qd_hash_insert_const(qd_hash_t *h, qd_iterator_t *key, const void *val, qd_hash_handle_t **handle);
qd_error_t qd_hash_insert_str(qd_hash_t *h, const unsigned char *key, void *val, qd_hash_handle_t **handle);

qd_error_t qd_hash_retrieve(qd_hash_t *h, qd_iterator_t *key, void **val);
qd_error_t qd_hash_retrieve_const(qd_hash_t *h, qd_iterator_t *key, const void **val);
qd_error_t qd_hash_retrieve_str(qd_hash_t *h, const unsigned char *key, void **val);

qd_error_t qd_hash_remove(qd_hash_t *h, qd_iterator_t *key);
qd_error_t qd_hash_remove_str(qd_hash_t *h, const unsigned char *key);

void qd_hash_handle_free(qd_hash_handle_t *handle);
const unsigned char *qd_hash_key_by_handle(const qd_hash_handle_t *handle);
qd_error_t qd_hash_remove_by_handle(qd_hash_t *h, qd_hash_handle_t *handle);
qd_error_t qd_hash_remove_by_handle2(qd_hash_t *h, qd_hash_handle_t *handle, unsigned char **key);

/**
 * Retrieves the value (qd_address_t) based on the hash by progressively first exact matching or prefix matching the address components.
 * If the iterator contains an address string 'policy/org/apache/dev' and the hashtable contains the hash for 'policy/org',
 * this function will look for the following in that order
 *  1. 'policy/org/apache/dev' - no match, proceed to step 2
 *  2. 'policy/org/apache' - no match, proceed to step 3
 *  3. 'policy/org' - We got a match here. This match is a prefix match - return the qd_address_t associated with this
 *
 * @param qd_hash_t
 * @param iter An iterator containing the address string to search on.
 * @param **val The qd_address_t value if there is a full or prefix match.
 */
void qd_hash_retrieve_prefix(qd_hash_t *h, qd_iterator_t *iter, void **val);

/**
 * Same as qd_hash_retrieve_prefix but returns the value as a constant which cannot be modified.
 * @see qd_hash_retrieve_prefix
 */
void qd_hash_retrieve_prefix_const(qd_hash_t *h, qd_iterator_t *iter, const void **val);

#endif

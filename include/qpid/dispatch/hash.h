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

#include <stdlib.h>
#include <qpid/dispatch/iterator.h>
#include <qpid/dispatch/error.h>

typedef struct qd_hash_t        qd_hash_t;
typedef struct qd_hash_handle_t qd_hash_handle_t;

qd_hash_t *qd_hash(int bucket_exponent, int batch_size, int value_is_const);
void qd_hash_free(qd_hash_t *h);

size_t qd_hash_size(qd_hash_t *h);
qd_error_t qd_hash_insert(qd_hash_t *h, qd_field_iterator_t *key, void *val, qd_hash_handle_t **handle);
qd_error_t qd_hash_insert_const(qd_hash_t *h, qd_field_iterator_t *key, const void *val, qd_hash_handle_t **handle);
qd_error_t qd_hash_retrieve(qd_hash_t *h, qd_field_iterator_t *key, void **val);
qd_error_t qd_hash_retrieve_const(qd_hash_t *h, qd_field_iterator_t *key, const void **val);
qd_error_t qd_hash_remove(qd_hash_t *h, qd_field_iterator_t *key);

void qd_hash_handle_free(qd_hash_handle_t *handle);
const unsigned char *qd_hash_key_by_handle(const qd_hash_handle_t *handle);
qd_error_t qd_hash_remove_by_handle(qd_hash_t *h, qd_hash_handle_t *handle);
qd_error_t qd_hash_remove_by_handle2(qd_hash_t *h, qd_hash_handle_t *handle, unsigned char **key);


#endif

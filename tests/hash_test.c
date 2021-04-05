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

#include "test_case.h"

#include "qpid/dispatch/iterator.h"

#include <stdio.h>
#include <string.h>

static const unsigned char *keys[] = {
    (const unsigned char *) "/global/foo",
    (const unsigned char *) "non-address key",
    (const unsigned char *) "amqp://host/global/sub",
    (const unsigned char *) "",
};

enum { key_count = sizeof(keys) / sizeof(keys[0]) };


// add using iterator key, lookup and remove using string key
//
static char *test_iter_keys(void *context)
{

    char *result = 0;
    qd_hash_handle_t *handles[key_count];
    qd_iterator_t *i_key = 0;
    qd_hash_t *hash = qd_hash(4, 10, false);
    if (!hash)
        return "hash table allocation failed";

    //
    // add using iterator, lookup and remove using string
    //

    for (int i = 0; i < key_count; ++i) {
        const unsigned char *key = keys[i];
        qd_hash_handle_t **handle = &handles[i];

        i_key = qd_iterator_string((const char *)key, ITER_VIEW_ALL);
        if (qd_hash_insert(hash, i_key, (void *)key, handle) != QD_ERROR_NONE) {
            result = "hash table insert failed";
            goto done;
        }

        if (strcmp((const char *) qd_hash_key_by_handle(*handle), (const char *)key) != 0) {
            result = "hash handle key did not match";
            goto done;
        }

        qd_iterator_free(i_key);
        i_key = 0;
    }

    if (qd_hash_size(hash) != key_count) {
        result = "hash size is incorrect";
        goto done;
    }

    for (int i = 0; i < key_count; ++i) {
        const unsigned char *key = keys[i];
        unsigned char *value;
        if (qd_hash_retrieve_str(hash, key, (void **)&value) != QD_ERROR_NONE) {
            result = "Key lookup failed";
            goto done;
        }
        if (strcmp((const char *)value, (const char *)key) != 0) {
            result= "key value mismatch";
            goto done;
        }
    }

    for (int i = 0; i < key_count; ++i) {
        const unsigned char *key = keys[i];

        if (qd_hash_remove_str(hash, key) != QD_ERROR_NONE) {
            result = "str key remove failed";
            goto done;
        }
        qd_hash_handle_free(handles[i]);
    }

done:
    qd_iterator_free(i_key);
    qd_hash_free(hash);
    return result;
}


// add using string key, lookup and remove using iterator key
//
static char *test_str_keys(void *context)
{
    char *result = 0;
    qd_iterator_t *i_key = 0;
    qd_hash_handle_t *handles[key_count];
    qd_hash_t *hash = qd_hash(4, 10, false);
    if (!hash)
        return "hash table allocation failed";

    for (int i = 0; i < key_count; ++i) {
        const unsigned char *key = keys[i];
        qd_hash_handle_t **handle = &handles[i];

        if (qd_hash_insert_str(hash, key, (void *)key, handle) != QD_ERROR_NONE) {
            result = "hash table insert failed";
            goto done;
        }

        if (strcmp((const char *) qd_hash_key_by_handle(*handle), (const char *)key) != 0) {
            result = "hash handle key did not match";
            goto done;
        }
    }

    if (qd_hash_size(hash) != key_count) {
        result = "hash size is incorrect";
        goto done;
    }

    for (int i = 0; i < key_count; ++i) {
        i_key = qd_iterator_string((const char *)keys[i], ITER_VIEW_ALL);
        unsigned char *value;
        if (qd_hash_retrieve(hash, i_key, (void **)&value) != QD_ERROR_NONE) {
            result =  "Iterator key lookup failed";
            goto done;
        }

        if (strcmp((const char *)value, (const char *)keys[i]) != 0) {
            result = "key value mismatch";
            goto done;
        }
        qd_iterator_free(i_key);
        i_key = 0;
    }

    for (int i = 0; i < key_count; ++i) {
        i_key = qd_iterator_string((const char *)keys[i], ITER_VIEW_ALL);
        if (qd_hash_remove(hash, i_key) != QD_ERROR_NONE) {
            result = "str key remove failed";
            goto done;
        }
        qd_hash_handle_free(handles[i]);
        qd_iterator_free(i_key);
        i_key = 0;
    }

done:
    qd_iterator_free(i_key);
    qd_hash_free(hash);
    return result;
}


// test lookup and remove failures using iterators
//
static char *test_iter_bad(void *context)
{
    char *result = 0;
    qd_iterator_t *i_key = 0;
    qd_hash_handle_t *handles[key_count];
    qd_hash_t *hash = qd_hash(4, 10, false);
    if (!hash)
        return "hash table allocation failed";

    for (int i = 0; i < key_count; ++i) {
        const unsigned char *key = keys[i];
        qd_hash_handle_t **handle = &handles[i];

        i_key = qd_iterator_string((const char *)key, ITER_VIEW_ALL);
        if (qd_hash_insert(hash, i_key, (void *)key, handle) != QD_ERROR_NONE) {
            result = "hash table insert failed";
            goto done;
        }

        if (strcmp((const char *) qd_hash_key_by_handle(*handle), (const char *)key) != 0) {
            result = "hash handle key did not match";
            goto done;
        }
        qd_iterator_free(i_key);
        i_key = 0;
    }

    if (qd_hash_size(hash) != key_count) {
        result = "hash size is incorrect";
        goto done;
    }

    i_key = qd_iterator_string((const char *)"I DO NOT EXIST", ITER_VIEW_ALL);
    void *value = (void *)i_key;   // just a non-zero value

    //  key not found
    qd_hash_retrieve(hash, i_key, &value);   // does not return error code, but sets value to 0
    if (value != 0) {
        result = "expected hash retrieve to find nothing";
        goto done;
    }

    // remove should return error
    if (qd_hash_remove(hash, i_key) != QD_ERROR_NOT_FOUND) {
        result = "expected hash remove to fail (not found)";
        goto done;
    }

    qd_iterator_free(i_key);
    i_key = 0;

    // cleanup

    for (int i = 0; i < key_count; ++i) {
        const unsigned char *key = keys[i];
        if (qd_hash_remove_str(hash, key) != QD_ERROR_NONE) {
            result = "str key remove failed";
            goto done;
        }
        qd_hash_handle_free(handles[i]);
    }

done:
    qd_iterator_free(i_key);
    qd_hash_free(hash);
    return result;
}


// test lookup and remove failures using strings
//
static char *test_str_bad(void *context)
{
    char *result = 0;
    qd_hash_handle_t *handles[key_count];
    qd_hash_t *hash = qd_hash(4, 10, false);
    if (!hash)
        return "hash table allocation failed";

    for (int i = 0; i < key_count; ++i) {
        const unsigned char *key = keys[i];
        qd_hash_handle_t **handle = &handles[i];

        if (qd_hash_insert_str(hash, key, (void *)key, handle) != QD_ERROR_NONE) {
            result = "hash table insert failed";
            goto done;
        }

        if (strcmp((const char *) qd_hash_key_by_handle(*handle), (const char *)key) != 0) {
            result = "hash handle key did not match";
            goto done;
        }
    }

    if (qd_hash_size(hash) != key_count) {
        result = "hash size is incorrect";
        goto done;
    }

    const unsigned char *key = (const unsigned char *) "I DO NOT EXIST";
    void *value = (void *)key;   // just a non-zero value

    //  key not found
    qd_hash_retrieve_str(hash, key, &value);   // does not return error code, but sets value to 0
    if (value != 0) {
        result = "expected hash retrieve to find nothing";
        goto done;
    }

    // remove should return error
    if (qd_hash_remove_str(hash, key) != QD_ERROR_NOT_FOUND) {
        result = "expected hash remove to fail (not found)";
        goto done;
    }

    // cleanup

    for (int i = 0; i < key_count; ++i) {
        key = keys[i];

        if (qd_hash_remove_str(hash, key) != QD_ERROR_NONE) {
            result = "str key remove failed";
            goto done;
        }
        qd_hash_handle_free(handles[i]);
    }

done:
    qd_hash_free(hash);
    return result;
}


int hash_tests(void)
{
    int result = 0;
    char *test_group = "hash_tests";

    TEST_CASE(test_iter_keys, 0);
    TEST_CASE(test_str_keys, 0);
    TEST_CASE(test_iter_bad, 0);
    TEST_CASE(test_str_bad, 0);

    return result;
}

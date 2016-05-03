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

#include "test_case.h"
#include <stdio.h>
#include <string.h>
#include <qpid/dispatch/iterator.h>
#include <qpid/dispatch/hash.h>
#include <qpid/dispatch/router.h>

#define FAIL_TEXT_SIZE 10000
static char fail_text[FAIL_TEXT_SIZE];

static void build_buffer_chain(qd_buffer_list_t *chain,
                               const char *text,
                               int segment_size)
{
    int len = strlen(text);
    while (len) {
        int count = (segment_size > len) ? len : segment_size;
        qd_buffer_t *buf = qd_buffer();
        count = (qd_buffer_capacity(buf) < count) ? qd_buffer_capacity(buf) : count;
        memcpy(qd_buffer_cursor(buf), text, count);
        qd_buffer_insert(buf, count);
        DEQ_INSERT_TAIL(*chain, buf);
        len -= count;
        text += count;
    }
}

static void release_buffer_chain(qd_buffer_list_t *chain)
{
    while (DEQ_SIZE(*chain)) {
        qd_buffer_t *buf = DEQ_HEAD(*chain);
        DEQ_REMOVE_HEAD(*chain);
        qd_buffer_free(buf);
    }
}

static char* test_view_global_dns(void *context)
{
    qd_field_iterator_t *iter = qd_address_iterator_string("amqp://host/global/sub", ITER_VIEW_ALL);
    if (!qd_field_iterator_equal(iter, (unsigned char*) "amqp://host/global/sub"))
        return "ITER_VIEW_ALL failed";

    qd_address_iterator_reset_view(iter, ITER_VIEW_NO_HOST);
    if (!qd_field_iterator_equal(iter, (unsigned char*) "global/sub"))
        return "ITER_VIEW_NO_HOST failed";

    qd_address_iterator_reset_view(iter, ITER_VIEW_NODE_ID);
    if (!qd_field_iterator_equal(iter, (unsigned char*) "global"))
        return "ITER_VIEW_NODE_ID failed";

    qd_address_iterator_reset_view(iter, ITER_VIEW_NODE_SPECIFIC);
    if (!qd_field_iterator_equal(iter, (unsigned char*) "sub"))
        return "ITER_VIEW_NODE_SPECIFIC failed";

    qd_address_iterator_reset_view(iter, ITER_VIEW_ADDRESS_HASH);
    if (!qd_field_iterator_equal(iter, (unsigned char*) "M0global/sub"))
        return "ITER_VIEW_ADDRESS_HASH failed";

    qd_field_iterator_free(iter);

    return 0;
}


static char* test_view_global_non_dns(void *context)
{
    qd_field_iterator_t *iter = qd_address_iterator_string("amqp:/global/sub", ITER_VIEW_ALL);
    if (!qd_field_iterator_equal(iter, (unsigned char*) "amqp:/global/sub"))
        return "ITER_VIEW_ALL failed";

    qd_address_iterator_reset_view(iter, ITER_VIEW_NO_HOST);
    if (!qd_field_iterator_equal(iter, (unsigned char*) "global/sub"))
        return "ITER_VIEW_NO_HOST failed";

    qd_address_iterator_reset_view(iter, ITER_VIEW_NODE_ID);
    if (!qd_field_iterator_equal(iter, (unsigned char*) "global"))
        return "ITER_VIEW_NODE_ID failed";

    qd_address_iterator_reset_view(iter, ITER_VIEW_NODE_SPECIFIC);
    if (!qd_field_iterator_equal(iter, (unsigned char*) "sub"))
        return "ITER_VIEW_NODE_SPECIFIC failed";

    qd_address_iterator_reset_view(iter, ITER_VIEW_ADDRESS_HASH);
    if (!qd_field_iterator_equal(iter, (unsigned char*) "M0global/sub"))
        return "ITER_VIEW_ADDRESS_HASH failed";

    qd_field_iterator_free(iter);

    return 0;
}


static char* test_view_global_no_host(void *context)
{
    qd_field_iterator_t *iter = qd_address_iterator_string("global/sub", ITER_VIEW_ALL);
    if (!qd_field_iterator_equal(iter, (unsigned char*) "global/sub"))
        return "ITER_VIEW_ALL failed";

    qd_address_iterator_reset_view(iter, ITER_VIEW_NO_HOST);
    if (!qd_field_iterator_equal(iter, (unsigned char*) "global/sub"))
        return "ITER_VIEW_NO_HOST failed";

    qd_address_iterator_reset_view(iter, ITER_VIEW_NODE_ID);
    if (!qd_field_iterator_equal(iter, (unsigned char*) "global"))
        return "ITER_VIEW_NODE_ID failed";

    qd_address_iterator_reset_view(iter, ITER_VIEW_NODE_SPECIFIC);
    if (!qd_field_iterator_equal(iter, (unsigned char*) "sub"))
        return "ITER_VIEW_NODE_SPECIFIC failed";

    qd_address_iterator_reset_view(iter, ITER_VIEW_ADDRESS_HASH);
    if (!qd_field_iterator_equal(iter, (unsigned char*) "M0global/sub"))
        return "ITER_VIEW_ADDRESS_HASH failed";

    qd_field_iterator_free(iter);

    return 0;
}


static char* test_view_global_no_host_slash(void *context)
{
    qd_field_iterator_t *iter = qd_address_iterator_string("/global/sub", ITER_VIEW_ALL);
    if (!qd_field_iterator_equal(iter, (unsigned char*) "/global/sub"))
        return "ITER_VIEW_ALL failed";

    qd_address_iterator_reset_view(iter, ITER_VIEW_NO_HOST);
    if (!qd_field_iterator_equal(iter, (unsigned char*) "global/sub"))
        return "ITER_VIEW_NO_HOST failed";

    qd_address_iterator_reset_view(iter, ITER_VIEW_NODE_ID);
    if (!qd_field_iterator_equal(iter, (unsigned char*) "global"))
        return "ITER_VIEW_NODE_ID failed";

    qd_address_iterator_reset_view(iter, ITER_VIEW_NODE_SPECIFIC);
    if (!qd_field_iterator_equal(iter, (unsigned char*) "sub"))
        return "ITER_VIEW_NODE_SPECIFIC failed";

    qd_address_iterator_reset_view(iter, ITER_VIEW_ADDRESS_HASH);
    if (!qd_field_iterator_equal(iter, (unsigned char*) "M0global/sub"))
        return "ITER_VIEW_ADDRESS_HASH failed";

    qd_field_iterator_free(iter);

    return 0;
}


static char* view_address_hash(void *context, qd_field_iterator_t *iter,
                               const char *addr, const char *view)
{
    qd_address_iterator_set_phase(iter, '1');
    if (!qd_field_iterator_equal(iter, (unsigned char*) view)) {
        char *got = (char*) qd_field_iterator_copy(iter);
        snprintf(fail_text, FAIL_TEXT_SIZE, "Addr '%s' failed.  Expected '%s', got '%s'",
                 addr, view, got);
        return fail_text;
    }
    return 0;
}

static char* test_view_address_hash(void *context)
{
    struct {const char *addr; const char *view;} cases[] = {
    {"amqp:/_local/my-addr/sub",                "Lmy-addr/sub"},
    {"amqp:/_local/my-addr",                    "Lmy-addr"},
    {"amqp:/_topo/area/router/local/sub",       "Aarea"},
    {"amqp:/_topo/my-area/router/local/sub",    "Rrouter"},
    {"amqp:/_topo/my-area/my-router/local/sub", "Llocal/sub"},
    {"amqp:/_topo/area/all/local/sub",          "Aarea"},
    {"amqp:/_topo/my-area/all/local/sub",       "Tlocal/sub"},
    {"amqp:/_topo/all/all/local/sub",           "Tlocal/sub"},
    {"amqp://host:port/_local/my-addr",         "Lmy-addr"},
    {"_topo/area/router/my-addr",               "Aarea"},
    {"_topo/my-area/router/my-addr",            "Rrouter"},
    {"_topo/my-area/my-router/my-addr",         "Lmy-addr"},
    {"_topo/my-area/router",                    "Rrouter"},
    {"amqp:/mobile",                            "M1mobile"},
    {"mobile",                                  "M1mobile"},
    {"/mobile",                                 "M1mobile"},

    // Re-run the above tests to make sure trailing dots are ignored.
    {"amqp:/_local/my-addr/sub.",                "Lmy-addr/sub"},
    {"amqp:/_local/my-addr.",                    "Lmy-addr"},
    {"amqp:/_topo/area/router/local/sub.",       "Aarea"},
    {"amqp:/_topo/my-area/router/local/sub.",    "Rrouter"},
    {"amqp:/_topo/my-area/my-router/local/sub.", "Llocal/sub"},
    {"amqp:/_topo/area/all/local/sub.",          "Aarea"},
    {"amqp:/_topo/my-area/all/local/sub.",       "Tlocal/sub"},
    {"amqp:/_topo/all/all/local/sub.",           "Tlocal/sub"},
    {"amqp://host:port/_local/my-addr.",         "Lmy-addr"},
    {"_topo/area/router/my-addr.",               "Aarea"},
    {"_topo/my-area/router/my-addr.",            "Rrouter"},
    {"_topo/my-area/my-router/my-addr.",         "Lmy-addr"},
    {"_topo/my-area/router.",                    "Rrouter"},
    {"_topo/my-area/router:",                    "Rrouter:"},

    {0, 0}
    };
    int idx;

    for (idx = 0; cases[idx].addr; idx++) {
        qd_field_iterator_t *iter = qd_address_iterator_string(cases[idx].addr, ITER_VIEW_ADDRESS_HASH);
        char *ret = view_address_hash(context, iter, cases[idx].addr, cases[idx].view);
        qd_field_iterator_free(iter);
        if (ret) return ret;
    }

    for (idx = 0; cases[idx].addr; idx++) {
        qd_buffer_list_t chain;
        DEQ_INIT(chain);
        build_buffer_chain(&chain, cases[idx].addr, 3);
        qd_field_iterator_t *iter = qd_address_iterator_buffer(DEQ_HEAD(chain), 0,
                                                               strlen(cases[idx].addr),
                                                               ITER_VIEW_ADDRESS_HASH);
        char *ret = view_address_hash(context, iter, cases[idx].addr, cases[idx].view);
        release_buffer_chain(&chain);
        if (ret) return ret;
    }

    return 0;
}


static char* test_view_address_hash_override(void *context)
{
    struct {const char *addr; const char *view;} cases[] = {
    {"amqp:/link-target",                    "Clink-target"},
    {"amqp:/domain/link-target",             "Cdomain/link-target"},
    {"domain/link-target",                   "Cdomain/link-target"},
    {"bbc79fb3-e1fd-4a08-92b2-9a2de232b558", "Cbbc79fb3-e1fd-4a08-92b2-9a2de232b558"},
    {0, 0}
    };
    int idx;

    for (idx = 0; cases[idx].addr; idx++) {
        qd_field_iterator_t *iter = qd_address_iterator_string(cases[idx].addr, ITER_VIEW_ADDRESS_HASH);
        qd_address_iterator_override_prefix(iter, 'C');
        if (!qd_field_iterator_equal(iter, (unsigned char*) cases[idx].view)) {
            char *got = (char*) qd_field_iterator_copy(iter);
            snprintf(fail_text, FAIL_TEXT_SIZE, "Addr '%s' failed.  Expected '%s', got '%s'",
                     cases[idx].addr, cases[idx].view, got);
            return fail_text;
        }
        qd_field_iterator_free(iter);
    }

    return 0;
}


static char* test_view_node_hash(void *context)
{
    struct {const char *addr; const char *view;} cases[] = {
    {"area/router",       "Aarea"},
    {"my-area/router",    "Rrouter"},
    {"my-area/my-router", "Rmy-router"},
    {0, 0}
    };
    int idx;

    for (idx = 0; cases[idx].addr; idx++) {
        qd_field_iterator_t *iter = qd_address_iterator_string(cases[idx].addr, ITER_VIEW_NODE_HASH);
        if (!qd_field_iterator_equal(iter, (unsigned char*) cases[idx].view)) {
            char *got = (char*) qd_field_iterator_copy(iter);
            snprintf(fail_text, FAIL_TEXT_SIZE, "Addr '%s' failed.  Expected '%s', got '%s'",
                     cases[idx].addr, cases[idx].view, got);
            return fail_text;
            qd_field_iterator_free(iter);
        }
        qd_field_iterator_free(iter);
    }

    return 0;
}

static char *field_advance_test(void *context,
                                qd_field_iterator_t *iter,
                                const unsigned char *template,
                                int increment)
{
    const unsigned char *original = template;
    while (*template) {
        // since qd_field_iterator_equal() resets the iterator to its original
        // view, we need to snapshot the iterator at the current point:
        qd_field_iterator_t *raw = qd_field_iterator_sub(iter,
                                                         qd_field_iterator_remaining(iter));
        if (!qd_field_iterator_equal(raw, (unsigned char*) template)) {

            snprintf(fail_text, FAIL_TEXT_SIZE,
                     "Field advance failed.  Expected '%s'",
                     (char *)template );
            return fail_text;
        }
        qd_field_iterator_advance(iter, increment);
        template += increment;
        qd_field_iterator_free(raw);
    }
    if (!qd_field_iterator_end(iter))
        return "Field advance to end failed";

    qd_field_iterator_reset(iter);
    if (!qd_field_iterator_equal(iter, (unsigned char*) original))
        return "Field advance reset failed";

    // try something stupid:
    qd_field_iterator_advance(iter, strlen((const char*)original) + 84);
    // expect no more data
    if (qd_field_iterator_octet(iter) || !qd_field_iterator_end(iter))
        return "Field over advance failed";

    qd_field_iterator_free(iter);
    return 0;

}


static char* test_field_advance_string(void *context)
{
    const char *template = "abcdefghijklmnopqrstuvwxyz";
    qd_field_iterator_t *iter = qd_field_iterator_string(template);
    return field_advance_test(context, iter,
                              (const unsigned char*)template, 2);
}


static char* test_field_advance_buffer(void *context)
{
    qd_buffer_list_t chain;
    DEQ_INIT(chain);
    const unsigned char *template = (unsigned char *)"AAABBB";
    build_buffer_chain(&chain, (const char *)template, 3);
    qd_field_iterator_t *iter = qd_field_iterator_buffer(DEQ_HEAD(chain), 0, 6);
    char *ret = field_advance_test(context, iter, template, 1);
    release_buffer_chain(&chain);
    return ret;
}

static char *test_qd_hash_retrieve_prefix_separator(void *context)
{
    qd_hash_t *hash = qd_hash(10, 32, 0);
    qd_field_iterator_t *iter = qd_address_iterator_string("policy.org.apache", ITER_VIEW_ADDRESS_HASH);
    qd_address_iterator_override_prefix(iter, 'C');

    // Insert that hash
    qd_error_t error = qd_hash_insert(hash, iter, "TEST", 0);

    // There should be no error on the insert hash
    if (error != QD_ERROR_NONE)
        return "qd_hash_insert failed";

    const char *taddr = "policy.org.apache.dev";

    qd_field_iterator_t *address_iter = qd_address_iterator_string(taddr, ITER_VIEW_ADDRESS_HASH);
    qd_address_iterator_override_prefix(address_iter, 'C');

    qd_address_t *addr;

    qd_hash_retrieve_prefix(hash, address_iter, (void*) &addr);

    qd_field_iterator_free(iter);
    qd_field_iterator_free(address_iter);

    if(addr)
        return 0;

    return "test_qd_hash_retrieve_prefix_separator() failed";
}


static char *test_qd_hash_retrieve_prefix(void *context)
{
    qd_hash_t *hash = qd_hash(10, 32, 0);

    qd_field_iterator_t *iter = qd_address_iterator_string("policy", ITER_VIEW_ADDRESS_HASH);
    qd_address_iterator_override_prefix(iter, 'C');

    // Insert that hash
    qd_error_t error = qd_hash_insert(hash, iter, "TEST", 0);

    // There should be no error on the insert hash
    if (error != QD_ERROR_NONE)
        return "qd_hash_insert failed";

    const char *taddr = "policy.org.apache.dev";

    qd_field_iterator_t *address_iter = qd_address_iterator_string(taddr, ITER_VIEW_ADDRESS_HASH);
    qd_address_iterator_override_prefix(address_iter, 'C');

    qd_address_t *addr;

    qd_hash_retrieve_prefix(hash, address_iter, (void*) &addr);

    qd_field_iterator_free(iter);
    qd_field_iterator_free(address_iter);

    if(addr)
        return 0;

    return "test_qd_hash_retrieve_prefix() failed";
}


static char *test_qd_hash_retrieve_prefix_no_match(void *context)
{
    qd_hash_t *hash = qd_hash(10, 32, 0);

    // No 'y' in policy. There should be no match.
    qd_field_iterator_t *iter = qd_address_iterator_string("polic", ITER_VIEW_ADDRESS_HASH);
    qd_address_iterator_override_prefix(iter, 'C');

    // Insert that hash
    qd_error_t error = qd_hash_insert(hash, iter, "TEST", 0);

    // There should be no error on the insert hash
    if (error != QD_ERROR_NONE)
        return "qd_hash_insert failed";

    const char *taddr = "policy.org.apache.dev";

    qd_field_iterator_t *address_iter = qd_address_iterator_string(taddr, ITER_VIEW_ADDRESS_HASH);
    qd_address_iterator_override_prefix(address_iter, 'C');

    qd_address_t *addr;

    qd_hash_retrieve_prefix(hash, address_iter, (void*) &addr);

    qd_field_iterator_free(iter);
    qd_field_iterator_free(address_iter);

    if(addr)
        return "test_qd_hash_retrieve_prefix_no_match() failed";

    return 0;
}


static char *test_qd_hash_retrieve_prefix_no_match_separator(void *context)
{
    qd_hash_t *hash = qd_hash(10, 32, 0);

    // No 'y' in policy. There should be no match.
    qd_field_iterator_t *iter = qd_address_iterator_string("policy.org.apach", ITER_VIEW_ADDRESS_HASH);
    qd_address_iterator_override_prefix(iter, 'C');

    // Insert that hash
    qd_error_t error = qd_hash_insert(hash, iter, "TEST", 0);

    // There should be no error on the insert hash
    if (error != QD_ERROR_NONE)
        return "qd_hash_insert failed";

    const char *taddr = "policy.org.apache.dev";

    qd_field_iterator_t *address_iter = qd_address_iterator_string(taddr, ITER_VIEW_ADDRESS_HASH);
    qd_address_iterator_override_prefix(address_iter, 'C');

    qd_address_t *addr;

    qd_hash_retrieve_prefix(hash, address_iter, (void*) &addr);

    qd_field_iterator_free(iter);
    qd_field_iterator_free(address_iter);

    if(addr)
        return "test_qd_hash_retrieve_prefix_no_match_separator() failed";

    return 0;
}

static char *test_qd_hash_retrieve_prefix_separator_exact_match(void *context)
{
    qd_hash_t *hash = qd_hash(10, 32, 0);
    qd_field_iterator_t *iter = qd_address_iterator_string("policy", ITER_VIEW_ADDRESS_HASH);
    qd_address_iterator_override_prefix(iter, 'C');

    // Insert that hash
    qd_error_t error = qd_hash_insert(hash, iter, "TEST", 0);

    // There should be no error on the insert hash
    if (error != QD_ERROR_NONE)
        return "qd_hash_insert failed";

    const char *taddr = "policy";

    qd_field_iterator_t *address_iter = qd_address_iterator_string(taddr, ITER_VIEW_ADDRESS_HASH);
    qd_address_iterator_override_prefix(address_iter, 'C');

    qd_address_t *addr;

    qd_hash_retrieve_prefix(hash, address_iter, (void*) &addr);

    qd_field_iterator_free(iter);
    qd_field_iterator_free(address_iter);

    if(addr)
        return 0;

    return "test_qd_hash_retrieve_prefix_separator_exact_match() failed";
}

static char *test_qd_hash_retrieve_prefix_separator_exact_match_1(void *context)
{
    qd_hash_t *hash = qd_hash(10, 32, 0);
    qd_field_iterator_t *iter = qd_address_iterator_string("policy.apache.org", ITER_VIEW_ADDRESS_HASH);
    qd_address_iterator_override_prefix(iter, 'C');

    // Insert that hash
    qd_error_t error = qd_hash_insert(hash, iter, "TEST", 0);

    // There should be no error on the insert hash
    if (error != QD_ERROR_NONE)
        return "qd_hash_insert failed";

    const char *taddr = "policy.apache.org";

    qd_field_iterator_t *address_iter = qd_address_iterator_string(taddr, ITER_VIEW_ADDRESS_HASH);
    qd_address_iterator_override_prefix(address_iter, 'C');

    qd_address_t *addr;

    qd_hash_retrieve_prefix(hash, address_iter, (void*) &addr);

    qd_field_iterator_free(iter);
    qd_field_iterator_free(address_iter);

    if(addr)
        return 0;

    return "test_qd_hash_retrieve_prefix_separator_exact_match_1() failed";
}


static char *test_qd_hash_retrieve_prefix_separator_exact_match_slashes(void *context)
{
    qd_hash_t *hash = qd_hash(10, 32, 0);

    // Use slashes. slashes are not treated as separators, they are just part of the literal string.
    qd_field_iterator_t *iter = qd_address_iterator_string("policy/apache/org", ITER_VIEW_ADDRESS_HASH);
    qd_address_iterator_override_prefix(iter, 'C');

    // Insert that hash
    qd_error_t error = qd_hash_insert(hash, iter, "TEST", 0);

    // There should be no error on the insert hash
    if (error != QD_ERROR_NONE)
        return "qd_hash_insert failed";

    const char *taddr = "policy/apache/org";

    qd_field_iterator_t *address_iter = qd_address_iterator_string(taddr, ITER_VIEW_ADDRESS_HASH);
    qd_address_iterator_override_prefix(address_iter, 'C');

    qd_address_t *addr;

    qd_hash_retrieve_prefix(hash, address_iter, (void*) &addr);

    qd_field_iterator_free(iter);
    qd_field_iterator_free(address_iter);

    if(addr)
        return 0;

    return "test_qd_hash_retrieve_prefix_separator_exact_match_slashes() failed";
}


static char *test_qd_hash_retrieve_prefix_separator_exact_match_dot_at_end(void *context)
{
    qd_hash_t *hash = qd_hash(10, 32, 0);
    qd_field_iterator_t *iter = qd_address_iterator_string("policy", ITER_VIEW_ADDRESS_HASH);
    qd_address_iterator_override_prefix(iter, 'C');

    // Insert that hash
    qd_error_t error = qd_hash_insert(hash, iter, "TEST", 0);

    // There should be no error on the insert hash
    if (error != QD_ERROR_NONE)
        return "qd_hash_insert failed";

    const char *taddr = "policy.";

    qd_field_iterator_t *address_iter = qd_address_iterator_string(taddr, ITER_VIEW_ADDRESS_HASH);
    qd_address_iterator_override_prefix(address_iter, 'C');

    qd_address_t *addr;

    qd_hash_retrieve_prefix(hash, address_iter, (void*) &addr);

    qd_field_iterator_free(iter);
    qd_field_iterator_free(address_iter);

    if(addr)
        return 0;

    return "test_qd_hash_retrieve_prefix_separator_exact_match_dot_at_end() failed";
}


static char *test_qd_hash_retrieve_prefix_separator_exact_match_dot_at_end_1(void *context)
{
    qd_hash_t *hash = qd_hash(10, 32, 0);
    qd_field_iterator_t *iter = qd_address_iterator_string("policy.apache", ITER_VIEW_ADDRESS_HASH);
    qd_address_iterator_override_prefix(iter, 'C');

    // Insert that hash
    qd_error_t error = qd_hash_insert(hash, iter, "TEST", 0);

    // There should be no error on the insert hash
    if (error != QD_ERROR_NONE)
        return "qd_hash_insert failed";

    const char *taddr = "policy.apache.";

    qd_field_iterator_t *address_iter = qd_address_iterator_string(taddr, ITER_VIEW_ADDRESS_HASH);
    qd_address_iterator_override_prefix(address_iter, 'C');

    qd_address_t *addr;

    qd_hash_retrieve_prefix(hash, address_iter, (void*) &addr);

    qd_field_iterator_free(iter);
    qd_field_iterator_free(address_iter);

    if(addr)
        return 0;

    return "test_qd_hash_retrieve_prefix_separator_exact_match_dot_at_end_1() failed";
}


static char *test_prefix_hash(void *context)
{
    static char error[200];
    char *entries[] = {"an_entry_with_no_separators",   //  0
                       "dot.separated.pattern.one",     //  1
                       "dot.separated.pattern.two",     //  2
                       "dot.separated.",                //  3
                       "dot",                           //  4
                       "slash",                         //  5
                       "slash/delimited",               //  6
                       "slash/delimited/first",         //  7
                       "slash/delimited/second",        //  8
                       "mixed.set/of/delimiters.one",   //  9
                       "mixed.set/of/delimiters.two",   // 10
                       "mixed.set/of/delimiters/three", // 11
                       "mixed.set",                     // 12
                       0};
    struct { char* pattern; int entry; } patterns[] = {{"an_entry_with_no_separators", 0},
                                                       {"dot.separated.pattern.one", 1},
                                                       {"dot.separated.pattern.two", 2},
                                                       {"dot.separated.pattern.three", 3},
                                                       {"dot.separated.other.pattern", 3},
                                                       {"dot.differentiated/other", 4},
                                                       {"slash/other", 5},
                                                       {"slash/delimited/first", 7},
                                                       {"slash/delimited/second", 8},
                                                       {"slash/delimited/third", 6},
                                                       {"mixed.other", -1},
                                                       {"mixed.set/other", 12},
                                                       {"mixed.set/of/delimiters.one/queue", 9},
                                                       {"mixed.set/of/delimiters.one.queue", 9},
                                                       {"mixed.set.of/delimiters.one.queue", 12},
                                                       {"other.thing.entirely", -1},
                                                       {0, 0}};

    qd_hash_t *hash = qd_hash(10, 32, 0);
    long idx = 0;

    //
    // Insert the entries into the hash table
    //
    while (entries[idx]) {
        qd_field_iterator_t *iter = qd_address_iterator_string(entries[idx], ITER_VIEW_ADDRESS_HASH);
        qd_hash_insert(hash, iter, (void*) (idx + 1), 0);
        qd_field_iterator_free(iter);
        idx++;
    }

    //
    // Test the patterns
    //
    idx = 0;
    while (patterns[idx].pattern) {
        qd_field_iterator_t *iter = qd_address_iterator_string(patterns[idx].pattern, ITER_VIEW_ADDRESS_HASH);
        void *ptr;
        qd_hash_retrieve_prefix(hash, iter, &ptr);
        int position = (int) ((long) ptr);
        position--;
        if (position != patterns[idx].entry) {
            snprintf(error, 200, "Pattern: '%s', expected %d, got %d",
                     patterns[idx].pattern, patterns[idx].entry, position);
            return error;
        }
        idx++;
    }

    return 0;
}


int field_tests(void)
{
    int result = 0;

    qd_field_iterator_set_address("my-area", "my-router");

    TEST_CASE(test_view_global_dns, 0);
    TEST_CASE(test_view_global_non_dns, 0);
    TEST_CASE(test_view_global_no_host, 0);
    TEST_CASE(test_view_global_no_host_slash, 0);
    TEST_CASE(test_view_address_hash, 0);
    TEST_CASE(test_view_address_hash_override, 0);
    TEST_CASE(test_view_node_hash, 0);
    TEST_CASE(test_field_advance_string, 0);
    TEST_CASE(test_field_advance_buffer, 0);
    TEST_CASE(test_qd_hash_retrieve_prefix_separator, 0);
    TEST_CASE(test_qd_hash_retrieve_prefix, 0);
    TEST_CASE(test_qd_hash_retrieve_prefix_no_match, 0);
    TEST_CASE(test_qd_hash_retrieve_prefix_no_match_separator, 0);
    TEST_CASE(test_qd_hash_retrieve_prefix_separator_exact_match, 0);
    TEST_CASE(test_qd_hash_retrieve_prefix_separator_exact_match_1, 0);
    TEST_CASE(test_qd_hash_retrieve_prefix_separator_exact_match_slashes, 0);
    TEST_CASE(test_qd_hash_retrieve_prefix_separator_exact_match_dot_at_end, 0);
    TEST_CASE(test_qd_hash_retrieve_prefix_separator_exact_match_dot_at_end_1, 0);
    TEST_CASE(test_prefix_hash, 0);

    return result;
}


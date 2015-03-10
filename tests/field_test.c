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
    {"amqp:/_topo/my-area/all/local/sub",       "Llocal/sub"},
    {"amqp:/_topo/all/all/local/sub",           "Llocal/sub"},
    {"amqp://host:port/_local/my-addr",         "Lmy-addr"},
    {"_topo/area/router/my-addr",               "Aarea"},
    {"_topo/my-area/router/my-addr",            "Rrouter"},
    {"_topo/my-area/my-router/my-addr",         "Lmy-addr"},
    {"_topo/my-area/router",                    "Rrouter"},
    {"amqp:/mobile",                            "M1mobile"},
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
    {"amqp:/link-target",        "Clink-target"},
    {"amqp:/domain/link-target", "Cdomain/link-target"},
    {"domain/link-target",       "Cdomain/link-target"},
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


int field_tests(void)
{
    int result = 0;

    qd_field_iterator_set_address("my-area", "my-router");

    TEST_CASE(test_view_global_dns, 0);
    TEST_CASE(test_view_global_non_dns, 0);
    TEST_CASE(test_view_global_no_host, 0);
    TEST_CASE(test_view_address_hash, 0);
    TEST_CASE(test_view_address_hash_override, 0);
    TEST_CASE(test_view_node_hash, 0);
    TEST_CASE(test_field_advance_string, 0);
    TEST_CASE(test_field_advance_buffer, 0);

    return result;
}


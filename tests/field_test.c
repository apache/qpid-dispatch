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

#include "qpid/dispatch/hash.h"
#include "qpid/dispatch/iterator.h"
#include "qpid/dispatch/router.h"

#include <stdio.h>
#include <string.h>

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
    qd_iterator_t *iter = qd_iterator_string("amqp://host/global/sub", ITER_VIEW_ALL);
    if (!qd_iterator_equal(iter, (unsigned char*) "amqp://host/global/sub")) {
        qd_iterator_free(iter);
        return "ITER_VIEW_ALL failed";
    }

    qd_iterator_reset_view(iter, ITER_VIEW_ADDRESS_NO_HOST);
    if (!qd_iterator_equal(iter, (unsigned char*) "global/sub")) {
        qd_iterator_free(iter);
        return "ITER_VIEW_ADDRESS_NO_HOST failed";
    }

    qd_iterator_reset_view(iter, ITER_VIEW_ADDRESS_HASH);
    if (!qd_iterator_equal(iter, (unsigned char*) "M0global/sub")) {
        qd_iterator_free(iter);
        return "ITER_VIEW_ADDRESS_HASH failed";
    }

    qd_iterator_free(iter);

    return 0;
}


static char* test_view_global_non_dns(void *context)
{
    qd_iterator_t *iter = qd_iterator_string("amqp:/global/sub", ITER_VIEW_ALL);
    if (!qd_iterator_equal(iter, (unsigned char*) "amqp:/global/sub")) {
        qd_iterator_free(iter);
        return "ITER_VIEW_ALL failed";
    }

    qd_iterator_reset_view(iter, ITER_VIEW_ADDRESS_NO_HOST);
    if (!qd_iterator_equal(iter, (unsigned char*) "global/sub")) {
        qd_iterator_free(iter);
        return "ITER_VIEW_ADDRESS_NO_HOST failed";
    }

    qd_iterator_reset_view(iter, ITER_VIEW_ADDRESS_HASH);
    if (!qd_iterator_equal(iter, (unsigned char*) "M0global/sub")) {
        qd_iterator_free(iter);
        return "ITER_VIEW_ADDRESS_HASH failed";
    }

    qd_iterator_free(iter);

    return 0;
}


static char* test_view_global_no_host(void *context)
{
    qd_iterator_t *iter = qd_iterator_string("global/sub", ITER_VIEW_ALL);
    if (!qd_iterator_equal(iter, (unsigned char*) "global/sub")) {
        qd_iterator_free(iter);
        return "ITER_VIEW_ALL failed";
    }

    qd_iterator_reset_view(iter, ITER_VIEW_ADDRESS_NO_HOST);
    if (!qd_iterator_equal(iter, (unsigned char*) "global/sub")) {
        qd_iterator_free(iter);
        return "ITER_VIEW_ADDRESS_NO_HOST failed";
    }

    qd_iterator_reset_view(iter, ITER_VIEW_ADDRESS_HASH);
    if (!qd_iterator_equal(iter, (unsigned char*) "M0global/sub")) {
        qd_iterator_free(iter);
        return "ITER_VIEW_ADDRESS_HASH failed";
    }

    qd_iterator_free(iter);

    return 0;
}


static char* test_view_global_no_host_slash(void *context)
{
    qd_iterator_t *iter = qd_iterator_string("/global/sub", ITER_VIEW_ALL);
    if (!qd_iterator_equal(iter, (unsigned char*) "/global/sub")) {
        qd_iterator_free(iter);
        return "ITER_VIEW_ALL failed";
    }

    qd_iterator_reset_view(iter, ITER_VIEW_ADDRESS_NO_HOST);
    if (!qd_iterator_equal(iter, (unsigned char*) "global/sub")) {
        qd_iterator_free(iter);
        return "ITER_VIEW_ADDRESS_NO_HOST failed";
    }

    qd_iterator_reset_view(iter, ITER_VIEW_ADDRESS_HASH);
    if (!qd_iterator_equal(iter, (unsigned char*) "M0global/sub")) {
        qd_iterator_free(iter);
        return "ITER_VIEW_ADDRESS_HASH failed";
    }

    qd_iterator_free(iter);

    return 0;
}


static char *test_trim(void *context)
{
    qd_iterator_t *iter = qd_iterator_string("testing.trim", ITER_VIEW_ALL);

    qd_iterator_trim_view(iter, 7);
    if (!qd_iterator_equal(iter, (unsigned char*) "testing")) {
        qd_iterator_free(iter);
        return "Trim on ITER_VIEW_ALL failed (1)";
    }

    qd_iterator_reset_view(iter, ITER_VIEW_ALL);
    if (!qd_iterator_equal(iter, (unsigned char*) "testing.trim")) {
        qd_iterator_free(iter);
        return "Trim on ITER_VIEW_ALL failed (2)";
    }

    qd_iterator_advance(iter, 4);
    qd_iterator_trim_view(iter, 5);

    if (!qd_iterator_equal(iter, (unsigned char*) "ing.t")) {
        qd_iterator_free(iter);
        return "Trim on ITER_VIEW_ALL failed (3)";
    }

    qd_iterator_reset_view(iter, ITER_VIEW_ADDRESS_HASH);
    qd_iterator_trim_view(iter, 9);
    if (!qd_iterator_equal(iter, (unsigned char*) "M0testing")) {
        qd_iterator_free(iter);
        return "Trim on ITER_VIEW_ADDRESS_HASH failed";
    }

    qd_iterator_reset(iter);
    qd_iterator_annotate_space(iter, "my_space.", 9);
    qd_iterator_trim_view(iter, 18);

    if (!qd_iterator_equal(iter, (unsigned char*) "M0my_space.testing")) {
        qd_iterator_free(iter);
        return "Trim on ITER_VIEW_ADDRESS_HASH (with space 1) failed";
    }

    qd_iterator_reset(iter);
    qd_iterator_trim_view(iter, 10);
    if (!qd_iterator_equal(iter, (unsigned char*) "M0my_space")) {
        qd_iterator_free(iter);
        return "Trim on ITER_VIEW_ADDRESS_HASH (in space 1) failed";
    }

    qd_iterator_reset(iter);
    qd_iterator_trim_view(iter, 2);
    if (!qd_iterator_equal(iter, (unsigned char*) "M0")) {
        qd_iterator_free(iter);
        return "Trim on ITER_VIEW_ADDRESS_HASH (in annotation 1) failed";
    }

    qd_iterator_reset(iter);
    qd_iterator_trim_view(iter, 1);
    if (!qd_iterator_equal(iter, (unsigned char*) "M")) {
        qd_iterator_free(iter);
        return "Trim on ITER_VIEW_ADDRESS_HASH (in annotation 2) failed";
    }

    qd_iterator_free(iter);
    return 0;
}


static char *test_sub_iterator(void *context)
{
    qd_iterator_t *iter = qd_iterator_string("test_sub_iterator", ITER_VIEW_ALL);
    qd_iterator_t *sub1 = qd_iterator_sub(iter, qd_iterator_remaining(iter));
    qd_iterator_advance(iter, 5);
    qd_iterator_t *sub2 = qd_iterator_sub(iter, qd_iterator_remaining(iter));
    qd_iterator_t *sub3 = qd_iterator_sub(iter, 3);

    if (!qd_iterator_equal(sub1, (unsigned char*) "test_sub_iterator")) {
        qd_iterator_free(iter);
        qd_iterator_free(sub1);
        qd_iterator_free(sub2);
        qd_iterator_free(sub3);
        return "Sub Iterator failed - 1";
    }

    if (!qd_iterator_equal(sub2, (unsigned char*) "sub_iterator")) {
        qd_iterator_free(iter);
        qd_iterator_free(sub1);
        qd_iterator_free(sub2);
        qd_iterator_free(sub3);
        return "Sub Iterator failed - 2";
    }

    if (!qd_iterator_equal(sub3, (unsigned char*) "sub")) {
        qd_iterator_free(iter);
        qd_iterator_free(sub1);
        qd_iterator_free(sub2);
        qd_iterator_free(sub3);
        return "Sub Iterator failed - 3";
    }

    qd_iterator_free(iter);
    qd_iterator_free(sub1);
    qd_iterator_free(sub2);
    qd_iterator_free(sub3);

    return 0;
}

// verify qd_iterator_copy works
static char *check_copy(void *context, qd_iterator_t *iter,
                        const char *addr, const char *view)
{
    char *got = (char *) qd_iterator_copy(iter);
    char *ret = 0;
    if (!got || strcmp(got, view) != 0) {
        snprintf(fail_text, FAIL_TEXT_SIZE, "Addr '%s' failed.  Expected '%s', got '%s'",
                 addr, view, got);
        ret = fail_text;
    }
    free(got);
    return ret;
}

static char* view_address_hash(void *context, qd_iterator_t *iter,
                               const char *addr, const char *view)
{
    if (!qd_iterator_equal(iter, (unsigned char*) view)) {
        char *got = (char*) qd_iterator_copy(iter);
        snprintf(fail_text, FAIL_TEXT_SIZE, "Addr '%s' failed.  Expected '%s', got '%s'",
                 addr, view, got);
        free(got);
        return fail_text;
    }
    return 0;
}

static char *check_dup(void *context, const qd_iterator_t *iter,
                         const char *addr, const char *view)
{
    qd_iterator_t *dup = qd_iterator_dup(iter);
    if (!dup)
        return "dup of iterator failed";
    char *ret = view_address_hash(context, dup, addr, view);
    qd_iterator_free(dup);
    return ret;
}

static char *verify_iterator(void *context, qd_iterator_t *iter,
                             const char *addr, const char *view)
{
    char *ret = view_address_hash(context, iter, addr, view);
    if (!ret)
        ret = check_dup(context, iter, addr, view);
    if (!ret)
        ret = check_copy(context, iter, addr, view);
    return ret;
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
    {"amqp:/_edge/router/sub",                  "Hrouter"},
    {"_edge/router/sub",                        "Hrouter"},

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
        qd_iterator_t *iter = qd_iterator_string(cases[idx].addr, ITER_VIEW_ADDRESS_HASH);
        qd_iterator_annotate_phase(iter, '1');
        char *ret = verify_iterator(context, iter, cases[idx].addr, cases[idx].view);
        qd_iterator_free(iter);
        if (ret) return ret;
    }

    for (idx = 0; cases[idx].addr; idx++) {
        qd_buffer_list_t chain;
        DEQ_INIT(chain);
        build_buffer_chain(&chain, cases[idx].addr, 3);
        qd_iterator_t *iter = qd_iterator_buffer(DEQ_HEAD(chain), 0,
                                                 strlen(cases[idx].addr),
                                                 ITER_VIEW_ADDRESS_HASH);
        qd_iterator_annotate_phase(iter, '1');
        char *ret = verify_iterator(context, iter, cases[idx].addr, cases[idx].view);
        release_buffer_chain(&chain);
        qd_iterator_free(iter);
        if (ret) return ret;
    }

    return 0;
}


static char* test_view_address_hash_edge(void *context)
{
    struct {const char *addr; const char *view;} cases[] = {
    {"amqp:/_local/my-addr/sub",                "Lmy-addr/sub"},
    {"amqp:/_local/my-addr",                    "Lmy-addr"},
    {"amqp:/_topo/area/router/local/sub",       "L_edge"},
    {"amqp:/_topo/my-area/router/local/sub",    "L_edge"},
    {"amqp:/_topo/my-area/my-router/local/sub", "Llocal/sub"},
    {"amqp:/_topo/area/all/local/sub",          "L_edge"},
    {"amqp:/_topo/my-area/all/local/sub",       "Tlocal/sub"},
    {"amqp:/_topo/all/all/local/sub",           "Tlocal/sub"},
    {"amqp://host:port/_local/my-addr",         "Lmy-addr"},
    {"_topo/area/router/my-addr",               "L_edge"},
    {"_topo/my-area/router/my-addr",            "L_edge"},
    {"_topo/my-area/my-router/my-addr",         "Lmy-addr"},
    {"_topo/my-area/router",                    "L_edge"},
    {"amqp:/mobile",                            "M1mobile"},
    {"mobile",                                  "M1mobile"},
    {"/mobile",                                 "M1mobile"},
    {"amqp:/_edge/router/sub",                  "L_edge"},
    {"_edge/router/sub",                        "L_edge"},
    {"amqp:/_edge/my-router/sub",               "Lsub"},
    {"_edge/my-router/sub",                     "Lsub"},

    {0, 0}
    };
    int idx;

    for (idx = 0; cases[idx].addr; idx++) {
        qd_iterator_t *iter = qd_iterator_string(cases[idx].addr, ITER_VIEW_ADDRESS_HASH);
        qd_iterator_annotate_phase(iter, '1');
        char *ret = verify_iterator(context, iter, cases[idx].addr, cases[idx].view);
        qd_iterator_free(iter);
        if (ret) return ret;
    }

    for (idx = 0; cases[idx].addr; idx++) {
        qd_buffer_list_t chain;
        DEQ_INIT(chain);
        build_buffer_chain(&chain, cases[idx].addr, 3);
        qd_iterator_t *iter = qd_iterator_buffer(DEQ_HEAD(chain), 0,
                                                 strlen(cases[idx].addr),
                                                 ITER_VIEW_ADDRESS_HASH);
        qd_iterator_annotate_phase(iter, '1');
        char *ret = verify_iterator(context, iter, cases[idx].addr, cases[idx].view);
        release_buffer_chain(&chain);
        qd_iterator_free(iter);
        if (ret) return ret;
    }

    return 0;
}


static char* test_view_address_with_space(void *context)
{
    struct {const char *addr; const char *view;} cases[] = {
    {"amqp:/_local/my-addr/sub",                "_local/my-addr/sub"},
    {"amqp:/_local/my-addr",                    "_local/my-addr"},
    {"amqp:/_topo/area/router/local/sub",       "_topo/area/router/local/sub"},
    {"amqp:/_topo/my-area/router/local/sub",    "_topo/my-area/router/local/sub"},
    {"amqp:/_topo/my-area/my-router/local/sub", "_topo/my-area/my-router/local/sub"},
    {"amqp:/_topo/area/all/local/sub",          "_topo/area/all/local/sub"},
    {"amqp:/_topo/my-area/all/local/sub",       "_topo/my-area/all/local/sub"},
    {"amqp:/_topo/all/all/local/sub",           "_topo/all/all/local/sub"},
    {"amqp://host:port/_local/my-addr",         "_local/my-addr"},
    {"_topo/area/router/my-addr",               "_topo/area/router/my-addr"},
    {"_topo/my-area/router/my-addr",            "_topo/my-area/router/my-addr"},
    {"_topo/my-area/my-router/my-addr",         "_topo/my-area/my-router/my-addr"},
    {"_topo/my-area/router",                    "_topo/my-area/router"},
    {"amqp:/mobile",                            "space/mobile"},
    {"mobile",                                  "space/mobile"},
    {"/mobile",                                 "space/mobile"},

    // Re-run the above tests to make sure trailing dots are ignored.
    {"amqp:/_local/my-addr/sub.",                "_local/my-addr/sub"},
    {"amqp:/_local/my-addr.",                    "_local/my-addr"},
    {"amqp:/_topo/area/router/local/sub.",       "_topo/area/router/local/sub"},
    {"amqp:/_topo/my-area/router/local/sub.",    "_topo/my-area/router/local/sub"},
    {"amqp:/_topo/my-area/my-router/local/sub.", "_topo/my-area/my-router/local/sub"},
    {"amqp:/_topo/area/all/local/sub.",          "_topo/area/all/local/sub"},
    {"amqp:/_topo/my-area/all/local/sub.",       "_topo/my-area/all/local/sub"},
    {"amqp:/_topo/all/all/local/sub.",           "_topo/all/all/local/sub"},
    {"amqp://host:port/_local/my-addr.",         "_local/my-addr"},
    {"_topo/area/router/my-addr.",               "_topo/area/router/my-addr"},
    {"_topo/my-area/router/my-addr.",            "_topo/my-area/router/my-addr"},
    {"_topo/my-area/my-router/my-addr.",         "_topo/my-area/my-router/my-addr"},
    {"_topo/my-area/router.",                    "_topo/my-area/router"},
    {"_topo/my-area/router:",                    "_topo/my-area/router:"},

    {0, 0}
    };
    int idx;

    for (idx = 0; cases[idx].addr; idx++) {
        qd_iterator_t *iter = qd_iterator_string(cases[idx].addr, ITER_VIEW_ADDRESS_WITH_SPACE);
        qd_iterator_annotate_space(iter, "space/", 6);
        qd_iterator_annotate_phase(iter, '1');
        char *ret = verify_iterator(context, iter, cases[idx].addr, cases[idx].view);
        qd_iterator_free(iter);
        if (ret) return ret;
    }

    for (idx = 0; cases[idx].addr; idx++) {
        qd_buffer_list_t chain;
        DEQ_INIT(chain);
        build_buffer_chain(&chain, cases[idx].addr, 3);
        qd_iterator_t *iter = qd_iterator_buffer(DEQ_HEAD(chain), 0,
                                                 strlen(cases[idx].addr),
                                                 ITER_VIEW_ADDRESS_WITH_SPACE);
        qd_iterator_annotate_space(iter, "space/", 6);
        qd_iterator_annotate_phase(iter, '1');
        char *ret = verify_iterator(context, iter, cases[idx].addr, cases[idx].view);
        release_buffer_chain(&chain);
        qd_iterator_free(iter);
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
        qd_iterator_t *iter = qd_iterator_string(cases[idx].addr, ITER_VIEW_ADDRESS_HASH);
        qd_iterator_annotate_prefix(iter, 'C');
        char *ret = verify_iterator(context, iter, cases[idx].addr, cases[idx].view);
        qd_iterator_free(iter);
        if (ret) return ret;
    }

    return 0;
}


static char* test_view_address_hash_with_space(void *context)
{
    struct {const char *addr; const char *view;} cases[] = {
    {"amqp:/link-target",                    "M0test.vhost.link-target"},
    {"amqp:/domain/link-target",             "M0test.vhost.domain/link-target"},
    {"domain/link-target",                   "M0test.vhost.domain/link-target"},
    {"bbc79fb3-e1fd-4a08-92b2-9a2de232b558", "M0test.vhost.bbc79fb3-e1fd-4a08-92b2-9a2de232b558"},
    {"_topo/my-area/router/address",         "Rrouter"},
    {"_topo/my-area/my-router/address",      "Laddress"},
    {0, 0}
    };
    int idx;

    for (idx = 0; cases[idx].addr; idx++) {
        qd_iterator_t *iter = qd_iterator_string(cases[idx].addr, ITER_VIEW_ADDRESS_HASH);
        qd_iterator_annotate_space(iter, "test.vhost.", 11);
        char *ret = verify_iterator(context, iter, cases[idx].addr, cases[idx].view);
        qd_iterator_free(iter);
        if (ret) return ret;
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
        qd_iterator_t *iter = qd_iterator_string(cases[idx].addr, ITER_VIEW_NODE_HASH);
        char *ret = verify_iterator(context, iter, cases[idx].addr, cases[idx].view);
        qd_iterator_free(iter);
        if (ret) return ret;
    }

    return 0;
}

static char *field_advance_test(void *context,
                                qd_iterator_t *iter,
                                const unsigned char *template,
                                int increment)
{
    const unsigned char *original = template;
    while (*template) {
        // since qd_iterator_equal() resets the iterator to its original
        // view, we need to snapshot the iterator at the current point:
        qd_iterator_t *raw = qd_iterator_sub(iter, qd_iterator_remaining(iter));
        if (!qd_iterator_equal(raw, (unsigned char*) template)) {

            snprintf(fail_text, FAIL_TEXT_SIZE,
                     "Field advance failed.  Expected '%s'",
                     (char *)template );
            qd_iterator_free(raw);
            return fail_text;
        }
        qd_iterator_advance(iter, increment);
        template += increment;
        qd_iterator_free(raw);
    }
    if (!qd_iterator_end(iter))
        return "Field advance to end failed";

    qd_iterator_reset(iter);
    if (!qd_iterator_equal(iter, (unsigned char*) original))
        return "Field advance reset failed";

    // try something stupid:
    qd_iterator_advance(iter, strlen((const char*)original) + 84);
    // expect no more data
    if (qd_iterator_octet(iter) || !qd_iterator_end(iter))
        return "Field over advance failed";

    qd_iterator_free(iter);
    return 0;

}


static char* test_field_advance_string(void *context)
{
    const char *template = "abcdefghijklmnopqrstuvwxyz";
    qd_iterator_t *iter = qd_iterator_string(template, ITER_VIEW_ALL);
    return field_advance_test(context, iter,
                              (const unsigned char*)template, 2);
}


static char* test_field_advance_buffer(void *context)
{
    qd_buffer_list_t chain;
    DEQ_INIT(chain);
    const unsigned char *template = (unsigned char *)"AAABBB";
    build_buffer_chain(&chain, (const char *)template, 3);
    qd_iterator_t *iter = qd_iterator_buffer(DEQ_HEAD(chain), 0, 6, ITER_VIEW_ALL);
    char *ret = field_advance_test(context, iter, template, 1);
    release_buffer_chain(&chain);
    return ret;
}

static char *test_qd_hash_retrieve_prefix_separator(void *context)
{
    qd_hash_t *hash = qd_hash(10, 32, 0);
    qd_iterator_t *iter = qd_iterator_string("policy.org.apache", ITER_VIEW_ADDRESS_HASH);
    qd_iterator_annotate_prefix(iter, 'C');

    // Insert that hash
    qd_error_t error = qd_hash_insert(hash, iter, "TEST", 0);

    // There should be no error on the insert hash
    if (error != QD_ERROR_NONE) {
        qd_iterator_free(iter);
        qd_hash_free(hash);
        return "qd_hash_insert failed";
    }

    const char *taddr = "policy.org.apache.dev";

    qd_iterator_t *address_iter = qd_iterator_string(taddr, ITER_VIEW_ADDRESS_HASH);
    qd_iterator_annotate_prefix(address_iter, 'C');

    qd_address_t *addr;

    qd_hash_retrieve_prefix(hash, address_iter, (void*) &addr);

    qd_iterator_free(iter);
    qd_iterator_free(address_iter);
    qd_hash_free(hash);

    if(addr)
        return 0;

    return "test_qd_hash_retrieve_prefix_separator() failed";
}


static char *test_qd_hash_retrieve_prefix(void *context)
{
    qd_hash_t *hash = qd_hash(10, 32, 0);

    qd_iterator_t *iter = qd_iterator_string("policy", ITER_VIEW_ADDRESS_HASH);
    qd_iterator_annotate_prefix(iter, 'C');

    // Insert that hash
    qd_error_t error = qd_hash_insert(hash, iter, "TEST", 0);

    // There should be no error on the insert hash
    if (error != QD_ERROR_NONE) {
        qd_iterator_free(iter);
        qd_hash_free(hash);
        return "qd_hash_insert failed";
    }

    const char *taddr = "policy.org.apache.dev";

    qd_iterator_t *address_iter = qd_iterator_string(taddr, ITER_VIEW_ADDRESS_HASH);
    qd_iterator_annotate_prefix(address_iter, 'C');

    qd_address_t *addr;

    qd_hash_retrieve_prefix(hash, address_iter, (void*) &addr);

    qd_iterator_free(iter);
    qd_iterator_free(address_iter);
    qd_hash_free(hash);

    if(addr)
        return 0;

    return "test_qd_hash_retrieve_prefix() failed";
}


static char *test_qd_hash_retrieve_prefix_no_match(void *context)
{
    qd_hash_t *hash = qd_hash(10, 32, 0);

    // No 'y' in policy. There should be no match.
    qd_iterator_t *iter = qd_iterator_string("polic", ITER_VIEW_ADDRESS_HASH);
    qd_iterator_annotate_prefix(iter, 'C');

    // Insert that hash
    qd_error_t error = qd_hash_insert(hash, iter, "TEST", 0);

    // There should be no error on the insert hash
    if (error != QD_ERROR_NONE) {
        qd_iterator_free(iter);
        qd_hash_free(hash);
        return "qd_hash_insert failed";
    }

    const char *taddr = "policy.org.apache.dev";

    qd_iterator_t *address_iter = qd_iterator_string(taddr, ITER_VIEW_ADDRESS_HASH);
    qd_iterator_annotate_prefix(address_iter, 'C');

    qd_address_t *addr;

    qd_hash_retrieve_prefix(hash, address_iter, (void*) &addr);

    qd_iterator_free(iter);
    qd_iterator_free(address_iter);
    qd_hash_free(hash);

    if(addr)
        return "test_qd_hash_retrieve_prefix_no_match() failed";

    return 0;
}


static char *test_qd_hash_retrieve_prefix_no_match_separator(void *context)
{
    qd_hash_t *hash = qd_hash(10, 32, 0);

    // No 'y' in policy. There should be no match.
    qd_iterator_t *iter = qd_iterator_string("policy.org.apach", ITER_VIEW_ADDRESS_HASH);
    qd_iterator_annotate_prefix(iter, 'C');

    // Insert that hash
    qd_error_t error = qd_hash_insert(hash, iter, "TEST", 0);

    // There should be no error on the insert hash
    if (error != QD_ERROR_NONE) {
        qd_iterator_free(iter);
        qd_hash_free(hash);
        return "qd_hash_insert failed";
    }

    const char *taddr = "policy.org.apache.dev";

    qd_iterator_t *address_iter = qd_iterator_string(taddr, ITER_VIEW_ADDRESS_HASH);
    qd_iterator_annotate_prefix(address_iter, 'C');

    qd_address_t *addr;

    qd_hash_retrieve_prefix(hash, address_iter, (void*) &addr);

    qd_iterator_free(iter);
    qd_iterator_free(address_iter);
    qd_hash_free(hash);

    if(addr)
        return "test_qd_hash_retrieve_prefix_no_match_separator() failed";

    return 0;
}

static char *test_qd_hash_retrieve_prefix_separator_exact_match(void *context)
{
    qd_hash_t *hash = qd_hash(10, 32, 0);
    qd_iterator_t *iter = qd_iterator_string("policy", ITER_VIEW_ADDRESS_HASH);
    qd_iterator_annotate_prefix(iter, 'C');

    // Insert that hash
    qd_error_t error = qd_hash_insert(hash, iter, "TEST", 0);

    // There should be no error on the insert hash
    if (error != QD_ERROR_NONE) {
        qd_iterator_free(iter);
        qd_hash_free(hash);
        return "qd_hash_insert failed";
    }

    const char *taddr = "policy";

    qd_iterator_t *address_iter = qd_iterator_string(taddr, ITER_VIEW_ADDRESS_HASH);
    qd_iterator_annotate_prefix(address_iter, 'C');

    qd_address_t *addr;

    qd_hash_retrieve_prefix(hash, address_iter, (void*) &addr);

    qd_iterator_free(iter);
    qd_iterator_free(address_iter);
    qd_hash_free(hash);

    if(addr)
        return 0;

    return "test_qd_hash_retrieve_prefix_separator_exact_match() failed";
}

static char *test_qd_hash_retrieve_prefix_separator_exact_match_1(void *context)
{
    qd_hash_t *hash = qd_hash(10, 32, 0);
    qd_iterator_t *iter = qd_iterator_string("policy.apache.org", ITER_VIEW_ADDRESS_HASH);
    qd_iterator_annotate_prefix(iter, 'C');

    // Insert that hash
    qd_error_t error = qd_hash_insert(hash, iter, "TEST", 0);

    // There should be no error on the insert hash
    if (error != QD_ERROR_NONE) {
        qd_iterator_free(iter);
        qd_hash_free(hash);
        return "qd_hash_insert failed";
    }

    const char *taddr = "policy.apache.org";

    qd_iterator_t *address_iter = qd_iterator_string(taddr, ITER_VIEW_ADDRESS_HASH);
    qd_iterator_annotate_prefix(address_iter, 'C');

    qd_address_t *addr;

    qd_hash_retrieve_prefix(hash, address_iter, (void*) &addr);

    qd_iterator_free(iter);
    qd_iterator_free(address_iter);
    qd_hash_free(hash);

    if(addr)
        return 0;

    return "test_qd_hash_retrieve_prefix_separator_exact_match_1() failed";
}


static char *test_qd_hash_retrieve_prefix_separator_exact_match_slashes(void *context)
{
    qd_hash_t *hash = qd_hash(10, 32, 0);

    // Use slashes. slashes are not treated as separators, they are just part of the literal string.
    qd_iterator_t *iter = qd_iterator_string("policy/apache/org", ITER_VIEW_ADDRESS_HASH);
    qd_iterator_annotate_prefix(iter, 'C');

    // Insert that hash
    qd_error_t error = qd_hash_insert(hash, iter, "TEST", 0);

    // There should be no error on the insert hash
    if (error != QD_ERROR_NONE) {
        qd_iterator_free(iter);
        qd_hash_free(hash);
        return "qd_hash_insert failed";
    }

    const char *taddr = "policy/apache/org";

    qd_iterator_t *address_iter = qd_iterator_string(taddr, ITER_VIEW_ADDRESS_HASH);
    qd_iterator_annotate_prefix(address_iter, 'C');

    qd_address_t *addr;

    qd_hash_retrieve_prefix(hash, address_iter, (void*) &addr);

    qd_iterator_free(iter);
    qd_iterator_free(address_iter);
    qd_hash_free(hash);

    if(addr)
        return 0;

    return "test_qd_hash_retrieve_prefix_separator_exact_match_slashes() failed";
}


static char *test_qd_hash_retrieve_prefix_separator_exact_match_dot_at_end(void *context)
{
    qd_hash_t *hash = qd_hash(10, 32, 0);
    qd_iterator_t *iter = qd_iterator_string("policy", ITER_VIEW_ADDRESS_HASH);
    qd_iterator_annotate_prefix(iter, 'C');

    // Insert that hash
    qd_error_t error = qd_hash_insert(hash, iter, "TEST", 0);

    // There should be no error on the insert hash
    if (error != QD_ERROR_NONE) {
        qd_iterator_free(iter);
        qd_hash_free(hash);
        return "qd_hash_insert failed";
    }

    const char *taddr = "policy.";

    qd_iterator_t *address_iter = qd_iterator_string(taddr, ITER_VIEW_ADDRESS_HASH);
    qd_iterator_annotate_prefix(address_iter, 'C');

    qd_address_t *addr;

    qd_hash_retrieve_prefix(hash, address_iter, (void*) &addr);

    qd_iterator_free(iter);
    qd_iterator_free(address_iter);
    qd_hash_free(hash);

    if(addr)
        return 0;

    return "test_qd_hash_retrieve_prefix_separator_exact_match_dot_at_end() failed";
}


static char *test_qd_hash_retrieve_prefix_separator_exact_match_dot_at_end_1(void *context)
{
    qd_hash_t *hash = qd_hash(10, 32, 0);
    qd_iterator_t *iter = qd_iterator_string("policy.apache", ITER_VIEW_ADDRESS_HASH);
    qd_iterator_annotate_prefix(iter, 'C');

    // Insert that hash
    qd_error_t error = qd_hash_insert(hash, iter, "TEST", 0);

    // There should be no error on the insert hash
    if (error != QD_ERROR_NONE) {
        qd_iterator_free(iter);
        qd_hash_free(hash);
        return "qd_hash_insert failed";
    }

    const char *taddr = "policy.apache.";

    qd_iterator_t *address_iter = qd_iterator_string(taddr, ITER_VIEW_ADDRESS_HASH);
    qd_iterator_annotate_prefix(address_iter, 'C');

    qd_address_t *addr;

    qd_hash_retrieve_prefix(hash, address_iter, (void*) &addr);

    qd_iterator_free(iter);
    qd_iterator_free(address_iter);
    qd_hash_free(hash);

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
        qd_iterator_t *iter = qd_iterator_string(entries[idx], ITER_VIEW_ADDRESS_HASH);
        qd_hash_insert(hash, iter, (void*) (idx + 1), 0);
        qd_iterator_free(iter);
        idx++;
    }

    //
    // Test the patterns
    //
    idx = 0;
    while (patterns[idx].pattern) {
        qd_iterator_t *iter = qd_iterator_string(patterns[idx].pattern, ITER_VIEW_ADDRESS_HASH);
        void *ptr;
        qd_hash_retrieve_prefix(hash, iter, &ptr);
        int position = (int) ((long) ptr);
        position--;
        if (position != patterns[idx].entry) {
            snprintf(error, 200, "Pattern: '%s', expected %d, got %d",
                     patterns[idx].pattern, patterns[idx].entry, position);
            qd_iterator_free(iter);
            qd_hash_free(hash);
            return error;
        }
        qd_iterator_free(iter);
        idx++;
    }

    qd_hash_free(hash);
    return 0;
}


static char *test_prefix_hash_with_space(void *context)
{
    static char error[200];
    char *entries[] = {"space.an_entry_with_no_separators",   //  0
                       "space.dot.separated.pattern.one",     //  1
                       "space.dot.separated.pattern.two",     //  2
                       "space.dot.separated.",                //  3
                       "space.dot",                           //  4
                       "space.slash",                         //  5
                       "space.slash/delimited",               //  6
                       "space.slash/delimited/first",         //  7
                       "space.slash/delimited/second",        //  8
                       "space.mixed.set/of/delimiters.one",   //  9
                       "space.mixed.set/of/delimiters.two",   // 10
                       "space.mixed.set/of/delimiters/three", // 11
                       "space.mixed.set",                     // 12
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
        qd_iterator_t *iter = qd_iterator_string(entries[idx], ITER_VIEW_ADDRESS_HASH);
        qd_hash_insert(hash, iter, (void*) (idx + 1), 0);
        qd_iterator_free(iter);
        idx++;
    }

    //
    // Test the patterns
    //
    idx = 0;
    while (patterns[idx].pattern) {
        qd_iterator_t *iter = qd_iterator_string(patterns[idx].pattern, ITER_VIEW_ADDRESS_HASH);
        qd_iterator_annotate_space(iter, "space.", 6);
        void *ptr;
        qd_hash_retrieve_prefix(hash, iter, &ptr);
        int position = (int) ((long) ptr);
        position--;
        if (position != patterns[idx].entry) {
            snprintf(error, 200, "Pattern: '%s', expected %d, got %d",
                     patterns[idx].pattern, patterns[idx].entry, position);
            qd_iterator_free(iter);
            qd_hash_free(hash);
            return error;
        }
        qd_iterator_free(iter);
        idx++;
    }

    qd_hash_free(hash);
    return 0;
}


static char *test_iterator_copy_octet(void *context)
{
    // verify qd_iterator_ncopy_octets()

    char *result = 0;
    uint8_t buffer[4];
    const char *expected[] = {"onl", "y/s", "ee/", "thi", "s"};

    qd_iterator_t *iter = qd_iterator_string("amqp://my.host.com:666/only/see/this",
                                             ITER_VIEW_ADDRESS_NO_HOST);
    int i = 0;
    while (!qd_iterator_end(iter)) {
        memset(buffer, 0, sizeof(buffer));
        size_t count = qd_iterator_ncopy_octets(iter, buffer, 3);
        if (count != strlen(expected[i]) || memcmp(buffer, expected[i], count) != 0) {
            fprintf(stderr, "qd_iterator_ncopy_octets failed,\n"
                    "Expected %zu octets set to '%s' got %zu octets set to '%.3s'\n",
                    strlen(expected[i]), expected[i],
                    count, (char *)buffer);
            result = "qd_iterator_ncopy_octets failed";
            break;
        }
        ++i;
    }
    qd_iterator_free(iter);
    return result;
}


int field_tests(void)
{
    int result = 0;
    char *test_group = "field_tests";

    qd_iterator_set_address(false, "my-area", "my-router");

    TEST_CASE(test_view_global_dns, 0);
    TEST_CASE(test_view_global_non_dns, 0);
    TEST_CASE(test_view_global_no_host, 0);
    TEST_CASE(test_view_global_no_host_slash, 0);
    TEST_CASE(test_trim, 0);
    TEST_CASE(test_sub_iterator, 0);
    TEST_CASE(test_view_address_hash, 0);
    TEST_CASE(test_view_address_with_space, 0);
    TEST_CASE(test_view_address_hash_override, 0);
    TEST_CASE(test_view_address_hash_with_space, 0);
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
    TEST_CASE(test_prefix_hash_with_space, 0);
    TEST_CASE(test_iterator_copy_octet, 0);

    qd_iterator_set_address(true, "my-area", "my-router");
    TEST_CASE(test_view_address_hash_edge, 0);

    return result;
}

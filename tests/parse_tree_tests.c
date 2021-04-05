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

#include "parse_tree.h"
#include "test_case.h"

#include <stdio.h>
#include <string.h>

static char *test_add_remove(void *context)
{
    qd_iterator_t *piter = qd_iterator_string("I.am.Sam", ITER_VIEW_ALL);
    qd_iterator_t *piter2 = qd_iterator_string("Sam.I.Am", ITER_VIEW_ALL);
    qd_parse_tree_t *node = qd_parse_tree_new(QD_PARSE_TREE_ADDRESS);
    void *payload;

    if (qd_parse_tree_remove_pattern(node, piter)) {
        qd_parse_tree_free(node);
        qd_iterator_free(piter);
        qd_iterator_free(piter2);
        return "Failed to remove a non-existing pattern";
    }

    if (qd_parse_tree_get_pattern(node, piter, &payload)) {
        qd_parse_tree_free(node);
        qd_iterator_free(piter);
        qd_iterator_free(piter2);
        return "Got a non-existing pattern";
    }

    if (qd_parse_tree_add_pattern(node, piter, "Hi Sam")) {
        qd_parse_tree_free(node);
        qd_iterator_free(piter);
        qd_iterator_free(piter2);
        return "Add returned existing value";
    }

    if (qd_parse_tree_add_pattern(node, piter2, "Bye Sam")) {
        qd_parse_tree_free(node);
        qd_iterator_free(piter);
        qd_iterator_free(piter2);
        return "Add returned existing value";
    }

    if (!qd_parse_tree_get_pattern(node, piter, &payload)) {
        qd_parse_tree_free(node);
        qd_iterator_free(piter);
        qd_iterator_free(piter2);
        return "Could not get pattern";
    }

    if (!payload || strcmp("Hi Sam", (char *)payload)) {
        qd_parse_tree_free(node);
        qd_iterator_free(piter);
        qd_iterator_free(piter2);
        return "Got bad pattern";
    }

    if (!qd_parse_tree_get_pattern(node, piter2, &payload)) {
        qd_parse_tree_free(node);
        qd_iterator_free(piter);
        qd_iterator_free(piter2);
        return "Could not get pattern";
    }

    if (!payload || strcmp("Bye Sam", (char *)payload)) {
        qd_parse_tree_free(node);
        qd_iterator_free(piter);
        qd_iterator_free(piter2);
        return "Got bad pattern";
    }

    if (!qd_parse_tree_remove_pattern(node, piter)) {
        qd_parse_tree_free(node);
        qd_iterator_free(piter);
        qd_iterator_free(piter2);
        return "Failed to remove an existing pattern";
    }

    if (!qd_parse_tree_remove_pattern(node, piter2)) {
        qd_parse_tree_free(node);
        qd_iterator_free(piter);
        qd_iterator_free(piter2);
        return "Failed to remove an existing pattern";
    }

    qd_parse_tree_free(node);
    qd_iterator_free(piter);
    qd_iterator_free(piter2);
    return NULL;
}

static char *test_add_and_match_str(void *context)
{
    const char *str1 = "I.am.Sam";
    const char *str2 = "Sam.I.Am";
    qd_parse_tree_t *node = qd_parse_tree_new(QD_PARSE_TREE_ADDRESS);
    void *payload;

    if (qd_parse_tree_add_pattern_str(node, str1, "Hi Sam")) {
        qd_parse_tree_free(node);
        return "Add returned existing value (1)";
    }

    if (qd_parse_tree_add_pattern_str(node, str2, "Bye Sam")) {
        qd_parse_tree_free(node);
        return "Add returned existing value (2)";
    }

    if (!qd_parse_tree_retrieve_match_str(node, str1, &payload)) {
        qd_parse_tree_free(node);
        return "Failed to get expected match (1)";
    }

    if (!qd_parse_tree_retrieve_match_str(node, str2, &payload)) {
        qd_parse_tree_free(node);
        return "Failed to get expected match (2)";
    }

    if (qd_parse_tree_retrieve_match_str(node, "I", &payload) ||
        qd_parse_tree_retrieve_match_str(node, "I.am", &payload)) {
        qd_parse_tree_free(node);
        return "Should not match part of a pattern";
    }

    if (qd_parse_tree_retrieve_match_str(node, "notSoFast", &payload)) {
        qd_parse_tree_free(node);
        return "Match pattern should not match but did match";
    }

    if (!qd_parse_tree_remove_pattern_str(node, str1)) {
        qd_parse_tree_free(node);
        return "Failed to remove an existing pattern str";
    }

    if (qd_parse_tree_retrieve_match_str(node, str1, &payload)) {
        qd_parse_tree_free(node);
        return "Removed pattern should not match but did match";
    }

    qd_parse_tree_free(node);
    return NULL;
}

static char *test_duplicate_error_str(void *context)
{
    // While A and B are different strings they are semantically the same
    // pattern and should trigger duplicate detection in the parse tree
    const char *A = "#";
    const char *B = "#.#.#.#";
    qd_parse_tree_t *node = qd_parse_tree_new(QD_PARSE_TREE_ADDRESS);
    void *payload;
    qd_error_t rc;

    rc = qd_parse_tree_add_pattern_str(node, A, (void *)A);
    if (rc) {
        qd_parse_tree_free(node);
        return (char *)qd_error_name(rc);
    }

    // matches on A or B both return A
    if (!qd_parse_tree_retrieve_match_str(node, A, &payload)) {
        qd_parse_tree_free(node);
        return "Could not get pattern";
    }

    if (!payload || strcmp(A, (char *)payload)) {
        qd_parse_tree_free(node);
        return "Got bad pattern";
    }

    if (!qd_parse_tree_retrieve_match_str(node, B, &payload)) {
        qd_parse_tree_free(node);
        return "Could not get pattern";
    }

    if (!payload || strcmp(A, (char *)payload)) {
        qd_parse_tree_free(node);
        return "Got bad pattern";
    }

    // attempt to add B pattern, expect duplication error

    rc = qd_parse_tree_add_pattern_str(node, B, (void *)B);
    if (!rc) {
        qd_parse_tree_free(node);
        return "Duplicate pattern NOT detected";
    }

    // matches on A or B both return B
    if (!qd_parse_tree_retrieve_match_str(node, A, &payload)) {
        qd_parse_tree_free(node);
        return "Could not get pattern";
    }

    if (!payload || strcmp(A, (char *)payload)) {
        qd_parse_tree_free(node);
        return "Got bad pattern";
    }

    if (!qd_parse_tree_retrieve_match_str(node, B, &payload)) {
        qd_parse_tree_free(node);
        return "Could not get pattern";
    }

    if (!payload || strcmp(A, (char *)payload)) {
        qd_parse_tree_free(node);
        return "Got bad pattern";
    }

    // now replace A with B correctly

    payload = qd_parse_tree_remove_pattern_str(node, A);
    if (!payload || strcmp(A, (char *)payload)) {
        qd_parse_tree_free(node);
        return "remove pattern failed";
    }

    rc = qd_parse_tree_add_pattern_str(node, B, (void *)B);
    if (rc) {
        qd_parse_tree_free(node);
        return "Replace add failed";
    }

    // matches on A or B both return B
    if (!qd_parse_tree_retrieve_match_str(node, A, &payload)) {
        qd_parse_tree_free(node);
        return "Could not get pattern";
    }

    if (!payload || strcmp(B, (char *)payload)) {
        qd_parse_tree_free(node);
        return "Got bad pattern";
    }

    if (!qd_parse_tree_retrieve_match_str(node, B, &payload)) {
        qd_parse_tree_free(node);
        return "Could not get pattern";
    }

    if (!payload || strcmp(B, (char *)payload)) {
        qd_parse_tree_free(node);
        return "Got bad pattern";
    }

    qd_parse_tree_free(node);
    return NULL;
}

// for pattern match callback
typedef struct {
    int count;
    const char **patterns;
    void **payloads;
} visit_handle_t;


// callback to visit all matching patterns in tree
static bool visit_all(void *handle,
                      const char *pattern,
                      void *payload)
{
    visit_handle_t *h = (visit_handle_t *)handle;
    h->patterns[h->count] = pattern;
    h->payloads[h->count] = payload;
    h->count++;
    return true;
}


// callback to return first (best) match
static bool find_best(void *handle,
                      const char *pattern,
                      void *payload)
{
    visit_handle_t *h = (visit_handle_t *)handle;
    h->patterns[0] = pattern;
    h->payloads[0] = payload;
    h->count = 1;
    return false;
}


// check if input patterns are correctly "normalized" (see parse_tree.c)
static char *check_normalize(const char *input,
                             const char *expected)
{
    const char *patterns[1];
    void *payloads[1];
    visit_handle_t vh = {0, patterns, payloads};
    qd_parse_tree_t *node = qd_parse_tree_new(QD_PARSE_TREE_ADDRESS);
    qd_iterator_t *iter = qd_iterator_string(input, ITER_VIEW_ALL);
    void *payload;

    if (qd_parse_tree_add_pattern(node, iter, (void *)input)) {
        qd_parse_tree_free(node);
        qd_iterator_free(iter);
        return "Unexpected duplicate pattern";
    }
    if (!qd_parse_tree_get_pattern(node, iter, &payload)) {
        qd_parse_tree_free(node);
        qd_iterator_free(iter);
        return "Could not find added pattern";
    }
    if (!payload || strcmp((const char *)payload, input)) {
        qd_parse_tree_free(node);
        qd_iterator_free(iter);
        return "Failed to find pattern";
    }

    qd_parse_tree_walk(node, visit_all, &vh);
    if (vh.count != 1) {
        qd_parse_tree_free(node);
        qd_iterator_free(iter);
        return "Did not find expected pattern";
    }
    if (strcmp(vh.payloads[0], input)) {
        qd_parse_tree_free(node);
        qd_iterator_free(iter);
        return "Unexpected payload!";
    }
    if (strcmp(vh.patterns[0], expected)) {
        fprintf(stderr, "%s %s\n", vh.patterns[0], expected);
        qd_parse_tree_free(node);
        qd_iterator_free(iter);
        return "Incorrect normalization";
    }

    payload = qd_parse_tree_remove_pattern(node, iter);
    if (!payload || strcmp((const char *)payload, input)) {
        qd_parse_tree_free(node);
        qd_iterator_free(iter);
        return "Failed to remove pattern";
    }

    qd_parse_tree_free(node);
    qd_iterator_free(iter);
    return NULL;
}


static char *test_normalization(void *context)
{
    char *rc = NULL;
    char *patterns[][2] = {
        // normalized  raw
        {"a.b.c",     "a.b.c"},
        {"a.*.c",     "a.*.c"},
        {"#",         "#"},
        {"#",         "#.#.#.#"},
        {"*.*.*.#",   "#.*.#.*.#.#.*"},
        {"a.*.*.*.#", "a.*.#.*.#.*.#"},
        {"a.*.*.*.#", "a.*.#.*.#.*"},
        {"*.*.*.#",   "*.#.#.*.*.#"},
        {NULL, NULL}
    };

    for (int i = 0; !rc && patterns[i][0]; i++)
        rc = check_normalize(patterns[i][1], patterns[i][0]);

    return rc;
}


typedef struct {
    const char *address;
    bool match;
} match_test_t;

static char *match_test(qd_parse_tree_type_t type,
                        const char *pattern,
                        const match_test_t *tests)
{
    char *rc = NULL;
    qd_iterator_t *piter = qd_iterator_string(pattern, ITER_VIEW_ALL);
    qd_parse_tree_t *node = qd_parse_tree_new(type);
    void *payload = (void *)"found";

    if (qd_parse_tree_add_pattern(node, piter, payload)) {
        qd_parse_tree_free(node);
        qd_iterator_free(piter);
        return "Unexpected error when adding pattern";
    }

    for (int i = 0; tests[i].address && !rc; i++) {
        qd_iterator_t *iter = qd_iterator_string(tests[i].address, ITER_VIEW_ALL);
        bool match = (int)qd_parse_tree_retrieve_match(node, iter, &payload);
        if (match != tests[i].match) {
            printf("match address '%s' to pattern '%s': expected %d got %d\n",
                   tests[i].address, pattern, (int)tests[i].match, (int)match);
            qd_iterator_free(iter);
            qd_parse_tree_free(node);
            qd_iterator_free(piter);
            return "Match test failed";
        }
        qd_iterator_free(iter);
    }

    qd_parse_tree_free(node);
    qd_iterator_free(piter);
    return NULL;
}


// check various pattern matches
static char *test_matches(void *context)
{
    match_test_t test1[] = {
        { "ab.cd.e",   true},
        { "abx.cd.e",  false},
        { "ab.cd",     false},
        { "ab.cd.ef.", false},
        { "ab.cd.E",   false},
        { "x.ab.cd.e", false},
        {NULL, false}
    };

    char *rc = match_test(QD_PARSE_TREE_ADDRESS, "ab.cd.e", test1);
    if (rc) return rc;

    match_test_t test4[] = {
        {"a.xx.b", true},
        {"a.b", false},
        {NULL, false}
    };
    rc = match_test(QD_PARSE_TREE_ADDRESS, "a.*.b", test4);
    if (rc) return rc;

    match_test_t test5[] = {
        {"y.x", true},
        {"x",   false},
        {"x.y", false},
        {NULL,  false}
    };
    rc = match_test(QD_PARSE_TREE_ADDRESS, "*.x", test5);
    if (rc) return rc;

    match_test_t test6[] = {
        {"x.x.y", true},
        {"x.x",   false},
        {"y.x.x", false},
        {"q.x.y", false},
        {NULL, false}
    };
    rc = match_test(QD_PARSE_TREE_ADDRESS, "x.x.*", test6);
    if (rc) return rc;


    match_test_t test7[] = {
        {"a.b", true},
        {"a.x.b", true},
        {"a..x.y.zz.b", true},
        {"a.b.z", false},
        {"z.a.b", false},
        {"q.x.b", false},
        {NULL, false}
    };
    rc = match_test(QD_PARSE_TREE_ADDRESS, "a.#.b", test7);
    if (rc) return rc;

    match_test_t test8[] = {
        {"a", true},
        {"a.b", true},
        {"a.b.c", true},
        {"b.a", false},
        {NULL, false}
    };
    rc = match_test(QD_PARSE_TREE_ADDRESS, "a.#", test8);
    if (rc) return rc;

    match_test_t test9[] = {
        {"a", true},
        {"x.y.a", true},
        {"a.b", false},
        {NULL, false}
    };
    rc = match_test(QD_PARSE_TREE_ADDRESS, "#.a", test9);
    if (rc) return rc;

    match_test_t test10[] = {
        {"a.b.c", true},
        {"a.x.b.y.c", true},
        {"a.x.x.b.y.y.c", true},
        {"a.b", false},
        {NULL, false}
    };
    rc = match_test(QD_PARSE_TREE_ADDRESS, "a.#.b.#.c", test10);
    if (rc) return rc;

    match_test_t test11[] = {
        {"a.x.y", true},
        {"a.x.p.qq.y", true},
        {"a.a.x.y", false},
        {"aa.x.b.c", false},
        {"x.p.qq.y", false},
        {"a.x.p.qq.y.b", false},
        {NULL, false}
    };
    rc = match_test(QD_PARSE_TREE_ADDRESS, "*.x.#.y", test11);
    if (rc) return rc;

    match_test_t test12[] = {
        {"a.b.x", true},
        {"a.x.x.x.b.x", true},
        {"a.b.b.b.b.y", true},
        {"a.b.b.b.b", true},
        {"a.b", false},
        {NULL, false}
    };
    rc = match_test(QD_PARSE_TREE_ADDRESS, "a.#.b.*", test12);
    if (rc) return rc;

    match_test_t test13[] = {
        {"x/y/z", true},
        {"x.y.z/a.b.c", true},
        {"x.y/z", true},
        {"x.y", false},
        {"x/y", false},
        {"x", false},
        {NULL, false}
    };
    rc = match_test(QD_PARSE_TREE_ADDRESS, "*.*.*.#", test13);
    if (rc) return rc;

    match_test_t test14[] = {
        {"x", false},
        {"x.y", true},
        {"x.y.z", true},
        {"x.y.z.y.z.y.z", true},
        {NULL, false}
    };
    rc = match_test(QD_PARSE_TREE_ADDRESS, "*/#/*", test14);

    match_test_t test15[] = {
        {"/policy", true},
        {"/good/policy", true},
        {"/really/really/good/policy", true},
        {"help/police", false},
        {"bad/polic", false},
        {"/bad/p", false},
        {NULL, false}
    };
    rc = match_test(QD_PARSE_TREE_ADDRESS, "/#/policy", test15);

    match_test_t test16[] = {
        {"some/policy", false},
        {"some.policy", true},
        {"policy", false},
        {"policy.ycilop", false},
        {"/hi/there/.policy", true},
        {"hi.there.policy", true},
        {"hi.there/policy", false},
        {NULL, false}
    };
    rc = match_test(QD_PARSE_TREE_AMQP_0_10, ".#.*.policy", test16);

    match_test_t test17[] = {
        {"some/policy", false},
        {"some.policy", false},
        {"policy", false},
        {".#./policy", false},
        {"hi/.#./policy", true},
        {"/hi/.there./policy", false},
        {NULL, false}
    };
    rc = match_test(QD_PARSE_TREE_MQTT, "+/.#./policy", test17);

    match_test_t test18[] = {
        {"test/?/*", true},
        {"test/?/*/one/two/three", true},
        {"test/something.or.other/*", true},
        {"test/something.or.other/xxx", false},
        {"test/something.or.other", false},
        {"test", false},
        {NULL, false}
    };
    rc = match_test(QD_PARSE_TREE_MQTT, "test/+/*/#", test18);
    return rc;
}

// For debug (see parse_tree.c)
// void qd_parse_tree_dump(qd_parse_node_t *node, int depth);


#define PCOUNT 17
typedef struct {
    const char *address;
    int count;
    const char *matches[PCOUNT];
} multi_match_t;

static char *multiple_matches(qd_parse_tree_type_t type,
                              const char *patterns[],
                              multi_match_t *tests)
{
    const char *_patterns[PCOUNT] = {NULL};
    void *_payloads[PCOUNT] = {NULL};
    visit_handle_t vh = {0, _patterns, _payloads};
    qd_parse_tree_t *node = qd_parse_tree_new(type);

    // build the tree
    for (int i = 0; i < PCOUNT; i++) {
        qd_iterator_t *pattern = qd_iterator_string(patterns[i], ITER_VIEW_ALL);
        if (qd_parse_tree_add_pattern(node, pattern, (void *)patterns[i])) {
            printf("Failed to add pattern %s to parse tree\n", patterns[i]);
            qd_iterator_free(pattern);
            qd_parse_tree_free(node);
            return "failed adding pattern to tree";
        }
        qd_iterator_free(pattern);
    }

    {
        // read all patterns and verify all are present
        qd_parse_tree_walk(node, visit_all, &vh);
        if (vh.count != PCOUNT) {
            qd_parse_tree_free(node);
            return "Not all patterns in tree";
        }
        for (int i = 0; i < PCOUNT; i++) {
            bool found = false;
            for (int j = 0; j < PCOUNT; j++) {
                if (strcmp(patterns[i], vh.patterns[j]) == 0)
                    found = true;
            }
            if (!found) {
                qd_parse_tree_free(node);
                return "All patterns not visited";
            }
        }
    }

    // verify all matching patterns are hit and in the correct order
    for (int k = 0; tests[k].address; k++) {
        qd_iterator_t *find_me = qd_iterator_string(tests[k].address, ITER_VIEW_ALL);
        vh.count = 0;
        qd_parse_tree_search(node, find_me, visit_all, (void *)&vh);
        //printf("Matches for %s:\n", tests[k].address);
        //for (int i = 0; i < vh.count; i++)
        //  printf("%s, ", vh.patterns[i]);
        //printf("count = %d\n", vh.count);
        if (vh.count != tests[k].count) {
            qd_iterator_free(find_me);
            qd_parse_tree_free(node);
            return "Unexpected match count";
        }
        for (int i = 0; i < tests[k].count; i++) {
            if (strcmp(vh.patterns[i], tests[k].matches[i])) {
                qd_iterator_free(find_me);
                qd_parse_tree_free(node);
                return "Unexpected pattern match";
            }
        }

        qd_iterator_free(find_me);
    }

    // verify 'best' match is found
    for (int k = 0; tests[k].address; k++) {
        qd_iterator_t *find_me = qd_iterator_string(tests[k].address, ITER_VIEW_ALL);
        vh.count = 0;
        qd_parse_tree_search(node, find_me, find_best, (void *)&vh);
        // printf("best match for %s: %s\n", tests[k].address, vh.patterns[0]);
        if (tests[k].count == 0) {
            if (vh.count != 0) {
                qd_iterator_free(find_me);
                qd_parse_tree_free(node);
                return "Did not expect to find a best match!";
            }
        } else if (vh.count == 0 || strcmp(vh.patterns[0], tests[k].matches[0])) {
            qd_iterator_free(find_me);
            qd_parse_tree_free(node);
            return "Unexpected best pattern match";
        }
        qd_iterator_free(find_me);
    }

    qd_parse_tree_free(node);

    return NULL;
}


// search a full parse tree for multiple and best matches
static char *test_multiple_matches(void *context)
{
    const char *patterns_amqp_0_10[PCOUNT] =
      { "alpha",
        "bravo",
        "alpha.bravo",
        "bravo.charlie",
        "alpha.bravo.charlie.delta",
        "bravo.charlie.delta.echo",
        "alpha.*",
        "alpha.#",
        "alpha.*.#",
        "#.bravo",
        "*.bravo",
        "*.#.bravo",
        "alpha.*.bravo",
        "alpha.#.bravo",
        "alpha.*.#.bravo",
        "*.bravo.*",
        "#.bravo.#",
      };
    // matches are listed in order of best->least best match
    multi_match_t tests_amqp_0_10[] = {
        {"alpha",       2, {"alpha", "alpha.#"}},
        {"alpha.zulu",  3, { "alpha.*", "alpha.*.#", "alpha.#"}},
        {"alpha.bravo", 9, {"alpha.bravo", "alpha.*", "alpha.*.#", "alpha.#.bravo", "alpha.#", "*.bravo", "*.#.bravo", "#.bravo", "#.bravo.#"}},
        {"bravo",       3, {"bravo", "#.bravo",  "#.bravo.#"}},
        {"xray.bravo",  4, {"*.bravo", "*.#.bravo", "#.bravo", "#.bravo.#"}},
        {"alpha.bravo.charlie",         4, {"alpha.*.#", "alpha.#", "*.bravo.*", "#.bravo.#"}},
        {"xray.yankee.zulu.bravo",      3, {"*.#.bravo", "#.bravo", "#.bravo.#"}},
        {"alpha.bravo.charlie.delta",   4, {"alpha.bravo.charlie.delta", "alpha.*.#","alpha.#", "#.bravo.#"}},
        {"alpha.charlie.charlie.bravo", 7, {"alpha.*.#.bravo", "alpha.*.#", "alpha.#.bravo", "alpha.#", "*.#.bravo", "#.bravo", "#.bravo.#"}},
        {"xray.yankeee.zulu.bravo.alpha.bravo.charlie", 2, {"#.bravo.#", "#.bravo.#"}},
        {"I.match.nothing", 0, {NULL}},
        {NULL, 0, {NULL}}
    };

    char *rc = multiple_matches(QD_PARSE_TREE_AMQP_0_10,
                                patterns_amqp_0_10,
                                tests_amqp_0_10);
    if (rc) return rc;

    const char *patterns_mqtt[PCOUNT] =
      { "alpha",
        "bravo",
        "alpha/bravo",
        "bravo/charlie",
        "alpha/bravo/charlie/delta",
        "alpha/+",
        "alpha/#",
        "alpha/+/#",
        "bravo/charlie/echo",
        "bravo/charlie/+",
        "+/bravo",
        "bravo/#",
        "alpha/+/bravo",
        "alpha/+/bravo/#",
        "+/+/+/#",
        "+/bravo/+",
        "xray/+/#"
      };

    // matches are listed in order of best->least best match
    multi_match_t tests_mqtt[] = {
        {"alpha",       2, {"alpha", "alpha/#"}},
        {"alpha/zulu",  3, { "alpha/+", "alpha/+/#", "alpha/#"}},
        {"alpha/bravo", 5, {"alpha/bravo", "alpha/+", "alpha/+/#", "alpha/#", "+/bravo"}},
        {"bravo",       2, {"bravo", "bravo/#"}},
        {"xray/bravo",  2, {"xray/+/#", "+/bravo"}},
        {"alpha/bravo/charlie",         4, {"alpha/+/#", "alpha/#", "+/bravo/+", "+/+/+/#"}},
        {"xray/yankee/zulu/bravo",      2, {"xray/+/#", "+/+/+/#"}},
        {"alpha/bravo/charlie/delta",   4, {"alpha/bravo/charlie/delta", "alpha/+/#", "alpha/#", "+/+/+/#"}},
        {"alpha/charlie/charlie/bravo", 3, {"alpha/+/#", "alpha/#", "+/+/+/#"}},
        {"xray/bravo/alpha", 3, {"xray/+/#", "+/bravo/+", "+/+/+/#"}},
        {"bravo/charlie/ech", 3, {"bravo/charlie/+", "bravo/#", "+/+/+/#"}},
        {"I/match.nothing", 0, {NULL}},
        {NULL, 0, {NULL}}
    };

    rc = multiple_matches(QD_PARSE_TREE_MQTT, patterns_mqtt, tests_mqtt);
    return rc;
}


static char *test_validation(void *context)
{
    qd_iterator_t *iter = qd_iterator_string("sam.*.am.#", ITER_VIEW_ALL);
    qd_iterator_t *iter_good = qd_iterator_string("sam/+/a.#.m/#", ITER_VIEW_ALL);
    qd_iterator_t *iter_bad  = qd_iterator_string("", ITER_VIEW_ALL);  // no tokens
    qd_iterator_t *iter_bad_slash = qd_iterator_string("/", ITER_VIEW_ALL);  // just separators
    qd_iterator_t *iter_bad_dot = qd_iterator_string(".", ITER_VIEW_ALL);  // just separators
    qd_iterator_t *iter_bad_mqtt = qd_iterator_string("sam/#/am/+", ITER_VIEW_ALL);  // glob must be last
    qd_iterator_t *iter_const = qd_iterator_string("sam/I/am", ITER_VIEW_ALL);
    qd_parse_tree_t *mqtt_tree = qd_parse_tree_new(QD_PARSE_TREE_MQTT);
    qd_parse_tree_t *addr_tree = qd_parse_tree_new(QD_PARSE_TREE_ADDRESS);
    qd_parse_tree_t *amqp_tree = qd_parse_tree_new(QD_PARSE_TREE_AMQP_0_10);

    char *error = 0;

    if (!qd_parse_tree_validate_pattern(addr_tree, iter) ||
        !qd_parse_tree_validate_pattern(amqp_tree, iter)) {
        error = "expected valid pattern";
        goto cleanup;
    }


    if (qd_parse_tree_validate_pattern(addr_tree, iter_bad) ||
        qd_parse_tree_validate_pattern(mqtt_tree, iter_bad) ||
        qd_parse_tree_validate_pattern(amqp_tree, iter_bad)) {
        error = "expected null pattern to be invalid";
        goto cleanup;
    }


    if (qd_parse_tree_validate_pattern(addr_tree, iter_bad_dot) ||
        qd_parse_tree_validate_pattern(amqp_tree, iter_bad_dot)) {
        error = "expected separator dot pattern to be invalid";
        goto cleanup;
    }

    if (qd_parse_tree_validate_pattern(addr_tree, iter_bad_slash) ||
        qd_parse_tree_validate_pattern(mqtt_tree, iter_bad_slash)) {
        error = "expected separator slash pattern to be invalid";
        goto cleanup;
    }

    if (!qd_parse_tree_validate_pattern(mqtt_tree, iter_good)) {
        error = "expected to pass mqtt validation";
        goto cleanup;
    }

    if (qd_parse_tree_validate_pattern(mqtt_tree, iter_bad_mqtt)) {
        error = "expected to fail mqtt validation";
        goto cleanup;
    }

    if (!qd_parse_tree_validate_pattern(mqtt_tree, iter_const)) {
        error = "expected to pass mqtt constant string validation";
        // fallthrough
    }

cleanup:
    qd_iterator_free(iter);
    qd_iterator_free(iter_good);
    qd_iterator_free(iter_bad);
    qd_iterator_free(iter_bad_slash);
    qd_iterator_free(iter_bad_dot);
    qd_iterator_free(iter_bad_mqtt);
    qd_iterator_free(iter_const);

    qd_parse_tree_free(mqtt_tree);
    qd_parse_tree_free(addr_tree);
    qd_parse_tree_free(amqp_tree);
    return error ? error : 0;
}


int parse_tree_tests(void)
{
    int result = 0;
    char *test_group = "parse_tree_tests";

    TEST_CASE(test_add_remove, 0);
    TEST_CASE(test_add_and_match_str, 0);
    TEST_CASE(test_duplicate_error_str, 0);
    TEST_CASE(test_normalization, 0);
    TEST_CASE(test_matches, 0);
    TEST_CASE(test_multiple_matches, 0);
    TEST_CASE(test_validation, 0);
    return result;
}

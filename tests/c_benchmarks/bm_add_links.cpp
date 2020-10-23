/*
 *
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
 *
 */

#include <benchmark/benchmark.h>
#include <cstdio>
#include <cstring>

extern "C" {
#include <qpid/dispatch.h>
#include "parse_tree.h"
#include "entity_cache.h"
}

static void BM_AddRemovePattern(benchmark::State &state) {
    qd_iterator_t *  piter = qd_iterator_string("I.am.Sam", ITER_VIEW_ALL);
    qd_parse_tree_t *node = qd_parse_tree_new(QD_PARSE_TREE_ADDRESS);
    void *           payload;

    for (auto _ : state) {
        qd_parse_tree_add_pattern(node, piter, &payload);
        qd_parse_tree_remove_pattern(node, piter);
    }

    qd_parse_tree_free(node);
    qd_iterator_free(piter);
}

BENCHMARK(BM_AddRemovePattern)->Unit(benchmark::kMicrosecond);


// https://github.com/apache/qpid-dispatch/pull/732/files
static void BM_AddAutolink(benchmark::State &state) {
    for(auto _: state) {
        // TODO
    }
}

BENCHMARK(BM_AddAutolink)->Unit(benchmark::kMicrosecond);

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

#include "../c_unittests/helpers.hpp"

#include <benchmark/benchmark.h>

#include <thread>

extern "C" {
#include "parse_tree.h"
}  // extern "C"

static void BM_AddRemoveSinglePattern(benchmark::State &state)
{
    std::thread([&state] {
        QDRMinimalEnv env{};

        qd_iterator_t *piter  = qd_iterator_string("I.am.Sam", ITER_VIEW_ALL);
        qd_parse_tree_t *node = qd_parse_tree_new(QD_PARSE_TREE_ADDRESS);
        void *payload;

        for (auto _ : state) {
            qd_parse_tree_add_pattern(node, piter, &payload);
            qd_parse_tree_remove_pattern(node, piter);
        }

        qd_parse_tree_free(node);
        qd_iterator_free(piter);
    }).join();
}

BENCHMARK(BM_AddRemoveSinglePattern)->Unit(benchmark::kMicrosecond);

static void BM_AddRemoveMultiplePatterns(benchmark::State &state)
{
    std::thread([&state] {
        QDRMinimalEnv env{};

        int batchSize = state.range(0);
        std::vector<std::string> data(batchSize);
        std::vector<qd_iterator_t *> piter(batchSize);
        for (int i = 0; i < batchSize; ++i) {
            data[i]  = "I.am.Sam_" + std::to_string(i);
            piter[i] = qd_iterator_string(data[i].c_str(), ITER_VIEW_ALL);
        }
        qd_parse_tree_t *node = qd_parse_tree_new(QD_PARSE_TREE_ADDRESS);
        const void *payload;

        for (auto _ : state) {
            for (int i = 0; i < batchSize; ++i) {
                qd_parse_tree_add_pattern(node, piter[i], &payload);
            }
            for (int i = 0; i < batchSize; ++i) {
                qd_parse_tree_remove_pattern(node, piter[i]);
            }
        }

        qd_parse_tree_free(node);
        for (int i = 0; i < batchSize; ++i) {
            qd_iterator_free(piter[i]);
        }

        state.SetComplexityN(batchSize);
    }).join();
}

BENCHMARK(BM_AddRemoveMultiplePatterns)
    ->Unit(benchmark::kMicrosecond)
    ->Arg(1)
    ->Arg(3)
    ->Arg(10)
    ->Arg(30)
    ->Arg(100)
    ->Arg(1000)
    ->Arg(100000)
    ->Complexity();

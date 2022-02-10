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

#define DOCTEST_CONFIG_IMPLEMENT
#include "qdr_doctest.hpp"
#include "cpp_stub.h"
#include "qdr_stubbing_probe.hpp"

#include <cstdio>
#include <cstdlib>

bool check_stubbing_works()
{

#if (defined(_FORTIFY_SOURCE))
    return false; // special checked glibc functions were substituted
#endif
#if (defined(__s390__) || defined(__s390x__) || defined(__zarch__))
    return false; // cpp-stub does not support
#endif

    {
        Stub stub;
        stub.set(probe, +[](int) -> int { return 42; });
        if (probe(0) != 42) {
            return false;
        }
    }
    {
        Stub stub;
        stub.set(abs, +[](int) -> int { return 24; });
        if (probe(0) != 24) {
            return false;
        }
    }

    return true;
}

// https://github.com/doctest/doctest/blob/master/doc/markdown/main.md
int main(int argc, char** argv)
{
    doctest::Context context;

    if (!check_stubbing_works()) {
#ifdef QD_REQUIRE_STUBBING_WORKS
        fprintf(stderr, "QD_REQUIRE_STUBBING_WORKS was defined, but stubbing doesn't work\n");
        abort();
#else
        fprintf(stderr, "Stubbing doesn't work. Define QD_REQUIRE_STUBBING_WORKS to get an abort()\n");
#endif
        context.addFilter("test-case-exclude", "*_STUB_*"); // skip testcases that require stubbing
        context.addFilter("subcase-exclude", "*_STUB_*"); //  ditto for subcases
    }

    context.applyCommandLine(argc, argv);

    int res = context.run();

    if (context.shouldExit()) {
        return res;
    }

    return res;
}

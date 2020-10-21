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

#ifndef QPID_DISPATCH_HELPERS_HPP
#define QPID_DISPATCH_HELPERS_HPP

extern "C" {
#include "../../src/router_core/agent_config_auto_link.h"

// declarations that don't have .h file
void qd_router_setup_late(qd_dispatch_t *qd);
}

// backport of C++14 feature
template< class T >
using remove_const_t = typename std::remove_const<T>::type;

// https://stackoverflow.com/questions/27440953/stdunique-ptr-for-c-functions-that-need-free
struct free_deleter{
    template <typename T>
    void operator()(T *p) const {
        std::free(const_cast<remove_const_t<T>*>(p));
    }
};
template <typename T>
using unique_C_ptr=std::unique_ptr<T,free_deleter>;
static_assert(sizeof(char *)==
              sizeof(unique_C_ptr<char>),""); // ensure no overhead

void set_content(qd_message_content_t *content, unsigned char *buffer, size_t len);

// This could be a viable path to address some sanitizer issues. Decide
// that unittested code is not allowed to leak, under unittests, and
// enforce it. As the amount of unittested code increases, the leaks
// will get squeezed out, maybe.

/// Redirects leak reports to a file, and fails the test if
/// anything is reported (even suppressed leaks).
class WithNoMemoryLeaks {
   public:
    unique_C_ptr<char> path_ptr {strdup("unittests_memory_debug_logs_XXXXXX")};
    WithNoMemoryLeaks() {
#if QD_MEMORY_DEBUG
        int fd = mkstemp(path_ptr.get());
            REQUIRE(fd != -1);
        qd_alloc_debug_dump(path_ptr.get());
#endif  // QD_MEMORY_DEBUG
    }

    ~WithNoMemoryLeaks() {
#if QD_MEMORY_DEBUG
        std::ifstream     f(path_ptr.get());
        std::stringstream buffer;
        buffer << f.rdbuf();
        std::string leak_reports = buffer.str();
        CHECK_MESSAGE(leak_reports.length() == 0, leak_reports);
        qd_alloc_debug_dump(nullptr);
#endif  // QD_MEMORY_DEBUG

        // TODO close that fd?
    }
};

// It is not possible to initialize the router multiple times in the same thread, due to
// alloc pools declared as `extern __thread qd_alloc_pool_t *`. These will have wrong values
// the second time around, and there is no good way to hunt them all down and NULL them.

// This also prevents me from doing a startup time benchmark. I can't run multiple startups
// in a loop to average them easily. There will be a way, but not with `for (auto _ : state)`.

/// Initializes and deinitializes the router
class QDR {
   public:
    qd_dispatch_t *qd;
    void start() {
        // prepare the smallest amount of things that qd_dispatch_free needs to be present
        qd = qd_dispatch(nullptr, false);
        // qd can be configured at this point, e.g. qd->thread_count
            REQUIRE(qd_dispatch_prepare(qd) == QD_ERROR_NONE);
        qd_router_setup_late(qd);  // sets up e.g. qd->router->router_core
    };

    /// cleaning up too early after init will lead to leaks and other
    /// unpleasantries (I observed some invalid pointer accesses)
    void wait() const {
        // todo Can I detect when startup is done?
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    void stop() const {
        qd_dispatch_free(qd);
    };
};
#endif  // QPID_DISPATCH_HELPERS_HPP

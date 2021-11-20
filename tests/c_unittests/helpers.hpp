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

#include <unistd.h>

#include <cassert>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>

// assertions without stack traces when running outside doctest
#ifndef QDR_DOCTEST
// https://stackoverflow.com/questions/3767869/adding-message-to-assert
#define REQUIRE(condition) assert(condition)
#define REQUIRE_MESSAGE(condition, message) \
do { \
if (! (condition)) { \
std::cerr << "Assertion `" #condition "` failed in " << __FILE__ \
<< " line " << __LINE__ << ": " << (message) << std::endl; \
std::terminate(); \
} \
} while (false)
#define CHECK_MESSAGE(condition, message) \
do { \
if (! (condition)) { \
std::cerr << "Assertion `" #condition "` failed in " << __FILE__ \
<< " line " << __LINE__ << ": " << (message) << std::endl; \
} \
} while (false)
#endif  // QDR_DOCTEST

extern "C" {
#include <dispatch_private.h>
#include <qpid/dispatch/dispatch.h>
#include <router_core/router_core_private.h>

// declarations that don't have .h file
void qd_router_setup_late(qd_dispatch_t *qd);
}

// low-level router initialization
extern "C" {
#include "entity_cache.h"
#include "log_private.h"

#include "qpid/dispatch.h"

// declarations that don't have .h file
void qd_error_initialize();
}

// backport of C++14 feature
template <class T>
using remove_const_t = typename std::remove_const<T>::type;

// https://stackoverflow.com/questions/17902405/how-to-implement-make-unique-function-in-c11
template<typename T, typename... Args>
std::unique_ptr<T> make_unique(Args&&... args)
{
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

// https://stackoverflow.com/questions/27440953/stdunique-ptr-for-c-functions-that-need-free
struct free_deleter {
    template <typename T>
    void operator()(T *p) const
    {
        std::free(const_cast<remove_const_t<T> *>(p));
    }
};
template <typename T>
using unique_C_ptr = std::unique_ptr<T, free_deleter>;
static_assert(sizeof(char *) == sizeof(unique_C_ptr<char>), "");  // ensure no overhead

// https://stackoverflow.com/questions/65290961/can-i-succintly-declare-stdunique-ptr-with-custom-deleter
template <typename T, typename Deleter>
std::unique_ptr<T, Deleter> qd_make_unique(T *raw, Deleter deleter)
{
    return std::unique_ptr<T, Deleter>(raw, deleter);
}

/* the above allows replacing the first declaration with the second, shorter, one:
std::unique_ptr<qdr_link_t, decltype(&free_qdr_link_t)> link{new_qdr_link_t(), free_qdr_link_t};
auto link = qd_make_unique(new_qdr_link_t(), free_qdr_link_t);
*/

/// Redirects leak reports to a file, and fails the test if
/// anything is reported (even suppressed leaks).
class WithNoMemoryLeaks
{
    bool fail;

   public:
    unique_C_ptr<char> path_ptr{strdup("unittests_memory_debug_logs_XXXXXX")};
    explicit WithNoMemoryLeaks(bool fail = true)
    {
        this->fail = fail;
#if QD_MEMORY_DEBUG
        int fd = mkstemp(path_ptr.get());
        (void) fd;  // unused
        REQUIRE(fd != -1);
        qd_alloc_debug_dump(path_ptr.get());
#endif  // QD_MEMORY_DEBUG
    }

    ~WithNoMemoryLeaks()
    {
#if QD_MEMORY_DEBUG
        std::ifstream f(path_ptr.get());
        std::stringstream buffer;
        buffer << f.rdbuf();
        std::string leak_reports = buffer.str();
        if (fail) {
            REQUIRE_MESSAGE(leak_reports.length() == 0, leak_reports);
        } else {
            CHECK_MESSAGE(leak_reports.length() == 0, leak_reports);
        }
        qd_alloc_debug_dump(nullptr);
        f.close();
#endif  // QD_MEMORY_DEBUG
    }
};

/// Submits an action to the router's action list. When action runs, we know router finished all previous actions.
///
/// This can be used to detect the router finished starting (i.e., performed all previously scheduled actions).
///
/// Enqueued actions get processed on the router core thread, one by one. These qdr_actions are different from Proton
/// proactor events that get processed in router's worker threads. Use qd timeouts to schedule on worker threads.
class RouterStartupLatch
{
   public:
    std::mutex mut;
    std::condition_variable cv;
    bool done = false;
    void wait_for_qdr(qd_dispatch_t *qd)
    {
        qdr_action_handler_t action_handler = [](qdr_core_t *core, qdr_action_t *action, bool discard) {
            auto that = static_cast<RouterStartupLatch *>(action->args.general.context_1);
            std::lock_guard<std::mutex> lock(that->mut);
            that->done = true;
            that->cv.notify_one();
        };
        qdr_action_t *action           = qdr_action(action_handler, "RouterStartupLatch action");
        action->args.general.context_1 = this;
        qdr_action_enqueue(qd->router->router_core, action);

        std::unique_lock<std::mutex> lock(mut);
        cv.wait(lock, [this] { return done; });  // wait for action_handler to notify us
    }
};

inline std::string get_env(std::string const &key)
{
    char *val = std::getenv(key.c_str());
    return val == nullptr ? std::string("") : std::string(val);
}

/// Manages the router lifecycle
/**
 Only one QDR instance may exist at the same time,
 and each time it has to be started in a fresh std::thread.

 The router uses global state, especially in relation to the Python module. That
 is why things break in weird ways if there is more than one running. It is
 possible to fork() and run multiple routers that way, but that gives no advantage
 over subprocess in Python system-tests, it only complicates things.

 It is not possible to initialize the router multiple times in the same thread, due to
 alloc pools declared as `extern __thread qd_alloc_pool_t *`. These will have wrong values
 the second time around, and there is no good way to hunt them all down and NULL them.
 */
class QDR
{
    // protects global variables around router startup and pool leak dumping
    static std::mutex startup_shutdown_lock;

   public:
    qd_dispatch_t *qd;
    /// prepare the smallest amount of things that qd_dispatch_free needs to be present
    void initialize(const std::string &config_path = "")
    {
        const std::lock_guard<std::mutex> lock(QDR::startup_shutdown_lock);

        qd = qd_dispatch(nullptr, false);
        REQUIRE(qd != nullptr);

        // qd can be configured at this point, e.g. qd->thread_count
        if (!config_path.empty()) {
            // call qd_dispatch_load_config to get management agent initialized
            // const std::string &source_dir = get_env("CMAKE_CURRENT_SOURCE_DIR");
            REQUIRE(qd_dispatch_validate_config(config_path.c_str()) == QD_ERROR_NONE);
            REQUIRE(qd_dispatch_load_config(qd, config_path.c_str()) == QD_ERROR_NONE);
        } else {
            // this is the abbreviated setup load_config() calls from Python, this way we can sometimes skip loading a
            // config file
            qd->thread_count = 1;
            REQUIRE(qd_dispatch_prepare(qd) == QD_ERROR_NONE);
            qd_router_setup_late(qd);  // sets up e.g. qd->router->router_core
        }
    };

    /// cleaning up too early after init will lead to leaks and other
    /// unpleasantries (I observed some invalid pointer accesses)
    void wait() const
    {
        // give the router core thread an action; when that executes, we're done starting up
        RouterStartupLatch{}.wait_for_qdr(qd);
    }

    /// Runs the router in the current thread (+ any new threads router decides to spawn).
    ///
    /// This method blocks until stop() is called.
    void run() const
    {
        qd_server_run(qd);
    }

    void stop() const
    {
        qd_server_stop(qd);
    }

    /// Schedules QDR.stop using qd_timer
    ///
    /// The returned value must outlive the end of timer activation!
    std::unique_ptr<qd_timer_t, void (*)(qd_timer_t *)> schedule_stop(int timeout = 0) const
    {
        qd_timer_t *timer = qd_timer(qd, [](void* context) {
                QDR *that = static_cast<QDR*>(context);
                that->stop();
            }, (void*)this);
        qd_timer_schedule(timer, timeout);
        return qd_make_unique(timer, qd_timer_free);
    }

    /// Frees the router and optionally checks for leaks.
    void deinitialize(bool check_leaks = true) const
    {
        const std::lock_guard<std::mutex> lock(QDR::startup_shutdown_lock);
        const WithNoMemoryLeaks wnml{check_leaks};

        qd_dispatch_free(qd);
        // cache is a global var and sanitizers are ok with not freeing what's reachable through it
        // however, redeclaring it in a second router without freeing what then becomes unreachable creates leak
        qd_entity_cache_free_entries();
    };
};

/// Synchronizes two threads. One waits at the latch, the other releases the latch.
class Latch
{
    std::mutex mut;
    std::condition_variable cv;
    bool opened = false;

   public:
    void notify()
    {
        std::lock_guard<std::mutex> lock(mut);
        opened = true;
        cv.notify_all();
    }
    void wait()
    {
        std::unique_lock<std::mutex> lock(mut);
        cv.wait(lock, [this] { return opened; });
    }
};

class QDRMinimalEnv
{
   public:
    QDRMinimalEnv()
    {
        qd_alloc_initialize();
        qd_log_initialize();
        qd_error_initialize();
    }

    ~QDRMinimalEnv()
    {
        qd_log_finalize();
        qd_alloc_finalize();
        qd_entity_cache_free_entries();
    }
};

class CaptureCStream
{
    FILE **mStream;
    FILE *mMemstream;
    FILE *mOriginal;

    char *buf;
    size_t size;
   public:
    CaptureCStream(FILE **stream) : mStream(stream), mOriginal(*stream) {
        mMemstream = open_memstream(&buf, &size);
        *mStream = mMemstream;
    }

    void reset() {
        *mStream = mOriginal;
    }

    size_t checkpoint() {
        fflush(mMemstream);
        return size;
    }

    std::string str() {
        fflush(mMemstream);
        return std::string(buf, size);
    }

    std::string str(size_t begin) {
        fflush(mMemstream);
        return std::string(buf + begin, size - begin);
    }

    ~CaptureCStream() {
        reset();
        fclose(mMemstream);
        free(buf);
    }
};

#endif  // QPID_DISPATCH_HELPERS_HPP

#include <cstddef>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

#include "./qdr_doctest.h"  // or .hpp, to make it clear this is a C++ header?

extern "C" {
#include "../../src/router_core/agent_config_auto_link.h"
//#include <router_core/agent_config_auto_link.h>

// declarations that don't have .h file
void qd_router_setup_late(qd_dispatch_t *qd);
}

// This could be a viable path to address some sanitizer issues. Decide
// that unittested code is not allowed to leak, under unittests, and
// enforce it. As the amount of unittested code increases, the leaks
// will get squeezed out, maybe.

/// Redirects leak reports to a file, and fails the test if
/// anything is reported (even suppressed leaks).
class WithNoMemoryLeaks {
   public:
    std::unique_ptr<char> path_ptr {strdup("unittests_memory_debug_logs_XXXXXX")};
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
        std::string reports = buffer.str();
        CHECK_MESSAGE(reports.length() == 0, reports);
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

TEST_CASE("Start and shutdown router twice" * doctest::skip(false)) {
    std::thread([]() {
        WithNoMemoryLeaks leaks{};
        QDR qdr{};
        qdr.start();
        qdr.wait();
        qdr.stop();
        // todo check for more errors, maybe in logging calls?
    }).join();
    std::thread([]() {
        WithNoMemoryLeaks leaks{};
        QDR qdr{};
        qdr.start();
        qdr.wait();
        qdr.stop();
    }).join();
}

TEST_CASE("More to come" * doctest::skip(false)) {
    std::thread([]() {
        WithNoMemoryLeaks leaks{};
        QDR qdr{};
        qdr.start();
        qdr.wait();

        qdr_core_t *core = qdr.qd->router->router_core;
        // qdr_route_table_setup_CT(core) happened in qd_router_setup_late

        qd_iterator_t *name = qd_iterator_string("I.am.Sam", ITER_VIEW_ALL);

        void *context = nullptr;
        qd_router_entity_type_t type = QD_ROUTER_LINK;
        qd_composed_field_t *composed_body = NULL;
        uint64_t in_conn_id = 0;
        qdr_query_t *query = qdr_query(core, context, type, composed_body, in_conn_id);

    // TODO fix the following
    //  70: Error performing CREATE of org.apache.qpid.dispatch.router.config.autoLink: Body of request must be a map
    qd_parsed_field_t *parsed_body = NULL;

    qdra_config_auto_link_create_CT(core, name, query, parsed_body);

    // don't do qdr_query_free(query), it was freed when configuring failed
    qd_iterator_free(name);

        // todo check for more errors, maybe in log calls?
    }).join();
}

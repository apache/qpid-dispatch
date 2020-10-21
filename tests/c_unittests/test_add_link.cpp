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

#include <cstddef>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

#include "./qdr_doctest.h"  // or .hpp, to make it clear this is a C++ header?

extern "C" {
#include <proton/message.h>
#include "../../src/router_core/agent_config_auto_link.h"
//#include <router_core/agent_config_auto_link.h>

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

// from message_test.c
static void set_content(qd_message_content_t *content, unsigned char *buffer, size_t len)
{
    unsigned char        *cursor = buffer;
    qd_buffer_t *buf;

    while (len > (size_t) (cursor - buffer)) {
        buf = qd_buffer();
        size_t segment   = qd_buffer_capacity(buf);
        size_t remaining = len - (size_t) (cursor - buffer);
        if (segment > remaining)
            segment = remaining;
        memcpy(qd_buffer_base(buf), cursor, segment);
        cursor += segment;
        qd_buffer_insert(buf, segment);
        DEQ_INSERT_TAIL(content->buffers, buf);
    }
    content->receive_complete = true;
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
        uint64_t in_conn_id = 0;

//        qd_composed_field_t *composed_body = NULL;
        qd_composed_field_t *composed_body = qd_compose_subfield(0);
        // nobody is looking at this, yet; TODO because this is to store the reply, not request!
//        qd_compose_start_map(composed_body);
//        qd_compose_insert_string(composed_body, "address");
//        qd_compose_insert_string(composed_body, "cc");
//        qd_compose_insert_string(composed_body, "direction");
//        qd_compose_insert_string(composed_body, "dd");
//        qd_compose_end_map(composed_body);

        qdr_query_t *query = qdr_query(core, context, type, composed_body, in_conn_id);

        // TODO fix the following
        //  70: Error performing CREATE of org.apache.qpid.dispatch.router.config.autoLink: Body of request must be a map
        qd_message_t *msg = qd_message();
        qd_message_content_t *content = MSG_CONTENT(msg);

        pn_message_t *pn_msg = pn_message();
        pn_data_t *body;
        body = pn_message_body(pn_msg);
        pn_data_put_map(body);
        pn_data_enter(body);

        auto put_key_value = [&body](const std::string& key, const std::string& value) {
          pn_data_put_string(body, pn_bytes(key.length(), key.c_str()));
          pn_data_put_string(body, pn_bytes(value.length(), value.c_str()));
        };
        put_key_value("address", "aa");
        put_key_value("direction", "out");

        pn_data_exit(body);
        pn_rwbytes_t buf{};
        REQUIRE(pn_message_encode2(pn_msg, &buf) != 0);
        set_content(content, (unsigned char *)buf.start, buf.size);
        free(buf.start);

        pn_message_free(pn_msg);

//        size_t       size = 10000;
//        int result = pn_message_encode(pn_msg, (char *)buffer, &size);
//        pn_message_free(pn_msg);

        qd_iterator_t* iter = qd_message_field_iterator(msg, QD_FIELD_BODY);
        qd_parsed_field_t *parsed_body = qd_parse(iter);
//        qd_message_free(msg);  // DONT free this yet! references into msg are held

        // huh? TODO, need this to see error from CREATE
        qdr.qd->router->router_core->agent_log = qdr.qd->router->log_source;

        // sanity check the body is set correctly
        const int QDR_CONFIG_AUTO_LINK_ADDRESS = 3;
        qd_parsed_field_t *addr_field = qd_parse_value_by_key(parsed_body, qdr_config_auto_link_columns[QDR_CONFIG_AUTO_LINK_ADDRESS]);
        REQUIRE(addr_field != nullptr);

        qdra_config_auto_link_create_CT(core, name, query, parsed_body);
        // called qdr_route_add_auto_link_CT to actually add the auto link

        // result is put into query; no need to read logs
        CHECK(query->status.status == QD_AMQP_CREATED.status);  // some smarter compare in doctest?
        CHECK(query->status.description == QD_AMQP_CREATED.description);
        // if query->body is null, it is not set
        if (query->body != nullptr) {  // nonsense, query would be freed at this point if query->body was == null
            //            CHECK(query->body)
            // todo, there will be map with 15 fields about the autoLink; looking in debugger, there's no map now...
            //  it's there, just hard to see in the buffer, had to do (char(*)[512])qd_buffer_base(query->body->buffers.head)
        }

        qd_parse_free(parsed_body);
        qd_message_free(msg);  // OK to free now

        // don't do qdr_query_free(query), it was freed when configuring failed
        qdr_query_free(query);  // actually, set query->body to non-null, and then it won't be auto-freed!
        qd_iterator_free(name);

    }).join();
}

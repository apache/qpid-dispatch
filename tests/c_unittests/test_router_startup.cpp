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

#include "./qdr_doctest.h"  // or .hpp, to make it clear this is a C++ header?
#include "./helpers.hpp"
#include "websocket_client.h"

#include <cstddef>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

extern "C" {
#include <proton/message.h>
#include <router_core/agent_config_auto_link.h>
}

#include <condition_variable>
#include <memory>

// It is not possible to initialize the router multiple times in the same thread, due to
// alloc pools declared as `extern __thread qd_alloc_pool_t *`. These will have wrong values
// the second time around, and there is no good way to hunt them all down and NULL them.

TEST_CASE("Initialize and deinitialize router twice" * doctest::skip(false)) {
    std::thread([]() {
        QDR qdr{};
        qdr.initialize("./minimal_silent.conf");
        qdr.wait();
        qdr.deinitialize();
        // TODO check for more errors, maybe in logging calls?
    }).join();

    std::thread([]() {
      QDR qdr{};
      qdr.initialize("./minimal_silent.conf");
      qdr.wait();
      qdr.deinitialize();
    }).join();
}

TEST_CASE("Shut down router while websocket is connected") {
    struct test_data_t {
        int http_port = 0;
    };

    std::string router_config = R"END(
listener {
    host: 0.0.0.0
    port: 0
    authenticatePeer: no
    saslMechanisms: ANONYMOUS
    http: yes
}

log {
    module: DEFAULT
    enable: debug+
}
)END";

    std::fstream f("config.conf", std::ios::out);
    f << router_config;
    f.close();

    std::thread([]() {
        QDR qdr{};
        qdr.initialize("./config.conf");

        test_data_t action_data{};

        qdr_action_handler_t handler = [](qdr_core_t *core, qdr_action_t *action, bool discard) {
            auto action_data = static_cast<test_data_t *>(action->args.general.context_1);
            printf("listeners: \n");
//            struct qd_connection_manager_t *connection_manager = core->qd->connection_manager;

//            action_data->port = qd_dispatch_get_listener_port(core->qd);
            action_data->http_port = qd_dispatch_get_http_listener_port(core->qd);
            fprintf(stderr, "port: %d", action_data->http_port);
        };
        qdr_action_t *action           = qdr_action(handler, "RouterStartupLatch action");
        action->args.general.context_1 = &action_data;
        qdr_action_enqueue(qdr.qd->router->router_core, action);

        qdr.wait();

        websocket_client wsc("localhost", action_data.http_port);
        auto lws_thread = std::thread([&wsc]{
            wsc.loop();
        });

      auto qdr_thread = std::thread([&qdr]{
            qdr.run();
       });

      // wait for websocket connection establishment
      {
          std::unique_lock<std::mutex> lk(wsc.mutex);
          wsc.cv.wait(lk, [&]{return wsc.is_connected;});
      }

      qdr.stop();
      qdr_thread.join();

      qdr.deinitialize();
      lws_thread.join();
    }).join();
}
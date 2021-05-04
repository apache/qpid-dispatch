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
#include "./helpers.hpp"

extern "C" {
#include <proton/message.h>
#include <router_core/agent_config_auto_link.h>
}

#include <fstream>
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
        // todo check for more errors, maybe in logging calls?
    }).join();

    std::thread([]() {
      QDR qdr{};
      qdr.initialize("./minimal_silent.conf");
      qdr.wait();
      qdr.deinitialize();
    }).join();
}

TEST_CASE("Shut down router while websocket is connected") {
    std::string config = R"END(
listener {
    host: 0.0.0.0
    port: 0
    authenticatePeer: no
    saslMechanisms: ANONYMOUS
    http: yes
}

log {
    module: DEFAULT
    enable: warn+
}
)END";

    std::fstream f("config.conf", std::ios::out);
    f << config;
    f.close();

    std::thread([]() {
      QDR qdr{};
      qdr.initialize("./config.conf");
      qdr.wait();

      qdr_action_handler_t handler = [](qdr_core_t *core, qdr_action_t *action, bool discard) {
        static_cast<RouterStartupLatch *>(action->args.general.context_1)->mut.unlock();
          printf("listeners: \n");
        qd_connection_manager_t *connection_manager = core->qd->connection_manager;
//        connection_manager->
        core->qd->container
      };
      qdr_action_t *action = qdr_action(handler, "RouterStartupLatch action");
      action->args.general.context_1 = nullptr;
      qdr_action_enqueue(qdr.qd->router->router_core, action);

      qdr.run();
      qdr.deinitialize();
    }).join();
}
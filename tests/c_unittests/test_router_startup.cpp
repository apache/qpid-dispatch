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

// from message_test.c
void set_content(qd_message_content_t *content, unsigned char *buffer, size_t len)
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

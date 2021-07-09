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

#include "qdr_doctest.hpp"

extern "C" {
#include "qpid/dispatch/amqp.h"
}

TEST_CASE("test_qd_port_int") {
    SUBCASE("numeric ports, edge cases") {
        CHECK(qd_port_int("-1") == -1);
        CHECK(qd_port_int("0") == 0);
        CHECK(qd_port_int("1") == 1);
        CHECK(qd_port_int("5672") == 5672);
        CHECK(qd_port_int("65535") == 65535);
        CHECK(qd_port_int("65536") == -1);
    }
    SUBCASE("well known symbolic ports") {
        CHECK(qd_port_int("amqp") == 5672);
        CHECK(qd_port_int("amqps") == 5671);
        CHECK(qd_port_int("http") == 80);
    }
    SUBCASE("invalid inputs") {
        CHECK(qd_port_int("") == -1);

        CHECK(qd_port_int("42http") == -1);
        CHECK(qd_port_int("http42") == -1);

        CHECK(qd_port_int("no_such_port") == -1);
    }
}

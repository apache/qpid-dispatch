#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(benchmark REQUIRED)

# need -Wno-pedantic, for two reasons:
# incompatibility in older libbenchmark, https://github.com/google/benchmark/issues/494#issuecomment-502444478
# PRIu64 macros without spaces around are invalid C++ (c.f. the C++11 suffixes feature)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CXX_STANDARD_FLAGS} ${SANITIZE_FLAGS} -Wno-pedantic")

add_executable(c-benchmarks
        ../c_unittests/helpers.cpp
        benchmarks_main.cpp
        bm_add_links.cpp
        echo_server.cpp
        PracticalSocket.cpp
        PracticalSocket.h
        echo_server.h)
target_link_libraries(c-benchmarks qpid-dispatch benchmark pthread qpid-dispatch)

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/minimal_silent.conf DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/tcp_benchmarks.conf DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/l1.conf DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/l2.conf DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

add_test(NAME c-benchmarks COMMAND ${TEST_WRAP} $<TARGET_FILE:c-benchmarks>)

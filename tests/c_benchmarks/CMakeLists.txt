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
#  incompatibility in older libbenchmark, https://github.com/google/benchmark/issues/494#issuecomment-502444478
#  PRIu64 macros without spaces around are invalid C++ (c.f. the C++11 value suffixes feature)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CXX_STANDARD_FLAGS} ${SANITIZE_FLAGS} -Wno-pedantic -Wno-unused-function")

add_executable(c-benchmarks
        ../c_unittests/helpers.cpp
        c_benchmarks_main.cpp
        bm_router_initialization.cpp
        bm_parse_tree.cpp
        echo_server.cpp echo_server.hpp
        socket_utils.cpp socket_utils.hpp
        Socket.cpp Socket.hpp
        SocketException.cpp SocketException.hpp
        TCPSocket.cpp TCPSocket.hpp
        TCPServerSocket.cpp TCPServerSocket.hpp
        $<TARGET_OBJECTS:qpid-dispatch>)
target_link_libraries(c-benchmarks qpid-dispatch-libraries benchmark pthread)

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/minimal_silent.conf DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

# set short minimal run time, so that the benchmark loops run ~once
add_test(NAME c-benchmarks COMMAND ${TEST_WRAP} $<TARGET_FILE:c-benchmarks> --benchmark_min_time=0.001)

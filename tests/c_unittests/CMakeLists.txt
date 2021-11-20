##
## Licensed to the Apache Software Foundation (ASF) under one
## or more contributor license agreements.  See the NOTICE file
## distributed with this work for additional information
## regarding copyright ownership.  The ASF licenses this file
## to you under the Apache License, Version 2.0 (the
## "License"); you may not use this file except in compliance
## with the License.  You may obtain a copy of the License at
##
##   http://www.apache.org/licenses/LICENSE-2.0
##
## Unless required by applicable law or agreed to in writing,
## software distributed under the License is distributed on an
## "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
## KIND, either express or implied.  See the License for the
## specific language governing permissions and limitations
## under the License.
##

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CXX_STANDARD_FLAGS}")
# DISPATCH-2196 Linking object files containing instances of the same C++ template fails on s390x with sanitizers enabled
if(CMAKE_SYSTEM_PROCESSOR STREQUAL "s390x")
    # set the sanitizer flags only for linking, not for compilation; this workarounds the failure
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${SANITIZE_FLAGS}")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${SANITIZE_FLAGS}")
endif()

add_executable(c_unittests
        c_unittests_main.cpp
        doctest.h
        qdr_doctest.hpp
        helpers.cpp
        helpers.hpp
        test_amqp.cpp
        test_listener_startup.cpp
        test_router_startup.cpp
        test_terminus.cpp
        $<TARGET_OBJECTS:qpid-dispatch>)
target_link_libraries(c_unittests pthread qpid-dispatch-libraries)

file(COPY
        ${CMAKE_CURRENT_SOURCE_DIR}/minimal_silent.conf
        ${CMAKE_CURRENT_SOURCE_DIR}/minimal_trace.conf
        DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

add_test(
    NAME c_unittests
    COMMAND ${TEST_WRAP} $<TARGET_FILE:c_unittests>
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

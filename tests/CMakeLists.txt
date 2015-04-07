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

include_directories(
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}/src
  )

##
## Build test applications
##
set(unit_test_SOURCES
    compose_test.c
    parse_test.c
    run_unit_tests.c
    server_test.c
    timer_test.c
    tool_test.c
    )
if (USE_MEMORY_POOL)
  list(APPEND unit_test_SOURCES alloc_test.c)
endif()

add_executable(unit_tests ${unit_test_SOURCES})
target_link_libraries(unit_tests qpid-dispatch)

set(unit_test_size_SOURCES
    field_test.c
    message_test.c
    buffer_test.c
    run_unit_tests_size.c
    )

add_executable(unit_tests_size ${unit_test_size_SOURCES})
target_link_libraries(unit_tests_size qpid-dispatch)

set(TEST_WRAP ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/run.py)

add_test(unit_tests_size_10000 ${TEST_WRAP} --vg unit_tests_size 10000)
add_test(unit_tests_size_512   ${TEST_WRAP} --vg unit_tests_size 512)
add_test(unit_tests_size_10    ${TEST_WRAP} --vg unit_tests_size 10)
add_test(unit_tests_size_7     ${TEST_WRAP} --vg unit_tests_size 7)
add_test(unit_tests_size_5     ${TEST_WRAP} --vg unit_tests_size 5)
add_test(unit_tests_size_3     ${TEST_WRAP} --vg unit_tests_size 3)
add_test(unit_tests_size_2     ${TEST_WRAP} --vg unit_tests_size 2)
add_test(unit_tests_size_1     ${TEST_WRAP} --vg unit_tests_size 1)
add_test(unit_tests            ${TEST_WRAP} --vg unit_tests ${CMAKE_CURRENT_SOURCE_DIR}/threads4.conf)
add_test(router_tests          ${TEST_WRAP} -s ${CMAKE_CURRENT_SOURCE_DIR}/router_engine_test.py -v)
add_test(management_tests      ${TEST_WRAP} -m unittest -v management)

macro(add_system_test test)
  add_test(${test} ${TEST_WRAP} -m ${test} -v)
endmacro(add_system_test)

add_system_test(system_tests_broker)
add_system_test(system_tests_management)
add_system_test(system_tests_one_router)
add_system_test(system_tests_qdmanage)
add_system_test(system_tests_qdstat)
add_system_test(system_tests_two_routers)

# NOTE: Don't install run.py. A system test of a dispatch installation should pick everything
# up from standard install locations.
#
set(SYSTEM_TEST_FILES
  run_system_tests.py system_test.py
  system_tests_one_router.py system_tests_two_routers.py
  system_tests_broker.py system_tests_management.py
  system_tests_qdstat.py system_tests_qdmanage.py)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config-2/A-ssl.conf.in ${CMAKE_CURRENT_BINARY_DIR}/config-2/A-ssl.conf)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config-2/B-ssl.conf.in ${CMAKE_CURRENT_BINARY_DIR}/config-2/B-ssl.conf)

install(FILES ${SYSTEM_TEST_FILES}
        DESTINATION ${QPID_DISPATCH_HOME_INSTALLED}/tests
        )

install(DIRECTORY config-1 config-2
        DESTINATION ${QPID_DISPATCH_HOME_INSTALLED}/tests
        PATTERN *.in EXCLUDE
        )

install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/config-2
        DESTINATION ${QPID_DISPATCH_HOME_INSTALLED}/tests
        FILES_MATCHING PATTERN *.conf
        )

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/ssl_certs
        DESTINATION ${QPID_DISPATCH_HOME_INSTALLED}/tests)


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

# Move all the python system tests from the src/tests dir to the build/tests dir and execute the tests from there.
# While moving the files make sure that configure_file() is called on any .py.in files resulting in .py file
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/system_tests_sasl_plain.py.in ${CMAKE_CURRENT_BINARY_DIR}/system_tests_sasl_plain.py)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/router_engine_test.py DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/router_policy_test.py DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/system_tests_broker.py DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/system_tests_management.py DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/system_tests_one_router.py DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/system_tests_qdmanage.py DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/system_tests_qdstat.py DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/system_tests_two_routers.py DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/system_tests_link_routes.py DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/system_tests_policy.py DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/run_system_tests.py DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/system_test.py DESTINATION ${CMAKE_CURRENT_BINARY_DIR})


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

# Add all sytem_tests* using add_test
add_test(router_tests             ${TEST_WRAP} -s ${CMAKE_CURRENT_BINARY_DIR}/router_engine_test.py -v)
add_test(router_tests_policy      ${TEST_WRAP} -s ${CMAKE_CURRENT_BINARY_DIR}/router_policy_test.py -v)
add_test(system_tests_broker      ${TEST_WRAP} -s ${CMAKE_CURRENT_BINARY_DIR}/system_tests_broker.py -v)
add_test(system_tests_management  ${TEST_WRAP} -s ${CMAKE_CURRENT_BINARY_DIR}/system_tests_management.py -v)
add_test(system_tests_one_router  ${TEST_WRAP} -s ${CMAKE_CURRENT_BINARY_DIR}/system_tests_one_router.py -v)
add_test(system_tests_qdmanage    ${TEST_WRAP} -s ${CMAKE_CURRENT_BINARY_DIR}/system_tests_qdmanage.py -v)
add_test(system_tests_qdstat      ${TEST_WRAP} -s ${CMAKE_CURRENT_BINARY_DIR}/system_tests_qdstat.py -v)
add_test(system_tests_two_routers ${TEST_WRAP} -s ${CMAKE_CURRENT_BINARY_DIR}/system_tests_two_routers.py -v)
add_test(system_tests_link_routes ${TEST_WRAP} -s ${CMAKE_CURRENT_BINARY_DIR}/system_tests_link_routes.py -v)
add_test(system_tests_policy      ${TEST_WRAP} -s ${CMAKE_CURRENT_BINARY_DIR}/system_tests_policy.py -v)
add_test(system_tests_sasl_plain  ${TEST_WRAP} -s ${CMAKE_CURRENT_BINARY_DIR}/system_tests_sasl_plain.py -v)
add_test(management_tests         ${TEST_WRAP} -m unittest -v management)

#macro(add_system_test test)
#  add_test(${test} ${TEST_WRAP} -m ${test} -v)
#endmacro(add_system_test)

# NOTE: Don't install run.py. A system test of a dispatch installation should pick everything
# up from standard install locations.
#
set(SYSTEM_TEST_FILES
  ${CMAKE_CURRENT_BINARY_DIR}/router_engine_test.py
  ${CMAKE_CURRENT_BINARY_DIR}/router_policy_test.py
  ${CMAKE_CURRENT_BINARY_DIR}/run_system_tests.py
  ${CMAKE_CURRENT_BINARY_DIR}/system_test.py
  ${CMAKE_CURRENT_BINARY_DIR}/system_tests_one_router.py
  ${CMAKE_CURRENT_BINARY_DIR}/system_tests_two_routers.py
  ${CMAKE_CURRENT_BINARY_DIR}/system_tests_broker.py
  ${CMAKE_CURRENT_BINARY_DIR}/system_tests_management.py
  ${CMAKE_CURRENT_BINARY_DIR}/system_tests_qdstat.py
  ${CMAKE_CURRENT_BINARY_DIR}/system_tests_qdmanage.py
  ${CMAKE_CURRENT_BINARY_DIR}/system_tests_link_routes.py
  ${CMAKE_CURRENT_BINARY_DIR}/system_tests_sasl_plain.py
  ${CMAKE_CURRENT_BINARY_DIR}/system_tests_policy.py)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config-2/A-ssl.conf.in ${CMAKE_CURRENT_BINARY_DIR}/config-2/A-ssl.conf)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config-2/B-ssl.conf.in ${CMAKE_CURRENT_BINARY_DIR}/config-2/B-ssl.conf)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/sasl_configs/qdrouterd.sasldb DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/sasl_configs)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/sasl_configs/tests-mech-EXTERNAL.conf DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/sasl_configs)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/sasl_configs/tests-mech-NOEXTERNAL.conf DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/sasl_configs)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/ssl_certs DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/sasl_configs/tests-mech-PLAIN.conf.in ${CMAKE_CURRENT_BINARY_DIR}/sasl_configs/tests-mech-PLAIN.conf)
file(COPY      ${CMAKE_CURRENT_SOURCE_DIR}/policy-2/policy-photoserver-sasl.sasldb  DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/policy-2)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/policy-2/policy-photoserver-sasl.conf.in ${CMAKE_CURRENT_BINARY_DIR}/policy-2/policy-photoserver-sasl.conf)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/policy-2/test-router-with-policy.json.in ${CMAKE_CURRENT_BINARY_DIR}/policy-2/test-router-with-policy.json)

# following install() functions will be called only if you do a make "install"
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

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
    policy_test.c
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

# Unit test python modules
add_test(router_engine_test    ${TEST_WRAP} -m unittest -v router_engine_test)
add_test(managemet_test        ${TEST_WRAP} -m unittest -v management)
add_test(router_policy_test    ${TEST_WRAP} -m unittest -v router_policy_test)

# System test python modules
foreach(py_test_module
#   system_tests_broker
    system_tests_link_routes
    system_tests_drain
    system_tests_management
    system_tests_one_router
    system_tests_policy
    system_tests_protocol_family
    system_tests_qdmanage
    system_tests_qdstat
    system_tests_sasl_plain
    system_tests_user_id
    system_tests_deprecated
    system_tests_two_routers)

  add_test(${py_test_module} ${TEST_WRAP} -m unittest -v ${py_test_module})
  list(APPEND SYSTEM_TEST_FILES ${CMAKE_CURRENT_SOURCE_DIR}/${py_test_module}.py)
endforeach()

# Additional files to install for running system tests.
# NOTE: Don't install run.py. A system test of a dispatch installation should pick everything
# up from standard install locations.
#
list(APPEND SYSTEM_TEST_FILES
  ${CMAKE_CURRENT_SOURCE_DIR}/run_system_tests.py
  ${CMAKE_CURRENT_SOURCE_DIR}/system_test.py)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config-2/A-ssl.conf.in ${CMAKE_CURRENT_BINARY_DIR}/config-2/A-ssl.conf)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config-2/B-ssl.conf.in ${CMAKE_CURRENT_BINARY_DIR}/config-2/B-ssl.conf)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/policy-1/test-policy-conf-includes-folder.conf.in ${CMAKE_CURRENT_BINARY_DIR}/policy-1/test-policy-conf-includes-folder.conf)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/policy-2/policy-photoserver-sasl.conf.in ${CMAKE_CURRENT_BINARY_DIR}/policy-2/policy-photoserver-sasl.conf)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/policy-2/test-router-with-policy.json.in ${CMAKE_CURRENT_BINARY_DIR}/policy-2/test-router-with-policy.json)

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/ssl_certs DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/displayname_files DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/policy-1/management-access.json  DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/policy-1/)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/policy-1/policy-boardwalk.json   DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/policy-1/)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/policy-1/policy-safari.json      DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/policy-1/)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/policy-2/policy-photoserver-sasl.sasldb  DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/policy-2)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/policy-3/test-sender-receiver-limits.json DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/policy-3)

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

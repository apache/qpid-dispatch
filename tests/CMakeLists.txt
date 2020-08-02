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
    policy_test.c
    run_unit_tests.c
    tool_test.c
    failoverlist_test.c
    timer_test.c
    core_timer_test.c
    parse_tree_tests.c
    proton_utils_tests.c
    alloc_test.c
    version_test.c
    hash_test.c
    thread_test.c
    )

add_executable(unit_tests ${unit_test_SOURCES})
target_link_libraries(unit_tests  qpid-dispatch)

set(unit_test_size_SOURCES
    field_test.c
    parse_test.c
    message_test.c
    buffer_test.c
    run_unit_tests_size.c
    )

add_executable(unit_tests_size ${unit_test_size_SOURCES})
target_link_libraries(unit_tests_size qpid-dispatch)

add_executable(test-sender test-sender.c)
target_link_libraries(test-sender ${Proton_LIBRARIES} qpid-dispatch)

add_executable(test-receiver test-receiver.c)
target_link_libraries(test-receiver ${Proton_LIBRARIES})

add_executable(clogger clogger.c)
target_link_libraries(clogger ${Proton_LIBRARIES})

# Bubblewrap is an unprivileged sandboxing tool for Linux. Setting --unshare-net allows
# running tests in parallel (the ctest -j option) without port clashes
set(USE_BWRAP OFF CACHE BOOL "Wrap test executions with bwrap (https://github.com/containers/bubblewrap)")
if(USE_BWRAP)
  # Inaccessible DNS servers produce "proton:io Temporary failure in name resolution".
  # For system_tests_bad_configuration we need to get "proton:io Name or service not known",
  # so blank /etc/nsswitch.conf to achieve that.
  set(BWRAP_ARGS bwrap --bind / / --bind /dev/zero /etc/nsswitch.conf --unshare-net --dev /dev --die-with-parent --)
else()
  set(BWRAP_ARGS "")
endif()
set(TEST_WRAP ${BWRAP_ARGS} ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/run.py)

add_test(unit_tests_size_10000 ${TEST_WRAP} unit_tests_size 10000)
add_test(unit_tests_size_512   ${TEST_WRAP} unit_tests_size 512)
add_test(unit_tests_size_10    ${TEST_WRAP} unit_tests_size 10)
add_test(unit_tests_size_7     ${TEST_WRAP} unit_tests_size 7)
add_test(unit_tests_size_5     ${TEST_WRAP} unit_tests_size 5)
add_test(unit_tests_size_3     ${TEST_WRAP} unit_tests_size 3)
add_test(unit_tests_size_2     ${TEST_WRAP} unit_tests_size 2)
add_test(unit_tests_size_1     ${TEST_WRAP} unit_tests_size 1)
add_test(unit_tests            ${TEST_WRAP} unit_tests ${CMAKE_CURRENT_SOURCE_DIR}/threads4.conf)

# Unit test python modules
add_test(router_engine_test    ${TEST_WRAP} ${PYTHON_TEST_COMMAND} -v router_engine_test)
add_test(management_test       ${TEST_WRAP} ${PYTHON_TEST_COMMAND} -v management)
add_test(router_policy_test    ${TEST_WRAP} ${PYTHON_TEST_COMMAND} -v router_policy_test)
add_test(test_command    ${TEST_WRAP} ${PYTHON_TEST_COMMAND} -v test_command)

if(USE_LIBWEBSOCKETS)
  set(SYSTEM_TESTS_HTTP system_tests_http)
endif()

if(CONSOLE_INSTALL)
  set(CONSOLE_TEST system_tests_console)
endif(CONSOLE_INSTALL)

# System test python modules
foreach(py_test_module
#   system_tests_broker
    system_tests_link_routes
    system_tests_link_routes_add_external_prefix
    system_tests_link_route_credit
    system_tests_autolinks
    system_tests_drain
    system_tests_management
    system_tests_one_router
    system_tests_handle_failover
    system_tests_default_distribution
    system_tests_deprecated
    system_tests_policy
    system_tests_policy_oversize_basic
    system_tests_policy_oversize_compound
    system_tests_protocol_family
    system_tests_protocol_settings
    system_tests_qdmanage
    system_tests_qdstat
    system_tests_sasl_plain
    system_tests_user_id
    system_tests_user_id_proxy
    system_tests_two_routers
    system_tests_interior_sync_up
    system_tests_distribution
    system_tests_multi_tenancy
    system_tests_multi_tenancy_policy
    system_tests_dynamic_terminus
    system_tests_log_message_components
    system_tests_failover_list
    system_tests_auth_service_plugin
    system_tests_authz_service_plugin
    system_tests_delivery_abort
    system_tests_topology
    system_tests_topology_disposition
    system_tests_topology_addition
    system_tests_disallow_link_resumable_link_route
    system_tests_delivery_counts
    system_tests_exchange_bindings
    system_tests_cmdline_parsing
    system_tests_bad_configuration
    system_tests_ssl
    system_tests_edge_router
    system_tests_connector_status
    system_tests_core_endpoint
    ${SYSTEM_TESTS_HTTP}
    ${CONSOLE_TEST}
    system_tests_priority
    system_tests_core_client
    system_tests_address_lookup
    system_tests_multi_phase
    system_tests_multicast
    system_tests_fallback_dest
    system_tests_router_mesh
    system_tests_stuck_deliveries
    system_tests_log_level_update
    system_tests_routing_protocol
    system_tests_open_properties
    )

  add_test(${py_test_module} ${TEST_WRAP} ${PYTHON_TEST_COMMAND} -v ${py_test_module})
  list(APPEND SYSTEM_TEST_FILES ${CMAKE_CURRENT_SOURCE_DIR}/${py_test_module}.py)
endforeach()

# Use tox to run the flake8 python linter tool on all the python
# sources. Highly recommended if you're hacking the python code.
# Unfortunately this can only be done for versions of tox >= 1.9.0
# since that version allows us to skip the install step (we have
# no modules thus no setup.py)
#
find_program(TOX_EXE "tox")
if (TOX_EXE)
  # the "skip_install" feature showed up in version 1.9.0 tox,
  # which is used by our tox.ini script:
  set(TOX_MIN_VERSION 1.9.0)
  execute_process(COMMAND "${TOX_EXE}" "--version"
                  RESULT_VARIABLE TOX_STATUS
                  OUTPUT_VARIABLE TOX_VERSION)
  if (TOX_STATUS EQUAL 0)
    string(STRIP "${TOX_VERSION}" TOX_VERSION)
    string(REGEX MATCH "^[0-9]+[.][0-9]+[.][0-9]+" TOX_VERSION "${TOX_VERSION}")
    if (TOX_VERSION STRLESS TOX_MIN_VERSION)
      message( STATUS "tox version ${TOX_VERSION} < required version ${TOX_MIN_VERSION} - skipping python-checker")
    else (TOX_VERSION STRLESS TOX_MIN_VERSION)
      configure_file(${CMAKE_CURRENT_SOURCE_DIR}/tox.ini.in ${CMAKE_CURRENT_BINARY_DIR}/tox.ini)
      add_test (NAME python-checker
                COMMAND ${TOX_EXE}
                WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
      # Once we move to tox >= v1.7.2 we can remove the next
      # line.  See tox.ini file for details
      set_tests_properties(python-checker
                           PROPERTIES
                           PASS_REGULAR_EXPRESSION "commands succeeded"
                           FAIL_REGULAR_EXPRESSION "commands failed")
    endif (TOX_VERSION STRLESS TOX_MIN_VERSION)
  else (TOX_STATUS EQUAL 0)
    message(STATUS "Cannot determine tox version - skipping python-checker")
  endif (TOX_STATUS EQUAL 0)

else (TOX_EXE)
  message(STATUS "Could NOT find 'tox' tool - unable to validate python code")
endif ()





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
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/policy-4/management-access.json  DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/policy-4/)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/authservice.py  DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

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

if(NOT (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "4.8.5"))
  add_subdirectory(c_unittests)
endif()

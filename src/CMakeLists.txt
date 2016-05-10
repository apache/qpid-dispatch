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

# Generate code from the management schema.
set(GENERATED_SOURCES
  schema_enum.h
  schema_enum.c
  )

set(GENERATOR_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/schema_c.py
  ${CMAKE_SOURCE_DIR}/python/qpid_dispatch/management/qdrouter.json)
file (GLOB GENERATOR_SRC ${CMAKE_SOURCE_DIR}/python/qpid_dispatch_internal/management/*.py)
set(GENERATOR_DEPENDS ${GENERATOR_DEPENDS} ${GENERATOR_SRC})
file (GLOB GENERATOR_SRC ${CMAKE_SOURCE_DIR}/python/qpid_dispatch/management/*.py)
set(GENERATOR_DEPENDS ${GENERATOR_DEPENDS} ${GENERATOR_SRC})

add_custom_command (
  OUTPUT ${GENERATED_SOURCES}
  COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_BINARY_DIR}/tests/run.py -s ${CMAKE_CURRENT_SOURCE_DIR}/schema_c.py
  DEPENDS ${GENERATOR_DEPENDS})

include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}
  )

# Build the qpid-dispatch library.
set(qpid_dispatch_SOURCES
  amqp.c
  bitmask.c
  buffer.c
  error.c
  compose.c
  connection_manager.c
  container.c
  dispatch.c
  entity.c
  entity_cache.c
  hash.c
  iovec.c
  iterator.c
  log.c
  message.c
  parse.c
  policy.c
  posix/driver.c
  posix/threading.c
  python_embedded.c
  router_agent.c
  router_config.c
  router_core/agent.c
  router_core/agent_address.c
  router_core/agent_config_address.c
  router_core/agent_config_auto_link.c
  router_core/agent_config_link_route.c
  router_core/agent_link.c
  router_core/connections.c
  router_core/error.c
  router_core/forwarder.c
  router_core/route_control.c
  router_core/router_core.c
  router_core/router_core_thread.c
  router_core/route_tables.c
  router_core/management_agent.c
  router_core/terminus.c
  router_core/transfer.c
  router_node.c
  router_pynode.c
  schema_enum.c
  server.c
  timer.c
  trace_mask.c
  )

if(USE_MEMORY_POOL)
  list(APPEND qpid_dispatch_SOURCES alloc_pool.c)
endif()

set_property(
  SOURCE python_embedded.c router_pynode.c
  PROPERTY COMPILE_FLAGS -Wno-strict-aliasing
  )

add_library(qpid-dispatch SHARED ${qpid_dispatch_SOURCES})
target_link_libraries(qpid-dispatch ${Proton_LIBRARIES} ${pthread_lib} ${rt_lib} ${dl_lib} ${PYTHON_LIBRARIES})
set_target_properties(qpid-dispatch PROPERTIES
  LINK_FLAGS "${CATCH_UNDEFINED}"
  )
install(TARGETS qpid-dispatch
  LIBRARY DESTINATION ${QPID_DISPATCH_HOME})

set (QPID_DISPATCH_LIB "libqpid-dispatch.so")

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h)

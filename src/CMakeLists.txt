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
  
if(NOT LIBWEBSOCKETS_FOUND)
include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}
  )
else()
include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}
  ${LIBWEBSOCKETS_INCLUDE_DIRS}
  )
endif()

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
  failoverlist.c
  hash.c
  iovec.c
  iterator.c
  log.c
  message.c
  parse.c
  parse_tree.c
  policy.c
  proton_utils.c
  remote_sasl.c
  posix/threading.c
  python_embedded.c
  router_agent.c
  router_config.c
  router_core/agent.c
  router_core/agent_address.c
  router_core/agent_config_address.c
  router_core/agent_config_auto_link.c
  router_core/agent_connection.c
  router_core/agent_config_link_route.c
  router_core/agent_link.c
  router_core/agent_router.c
  router_core/connections.c
  router_core/error.c
  router_core/exchange_bindings.c
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
  python_utils.c
  )

# USE_LIBWEBSOCKETS is true only if LIBWEBSOCKETS_FOUND
if(USE_LIBWEBSOCKETS)
  set(qpid_dispatch_SOURCES ${qpid_dispatch_SOURCES} http-libwebsockets.c)
else(USE_LIBWEBSOCKETS)
  set(qpid_dispatch_SOURCES ${qpid_dispatch_SOURCES} http-none.c)
endif(USE_LIBWEBSOCKETS)

if(USE_MEMORY_POOL)
  list(APPEND qpid_dispatch_SOURCES alloc_pool.c)
endif()

# strict aliasing optimization is only available in GCC
if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
    set_property(
      SOURCE python_embedded.c router_pynode.c
      PROPERTY COMPILE_FLAGS -Wno-strict-aliasing
    )
endif (CMAKE_C_COMPILER_ID STREQUAL "GNU")

add_library(qpid-dispatch SHARED ${qpid_dispatch_SOURCES})
if(NOT LIBWEBSOCKETS_FOUND)
target_link_libraries(qpid-dispatch ${Proton_Core_LIBRARIES} ${Proton_Proactor_LIBRARIES} ${pthread_lib} ${rt_lib} ${dl_lib} ${PYTHON_LIBRARIES})
else()
target_link_libraries(qpid-dispatch ${Proton_Core_LIBRARIES} ${Proton_Proactor_LIBRARIES} ${pthread_lib} ${rt_lib} ${dl_lib} ${PYTHON_LIBRARIES} ${LIBWEBSOCKETS_LIBRARIES})
endif()
set_target_properties(qpid-dispatch PROPERTIES
  LINK_FLAGS "${CATCH_UNDEFINED}"
  )
install(TARGETS qpid-dispatch
  LIBRARY DESTINATION ${QPID_DISPATCH_HOME})

set (QPID_DISPATCH_LIB "libqpid-dispatch.so")

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h)

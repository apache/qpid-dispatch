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

project(qpid-dispatch C)

cmake_minimum_required(VERSION 2.6)
include(CheckLibraryExists)
include(CheckSymbolExists)
include(CheckFunctionExists)
include(CheckIncludeFiles)
include(FindPythonInterp)
include(FindPythonLibs)

enable_testing()
include (CTest)

if (NOT PYTHONLIBS_FOUND)
     message(FATAL_ERROR "Python Development Libraries are needed.")
endif (NOT PYTHONLIBS_FOUND)

set (SO_VERSION_MAJOR 0)
set (SO_VERSION_MINOR 1)
set (SO_VERSION "${SO_VERSION_MAJOR}.${SO_VERSION_MINOR}")

if (NOT DEFINED LIB_SUFFIX)
    get_property(LIB64 GLOBAL PROPERTY FIND_LIBRARY_USE_LIB64_PATHS)
    if ("${LIB64}" STREQUAL "TRUE" AND ${CMAKE_SIZEOF_VOID_P} STREQUAL "8")
        set(LIB_SUFFIX 64)
    else()
        set(LIB_SUFFIX "")
    endif()
endif()

set(INCLUDE_INSTALL_DIR include CACHE PATH "Include file directory")
set(QPID_DISPATCH_HOME "lib/qpid-dispatch" CACHE PATH "Private Dispatch library directory")
set(LIB_INSTALL_DIR "lib${LIB_SUFFIX}" CACHE PATH "Library object file directory")
set(SHARE_INSTALL_DIR share CACHE PATH "Shared read only data directory")
set(DOC_INSTALL_DIR ${SHARE_INSTALL_DIR}/doc CACHE PATH "Shared read-only data directory")
set(MAN_INSTALL_DIR share/man CACHE PATH "Manpage directory")
set(QPID_DISPATCH_HOME_INSTALLED ${CMAKE_INSTALL_PREFIX}/${QPID_DISPATCH_HOME})

# define the configuration directory based on whether or not the install prefix is defined
if(NOT DEFINED SYSCONF_INSTALL_DIR)
    if(CMAKE_INSTALL_PREFIX STREQUAL "/usr")
        set(SYSCONF_INSTALL_DIR "/etc")
    else()
        set(SYSCONF_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/etc")
    endif()
endif()

set(QPID_DISPATCH_CONFDIR ${SYSCONF_INSTALL_DIR}/qpid-dispatch)

##
## Find dependencies
##
find_library(proton_lib qpid-proton)
find_library(pthread_lib pthread)
find_library(rt_lib rt)
find_path(proton_include proton/driver.h)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}
    ${proton_include}
    ${PYTHON_INCLUDE_PATH}
    )

if (NOT COMMAND add_compile_options)
  macro (add_compile_options option)
    SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${option}")
  endmacro (add_compile_options)
endif (NOT COMMAND add_compile_options)

add_compile_options(-pthread)
add_compile_options(-Wall)
add_compile_options(-Werror)
add_compile_options(-std=gnu99)
set(CATCH_UNDEFINED "-Wl,--no-undefined")

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/src/conditionals.h.in
               ${CMAKE_CURRENT_BINARY_DIR}/conditionals.h)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/tools/qdstat.in
               ${CMAKE_CURRENT_BINARY_DIR}/tools/qdstat)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/tools/qdtest.in
               ${CMAKE_CURRENT_BINARY_DIR}/tools/qdtest)

##
## Build the Multi-Threaded Server Library
##
set(server_SOURCES
    src/agent.c
    src/alloc.c
    src/amqp.c
    src/bitmask.c
    src/buffer.c
    src/compose.c
    src/config.c
    src/connection_manager.c
    src/container.c
    src/dispatch.c
    src/hash.c
    src/iovec.c
    src/iterator.c
    src/log.c
    src/message.c
    src/parse.c
    src/posix/threading.c
    src/python_embedded.c
    src/router_agent.c
    src/router_config.c
    src/router_node.c
    src/router_pynode.c
    src/server.c
    src/timer.c
    src/waypoint.c
    )

set_property(SOURCE src/python_embedded.c src/router_pynode.c
    PROPERTY COMPILE_FLAGS -Wno-strict-aliasing
    )

add_library(qpid-dispatch SHARED ${server_SOURCES})
target_link_libraries(qpid-dispatch ${proton_lib} ${pthread_lib} ${rt_lib} ${PYTHON_LIBRARIES})
set_target_properties(qpid-dispatch PROPERTIES
                      VERSION "${SO_VERSION}"
                      SOVERSION "${SO_VERSION_MAJOR}"
                      LINK_FLAGS "${CATCH_UNDEFINED}"
                      )
install(TARGETS qpid-dispatch
        LIBRARY DESTINATION ${LIB_INSTALL_DIR})
file(GLOB headers "include/qpid/dispatch/*.h")
install(FILES ${headers} DESTINATION ${INCLUDE_INSTALL_DIR}/qpid/dispatch)
install(FILES include/qpid/dispatch.h DESTINATION ${INCLUDE_INSTALL_DIR}/qpid)
install(FILES etc/qdrouterd.conf DESTINATION ${SYSCONF_INSTALL_DIR}/qpid-dispatch)

##
## Python modules installation
##
set(TOOLS_EXECUTABLES
    ${CMAKE_CURRENT_BINARY_DIR}/tools/qdstat
    ${CMAKE_CURRENT_BINARY_DIR}/tools/qdtest
)

set(DOC_FILES
    LICENSE
    README
    TODO
)

install(DIRECTORY python/qpid_dispatch_internal
        DESTINATION ${QPID_DISPATCH_HOME}/python)

install(PROGRAMS ${TOOLS_EXECUTABLES}
        DESTINATION bin)

install(FILES ${DOC_FILES}
        DESTINATION ${DOC_INSTALL_DIR}/qpid-dispatch)

##
## Build Tests
##
add_subdirectory(doc)
add_subdirectory(router)
add_subdirectory(tests)

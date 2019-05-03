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


set(DEFAULT_CONFIG_PATH "${QPID_DISPATCH_CONFDIR}/qdrouterd.conf" CACHE STRING "Default config file path")
set(DEFAULT_DISPATCH_PYTHON_DIR ${QPID_DISPATCH_PYTHON_DIR})

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/src/config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h)

include_directories(${CMAKE_CURRENT_BINARY_DIR})

##
## Build the router application
##
set(router_SOURCES
    src/main.c
    )

SET(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${QPID_DISPATCH_HOME}")

add_executable(qdrouterd ${router_SOURCES})
target_link_libraries(qdrouterd qpid-dispatch)

install(TARGETS qdrouterd RUNTIME DESTINATION sbin)

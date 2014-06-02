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

configure_file(qdrouterd.8.in ${CMAKE_CURRENT_BINARY_DIR}/qdrouterd.8)
configure_file(qdrouterd.conf.5.in ${CMAKE_CURRENT_BINARY_DIR}/qdrouterd.conf.5)
configure_file(qdstat.8.in ${CMAKE_CURRENT_BINARY_DIR}/qdstat.8)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/qdrouterd.8 DESTINATION ${MAN_INSTALL_DIR}/man8)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/qdrouterd.conf.5 DESTINATION ${MAN_INSTALL_DIR}/man5)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/qdstat.8 DESTINATION ${MAN_INSTALL_DIR}/man8)

# Generate a man page from the new qdrouter.json schema. This will eventually replace
# the hand-written man page generated from qdrouterd.conf.5.in

file (GLOB_RECURSE MAN_PAGE_GENERATOR
  ${CMAKE_CURRENT_SOURCE_DIR}/qdrouterd_man.py
  ${CMAKE_SOURCE_DIR}/python/qpid_router_internal/management/*.py
  ${CMAKE_SOURCE_DIR}/python/qpid_router_internal/management/qdrouterd.json)

set (QDROUTERD_MAN qdrouterdconf.conf.5.new)

add_custom_command (OUTPUT ${QDROUTERD_MAN}
    COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/qdrouterd_man.py ${QDROUTERD_MAN}
    DEPENDS ${MAN_PAGE_GENERATOR})

add_custom_target(man ALL DEPENDS ${QDROUTERD_MAN})

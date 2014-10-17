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
configure_file(qdstat.8.in ${CMAKE_CURRENT_BINARY_DIR}/qdstat.8)

# Generate a man page from the qdrouter.json schema.
set (QDROUTERD_CONF qdrouterd.conf.5)

file (GLOB_RECURSE QDROUTERD_CONF_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/qdrouterd_conf_man.py
  ${CMAKE_SOURCE_DIR}/python/qpid_router_internal/management/*.py
  ${CMAKE_SOURCE_DIR}/python/qpid_router/management/qdrouterd.json)

add_custom_command (OUTPUT ${QDROUTERD_CONF}
    COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_BINARY_DIR}/tests/run.py -s ${CMAKE_CURRENT_SOURCE_DIR}/qdrouterd_conf_man.py ${QDROUTERD_CONF}
    DEPENDS ${QDROUTERD_CONF_DEPENDS})

add_custom_target(man ALL DEPENDS ${QDROUTERD_CONF})

# Install man pages
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/qdrouterd.8 DESTINATION ${MAN_INSTALL_DIR}/man8)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${QDROUTERD_CONF} DESTINATION ${MAN_INSTALL_DIR}/man5)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/qdstat.8 DESTINATION ${MAN_INSTALL_DIR}/man8)

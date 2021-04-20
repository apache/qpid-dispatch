#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#

# Private python modules, not in standard python site dir.
install(DIRECTORY qpid_dispatch_internal DESTINATION ${QPID_DISPATCH_HOME}/python)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/setup.py.in
               ${CMAKE_CURRENT_BINARY_DIR}/setup.py)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/qpid_dispatch_site.py.in
               ${CMAKE_CURRENT_BINARY_DIR}/qpid_dispatch_site.py)

# Install script for public python modules
install(CODE "execute_process(COMMAND ${Python_EXECUTABLE} setup.py -v install --prefix=${CMAKE_INSTALL_PREFIX} WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})")

install(FILES
  ${CMAKE_CURRENT_SOURCE_DIR}/qpid_dispatch/management/qdrouter.json
  ${CMAKE_CURRENT_SOURCE_DIR}/qpid_dispatch/management/qdrouter.json.readme.txt
  DESTINATION ${QD_DOC_INSTALL_DIR}
  )

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

# Install script to install public python modules.
# Install with setup.py, add installed files to install manifest.

include(FindPythonInterp)

set(PYTHON_MANIFEST ${CMAKE_CURRENT_BINARY_DIR}/python/python_manifest.txt)

execute_process(COMMAND ${PYTHON_EXECUTABLE}
  setup.py -v install --prefix=${CMAKE_INSTALL_PREFIX} --record=${PYTHON_MANIFEST}
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/python)

file(READ ${PYTHON_MANIFEST} PYTHON_MANIFEST_FILES)
list(APPEND CMAKE_INSTALL_MANIFEST_FILES ${PYTHON_MANIFEST_FILES})

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

if(USE_DOXYGEN)

  # Create doxygen configuration files.
  function(configure_doxygen DOXYGEN_OUTPUT_DIRECTORY DOXYGEN_INPUT DOXYGEN_MORE)
    configure_file(
      ${CMAKE_CURRENT_SOURCE_DIR}/doxygen.in
      ${CMAKE_CURRENT_BINARY_DIR}/${DOXYGEN_OUTPUT_DIRECTORY}.doxygen)
  endfunction(configure_doxygen)
  configure_doxygen(user "${CMAKE_SOURCE_DIR}/include" "")
  configure_doxygen(dev "${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src"
    "ENABLED_SECTIONS=INTERNAL\nINTERNAL_DOCS=yes")

  # This is not ideal: if files are added/removed it won't be detected til we re-run cmake.
  file(GLOB_RECURSE API_SOURCES
    ${CMAKE_SOURCE_DIR}/include/*.h
    ${CMAKE_SOURCE_DIR}/src/*.h
    ${CMAKE_SOURCE_DIR}/src/*.c)

  add_custom_command (OUTPUT user
    COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/doxygen-wrapper.py ${DOXYGEN_EXECUTABLE} user.doxygen
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/doxygen.in ${API_SOURCES})

  add_custom_command (OUTPUT dev
    COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/doxygen-wrapper.py ${DOXYGEN_EXECUTABLE} dev.doxygen
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/doxygen.in ${API_SOURCES})

  add_custom_target(apidocs ALL DEPENDS user dev)

  install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/user/html/ DESTINATION ${QD_DOC_INSTALL_DIR}/api)

endif(USE_DOXYGEN)

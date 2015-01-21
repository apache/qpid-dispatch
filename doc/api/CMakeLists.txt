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

set(bin ${CMAKE_CURRENT_BINARY_DIR})

if(USE_DOXYGEN)

  # Create doxygen configuration files.
  function(configure_doxygen  HTML_OUTPUT INPUT MORE_CONFIG)
    if(USE_DOT)
      set(HAVE_DOT yes)
    else()
      set(HAVE_DOT no)
    endif()
    # Arguments and local variables are used to configure doxygen.in
    configure_file(
      ${CMAKE_CURRENT_SOURCE_DIR}/doxygen.in
      ${CMAKE_CURRENT_BINARY_DIR}/${HTML_OUTPUT}.doxygen)
  endfunction(configure_doxygen)

  configure_doxygen(api "${CMAKE_SOURCE_DIR}/include" "")
  configure_doxygen(api_dev "${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src"
    "ENABLED_SECTIONS=INTERNAL\nINTERNAL_DOCS=yes\nEXTRACT_ALL=yes")

  # Dependencies: calculated at configuration time so we won't detect
  # new/removed files till re-configuration. Usually adding/removing sources
  # will force a re-configuration so by deleting the output to force a re-build
  # after re-configuration we should catch most cases.
  file(GLOB_RECURSE API_SOURCES
    ${CMAKE_SOURCE_DIR}/include/*.h
    ${CMAKE_SOURCE_DIR}/src/*.h
    ${CMAKE_SOURCE_DIR}/src/*.c)
  file(REMOVE_RECURSE ${bin}/api ${bin}/api_dev) # Force rebuild after re-configuration.

  add_custom_command (OUTPUT api api_dev
    COMMAND ${DOXYGEN_EXECUTABLE} api.doxygen
    COMMAND ${DOXYGEN_EXECUTABLE} api_dev.doxygen
    COMMAND ${CMAKE_COMMAND} -E touch api api_dev
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/doxygen.in ${API_SOURCES})

  add_custom_target(apidocs ALL DEPENDS api api_dev)

  install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/api DESTINATION ${QD_DOC_INSTALL_DIR})
  install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/api_dev DESTINATION ${QD_DOC_INSTALL_DIR})

endif(USE_DOXYGEN)

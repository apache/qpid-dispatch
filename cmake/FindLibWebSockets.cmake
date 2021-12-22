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

# Find libwebsockets include dirs and libraries.
#
# Sets the following variables:
#
#   LIBWEBSOCKETS_FOUND            - True if headers and requested libraries were found
#   LIBWEBSOCKETS_INCLUDE_DIRS     - LibWebSockets include directories
#   LIBWEBSOCKETS_LIBRARIES        - Link these to use libwebsockets
#   LIBWEBSOCKETS_VERSION_STRING   - The library version number
#
# This module reads hints about search locations from variables::
#   LIBWEBSOCKETS_LIBRARYDIR       - Preferred library directory e.g. <prefix>/lib
#   LIBWEBSOCKETS_ROOT             - Preferred installation prefix
#   CMAKE_INSTALL_PREFIX           - Install location for the current project.
#   LIBWEBSOCKETS_INCLUDEDIR       - Preferred include directory e.g. <prefix>/include

set(LIBWEBSOCKETS_FOUND FALSE)

find_library(LIBWEBSOCKETS_LIBRARIES
  NAMES websockets libwebsockets
  HINTS "${LIBWEBSOCKETS_LIBRARYDIR}" "${LIBWEBSOCKETS_ROOT}" "${CMAKE_INSTALL_PREFIX}"
  )

find_path(LIBWEBSOCKETS_INCLUDE_DIRS
  NAMES libwebsockets.h
  HINTS "${LIBWEBSOCKETS_INCLUDEDIR}" "${LIBWEBSOCKETS_ROOT}/include" "${CMAKE_INSTALL_PREFIX}/include"
  PATHS "/usr/include"
  )

# strips trailing version elaboration, e.g. #define LWS_LIBRARY_VERSION "4.1.6-git..."
if(LIBWEBSOCKETS_INCLUDE_DIRS AND EXISTS "${LIBWEBSOCKETS_INCLUDE_DIRS}/lws_config.h")
  file(STRINGS "${LIBWEBSOCKETS_INCLUDE_DIRS}/lws_config.h" lws_version_str
    REGEX "^#define[ \t]+LWS_LIBRARY_VERSION[ \t]+\"[^\"]+\"")
  string(REGEX REPLACE "^#define[ \t]+LWS_LIBRARY_VERSION[ \t]+\"([0-9.]+).*" "\\1"
    LIBWEBSOCKETS_VERSION_STRING "${lws_version_str}")
  unset(lws_version_str)
endif()

if (LIBWEBSOCKETS_VERSION_STRING AND LibWebSockets_FIND_VERSION AND (LIBWEBSOCKETS_VERSION_STRING VERSION_LESS LibWebSockets_FIND_VERSION))
  message(STATUS "Found libwebsockets version ${LIBWEBSOCKETS_VERSION_STRING} but least ${LibWebSockets_FIND_VERSION} is required. WebSocket support disabled.")
else()
  include(FindPackageHandleStandardArgs)
  find_package_handle_standard_args(
    LibWebSockets DEFAULT_MSG LIBWEBSOCKETS_VERSION_STRING LIBWEBSOCKETS_LIBRARIES LIBWEBSOCKETS_INCLUDE_DIRS)
endif()

if(NOT LIBWEBSOCKETS_FOUND)
  unset(LIBWEBSOCKETS_LIBRARIES)
  unset(LIBWEBSOCKETS_INCLUDE_DIRS)
  unset(LIBWEBSOCKETS_VERSION_STRING)
endif()

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
#   LIBWEBSOCKETS_LIBRARIES        - Link these to use libwebsockets.
#
# This module reads hints about search locations from variables::
#   LIBWEBSOCKETS_LIBRARYDIR       - Preferred library directory e.g. <prefix>/lib
#   LIBWEBSOCKETS_ROOT             - Preferred installation prefix
#   CMAKE_INSTALL_PREFIX           - Install location for the current project.
#   LIBWEBSOCKETS_INCLUDEDIR       - Preferred include directory e.g. <prefix>/include

find_library(LIBWEBSOCKETS_LIBRARIES
  NAMES websockets libwebsockets
  HINTS ${LIBWEBSOCKETS_LIBRARYDIR} ${LIBWEBSOCKETS_ROOT}  ${CMAKE_INSTALL_PREFIX}
  )

find_path(LIBWEBSOCKETS_INCLUDE_DIRS
  NAMES libwebsockets.h
  HINTS ${LIBWEBSOCKETS_INCLUDEDIR} ${LIBWEBSOCKETS_ROOT}/include ${CMAKE_INSTALL_PREFIX}/include
  PATHS /usr/include
  )

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(LIBWEBSOCKETS DEFAULT_MSG LIBWEBSOCKETS_LIBRARIES LIBWEBSOCKETS_INCLUDE_DIRS)

if(LIBWEBSOCKETS_FOUND)
  # For the moment we need a patched version of LibWebSockets:
  # https://github.com/alanconway/libwebsockets/tree/v2.1-stable-aconway-adopt-ssl
  # This function check verifies we have it.
  set(CMAKE_REQUIRED_INCLUDES ${LIBWEBSOCKETS_INCLUDE_DIRS})
  set(CMAKE_REQUIRED_LIBRARIES ${LIBWEBSOCKETS_LIBRARIES})
  check_function_exists(lws_adopt_socket_vhost LWS_ADOPT_SOCKET_VHOST_FOUND)
  if (NOT LWS_ADOPT_SOCKET_VHOST_FOUND)
    message("Cannot use LibWebSockets, no function lws_adopt_socket_vhost")
    unset(LIBWEBSOCKETS_FOUND)
  endif()
endif()

if(NOT LIBWEBSOCKETS_FOUND)
  set(LIBWEBSOCKETS_LIBRARIES "")
  set(LIBWEBSOCKETS_INCLUDE_DIRS "")
endif()

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

# Sets LIBNGHTTP2_VERSION_STRING from nghttp2ver.h

find_library(NGHTTP2_LIBRARIES
  NAMES libnghttp2 nghttp2
)

find_path(NGHTTP2_INCLUDE_DIRS
  NAMES "nghttp2/nghttp2.h" "nghttp2/nghttp2ver.h"
  HINTS "${CMAKE_INSTALL_PREFIX}/include"
  PATHS "/usr/include"
)

if(NGHTTP2_INCLUDE_DIRS AND EXISTS "${NGHTTP2_INCLUDE_DIRS}/nghttp2/nghttp2ver.h")
  # Extract the version info from nghttp2ver.h and set it in LIBNGHTTP2_VERSION_STRING
  file(STRINGS "${NGHTTP2_INCLUDE_DIRS}/nghttp2/nghttp2ver.h" libnghttp2_version_str
    REGEX "^#define[ \t]+NGHTTP2_VERSION[ \t]+\"[^\"]+\"")
  string(REGEX REPLACE "^#define[ \t]+NGHTTP2_VERSION[ \t]+\"([^\"]+)\".*" "\\1"
    LIBNGHTTP2_VERSION_STRING "${libnghttp2_version_str}")
  unset(libnghttp2_version_str)
endif()

if (LIBNGHTTP2_VERSION_STRING AND libnghttp2_FIND_VERSION AND (LIBNGHTTP2_VERSION_STRING VERSION_LESS libnghttp2_FIND_VERSION))
  message(STATUS "Found libnghttp2 version ${LIBNGHTTP2_VERSION_STRING} but at least ${libnghttp2_FIND_VERSION} is required. http2 support is disabled")
else()
  include(FindPackageHandleStandardArgs)
  find_package_handle_standard_args(
    libnghttp2 DEFAULT_MSG LIBNGHTTP2_VERSION_STRING NGHTTP2_LIBRARIES NGHTTP2_INCLUDE_DIRS)
endif()

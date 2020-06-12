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

find_library(NGHTTP2_LIBRARIES
  NAMES libnghttp2 nghttp2
  REQUIRED
)

find_path(NGHTTP2_INCLUDE_DIRS
  NAMES nghttp2.h
  HINTS "${LIBWEBSOCKETS_INCLUDEDIR}" "${LIBWEBSOCKETS_ROOT}/include" "${CMAKE_INSTALL_PREFIX}/include"
  PATHS "/usr/include/nghttp2"
)

include(FindPackageHandleStandardArgs)
  find_package_handle_standard_args(
    libnghttp2 DEFAULT_MSG NGHTTP2_LIBRARIES NGHTTP2_INCLUDE_DIRS)




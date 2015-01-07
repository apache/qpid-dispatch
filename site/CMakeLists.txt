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

# Content for the qpid website.
# Copy ${CMAKE_CURRENT_BINARY_DIR}/input to to a checkout of the qpid site repository at:
#  https://svn.apache.org/repos/asf/qpid/site
# Then build and publish the site as per the README there.

set(QPID_SITE_DIR ${CMAKE_CURRENT_BINARY_DIR} CACHE string
  "Qpid site checkout directory for installing site documentation.")

set(input ${QPID_SITE_DIR}/input)
set(book ${CMAKE_BINARY_DIR}/doc/book)
set(release ${input}/releases/qpid-dispatch-${QPID_DISPATCH_VERSION})

add_custom_target(
  site
  COMMAND ${CMAKE_COMMAND} -E make_directory ${release}
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${book}/qpid-dispatch-book.html.in ${release}
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${book}/qpid-dispatch-book.pdf ${release}
  )

add_dependencies(site book man)

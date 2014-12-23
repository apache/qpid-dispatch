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

# The dipatch book is written in MarkDown, this generates a HTML  version.
# Requires pandoc, available on yum-based systems as pandoc.

find_program(PANDOC pandoc)

if (PANDOC)
  set(src ${CMAKE_CURRENT_SOURCE_DIR})
  set(BOOK ${src}/introduction.md ${src}/using.md ${src}/addressing.md ${src}/amqp-mapping.md)
  set(CMD pandoc -s -S --toc -f markdown)
  set(BASE ${CMAKE_CURRENT_BINARY_DIR}/qpid-dispatch-book)
  set(HTML ${BASE}.html)
  set(PDF ${BASE}.pdf)

  add_custom_command (
      OUTPUT qpid-dispatch-book.html qpid-dispatch-book.pdf
      COMMAND ${CMD} -f markdown -o ${HTML} ${BOOK}
      COMMAND ${CMD} -f markdown -o ${PDF} ${BOOK}
      DEPENDS ${program} ${BOOK}
      )
  add_custom_target(book ALL DEPENDS ${HTML} ${BOOK})

  install(FILES ${HTML} ${PDF} DESTINATION ${QD_DOC_INSTALL_DIR})

endif(PANDOC)

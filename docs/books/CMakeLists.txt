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

if (POLICY CMP0009)
  # FILE GLOB_RECURSE calls should not follow symlinks by default.
  cmake_policy (SET CMP0009 NEW)
endif ()

find_program (ASCIIDOCTOR_EXE asciidoctor DOC "Generate books")

set (
  books
  ${CMAKE_CURRENT_BINARY_DIR}/user-guide/index.html
  )

if (ASCIIDOCTOR_EXE)
  file (GLOB_RECURSE adoc_files RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *.adoc)
  file (GLOB_RECURSE png_files RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *.png)

  foreach (file ${png_files})
    execute_process (
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
      ${CMAKE_CURRENT_SOURCE_DIR}/${file}
      ${CMAKE_CURRENT_BINARY_DIR}/user-guide/${file}
      )
  endforeach ()

  add_custom_command (
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/user-guide/index.html
    COMMAND ${ASCIIDOCTOR_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/user-guide/book.adoc -o user-guide/index.html
    DEPENDS ${adoc_files}
    )

  add_custom_target (books DEPENDS ${books})
  add_dependencies (docs books)
else ()
  message (STATUS "AsciiDoctor not found: Not generating books")
endif ()

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


# In recent versions of cmake the documentation is not built automatically, you need to
# run "make doc", so you can leave this ON. In older (< 2.8) versions, documentation
# is built by "make all" so you need to turn BUILD_DOCS OFF if you don't want that.
option(BUILD_DOCS "Generate documentation" ON)

set(src ${CMAKE_CURRENT_SOURCE_DIR})
set(bin ${CMAKE_CURRENT_BINARY_DIR})

if(BUILD_DOCS)

  if ("${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION}" VERSION_LESS "2.8")
    # OPTIONAL does not exist in install before 2.8 so always make docs and install
    add_custom_target(doc ALL)
  else()
    set (OPTIONAL OPTIONAL)
    add_custom_target(doc)
  endif()
  macro(install_doc)
    install(${ARGN} ${OPTIONAL})
  endmacro()

  find_package(Doxygen)
  find_program(PANDOC pandoc)
  find_program(PDFLATEX pdflatex)

  # Create an option to enable/disable a doc tool and check if the tool is present.
  # If tool is present, option defaults to ON else OFF.
  # If option is set ON by user and tool is not present, that is an error.
  # If tool is not present and option is not set on just issue a status message.
  #
  macro(doc_tool tool use var what)
    if(${var})
      option(${use} "Generate ${what} with ${tool}" ON)
    else(${var})
      option(${use} "Generate ${what} with ${tool}" OFF)
      if(${use})
        message(SEND_ERROR "${use} enabled but ${tool} not found.")
      else(${use})
        message(STATUS "${tool} not found, not generating ${what}.")
      endif(${use})
    endif(${var})
  endmacro()

  doc_tool(pandoc USE_PANDOC PANDOC "HTML and man page documentation")
  doc_tool(pdflatex USE_PDFLATEX PDFLATEX "PDF documentation (also requires pandoc)")
  doc_tool(doxygen USE_DOXYGEN DOXYGEN_FOUND "API documentation")
  doc_tool(dot USE_DOT DOT "diagrams in API documentation (also requires doxygen)")

  add_subdirectory(api)
  add_subdirectory(man)
  add_subdirectory(book)

  # Index for installed doc
  install_doc(FILES ${src}/index.md DESTINATION ${QD_DOC_INSTALL_DIR})
  if(USE_PANDOC)
    add_custom_command(
      OUTPUT ${bin}/index.html
      COMMAND ${PANDOC} --standalone -f markdown -t html -o ${bin}/index.html ${src}/index.md
      DEPENDS ${src}/index.md
      )
    add_custom_target(doc-index DEPENDS ${bin}/index.html)
    add_dependencies(doc doc-index)
    install_doc(FILES ${bin}/index.html DESTINATION ${QD_DOC_INSTALL_DIR})
  endif(USE_PANDOC)

endif(BUILD_DOCS)

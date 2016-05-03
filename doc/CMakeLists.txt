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

if(BUILD_DOCS)

  set(src ${CMAKE_CURRENT_SOURCE_DIR})
  set(bin ${CMAKE_CURRENT_BINARY_DIR})
  set(tools ${CMAKE_SOURCE_DIR}/tools)

  if ("${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION}" VERSION_LESS "2.8")
    # OPTIONAL does not exist in install before 2.8 so always make docs and install
    add_custom_target(doc ALL)
  else()
    set (OPTIONAL OPTIONAL)
    add_custom_target(doc)
    add_custom_target(docs DEPENDS doc) # some projects say "make doc" others "make docs" allow both.
  endif()
  macro(install_doc)
    install(${ARGN} ${OPTIONAL})
  endmacro()

  find_package(Doxygen)
  find_program(SPHINX_BUILD sphinx-build)

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

  doc_tool(sphinx-build USE_SPHINX SPHINX_BUILD "HTML and man page documentation")
  doc_tool(doxygen USE_DOXYGEN DOXYGEN_FOUND "API documentation")
  doc_tool(dot USE_DOT DOT "diagrams in API documentation (also requires doxygen)")

  # Copy configuration and rst sources to build directory to generate book.
  # Use configure_file to ensure we re-copy if the source changes.
  configure_file(${src}/conf.py.in ${bin}/conf.py)
  configure_file(${src}/man/qdmanage.rst.in ${bin}/man/qdmanage.rst)
  configure_file(${src}/man/qdrouterd.rst.in ${bin}/man/qdrouterd.rst)
  file(GLOB_RECURSE RST_SRC RELATIVE ${src} *.rst)
  foreach(file ${RST_SRC})
    configure_file(${src}/${file} ${bin}/${file} COPYONLY)
  endforeach()

  # Copy the images. Don't use configure_file since it truncates .png files at 1st ^Z character
  file(GLOB_RECURSE PNG_SRC RELATIVE ${src} *.png)
  foreach(file ${PNG_SRC})
    EXECUTE_PROCESS(COMMAND ${CMAKE_COMMAND} -E copy_if_different ${src}/${file} ${bin}/${file})
  endforeach()

  # Generate files from management schema
  set(schema, "${CMAKE_SOURCE_DIR}/python/qpid_router/management/qdrouterd.json")
  set(py_management ${CMAKE_SOURCE_DIR}/python/qpid_dispatch_internal/management)
  set(schema_gen_deps ${py_management}/schema_doc.py ${py_management}/schema.py ${schema})

  macro(schema_gen output script)
    add_custom_command(
      OUTPUT ${output}
      COMMAND ${RUN} -s ${script} 1> ${output}
      DEPENDS ${script} ${schema_gen_deps}
      )
    list(APPEND SCHEMA_GEN ${output})
  endmacro()

  schema_gen(${bin}/book/schema.rst ${src}/book/schema_rst.py)
  schema_gen(${bin}/man/qdrouterd.conf.rst ${src}/man/qdrouterd_conf_man.py)

  add_custom_target(doc-schema DEPENDS ${SCHEMA_GEN})
  add_dependencies(doc doc-schema)

  # Generate rst text from --help output for man pages
  macro(help2rst program path)
    set(help ${bin}/man/${program}_help.rst)
    list(APPEND HELP_GEN ${help})
    add_custom_command (
      OUTPUT ${help}
      COMMAND ${RUN} -s ${src}/man/help2rst.py ${path}/${program} --help > ${help}
      DEPENDS ${path}/${program} ${schema_doc} ${src}/man/help2rst.py
      )
  endmacro()

  help2rst(qdrouterd ${CMAKE_BINARY_DIR}/router)
  help2rst(qdstat ${tools})
  help2rst(qdmanage ${tools})

  # Run sphinx to generate HTML and man pages
  if(USE_SPHINX)
    foreach(manpage qdmanage.8 qdrouterd.8 qdrouterd.conf.5 qdstat.8)
      list(APPEND MAN_PAGES ${bin}/man/${manpage})
      string(REGEX MATCH "[0-9]*$" section  ${manpage})
      install_doc(FILES ${bin}/man/${manpage}
        DESTINATION ${CMAKE_INSTALL_PREFIX}/${MAN_INSTALL_DIR}/man${section})
    endforeach()

    set(SPHINX_OUTPUT ${bin}/html/index.html ${bin}/singlehtml/book/book.html ${MAN_PAGES})

    set(html_raw_options -D html_theme=bare -D html_add_permalinks=".")
    add_custom_command(
      OUTPUT ${SPHINX_OUTPUT}
      # html/ contains plain HTML suitable for embedding in the Qpid website.
      COMMAND ${SPHINX_BUILD} -d ${bin}/doctrees -b html ${html_raw_options} ${bin} ${bin}/html
      # dochtml/ is a self-contained site with searching, navigation etc. installed with the docs.
      COMMAND ${SPHINX_BUILD} -d ${bin}/doctrees -b html ${bin} ${bin}/dochtml
      # man/ contains Unix man pages.
      COMMAND ${SPHINX_BUILD} -d ${bin}/doctrees -b man  ${bin} ${bin}/man
      # singlehtml is a single HTML page
      COMMAND ${SPHINX_BUILD} -d ${bin}/doctrees -b singlehtml ${bin} ${bin}/singlehtml
      DEPENDS ${RST_SRC} ${HELP_GEN} ${SCHEMA_GEN}
      )
    add_custom_target(doc-sphinx DEPENDS ${SPHINX_OUTPUT})
    add_dependencies(doc doc-sphinx)

    install_doc(DIRECTORY ${bin}/dochtml/  DESTINATION ${QD_DOC_INSTALL_DIR}/html)

    set_directory_properties(PROPERTIES ADDITIONAL_MAKE_CLEAN_FILES
      ${bin}/html
      ${bin}/singlehtml
      ${bin}/dochtml)
  endif(USE_SPHINX)

  # Install rst documentation as baseline in case we have no generator
  install(DIRECTORY ${bin}/ DESTINATION ${QD_DOC_INSTALL_DIR}/rst ${OPTIONAL} FILES_MATCHING PATTERN "*.rst")


endif(BUILD_DOCS)

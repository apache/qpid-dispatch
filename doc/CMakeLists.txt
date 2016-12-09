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

# In old cmake (< 2.8) documentation is built by "make all" so set BUILD_DOC OFF
# if you don't want that. For new cmake it is only built by "make doc"
option(BUILD_DOCS "Generate documentation with 'make doc'" ON)
option(DOC_VERBOSE "Verbose output from doc tools for debugging" OFF)
option(DOC_XMLLINT "Use xmllint to verify XML output" OFF)

if(BUILD_DOCS)
  if ("${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION}" VERSION_LESS "2.8")
    # OPTIONAL does not exist in install before 2.8 so always make and install docs
    set(DOC_TARGET ALL)
    macro(install_doc)
      install(${ARGN})
    endmacro()
  else()
    macro(install_doc)
      install(${ARGN} OPTIONAL)
    endmacro()
  endif()

  # Popular locations
  set(src ${CMAKE_CURRENT_SOURCE_DIR})
  set(bin ${CMAKE_CURRENT_BINARY_DIR})
  set(tools ${CMAKE_SOURCE_DIR}/tools)
  set(schema ../python/qpid_dispatch/management/qdrouter.json)
  set(py_management ../python/qpid_dispatch_internal/management)
  set(schema_depends ${schema} ${py_management}/schema_doc.py ${py_management}/schema.py)

  # Flags for doc tools, based on cmake options
  set(ADOC_FLAGS "--conf-file=${bin}/asciidoc.conf")
  set(A2X_FLAGS "--asciidoc-opts=--conf-file=${bin}/asciidoc.conf" -D${bin})
  if (DOC_VERBOSE)
    set(ADOC_FLAGS ${ADOC_FLAGS} -v)
    set(A2X_FLAGS ${A2X_FLAGS} -v)
  endif()
  if (NOT DOC_XMLLINT)
    set(A2X_FLAGS ${A2X_FLAGS} --no-xmllint)
  endif()

  # Generate asciidoc fragments from management schema to incorporate in text
  macro(schema_gen script output)
    add_custom_command(
      OUTPUT ${output}
      COMMAND ${RUN} -s ${script} 1> ${output}
      DEPENDS ${script} ${schema_depends})
    list(APPEND GENERATED_TXT "${output}")
  endmacro()

  schema_gen(${src}/book/schema_txt.py ${bin}/book/schema.adoc)
  schema_gen(${src}/man/qdrouterd.conf.5.py ${bin}/man/qdrouterd.conf.5.adoc)

  # Generate asciidoc .adoc from --help output for man pages
  macro(help2txt program)
    get_filename_component(name ${program} NAME)
    set(output ${bin}/man/${name}_help.adoc)
    add_custom_command(
      OUTPUT ${output}
      COMMAND ${RUN} -s ${src}/man/help2txt.py ${program} --help 1> ${output}
      DEPENDS ${program} ${schema_depends} ${src}/man/help2txt.py)
    list(APPEND GENERATED_TXT "${output}")
  endmacro()

  help2txt(${CMAKE_BINARY_DIR}/router/qdrouterd)
  help2txt(${tools}/qdmanage)
  help2txt(${tools}/qdstat)

  add_custom_target(doc_gen ALL DEPENDS ${GENERATED_TXT})
  install(DIRECTORY ${src}/book/ DESTINATION ${QD_DOC_INSTALL_DIR})
  
  install(FILES ${bin}/book/schema.adoc DESTINATION ${QD_DOC_INSTALL_DIR})
  install(FILES ${bin}/man/qdrouterd.conf.5.adoc DESTINATION ${QD_DOC_INSTALL_DIR})
  
  EXECUTE_PROCESS(COMMAND ${CMAKE_COMMAND} -E copy_directory ${src}/book/ ${bin}/book)
  EXECUTE_PROCESS(COMMAND ${CMAKE_COMMAND} -E copy_directory ${src}/man/ ${bin}/man)  

  find_program(ASCIIDOC_EXE asciidoc DOC "Generate HTML documentation")
  if (ASCIIDOC_EXE)
    configure_file(${src}/asciidoc.conf.in ${bin}/asciidoc.conf)
    configure_file(${src}/index.adoc ${bin}/index.adoc)

    # Copy the book dir images for HTML viewing in the build directory.
    # Don't use configure_file since it truncates .png files at 1st ^Z character
    
    # Copy the images. Don't use configure_file since it truncates .png files at 1st ^Z character
    file(GLOB_RECURSE PNG_SRC RELATIVE ${src} *.png)
    foreach(file ${PNG_SRC})
        get_filename_component(name ${file} NAME)
        EXECUTE_PROCESS(COMMAND ${CMAKE_COMMAND} -E copy_if_different ${src}/${file} ${bin}/_images/${name})
    endforeach()    

    # Generate HTML
    file(GLOB_RECURSE ADOC_SRC *.adoc)
    foreach(source index book/book man/qdmanage.8 man/qdrouterd.8 man/qdstat.8 man/qdrouterd.conf.5)
      get_filename_component(name ${source} NAME)
      get_filename_component(dir ${source} PATH)
      string(FIND ${name} "." dot)
      if(dot GREATER 0)
        string(LENGTH ${name} length)
        MATH(EXPR desired_length "${length}-2")
        string(SUBSTRING ${name} "0" ${desired_length} name)
      endif(dot GREATER 0)
      set(output ${bin}/${dir}/${name}.html)
      add_custom_command(
        OUTPUT ${output} ${output}.in
        # Pretty .html for direct viewing.
        COMMAND ${ASCIIDOC_EXE} ${ADOC_FLAGS} -o ${output} ${source}.adoc
        # Raw .html.in body for inclusion in Qpid website.
        COMMAND  ${ASCIIDOC_EXE} ${ADOC_FLAGS} -s -o ${output}.in ${source}.adoc
        DEPENDS ${source}.adoc ${GENERATED_TXT} ${ADOC_SRC} ${bin}/asciidoc.conf
        WORKING_DIRECTORY ${bin}
        )
      list(APPEND DOC_DEPENDS ${output})
      install_doc(FILES ${output} DESTINATION ${QD_DOC_INSTALL_DIR})
    endforeach()

    find_program(A2X_EXE a2x DOC DOC "Generate Unix man pages")
    if (A2X_EXE)
      # Generate man pages.
      foreach(source ${bin}/man/qdmanage.8 ${bin}/man/qdrouterd.8 ${bin}/man/qdstat.8 ${bin}/man/qdrouterd.conf.5)
        get_filename_component(name ${source} NAME)
        get_filename_component(dir ${source} PATH)
        string(REGEX REPLACE ".*\\.([0-9])$" "\\1" section ${source}) # Man section number
        set(output ${bin}/${name})
        add_custom_command(
          OUTPUT ${output}
          COMMAND ${A2X_EXE} ${A2X_FLAGS} -f manpage -D ${bin} ${source}.adoc
          DEPENDS ${source}.adoc ${GENERATED_TXT})
        list(APPEND DOC_DEPENDS ${output})
        install_doc(FILES ${output} DESTINATION ${CMAKE_INSTALL_PREFIX}/${MAN_INSTALL_DIR}/man${section})
      endforeach()

      # Generate PDF if we have the latex tool
      find_program(DBLATEX_EXE dblatex "Generate PDF documentation")
      if (DBLATEX_EXE)
        add_custom_command(
          OUTPUT ${bin}/book.pdf
          COMMAND ${A2X_EXE} ${A2X_FLAGS} -f pdf ${src}/book/book.adoc
          DEPENDS ${src}/book/book.adoc ${GENERATED_TXT})
        list(APPEND DOC_DEPENDS ${bin}/book.pdf)
        install_doc(FILES ${bin}/book.pdf DESTINATION ${QD_DOC_INSTALL_DIR})
      else(DBLATEX_EXE)
        message(STATUS "dblatex not found: not generating PDF")
      endif(DBLATEX_EXE)
    else(A2X_EXE)
      message(STATUS "a2x not found: not generating man pages or PDF")
    endif(A2X_EXE)
  else(ASCIIDOC_EXE)
    message(STATUS "asciidoc not found: not generating HTML, man pages or PDF")
  endif(ASCIIDOC_EXE)

  add_custom_target(doc ${DOC_TARGET} DEPENDS ${DOC_DEPENDS})
endif(BUILD_DOCS)

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

# Popular locations
set (src ${CMAKE_CURRENT_SOURCE_DIR})
set (bin ${CMAKE_CURRENT_BINARY_DIR})
set (tools ${CMAKE_SOURCE_DIR}/tools)
set (schema ../../python/qpid_dispatch/management/qdrouter.json)
set (py_management ../../python/qpid_dispatch_internal/management)
set (schema_deps ${schema} ${py_management}/schema_doc.py ${py_management}/schema.py)

# Flags for doc tools, based on cmake options
set (ADOC_FLAGS "--conf-file=${bin}/asciidoc.conf")
set (A2X_FLAGS "--asciidoc-opts=--conf-file=${bin}/asciidoc.conf" -D${bin})

if (DOC_VERBOSE)
  set (ADOC_FLAGS ${ADOC_FLAGS} -v)
  set (A2X_FLAGS ${A2X_FLAGS} -v)
endif ()

if (NOT DOC_XMLLINT)
  set (A2X_FLAGS ${A2X_FLAGS} --no-xmllint)
endif ()

# Generate asciidoc fragments from management schema to incorporate in text
macro (schema_gen script output)
  add_custom_command (
    OUTPUT ${output}
    COMMAND ${RUN} -s ${script} 1> ${output}
    DEPENDS ${script} ${schema_deps})
  list (APPEND generated_txt "${output}")
endmacro ()

schema_gen (${src}/qdrouterd.conf.5.py ${bin}/qdrouterd.conf.5.adoc)

# Generate asciidoc .adoc from --help output for man pages
macro (help2txt program)
  get_filename_component (name ${program} NAME)
  set (output ${bin}/${name}_help.adoc)
  add_custom_command (
    OUTPUT ${output}
    COMMAND ${RUN} -s ${src}/help2txt.py ${program} --help 1> ${output}
    DEPENDS ${program} ${schema_deps} ${src}/help2txt.py
    )
  list (APPEND generated_txt "${output}")
endmacro ()

help2txt (${CMAKE_BINARY_DIR}/router/qdrouterd)
help2txt (${tools}/qdmanage)
help2txt (${tools}/qdstat)

add_custom_target (doc_gen DEPENDS ${generated_txt})

find_program (ASCIIDOC_EXE asciidoc DOC "Generate HTML documentation")

if (ASCIIDOC_EXE)
  configure_file (${src}/asciidoc.conf.in ${bin}/asciidoc.conf)

  # Generate HTML
  file (GLOB_RECURSE adoc_files *.adoc)
  foreach (source ${src}/qdmanage.8 ${src}/qdrouterd.8 ${src}/qdstat.8 ${bin}/qdrouterd.conf.5)
    get_filename_component (name ${source} NAME)
    string(FIND ${name} "." dot)
    if (dot GREATER 0)
      string (LENGTH ${name} length)
      math (EXPR desired_length "${length}-2")
      string (SUBSTRING ${name} "0" ${desired_length} name)
    endif ()
    set (output ${bin}/${name}.html)
    add_custom_command (
      OUTPUT ${output} ${output}.in
      # Pretty .html for direct viewing.
      COMMAND ${ASCIIDOC_EXE} ${ADOC_FLAGS} -o ${output} ${source}.adoc
      # Raw .html.in body for inclusion in Qpid website.
      COMMAND ${ASCIIDOC_EXE} ${ADOC_FLAGS} -s -o ${output}.in ${source}.adoc
      DEPENDS ${source}.adoc ${generated_txt} ${adoc_files} ${bin}/asciidoc.conf
      WORKING_DIRECTORY ${bin}
      )
    list (APPEND docs_deps ${output})
  endforeach ()

  find_program(A2X_EXE a2x DOC DOC "Generate Unix man pages")
  if (A2X_EXE)
    # Generate man pages.
    foreach (source ${src}/qdmanage.8 ${src}/qdrouterd.8 ${src}/qdstat.8 ${bin}/qdrouterd.conf.5)
      get_filename_component (name ${source} NAME)
      string(REGEX REPLACE ".*\\.([0-9])$" "\\1" section ${source}) # Man section number
      set (output ${bin}/${name})
      add_custom_command (
        OUTPUT ${output}
        COMMAND ${A2X_EXE} ${A2X_FLAGS} -f manpage -D ${bin} ${source}.adoc
        DEPENDS ${source}.adoc ${generated_txt}
        )
      list (APPEND docs_deps ${output})
      install (FILES ${output} DESTINATION ${CMAKE_INSTALL_PREFIX}/${MAN_INSTALL_DIR}/man${section} OPTIONAL)
    endforeach ()
  else ()
    message (STATUS "a2x not found: not generating man pages or PDF")
  endif ()
else ()
  message (STATUS "asciidoc not found: not generating HTML, man pages or PDF")
endif ()

add_custom_target (man DEPENDS ${docs_deps} doc_gen)
add_dependencies (docs man)

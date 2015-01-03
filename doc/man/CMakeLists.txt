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

#
# Generate man pages with pandoc.
# Documentation source is in markdown, generate man pages.
#

find_program(PANDOC pandoc)

set(src ${CMAKE_CURRENT_SOURCE_DIR})
set(bin ${CMAKE_CURRENT_BINARY_DIR})
set(tools ${CMAKE_SOURCE_DIR}/tools)

# Configure man page sources and combine with --help options output from programs.
# Install markdown version of man pages in share/doc.
macro(help2md program section path)
  set(infile "${src}/${program}.${section}.noopt.md.in")
  set(manpage "${bin}/${program}.${section}")
  configure_file(${infile} "${manpage}.noopt.md")
  add_custom_command (
    OUTPUT ${manpage}.md
    COMMAND ${RUN} -s "${src}/help2md.py" "${manpage}.noopt.md" "${manpage}.md" "${path}/${program}" --help
    DEPENDS ${path}/${program} ${manpage}.noopt.md ${src}/help2md.py ${infile}
    )
  set(MANPAGES_MD ${MANPAGES_MD} ${manpage}.md)
endmacro()

# If pandoc is avaliable generate and install proper man files.
macro(manpage program section)
  set(manpage "${bin}/${program}.${section}")
  string(TIMESTAMP date "%Y-%m-%d")
  if(PANDOC)
    if(${section} EQUAL 8)
      set(header "System Manager's Manual")
    elseif(${section} EQUAL 5)
      set(header "File Formats Manual")
    else()
      set(header "")
    endif()
    add_custom_command (
      OUTPUT ${manpage}
      COMMAND ${PANDOC} --template ${src}/man.template.pd -s -V date="${date}" -V title="${program}" -V section="${section}" -V version="${QPID_DISPATCH_VERSION}" -V header="${header}" -f markdown -t man -o ${manpage} "${manpage}.md"
      DEPENDS "${manpage}.md" "${src}/man.template.pd"
      )
    set(MANPAGES ${MANPAGES} ${manpage})
    install(FILES ${manpage} DESTINATION "${MAN_INSTALL_DIR}/man${section}")
  else(PANDOC)
    message(WARNING "pandoc is not avaliable, cannot generate ${manpage}")
  endif(PANDOC)
endmacro()

# Shortcut to run both help2md and manpage
macro(help2man program section path)
  help2md(${program} ${section} ${path})
  manpage(${program} ${section})
endmacro()

# Man pages for executables.
help2man(qdrouterd 8 ${CMAKE_BINARY_DIR}/router)
help2man(qdstat 8 ${tools})
help2man(qdmanage 8 ${tools})

# Generate a man page from the qdrouter.json schema.
set(schema, "${CMAKE_SOURCE_DIR}/python/qpid_router/management/qdrouterd.json")
set(qdrouterd_conf_man "${bin}/qdrouterd.conf.5")
add_custom_command(
  OUTPUT ${qdrouterd_conf_man}.md
  COMMAND ${RUN} -s ${src}/qdrouterd_conf_man.py ${qdrouterd_conf_man}.md
  DEPENDS ${src}/qdrouterd_conf_man.py ${schema}
  )
set(MANPAGES_MD ${MANPAGES_MD} ${qdrouterd_conf_man}.md)
manpage(qdrouterd.conf 5)

# Generate the man page section of the book

add_custom_command(
  OUTPUT manpages.md
  COMMAND ${RUN} -s ${src}/man2book.py ${MANPAGES_MD} > manpages.md
  DEPENDS ${src}/man2book.py ${MANPAGES_MD}
  )

# Install all the markdown files
install(FILES ${MANPAGES_MD} DESTINATION ${QD_DOC_INSTALL_DIR})

# Target to build the generated files
add_custom_target(man ALL DEPENDS ${MANPAGES_MD} ${MANPAGES} manpages.md)


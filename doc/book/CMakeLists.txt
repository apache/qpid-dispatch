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

# The dipatch book is written in markdown, this generates a HTML version.
# Requires pandoc, available on yum-based systems as pandoc.

set(src ${CMAKE_CURRENT_SOURCE_DIR})
set(bin ${CMAKE_CURRENT_BINARY_DIR})

# Generate the schema chapter of the book.
set(schema, "${CMAKE_SOURCE_DIR}/python/qpid_router/management/qdrouterd.json")
add_custom_command(
  OUTPUT schema.md
  COMMAND ${RUN} -s ${src}/schema_md.py > schema.md
  DEPENDS ${src}/schema_md.py ${schema}
  )

set(BOOK ${src}/introduction.md ${src}/using.md ${src}/addressing.md ${src}/amqp-mapping.md ${bin}/schema.md ${CMAKE_BINARY_DIR}/doc/man/manpages.md)

# Install markdown documentation in the doc directory.
install(FILES ${BOOK} DESTINATION ${QD_DOC_INSTALL_DIR})

# Generate man pages and book if pandoc is installed
find_program(PANDOC pandoc)
if (PANDOC)
  set(PD_ARGS --standalone --toc --number-sections -f markdown -V "title=Qpid Dispatch Router Book")

  # Generate a book with the given suffix. Extra arguments are passed to pandoc
  macro(generate_book SUFFIX)
    set(OUTPUT ${bin}/qpid-dispatch-book${SUFFIX})
    add_custom_command(
      OUTPUT ${OUTPUT}
      COMMAND ${PANDOC} ${PD_ARGS} -o ${OUTPUT} ${ARGN} ${BOOK}
      DEPENDS man ${BOOK}
    )
    install(FILES ${OUTPUT} DESTINATION ${QD_DOC_INSTALL_DIR})
    set(BOOKS ${BOOKS} ${OUTPUT})
  endmacro()

  generate_book(.html)          # Complete HTML document.
  generate_book(.html.in --template ${src}/site-html.pd) # Headless fragment for Qpid website.

  find_program(PDFLATEX pdflatex)
  if (PDFLATEX)
    generate_book(.pdf)      # Complete PDF document.
  else(PDFLATEX)
    message("Not generating PDF format book, pdflatex not available.")
  endif(PDFLATEX)
else(PANDOC)
    message("Not generating HTML format book, pandoc not available.")
endif(PANDOC)


# Target to generate all chapters and books.
add_custom_target(book ALL DEPENDS ${BOOK} ${BOOKS})
add_dependencies(book man) # Note: adding man to book DEPENDS does not work on cmake 2.6.4

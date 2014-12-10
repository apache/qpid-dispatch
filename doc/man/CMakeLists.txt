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

set(src ${CMAKE_CURRENT_SOURCE_DIR})
set(bin ${CMAKE_CURRENT_BINARY_DIR})
set(tools ${CMAKE_SOURCE_DIR}/tools)

find_program(HELP2MAN help2man)

if(HELP2MAN)

  macro(help2man manpage h2mfile program)
    configure_file(${src}/${h2mfile}.in ${bin}/${h2mfile})
    string(REGEX MATCH "[0-9]*$" section ${manpage})
    add_custom_command (
      OUTPUT ${manpage}
      COMMAND ${RUN} -s ${src}/help2man.py ${HELP2MAN} -s ${section} -i ${bin}/${h2mfile} -N -o ${bin}/${manpage} ${program}
      DEPENDS ${program} ${bin}/${h2mfile}
      )
  endmacro()

else(HELP2MAN)

  message(WARNING "help2man is not avaliable, cannot update man pages")
  macro(help2man manpage h2mfile program)
    # Copy a pre-generated version of the man page.
    configure_file(${src}/${manpage}.in ${bin}/${manpage})
  endmacro()

endif(HELP2MAN)

configure_file(qdrouterd.8.in ${bin}/qdrouterd.8)
help2man(qdstat.8 qdstat.h2m ${tools}/qdstat)
help2man(qdmanage.8 qdmanage.h2m ${tools}/qdmanage)

# Generate a man page from the qdrouter.json schema.
set (QDROUTER_CONF_MAN qdrouterd.conf.5)

file (GLOB_RECURSE QDROUTER_CONF_MAN_DEPENDS
  ${src}/qdrouterd_conf_man.py
  ${CMAKE_SOURCE_DIR}/python/qpid_router_internal/management/*.py
  ${CMAKE_SOURCE_DIR}/python/qpid_router/management/qdrouterd.json)

add_custom_command (OUTPUT ${QDROUTER_CONF_MAN}
    COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_BINARY_DIR}/tests/run.py -s ${src}/qdrouterd_conf_man.py ${QDROUTER_CONF_MAN}
    DEPENDS ${QDROUTER_CONF_MAN_DEPENDS})

# Target to build the generated files
add_custom_target(man ALL DEPENDS ${QDROUTER_CONF_MAN} ${bin}/qdmanage.8 ${bin}/qdstat.8)
# Manual target to check if the generated files differ from the checked-in files
add_custom_target(check-man COMMAND ${RUN} -s ${src}/check_help2man.py ${src} ${bin} qdmanage.8 qdstat.8)

# Install man pages
install(FILES ${bin}/qdrouterd.8 DESTINATION ${MAN_INSTALL_DIR}/man8)
install(FILES ${bin}/${QDROUTER_CONF_MAN} DESTINATION ${MAN_INSTALL_DIR}/man5)
install(FILES ${bin}/qdstat.8 DESTINATION ${MAN_INSTALL_DIR}/man8)
install(FILES ${bin}/qdmanage.8 DESTINATION ${MAN_INSTALL_DIR}/man8)

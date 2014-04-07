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

configure_file(man/qdrouterd.8.in ${CMAKE_CURRENT_BINARY_DIR}/man/qdrouterd.8)
configure_file(man/qdrouterd.conf.5.in ${CMAKE_CURRENT_BINARY_DIR}/man/qdrouterd.conf.5)
configure_file(man/qdstat.8.in ${CMAKE_CURRENT_BINARY_DIR}/man/qdstat.8)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/man/qdrouterd.8 DESTINATION ${MAN_INSTALL_DIR}/man8)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/man/qdrouterd.conf.5 DESTINATION ${MAN_INSTALL_DIR}/man5)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/man/qdstat.8 DESTINATION ${MAN_INSTALL_DIR}/man8)

add_subdirectory(api)

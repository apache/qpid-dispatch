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

set(CONSOLE_BASE_SOURCE_DIR "${CMAKE_SOURCE_DIR}/console/stand-alone/")

##
## Add option to not install the stand-alone console
##
option(CONSOLE_INSTALL "Install stand-alone console" ON)
if(CONSOLE_INSTALL)

	# Static console files
	install(
	  DIRECTORY ${CONSOLE_BASE_SOURCE_DIR}
          DESTINATION ${CONSOLE_STAND_ALONE_INSTALL_DIR}
    )

endif(CONSOLE_INSTALL)

##
## Add a custom make hawtio target
##
find_program (MAVEN_EXE mvn DOC "Location of the maven program")
if (MAVEN_EXE)
	# the directory where the .war file will be built
	set(HAWTIO_BUILD_DIR "${CMAKE_BINARY_DIR}/hawtio")

	# create the console .war file
	add_custom_target(hawtio
	    COMMAND ${MAVEN_EXE} -DbuildDirectory=${HAWTIO_BUILD_DIR} package
	    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/console/hawtio
	)

	# install the built war file into the console dir
	install(
	    # We don't know in advance what the name of the final .war will be because
		# the war file name depends on the version in the pom.xml. The version will change each release
	    CODE "file( GLOB builtwar \"${HAWTIO_BUILD_DIR}/dispatch-hawtio-console*.war\" )"
	    CODE "file( INSTALL \${builtwar} DESTINATION \"${CONSOLE_INSTALL_DIR}/hawtio\" )"
	)
endif(MAVEN_EXE)

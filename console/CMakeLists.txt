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

##
## Add cmake option to choose whether to install stand-alone console
##
option(CONSOLE_INSTALL "Build and install console (requires npm 3.1.10+)" ON)

if(CONSOLE_INSTALL)
  find_program (NPM_EXECUTABLE npm DOC "Location of npm package manager")

  if (NPM_EXECUTABLE)
    execute_process(COMMAND ${NPM_EXECUTABLE} --version
        OUTPUT_VARIABLE NPM_VERSION)
    if(${NPM_VERSION} VERSION_LESS "3.1.10")
      message(STATUS "Cannot build console. npm version 3.1.10 or greater is required.")
    else(${NPM_VERSION} VERSION_LESS "3.1.10")
          set(CONSOLE_SOURCE_DIR "${CMAKE_SOURCE_DIR}/console/react")
          set(CONSOLE_BUILD_DIR "${CMAKE_BINARY_DIR}/console")

          ## copy the build config files
          configure_file( ${CONSOLE_SOURCE_DIR}/package.json ${CONSOLE_BUILD_DIR}/ COPYONLY)
          configure_file( ${CONSOLE_SOURCE_DIR}/package-lock.json ${CONSOLE_BUILD_DIR}/ COPYONLY)

          function(copyDirectory fromDir toDir subDir)
            set(BASE "${fromDir}/${subDir}")
            file (GLOB_RECURSE FILES ${BASE}/*.*)
            foreach(ITEM ${FILES})
              get_filename_component(ITEM_PATH ${ITEM} PATH)
              get_filename_component(ITEM_FILE ${ITEM} NAME)
              string(REPLACE ${BASE} "" ITEM_PATH ${ITEM_PATH})
              #message(STATUS "from ${ITEM} to ${toDir}/${subDir}${ITEM_PATH}/${ITEM_FILE}")
              configure_file( ${ITEM} "${toDir}/${subDir}${ITEM_PATH}/${ITEM_FILE}" COPYONLY)
            endforeach()
          endfunction()

          # copy the files needed for the build to the build/console directory
          # this is needed because 'npm run build' and 'npm test' create and modify
          # files and we don't want that to happen in the source directory
          copyDirectory(${CONSOLE_SOURCE_DIR} ${CONSOLE_BUILD_DIR} "src")
          copyDirectory(${CONSOLE_SOURCE_DIR} ${CONSOLE_BUILD_DIR} "public")
          copyDirectory(${CONSOLE_SOURCE_DIR} ${CONSOLE_BUILD_DIR} "test_data")

          ## If any of these files change, rebuild the console
          file (GLOB_RECURSE CONSOLE_SOURCES ${CONSOLE_SOURCE_DIR}/src/*.*)
          file (GLOB_RECURSE CONSOLE_PUBLIC_FILES ${CONSOLE_SOURCE_DIR}/public/*.*)
          file (GLOB_RECURSE CONSOLE_TEST_FILES ${CONSOLE_SOURCE_DIR}/test_data/*.*)
          file (GLOB_RECURSE CONSOLE_BUILD_FILES ${CONSOLE_SOURCE_DIR}/package*.json)
          set(ALL_CONSOLE_SOURCES ${CONSOLE_SOURCES} ${CONSOLE_PUBLIC_FILES} ${CONSOLE_TEST_FILES} ${CONSOLE_BUILD_FILES})

          ## If index.html is older than any of the above ALL_CONSOLE_SOURCE
          ## then rebuild the console
          set(CONSOLE_ARTIFACTS
            ${CONSOLE_BUILD_DIR}/build/index.html
          )

          ## Tell cmake how and when to build ${CONSOLE_ARTIFACTS}
          add_custom_command (
            OUTPUT ${CONSOLE_ARTIFACTS}
            COMMENT "Running console build"
            COMMAND ${NPM_EXECUTABLE} install --loglevel=error
            COMMAND ${NPM_EXECUTABLE} run build -- --target "production"
            DEPENDS ${ALL_CONSOLE_SOURCES}
            WORKING_DIRECTORY ${CONSOLE_BUILD_DIR}/
            )

          ## Ensure ${CONSOLE_ARTIFACTS} is built on a make when needed
          add_custom_target(console ALL
            DEPENDS ${CONSOLE_ARTIFACTS}
          )

          ##
          ## Install the static and built console files
          ##

          ## Files copied to the root of the console's install dir
          install(DIRECTORY ${CONSOLE_BUILD_DIR}/build/
            DESTINATION ${CONSOLE_STAND_ALONE_INSTALL_DIR}
            PATTERN "*.map" EXCLUDE
          )

    endif(${NPM_VERSION} VERSION_LESS "3.1.10")
  endif(NPM_EXECUTABLE)

endif(CONSOLE_INSTALL)

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
## Add cmake option to choose whether to install stand-alone console
##
option(CONSOLE_INSTALL "Build and install console (requires npm)" ON)

## Files copied to the root of the console's install dir
set(BASE_FILES
  ${CONSOLE_BASE_SOURCE_DIR}index.html
  ${CONSOLE_BASE_SOURCE_DIR}favicon-32x32.png
)
## Files copied to the css/ dir
set(CSS_FONTS
  ${CONSOLE_BASE_SOURCE_DIR}plugin/css/brokers.ttf
  ${CONSOLE_BASE_SOURCE_DIR}node_modules/angular-ui-grid/ui-grid.woff
  ${CONSOLE_BASE_SOURCE_DIR}node_modules/angular-ui-grid/ui-grid.ttf
)
## Files copied to the fonts/ dir
set(VENDOR_FONTS
  ${CONSOLE_BASE_SOURCE_DIR}node_modules/patternfly/dist/fonts/OpenSans-Regular-webfont.woff2
  ${CONSOLE_BASE_SOURCE_DIR}node_modules/patternfly/dist/fonts/OpenSans-Bold-webfont.woff2
  ${CONSOLE_BASE_SOURCE_DIR}node_modules/patternfly/dist/fonts/OpenSans-Light-webfont.woff2
  ${CONSOLE_BASE_SOURCE_DIR}node_modules/patternfly/dist/fonts/OpenSans-Semibold-webfont.woff2
  ${CONSOLE_BASE_SOURCE_DIR}node_modules/patternfly/dist/fonts/OpenSans-BoldItalic-webfont.woff2
  ${CONSOLE_BASE_SOURCE_DIR}node_modules/patternfly/dist/fonts/OpenSans-Italic-webfont.woff2
  ${CONSOLE_BASE_SOURCE_DIR}node_modules/patternfly/dist/fonts/fontawesome-webfont.woff2
  ${CONSOLE_BASE_SOURCE_DIR}node_modules/patternfly/dist/fonts/fontawesome-webfont.eot
  ${CONSOLE_BASE_SOURCE_DIR}node_modules/patternfly/dist/fonts/PatternFlyIcons-webfont.ttf
  ${CONSOLE_BASE_SOURCE_DIR}node_modules/bootstrap/dist/fonts/glyphicons-halflings-regular.woff2
)
## Files needed to create the ${GULPED_ARTIFACTS}
file (GLOB_RECURSE CONSOLE_JS_SOURCES ${CONSOLE_BASE_SOURCE_DIR}plugin/js/*.js)
file (GLOB_RECURSE CONSOLE_TS_SOURCES ${CONSOLE_BASE_SOURCE_DIR}plugin/js/*.ts)
file (GLOB CONSOLE_CSS_SOURCES ${CONSOLE_BASE_SOURCE_DIR}plugin/css/dispatch.css)
set(CONSOLE_SOURCES ${CONSOLE_JS_SOURCES} ${CONSOLE_TS_SOURCES} ${CONSOLE_CSS_SOURCES})

## Files created during the console build
set(GULPED_ARTIFACTS
  ${CONSOLE_BASE_SOURCE_DIR}dist/js/dispatch.min.js
  ${CONSOLE_BASE_SOURCE_DIR}dist/js/vendor.min.js
  ${CONSOLE_BASE_SOURCE_DIR}dist/css/dispatch.min.css
  ${CONSOLE_BASE_SOURCE_DIR}dist/css/vendor.min.css
)

if(CONSOLE_INSTALL)
  find_program(NPM_EXE npm DOC "Location of the npm package manager")
    if (NPM_EXE)

      ## Tell cmake how and when to build ${GULPED_ARTIFACTS}
      add_custom_command (
        OUTPUT ${GULPED_ARTIFACTS}
        COMMENT "Running console build"
        COMMAND npm install --loglevel=error
        COMMAND npx gulp
        DEPENDS ${CONSOLE_SOURCES}
        WORKING_DIRECTORY ${CONSOLE_BASE_SOURCE_DIR}
        )

      ## Ensure ${GULPED_ARTIFACTS} is built on a make when needed
      add_custom_target(console ALL
        DEPENDS ${GULPED_ARTIFACTS}
        WORKING_DIRECTORY ${CONSOLE_BASE_SOURCE_DIR}
      )

      ##
      ## Install the static and built console files
      ##
      install(DIRECTORY ${CONSOLE_BASE_SOURCE_DIR}dist/
        DESTINATION ${CONSOLE_STAND_ALONE_INSTALL_DIR}
      )
      install(DIRECTORY ${CONSOLE_BASE_SOURCE_DIR}plugin/html/
        DESTINATION ${CONSOLE_STAND_ALONE_INSTALL_DIR}/html/
        FILES_MATCHING PATTERN "*.html"
      )
      install(FILES ${BASE_FILES}
        DESTINATION ${CONSOLE_STAND_ALONE_INSTALL_DIR}
      )
      install(FILES ${CSS_FONTS}
        DESTINATION ${CONSOLE_STAND_ALONE_INSTALL_DIR}/css/
      )
      install(FILES ${VENDOR_FONTS}
        DESTINATION ${CONSOLE_STAND_ALONE_INSTALL_DIR}/fonts/
      )

    else(NPM_EXE)
      message(WARNING "Cannot build console, npm not found")
    endif(NPM_EXE)
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

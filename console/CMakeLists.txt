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
option(CONSOLE_INSTALL "Build and install console (requires npm 5.2+)" ON)

if(CONSOLE_INSTALL)
  find_program (NPM_EXECUTABLE npm DOC "Location of npm package manager")

  if (NPM_EXECUTABLE)
    execute_process(COMMAND ${NPM_EXECUTABLE} --version
        OUTPUT_VARIABLE NPM_VERSION)
    if(${NPM_VERSION} VERSION_EQUAL "5.2.0" OR ${NPM_VERSION} VERSION_GREATER "5.2.0")

      find_program(NPX_EXE npx DOC "Location of the npx task runner")
        if (NPX_EXE)

          set(CONSOLE_SOURCE_DIR "${CMAKE_SOURCE_DIR}/console/stand-alone")
          set(CONSOLE_BUILD_DIR "${CMAKE_BINARY_DIR}/console")

          ## copy the build config files
          configure_file( ${CONSOLE_SOURCE_DIR}/package.json ${CONSOLE_BUILD_DIR}/ COPYONLY)
          configure_file( ${CONSOLE_SOURCE_DIR}/package-lock.json ${CONSOLE_BUILD_DIR}/ COPYONLY)
          configure_file( ${CONSOLE_SOURCE_DIR}/tslint.json ${CONSOLE_BUILD_DIR}/ COPYONLY)
          configure_file( ${CONSOLE_SOURCE_DIR}/gulpfile.js ${CONSOLE_BUILD_DIR}/ COPYONLY)
          configure_file( ${CONSOLE_SOURCE_DIR}/vendor-js.txt ${CONSOLE_BUILD_DIR}/ COPYONLY)
          configure_file( ${CONSOLE_SOURCE_DIR}/vendor-css.txt ${CONSOLE_BUILD_DIR}/ COPYONLY)

          ## Files needed to create the ${CONSOLE_ARTIFACTS}
          file (GLOB_RECURSE CONSOLE_JS_SOURCES ${CONSOLE_SOURCE_DIR}/plugin/js/*.js)
          file (GLOB_RECURSE CONSOLE_TS_SOURCES ${CONSOLE_SOURCE_DIR}/plugin/js/*.ts)
          file (GLOB CONSOLE_TXT_SOURCES ${CONSOLE_BUILD_DIR}/vendor-*.txt)
          set(CONSOLE_CSS_SOURCE ${CONSOLE_SOURCE_DIR}/plugin/css/dispatch.css)
          set(ALL_CONSOLE_SOURCES ${CONSOLE_CSS_SOURCE} ${CONSOLE_TXT_SOURCES})

          ## Files created during the console build
          set(CONSOLE_ARTIFACTS
            ${CONSOLE_BUILD_DIR}/dist/js/vendor.min.js
            ${CONSOLE_BUILD_DIR}/dist/css/dispatch.min.css
            ${CONSOLE_BUILD_DIR}/dist/css/vendor.min.css
          )

          ## Tell cmake how and when to build ${CONSOLE_ARTIFACTS}
          add_custom_command (
            OUTPUT ${CONSOLE_ARTIFACTS}
            COMMENT "Running console build"
            COMMAND npm install --loglevel=error
            COMMAND ${NPX_EXE} gulp --src ${CONSOLE_SOURCE_DIR} --build "production"
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
          set(BASE_FILES
            ${CONSOLE_SOURCE_DIR}/index.html
            ${CONSOLE_SOURCE_DIR}/main.js
            ${CONSOLE_SOURCE_DIR}/favicon-32x32.png
          )
          ## Files copied to the css/ dir
          set(CSS_FONTS
            ${CONSOLE_SOURCE_DIR}/plugin/css/brokers.ttf
          )
          ## Files copied to the css/fonts/ dir
          set(CSSFONTS_FONTS
            ${CONSOLE_BUILD_DIR}/node_modules/angular-ui-grid/fonts/ui-grid.woff
            ${CONSOLE_BUILD_DIR}/node_modules/angular-ui-grid/fonts/ui-grid.ttf
          )
          ## Files copied to the fonts/ dir
          set(VENDOR_FONTS
            ${CONSOLE_BUILD_DIR}/node_modules/patternfly/dist/fonts/OpenSans-Regular-webfont.woff2
            ${CONSOLE_BUILD_DIR}/node_modules/patternfly/dist/fonts/OpenSans-Bold-webfont.woff2
            ${CONSOLE_BUILD_DIR}/node_modules/patternfly/dist/fonts/OpenSans-Light-webfont.woff2
            ${CONSOLE_BUILD_DIR}/node_modules/patternfly/dist/fonts/OpenSans-Semibold-webfont.woff2
            ${CONSOLE_BUILD_DIR}/node_modules/patternfly/dist/fonts/OpenSans-SemiboldItalic-webfont.woff2
            ${CONSOLE_BUILD_DIR}/node_modules/patternfly/dist/fonts/OpenSans-BoldItalic-webfont.woff2
            ${CONSOLE_BUILD_DIR}/node_modules/patternfly/dist/fonts/OpenSans-Italic-webfont.woff2
            ${CONSOLE_BUILD_DIR}/node_modules/patternfly/dist/fonts/fontawesome-webfont.woff2
            ${CONSOLE_BUILD_DIR}/node_modules/patternfly/dist/fonts/fontawesome-webfont.eot
            ${CONSOLE_BUILD_DIR}/node_modules/patternfly/dist/fonts/PatternFlyIcons-webfont.ttf
            ${CONSOLE_BUILD_DIR}/node_modules/bootstrap/dist/fonts/glyphicons-halflings-regular.woff2
          )

          install(DIRECTORY ${CONSOLE_BUILD_DIR}/dist/
            DESTINATION ${CONSOLE_STAND_ALONE_INSTALL_DIR}
            PATTERN "*.map" EXCLUDE
          )
          install(DIRECTORY ${CONSOLE_SOURCE_DIR}/plugin/html/
            DESTINATION ${CONSOLE_STAND_ALONE_INSTALL_DIR}/html
            FILES_MATCHING PATTERN "*.html"
          )
          install(DIRECTORY ${CONSOLE_SOURCE_DIR}/plugin/js/
            DESTINATION ${CONSOLE_STAND_ALONE_INSTALL_DIR}/plugin/js
            FILES_MATCHING PATTERN "*.js"
          )
          install(FILES ${BASE_FILES}
            DESTINATION ${CONSOLE_STAND_ALONE_INSTALL_DIR}
          )
          install(FILES ${CSS_FONTS}
            DESTINATION ${CONSOLE_STAND_ALONE_INSTALL_DIR}/css/
          )
          install(FILES ${CSSFONTS_FONTS}
            DESTINATION ${CONSOLE_STAND_ALONE_INSTALL_DIR}/css/fonts/
          )
          install(FILES ${VENDOR_FONTS}
            DESTINATION ${CONSOLE_STAND_ALONE_INSTALL_DIR}/fonts/
          )
          install(DIRECTORY ${CONSOLE_SOURCE_DIR}/plugin/data/
            DESTINATION ${CONSOLE_STAND_ALONE_INSTALL_DIR}/plugin/data
          )
        else(NPX_EXE)
          message(STATUS "Cannot build console, npx not found.")
        endif(NPX_EXE)
    else(${NPM_VERSION} VERSION_EQUAL "5.2.0" OR ${NPM_VERSION} VERSION_GREATER "5.2.0")
      message(STATUS "Cannot build console. npm version 5.2 or greater is required.")
    endif(${NPM_VERSION} VERSION_EQUAL "5.2.0" OR ${NPM_VERSION} VERSION_GREATER "5.2.0")
  endif(NPM_EXECUTABLE)

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

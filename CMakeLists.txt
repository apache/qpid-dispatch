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

cmake_minimum_required(VERSION 2.8.12)
project(qpid-dispatch C CXX)

# This effectively checks for cmake version 3.1 or later
if (DEFINED CMAKE_C_COMPILE_FEATURES)
    set(CMAKE_C_STANDARD 11)
    set(CMAKE_C_STANDARD_REQUIRED ON)
    set(CMAKE_C_EXTENSIONS ON) # gnu11

    set(CMAKE_CXX_STANDARD 11)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
else()
    set(C_STANDARD_GNU "-std=gnu11")
    set(C_STANDARD_Clang "-std=gnu11")
    set(C_STANDARD_SunPro "-std=c11")

    set(C_STANDARD_FLAGS ${C_STANDARD_${CMAKE_C_COMPILER_ID}})
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${C_STANDARD_FLAGS}")

    set(CXX_STANDARD_FLAGS "-std=c++11")
endif()

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")
set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
set(CMAKE_MACOSX_RPATH TRUE)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

# https://bugzilla.redhat.com/show_bug.cgi?id=1850174 GCC 4.8 claims to support atomics, but it's missing stdatomic.h
include(CheckIncludeFile)
check_include_file("stdatomic.h" HAVE_STDATOMIC_H)
if (NOT HAVE_STDATOMIC_H)
    add_definitions(-D__STDC_NO_ATOMICS__)
endif()

# Set warning compile flags
set(C_WARNING_GNU -Wall -Wextra -Werror -Wpedantic -pedantic-errors -Wno-empty-body -Wno-implicit-fallthrough -Wno-unused-parameter -Wno-ignored-qualifiers -Wno-missing-field-initializers -Wno-sign-compare -Wno-type-limits)
set(C_WARNING_Clang -Wall -Wextra -Werror -Wpedantic -Wno-unused-parameter -Wno-ignored-qualifiers -Wno-missing-field-initializers -Wno-sign-compare -Wno-gnu-statement-expression)
set(C_WARNING_SunPro -errwarn=%all)

set(C_WARNING_FLAGS ${C_WARNING_${CMAKE_C_COMPILER_ID}})
add_compile_options(${C_WARNING_FLAGS})

# Set default build type. Must use FORCE because project() sets default to ""
if (NOT CMAKE_BUILD_TYPE)
  set (CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING
    "Build type: Debug, Release, RelWithDebInfo, MinSizeRel or Coverage (default RelWithDebInfo)" FORCE)
endif(NOT CMAKE_BUILD_TYPE)
if (CMAKE_BUILD_TYPE MATCHES "Deb|Cover")
  set (has_debug_symbols " (has debug symbols)")
endif (CMAKE_BUILD_TYPE MATCHES "Deb|Cover")
message(STATUS "Build type is \"${CMAKE_BUILD_TYPE}\"${has_debug_symbols}")
# This is commonly needed so define it before we include anything else.
string(TOUPPER "${CMAKE_BUILD_TYPE}" uppercase_CMAKE_BUILD_TYPE)

option(QD_MEMORY_STATS "Track memory pool usage statistics" ON)

if(uppercase_CMAKE_BUILD_TYPE STREQUAL "DEBUG" )
    option(QD_ENABLE_ASSERTIONS "Enable assertions" ON)
else()
    option(QD_ENABLE_ASSERTIONS "Enable assertions" OFF)
endif()

file(STRINGS "${CMAKE_SOURCE_DIR}/VERSION.txt" QPID_DISPATCH_VERSION)

include(CheckLibraryExists)
include(CheckSymbolExists)
include(CheckFunctionExists)
include(CheckIncludeFiles)
include(CMakeDependentOption)

##
## Find dependencies
##
if (NOT APPLE)
  find_library(rt_lib rt)
else (NOT APPLE)
  set(rt "")
endif (NOT APPLE)

# This setting mirrors FindPythonInterp behavior on Windows/macOS, which did not consult registry/frameworks first.
if (NOT DEFINED Python_FIND_REGISTRY)
    set(Python_FIND_REGISTRY "LAST")
endif ()
if (NOT DEFINED Python_FIND_FRAMEWORK)
    set(Python_FIND_FRAMEWORK "LAST")
endif ()
find_package(Python 3.6.8 REQUIRED COMPONENTS Interpreter Development)
find_package(Threads REQUIRED)
find_package(Proton 0.34.0 REQUIRED COMPONENTS Core Proactor)
message(STATUS "Found Proton: ${Proton_LIBRARIES} (found version \"${Proton_VERSION}\")" )
##
## Optional dependencies
##

# http2 support is optional
find_package(libnghttp2 1.33.0)
CMAKE_DEPENDENT_OPTION(USE_LIBNGHTTP2 "Use libnghttp2 for HTTP/2 support" ON
                         "libnghttp2_FOUND" OFF)

# Web Sockets
find_package(LibWebSockets 2.4.2)
CMAKE_DEPENDENT_OPTION(USE_LIBWEBSOCKETS "Use libwebsockets for WebSocket support" ON
                         "LIBWEBSOCKETS_FOUND" OFF)

set(SKIP_DELETE_HTTP_LISTENER "True")
if (LIBWEBSOCKETS_FOUND AND LIBWEBSOCKETS_VERSION_STRING)
  # This is a fix for DISPATCH-1513. libwebsockets versions 3.2.0 introduces a new flag called LWS_SERVER_OPTION_ALLOW_HTTP_ON_HTTPS_LISTENER
  # The new flag allows (as the flag says) HTTP over HTTPS listeners. Since this flag is not available before lws 3.2.0 we need
  # to selectively comment out a test.
  set(TEST_OPTION_ALLOW_HTTP_ON_HTTPS_LISTENER "#")
  set(LWS_VERSION_WITH_SSL_FIX "3.2.0")
  if ((LIBWEBSOCKETS_VERSION_STRING STREQUAL LWS_VERSION_WITH_SSL_FIX) OR (LIBWEBSOCKETS_VERSION_STRING STRGREATER  LWS_VERSION_WITH_SSL_FIX))
    set(TEST_OPTION_ALLOW_HTTP_ON_HTTPS_LISTENER "")
  endif()
  # The listener delete functionality worked in all versions of LibWebSockets
  # starting from v2.4.2 thru v3.2.3. It stopped working in v4.0.0 and beyond
  if ( (LIBWEBSOCKETS_VERSION_STRING STREQUAL "2.4.2") OR (  (LIBWEBSOCKETS_VERSION_STRING STRGREATER_EQUAL  "3.0.0") AND (LIBWEBSOCKETS_VERSION_STRING STRLESS  "4.0.0") )  )
    # Do not skip the tests (in system_tests_http.py) that specifically
    # test deletion of http:yes listeners.
    set(SKIP_DELETE_HTTP_LISTENER "False")
  endif()

endif(LIBWEBSOCKETS_FOUND AND LIBWEBSOCKETS_VERSION_STRING)

if (NOT DEFINED DISPATCH_TEST_TIMEOUT)
    set(DISPATCH_TEST_TIMEOUT "600")
endif (NOT DEFINED DISPATCH_TEST_TIMEOUT)
SET(DART_TESTING_TIMEOUT ${DISPATCH_TEST_TIMEOUT} CACHE STRING "Default CTest timeout in seconds")
include (CTest)

set(PYTHON_TEST_COMMAND "-m" "unittest" "-v" "\${py_test_module}"
    CACHE STRING "Command used to run tests implemented in Python unittest; \${py_test_module} will be replaced with test module name.")

set (SO_VERSION_MAJOR 2)
set (SO_VERSION_MINOR 0)
set (SO_VERSION "${SO_VERSION_MAJOR}.${SO_VERSION_MINOR}")

if (NOT DEFINED LIB_SUFFIX)
    get_property(LIB64 GLOBAL PROPERTY FIND_LIBRARY_USE_LIB64_PATHS)
    if ("${LIB64}" STREQUAL "TRUE" AND ${CMAKE_SIZEOF_VOID_P} STREQUAL "8")
        set(LIB_SUFFIX 64)
    else()
        set(LIB_SUFFIX "")
    endif()
endif()

set(INCLUDE_INSTALL_DIR include CACHE PATH "Include file directory")
set(QPID_DISPATCH_HOME "lib/qpid-dispatch" CACHE PATH "Private Dispatch library directory")
set(LIB_INSTALL_DIR "lib${LIB_SUFFIX}" CACHE PATH "Library object file directory")
set(SHARE_INSTALL_DIR share CACHE PATH "Shared read only data directory")
set(DOC_INSTALL_DIR ${SHARE_INSTALL_DIR}/doc CACHE PATH "Documentation directory")
set(QD_DOC_INSTALL_DIR ${SHARE_INSTALL_DIR}/doc/qpid-dispatch CACHE PATH "Qpid Dispatch documentation directory")
set(MAN_INSTALL_DIR share/man CACHE PATH "Manpage directory")
set(QPID_DISPATCH_HOME_INSTALLED ${CMAKE_INSTALL_PREFIX}/${QPID_DISPATCH_HOME})

set(CONSOLE_BASE_INSTALL_DIR "share/qpid-dispatch/console")
set(CONSOLE_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/${CONSOLE_BASE_INSTALL_DIR}")
set(CONSOLE_STAND_ALONE_INSTALL_DIR "${CONSOLE_INSTALL_DIR}")

set(RUN ${Python_EXECUTABLE} ${CMAKE_BINARY_DIR}/run.py)

# define the configuration directory based on whether or not the install prefix is defined
if(NOT DEFINED SYSCONF_INSTALL_DIR)
    if(CMAKE_INSTALL_PREFIX STREQUAL "/usr")
        set(SYSCONF_INSTALL_DIR "/etc")
    else()
        set(SYSCONF_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/etc")
    endif()
endif()

set(QPID_DISPATCH_CONFDIR ${SYSCONF_INSTALL_DIR}/qpid-dispatch)

# Set up runtime checks (valgrind, sanitizers etc.)
include(cmake/RuntimeChecks.cmake)
SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${SANITIZE_FLAGS}")

##
## Include directories used by all sub-directories.
##
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}/include
    ${Proton_Proactor_INCLUDE_DIRS}
    ${Proton_Core_INCLUDE_DIRS}
    ${Python_INCLUDE_DIRS}
    )

# Originally from the LLVM project, https://opensource.apple.com/source/llvmCore/llvmCore-2358.3/CMakeLists.txt.auto.html
if(QD_ENABLE_ASSERTIONS)
    # MSVC doesn't like _DEBUG on release builds.
    if(NOT MSVC)
        add_definitions(-D_DEBUG)
    endif()
    # On non-Debug builds cmake automatically defines NDEBUG, so we explicitly undefine it:
    if(NOT uppercase_CMAKE_BUILD_TYPE STREQUAL "DEBUG")
        # NOTE: use `add_compile_options` rather than `add_definitions` since
        # `add_definitions` does not support generator expressions.
        if(${CMAKE_VERSION} VERSION_LESS "3.11.4")
            add_compile_options(-UNDEBUG)
        else()
            add_compile_options($<$<OR:$<COMPILE_LANGUAGE:C>,$<COMPILE_LANGUAGE:CXX>>:-UNDEBUG>)
        endif()


        # Also remove /D NDEBUG to avoid MSVC warnings about conflicting defines.
        foreach (flags_var_to_scrub
                CMAKE_CXX_FLAGS_RELEASE
                CMAKE_CXX_FLAGS_RELWITHDEBINFO
                CMAKE_CXX_FLAGS_MINSIZEREL
                CMAKE_C_FLAGS_RELEASE
                CMAKE_C_FLAGS_RELWITHDEBINFO
                CMAKE_C_FLAGS_MINSIZEREL)
            string (REGEX REPLACE "(^| )[/-]D *NDEBUG($| )" " "
                    "${flags_var_to_scrub}" "${${flags_var_to_scrub}}")
        endforeach()
    endif()
endif()

# Set up extra coverage analysis options for gcc and clang
if (CMAKE_BUILD_TYPE MATCHES "Coverage")
 set (CMAKE_C_FLAGS_COVERAGE "-g -O0 --coverage")
 set (CMAKE_EXE_LINKER_FLAGS_COVERAGE "--coverage")
 set (CMAKE_MODULE_LINKER_FLAGS_COVERAGE "--coverage")
 set (CMAKE_SHARED_LINKER_FLAGS_COVERAGE "--coverage")
 mark_as_advanced(
   CMAKE_C_FLAGS_COVERAGE
   CMAKE_EXE_LINKER_FLAGS_COVERAGE
   CMAKE_MODULE_LINKER_FLAGS_COVERAGE
   CMAKE_SHARED_LINKER_FLAGS_COVERAGE)
 make_directory(${CMAKE_BINARY_DIR}/coverage_results)
 add_custom_target(coverage
   WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/coverage_results
   COMMAND ${CMAKE_SOURCE_DIR}/bin/record-coverage.sh ${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR})
endif(CMAKE_BUILD_TYPE MATCHES "Coverage")

##
## Header file installation
##
file(GLOB headers "include/qpid/dispatch/*.h")
install(FILES ${headers} DESTINATION ${INCLUDE_INSTALL_DIR}/qpid/dispatch)
install(FILES include/qpid/dispatch.h DESTINATION ${INCLUDE_INSTALL_DIR}/qpid)
install(FILES etc/qdrouterd.conf DESTINATION ${SYSCONF_INSTALL_DIR}/qpid-dispatch)
install(FILES etc/sasl2/qdrouterd.conf DESTINATION ${SYSCONF_INSTALL_DIR}/sasl2)


# Tools
install(PROGRAMS
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/qdstat
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/qdmanage
    DESTINATION bin)

# Doc files
install(FILES
  LICENSE
  README
  DESTINATION ${QD_DOC_INSTALL_DIR})

add_subdirectory(src) # Build src first so other subdirs can use QPID_DISPATCH_LIB

# run.py environment runner script - needs QPID_DISPATCH_LIB
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/run.py.in ${CMAKE_CURRENT_BINARY_DIR}/run.py)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/run.py.in ${CMAKE_CURRENT_BINARY_DIR}/tests/run.py)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/tests/system_tests_http.py.in ${CMAKE_CURRENT_SOURCE_DIR}/tests/system_tests_http.py)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/python/qpid_dispatch_internal/management/agent.py.in ${CMAKE_CURRENT_SOURCE_DIR}/python/qpid_dispatch_internal/management/agent.py)
execute_process(COMMAND ${RUN} --sh OUTPUT_FILE config.sh)

if (NOT UNITTEST_MISSING)
  file(GLOB SCRAPER_SRC ${CMAKE_CURRENT_SOURCE_DIR}/tools/scraper/*.py)
  file(COPY ${SCRAPER_SRC} DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/tests/scraper/)
  add_subdirectory(tests)
endif(NOT UNITTEST_MISSING)
add_subdirectory(python)
add_subdirectory(router)
add_subdirectory(docs)
add_subdirectory(console)

# reconfigure.in is a workaround to force cmake re-configuration. For example,
# we use GLOB to collect .h files for install and apidoc, so if you _remove_ a
# .h file it won't trigger automatic re-configure and everybody's builds will
# fail till they run cmake manually.
#
# If you do check in such a change, increase the number in this file by 1.
# That will force automatic re-configure and everybody will be happy.
#
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/reconfigure.in ${CMAKE_CURRENT_BINARY_DIR}/reconfigure)

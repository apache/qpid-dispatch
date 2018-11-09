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

# Set default build type. Must come before project() which sets default to ""
if (NOT CMAKE_BUILD_TYPE)
  set (CMAKE_BUILD_TYPE RelWithDebInfo CACHE string
    "Build type: Debug, Release, RelWithDebInfo, MinSizeRel or Coverage (default RelWithDebInfo)" FORCE)
endif(NOT CMAKE_BUILD_TYPE)
if (CMAKE_BUILD_TYPE MATCHES "Deb|Cover")
  set (has_debug_symbols " (has debug symbols)")
endif (CMAKE_BUILD_TYPE MATCHES "Deb|Cover")
message(STATUS "Build type is \"${CMAKE_BUILD_TYPE}\"${has_debug_symbols}")

project(qpid-dispatch C)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")

# Build time switch to turn off memory pooling.
option(USE_MEMORY_POOL "Use per-thread memory pools" ON)
option(QD_MEMORY_STATS "Track memory pool usage statistics" ON)

file(STRINGS "${CMAKE_SOURCE_DIR}/VERSION.txt" QPID_DISPATCH_VERSION)

cmake_minimum_required(VERSION 2.6.2)
include(CheckLibraryExists)
include(CheckSymbolExists)
include(CheckFunctionExists)
include(CheckIncludeFiles)
include(FindPythonInterp)
include(FindPythonLibs)


if (PYTHON_VERSION_MAJOR STREQUAL 3)
    set(PY_STRING "python3")
elseif(PYTHON_VERSION_MAJOR STREQUAL 2)
    set(PY_STRING "python")
endif()

# Find python-unittest2
execute_process(COMMAND "${PYTHON_EXECUTABLE}" "-c" "import unittest2"
			RESULT_VARIABLE UNITTEST2_MISSING
			ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
enable_testing()
if(UNITTEST2_MISSING)
  message(STATUS "WARNING: python-unittest2 is not installed. ***unit tests cannot be run***" )
  # add bogus test for folk who may have missed the cmake warning
  add_test(WARNING_python_unittest2_module_NOT_INSTALLED false)
else(UNITTEST2_MISSING)
  include (CTest)
endif(UNITTEST2_MISSING)


if (NOT PYTHONLIBS_FOUND)
     message(FATAL_ERROR "Python Development Libraries are needed.")
endif (NOT PYTHONLIBS_FOUND)

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
set(QD_DOC_INSTALL_DIR ${SHARE_INSTALL_DIR}/doc/qpid-dispatch CACHE PATH "Qpid dispatch documentation directory")
set(MAN_INSTALL_DIR share/man CACHE PATH "Manpage directory")
set(QPID_DISPATCH_HOME_INSTALLED ${CMAKE_INSTALL_PREFIX}/${QPID_DISPATCH_HOME})

set(CONSOLE_BASE_INSTALL_DIR "share/qpid-dispatch/console")
set(CONSOLE_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/${CONSOLE_BASE_INSTALL_DIR}")
set(CONSOLE_STAND_ALONE_INSTALL_DIR "${CONSOLE_INSTALL_DIR}/stand-alone")

set(RUN ${PYTHON_EXECUTABLE} ${CMAKE_BINARY_DIR}/run.py)

# define the configuration directory based on whether or not the install prefix is defined
if(NOT DEFINED SYSCONF_INSTALL_DIR)
    if(CMAKE_INSTALL_PREFIX STREQUAL "/usr")
        set(SYSCONF_INSTALL_DIR "/etc")
    else()
        set(SYSCONF_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/etc")
    endif()
endif()

set(QPID_DISPATCH_CONFDIR ${SYSCONF_INSTALL_DIR}/qpid-dispatch)

if (NOT COMMAND add_compile_options)
  macro (add_compile_options option)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${option}")
  endmacro (add_compile_options)
endif (NOT COMMAND add_compile_options)

##
## Find dependencies
##
find_library(pthread_lib pthread)
find_library(dl_lib dl)
find_library(rt_lib rt)

find_package(Proton 0.23 REQUIRED)

message(STATUS "Found Proton: ${Proton_LIBRARIES} (found version \"${Proton_VERSION}\")" )

## Optional dependencies
include(FindLibWebSockets)
option(USE_LIBWEBSOCKETS "Use libwebsockets for WebSocket support" ${LIBWEBSOCKETS_FOUND})

##
## Find Valgrind
##
find_program(VALGRIND_EXECUTABLE valgrind DOC "Location of the valgrind program")
mark_as_advanced(VALGRIND_EXECUTABLE)
find_package_handle_standard_args(VALGRIND DEFAULT_MSG VALGRIND_EXECUTABLE)
option(USE_VALGRIND "Use valgrind when running tests" OFF)
option(VALGRIND_XML "Write valgrind output as XML" OFF)

if (USE_VALGRIND)
  if (CMAKE_BUILD_TYPE MATCHES "Coverage")
    message(WARNING "Building for coverage analysis; disabling valgrind run-time error detection")
  else ()
    set(TEST_RUNNER "${VALGRIND_EXECUTABLE} --quiet --leak-check=full --show-leak-kinds=definite --errors-for-leak-kinds=definite --error-exitcode=42 --suppressions=${CMAKE_SOURCE_DIR}/tests/valgrind.supp")
    if (VALGRIND_XML)
      set(TEST_RUNNER "${TEST_RUNNER} --xml=yes --xml-file=valgrind-%p.xml")
    endif()
  endif ()
endif()

option(USE_SANITIZERS "Compile with sanitizers (ASan, UBSan, TSan); incompatible with Valgrind" OFF)
if (USE_SANITIZERS)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -fsanitize=leak -fsanitize=undefined")
  add_compile_options(-g)
  add_compile_options(-fno-omit-frame-pointer)
endif (USE_SANITIZERS)

option(USE_TSAN "Compile with ThreadSanitizer (TSan); incompatible with Valgrind" OFF)
if (USE_TSAN)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=thread")
  add_compile_options(-g)
  add_compile_options(-fno-omit-frame-pointer)
endif (USE_TSAN)

##
## Include directories used by all sub-directories.
##
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}/include
    ${Proton_Proactor_INCLUDE_DIRS}
    ${Proton_Core_INCLUDE_DIRS}
    ${PYTHON_INCLUDE_PATH}
    )

if (NOT CMAKE_SYSTEM_NAME STREQUAL SunOS)
 add_compile_options(-Werror)
 add_compile_options(-Wall)
 include(CheckCCompilerFlag)
 check_c_compiler_flag(-Wpedantic HAS_PEDANTIC_FLAG)
 if (HAS_PEDANTIC_FLAG)
    add_compile_options(-Wpedantic)
 endif(HAS_PEDANTIC_FLAG)
 add_compile_options(-std=gnu99)
 add_compile_options(-pthread)
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
else (NOT CMAKE_SYSTEM_NAME STREQUAL SunOS)
 add_compile_options(-xc99)
 add_compile_options(-errwarn=%all)
 add_compile_options(-lpthread)
endif (NOT CMAKE_SYSTEM_NAME STREQUAL SunOS)

if (CMAKE_C_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wno-gnu-statement-expression)
endif()

if (NOT (USE_SANITIZERS OR USE_TSAN))
  set(CATCH_UNDEFINED "-Wl,-z,defs")
endif ()

##
## Header file installation
##
file(GLOB headers "include/qpid/dispatch/*.h")
install(FILES ${headers} DESTINATION ${INCLUDE_INSTALL_DIR}/qpid/dispatch)
install(FILES include/qpid/dispatch.h DESTINATION ${INCLUDE_INSTALL_DIR}/qpid)
install(FILES etc/qdrouterd.conf DESTINATION ${SYSCONF_INSTALL_DIR}/qpid-dispatch)
install(FILES etc/sasl2/qdrouterd.conf DESTINATION ${SYSCONF_INSTALL_DIR}/sasl2)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/tools/qdstat.in ${CMAKE_CURRENT_SOURCE_DIR}/tools/qdstat)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/tools/qdmanage.in ${CMAKE_CURRENT_SOURCE_DIR}/tools/qdmanage)


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
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/tests/authservice.py.in ${CMAKE_CURRENT_SOURCE_DIR}/tests/authservice.py)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/tests/failoverserver.py.in ${CMAKE_CURRENT_SOURCE_DIR}/tests/failoverserver.py)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/tests/system_tests_authz_service_plugin.py.in ${CMAKE_CURRENT_SOURCE_DIR}/tests/system_tests_authz_service_plugin.py)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/tests/system_tests_handle_failover.py.in ${CMAKE_CURRENT_SOURCE_DIR}/tests/system_tests_handle_failover.py)
execute_process(COMMAND ${RUN} --sh OUTPUT_FILE config.sh)

if (NOT UNITTEST2_MISSING)
  add_subdirectory(tests)
endif(NOT UNITTEST2_MISSING)
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

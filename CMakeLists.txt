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
set (CMAKE_BUILD_TYPE RelWithDebInfo CACHE string
  "Build type: Debug, Release, RelWithDebInfo or MinSizeRel (default RelWithDebInfo)")
if (CMAKE_BUILD_TYPE MATCHES "Deb")
  set (has_debug_symbols " (has debug symbols)")
endif (CMAKE_BUILD_TYPE MATCHES "Deb")

project(qpid-dispatch C)

# Build time switch to turn off memory pooling.
option(USE_MEMORY_POOL "Use per-thread memory pools" ON)

file(STRINGS "${CMAKE_SOURCE_DIR}/VERSION.txt" QPID_DISPATCH_VERSION)

cmake_minimum_required(VERSION 2.6)
include(CheckLibraryExists)
include(CheckSymbolExists)
include(CheckFunctionExists)
include(CheckIncludeFiles)
include(FindPythonInterp)
include(FindPythonLibs)

enable_testing()
include (CTest)

if (NOT PYTHONLIBS_FOUND)
     message(FATAL_ERROR "Python Development Libraries are needed.")
endif (NOT PYTHONLIBS_FOUND)

set (SO_VERSION_MAJOR 1)
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

##
## Find dependencies
##
find_library(pthread_lib pthread)
find_library(dl_lib dl)
find_library(rt_lib rt)
find_package(Proton 0.10 REQUIRED)

##
## Find Valgrind
##
find_program(VALGRIND_EXECUTABLE valgrind DOC "Location of the valgrind program")
mark_as_advanced(VALGRIND_EXECUTABLE)
find_package_handle_standard_args(VALGRIND DEFAULT_MSG VALGRIND_EXECUTABLE)
option(USE_VALGRIND "Use valgrind when running tests" OFF)

##
## Include directories used by all sub-directories.
##
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}/include
    ${Proton_INCLUDE_DIRS}
    ${PYTHON_INCLUDE_PATH}
    )

if (NOT COMMAND add_compile_options)
  macro (add_compile_options option)
    SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${option}")
  endmacro (add_compile_options)
endif (NOT COMMAND add_compile_options)

add_compile_options(-pthread)
add_compile_options(-Wall)
add_compile_options(-Werror)
add_compile_options(-std=gnu99)
set(CATCH_UNDEFINED "-Wl,--no-undefined")

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
  TODO
  DESTINATION ${QD_DOC_INSTALL_DIR})


add_subdirectory(src) # Build src first so other subdirs can use QPID_DISPATCH_LIB

# run.py environment runner script - needs QPID_DISPATCH_LIB
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/run.py.in ${CMAKE_CURRENT_BINARY_DIR}/run.py)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/run.py.in ${CMAKE_CURRENT_BINARY_DIR}/tests/run.py)
execute_process(COMMAND ${RUN} --sh OUTPUT_FILE config.sh)

add_subdirectory(tests)
add_subdirectory(python)
add_subdirectory(router)
add_subdirectory(doc)

# reconfigure.in is a workaround to force cmake re-configuration. For example,
# we use GLOB to collect .h files for install and apidoc, so if you _remove_ a
# .h file it won't trigger automatic re-configure and everybody's builds will
# fail till they run cmake manually.
#
# If you do check in such a change, increase the number in this file by 1.
# That will force automatic re-configure and everybody will be happy.
#
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/reconfigure.in ${CMAKE_CURRENT_BINARY_DIR}/reconfigure)

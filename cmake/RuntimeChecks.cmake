#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#

# Configuration for code analysis tools.
#
# The RUNTIME_CHECK variable enables run-time checking when running
# the CTest test suite. The following tools are supported
#
# -DRUNTIME_CHECK=tsan      # turns on thread sanitizer
# -DRUNTIME_CHECK=asan      # address and undefined behavior sanitizer
# -DRUNTIME_CHECK=memcheck  # valgrind memcheck (in progress)
# -DRUNTIME_CHECK=helgrind  # valgrind helgrind (in progress)
#
# This file updates the QDROUTERD_RUNNER and CMAKE_C_FLAGS
# appropriately for use when running the ctest suite.


# Valgrind configuration
#
find_program(VALGRIND_EXECUTABLE valgrind DOC "Location of the valgrind program")
set(VALGRIND_SUPPRESSIONS "${CMAKE_SOURCE_DIR}/tests/valgrind.supp" CACHE STRING "Suppressions file for valgrind")
set(VALGRIND_COMMON_ARGS "--error-exitcode=42 --xml=yes --xml-file=valgrind-%p.xml --quiet --suppressions=${VALGRIND_SUPPRESSIONS}")
mark_as_advanced(VALGRIND_EXECUTABLE VALGRIND_SUPPRESSIONS VALGRIND_COMMON_ARGS)
macro(assert_has_valgrind)
  if(NOT VALGRIND_EXECUTABLE)
    message(FATAL_ERROR "valgrind is not available")
  endif()
endmacro()

# Check for compiler's support of sanitizers.
# Currently have tested back to gcc 5.4.0 and clang 6.0.0, older
# versions may require more work
#
if((CMAKE_C_COMPILER_ID MATCHES "GNU"
      AND (CMAKE_C_COMPILER_VERSION VERSION_GREATER 5.4
        OR CMAKE_C_COMPILER_VERSION VERSION_EQUAL 5.4))
    OR (CMAKE_C_COMPILER_ID MATCHES "Clang"
      AND (CMAKE_C_COMPILER_VERSION VERSION_GREATER 6.0
        OR CMAKE_C_COMPILER_VERSION VERSION_EQUAL 6.0)))
  set(HAS_SANITIZERS TRUE)
endif()
macro(assert_has_sanitizers)
  if(NOT HAS_SANITIZERS)
    message(FATAL_ERROR "compiler sanitizers are not available")
  endif()
endmacro()

# Valid options for RUNTIME_CHECK
#
set(runtime_checks OFF tsan asan memcheck helgrind)

# Set RUNTIME_CHECK value and deal with the older cmake flags for
# valgrind and TSAN
#
set(RUNTIME_CHECK_DEFAULT OFF)
macro(deprecated_enable_check old new doc)
  if (${old})
    message("WARNING: option ${old} is deprecated, use -DRUNTIME_CHECK=${new} instead")
    set(RUNTIME_CHECK_DEFAULT ${new})
  endif()
  unset(${old} CACHE)
endmacro()
option(VALGRIND_XML "Write valgrind output as XML (DEPRECATED)" OFF)
deprecated_enable_check(USE_VALGRIND memcheck "Use valgrind to detect run-time problems")
deprecated_enable_check(USE_TSAN tsan "Compile with thread sanitizer (tsan)")

set(RUNTIME_CHECK ${RUNTIME_CHECK_DEFAULT} CACHE STRING "Enable runtime checks. Valid values: ${runtime_checks}")
if(CMAKE_BUILD_TYPE MATCHES "Coverage" AND RUNTIME_CHECK)
  message(FATAL_ERROR "Cannot set RUNTIME_CHECK with CMAKE_BUILD_TYPE=Coverage")
endif()

if(RUNTIME_CHECK STREQUAL "memcheck")
  assert_has_valgrind()
  message(STATUS "Runtime memory checker: valgrind memcheck")
  set(QDROUTERD_RUNNER "${VALGRIND_EXECUTABLE} --tool=memcheck --leak-check=full --show-leak-kinds=definite --errors-for-leak-kinds=definite ${VALGRIND_COMMON_ARGS}")

elseif(RUNTIME_CHECK STREQUAL "helgrind")
  assert_has_valgrind()
  message(STATUS "Runtime race checker: valgrind helgrind")
  set(QDROUTERD_RUNNER "${VALGRIND_EXECUTABLE} --tool=helgrind ${VALGRIND_COMMON_ARGS}")

elseif(RUNTIME_CHECK STREQUAL "asan")
  assert_has_sanitizers()
  find_library(ASAN_LIBRARY NAME asan libasan)
  if(ASAN_LIBRARY-NOTFOUND)
    message(FATAL_ERROR "libasan not installed - address sanitizer not available")
  endif(ASAN_LIBRARY-NOTFOUND)
  find_library(UBSAN_LIBRARY NAME ubsan libubsan)
  if(UBSAN_LIBRARY-NOTFOUND)
    message(FATAL_ERROR "libubsan not installed - address sanitizer not available")
  endif(UBSAN_LIBRARY-NOTFOUND)
  message(STATUS "Runtime memory checker: gcc/clang address sanitizers")
  set(SANITIZE_FLAGS "-g -fno-omit-frame-pointer -fsanitize=address,undefined")
  set(RUNTIME_ASAN_ENV_OPTIONS "detect_leaks=true suppressions=${CMAKE_SOURCE_DIR}/tests/asan.supp")
  set(RUNTIME_LSAN_ENV_OPTIONS "suppressions=${CMAKE_SOURCE_DIR}/tests/lsan.supp")

elseif(RUNTIME_CHECK STREQUAL "tsan")
  assert_has_sanitizers()
  find_library(TSAN_LIBRARY NAME tsan libtsan)
  if(TSAN_LIBRARY-NOTFOUND)
    message(FATAL_ERROR "libtsan not installed - thread sanitizer not available")
  endif(TSAN_LIBRARY-NOTFOUND)
  message(STATUS "Runtime race checker: gcc/clang thread sanitizer")
  set(SANITIZE_FLAGS "-g -fno-omit-frame-pointer -fsanitize=thread")
  set(RUNTIME_TSAN_ENV_OPTIONS "second_deadlock_stack=1 suppressions=${CMAKE_SOURCE_DIR}/tests/tsan.supp")

elseif(RUNTIME_CHECK)
  message(FATAL_ERROR "'RUNTIME_CHECK=${RUNTIME_CHECK}' is invalid, valid values: ${runtime_checks}")
endif()

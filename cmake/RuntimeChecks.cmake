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

# Configuration for code analysis tools: runtime checking and coverage.

##
## Valgrind
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
        set(QDROUTERD_RUNNER "${VALGRIND_EXECUTABLE} --quiet --leak-check=full --show-leak-kinds=definite --errors-for-leak-kinds=definite --error-exitcode=42 --suppressions=${CMAKE_SOURCE_DIR}/tests/valgrind.supp")
        if (VALGRIND_XML)
            set(QDROUTERD_RUNNER "${QDROUTERD_RUNNER} --xml=yes --xml-file=valgrind-%p.xml")
        endif()
    endif ()
endif()

##
## Sanitizers
##
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

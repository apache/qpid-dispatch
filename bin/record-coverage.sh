#! /usr/bin/env bash

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
# under the License
#


# This script collates coverage data already present from running instrumented code.
#
# It requires the lcov tool to be installed (this provides the lcov and genhtml commands)
#
# It will produce a coverage analysis for gcc or clang compiled builds and currently for
# C and C++ parts of the build tree.
#
# It takes two command line arguments:
# - The first is the dispatch router source tree: this is mandatory.
# - The second is the build tree: this is optional and if not specified is assumed to be the
#   current directory.
#
# The output is in the form of an html report which will be found in the generated html direectory.
# - There will also be a number of intermediate files left in the current directory.
#
# The typical way to use it would be to use the "Coverage" build type to get instrumented
# code, then to run the tests then to extract the coverage information from running the
# tests.
# Something like:
#   cmake -DCMAKE_BUILD_TYPE=Coverage ..
#   make
#   make test
#   make coverage

# set -x

# get full path
function getpath {
  pushd -n $1 > /dev/null
  echo $(dirs -0 -l)
  popd -n > /dev/null
}

SRC=${1?}
BLD=${2:-.}

BLDPATH=$(getpath $BLD)
SRCPATH=$(getpath $SRC)

# Get base profile
# - this initialises 0 counts for every profiled file
#   without this step any file with no counts at all wouldn't
#   show up on the final output.
lcov -c -i -d $BLDPATH -o dispatch-base.info

# Get actual coverage data
lcov -c -d $BLDPATH -o dispatch-ctest.info

# Total them up
lcov --add dispatch-base.info --add dispatch-ctest.info > dispatch-total-raw.info

# Snip out stuff in /usr (we don't care about coverage in system code)
lcov --remove dispatch-total-raw.info "/usr/include*" "/usr/share*" "${SRCPATH}/tests/*" > dispatch-total.info

# Generate report
rm -rf html
genhtml -p $SRCPATH -p $BLDPATH dispatch-total.info --title "Dispatch Router Test Coverage" --demangle-cpp -o html


#!/usr/bin/env bash
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

test -z "$SOURCE_DIR" -a -f config.sh && source ./config.sh

if [[ -z "$SOURCE_DIR" ]]; then
    echo "The devel environment isn't ready.  Run 'source config.sh' from"
    echo "the base of the dispatch source tree"
    exit 1
fi

set -ev

rm -rf $BUILD_DIR
rm -rf $INSTALL_DIR

mkdir $BUILD_DIR
cd $BUILD_DIR

cmake -DCMAKE_INSTALL_PREFIX=$INSTALL_DIR -DCMAKE_BUILD_TYPE=Debug ${DISPATCH_CMAKE_EXTRA_ARGS} $SOURCE_DIR
make -j4
# Test the build.
ctest -VV ${DISPATCH_CTEST_EXTRA_ARGS}
# Run system tests on the install.
make install
python $INSTALL_DIR/lib/qpid-dispatch/tests/run_system_tests.py

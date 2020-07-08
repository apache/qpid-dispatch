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

if [[ ! -f config.sh ]]; then
    echo "You must source config.sh from within its own directory"
    return
fi

export SOURCE_DIR=$(pwd)
export BUILD_DIR=$SOURCE_DIR/${1:-build}
export INSTALL_DIR=$SOURCE_DIR/${2:-install}

PYTHON_BIN=`type -P python || type -P python3`
PYTHON_LIB=$(${PYTHON_BIN} -c "from distutils.sysconfig import get_python_lib; print(get_python_lib(prefix='$INSTALL_DIR'))")

export PYTHONPATH=$PYTHON_LIB:$PYTHONPATH
export PATH=$INSTALL_DIR/sbin:$INSTALL_DIR/bin:$SOURCE_DIR/bin:$PATH

#!/bin/bash

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

# export.sh - Create a release archive.
set -e
trap "cleanup" 0 1 2 3 9 11 13 15

# ME=export.sh
ME=$(basename ${0})

SRC=$(dirname $(dirname $(readlink -f $0)))
echo Source directory=${SRC}

usage()
{
    echo
    echo "Usage: ${ME} [DIR] [TAG]"
    exit 1
}

cleanup()
{
    trap - 0 1 2 3 9 11 13 15
    echo
    [ ${WORKDIR} ] && [ -d ${WORKDIR} ] && rm -rf ${WORKDIR}
}


DIR=$PWD

# This will get the latest created tag
TAG=$(git describe --tags --always)

##
## Allow overrides to be passed on the cmdline
##
if [ $# -gt 2 ]; then
    usage
elif [ $# -ge 1 ]; then
    DIR=$1
    if [ $# -eq 2 ]; then
        TAG=$2
    fi
fi

if [ "$DIR" = "." ]; then 
    DIR=$PWD
fi

echo Using tag ${TAG} to create archive
echo File will be output to ${DIR}

# verify the tag exists
git rev-list -1 tags/${TAG} -- >/dev/null || usage

# mktemp command creates a temp directory for example - /tmp/tmp.k8vDddIzni
WORKDIR=$(mktemp -d)
echo Working Directory=${WORKDIR}


##
## Create the archive
##
(
    cd ${SRC}
    MTIME=$(date -d @`git log -1 --pretty=format:%ct tags/${TAG}` '+%Y-%m-%d %H:%M:%S')
    VERSION=$(git show tags/${TAG}:VERSION.txt)
    ARCHIVE=$DIR/qpid-dispatch-console-${VERSION}.tar.gz
    PREFIX=qpid-dispatch-${VERSION}
    [ -d ${WORKDIR} ] || mkdir -p ${WORKDIR}
    git archive --format=tar --prefix=${PREFIX}/ tags/${TAG} \
        | tar -x -C ${WORKDIR}
    cd ${WORKDIR}
    BUILD_DIR=${WORKDIR}/build
    INSTALL_DIR=${WORKDIR}/install
    mkdir $BUILD_DIR
    pushd $BUILD_DIR
    cmake -DCMAKE_INSTALL_PREFIX=$INSTALL_DIR -DCMAKE_BUILD_TYPE=Release ../$PREFIX
    make install
    pushd $INSTALL_DIR/share/qpid-dispatch/
    tar -c -h -z \
        --owner=root --group=root --numeric-owner \
        --mtime="${MTIME}" \
        -f ${ARCHIVE} console
    popd
    popd
    echo Created "${ARCHIVE}"
    echo Success!!!
)

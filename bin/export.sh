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

# run this script like this - 
# Before executing this script, change directory to the folder in which this file is located, for example.
#   1. cd /home/jdoe/qpid-dispatch/bin
# Run the script like so - 
#  2. ./export.sh <output_folder-full-path> <tag-name> 
#  (Example : ./export.sh /home/jdoe/ 1.5.1 
#  (/home/jdoe is the folder you want the tar.gz file to be put - specify the full path) 
#  1.5.1 is the tag name
# A file named qpid-dispatch-<tag-name>.tar.gz will be created at <output_folder-full-path>

# Simply running ./export.sh will put the tar.gz file in the current folder and use the very latest createed tag 

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
echo Using tag ${TAG} to create archive

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
    ARCHIVE=$DIR/qpid-dispatch-${VERSION}.tar.gz
    PREFIX=qpid-dispatch-${VERSION}
    [ -d ${WORKDIR} ] || mkdir -p ${WORKDIR}
    git archive --format=tar --prefix=${PREFIX}/ tags/${TAG} \
        | tar -x -C ${WORKDIR}
    cd ${WORKDIR}
    tar -c -z \
        --owner=root --group=root --numeric-owner \
        --mtime="${MTIME}" \
        -f ${ARCHIVE} ${PREFIX}
    echo Created "${ARCHIVE}"
    echo Success!!!
)

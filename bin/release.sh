#!/usr/bin/bash
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
#
# Script to pull together an Apache Release
#

ME=$(basename $0)

usage() {
    cat <<-EOF
USAGE: ${ME} [options] BRANCH VERSION
Creates an Apache release tarball.

Mandatory arguments:
  BRANCH   The git branch/tag for the build
  VERSION  The release version.

Optional arguments:
  -h       This help screen
EOF
}

while getopts "h" opt; do
    case $opt in
        h)
            usage
            exit 0
            ;;

        \?)
            echo "Invalid option: -$OPTARG" >&2
            usage
            exit 1
            ;;

        :)
            echo "Option -$OPTARG requires an argument." >&2
            usage
            exit 1
            ;;
    esac
done

BRANCH=${1-}
VERSION=${2-}

if [[ -z "$BRANCH" ]] || [[ -z "$VERSION" ]]; then
    printf "Missing one or more required argument.\n\n" >&2
    usage
    exit 1
fi

URL=https://git-wip-us.apache.org/repos/asf/qpid-dispatch.git

WORKDIR=$(mktemp -d)
BASENAME=qpid-dispatch-${VERSION}
GITDIR=$WORKDIR/$BASENAME
FILENAME=$PWD/${BASENAME}.tar.gz

if [ -f $FILENAME ]; then rm -f $FILENAME; fi

echo "Checking out to ${WORKDIR}..."
cd $WORKDIR
git clone $URL $BASENAME >/dev/null || exit 1
cd $GITDIR
git checkout $BRANCH >/dev/null || exit 1

BUILD_VERSION=$(cat $GITDIR/VERSION.txt) || exit 1
test "$VERSION" == "$BUILD_VERSION" || {
    echo "Version mismatch: $VERSION != $BUILD_VERSION. Please update VERSION.txt in the source"
    exit 1
}

echo "Building source tarball $FILENAME"
cd $WORKDIR
tar --exclude release.sh --exclude .git --exclude .gitignore -zcvf $FILENAME ${BASENAME} >/dev/null || exit 1

echo "Done!"

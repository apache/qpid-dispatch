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

# Based on https://github.com/GiovanniBussi/macports-ci

set -Exeuo pipefail

export COLUMNS=80

MACPORTS_VERSION=2.6.4
MACPORTS_PREFIX=/opt/local
MACPORTS_SYNC=tarball

pushd "$(mktemp -d)"

OSX_VERSION="$(sw_vers -productVersion | sed 's/\.[^\.]*$//')"

if test "$OSX_VERSION" == 10.10 ; then
  OSX_NAME=Yosemite
elif test "$OSX_VERSION" == 10.11 ; then
  OSX_NAME=ElCapitan
elif test "$OSX_VERSION" == 10.12 ; then
  OSX_NAME=Sierra
elif test "$OSX_VERSION" == 10.13 ; then
  OSX_NAME=HighSierra
elif test "$OSX_VERSION" == 10.14 ; then
  OSX_NAME=Mojave
else
  echo "macports-ci: Unknown OSX version $OSX_VERSION"
  exit 1
fi

echo "macports-ci: OSX version $OSX_VERSION $OSX_NAME"

MACPORTS_PKG=MacPorts-${MACPORTS_VERSION}-${OSX_VERSION}-${OSX_NAME}.pkg

URL="https://distfiles.macports.org/MacPorts"
URL="https://github.com/macports/macports-base/releases/download/v$MACPORTS_VERSION/"

echo "macports-ci: Base URL is $URL"

# download installer:
curl -LO $URL/$MACPORTS_PKG
# install:
sudo installer -verbose -pkg $MACPORTS_PKG -target /

# update:
export PATH="$MACPORTS_PREFIX/bin:$PATH"

i=1
# run through a while to retry upon failure
while true
do
  echo "macports-ci: Trying to selfupdate (iteration $i)"
# here I test for the presence of a known portfile
# this check confirms that ports were installed
# notice that port -N selfupdate && break is not sufficient as a test
# (sometime it returns a success even though ports have not been installed)
# for some misterious reasons, running without "-d" does not work in some case
  sudo port -d -N selfupdate 2>&1 | grep -v DEBUG | awk '{if($1!="x")print}'
  port info xdrfile > /dev/null && break || true
  sleep 5
  i=$((i+1))
  if ((i>20)) ; then
    echo "macports-ci: Failed after $i iterations"
    exit 1
  fi
done

echo "macports-ci: Selfupdate successful after $i iterations"

dir="$PWD"
popd
sudo rm -rf $dir

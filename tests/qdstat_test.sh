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

set -e

tmpdir=$(mktemp -d)
randport=$(python -c "import random; print random.randint(49152, 65535)")

echo "listener {
  addr: localhost
  port: $randport
  sasl-mechanisms: ANONYMOUS
}" > $tmpdir/conf

qdrouterd -c $tmpdir/conf &

pid=$!

qdstat --help > /dev/null
qdstat --bus localhost:$randport --general > /dev/null
qdstat --bus localhost:$randport --connections > /dev/null
qdstat --bus localhost:$randport --links > /dev/null
qdstat --bus localhost:$randport --nodes > /dev/null
qdstat --bus localhost:$randport --address > /dev/null
qdstat --bus localhost:$randport --memory > /dev/null

kill $pid
wait $pid

rm -rf $tmpdir

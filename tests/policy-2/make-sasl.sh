#!/bin/bash -ex
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
# Generate sasl files for policy tests using test setup for 'photoserver'
# This file is used to generate the files which are then committed to git
# and distributed in the make install.
#
# sasldb file is generated and copied to tests/policy-2/ by cmake
#
export sasl_file=./policy-photoserver-sasl.sasldb

echo password | saslpasswd2 -c -p -f ${sasl_file} -u qdrouterd anonymous
echo password | saslpasswd2 -c -p -f ${sasl_file} -u qdrouterd u1
echo password | saslpasswd2 -c -p -f ${sasl_file} -u qdrouterd u2
echo password | saslpasswd2 -c -p -f ${sasl_file} -u qdrouterd p1
echo password | saslpasswd2 -c -p -f ${sasl_file} -u qdrouterd p2
echo password | saslpasswd2 -c -p -f ${sasl_file} -u qdrouterd zeke
echo password | saslpasswd2 -c -p -f ${sasl_file} -u qdrouterd ynot
echo password | saslpasswd2 -c -p -f ${sasl_file} -u qdrouterd alice
echo password | saslpasswd2 -c -p -f ${sasl_file} -u qdrouterd bob
echo password | saslpasswd2 -c -p -f ${sasl_file} -u qdrouterd ellen
echo password | saslpasswd2 -c -p -f ${sasl_file} -u qdrouterd charlie

echo password | saslpasswd2 -c -p -f ${sasl_file} anonymous
echo password | saslpasswd2 -c -p -f ${sasl_file} u1
echo password | saslpasswd2 -c -p -f ${sasl_file} u2
echo password | saslpasswd2 -c -p -f ${sasl_file} p1
echo password | saslpasswd2 -c -p -f ${sasl_file} p2
echo password | saslpasswd2 -c -p -f ${sasl_file} zeke
echo password | saslpasswd2 -c -p -f ${sasl_file} ynot
echo password | saslpasswd2 -c -p -f ${sasl_file} alice
echo password | saslpasswd2 -c -p -f ${sasl_file} bob
echo password | saslpasswd2 -c -p -f ${sasl_file} ellen
echo password | saslpasswd2 -c -p -f ${sasl_file} charlie

sasldblistusers2                  -f ${sasl_file}

# Make sasl conf file
# sasl.conf is generated and 'config'd by cmake
cat > ./policy-photoserver-sasl.conf.in << "EOF"
pwcheck_method: auxprop
auxprop_plugin: sasldb
sasldb_path: ${CMAKE_CURRENT_BINARY_DIR}/policy-2/policy-photoserver-sasl.sasldb
mech_list: PLAIN ANONYMOUS
EOF

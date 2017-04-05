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
# under the License
#

rm -f *.pem *.pkcs12 *.p12

export SERVER=localhost
export CLIENT=127.0.0.1
export KEYARGS="-storetype pkcs12 -keyalg RSA -keysize 2048 -sigalg sha384WithRSA"

keytool $KEYARGS -keystore ca.pkcs12 -storepass ca-password -alias ca -keypass ca-password -genkey -dname "O=Trust Me Inc.,CN=Trusted.CA.com" -validity 99999 -ext bc:c=ca:true,pathlen:0  -startdate -1m
openssl pkcs12 -nokeys -passin pass:ca-password -in ca.pkcs12 -passout pass:ca-password -out ca-certificate.pem

keytool $KEYARGS -keystore bad-ca.pkcs12 -storepass bad-ca-password -alias bad-ca -keypass bad-ca-password -genkey -dname "O=Trust Me Inc.,CN=Trusted.CA.com" -validity 99999
openssl pkcs12 -nokeys -passin pass:bad-ca-password -in bad-ca.pkcs12 -passout pass:bad-ca-password -out bad-ca-certificate.pem

keytool $KEYARGS -keystore server.pkcs12 -storepass server-password -alias server-certificate -keypass server-password -genkey  -dname "O=Server,CN=$SERVER" -validity 99999
keytool $KEYARGS -keystore server.pkcs12 -storepass server-password -alias server-certificate -keypass server-password -certreq -file server-request.pem
keytool $KEYARGS -keystore ca.pkcs12 -storepass ca-password -alias ca -keypass ca-password -gencert -rfc -validity 99999 -infile server-request.pem -outfile server-certificate.pem
openssl pkcs12 -nocerts -passin pass:server-password -in server.pkcs12 -passout pass:server-password -out server-private-key.pem

# Generate a PKCS12 key which will be used for client side cert
keytool $KEYARGS -keystore client.pkcs12 -storepass client-password -alias client-certificate -keypass client-password -genkey  -dname "C=US,ST=NC,L=Raleigh,OU=Dev,O=Client,CN=$CLIENT" -validity 99999
# Create a certificate request file
keytool $KEYARGS -keystore client.pkcs12 -storepass client-password -alias client-certificate -keypass client-password -certreq -file client-request.pem
# Create a client certificate
keytool $KEYARGS -keystore ca.pkcs12 -storepass ca-password -alias ca -keypass ca-password -gencert -rfc -validity 99999 -infile client-request.pem -outfile client-certificate.pem
# Create a client private key file.
openssl pkcs12 -nocerts -passin pass:client-password -in client.pkcs12 -passout pass:client-password -out client-private-key.pem


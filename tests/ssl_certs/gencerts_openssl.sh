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

# Creates a root CA and intermediate CA and creates password protected server and client certificates using openssl commands

##### Create root CA #####
# Create a password protected private key for root CA
openssl genrsa -aes256 -passout pass:ca-password -out ca-private-key.pem 4096

# Use the private key to create a root CA cert
openssl req -key ca-private-key.pem -new -x509 -days 99999 -sha256 -out ca-certificate.pem -passin pass:ca-password -subj "/C=US/ST=New York/L=Brooklyn/O=Trust Me Inc./CN=Trusted.CA.com"



##### Create an intermediate CA #####
# Create a password protected private key for the intermediate CA
openssl genrsa -aes256 -passout pass:intermediate-ca-password -out intermediate-ca-private-key.pem 4096

# Create a CSR using the private key created from the previous step
openssl req -new -key intermediate-ca-private-key.pem -passin pass:intermediate-ca-password -out intermediate.csr -subj "/C=US/ST=FL/L=Miami/O=Server/CN=Trusted.IntermediateCA.com"

# Create the intermediate signed certificate signed by the root CA
# Note here that the v3_ca.ext file sets basicConstraints=critical, CA:true which means that the issued certificate is for a Certificate Authority, in this case an intermediate CA
# and this certificate must not be used to create further CA certificates
openssl x509 -req -in intermediate.csr -CA ca-certificate.pem -CAkey ca-private-key.pem -CAcreateserial -days 9999 -out intermediate-ca-certificate.pem -passin pass:ca-password -extfile v3_ca.ext

# Concatenate the intermediate-ca-certificate.pem and ca-certificate.pem to form the ca-chain-cert.pem
cat ca-certificate.pem intermediate-ca-certificate.pem > ca-chain-cert.pem



##### Create a server certificate signed by the intermediate CA #####
# Create a password protected server private key which will be used to create the server certificate
openssl genrsa -aes256 -passout pass:server-password -out server-private-key.pem 4096

# Create a CSR using the private key created from the previous step
openssl req -new -key server-private-key.pem -passin pass:server-password -out server.csr -subj "/C=US/ST=CA/L=San Francisco/O=Server/CN=server.com"

# Now the CSR has been created and must be sent to the CA.
# The intermediate CA receives the CSR and runs this command to create a server certificate (server-certificate.pem)
openssl x509 -req -in server.csr -CA intermediate-ca-certificate.pem -CAkey intermediate-ca-private-key.pem -CAcreateserial -days 9999 -out server-certificate.pem -passin pass:intermediate-ca-password



##### Create a client certificate signed by the root CA #####
# Create a password protected client private key which will be used to create the client certificate
openssl genrsa -aes256 -passout pass:client-password -out client-private-key.pem 4096

# Create a CSR using the client private key created from the previous step
openssl req -new -key client-private-key.pem -passin pass:client-password -out client.csr -subj "/C=US/ST=CA/L=San Francisco/O=Client/CN=client.com"

# Now the CSR has been created and must be sent to the CA.
# The root CA receives the CSR and runs this command to create a client certificate (client_certificate.pem)
openssl x509 -req -in client.csr -CA intermediate-ca-certificate.pem -CAkey intermediate-ca-private-key.pem -CAcreateserial -days 9999 -out client-certificate.pem -passin pass:intermediate-ca-password


# Verify the certs with the cert chain
openssl verify -verbose -CAfile ca-chain-cert.pem server-certificate.pem
openssl verify -verbose -CAfile ca-chain-cert.pem client-certificate.pem
openssl verify -verbose -CAfile ca-chain-cert.pem intermediate-ca-certificate.pem
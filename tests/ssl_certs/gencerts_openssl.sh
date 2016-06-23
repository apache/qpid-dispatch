#!/bin/bash -ex

# Creates a root CA and creates password protected server and client certificates using openssl commands

##### Create root CA #####
# Create a password protected private key for root CA
openssl genrsa -aes256 -passout pass:ca-password -out ca-private-key.pem 4096

# Use the private key to create a root CA cert
openssl req -key ca-private-key.pem -new -x509 -days 99999 -sha256 -out ca-certificate.pem -passin pass:ca-password -subj "/C=US/ST=New York/L=Brooklyn/O=Trust Me Inc./CN=Trusted.CA.com"

##### Create a server certificate signed by the root CA #####
# Create a password protected server private key which will be used to create the server certificate
openssl genrsa -aes256 -passout pass:server-password -out server-private-key.pem 4096

# Create a CSR using the private key created from the previous step
openssl req -new -key server-private-key.pem -passin pass:server-password -out server.csr -subj "/C=US/ST=CA/L=San Francisco/O=Server/CN=127.0.0.1"

# Now the CSR has been created and must be sent to the CA.
# The root CA receives the CSR and runs this command to create a server certificate (server-certificate.pem)
openssl x509 -req -in server.csr -CA ca-certificate.pem -CAkey ca-private-key.pem -CAcreateserial -days 9999 -out server-certificate.pem -passin pass:ca-password


##### Create a client certificate signed by the root CA #####
# Create a password protected client private key which will be used to create the client certificate
openssl genrsa -aes256 -passout pass:client-password -out client-private-key.pem 4096

# Create a CSR using the client private key created from the previous step
openssl req -new -key client-private-key.pem -passin pass:client-password -out client.csr -subj "/C=US/ST=CA/L=San Francisco/O=Client/CN=127.0.0.1"

# Now the CSR has been created and must be sent to the CA.
# The root CA receives the CSR and runs this command to create a client certificate (client_certificate.pem)
openssl x509 -req -in client.csr -CA ca-certificate.pem -CAkey ca-private-key.pem -CAcreateserial -days 9999 -out client-certificate.pem -passin pass:ca-password
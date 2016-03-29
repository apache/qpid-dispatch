#!/bin/bash -ex

rm -f *.pem *.pkcs12 *.p12

export SERVER=A1.Good.Server.domain.com
export CLIENT=127.0.0.1

keytool -storetype pkcs12 -keystore ca.pkcs12 -storepass ca-password -alias ca -keypass ca-password -genkey -dname "O=Trust Me Inc.,CN=Trusted.CA.com" -validity 99999
openssl pkcs12 -nokeys -passin pass:ca-password -in ca.pkcs12 -passout pass:ca-password -out ca-certificate.pem

keytool -storetype pkcs12 -keystore bad-ca.pkcs12 -storepass bad-ca-password -alias bad-ca -keypass bad-ca-password -genkey -dname "O=Trust Me Inc.,CN=Trusted.CA.com" -validity 99999
openssl pkcs12 -nokeys -passin pass:bad-ca-password -in bad-ca.pkcs12 -passout pass:bad-ca-password -out bad-ca-certificate.pem

keytool -storetype pkcs12 -keystore server.pkcs12 -storepass server-password -alias server-certificate -keypass server-password -genkey  -dname "O=Server,CN=$SERVER" -validity 99999
keytool -storetype pkcs12 -keystore server.pkcs12 -storepass server-password -alias server-certificate -keypass server-password -certreq -file server-request.pem
keytool -storetype pkcs12 -keystore ca.pkcs12 -storepass ca-password -alias ca -keypass ca-password -gencert -rfc -validity 99999 -infile server-request.pem -outfile server-certificate.pem
openssl pkcs12 -nocerts -passin pass:server-password -in server.pkcs12 -passout pass:server-password -out server-private-key.pem

# Generate a PKCS12 key which will be used for client side cert
keytool -storetype pkcs12 -keystore client.pkcs12 -storepass client-password -alias client-certificate -keypass client-password -genkey  -dname "C=US,ST=NC,L=Raleigh,OU=Dev,O=Client,CN=$CLIENT" -validity 99999
# Create a certificate request file
keytool -storetype pkcs12 -keystore client.pkcs12 -storepass client-password -alias client-certificate -keypass client-password -certreq -file client-request.pem
# Create a client certificate
keytool -storetype pkcs12 -keystore ca.pkcs12 -storepass ca-password -alias ca -keypass ca-password -gencert -rfc -validity 99999 -infile client-request.pem -outfile client-certificate.pem
# Create a client private key file.
openssl pkcs12 -nocerts -passin pass:client-password -in client.pkcs12 -passout pass:client-password -out client-private-key.pem


##########################################################################################################################

#keytool -storetype pkcs12 -keystore client1.pkcs12 -storepass client-password -alias client-certificate1 -keypass client-password -genkey  -dname "O=Client,CN=$CLIENT" -validity 99999
#keytool -storetype pkcs12 -keystore client1.pkcs12 -storepass client-password -alias client-certificate1 -keypass client-password -certreq -file client-request1.pem
#keytool -storetype pkcs12 -keystore ca.pkcs12 -storepass ca-password -alias ca -keypass ca-password -gencert -rfc -validity 99999 -infile client-request1.pem -outfile client-certificate1.pem
#openssl pkcs12 -nocerts -passin pass:client-password -in client1.pkcs12 -passout pass:client-password -out client-private-key1.pem

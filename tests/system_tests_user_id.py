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

import os
from system_test import TestCase, Qdrouterd, DIR, main_module
from system_test import unittest
from qpid_dispatch.management.client import Node
from proton import SSLDomain


class QdSSLUseridTest(TestCase):

    @staticmethod
    def ssl_file(name):
        return os.path.join(DIR, 'ssl_certs', name)

    @classmethod
    def setUpClass(cls):
        super(QdSSLUseridTest, cls).setUpClass()

        os.environ["TLS_SERVER_PASSWORD"] = "server-password"

        ssl_profile1_json = os.path.join(DIR, 'displayname_files', 'profile_names1.json')
        ssl_profile2_json = os.path.join(DIR, 'displayname_files', 'profile_names2.json')

        config = Qdrouterd.Config([
            ('router', {'id': 'QDR', 'workerThreads': 1}),

            # sha1
            ('sslProfile', {'name': 'server-ssl1',
                            'caCertFile': cls.ssl_file('ca-certificate.pem'),
                            'certFile': cls.ssl_file('server-certificate.pem'),
                            'privateKeyFile': cls.ssl_file('server-private-key.pem'),
                            'uidFormat': '1',
                            'password': 'server-password'}),

            # sha256
            ('sslProfile', {'name': 'server-ssl2',
                            'caCertFile': cls.ssl_file('ca-certificate.pem'),
                            'certFile': cls.ssl_file('server-certificate.pem'),
                            'privateKeyFile': cls.ssl_file('server-private-key.pem'),
                            'uidFormat': '2',
                            'password': 'server-password'}),

            # sha512
            ('sslProfile', {'name': 'server-ssl3',
                            'caCertFile': cls.ssl_file('ca-certificate.pem'),
                            'certFile': cls.ssl_file('server-certificate.pem'),
                            'privateKeyFile': cls.ssl_file('server-private-key.pem'),
                            'uidFormat': '5',
                            'password': 'server-password'}),

            # sha256 combination
            ('sslProfile', {'name': 'server-ssl4',
                            'caCertFile': cls.ssl_file('ca-certificate.pem'),
                            'certFile': cls.ssl_file('server-certificate.pem'),
                            'privateKeyFile': cls.ssl_file('server-private-key.pem'),
                            'uidFormat': '2noucs',
                            'password': 'server-password'}),

            # sha1 combination
            ('sslProfile', {'name': 'server-ssl5',
                            'caCertFile': cls.ssl_file('ca-certificate.pem'),
                            'certFile': cls.ssl_file('server-certificate.pem'),
                            'privateKeyFile': cls.ssl_file('server-private-key.pem'),
                            'uidFormat': '1cs',
                            'password': 'server-password'}),

            # sha512 combination
            ('sslProfile', {'name': 'server-ssl6',
                            'caCertFile': cls.ssl_file('ca-certificate.pem'),
                            'certFile': cls.ssl_file('server-certificate.pem'),
                            'privateKeyFile': cls.ssl_file('server-private-key.pem'),
                            'uidFormat': 'cs5',
                            # Use the env: prefix TLS_SERVER_PASSWORD. The TLS_SERVER_PASSWORD
                            # is set to 'server-password'
                            'password': 'env:TLS_SERVER_PASSWORD'}),

            # no fingerprint field
            ('sslProfile', {'name': 'server-ssl7',
                            'caCertFile': cls.ssl_file('ca-certificate.pem'),
                            'certFile': cls.ssl_file('server-certificate.pem'),
                            'privateKeyFile': cls.ssl_file('server-private-key.pem'),
                            'uidFormat': 'nsuco',
                            'password': 'server-password'}),

            # no fingerprint field variation
            ('sslProfile', {'name': 'server-ssl8',
                            'caCertFile': cls.ssl_file('ca-certificate.pem'),
                            'certFile': cls.ssl_file('server-certificate.pem'),
                            'privateKeyFile': cls.ssl_file('server-private-key.pem'),
                            'uidFormat': 'scounl',
                            'password': 'server-password'}),

            # no uidFormat
            ('sslProfile', {'name': 'server-ssl9',
                            'caCertFile': cls.ssl_file('ca-certificate.pem'),
                            'certFile': cls.ssl_file('server-certificate.pem'),
                            'privateKeyFile': cls.ssl_file('server-private-key.pem'),
                            # Use the prefix 'file:' for the password. This should read the file and
                            # use the password from the file.
                            'password': 'file:' + cls.ssl_file('server-password-file.txt')}),

            # one component of uidFormat is invalid (x), this will result in an error in the fingerprint calculation.
            # The user_id will fall back to proton's pn_transport_get_user
            ('sslProfile', {'name': 'server-ssl10',
                            'caCertFile': cls.ssl_file('ca-certificate.pem'),
                            'certFile': cls.ssl_file('server-certificate.pem'),
                            'privateKeyFile': cls.ssl_file('server-private-key.pem'),
                            'uidFormat': '1x',
                            'uidNameMappingFile': ssl_profile2_json,
                            'password': 'server-password'}),

            # All components in the uidFormat are unrecognized, pn_get_transport_user will be returned
            ('sslProfile', {'name': 'server-ssl11',
                            'caCertFile': cls.ssl_file('ca-certificate.pem'),
                            'certFile': cls.ssl_file('server-certificate.pem'),
                            'privateKeyFile': cls.ssl_file('server-private-key.pem'),
                            'uidFormat': 'abxd',
                            # Use the prefix 'literal:'. This makes sure we maintain
                            # backward compatability
                            'password': 'literal:server-password'}),

            ('sslProfile', {'name': 'server-ssl12',
                            'caCertFile': cls.ssl_file('ca-certificate.pem'),
                            'certFile': cls.ssl_file('server-certificate.pem'),
                            'privateKeyFile': cls.ssl_file('server-private-key.pem'),
                            'uidFormat': '1',
                            'uidNameMappingFile': ssl_profile1_json,
                            # Use the pass: followed by the actual password
                            'password': 'pass:server-password'}),

            # should translate a display name
            # specifying both passwordFile and password, password takes precedence.
            ('sslProfile', {'name': 'server-ssl13',
                            'caCertFile': cls.ssl_file('ca-certificate.pem'),
                            'certFile': cls.ssl_file('server-certificate.pem'),
                            'privateKeyFile': cls.ssl_file('server-private-key.pem'),
                            'uidFormat': '2',
                            'uidNameMappingFile': ssl_profile2_json,
                            'password': 'server-password',
                            # If both passwordFile and password are provided, the passwordFile is ignored.
                            # Here we specify a bad password in the passwordFile and
                            # it is ignored.
                            'passwordFile': cls.ssl_file('server-password-file-bad.txt')}),

            ('sslProfile', {'name': 'server-ssl14',
                            'caCertFile': cls.ssl_file('ca-certificate.pem'),
                            'certFile': cls.ssl_file('server-certificate.pem'),
                            'privateKeyFile': cls.ssl_file('server-private-key.pem'),
                            'uidFormat': '1',
                            'uidNameMappingFile': ssl_profile1_json,
                            # Use just the deprecated passwordFile entity to make sure it is backward
                            # compatible.
                            'passwordFile': cls.ssl_file('server-password-file.txt')}),

            ('listener', {'port': cls.tester.get_port(), 'sslProfile': 'server-ssl1', 'authenticatePeer': 'yes',
                          'requireSsl': 'yes', 'saslMechanisms': 'EXTERNAL'}),

            ('listener', {'port': cls.tester.get_port(), 'sslProfile': 'server-ssl2', 'authenticatePeer': 'yes',
                          'requireSsl': 'yes', 'saslMechanisms': 'EXTERNAL'}),

            ('listener', {'port': cls.tester.get_port(), 'sslProfile': 'server-ssl3', 'authenticatePeer': 'yes',
                          'requireSsl': 'yes', 'saslMechanisms': 'EXTERNAL'}),

            ('listener', {'port': cls.tester.get_port(), 'sslProfile': 'server-ssl4', 'authenticatePeer': 'yes',
                          'requireSsl': 'yes', 'saslMechanisms': 'EXTERNAL'}),

            ('listener', {'port': cls.tester.get_port(), 'sslProfile': 'server-ssl5', 'authenticatePeer': 'yes',
                          'requireSsl': 'yes', 'saslMechanisms': 'EXTERNAL'}),

            ('listener', {'port': cls.tester.get_port(), 'sslProfile': 'server-ssl6', 'authenticatePeer': 'yes',
                          'requireSsl': 'yes', 'saslMechanisms': 'EXTERNAL'}),

            ('listener', {'port': cls.tester.get_port(), 'sslProfile': 'server-ssl7', 'authenticatePeer': 'yes',
                          'requireSsl': 'yes', 'saslMechanisms': 'EXTERNAL'}),

            ('listener', {'port': cls.tester.get_port(), 'sslProfile': 'server-ssl8', 'authenticatePeer': 'yes',
                          'requireSsl': 'yes', 'saslMechanisms': 'EXTERNAL'}),

            ('listener', {'port': cls.tester.get_port(), 'sslProfile': 'server-ssl9', 'authenticatePeer': 'yes',
                          'requireSsl': 'yes', 'saslMechanisms': 'EXTERNAL'}),

            ('listener', {'port': cls.tester.get_port(), 'sslProfile': 'server-ssl10', 'authenticatePeer': 'yes',
                          'requireSsl': 'yes', 'saslMechanisms': 'EXTERNAL'}),

            ('listener', {'port': cls.tester.get_port(), 'sslProfile': 'server-ssl11', 'authenticatePeer': 'yes',
                          'requireSsl': 'yes', 'saslMechanisms': 'EXTERNAL'}),

            # peer is not being authenticated here. the user must be "anonymous" which is what pn_transport_get_user
            # returns
            ('listener', {'port': cls.tester.get_port(), 'sslProfile': 'server-ssl12', 'authenticatePeer': 'no',
                          'requireSsl': 'yes', 'saslMechanisms': 'ANONYMOUS'}),

            ('listener', {'port': cls.tester.get_port(), 'sslProfile': 'server-ssl13', 'authenticatePeer': 'yes',
                          'requireSsl': 'yes', 'saslMechanisms': 'EXTERNAL'}),

            ('listener', {'port': cls.tester.get_port(), 'sslProfile': 'server-ssl14', 'authenticatePeer': 'yes',
                          'requireSsl': 'yes', 'saslMechanisms': 'EXTERNAL'}),

            ('listener', {'port': cls.tester.get_port(), 'authenticatePeer': 'no'})

        ])

        cls.router = cls.tester.qdrouterd('ssl-test-router', config, wait=True)

    def address(self, index):
        return self.router.addresses[index]

    def create_ssl_domain(self, ssl_options_dict, mode=SSLDomain.MODE_CLIENT):
        """Return proton.SSLDomain from command line options or None if no SSL options specified."""
        certificate, key, trustfile, password = ssl_options_dict.get('ssl-certificate'), \
            ssl_options_dict.get('ssl-key'), \
            ssl_options_dict.get('ssl-trustfile'), \
            ssl_options_dict.get('ssl-password')

        if not (certificate or trustfile):
            return None
        domain = SSLDomain(mode)
        if trustfile:
            domain.set_trusted_ca_db(str(trustfile))
        domain.set_peer_authentication(SSLDomain.VERIFY_PEER, None)
        if certificate:
            domain.set_credentials(str(certificate), str(key), str(password))

        return domain

    def test_ssl_user_id(self):
        ssl_opts = dict()
        ssl_opts['ssl-trustfile'] = self.ssl_file('ca-certificate.pem')
        ssl_opts['ssl-certificate'] = self.ssl_file('client-certificate.pem')
        ssl_opts['ssl-key'] = self.ssl_file('client-private-key.pem')
        ssl_opts['ssl-password'] = 'client-password'

        # create the SSL domain object
        domain = self.create_ssl_domain(ssl_opts)

        addr = self.address(0).replace("amqp", "amqps")

        node = Node.connect(addr, ssl_domain=domain)
        user_id = node.query(type='org.apache.qpid.dispatch.connection', attribute_names=['user']).results[0][0]
        self.assertEqual("3eccbf1a2f3e46da823c63a9da9158983cb495a3", user_id)

        addr = self.address(1).replace("amqp", "amqps")
        node = Node.connect(addr, ssl_domain=domain)
        self.assertEqual("72d543690cb0a8fc2d0f4c704c65411b9ee8ad53839fced4c720d73e58e4f0d7",
                         node.query(type='org.apache.qpid.dispatch.connection', attribute_names=['user']).results[1][0])

        addr = self.address(2).replace("amqp", "amqps")
        node = Node.connect(addr, ssl_domain=domain)
        self.assertEqual("c6de3a340014b0f8a1d2b41d22e414fc5756494ffa3c8760bbff56f3aa9f179a5a6eae09413fd7a6afbf36b5fb4bad8795c2836774acfe00a701797cc2a3a9ab",
                         node.query(type='org.apache.qpid.dispatch.connection', attribute_names=['user']).results[2][0])

        addr = self.address(3).replace("amqp", "amqps")
        node = Node.connect(addr, ssl_domain=domain)
        self.assertEqual("72d543690cb0a8fc2d0f4c704c65411b9ee8ad53839fced4c720d73e58e4f0d7;127.0.0.1;Client;Dev;US;NC",
                         node.query(type='org.apache.qpid.dispatch.connection', attribute_names=['user']).results[3][0])

        addr = self.address(4).replace("amqp", "amqps")
        node = Node.connect(addr, ssl_domain=domain)
        self.assertEqual("3eccbf1a2f3e46da823c63a9da9158983cb495a3;US;NC",
                         node.query(type='org.apache.qpid.dispatch.connection', attribute_names=['user']).results[4][0])

        addr = self.address(5).replace("amqp", "amqps")
        node = Node.connect(addr, ssl_domain=domain)
        self.assertEqual("US;NC;c6de3a340014b0f8a1d2b41d22e414fc5756494ffa3c8760bbff56f3aa9f179a5a6eae09413fd7a6afbf36b5fb4bad8795c2836774acfe00a701797cc2a3a9ab",
                         node.query(type='org.apache.qpid.dispatch.connection', attribute_names=['user']).results[5][0])

        addr = self.address(6).replace("amqp", "amqps")
        node = Node.connect(addr, ssl_domain=domain)
        self.assertEqual("127.0.0.1;NC;Dev;US;Client",
                         node.query(type='org.apache.qpid.dispatch.connection', attribute_names=['user']).results[6][0])

        addr = self.address(7).replace("amqp", "amqps")
        node = Node.connect(addr, ssl_domain=domain)
        self.assertEqual("NC;US;Client;Dev;127.0.0.1;Raleigh",
                         node.query(type='org.apache.qpid.dispatch.connection', attribute_names=['user']).results[7][0])

        addr = self.address(8).replace("amqp", "amqps")
        node = Node.connect(addr, ssl_domain=domain)
        self.assertEqual("C=US,ST=NC,L=Raleigh,OU=Dev,O=Client,CN=127.0.0.1",
                         node.query(type='org.apache.qpid.dispatch.connection', attribute_names=['user']).results[8][0])

        addr = self.address(9).replace("amqp", "amqps")
        node = Node.connect(addr, ssl_domain=domain)
        self.assertEqual("C=US,ST=NC,L=Raleigh,OU=Dev,O=Client,CN=127.0.0.1",
                         node.query(type='org.apache.qpid.dispatch.connection', attribute_names=['user']).results[9][0])

        addr = self.address(10).replace("amqp", "amqps")
        node = Node.connect(addr, ssl_domain=domain)
        user = node.query(type='org.apache.qpid.dispatch.connection', attribute_names=['user']).results[10][0]
        self.assertEqual("C=US,ST=NC,L=Raleigh,OU=Dev,O=Client,CN=127.0.0.1", str(user))

        # authenticatePeer is set to 'no' in this listener, the user should anonymous on the connection.
        addr = self.address(11).replace("amqp", "amqps")
        node = Node.connect(addr, ssl_domain=domain)
        user = node.query(type='org.apache.qpid.dispatch.connection', attribute_names=['user']).results[11][0]
        self.assertEqual("anonymous", user)

        addr = self.address(12).replace("amqp", "amqps")
        node = Node.connect(addr, ssl_domain=domain)
        user = node.query(type='org.apache.qpid.dispatch.connection', attribute_names=['user']).results[12][0]
        self.assertEqual("user12", str(user))

        addr = self.address(13).replace("amqp", "amqps")
        node = Node.connect(addr, ssl_domain=domain)
        user_id = node.query(type='org.apache.qpid.dispatch.connection', attribute_names=['user']).results[13][0]
        self.assertEqual("user13", user_id)

        node.close()


if __name__ == '__main__':
    unittest.main(main_module())

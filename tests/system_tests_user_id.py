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
import unittest
from time import sleep
from system_test import TestCase, Qdrouterd, DIR, main_module
from qpid_dispatch.management.client import Node
from proton import SSLDomain, Message

class QdSSLUseridTest(TestCase):

    @staticmethod
    def ssl_file(name):
        return os.path.join(DIR, 'ssl_certs', name)

    @classmethod
    def setUpClass(cls):
        super(QdSSLUseridTest, cls).setUpClass()

        ssl_profile1_json = os.path.join(DIR, 'displayname_files', 'profile_names1.json')
        ssl_profile2_json = os.path.join(DIR, 'displayname_files', 'profile_names2.json')

        config = Qdrouterd.Config([
            ('router', {'id': 'QDR', 'workerThreads': 1}),

            # sha1
            ('sslProfile', {'name': 'server-ssl1',
                             'cert-db': cls.ssl_file('ca-certificate.pem'),
                             'cert-file': cls.ssl_file('server-certificate.pem'),
                             'key-file': cls.ssl_file('server-private-key.pem'),
                             'uidFormat': '1',
                             'password': 'server-password'}),

            # sha256
            ('sslProfile', {'name': 'server-ssl2',
                             'cert-db': cls.ssl_file('ca-certificate.pem'),
                             'cert-file': cls.ssl_file('server-certificate.pem'),
                             'key-file': cls.ssl_file('server-private-key.pem'),
                             'uidFormat': '2',
                             'password': 'server-password'}),

            # sha512
            ('sslProfile', {'name': 'server-ssl3',
                             'cert-db': cls.ssl_file('ca-certificate.pem'),
                             'cert-file': cls.ssl_file('server-certificate.pem'),
                             'key-file': cls.ssl_file('server-private-key.pem'),
                             'uidFormat': '5',
                             'password': 'server-password'}),

            # sha256 combination
            ('sslProfile', {'name': 'server-ssl4',
                             'cert-db': cls.ssl_file('ca-certificate.pem'),
                             'cert-file': cls.ssl_file('server-certificate.pem'),
                             'key-file': cls.ssl_file('server-private-key.pem'),
                             'uidFormat': '2noucs',
                             'password': 'server-password'}),

            # sha1 combination
            ('sslProfile', {'name': 'server-ssl5',
                             'cert-db': cls.ssl_file('ca-certificate.pem'),
                             'cert-file': cls.ssl_file('server-certificate.pem'),
                             'key-file': cls.ssl_file('server-private-key.pem'),
                             'uidFormat': '1cs',
                             'password': 'server-password'}),

            # sha512 combination
            ('sslProfile', {'name': 'server-ssl6',
                             'cert-db': cls.ssl_file('ca-certificate.pem'),
                             'cert-file': cls.ssl_file('server-certificate.pem'),
                             'key-file': cls.ssl_file('server-private-key.pem'),
                             'uidFormat': 'cs5',
                             'password': 'server-password'}),

            # no fingerprint field
            ('sslProfile', {'name': 'server-ssl7',
                             'cert-db': cls.ssl_file('ca-certificate.pem'),
                             'cert-file': cls.ssl_file('server-certificate.pem'),
                             'key-file': cls.ssl_file('server-private-key.pem'),
                             'uidFormat': 'nsuco',
                             'password': 'server-password'}),

            # no fingerprint field variation
            ('sslProfile', {'name': 'server-ssl8',
                             'cert-db': cls.ssl_file('ca-certificate.pem'),
                             'cert-file': cls.ssl_file('server-certificate.pem'),
                             'key-file': cls.ssl_file('server-private-key.pem'),
                             'uidFormat': 'scounl',
                             'password': 'server-password'}),

            #no uidFormat
            ('sslProfile', {'name': 'server-ssl9',
                             'cert-db': cls.ssl_file('ca-certificate.pem'),
                             'cert-file': cls.ssl_file('server-certificate.pem'),
                             'key-file': cls.ssl_file('server-private-key.pem'),
                             'password': 'server-password'}),

            # one component of uidFormat is invalid (x), the unrecognized component will be ignored,
            # this will be treated like 'uidFormat': '1'
            ('sslProfile', {'name': 'server-ssl10',
                             'cert-db': cls.ssl_file('ca-certificate.pem'),
                             'cert-file': cls.ssl_file('server-certificate.pem'),
                             'key-file': cls.ssl_file('server-private-key.pem'),
                             'uidFormat': '1x',
                             'displayNameFile': ssl_profile2_json,
                             'password': 'server-password'}),

            # All components in the uidFormat are unrecognized, pn_get_transport_user will be returned
            ('sslProfile', {'name': 'server-ssl11',
                             'cert-db': cls.ssl_file('ca-certificate.pem'),
                             'cert-file': cls.ssl_file('server-certificate.pem'),
                             'key-file': cls.ssl_file('server-private-key.pem'),
                             'uidFormat': 'abxd',
                             'password': 'server-password'}),

            ('sslProfile', {'name': 'server-ssl12',
                             'cert-db': cls.ssl_file('ca-certificate.pem'),
                             'cert-file': cls.ssl_file('server-certificate.pem'),
                             'key-file': cls.ssl_file('server-private-key.pem'),
                             'uidFormat': '1',
                             'displayNameFile': ssl_profile1_json,
                             'password': 'server-password'}),

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

            # peer is not being authenticated here. the user must "anonymous" which is what pn_transport_get_user
            # returns
            ('listener', {'port': cls.tester.get_port(), 'sslProfile': 'server-ssl12', 'authenticatePeer': 'no',
                          'requireSsl': 'yes', 'saslMechanisms': 'ANONYMOUS'}),

            ('listener', {'port': cls.tester.get_port(), 'authenticatePeer': 'no'})

        ])

        cls.router = cls.tester.qdrouterd('ssl-test-router', config, wait=True)

    def address(self, index):
        return self.router.addresses[index]

    def create_ssl_domain(self, ssl_options_dict, mode=SSLDomain.MODE_CLIENT):
        """Return proton.SSLDomain from command line options or None if no SSL options specified.
            @param opts: Parsed optoins including connection_options()
        """
        certificate, key, trustfile, password = ssl_options_dict.get('ssl-certificate'), \
                                                ssl_options_dict.get('ssl-key'), \
                                                ssl_options_dict.get('ssl-trustfile'), \
                                                ssl_options_dict.get('ssl-password')

        if not (certificate or trustfile):
            return None
        domain = SSLDomain(mode)
        if trustfile:
            domain.set_trusted_ca_db(str(trustfile))
            domain.set_peer_authentication(SSLDomain.VERIFY_PEER, str(trustfile))
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
        self.assertEqual("60f5dbd7ed14a5ea243785e81745ac8463494298",
                         user_id)

        addr = self.address(1).replace("amqp", "amqps")
        node = Node.connect(addr, ssl_domain=domain)
        self.assertEqual("7c87f0c974f9e1aa5cb98f13fae9675625f240c98034b888753140da28094879",
                         node.query(type='org.apache.qpid.dispatch.connection', attribute_names=['user']).results[1][0])

        addr = self.address(2).replace("amqp", "amqps")
        node = Node.connect(addr, ssl_domain=domain)
        self.assertEqual("82244216b6d02ffdfb886c8da3c803e0f7a7b330a7b665dccabd30bd25d0f35e2a4fff5f0a2a01d56eb7dbae085c108e71a32b84bab16c9ec243a1f6d014900d",
                         node.query(type='org.apache.qpid.dispatch.connection', attribute_names=['user']).results[2][0])

        addr = self.address(3).replace("amqp", "amqps")
        node = Node.connect(addr, ssl_domain=domain)
        self.assertEqual("7c87f0c974f9e1aa5cb98f13fae9675625f240c98034b888753140da28094879;127.0.0.1;Client;Dev;US;NC",
        node.query(type='org.apache.qpid.dispatch.connection', attribute_names=['user']).results[3][0])

        addr = self.address(4).replace("amqp", "amqps")
        node = Node.connect(addr, ssl_domain=domain)
        self.assertEqual("60f5dbd7ed14a5ea243785e81745ac8463494298;US;NC",
        node.query(type='org.apache.qpid.dispatch.connection', attribute_names=['user']).results[4][0])

        addr = self.address(5).replace("amqp", "amqps")
        node = Node.connect(addr, ssl_domain=domain)
        self.assertEqual("US;NC;82244216b6d02ffdfb886c8da3c803e0f7a7b330a7b665dccabd30bd25d0f35e2a4fff5f0a2a01d56eb7dbae085c108e71a32b84bab16c9ec243a1f6d014900d",
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

        addr = self.address(11).replace("amqp", "amqps")
        node = Node.connect(addr)
        user = node.query(type='org.apache.qpid.dispatch.connection', attribute_names=['user']).results[11][0]
        self.assertEqual("anonymous", user)

        M1 = self.messenger()
        M1.route("amqp:/*", self.address(12)+"/$1")

        subscription = M1.subscribe("amqp:/#")

        reply_to = subscription.address
        addr = 'amqp:/_local/$displayname'

        tm = Message()
        rm = Message()
        tm.address = addr
        tm.reply_to = reply_to
        tm.body = {'profilename': 'server-ssl10', 'opcode': 'QUERY', 'userid': '94745961c5646ee0129536b3acef1eea0d8d2f26f8c353455233027bcd47'}
        M1.put(tm)

        M1.send()
        M1.recv(1)
        M1.get(rm)
        self.assertEqual('elaine', rm.body['user_name'])

        tm = Message()
        rm = Message()
        tm.address = addr
        tm.reply_to = reply_to
        tm.body =  {'profilename': 'server-ssl14', 'opcode': 'QUERY', 'userid': '94745961c5646ee0129536b3acef1eea0d8d2f26f8c3ed08ece4f8f3027bcd48'}
        M1.put(tm)
        M1.send()
        M1.recv(1)
        M1.get(rm)
        self.assertEqual('94745961c5646ee0129536b3acef1eea0d8d2f26f8c3ed08ece4f8f3027bcd48', rm.body['user_name'])

        # The profile name, userid pair have a matching user name
        tm = Message()
        rm = Message()
        tm.address = addr
        tm.reply_to = reply_to
        tm.body = {'profilename': 'server-ssl12', 'opcode': 'QUERY', 'userid': '94745961c5646ee0129536b3acef1eea0d8d2f26f8c3ed08ece4f8f3027bcd48'}
        M1.put(tm)
        M1.send()
        M1.recv(1)
        M1.get(rm)
        self.assertEqual('johndoe', rm.body['user_name'])

        tm = Message()
        rm = Message()
        tm.address = addr
        tm.reply_to = reply_to
        tm.body =  {'profilename': 'server-ssl10', 'opcode': 'QUERY', 'userid': '12345'}
        M1.put(tm)
        M1.send()
        M1.recv(1)
        M1.get(rm)
        self.assertEqual('12345', rm.body['user_name'])

        M1.stop()

        node.close()

if __name__ == '__main__':
    unittest.main(main_module())

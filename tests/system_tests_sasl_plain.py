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

import unittest, os, time
from subprocess import PIPE, Popen
from system_test import TestCase, Qdrouterd, main_module, DIR, TIMEOUT

from qpid_dispatch.management.client import Node

class RouterTestPlainSaslCommon(TestCase):

    @classmethod
    def router(cls, name, connection):

        config = [
            ('fixedAddress', {'prefix': '/closest/', 'fanout': 'single', 'bias': 'closest'}),
            ('fixedAddress', {'prefix': '/spread/', 'fanout': 'single', 'bias': 'spread'}),
            ('fixedAddress', {'prefix': '/multicast/', 'fanout': 'multiple'}),
            ('fixedAddress', {'prefix': '/', 'fanout': 'multiple'}),

        ] + connection

        config = Qdrouterd.Config(config)

        cls.routers.append(cls.tester.qdrouterd(name, config, wait=False))

    @classmethod
    def createSaslFiles(cls):
        # Create a sasl database.
        p = Popen(['saslpasswd2', '-c', '-p', '-f', 'qdrouterd.sasldb', '-u', 'domain.com', 'test'],
                  stdin=PIPE, stdout=PIPE, stderr=PIPE)
        result = p.communicate('password')
        assert p.returncode == 0, \
            "saslpasswd2 exit status %s, output:\n%s" % (p.returncode, result)

        # Create a SASL configuration file.
        with open('tests-mech-PLAIN.conf', 'w') as sasl_conf:
            sasl_conf.write("""
pwcheck_method: auxprop
auxprop_plugin: sasldb
sasldb_path: qdrouterd.sasldb
mech_list: ANONYMOUS DIGEST-MD5 EXTERNAL PLAIN
# The following line stops spurious 'sql_select option missing' errors when cyrus-sql-sasl plugin is installed
sql_select: dummy select
""")

class RouterTestPlainSasl(RouterTestPlainSaslCommon):

    @classmethod
    def setUpClass(cls):
        """
        Tests the sasl_username, sasl_password property of the dispatch router.

        Creates two routers (QDR.X and QDR.Y) and sets up PLAIN authentication on QDR.X.
        QDR.Y connects to QDR.X by providing a sasl_username and a sasl_password.

        """
        super(RouterTestPlainSasl, cls).setUpClass()

        super(RouterTestPlainSasl, cls).createSaslFiles()

        cls.routers = []

        x_listener_port = cls.tester.get_port()
        y_listener_port = cls.tester.get_port()

        super(RouterTestPlainSasl, cls).router('X', [
                     ('listener', {'host': '0.0.0.0', 'role': 'inter-router', 'port': x_listener_port,
                                   'saslMechanisms':'PLAIN', 'authenticatePeer': 'yes'}),
                     # This unauthenticated listener is for qdstat to connect to it.
                     ('listener', {'host': '0.0.0.0', 'role': 'normal', 'port': cls.tester.get_port(),
                                   'authenticatePeer': 'no'}),
                     ('router', {'workerThreads': 1,
                                 'id': 'QDR.X',
                                 'mode': 'interior',
                                 'saslConfigName': 'tests-mech-PLAIN',
                                 'saslConfigPath': os.getcwd()}),
        ])

        super(RouterTestPlainSasl, cls).router('Y', [
                     ('connector', {'host': '0.0.0.0', 'role': 'inter-router', 'port': x_listener_port,
                                    # Provide a sasl user name and password to connect to QDR.X
                                   'saslMechanisms': 'PLAIN',
                                    'saslUsername': 'test@domain.com',
                                    'saslPassword': 'password'}),
                     ('router', {'workerThreads': 1,
                                 'mode': 'interior',
                                 'id': 'QDR.Y'}),
                     ('listener', {'host': '0.0.0.0', 'role': 'normal', 'port': y_listener_port}),
        ])

        cls.routers[1].wait_router_connected('QDR.X')

    def test_inter_router_plain_exists(self):
        """The setUpClass sets up two routers with SASL PLAIN enabled.

        This test makes executes a qdstat -c via an unauthenticated listener to
        QDR.X and makes sure that the output has an "inter-router" connection to
        QDR.Y whose authentication is PLAIN. This ensures that QDR.Y did not
        somehow use SASL ANONYMOUS to connect to QDR.X

        """
        p = self.popen(
            ['qdstat', '-b', str(self.routers[0].addresses[1]), '-c'],
            name='qdstat-'+self.id(), stdout=PIPE, expect=None)
        out = p.communicate()[0]
        assert p.returncode == 0, \
            "qdstat exit status %s, output:\n%s" % (p.returncode, out)

        self.assertIn("inter-router", out)
        self.assertIn("test@domain.com(PLAIN)", out)


class RouterTestPlainSaslOverSsl(RouterTestPlainSaslCommon):

    @staticmethod
    def ssl_file(name):
        return os.path.join(DIR, 'ssl_certs', name)

    @classmethod
    def setUpClass(cls):
        """
        Tests the sasl_username, sasl_password property of the dispatch router.

        Creates two routers (QDR.X and QDR.Y) and sets up PLAIN authentication on QDR.X.
        QDR.Y connects to QDR.X by providing a sasl_username and a sasl_password.
        This PLAIN authentication is done over an TLS/SSLv3 connection.

        """
        super(RouterTestPlainSaslOverSsl, cls).setUpClass()

        super(RouterTestPlainSaslOverSsl, cls).createSaslFiles()

        cls.routers = []

        x_listener_port = cls.tester.get_port()
        y_listener_port = cls.tester.get_port()

        super(RouterTestPlainSaslOverSsl, cls).router('X', [
                     ('listener', {'host': '0.0.0.0', 'role': 'inter-router', 'port': x_listener_port,
                                   'sslProfile':'server-ssl-profile',
                                   'saslMechanisms':'PLAIN', 'authenticatePeer': 'yes'}),
                     # This unauthenticated listener is for qdstat to connect to it.
                     ('listener', {'host': '0.0.0.0', 'role': 'normal', 'port': cls.tester.get_port(),
                                   'authenticatePeer': 'no'}),
                     ('sslProfile', {'name': 'server-ssl-profile',
                                     'cert-db': cls.ssl_file('ca-certificate.pem'),
                                     'cert-file': cls.ssl_file('server-certificate.pem'),
                                     'key-file': cls.ssl_file('server-private-key.pem'),
                                     'password': 'server-password'}),
                     ('router', {'workerThreads': 1,
                                 'id': 'QDR.X',
                                 'mode': 'interior',
                                 'saslConfigName': 'tests-mech-PLAIN',
                                 'saslConfigPath': os.getcwd()}),
        ])

        super(RouterTestPlainSaslOverSsl, cls).router('Y', [
                     # This router will act like a client. First an SSL connection will be established and then
                     # we will have SASL plain authentication over SSL.
                     ('connector', {'host': '0.0.0.0', 'role': 'inter-router', 'port': x_listener_port,
                                    'ssl-profile': 'client-ssl-profile',
                                    'verifyHostName': 'no',
                                    # Provide a sasl user name and password to connect to QDR.X
                                    'saslMechanisms': 'PLAIN',
                                    'saslUsername': 'test@domain.com',
                                    'saslPassword': 'password'}),
                     ('router', {'workerThreads': 1,
                                 'mode': 'interior',
                                 'id': 'QDR.Y'}),
                     ('listener', {'host': '0.0.0.0', 'role': 'normal', 'port': y_listener_port}),
                     ('sslProfile', {'name': 'client-ssl-profile',
                                     'cert-db': cls.ssl_file('ca-certificate.pem'),
                                     'cert-file': cls.ssl_file('client-certificate.pem'),
                                     'key-file': cls.ssl_file('client-private-key.pem'),
                                     'password': 'client-password'}),
        ])

        cls.routers[1].wait_router_connected('QDR.X')

    def test_inter_router_plain_over_ssl_exists(self):
        """The setUpClass sets up two routers with SASL PLAIN enabled over TLS/SSLv3.

        This test makes executes a query for type='org.apache.qpid.dispatch.connection' over
        an unauthenticated listener to
        QDR.X and makes sure that the output has an "inter-router" connection to
        QDR.Y whose authentication is PLAIN. This ensures that QDR.Y did not
        somehow use SASL ANONYMOUS to connect to QDR.X
        Also makes sure that TLSv1/SSLv3 was used as sslProto

        """
        local_node = Node.connect(self.routers[0].addresses[1], timeout=TIMEOUT)

        # sslProto should be TLSv1/SSLv3
        self.assertEqual(u'TLSv1/SSLv3', local_node.query(type='org.apache.qpid.dispatch.connection').results[0][4])

        # role should be inter-router
        self.assertEqual(u'inter-router', local_node.query(type='org.apache.qpid.dispatch.connection').results[0][9])

        # sasl must be plain
        self.assertEqual(u'PLAIN', local_node.query(type='org.apache.qpid.dispatch.connection').results[0][12])

        # user must be test@domain.com
        self.assertEqual(u'test@domain.com', local_node.query(type='org.apache.qpid.dispatch.connection').results[0][16])

class RouterTestVerifyHostNameYes(RouterTestPlainSaslCommon):

    @staticmethod
    def ssl_file(name):
        return os.path.join(DIR, 'ssl_certs', name)

    @classmethod
    def setUpClass(cls):
        """
        Tests the verifyHostName property of the connector. The hostname on the server certificate we use is
        A1.Good.Server.domain.com and the host is 0.0.0.0 on the client router initiating the SSL connection.
        Since the host names do not match and the verifyHostName is set to true, the client router
        will NOT be able make a successful SSL connection the server router.
        """
        super(RouterTestVerifyHostNameYes, cls).setUpClass()

        super(RouterTestVerifyHostNameYes, cls).createSaslFiles()

        cls.routers = []

        x_listener_port = cls.tester.get_port()
        y_listener_port = cls.tester.get_port()

        super(RouterTestVerifyHostNameYes, cls).router('X', [
                     ('listener', {'addr': '0.0.0.0', 'role': 'inter-router', 'port': x_listener_port,
                                   'sslProfile':'server-ssl-profile',
                                   'saslMechanisms':'PLAIN', 'authenticatePeer': 'yes'}),
                     # This unauthenticated listener is for qdstat to connect to it.
                     ('listener', {'addr': '0.0.0.0', 'role': 'normal', 'port': cls.tester.get_port(),
                                   'authenticatePeer': 'no'}),
                     ('sslProfile', {'name': 'server-ssl-profile',
                                     'cert-db': cls.ssl_file('ca-certificate.pem'),
                                     'cert-file': cls.ssl_file('server-certificate.pem'),
                                     'key-file': cls.ssl_file('server-private-key.pem'),
                                     'password': 'server-password'}),
                     ('router', {'workerThreads': 1,
                                 'routerId': 'QDR.X',
                                 'mode': 'interior',
                                 'saslConfigName': 'tests-mech-PLAIN',
                                 'saslConfigPath': os.getcwd()}),
        ])

        super(RouterTestVerifyHostNameYes, cls).router('Y', [
                     ('connector', {'addr': '127.0.0.1', 'role': 'inter-router', 'port': x_listener_port,
                                    'ssl-profile': 'client-ssl-profile',
                                    'verifyHostName': 'yes',
                                    'saslMechanisms': 'PLAIN',
                                    'saslUsername': 'test@domain.com', 'saslPassword': 'password'}),
                     ('router', {'workerThreads': 1,
                                 'mode': 'interior',
                                 'routerId': 'QDR.Y'}),
                     ('listener', {'addr': '0.0.0.0', 'role': 'normal', 'port': y_listener_port}),
                     ('sslProfile', {'name': 'client-ssl-profile',
                                     'cert-db': cls.ssl_file('ca-certificate.pem'),
                                     'cert-file': cls.ssl_file('client-certificate.pem'),
                                     'key-file': cls.ssl_file('client-private-key.pem'),
                                     'password': 'client-password'}),
        ])

        cls.routers[0].wait_ports()
        cls.routers[1].wait_ports()
        try:
            # This will time out because there is no inter-router connection
            cls.routers[1].wait_connectors(timeout=3)
        except:
            pass

    def test_no_inter_router_connection(self):
        """
        Tests to make sure that there are no 'inter-router' connections.
        The connection to the other router will not happen because the connection failed
        due to setting 'verifyHostName': 'yes'
        """
        local_node = Node.connect(self.routers[1].addresses[0], timeout=TIMEOUT)

        # There should be only two connections.
        # There will be no inter-router connection
        self.assertEqual(2, len(local_node.query(type='org.apache.qpid.dispatch.connection').results))
        self.assertEqual('in', local_node.query(type='org.apache.qpid.dispatch.connection').results[0][15])
        self.assertEqual('normal', local_node.query(type='org.apache.qpid.dispatch.connection').results[0][9])
        self.assertEqual('anonymous', local_node.query(type='org.apache.qpid.dispatch.connection').results[0][16])

        self.assertEqual('normal', local_node.query(type='org.apache.qpid.dispatch.connection').results[1][9])
        self.assertEqual('anonymous', local_node.query(type='org.apache.qpid.dispatch.connection').results[1][16])

class RouterTestVerifyHostNameNo(RouterTestPlainSaslCommon):

    @staticmethod
    def ssl_file(name):
        return os.path.join(DIR, 'ssl_certs', name)

    @classmethod
    def setUpClass(cls):
        """
        Tests the verifyHostName property of the connector. The hostname on the server certificate we use is
        A1.Good.Server.domain.com and the host is 0.0.0.0 on the client router initiating the SSL connection.
        Since the host names do not match but verifyHostName is set to false, the client router
        will be successfully able to make an SSL connection the server router.
        """
        super(RouterTestVerifyHostNameNo, cls).setUpClass()

        super(RouterTestVerifyHostNameNo, cls).createSaslFiles()

        cls.routers = []

        x_listener_port = cls.tester.get_port()
        y_listener_port = cls.tester.get_port()

        super(RouterTestVerifyHostNameNo, cls).router('X', [
                     ('listener', {'addr': '0.0.0.0', 'role': 'inter-router', 'port': x_listener_port,
                                   'sslProfile':'server-ssl-profile',
                                   'saslMechanisms':'PLAIN', 'authenticatePeer': 'yes'}),
                     # This unauthenticated listener is for qdstat to connect to it.
                     ('listener', {'addr': '0.0.0.0', 'role': 'normal', 'port': cls.tester.get_port(),
                                   'authenticatePeer': 'no'}),
                     ('sslProfile', {'name': 'server-ssl-profile',
                                     'cert-db': cls.ssl_file('ca-certificate.pem'),
                                     'cert-file': cls.ssl_file('server-certificate.pem'),
                                     'key-file': cls.ssl_file('server-private-key.pem'),
                                     'password': 'server-password'}),
                     ('router', {'workerThreads': 1,
                                 'routerId': 'QDR.X',
                                 'mode': 'interior',
                                 'saslConfigName': 'tests-mech-PLAIN',
                                 'saslConfigPath': os.getcwd()}),
        ])

        super(RouterTestVerifyHostNameNo, cls).router('Y', [
                     # This router will act like a client. First an SSL connection will be established and then
                     # we will have SASL plain authentication over SSL.
                     ('connector', {'addr': '127.0.0.1', 'role': 'inter-router', 'port': x_listener_port,
                                    'ssl-profile': 'client-ssl-profile',
                                    # Provide a sasl user name and password to connect to QDR.X
                                    'saslMechanisms': 'PLAIN',
                                    'verifyHostName': 'no',
                                    'saslUsername': 'test@domain.com', 'saslPassword': 'password'}),
                     ('router', {'workerThreads': 1,
                                 'mode': 'interior',
                                 'routerId': 'QDR.Y'}),
                     ('listener', {'addr': '0.0.0.0', 'role': 'normal', 'port': y_listener_port}),
                     ('sslProfile', {'name': 'client-ssl-profile',
                                     'cert-db': cls.ssl_file('ca-certificate.pem'),
                                     'cert-file': cls.ssl_file('client-certificate.pem'),
                                     'key-file': cls.ssl_file('client-private-key.pem'),
                                     'password': 'client-password'}),
        ])

        cls.routers[0].wait_ports()
        cls.routers[1].wait_ports()
        cls.routers[1].wait_router_connected('QDR.X')

    def test_inter_router_plain_over_ssl_exists(self):
        """
        Tests to make sure that an inter-router connection exists between the routers since verifyHostName is 'no'.
        """
        local_node = Node.connect(self.routers[1].addresses[0], timeout=TIMEOUT)

        results = local_node.query(type='org.apache.qpid.dispatch.connection').results

        self.assertEqual(4, len(results))

        search = "QDR.X"
        found = False

        for N in range(0,3):
            if results[N][0] == search:
                found = True
                break

        self.assertTrue(found, "Connection to %s not found" % search)

        # sslProto should be TLSv1/SSLv3
        self.assertEqual(u'TLSv1/SSLv3', results[N][4])

        # role should be inter-router
        self.assertEqual(u'inter-router', results[N][9])

        # sasl must be plain
        self.assertEqual(u'PLAIN', results[N][12])

        # user must be test@domain.com
        self.assertEqual(u'test@domain.com', results[N][16])

if __name__ == '__main__':
    unittest.main(main_module())


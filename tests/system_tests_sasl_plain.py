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

from __future__ import unicode_literals
from __future__ import division
from __future__ import absolute_import
from __future__ import print_function

from time import sleep
import os, json
from subprocess import PIPE, STDOUT, Popen
from system_test import TestCase, Qdrouterd, main_module, DIR, TIMEOUT, SkipIfNeeded, Process
from system_test import unittest, QdManager
from qpid_dispatch.management.client import Node
from proton import SASL

class RouterTestPlainSaslCommon(TestCase):
    @classmethod
    def router(cls, name, connection):

        config = Qdrouterd.Config(connection)

        cls.routers.append(cls.tester.qdrouterd(name, config, wait=False))

    @classmethod
    def createSaslFiles(cls):
        # Create a sasl database.
        p = Popen(['saslpasswd2', '-c', '-p', '-f', 'qdrouterd.sasldb', '-u', 'domain.com', 'test'],
                  stdin=PIPE, stdout=PIPE, stderr=PIPE, universal_newlines=True)
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


class RouterTestPlainSaslFailure(RouterTestPlainSaslCommon):
    @staticmethod
    def sasl_file(name):
        return os.path.join(DIR, 'sasl_files', name)


    @classmethod
    def setUpClass(cls):
        """
        Tests the sasl_username, sasl_password property of the dispatch router.

        Creates two routers (QDR.X and QDR.Y) and sets up PLAIN authentication on QDR.X.
        QDR.Y connects to QDR.X by providing a sasl_username and a bad sasl_password
        as a non-existent file.

        """
        super(RouterTestPlainSaslFailure, cls).setUpClass()

        if not SASL.extended():
            return

        super(RouterTestPlainSaslFailure, cls).createSaslFiles()

        cls.routers = []

        x_listener_port = cls.tester.get_port()
        y_listener_port = cls.tester.get_port()

        super(RouterTestPlainSaslFailure, cls).router('X', [
                     ('listener', {'host': '0.0.0.0', 'role': 'inter-router', 'port': x_listener_port,
                                   'saslMechanisms':'PLAIN', 'authenticatePeer': 'yes'}),
                     # This unauthenticated listener is for qdstat to connect to it.
                     ('listener', {'host': '0.0.0.0', 'role': 'normal', 'port': cls.tester.get_port(),
                                   'authenticatePeer': 'no'}),
                     ('listener', {'host': '0.0.0.0', 'role': 'normal', 'port': cls.tester.get_port(),
                                   'saslMechanisms':'PLAIN', 'authenticatePeer': 'yes'}),
                     ('router', {'workerThreads': 1,
                                 'id': 'QDR.X',
                                 'mode': 'interior',
                                 'saslConfigName': 'tests-mech-PLAIN',
                                 # Leave as saslConfigPath for testing backward compatibility
                                 'saslConfigPath': os.getcwd()}),
        ])

        super(RouterTestPlainSaslFailure, cls).router('Y', [
                     ('connector', {'host': '0.0.0.0', 'role': 'inter-router', 'port': x_listener_port,
                                    # Provide a sasl user name and password to connect to QDR.X
                                   'saslMechanisms': 'PLAIN',
                                    'saslUsername': 'test@domain.com',
                                    # Provide a non-existen file.
                                    'saslPassword': 'file:' + cls.sasl_file('non-existent-password-file.txt')}),
                     ('router', {'workerThreads': 1,
                                 'mode': 'interior',
                                 'id': 'QDR.Y'}),
                     ('listener', {'host': '0.0.0.0', 'role': 'normal', 'port': y_listener_port}),
        ])

        cls.routers[0].wait_ports()
        cls.routers[1].wait_ports()
        try:
            # This will time out in 5 seconds because there is no inter-router connection
            cls.routers[1].wait_connectors(timeout=5)
        except:
            pass

        # Give some time for connector failures to be written to the log.
        sleep(3)

    @SkipIfNeeded(not SASL.extended(), "Cyrus library not available. skipping test")
    def test_inter_router_sasl_fail(self):
        passed = False
        long_type = 'org.apache.qpid.dispatch.connection'
        qd_manager = QdManager(self, address=self.routers[1].addresses[0])
        connections = qd_manager.query(long_type)
        for connection in connections:
            if connection['role'] == 'inter-router':
                passed = True
                break

        # There was no inter-router connection established.
        self.assertFalse(passed)

        qd_manager = QdManager(self, address=self.routers[1].addresses[0])
        logs = qd_manager.get_log()

        sasl_failed = False
        file_open_failed = False
        for log in logs:
            if log[0] == 'SERVER' and log[1] == "info" and "amqp:unauthorized-access Authentication failed [mech=PLAIN]" in log[2]:
                sasl_failed = True
            if log[0] == "CONN_MGR" and log[1] == "error" and "Unable to open password file" in log[2] and "error: No such file or directory" in log[2]:
                file_open_failed = True

        self.assertTrue(sasl_failed)
        self.assertTrue(file_open_failed)


class RouterTestPlainSaslFailureUsingLiteral(RouterTestPlainSaslCommon):
    @staticmethod
    def sasl_file(name):
        return os.path.join(DIR, 'sasl_files', name)


    @classmethod
    def setUpClass(cls):
        """
        Tests the sasl_username, sasl_password property of the dispatch router.

        Creates two routers (QDR.X and QDR.Y) and sets up PLAIN authentication on QDR.X.
        QDR.Y connects to QDR.X by providing a sasl_username and a bad sasl_password
        using the literal: prefix.

        """
        super(RouterTestPlainSaslFailureUsingLiteral, cls).setUpClass()

        if not SASL.extended():
            return

        super(RouterTestPlainSaslFailureUsingLiteral, cls).createSaslFiles()

        cls.routers = []

        x_listener_port = cls.tester.get_port()
        y_listener_port = cls.tester.get_port()

        super(RouterTestPlainSaslFailureUsingLiteral, cls).router('X', [
                     ('listener', {'host': '0.0.0.0', 'role': 'inter-router', 'port': x_listener_port,
                                   'saslMechanisms':'PLAIN', 'authenticatePeer': 'yes'}),
                     # This unauthenticated listener is for qdstat to connect to it.
                     ('listener', {'host': '0.0.0.0', 'role': 'normal', 'port': cls.tester.get_port(),
                                   'authenticatePeer': 'no'}),
                     ('listener', {'host': '0.0.0.0', 'role': 'normal', 'port': cls.tester.get_port(),
                                   'saslMechanisms':'PLAIN', 'authenticatePeer': 'yes'}),
                     ('router', {'workerThreads': 1,
                                 'id': 'QDR.X',
                                 'mode': 'interior',
                                 'saslConfigName': 'tests-mech-PLAIN',
                                 # Leave as saslConfigPath for testing backward compatibility
                                 'saslConfigPath': os.getcwd()}),
        ])

        super(RouterTestPlainSaslFailureUsingLiteral, cls).router('Y', [
                     ('connector', {'host': '0.0.0.0', 'role': 'inter-router', 'port': x_listener_port,
                                    # Provide a sasl user name and password to connect to QDR.X
                                   'saslMechanisms': 'PLAIN',
                                    'saslUsername': 'test@domain.com',
                                    # Provide the password with a prefix of literal. This should fail..
                                    'saslPassword': 'literal:password'}),
                     ('router', {'workerThreads': 1,
                                 'mode': 'interior',
                                 'id': 'QDR.Y'}),
                     ('listener', {'host': '0.0.0.0', 'role': 'normal', 'port': y_listener_port}),
        ])

        cls.routers[0].wait_ports()
        cls.routers[1].wait_ports()
        try:
            # This will time out in 5 seconds because there is no inter-router connection
            cls.routers[1].wait_connectors(timeout=5)
        except:
            pass

        # Give some time for connector failures to be written to the log.
        sleep(3)

    @SkipIfNeeded(not SASL.extended(), "Cyrus library not available. skipping test")
    def test_inter_router_sasl_fail(self):
        passed = False
        long_type = 'org.apache.qpid.dispatch.connection'

        qd_manager = QdManager(self, address=self.routers[1].addresses[0])
        connections = qd_manager.query(long_type)

        for connection in connections:
            if connection['role'] == 'inter-router':
                passed = True
                break

        # There was no inter-router connection established.
        self.assertFalse(passed)
        logs = qd_manager.get_log()

        sasl_failed = False
        for log in logs:
            if log[0] == 'SERVER' and log[1] == "info" and "amqp:unauthorized-access Authentication failed [mech=PLAIN]" in log[2]:
                sasl_failed = True

        self.assertTrue(sasl_failed)


class RouterTestPlainSasl(RouterTestPlainSaslCommon):

    @classmethod
    def setUpClass(cls):
        """
        Tests the sasl_username, sasl_password property of the dispatch router.

        Creates two routers (QDR.X and QDR.Y) and sets up PLAIN authentication on QDR.X.
        QDR.Y connects to QDR.X by providing a sasl_username and a sasl_password.

        """
        super(RouterTestPlainSasl, cls).setUpClass()

        if not SASL.extended():
            return

        os.environ["ENV_SASL_PASSWORD"] = "password"

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
                     ('listener', {'host': '0.0.0.0', 'role': 'normal', 'port': cls.tester.get_port(),
                                   'saslMechanisms':'PLAIN', 'authenticatePeer': 'yes'}),
                     ('router', {'workerThreads': 1,
                                 'id': 'QDR.X',
                                 'mode': 'interior',
                                 'saslConfigName': 'tests-mech-PLAIN',
                                 # Leave as saslConfigPath for testing backward compatibility
                                 'saslConfigPath': os.getcwd()}),
        ])

        super(RouterTestPlainSasl, cls).router('Y', [
                     ('connector', {'host': '0.0.0.0', 'role': 'inter-router', 'port': x_listener_port,
                                    # Provide a sasl user name and password to connect to QDR.X
                                   'saslMechanisms': 'PLAIN',
                                    'saslUsername': 'test@domain.com',
                                    'saslPassword': 'env:ENV_SASL_PASSWORD'}),
                     ('router', {'workerThreads': 1,
                                 'mode': 'interior',
                                 'id': 'QDR.Y'}),
                     ('listener', {'host': '0.0.0.0', 'role': 'normal', 'port': y_listener_port}),
        ])

        cls.routers[1].wait_router_connected('QDR.X')

    @SkipIfNeeded(not SASL.extended(), "Cyrus library not available. skipping test")
    def test_inter_router_plain_exists(self):
        """
        Check authentication of inter-router link is PLAIN.

        This test makes executes a qdstat -c via an unauthenticated listener to
        QDR.X and makes sure that the output has an "inter-router" connection to
        QDR.Y whose authentication is PLAIN. This ensures that QDR.Y did not
        somehow use SASL ANONYMOUS to connect to QDR.X

        """

        p = self.popen(
            ['qdstat', '-b', str(self.routers[0].addresses[1]), '-c'],
            name='qdstat-'+self.id(), stdout=PIPE, expect=None,
            universal_newlines=True)
        out = p.communicate()[0]
        assert p.returncode == 0, \
            "qdstat exit status %s, output:\n%s" % (p.returncode, out)

        self.assertIn("inter-router", out)
        self.assertIn("test@domain.com(PLAIN)", out)

    @SkipIfNeeded(not SASL.extended(), "Cyrus library not available. skipping test")
    def test_qdstat_connect_sasl(self):
        """
        Make qdstat use sasl plain authentication.
        """

        p = self.popen(
            ['qdstat', '-b', str(self.routers[0].addresses[2]), '-c', '--sasl-mechanisms=PLAIN',
             '--sasl-username=test@domain.com', '--sasl-password=password'],
            name='qdstat-'+self.id(), stdout=PIPE, expect=None,
            universal_newlines=True)

        out = p.communicate()[0]
        assert p.returncode == 0, \
            "qdstat exit status %s, output:\n%s" % (p.returncode, out)

        split_list = out.split()

        # There will be 2 connections that have authenticated using SASL PLAIN. One inter-router connection
        # and the other connection that this qdstat client is making
        self.assertEqual(2, split_list.count("test@domain.com(PLAIN)"))
        self.assertEqual(1, split_list.count("inter-router"))
        self.assertEqual(1, split_list.count("normal"))

    @SkipIfNeeded(not SASL.extended(), "Cyrus library not available. skipping test")
    def test_qdstat_connect_sasl_password_file(self):
        """
        Make qdstat use sasl plain authentication with client password specified in a file.
        """
        password_file = os.getcwd() + '/sasl-client-password-file.txt'
        # Create a SASL configuration file.
        with open(password_file, 'w') as sasl_client_password_file:
            sasl_client_password_file.write("password")

        sasl_client_password_file.close()

        p = self.popen(
            ['qdstat', '-b', str(self.routers[0].addresses[2]), '-c', '--sasl-mechanisms=PLAIN',
             '--sasl-username=test@domain.com', '--sasl-password-file=' + password_file],
            name='qdstat-'+self.id(), stdout=PIPE, expect=None,
            universal_newlines=True)

        out = p.communicate()[0]
        assert p.returncode == 0, \
            "qdstat exit status %s, output:\n%s" % (p.returncode, out)

        split_list = out.split()

        # There will be 2 connections that have authenticated using SASL PLAIN. One inter-router connection
        # and the other connection that this qdstat client is making
        self.assertEqual(2, split_list.count("test@domain.com(PLAIN)"))
        self.assertEqual(1, split_list.count("inter-router"))
        self.assertEqual(1, split_list.count("normal"))


class RouterTestPlainSaslOverSsl(RouterTestPlainSaslCommon):

    @staticmethod
    def ssl_file(name):
        return os.path.join(DIR, 'ssl_certs', name)

    @staticmethod
    def sasl_file(name):
        return os.path.join(DIR, 'sasl_files', name)

    @classmethod
    def setUpClass(cls):
        """
        Tests the sasl_username, sasl_password property of the dispatch router.

        Creates two routers (QDR.X and QDR.Y) and sets up PLAIN authentication on QDR.X.
        QDR.Y connects to QDR.X by providing a sasl_username and a sasl_password.
        This PLAIN authentication is done over a TLS connection.

        """
        super(RouterTestPlainSaslOverSsl, cls).setUpClass()

        if not SASL.extended():
            return

        super(RouterTestPlainSaslOverSsl, cls).createSaslFiles()

        cls.routers = []

        x_listener_port = cls.tester.get_port()
        y_listener_port = cls.tester.get_port()

        super(RouterTestPlainSaslOverSsl, cls).router('X', [
                     ('listener', {'host': '0.0.0.0', 'role': 'inter-router', 'port': x_listener_port,
                                   'sslProfile':'server-ssl-profile',
                                   'saslMechanisms':'PLAIN', 'authenticatePeer': 'yes'}),
                     ('listener', {'host': '0.0.0.0', 'role': 'normal', 'port': cls.tester.get_port(),
                                   'authenticatePeer': 'no'}),
                     ('listener', {'host': '0.0.0.0', 'role': 'normal', 'port': cls.tester.get_port(),
                                   'sslProfile':'server-ssl-profile',
                                   'saslMechanisms':'PLAIN', 'authenticatePeer': 'yes'}),
                     ('sslProfile', {'name': 'server-ssl-profile',
                                     'certFile': cls.ssl_file('server-certificate.pem'),
                                     'privateKeyFile': cls.ssl_file('server-private-key.pem'),
                                     'ciphers': 'ECDH+AESGCM:DH+AESGCM:ECDH+AES256:DH+AES256:ECDH+AES128:DH+AES:RSA+AESGCM:RSA+AES:!aNULL:!MD5:!DSS',
                                     'protocols': 'TLSv1.1 TLSv1.2',
                                     'password': 'server-password'}),
                     ('router', {'workerThreads': 1,
                                 'id': 'QDR.X',
                                 'mode': 'interior',
                                 'saslConfigName': 'tests-mech-PLAIN',
                                 'saslConfigDir': os.getcwd()}),
        ])

        super(RouterTestPlainSaslOverSsl, cls).router('Y', [
                     # This router will act like a client. First an SSL connection will be established and then
                     # we will have SASL plain authentication over SSL.
                     ('connector', {'host': 'localhost', 'role': 'inter-router', 'port': x_listener_port,
                                    'sslProfile': 'client-ssl-profile',
                                    # Provide a sasl user name and password to connect to QDR.X
                                    'saslMechanisms': 'PLAIN',
                                    'saslUsername': 'test@domain.com',
                                    'saslPassword': 'file:' + cls.sasl_file('password.txt')}),
                     ('router', {'workerThreads': 1,
                                 'mode': 'interior',
                                 'id': 'QDR.Y'}),
                     ('listener', {'host': '0.0.0.0', 'role': 'normal', 'port': y_listener_port}),
                     ('sslProfile', {'name': 'client-ssl-profile',
                                     'caCertFile': cls.ssl_file('ca-certificate.pem')}),
        ])

        cls.routers[1].wait_router_connected('QDR.X')

    @SkipIfNeeded(not SASL.extended(), "Cyrus library not available. skipping test")
    def test_aaa_qdstat_connect_sasl_over_ssl(self):
        """
        Make qdstat use sasl plain authentication over ssl.
        """
        p = self.popen(
            ['qdstat', '-b', str(self.routers[0].addresses[2]), '-c',
             # The following are SASL args
             '--sasl-mechanisms=PLAIN',
             '--sasl-username=test@domain.com',
             '--sasl-password=password',
             # The following are SSL args
             '--ssl-disable-peer-name-verify',
             '--ssl-trustfile=' + self.ssl_file('ca-certificate.pem'),
             '--ssl-certificate=' + self.ssl_file('client-certificate.pem'),
             '--ssl-key=' + self.ssl_file('client-private-key.pem'),
             '--ssl-password=client-password'],
            name='qdstat-'+self.id(), stdout=PIPE, expect=None,
            universal_newlines=True)

        out = p.communicate()[0]
        assert p.returncode == 0, \
            "qdstat exit status %s, output:\n%s" % (p.returncode, out)

        split_list = out.split()

        # There will be 2 connections that have authenticated using SASL PLAIN. One inter-router connection
        # and the other connection that this qdstat client is making
        self.assertEqual(2, split_list.count("test@domain.com(PLAIN)"))
        self.assertEqual(1, split_list.count("inter-router"))
        self.assertEqual(1, split_list.count("normal"))

    @SkipIfNeeded(not SASL.extended(), "Cyrus library not available. skipping test")
    def test_inter_router_plain_over_ssl_exists(self):
        """The setUpClass sets up two routers with SASL PLAIN enabled over TLS.

        This test makes executes a query for type='org.apache.qpid.dispatch.connection' over
        an unauthenticated listener to
        QDR.X and makes sure that the output has an "inter-router" connection to
        QDR.Y whose authentication is PLAIN. This ensures that QDR.Y did not
        somehow use SASL ANONYMOUS to connect to QDR.X
        Also makes sure that TLSv1.x was used as sslProto

        """
        local_node = Node.connect(self.routers[0].addresses[1], timeout=TIMEOUT)
        results = local_node.query(type='org.apache.qpid.dispatch.connection').results

        # sslProto should be TLSv1.x
        self.assertIn(u'TLSv1', results[0][10])

        # role should be inter-router
        self.assertEqual(u'inter-router', results[0][3])

        # sasl must be plain
        self.assertEqual(u'PLAIN', results[0][6])

        # user must be test@domain.com
        self.assertEqual(u'test@domain.com', results[0][8])


class RouterTestVerifyHostNameYes(RouterTestPlainSaslCommon):
    @staticmethod
    def ssl_file(name):
        return os.path.join(DIR, 'ssl_certs', name)

    @staticmethod
    def sasl_file(name):
        return os.path.join(DIR, 'sasl_files', name)

    @classmethod
    def setUpClass(cls):
        """
        Tests the verifyHostname property of the connector. The hostname on the server certificate we use is
        localhost and the host is 127.0.0.1 on the client router initiating the SSL connection.
        Since the host names do not match and the verifyHostname is set to true, the client router
        will NOT be able make a successful SSL connection the server router.
        """
        super(RouterTestVerifyHostNameYes, cls).setUpClass()

        if not SASL.extended():
            return

        super(RouterTestVerifyHostNameYes, cls).createSaslFiles()

        cls.routers = []

        x_listener_port = cls.tester.get_port()
        y_listener_port = cls.tester.get_port()

        super(RouterTestVerifyHostNameYes, cls).router('X', [
                     ('listener', {'host': '0.0.0.0', 'role': 'inter-router', 'port': x_listener_port,
                                   'sslProfile':'server-ssl-profile',
                                   'saslMechanisms':'PLAIN', 'authenticatePeer': 'yes'}),
                     # This unauthenticated listener is for qdstat to connect to it.
                     ('listener', {'host': '0.0.0.0', 'role': 'normal', 'port': cls.tester.get_port(),
                                   'authenticatePeer': 'no'}),
                     ('sslProfile', {'name': 'server-ssl-profile',
                                     'certFile': cls.ssl_file('server-certificate.pem'),
                                     'privateKeyFile': cls.ssl_file('server-private-key.pem'),
                                     'password': 'server-password'}),
                     ('router', {'workerThreads': 1,
                                 'id': 'QDR.X',
                                 'mode': 'interior',
                                 'saslConfigName': 'tests-mech-PLAIN',
                                 'saslConfigDir': os.getcwd()}),
        ])

        super(RouterTestVerifyHostNameYes, cls).router('Y', [
                     ('connector', {'host': '127.0.0.1', 'role': 'inter-router', 'port': x_listener_port,
                                    'sslProfile': 'client-ssl-profile',
                                    # verifyHostName has been deprecated. We are using it here to test
                                    # backward compatibility. TODO: should add a specific test.
                                    'verifyHostName': 'yes',
                                    'saslMechanisms': 'PLAIN',
                                    'saslUsername': 'test@domain.com',
                                    'saslPassword': 'file:' + cls.sasl_file('password.txt')}),
                     ('router', {'workerThreads': 1,
                                 'mode': 'interior',
                                 'id': 'QDR.Y'}),
                     ('listener', {'host': '0.0.0.0', 'role': 'normal', 'port': y_listener_port}),
                     ('sslProfile', {'name': 'client-ssl-profile',
                                     'caCertFile': cls.ssl_file('ca-certificate.pem')}),
        ])

        cls.routers[0].wait_ports()
        cls.routers[1].wait_ports()
        try:
            # This will time out because there is no inter-router connection
            cls.routers[1].wait_connectors(timeout=3)
        except:
            pass

    @SkipIfNeeded(not SASL.extended(), "Cyrus library not available. skipping test")
    def test_no_inter_router_connection(self):
        """
        Tests to make sure that there are no 'inter-router' connections.
        The connection to the other router will not happen because the connection failed
        due to setting 'verifyHostname': 'yes'
        """
        local_node = Node.connect(self.routers[1].addresses[0], timeout=TIMEOUT)
        results = local_node.query(type='org.apache.qpid.dispatch.connection').results
        # There should be only two connections.
        # There will be no inter-router connection
        self.assertEqual(2, len(results))
        self.assertEqual('in', results[0][4])
        self.assertEqual('normal', results[0][3])
        self.assertEqual('anonymous', results[0][8])
        self.assertEqual('normal', results[1][3])
        self.assertEqual('anonymous', results[1][8])

class RouterTestVerifyHostNameNo(RouterTestPlainSaslCommon):

    @staticmethod
    def ssl_file(name):
        return os.path.join(DIR, 'ssl_certs', name)

    x_listener_port = None

    @classmethod
    def setUpClass(cls):
        """
        Tests the verifyHostname property of the connector. The hostname on the server certificate we use is
        localhost and the host is 127.0.0.1 on the client router initiating the SSL connection.
        Since the host names do not match but verifyHostname is set to false, the client router
        will be successfully able to make an SSL connection the server router.
        """
        super(RouterTestVerifyHostNameNo, cls).setUpClass()

        if not SASL.extended():
            return

        super(RouterTestVerifyHostNameNo, cls).createSaslFiles()

        cls.routers = []

        x_listener_port = cls.tester.get_port()
        RouterTestVerifyHostNameNo.x_listener_port = x_listener_port
        y_listener_port = cls.tester.get_port()

        super(RouterTestVerifyHostNameNo, cls).router('X', [
                     ('listener', {'host': '0.0.0.0', 'role': 'inter-router', 'port': x_listener_port,
                                   'sslProfile':'server-ssl-profile',
                                   'saslMechanisms':'PLAIN', 'authenticatePeer': 'yes'}),
                     # This unauthenticated listener is for qdstat to connect to it.
                     ('listener', {'host': '0.0.0.0', 'role': 'normal', 'port': cls.tester.get_port(),
                                   'authenticatePeer': 'no'}),
                     ('sslProfile', {'name': 'server-ssl-profile',
                                     # certDb has been deprecated. We are using it here to test backward compatibility.
                                     # TODO: should add a specific test, this one presumably doesnt even use it due to not doing client-certificate authentication
                                     'certDb': cls.ssl_file('ca-certificate.pem'),
                                     'certFile': cls.ssl_file('server-certificate.pem'),
                                     # keyFile has been deprecated. We are using it here to test backward compatibility.
                                     'keyFile': cls.ssl_file('server-private-key.pem'),
                                     'password': 'server-password'}),
                     ('router', {'workerThreads': 1,
                                 'id': 'QDR.X',
                                 'mode': 'interior',
                                 'saslConfigName': 'tests-mech-PLAIN',
                                 'saslConfigDir': os.getcwd()}),
        ])

        super(RouterTestVerifyHostNameNo, cls).router('Y', [
                     # This router will act like a client. First an SSL connection will be established and then
                     # we will have SASL plain authentication over SSL.
                     ('connector', {'name': 'connectorToX',
                                    'host': '127.0.0.1', 'role': 'inter-router',
                                    'port': x_listener_port,
                                    'sslProfile': 'client-ssl-profile',
                                    # Provide a sasl user name and password to connect to QDR.X
                                    'saslMechanisms': 'PLAIN',
                                    'verifyHostname': 'no',
                                    'saslUsername': 'test@domain.com', 'saslPassword': 'pass:password'}),
                     ('router', {'workerThreads': 1,
                                 'mode': 'interior',
                                 'id': 'QDR.Y'}),
                     ('listener', {'host': '0.0.0.0', 'role': 'normal', 'port': y_listener_port}),
                     ('sslProfile', {'name': 'client-ssl-profile',
                                     'caCertFile': cls.ssl_file('ca-certificate.pem')}),
        ])

        cls.routers[0].wait_ports()
        cls.routers[1].wait_ports()

        cls.routers[1].wait_router_connected('QDR.X')

    @staticmethod
    def ssl_file(name):
        return os.path.join(DIR, 'ssl_certs', name)

    def common_asserts(self, results):
        search = "QDR.X"
        found = False

        for N in range(0, len(results)):
            if results[N][5] == search:
                found = True
                break

        self.assertTrue(found, "Connection to %s not found" % search)

        # sslProto should be TLSv1.x
        self.assertIn(u'TLSv1', results[N][10])

        # role should be inter-router
        self.assertEqual(u'inter-router', results[N][3])

        # sasl must be plain
        self.assertEqual(u'PLAIN', results[N][6])

        # user must be test@domain.com
        self.assertEqual(u'test@domain.com', results[N][8])

    @SkipIfNeeded(not SASL.extended(), "Cyrus library not available. skipping test")
    def test_inter_router_plain_over_ssl_exists(self):
        """
        Tests to make sure that an inter-router connection exists between the routers since verifyHostname is 'no'.
        """
        local_node = Node.connect(self.routers[1].addresses[0], timeout=TIMEOUT)

        results = local_node.query(type='org.apache.qpid.dispatch.connection').results

        self.common_asserts(results)

    @SkipIfNeeded(not SASL.extended(), "Cyrus library not available. skipping test")
    def test_zzz_delete_create_ssl_profile(self):
        """
        Deletes a connector and its corresponding ssl profile and recreates both
        """
        local_node = self.routers[1].management

        connections = local_node.query(type='org.apache.qpid.dispatch.connection').get_entities()
        self.assertIn("QDR.X", [c.container for c in connections]) # We can find the connection before
        local_node.delete(type='connector', name='connectorToX')
        local_node.delete(type='sslProfile', name='client-ssl-profile')
        connections = local_node.query(type='org.apache.qpid.dispatch.connection').get_entities()
        is_qdr_x = "QDR.X" in [c.container for c in connections]
        self.assertFalse(is_qdr_x) # Should not be present now

        # re-create the ssl profile
        local_node.create({'type': 'sslProfile',
                     'name': 'client-ssl-profile',
                     'certFile': self.ssl_file('client-certificate.pem'),
                     'privateKeyFile': self.ssl_file('client-private-key.pem'),
                     'password': 'client-password',
                     'caCertFile': self.ssl_file('ca-certificate.pem')})
        # re-create connector
        local_node.create({'type': 'connector',
                     'name': 'connectorToX',
                     'host': '127.0.0.1',
                     'port': self.x_listener_port,
                     'saslMechanisms': 'PLAIN',
                     'sslProfile': 'client-ssl-profile',
                     'role': 'inter-router',
                     'verifyHostname': False,
                     'saslUsername': 'test@domain.com',
                     'saslPassword': 'password'})
        self.routers[1].wait_connectors()
        results = local_node.query(type='org.apache.qpid.dispatch.connection').results

        self.common_asserts(results)


if __name__ == '__main__':
    unittest.main(main_module())


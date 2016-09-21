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

import unittest, os, json
from time import sleep
from subprocess import PIPE, Popen, STDOUT
from system_test import TestCase, Qdrouterd, main_module, DIR, TIMEOUT, Process

from qpid_dispatch.management.client import Node

class RouterTestPlainSaslCommon(TestCase):

    @classmethod
    def router(cls, name, connection):

        config = Qdrouterd.Config(connection)

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
                     ('listener', {'host': '0.0.0.0', 'role': 'normal', 'port': cls.tester.get_port(),
                                   'saslMechanisms':'PLAIN', 'authenticatePeer': 'yes'}),
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

    def test_qdstat_connect_sasl(self):
        """
        Make qdstat use sasl plain authentication.
        """
        p = self.popen(
            ['qdstat', '-b', str(self.routers[0].addresses[2]), '-c', '--sasl-mechanisms=PLAIN',
             '--sasl-username=test@domain.com', '--sasl-password=password'],
            name='qdstat-'+self.id(), stdout=PIPE, expect=None)

        out = p.communicate()[0]
        assert p.returncode == 0, \
            "qdstat exit status %s, output:\n%s" % (p.returncode, out)

        split_list = out.split()

        # There will be 2 connections that have authenticated using SASL PLAIN. One inter-router connection
        # and the other connection that this qdstat client is making
        self.assertEqual(2, split_list.count("test@domain.com(PLAIN)"))
        self.assertEqual(1, split_list.count("inter-router"))
        self.assertEqual(1, split_list.count("normal"))

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
            name='qdstat-'+self.id(), stdout=PIPE, expect=None)

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
                     ('listener', {'host': '0.0.0.0', 'role': 'normal', 'port': cls.tester.get_port(),
                                   'authenticatePeer': 'no'}),
                     ('listener', {'host': '0.0.0.0', 'role': 'normal', 'port': cls.tester.get_port(),
                                   'sslProfile':'server-ssl-profile',
                                   'saslMechanisms':'PLAIN', 'authenticatePeer': 'yes'}),
                     ('sslProfile', {'name': 'server-ssl-profile',
                                     'certDb': cls.ssl_file('ca-certificate.pem'),
                                     'certFile': cls.ssl_file('server-certificate.pem'),
                                     'keyFile': cls.ssl_file('server-private-key.pem'),
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
                                    'sslProfile': 'client-ssl-profile',
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
                                     'certDb': cls.ssl_file('ca-certificate.pem'),
                                     'certFile': cls.ssl_file('client-certificate.pem'),
                                     'keyFile': cls.ssl_file('client-private-key.pem'),
                                     'password': 'client-password'}),
        ])

        cls.routers[1].wait_router_connected('QDR.X')

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
            name='qdstat-'+self.id(), stdout=PIPE, expect=None)

        out = p.communicate()[0]
        assert p.returncode == 0, \
            "qdstat exit status %s, output:\n%s" % (p.returncode, out)

        split_list = out.split()

        # There will be 2 connections that have authenticated using SASL PLAIN. One inter-router connection
        # and the other connection that this qdstat client is making
        self.assertEqual(2, split_list.count("test@domain.com(PLAIN)"))
        self.assertEqual(1, split_list.count("inter-router"))
        self.assertEqual(1, split_list.count("normal"))

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
                                     'certDb': cls.ssl_file('ca-certificate.pem'),
                                     'certFile': cls.ssl_file('server-certificate.pem'),
                                     'keyFile': cls.ssl_file('server-private-key.pem'),
                                     'password': 'server-password'}),
                     ('router', {'workerThreads': 1,
                                 'routerId': 'QDR.X',
                                 'mode': 'interior',
                                 'saslConfigName': 'tests-mech-PLAIN',
                                 'saslConfigPath': os.getcwd()}),
        ])

        super(RouterTestVerifyHostNameYes, cls).router('Y', [
                     ('connector', {'addr': '127.0.0.1', 'role': 'inter-router', 'port': x_listener_port,
                                    'sslProfile': 'client-ssl-profile',
                                    'verifyHostName': 'yes',
                                    'saslMechanisms': 'PLAIN',
                                    'saslUsername': 'test@domain.com',
                                    'saslPassword': 'password'}),
                     ('router', {'workerThreads': 1,
                                 'mode': 'interior',
                                 'routerId': 'QDR.Y'}),
                     ('listener', {'addr': '0.0.0.0', 'role': 'normal', 'port': y_listener_port}),
                     ('sslProfile', {'name': 'client-ssl-profile',
                                     'certDb': cls.ssl_file('ca-certificate.pem'),
                                     'certFile': cls.ssl_file('client-certificate.pem'),
                                     'keyFile': cls.ssl_file('client-private-key.pem'),
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

    x_listener_port = None

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
        RouterTestVerifyHostNameNo.x_listener_port = x_listener_port
        y_listener_port = cls.tester.get_port()

        super(RouterTestVerifyHostNameNo, cls).router('X', [
                     ('listener', {'addr': '0.0.0.0', 'role': 'inter-router', 'port': x_listener_port,
                                   'sslProfile':'server-ssl-profile',
                                   'saslMechanisms':'PLAIN', 'authenticatePeer': 'yes'}),
                     # This unauthenticated listener is for qdstat to connect to it.
                     ('listener', {'addr': '0.0.0.0', 'role': 'normal', 'port': cls.tester.get_port(),
                                   'authenticatePeer': 'no'}),
                     ('sslProfile', {'name': 'server-ssl-profile',
                                     'certDb': cls.ssl_file('ca-certificate.pem'),
                                     'certFile': cls.ssl_file('server-certificate.pem'),
                                     'keyFile': cls.ssl_file('server-private-key.pem'),
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
                     ('connector', {'name': 'connectorToX',
                                    'addr': '127.0.0.1', 'role': 'inter-router',
                                    'port': x_listener_port,
                                    'sslProfile': 'client-ssl-profile',
                                    # Provide a sasl user name and password to connect to QDR.X
                                    'saslMechanisms': 'PLAIN',
                                    'verifyHostName': 'no',
                                    'saslUsername': 'test@domain.com', 'saslPassword': 'password'}),
                     ('router', {'workerThreads': 1,
                                 'mode': 'interior',
                                 'routerId': 'QDR.Y'}),
                     ('listener', {'host': '0.0.0.0', 'role': 'normal', 'port': y_listener_port}),
                     ('sslProfile', {'name': 'client-ssl-profile',
                                     'certDb': cls.ssl_file('ca-certificate.pem'),
                                     'certFile': cls.ssl_file('client-certificate.pem'),
                                     'keyFile': cls.ssl_file('client-private-key.pem'),
                                     'password': 'client-password'}),
        ])

        cls.routers[0].wait_ports()
        cls.routers[1].wait_ports()
        cls.routers[1].wait_router_connected('QDR.X')

    @staticmethod
    def ssl_file(name):
        return os.path.join(DIR, 'ssl_certs', name)

    def run_qdmanage(self, cmd, input=None, expect=Process.EXIT_OK, address=None):
        p = self.popen(
            ['qdmanage'] + cmd.split(' ') + ['--bus', address or self.address(), '--indent=-1', '--timeout',
                                             str(TIMEOUT)], stdin=PIPE, stdout=PIPE, stderr=STDOUT, expect=expect)
        out = p.communicate(input)[0]
        try:
            p.teardown()
        except Exception, e:
            raise Exception("%s\n%s" % (e, out))
        return out

    def common_asserts(self, results):
        search = "QDR.X"
        found = False

        for N in range(0, len(results)):
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

    def test_inter_router_plain_over_ssl_exists(self):
        """
        Tests to make sure that an inter-router connection exists between the routers since verifyHostName is 'no'.
        """
        local_node = Node.connect(self.routers[1].addresses[0], timeout=TIMEOUT)

        results = local_node.query(type='org.apache.qpid.dispatch.connection').results

        self.common_asserts(results)

    def test_zzz_delete_create_connector(self):
        """
        Delete an ssl profile before deleting the connector and make sure it fails.
        Delete an ssl profile after deleting the connector and make sure it succeeds.
        Re-add the deleted connector and associate it with an ssl profile and make sure
        that the two routers are able to communicate over the connection.
        """

        ssl_profile_name = 'client-ssl-profile'

        delete_command = 'DELETE --type=sslProfile --name=' + ssl_profile_name

        cannot_delete = False
        try:
            json.loads(self.run_qdmanage(delete_command, address=self.routers[1].addresses[0]))
        except Exception as e:
            cannot_delete = True
            self.assertTrue('ForbiddenStatus: SSL Profile is referenced by other listeners/connectors' in e.message)

        self.assertTrue(cannot_delete)

        # Deleting the connector
        delete_command = 'DELETE --type=connector --name=connectorToX'
        self.run_qdmanage(delete_command, address=self.routers[1].addresses[0])

        #Assert here that the connection to QDR.X is gone

        # Re-add connector
        connector_create_command = 'CREATE --type=connector name=connectorToX host=127.0.0.1 port=' + \
                                   str(RouterTestVerifyHostNameNo.x_listener_port) + \
                                   ' saslMechanisms=PLAIN sslProfile=' + ssl_profile_name + \
                                   ' role=inter-router verifyHostName=no saslUsername=test@domain.com' \
                                   ' saslPassword=password'

        json.loads(self.run_qdmanage(connector_create_command, address=self.routers[1].addresses[0]))
        sleep(1)
        local_node = Node.connect(self.routers[1].addresses[0], timeout=TIMEOUT)
        results = local_node.query(type='org.apache.qpid.dispatch.connection').results
        self.common_asserts(results)

    def test_zzz_delete_create_ssl_profile(self):
        """
        Deletes a connector and its corresponding ssl profile and recreates both
        """

        ssl_profile_name = 'client-ssl-profile'

        # Deleting the connector first and then its SSL profile must work.
        delete_command = 'DELETE --type=connector --name=connectorToX'
        self.run_qdmanage(delete_command, address=self.routers[1].addresses[0])

        # Delete the connector's associated ssl profile
        delete_command = 'DELETE --type=sslProfile --name=' + ssl_profile_name
        self.run_qdmanage(delete_command, address=self.routers[1].addresses[0])

        local_node = Node.connect(self.routers[1].addresses[0], timeout=TIMEOUT)
        results = local_node.query(type='org.apache.qpid.dispatch.connection').results
        search = "QDR.X"
        found = False

        for N in range(0, 3):
            if results[N][0] == search:
                found = True
                break

        self.assertFalse(found)

        # re-create the ssl profile
        long_type = 'org.apache.qpid.dispatch.sslProfile'
        ssl_create_command = 'CREATE --type=' + long_type + ' certFile=' + self.ssl_file('client-certificate.pem') + \
                             ' keyFile=' + self.ssl_file('client-private-key.pem') + ' password=client-password' + \
                             ' name=' + ssl_profile_name + ' certDb=' + self.ssl_file('ca-certificate.pem')

        output = json.loads(self.run_qdmanage(ssl_create_command, address=self.routers[1].addresses[0]))
        name = output['name']
        self.assertEqual(name, ssl_profile_name)

        # Re-add connector
        connector_create_command = 'CREATE --type=connector name=connectorToX host=127.0.0.1 port=' + \
                                   str(RouterTestVerifyHostNameNo.x_listener_port) + \
                                   ' saslMechanisms=PLAIN sslProfile=' + ssl_profile_name + \
                                   ' role=inter-router verifyHostName=no saslUsername=test@domain.com' \
                                   ' saslPassword=password'

        json.loads(self.run_qdmanage(connector_create_command, address=self.routers[1].addresses[0]))

        sleep(1)

        local_node = Node.connect(self.routers[1].addresses[0], timeout=TIMEOUT)
        results = local_node.query(type='org.apache.qpid.dispatch.connection').results

        self.common_asserts(results)


if __name__ == '__main__':
    unittest.main(main_module())


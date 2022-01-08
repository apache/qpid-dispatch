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

import os
from subprocess import PIPE, Popen
from system_test import TestCase, Qdrouterd, main_module
from system_test import unittest
from proton import SASL
from proton.handlers import MessagingHandler
from proton.reactor import Container


class AuthServicePluginTest(TestCase):

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
mech_list: SCRAM-SHA1 PLAIN
# The following line stops spurious 'sql_select option missing' errors when cyrus-sql-sasl plugin is installed
sql_select: dummy select
""")

    @classmethod
    def setUpClass(cls):
        """
        Tests the delegation of sasl auth to an external auth service.

        Creates two routers, one acts as the authe service, the other configures the auth service plugin
        to point at this auth service.

        """
        super(AuthServicePluginTest, cls).setUpClass()

        if not SASL.extended():
            return

        cls.createSaslFiles()

        print('launching auth service...')
        auth_service_port = cls.tester.get_port()
        cls.tester.qdrouterd('auth_service', Qdrouterd.Config([
            ('listener', {'host': '0.0.0.0', 'role': 'normal', 'port': auth_service_port,
                          'saslMechanisms': 'PLAIN', 'authenticatePeer': 'yes'}),
            ('router', {'workerThreads': 1,
                        'id': 'auth_service',
                        'mode': 'standalone',
                        'saslConfigName': 'tests-mech-PLAIN',
                        'saslConfigPath': os.getcwd()})
        ])).wait_ready()

        cls.router_port = cls.tester.get_port()
        cls.tester.qdrouterd('router', Qdrouterd.Config([
            ('authServicePlugin', {'name': 'myauth', 'host': '127.0.0.1', 'port': auth_service_port}),
            ('listener', {'host': '0.0.0.0', 'port': cls.router_port, 'role': 'normal', 'saslPlugin': 'myauth', 'saslMechanisms': 'PLAIN'}),
            ('router', {'mode': 'standalone', 'id': 'router'})
        ])).wait_ready()

    @unittest.skipIf(not SASL.extended(), "Cyrus library not available. skipping test")
    def test_valid_credentials(self):
        """Check authentication succeeds when valid credentials are presented."""
        test = SimpleConnect("127.0.0.1:%d" % self.router_port, 'test@domain.com', 'password')
        test.run()
        self.assertEqual(True, test.connected)
        self.assertIsNone(test.error)

    @unittest.skipIf(not SASL.extended(), "Cyrus library not available. skipping test")
    def test_invalid_credentials(self):
        """Check authentication fails when invalid credentials are presented."""
        test = SimpleConnect("127.0.0.1:%d" % self.router_port, 'test@domain.com', 'foo')
        test.run()
        self.assertEqual(False, test.connected)
        self.assertEqual('amqp:unauthorized-access', test.error.name)
        self.assertEqual(test.error.description.startswith('Authentication failed'), True)


class AuthServicePluginDeprecatedTest(AuthServicePluginTest):

    @classmethod
    def setUpClass(cls):
        """
        Tests the delegation of sasl auth to an external auth service.

        Creates two routers, one acts as the authe service, the other configures the auth service plugin
        to point at this auth service.

        """
        super(AuthServicePluginTest, cls).setUpClass()

        if not SASL.extended():
            return

        cls.createSaslFiles()

        print('launching auth service...')
        auth_service_port = cls.tester.get_port()
        cls.tester.qdrouterd('auth_service', Qdrouterd.Config([
            ('listener', {'host': '0.0.0.0', 'role': 'normal', 'port': auth_service_port,
                          'saslMechanisms': 'PLAIN', 'authenticatePeer': 'yes'}),
            ('router', {'workerThreads': 1,
                        'id': 'auth_service',
                        'mode': 'standalone',
                        'saslConfigName': 'tests-mech-PLAIN',
                        'saslConfigPath': os.getcwd()})
        ])).wait_ready()

        cls.router_port = cls.tester.get_port()
        cls.tester.qdrouterd('router', Qdrouterd.Config([
            ('authServicePlugin', {'name': 'myauth', 'authService': '127.0.0.1:%d' % auth_service_port}),
            ('listener', {'host': '0.0.0.0', 'port': cls.router_port, 'role': 'normal',
                          'saslPlugin': 'myauth', 'saslMechanisms': 'PLAIN'}),
            ('router', {'mode': 'standalone', 'id': 'router'})
        ])).wait_ready()


class SimpleConnect(MessagingHandler):

    def __init__(self, url, username, password):
        super(SimpleConnect, self).__init__()
        self.url = url
        self.username = username
        self.password = password
        self.connected = False
        self.error = None

    def on_start(self, event):
        event.container.user = self.username
        event.container.password = self.password
        conn = event.container.connect(self.url)

    def on_connection_opened(self, event):
        self.connected = True
        event.connection.close()

    def on_transport_error(self, event):
        self.error = event.transport.condition
        if event.connection:
            event.connection.close()
        else:
            print("ERROR: %s" % self.error)

    def run(self):
        Container(self).run()


if __name__ == '__main__':
    unittest.main(main_module())

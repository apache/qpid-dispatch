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
import sys
from subprocess import PIPE, Popen

from proton import Message, SASL
from proton.handlers import MessagingHandler
from proton.reactor import Container

import system_test
from system_test import TestCase, Qdrouterd, main_module, DIR, Process
from system_test import unittest


class AuthServicePluginAuthzTest(TestCase):

    @classmethod
    def addUser(cls, user, password):
        # Create a sasl database.
        p = Popen(['saslpasswd2', '-c', '-p', '-f', 'users.sasldb', user],
                  stdin=PIPE, stdout=PIPE, stderr=PIPE, universal_newlines=True)
        result = p.communicate(password)
        assert p.returncode == 0, "saslpasswd2 exit status %s, output:\n%s" % (p.returncode, result)

    @classmethod
    def createSaslFiles(cls):
        cls.addUser('guest', 'guest')
        cls.addUser('admin', 'admin')
        # Create a SASL configuration file.
        with open('tests-mech-SCRAM.conf', 'w') as sasl_conf:
            sasl_conf.write("""
mech_list: SCRAM-SHA-1 PLAIN
""")
        with open('proton-server.conf', 'w') as sasl_conf:
            sasl_conf.write("""
pwcheck_method: auxprop
auxprop_plugin: sasldb
sasldb_path: users.sasldb
mech_list: SCRAM-SHA-1 PLAIN
""")

    @staticmethod
    def ssl_file(name):
        return os.path.join(system_test.DIR, 'ssl_certs', name)

    @classmethod
    def setUpClass(cls):
        """Tests the delegation of sasl auth to an external auth service."""
        super(AuthServicePluginAuthzTest, cls).setUpClass()

        if not SASL.extended():
            return

        cls.createSaslFiles()

        cls.auth_service_port = cls.tester.get_port()
        cls.tester.popen([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'authservice.py'),
                          '-a', 'amqps://localhost:%d' % cls.auth_service_port, '-c', os.getcwd()], expect=Process.RUNNING)

        policy_config_path = os.path.join(DIR, 'policy-authz')

        cls.router_port = cls.tester.get_port()
        cls.tester.qdrouterd('router', Qdrouterd.Config([
            ('sslProfile', {'name': 'myssl', 'caCertFile': cls.ssl_file('ca-certificate.pem')}),
            ('policy', {'maxConnections': 2, 'policyDir': policy_config_path, 'enableVhostPolicy': 'true'}),
            # authService attribute has been deprecated. We are using it here to make sure that we are
            # still backward compatible.
            ('authServicePlugin', {'name': 'myauth', 'sslProfile': 'myssl', 'port': cls.auth_service_port, 'host': 'localhost'}),
            ('listener', {'host': 'localhost', 'port': cls.router_port, 'role': 'normal', 'saslPlugin': 'myauth', 'saslMechanisms': 'SCRAM-SHA-1 PLAIN'}),
            ('router', {'mode': 'standalone', 'id': 'router',
                        'saslConfigName': 'tests-mech-SCRAM',
                        'saslConfigPath': os.getcwd()})
        ])).wait_ready()

    @unittest.skipIf(not SASL.extended(), "Cyrus library not available. skipping test")
    def test_authorized(self):
        container = Container()
        client = ConnectionHandler('foo', 1)
        container.connect("guest:guest@127.0.0.1:%d" % self.router_port, handler=client)
        container.run()
        self.assertEqual(1, client.sent)
        self.assertEqual(1, client.received)
        self.assertEqual(0, len(client.errors))

    @unittest.skipIf(not SASL.extended(), "Cyrus library not available. skipping test")
    def test_unauthorized(self):
        container = Container()
        client = ConnectionHandler('bar', 1)
        container.connect("guest:guest@127.0.0.1:%d" % self.router_port, handler=client)
        container.run()
        self.assertEqual(0, client.sent)
        self.assertEqual(0, client.received)
        self.assertEqual(2, len(client.errors))
        self.assertEqual('amqp:unauthorized-access', client.errors[0])
        self.assertEqual('amqp:unauthorized-access', client.errors[1])

    @unittest.skipIf(not SASL.extended(), "Cyrus library not available. skipping test")
    def test_wildcard(self):
        container = Container()
        client = ConnectionHandler('whatever', 1)
        container.connect("admin:admin@127.0.0.1:%d" % self.router_port, handler=client)
        container.run()
        self.assertEqual(1, client.sent)
        self.assertEqual(1, client.received)
        self.assertEqual(0, len(client.errors))

    @unittest.skipIf(not SASL.extended(), "Cyrus library not available. skipping test")
    def test_dynamic_source_anonymous_sender(self):
        container = Container()
        client = DynamicSourceAnonymousSender()
        container.connect("admin:admin@127.0.0.1:%d" % self.router_port, handler=client)
        container.run()
        self.assertEqual(1, client.accepted)
        self.assertEqual('hello', client.message)
        self.assertEqual(0, len(client.errors))

    @unittest.skipIf(not SASL.extended(), "Cyrus library not available. skipping test")
    def test_unauthorized_anonymous_sender_target(self):
        container = Container()
        client = DynamicSourceAnonymousSender()
        container.connect("guest:guest@127.0.0.1:%d" % self.router_port, handler=client)
        container.run()
        self.assertEqual(0, client.accepted)
        self.assertEqual(1, client.rejected)
        self.assertIsNone(client.message)


class AuthServicePluginAuthzDeprecatedTest(AuthServicePluginAuthzTest):

    @classmethod
    def setUpClass(cls):
        """Tests the delegation of sasl auth to an external auth service."""
        super(AuthServicePluginAuthzTest, cls).setUpClass()

        if not SASL.extended():
            return

        cls.createSaslFiles()

        cls.auth_service_port = cls.tester.get_port()
        cls.tester.popen([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'authservice.py'),
                          '-a', 'amqps://localhost:%d' % cls.auth_service_port, '-c', os.getcwd()], expect=Process.RUNNING)

        cls.router_port = cls.tester.get_port()
        cls.tester.qdrouterd('router', Qdrouterd.Config([
            ('sslProfile', {'name': 'myssl', 'caCertFile': cls.ssl_file('ca-certificate.pem')}),
            # authService and authSslProfile attributea have been deprecated.
            # We are using it here to make sure that we are backward compatible.
            ('authServicePlugin', {'name': 'myauth', 'authSslProfile': 'myssl', 'authService': 'localhost:%d' % cls.auth_service_port}),
            ('listener', {'host': 'localhost', 'port': cls.router_port, 'role': 'normal', 'saslPlugin': 'myauth', 'saslMechanisms': 'SCRAM-SHA-1 PLAIN'}),
            ('router', {'mode': 'standalone', 'id': 'router',
                        'saslConfigName': 'tests-mech-SCRAM',
                        'saslConfigPath': os.getcwd()})
        ])).wait_ready()


class ConnectionHandler(MessagingHandler):

    def __init__(self, address, count):
        super(ConnectionHandler, self).__init__()
        self.address = address
        self.count = count
        self.received = 0
        self.sent = 0
        self.errors = []

    def on_message(self, event):
        self.received += 1
        if self.received == self.count:
            event.connection.close()

    def on_sendable(self, event):
        if self.sent < self.count:
            self.sent += 1
            event.sender.send(Message(body='msg-%s' % self.sent))

    def on_link_error(self, event):
        self.errors.append(event.link.remote_condition.name)
        event.connection.close()

    def on_connection_opened(self, event):
        event.container.create_receiver(event.connection, self.address)
        event.container.create_sender(event.connection, self.address)


class DynamicSourceAnonymousSender(MessagingHandler):

    def __init__(self):
        super(DynamicSourceAnonymousSender, self).__init__()
        self.sender = None
        self.message = None
        self.accepted = 0
        self.rejected = 0
        self.errors = []

    def on_message(self, event):
        self.message = event.message.body

    def on_link_opened(self, event):
        if event.receiver:
            self.sender.send(Message(address=event.receiver.remote_source.address, body='hello'))

    def on_connection_opened(self, event):
        event.container.create_receiver(event.connection, None, dynamic=True)
        self.sender = event.container.create_sender(event.connection, None)

    def on_accepted(self, event):
        self.accepted += 1
        event.connection.close()

    def on_rejected(self, event):
        self.rejected += 1
        event.connection.close()


if __name__ == '__main__':
    unittest.main(main_module())

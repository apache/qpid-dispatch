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

import unittest2 as unitest
import os, json
from subprocess import PIPE, Popen, STDOUT
from system_test import TestCase, Qdrouterd, main_module, DIR, TIMEOUT, Process
from proton import Array, Data, Message, SASL, symbol, UNDESCRIBED
from proton.handlers import MessagingHandler
from proton.reactor import Container

class AuthServicePluginAuthzTest(TestCase):
    @classmethod
    def addUser(cls, user, password):
        # Create a sasl database.
        p = Popen(['saslpasswd2', '-c', '-p', '-f', 'users.sasldb', user],
                  stdin=PIPE, stdout=PIPE, stderr=PIPE)
        result = p.communicate(password)
        assert p.returncode == 0, "saslpasswd2 exit status %s, output:\n%s" % (p.returncode, result)

    @classmethod
    def createSaslFiles(cls):
        cls.addUser('guest', 'guest')
        cls.addUser('admin', 'admin')
        # Create a SASL configuration file.
        with open('tests-mech-SCRAM.conf', 'w') as sasl_conf:
            sasl_conf.write("""
mech_list: SCRAM-SHA-1
""")
        with open('proton-server.conf', 'w') as sasl_conf:
            sasl_conf.write("""
pwcheck_method: auxprop
auxprop_plugin: sasldb
sasldb_path: users.sasldb
mech_list: SCRAM-SHA-1
""")


    @classmethod
    def setUpClass(cls):
        """
        Tests the delegation of sasl auth to an external auth service.
        """
        super(AuthServicePluginAuthzTest, cls).setUpClass()

        if not SASL.extended():
            return

        authservice = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'authservice.py')
        if not os.path.isfile(authservice):
            print('authservice script does NOT exist: %s' % authservice)
        else:
            print('authservice script DOES exist: %s' % authservice)

        cls.createSaslFiles()

        cls.auth_service_port = cls.tester.get_port()
        cls.tester.popen(['/usr/bin/env', 'python', authservice, '-a', '127.0.0.1:%d' % cls.auth_service_port, '-c', os.getcwd()], expect=Process.RUNNING)

        cls.router_port = cls.tester.get_port()
        cls.tester.qdrouterd('router', Qdrouterd.Config([
                     ('authServicePlugin', {'name':'myauth', 'authService': '127.0.0.1:%d' % cls.auth_service_port}),
                     ('listener', {'host': '0.0.0.0', 'port': cls.router_port, 'role': 'normal', 'saslPlugin':'myauth', 'saslMechanisms':'SCRAM-SHA-1'}),
                     ('router', {'mode': 'standalone', 'id': 'router',
                                 'saslConfigName': 'tests-mech-SCRAM',
                                 'saslConfigPath': os.getcwd()})
        ])).wait_ready()

    def test_authorized(self):
        if not SASL.extended():
            self.skipTest("Cyrus library not available. skipping test")

        container = Container()
        client = ConnectionHandler('foo', 1)
        container.connect("guest:guest@127.0.0.1:%d" % self.router_port, handler=client)
        container.run()
        self.assertEqual(1, client.sent)
        self.assertEqual(1, client.received)
        self.assertEqual(0, len(client.errors))

    def test_unauthorized(self):
        if not SASL.extended():
            self.skipTest("Cyrus library not available. skipping test")

        container = Container()
        client = ConnectionHandler('bar', 1)
        container.connect("guest:guest@127.0.0.1:%d" % self.router_port, handler=client)
        container.run()
        self.assertEqual(0, client.sent)
        self.assertEqual(0, client.received)
        self.assertEqual(2, len(client.errors))
        self.assertEqual('amqp:unauthorized-access', client.errors[0])
        self.assertEqual('amqp:unauthorized-access', client.errors[1])

    def test_wildcard(self):
        if not SASL.extended():
            self.skipTest("Cyrus library not available. skipping test")

        container = Container()
        client = ConnectionHandler('whatever', 1)
        container.connect("admin:admin@127.0.0.1:%d" % self.router_port, handler=client)
        container.run()
        self.assertEqual(1, client.sent)
        self.assertEqual(1, client.received)
        self.assertEqual(0, len(client.errors))


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
            event.sender.send(Message(body='msg-%s' %self.sent))

    def on_link_error(self, event):
        self.errors.append(event.link.remote_condition.name)
        event.connection.close()

    def on_connection_opened(self, event):
        event.container.create_receiver(event.connection, self.address)
        event.container.create_sender(event.connection, self.address)

if __name__ == '__main__':
    unittest.main(main_module())


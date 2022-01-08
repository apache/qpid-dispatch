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

from proton import Message
from proton.handlers import MessagingHandler
from proton.reactor import Container
from system_test import TestCase, Qdrouterd, main_module
from system_test import unittest
from qpid_dispatch_internal.policy.policy_util import is_ipv6_enabled


class ProtocolFamilyTest(TestCase):

    @classmethod
    def setUpClass(cls):
        """
        Starts three routers with various listeners and connectors.
        There is a call to wait_router_connected to make sure that the routers are able to communicate with each
        other on ports using the assigned protocol family.
        """
        super(ProtocolFamilyTest, cls).setUpClass()

        def router(name, connection):

            config = [
                ('router', {'mode': 'interior', 'id': 'QDR.%s' % name}),

                # No protocolFamily is specified for this listener.
                # This will test if the router defaults host to 127.0.0.1 and if the router auto-detects protocol family

                ('listener', {'port': cls.tester.get_port()}),

                # Specify host as 127.0.0.1 and protocol family as IPv4
                ('listener', {'host': '127.0.0.1', 'protocolFamily': 'IPv4', 'port': cls.tester.get_port()}),

                # Specify protocol family as IPv4 but don't specify any host
                # This will test if the router defaults the host field to 127.0.0.1
                ('listener', {'protocolFamily': 'IPv4', 'port': cls.tester.get_port()}),

                # Specify the host as 127.0.0.1
                # This will test router's auto-detection of protocol family
                ('listener', {'host': '127.0.0.1', 'port': cls.tester.get_port()}),


                # Specify host as ::1 and protocol family as IPv6
                ('listener', {'host': '::1', 'protocolFamily': 'IPv6', 'port': cls.tester.get_port(protocol_family='IPv6')}),

            ] + connection

            config = Qdrouterd.Config(config)

            # The wait=True attempts to connect to each listening port with the appropriate protocol family
            # and tests each connector
            cls.routers.append(cls.tester.qdrouterd(name, config, wait=True))

        if not is_ipv6_enabled():
            return

        cls.routers = []

        inter_router_port = cls.tester.get_port(protocol_family='IPv6')
        inter_router_ipv4_port = cls.tester.get_port(protocol_family='IPv4')

        router('A',
               [
                   ('listener', {'host': '::1', 'role': 'inter-router', 'protocolFamily': 'IPv6', 'port': inter_router_port})
               ]
               )

        router('B',
               [
                   # Tests an IPv6 connector
                   ('connector', {'host': '::1', 'role': 'inter-router', 'protocolFamily': 'IPv6', 'port': inter_router_port}),
                   ('listener', {'host': '127.0.0.1', 'role': 'inter-router', 'port': inter_router_ipv4_port})
               ]

               )

        router('C',
               [
                   # Tests an IPv4 connector
                   ('connector', {'host': '127.0.0.1', 'role': 'inter-router', 'port': inter_router_ipv4_port})
               ]
               )
        cls.routers[0].wait_router_connected('QDR.B')
        cls.routers[1].wait_router_connected('QDR.A')
        cls.routers[2].wait_router_connected('QDR.B')

    # Without at least one test the setUpClass does not execute
    # If this test has started executing, it means that the setUpClass() has successfully executed which means that
    # the routers were able to communicate with each other successfully using the specified protocol family.

    def test_simple_send_receive(self):

        if not is_ipv6_enabled():
            return self.skipTest("Skipping test..IPV6 not enabled")

        simple_send_receive_test = SimpleSndRecv(self.routers[0].addresses[4])
        simple_send_receive_test.run()
        self.assertTrue(simple_send_receive_test.message_received)


class SimpleSndRecv(MessagingHandler):

    def __init__(self, address):
        super(SimpleSndRecv, self).__init__()
        self.address = address
        self.sender = None
        self.receiver = None
        self.conn = None
        self.message_received = False

    def on_start(self, event):
        self.conn = event.container.connect(self.address)
        self.receiver = event.container.create_receiver(self.conn, "test_addr")
        self.sender = event.container.create_sender(self.conn, "test_addr")

    def on_sendable(self, event):
        msg = Message(body="Hello World")
        event.sender.send(msg)

    def on_message(self, event):
        if "Hello World" == event.message.body:
            self.message_received = True
            self.conn.close()

    def run(self):
        Container(self).run()


if __name__ == '__main__':
    unittest.main(main_module())

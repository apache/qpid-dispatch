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

import unittest
from time import sleep
from proton import Message
from system_test import TestCase, Qdrouterd, main_module

try:
    from proton import MODIFIED
except ImportError:
    from proton import PN_STATUS_MODIFIED as MODIFIED


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
                ('router', {'mode': 'interior', 'id': 'QDR.%s'%name}),

                # No protocolFamily is specified for this listener.
                # This will test if the router defaults host to 127.0.0.1 and if the router auto-detects protocol family

                ('listener', {'port': cls.tester.get_port()}),

                # Specify host as 127.0.0.1 and protocol family as IPv4
                ('listener', {'host': '127.0.0.1', 'protocolFamily': 'IPv4','port': cls.tester.get_port()}),

                # Specify protocol family as IPv4 but don't specify any host
                # This will test if the router defaults the host field to 127.0.0.1
                ('listener', {'protocolFamily': 'IPv4', 'port': cls.tester.get_port()}),

                # Specify the host as 127.0.0.1
                # This will test router's auto-detection of protocol family
                ('listener', {'host': '127.0.0.1', 'port': cls.tester.get_port()}),


                # Specify host as ::1 and protocol family as IPv6
                ('listener', {'host': '::1', 'protocolFamily': 'IPv6', 'port': cls.tester.get_port(protocol_family='IPv6')}),

                ('fixedAddress', {'prefix': '/closest/', 'fanout': 'single', 'bias': 'closest'}),
                ('fixedAddress', {'prefix': '/spread/', 'fanout': 'single', 'bias': 'spread'}),
                ('fixedAddress', {'prefix': '/multicast/', 'fanout': 'multiple'}),
                ('fixedAddress', {'prefix': '/', 'fanout': 'multiple'}),

            ] + connection

            config = Qdrouterd.Config(config)

            # The wait=True attempts to connect to each listening port with the appropriate protocol family
            # and tests each connector
            cls.routers.append(cls.tester.qdrouterd(name, config, wait=True))

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
    def test_simple_pre_settled(self):
        addr = self.routers[0].addresses[4]+"/test/1"
        M1 = self.messenger()
        M2 = self.messenger()

        M1.start()
        M2.start()
        M2.subscribe(addr)

        tm = Message()
        rm = Message()

        tm.address = addr
        for i in range(100):
            tm.body = {'number': i}
            M1.put(tm)
        M1.send()

        for i in range(100):
            M2.recv(1)
            M2.get(rm)
            self.assertEqual(i, rm.body['number'])

        M1.stop()
        M2.stop()

if __name__ == '__main__':
    unittest.main(main_module())
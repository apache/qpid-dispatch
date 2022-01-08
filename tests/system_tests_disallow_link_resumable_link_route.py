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

from time import sleep

from system_test import TestCase, Qdrouterd, main_module, TIMEOUT
from system_test import unittest

from proton import Terminus
from proton.reactor import DurableSubscription, SenderOption
from proton.utils import BlockingConnection, LinkDetached

from qpid_dispatch.management.client import Node


class SenderExpiry(SenderOption):

    def __init__(self, expiry):
        self.expiry = expiry

    def apply(self, sender):
        sender.target.expiry_policy = self.expiry


class SenderTimeout(SenderOption):

    def __init__(self, timeout):
        self.timeout = timeout

    def apply(self, sender):
        sender.target.timeout = self.timeout


class LinkRouteTest(TestCase):

    @classmethod
    def get_router(cls, index):
        return cls.routers[index]

    @classmethod
    def setUpClass(cls):
        """Start three routers"""
        super(LinkRouteTest, cls).setUpClass()

        def router(name, config):
            config = Qdrouterd.Config(config)
            cls.routers.append(cls.tester.qdrouterd(name, config, wait=False))

        cls.routers = []
        a_listener_port = cls.tester.get_port()
        b_listener_port = cls.tester.get_port()

        router('A',
               [
                   ('router', {'mode': 'standalone', 'id': 'QDR.A'}),
                   ('listener', {'role': 'normal', 'host': '0.0.0.0', 'port': a_listener_port, 'saslMechanisms': 'ANONYMOUS'}),
               ])
        router('B',
               [
                   # disallow resumable links
                   ('router', {'mode': 'interior', 'id': 'QDR.B', 'allowResumableLinkRoute': False}),
                   ('listener', {'role': 'normal', 'host': '0.0.0.0', 'port': b_listener_port, 'saslMechanisms': 'ANONYMOUS'}),
                   # define link routes
                   ('connector', {'name': 'broker', 'role': 'route-container', 'host': '0.0.0.0', 'port': a_listener_port, 'saslMechanisms': 'ANONYMOUS'}),
                   ('linkRoute', {'prefix': 'org.apache', 'containerId': 'QDR.A', 'dir': 'in'}),
                   ('linkRoute', {'prefix': 'org.apache', 'containerId': 'QDR.A', 'dir': 'out'}),
               ]
               )
        cls.routers[0].wait_ports()
        cls.routers[1].wait_ports()

    def test_000_wait_for_link_route_up(self):
        # wait up to 60 seconds for link route to get set up
        # The name of this test must dictate that it runs first
        wLoops = 600
        wTimeS = 0.1
        waitTimeS = float(wLoops) * wTimeS
        local_node = Node.connect(self.routers[1].addresses[0], timeout=TIMEOUT)
        counted = False
        for i in range(wLoops):
            try:
                results = local_node.query(type='org.apache.qpid.dispatch.router.address',
                                           attribute_names=['name', 'containerCount']
                                           ).results
                for res in results:
                    if res[0] == 'Corg.apache':
                        if res[1] == 1:
                            counted = True
                        break
                if counted:
                    break
                sleep(wTimeS)
            except Exception as e:
                self.fail("Exception: " + str(e))
        if not counted:
            self.fail("Interrouter link route failed to connect after %f seconds" % waitTimeS)

    def test_normal_receiver_allowed(self):
        addr = self.routers[1].addresses[0]

        connection = BlockingConnection(addr)
        receiver = connection.create_receiver(address="org.apache")
        connection.close()

    def test_resumable_receiver_disallowed(self):
        addr = self.routers[1].addresses[0]

        connection = BlockingConnection(addr)
        try:
            receiver = connection.create_receiver(address="org.apache", options=[DurableSubscription()])
            self.fail("link should have been detached")
        except LinkDetached:
            pass
        connection.close()

    def test_normal_sender_allowed(self):
        addr = self.routers[1].addresses[0]

        connection = BlockingConnection(addr)
        sender = connection.create_sender(address="org.apache")
        connection.close()

    def test_expire_never_sender_disallowed(self):
        addr = self.routers[1].addresses[0]

        connection = BlockingConnection(addr)
        try:
            sender = connection.create_sender(address="org.apache", options=[SenderExpiry(Terminus.EXPIRE_NEVER)])
            self.fail("link should have been detached")
        except LinkDetached:
            pass
        connection.close()

    def test_non_zero_timeout_sender_disallowed(self):
        addr = self.routers[1].addresses[0]

        connection = BlockingConnection(addr)
        try:
            sender = connection.create_sender(address="org.apache", options=[SenderTimeout(10)])
            self.fail("link should have been detached")
        except LinkDetached:
            pass
        connection.close()


if __name__ == '__main__':
    unittest.main(main_module())

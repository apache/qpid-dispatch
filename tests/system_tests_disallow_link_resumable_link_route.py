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

import unittest2 as unittest
from time import sleep, time
from subprocess import PIPE, STDOUT

from system_test import TestCase, Qdrouterd, main_module, TIMEOUT, Process

from proton import Message, Terminus
from proton.reactor import DurableSubscription, SenderOption
from proton.utils import BlockingConnection, LinkDetached

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
                   #disallow resumable links
                   ('router', {'mode': 'interior', 'id': 'QDR.B', 'allowResumableLinkRoute':False}),
                   ('listener', {'role': 'normal', 'host': '0.0.0.0', 'port': b_listener_port, 'saslMechanisms': 'ANONYMOUS'}),
                   #define link routes
                   ('connector', {'name': 'broker', 'role': 'route-container', 'host': '0.0.0.0', 'port': a_listener_port, 'saslMechanisms': 'ANONYMOUS'}),
                   ('linkRoute', {'prefix': 'org.apache', 'containerId': 'QDR.A', 'dir': 'in'}),
                   ('linkRoute', {'prefix': 'org.apache', 'containerId': 'QDR.A', 'dir': 'out'}),
               ]
               )
        sleep(2)


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
        except LinkDetached, e: None
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
        except LinkDetached, e: None
        connection.close()

    def test_non_zero_timeout_sender_disallowed(self):
        addr = self.routers[1].addresses[0]

        connection = BlockingConnection(addr)
        try:
            sender = connection.create_sender(address="org.apache", options=[SenderTimeout(10)])
            self.fail("link should have been detached")
        except LinkDetached, e: None
        connection.close()


if __name__ == '__main__':
    unittest.main(main_module())

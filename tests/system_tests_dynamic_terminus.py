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
from proton.reactor import Container, LinkOption

from system_test import unittest
from system_test import TestCase, Qdrouterd, main_module, TIMEOUT, TestTimeout


class RouterTest(TestCase):

    inter_router_port = None

    @classmethod
    def setUpClass(cls):
        """Start a router"""
        super(RouterTest, cls).setUpClass()

        def router(name, connection):

            config = [
                ('router', {'mode': 'interior', 'id': name}),
                ('listener', {'port': cls.tester.get_port(), 'stripAnnotations': 'no'}),
                ('address', {'prefix': 'closest', 'distribution': 'closest'}),
                ('address', {'prefix': 'spread', 'distribution': 'balanced'}),
                ('address', {'prefix': 'multicast', 'distribution': 'multicast'}),
                connection
            ]

            config = Qdrouterd.Config(config)

            cls.routers.append(cls.tester.qdrouterd(name, config, wait=True))

        cls.routers = []

        inter_router_port = cls.tester.get_port()

        router('A', ('listener', {'role': 'inter-router', 'port': inter_router_port}))
        router('B', ('connector', {'name': 'connectorToA', 'role': 'inter-router', 'port': inter_router_port}))

        cls.routers[0].wait_router_connected('B')
        cls.routers[1].wait_router_connected('A')

    def test_01_dynamic_source_test(self):
        test = DynamicSourceTest(self.routers[0].addresses[0], self.routers[1].addresses[0])
        test.run()
        self.assertIsNone(test.error)

    def test_02_dynamic_target_one_router_test(self):
        test = DynamicTargetTest(self.routers[0].addresses[0], self.routers[0].addresses[0])
        test.run()
        if test.skip:
            self.skipTest(test.skip)
        self.assertIsNone(test.error)

    def test_03_dynamic_target_two_router_test(self):
        test = DynamicTargetTest(self.routers[0].addresses[0], self.routers[1].addresses[0])
        test.run()
        if test.skip:
            self.skipTest(test.skip)
        self.assertIsNone(test.error)


class DynamicSourceTest(MessagingHandler):

    def __init__(self, router_1_host, router_2_host):
        super(DynamicSourceTest, self).__init__()
        self.router_1_host = router_1_host
        self.router_2_host = router_2_host

        self.error         = None
        self.receiver_conn = None
        self.sender_1_conn = None
        self.sender_2_conn = None
        self.sender_1      = None
        self.sender_2      = None
        self.receiver      = None
        self.address       = None

        self.count      = 10
        self.n_sent_1   = 0
        self.n_sent_2   = 0
        self.n_rcvd     = 0
        self.n_accepted = 0

    def timeout(self):
        self.error = "Timeout Expired: n_sent_1=%d n_sent_2=%d n_rcvd=%d n_accepted=%d" %\
            (self.n_sent_1, self.n_sent_2, self.n_rcvd, self.n_accepted)
        self.sender_1_conn.close()
        self.sender_2_conn.close()
        self.receiver_conn.close()

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))

        self.sender_1_conn = event.container.connect(self.router_1_host)
        self.sender_2_conn = event.container.connect(self.router_2_host)
        self.receiver_conn = event.container.connect(self.router_1_host)

        self.receiver = event.container.create_receiver(self.receiver_conn, dynamic=True)
        self.sender_1 = event.container.create_sender(self.sender_1_conn)
        self.sender_2 = event.container.create_sender(self.sender_2_conn)

    def send(self):
        if self.address is None:
            return

        while self.sender_1.credit > 0 and self.n_sent_1 < self.count:
            self.n_sent_1 += 1
            m = Message(address=self.address, body="Message %d of %d (via 1)" % (self.n_sent_1, self.count))
            self.sender_1.send(m)

        while self.sender_2.credit > 0 and self.n_sent_2 < self.count:
            self.n_sent_2 += 1
            m = Message(address=self.address, body="Message %d of %d (via 2)" % (self.n_sent_2, self.count))
            self.sender_2.send(m)

    def on_link_opened(self, event):
        if event.receiver == self.receiver:
            self.address = self.receiver.remote_source.address
            self.send()

    def on_sendable(self, event):
        self.send()

    def on_message(self, event):
        if event.receiver == self.receiver:
            self.n_rcvd += 1

    def on_accepted(self, event):
        self.n_accepted += 1
        if self.n_accepted == self.count * 2:
            self.sender_1_conn.close()
            self.sender_2_conn.close()
            self.receiver_conn.close()
            self.timer.cancel()

    def run(self):
        Container(self).run()


class DynamicTarget(LinkOption):

    def apply(self, link):
        link.target.dynamic = True
        link.target.address = None


class DynamicTargetTest(MessagingHandler):

    def __init__(self, sender_host, receiver_host):
        super(DynamicTargetTest, self).__init__()
        self.sender_host   = sender_host
        self.receiver_host = receiver_host

        self.error         = None
        self.skip          = None
        self.receiver_conn = None
        self.sender_conn   = None
        self.sender        = None
        self.receiver      = None
        self.address       = None

        self.count      = 10
        self.n_sent     = 0
        self.n_rcvd     = 0
        self.n_accepted = 0

    def timeout(self):
        self.error = "Timeout Expired: n_sent=%d n_rcvd=%d n_accepted=%d" %\
            (self.n_sent, self.n_rcvd, self.n_accepted)
        self.sender_conn.close()
        self.receiver_conn.close()

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))

        self.sender_conn   = event.container.connect(self.sender_host)
        self.receiver_conn = event.container.connect(self.receiver_host)

        self.sender = event.container.create_sender(self.sender_conn, options=DynamicTarget())

    def send(self):
        while self.sender.credit > 0 and self.n_sent < self.count:
            self.n_sent += 1
            m = Message(address=self.address, body="Message %d of %d" % (self.n_sent, self.count))
            self.sender.send(m)

    def on_link_opened(self, event):
        if event.sender == self.sender:
            self.address = self.sender.remote_target.address
            self.receiver = event.container.create_receiver(self.receiver_conn, self.address)

    def on_sendable(self, event):
        self.send()

    def on_message(self, event):
        if event.receiver == self.receiver:
            self.n_rcvd += 1

    def on_accepted(self, event):
        self.n_accepted += 1
        if self.n_accepted == self.count:
            self.sender_conn.close()
            self.receiver_conn.close()
            self.timer.cancel()

    def run(self):
        Container(self).run()


if __name__ == '__main__':
    unittest.main(main_module())

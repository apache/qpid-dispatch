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

from system_test import TestCase, Qdrouterd, main_module, TIMEOUT, unittest, TestTimeout


class RouterTest(TestCase):

    inter_router_port = None

    @classmethod
    def setUpClass(cls):
        """Start a router"""
        super(RouterTest, cls).setUpClass()

        def router(name, connection, args=None):

            config = [
                ('router', {'mode': 'interior', 'id': name}),
                ('listener', {'port': cls.tester.get_port(), 'stripAnnotations': 'no'}),
                ('listener', {'port': cls.tester.get_port(), 'stripAnnotations': 'no', 'multiTenant': 'yes'}),
                ('listener', {'port': cls.tester.get_port(), 'stripAnnotations': 'no', 'role': 'route-container'}),
                ('address', {'prefix': 'closest', 'distribution': 'closest'}),
                ('address', {'prefix': 'spread', 'distribution': 'balanced'}),
                ('address', {'prefix': 'multicast', 'distribution': 'multicast'}),
                ('address', {'prefix': '0.0.0.0/queue', 'waypoint': 'yes'}),
                connection
            ]

            config = Qdrouterd.Config(config)

            cls.routers.append(cls.tester.qdrouterd(name, config, wait=True, cl_args=args or []))

        cls.routers = []
        inter_router_port = cls.tester.get_port()

        router('A', ('listener', {'role': 'inter-router', 'port': inter_router_port}), ["-T"])

    def test_01_denied_link(self):
        test = DenyLinkTest(self.routers[0].addresses[0], "org.apache.qpid.dispatch.router/test/deny")
        test.run()
        self.assertIsNone(test.error)

    def test_02_discard_deliveries(self):
        test = DiscardTest(self.routers[0].addresses[0], "org.apache.qpid.dispatch.router/test/discard")
        test.run()
        self.assertIsNone(test.error)

    def test_03_presettled_source(self):
        test = SourceTest(self.routers[0].addresses[0], "org.apache.qpid.dispatch.router/test/source_ps", 300, 300)
        test.run()
        self.assertIsNone(test.error)

    def test_04_unsettled_source(self):
        test = SourceTest(self.routers[0].addresses[0], "org.apache.qpid.dispatch.router/test/source", 300, 0)
        test.run()
        self.assertIsNone(test.error)

    def test_05_echo_attach_detach(self):
        test = EchoTest(self.routers[0].addresses[0], "org.apache.qpid.dispatch.router/test/echo")
        test.run()
        self.assertIsNone(test.error)


class DenyLinkTest(MessagingHandler):

    def __init__(self, host, address):
        super(DenyLinkTest, self).__init__(prefetch=0)
        self.host      = host
        self.address   = address

        self.conn     = None
        self.error    = None
        self.receiver = None
        self.sender   = None
        self.receiver_failed = False
        self.sender_failed   = False

    def timeout(self):
        self.error = "Timeout Expired: receiver_failed=%s sender_failed=%s" %\
                     ("yes" if self.receiver_failed else "no",
                      "yes" if self.sender_failed else "no")
        self.conn.close()

    def on_start(self, event):
        self.timer    = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.conn     = event.container.connect(self.host)
        self.receiver = event.container.create_receiver(self.conn, self.address)
        self.sender   = event.container.create_sender(self.conn, self.address)

    def on_link_error(self, event):
        if event.receiver == self.receiver:
            self.receiver_failed = True
        if event.sender == self.sender:
            self.sender_failed = True

        if self.receiver_failed and self.sender_failed:
            self.conn.close()
            self.timer.cancel()

    def run(self):
        Container(self).run()


class DiscardTest(MessagingHandler):

    def __init__(self, host, address):
        super(DiscardTest, self).__init__(prefetch=0)
        self.host      = host
        self.address   = address

        self.conn     = None
        self.error    = None
        self.sender   = None

        self.count    = 300
        self.sent     = 0
        self.rejected = 0

    def timeout(self):
        self.error = "Timeout Expired: n_sent=%d n_rejected=%d" % (self.sent, self.rejected)
        self.conn.close()

    def on_start(self, event):
        self.timer  = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.conn   = event.container.connect(self.host)
        self.sender = event.container.create_sender(self.conn, self.address)

    def on_sendable(self, event):
        while self.sender.credit > 0 and self.sent < self.count:
            msg = Message(body="Discard Test")
            self.sender.send(msg)
            self.sent += 1

    def on_rejected(self, event):
        self.rejected += 1
        self.conn.close()
        self.timer.cancel()

    def on_link_error(self, event):
        if event.receiver == self.receiver:
            self.receiver_failed = True
        if event.sender == self.sender:
            self.sender_failed = True

        if self.receiver_failed and self.sender_failed:
            self.conn.close()
            self.timer.cancel()

    def run(self):
        Container(self).run()


class SourceTest(MessagingHandler):

    def __init__(self, host, address, count, expected_ps):
        super(SourceTest, self).__init__(prefetch=0)
        self.host        = host
        self.address     = address
        self.expected_ps = expected_ps

        self.conn     = None
        self.error    = None
        self.receiver = None

        self.count          = count
        self.n_credit_given = 0
        self.n_rcvd         = 0
        self.n_rcvd_ps      = 0

    def timeout(self):
        self.error = "Timeout Expired: n_rcvd=%d" % (self.n_rcvd)
        self.conn.close()

    def on_start(self, event):
        self.timer    = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.conn     = event.container.connect(self.host)
        self.receiver = event.container.create_receiver(self.conn, self.address)
        self.receiver.flow(3)
        self.n_credit_given = 3

    def on_message(self, event):
        dlv = event.delivery
        if dlv.settled:
            self.n_rcvd_ps += 1
        self.n_rcvd += 1
        if self.n_rcvd == self.count:
            self.conn.close()
            self.timer.cancel()
            if self.n_rcvd_ps != self.expected_ps:
                self.error = "Received %d deliveries, %d were settled (expected %d)" %\
                             (self.n_rcvd, self.n_rcvd_ps, self.expected_ps)

        elif self.n_rcvd == self.n_credit_given:
            self.receiver.flow(5)
            self.n_credit_given += 5

    def run(self):
        Container(self).run()


class EchoTest(MessagingHandler):

    def __init__(self, host, address):
        super(EchoTest, self).__init__(prefetch=0)
        self.host      = host
        self.address   = address

        self.conn     = None
        self.error    = None
        self.action   = "Connecting to router"
        self.receiver = None
        self.sender   = None

    def timeout(self):
        self.error = "Timeout Expired while attempting action: %s" % self.action
        self.conn.close()

    def fail(self, error):
        self.error = error
        self.conn.close()
        self.timer.cancel()

    def on_start(self, event):
        self.timer    = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.conn     = event.container.connect(self.host)
        self.receiver = event.container.create_receiver(self.conn, self.address)

    def on_link_opening(self, event):
        if event.sender:
            self.action = "Attaching incoming echoed link"
            self.sender = event.sender
            if event.sender.remote_source.address == self.address:
                event.sender.source.address = self.address
                event.sender.open()
            else:
                self.fail("Incorrect address on incoming sender: got %s, expected %s" %
                          (event.sender.remote_source.address, self.address))

    def on_link_opened(self, event):
        if event.receiver == self.receiver:
            self.action = "Closing the echoed link"
            self.receiver.close()

    def on_link_closed(self, event):
        if event.receiver == self.receiver:
            self.conn.close()
            self.timer.cancel()

    def run(self):
        Container(self).run()


if __name__ == '__main__':
    unittest.main(main_module())

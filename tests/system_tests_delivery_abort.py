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

from __future__ import unicode_literals
from __future__ import division
from __future__ import absolute_import
from __future__ import print_function

import unittest2 as unittest
from proton import Message, Timeout
from system_test import TestCase, Qdrouterd, main_module
from proton.handlers import MessagingHandler
from proton.reactor import Container
from qpid_dispatch_internal.compat import BINARY


class RouterTest(TestCase):

    inter_router_port = None

    @classmethod
    def setUpClass(cls):
        """Start a router"""
        super(RouterTest, cls).setUpClass()

        def router(name, connection):

            config = [
                ('router', {'mode': 'interior', 'id': name, 'allowUnsettledMulticast': 'yes'}),
                ('listener', {'port': cls.tester.get_port(), 'stripAnnotations': 'no'}),
                ('listener', {'port': cls.tester.get_port(), 'stripAnnotations': 'no', 'role': 'route-container'}),
                ('linkRoute', {'prefix': 'link', 'direction': 'in', 'containerId': 'LRC'}),
                ('linkRoute', {'prefix': 'link', 'direction': 'out', 'containerId': 'LRC'}),
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
        router('B', ('connector', {'name': 'connectorToA', 'role': 'inter-router', 'port': inter_router_port, 'verifyHostname': 'no'}))

        cls.routers[0].wait_router_connected('B')
        cls.routers[1].wait_router_connected('A')


    def test_01_message_route_truncated_one_router(self):
        test = MessageRouteTruncateTest(self.routers[0].addresses[0],
                                        self.routers[0].addresses[0],
                                        "addr_01")
        test.run()
        self.assertEqual(None, test.error)


    def test_02_message_route_truncated_two_routers(self):
        test = MessageRouteTruncateTest(self.routers[0].addresses[0],
                                        self.routers[1].addresses[0],
                                        "addr_02")
        test.run()
        self.assertEqual(None, test.error)


    def test_03_link_route_truncated_one_router(self):
        test = LinkRouteTruncateTest(self.routers[0].addresses[0],
                                     self.routers[0].addresses[1],
                                     "link.addr_03",
                                     self.routers[0].addresses[0])
        test.run()
        self.assertEqual(None, test.error)


    def test_04_link_route_truncated_two_routers(self):
        test = LinkRouteTruncateTest(self.routers[1].addresses[0],
                                     self.routers[0].addresses[1],
                                     "link.addr_04",
                                     self.routers[1].addresses[0])
        test.run()
        self.assertEqual(None, test.error)


    def test_05_message_route_abort_one_router(self):
        test = MessageRouteAbortTest(self.routers[0].addresses[0],
                                     self.routers[0].addresses[0],
                                     "addr_05")
        test.run()
        self.assertEqual(None, test.error)


    def test_06_message_route_abort_two_routers(self):
        test = MessageRouteAbortTest(self.routers[0].addresses[0],
                                     self.routers[1].addresses[0],
                                     "addr_06")
        test.run()
        self.assertEqual(None, test.error)


    def test_07_multicast_truncate_one_router(self):
        test = MulticastTruncateTest(self.routers[0].addresses[0],
                                     self.routers[0].addresses[0],
                                     self.routers[0].addresses[0],
                                     "multicast.addr_07")
        test.run()
        self.assertEqual(None, test.error)


class Entity(object):
    def __init__(self, status_code, status_description, attrs):
        self.status_code        = status_code
        self.status_description = status_description
        self.attrs              = attrs

    def __getattr__(self, key):
        return self.attrs[key]


class RouterProxy(object):
    def __init__(self, reply_addr):
        self.reply_addr = reply_addr

    def response(self, msg):
        ap = msg.properties
        return Entity(ap['statusCode'], ap['statusDescription'], msg.body)

    def read_address(self, name):
        ap = {'operation': 'READ', 'type': 'org.apache.qpid.dispatch.router.address', 'name': name}
        return Message(properties=ap, reply_to=self.reply_addr)

    def query_addresses(self):
        ap = {'operation': 'QUERY', 'type': 'org.apache.qpid.dispatch.router.address'}
        return Message(properties=ap, reply_to=self.reply_addr)


class Timeout(object):
    def __init__(self, parent):
        self.parent = parent

    def on_timer_task(self, event):
        self.parent.timeout()


class PollTimeout(object):
    def __init__(self, parent):
        self.parent = parent

    def on_timer_task(self, event):
        self.parent.poll_timeout()


class MessageRouteTruncateTest(MessagingHandler):
    def __init__(self, sender_host, receiver_host, address):
        super(MessageRouteTruncateTest, self).__init__()
        self.sender_host      = sender_host
        self.receiver_host    = receiver_host
        self.address          = address

        self.sender_conn   = None
        self.receiver_conn = None
        self.error         = None
        self.sender1       = None
        self.sender2       = None
        self.sender3       = None
        self.receiver      = None
        self.streaming     = False
        self.delivery      = None
        self.data          = "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
        self.long_data     = ""

        self.sent_stream   = 0
        self.program       = ['Send_Short_1', 'Send_Long_Truncated', 'Send_Short_2', 'Send_Short_3']
        self.result        = []
        self.expected_result = ['Send_Short_1', 'Aborted_Delivery', '2', '2', '2', '2', '2',
                                '2', '2', '2', '2', '2', 'Send_Short_2', '3', '3', '3', '3',
                                '3', '3', '3', '3', '3', '3', 'Send_Short_3']

    def timeout(self):
        self.error = "Timeout Expired - Unprocessed Ops: %r, Result: %r" % (self.program, self.result)
        self.sender_conn.close()
        self.receiver_conn.close()

    def on_start(self, event):
        self.timer         = event.reactor.schedule(10.0, Timeout(self))
        self.sender_conn   = event.container.connect(self.sender_host)
        self.receiver_conn = event.container.connect(self.receiver_host)
        self.sender1       = event.container.create_sender(self.sender_conn, self.address, name="S1")
        self.sender2       = event.container.create_sender(self.sender_conn, self.address, name="S2")
        self.sender3       = event.container.create_sender(self.sender_conn, self.address, name="S3")
        self.receiver      = event.container.create_receiver(self.receiver_conn, self.address)

    def stream(self):
        self.sender1.stream(BINARY(self.long_data))
        self.sent_stream += len(self.long_data)
        if self.sent_stream >= 1000000:
            self.streaming = False
            self.sender1.close()
            self.send()

    def send(self):
        next_op = self.program.pop(0) if len(self.program) > 0 else None
        if next_op == 'Send_Short_1':
            m = Message(body="%s" % next_op)
            self.sender1.send(m)
        elif next_op == 'Send_Long_Truncated':
            for i in range(100):
                self.long_data += self.data
            self.delivery  = self.sender1.delivery(self.sender1.delivery_tag())
            self.streaming = True
            self.stream()
        elif next_op == 'Send_Short_2':
            m = Message(body="2")
            for i in range(10):
                self.sender2.send(m)
            m = Message(body="Send_Short_2")
            self.sender2.send(m)
            self.sender2.close()
        elif next_op == 'Send_Short_3':
            m = Message(body="3")
            for i in range(10):
                self.sender3.send(m)
            m = Message(body="%s" % next_op)
            self.sender3.send(m)
            self.sender_conn.close()

    def on_sendable(self, event):
        if event.sender == self.sender1 and self.program[0] == 'Send_Short_1':
            self.send()
        if self.streaming:
            self.stream()

    def on_message(self, event):
        m = event.message
        self.result.append(m.body)
        if m.body == 'Send_Short_1':
            self.send()
        elif m.body == 'Send_Short_2':
            self.send()
        elif m.body == 'Send_Short_3':
            if self.result != self.expected_result:
                self.error = "Expected: %r, Actual: %r" % (self.expected_result, self.result)
            self.receiver_conn.close()
            self.timer.cancel()

    def on_aborted(self, event):
        self.result.append('Aborted_Delivery')
        self.send()

    def run(self):
        Container(self).run()


class LinkRouteTruncateTest(MessagingHandler):
    def __init__(self, sender_host, receiver_host, address, query_host):
        super(LinkRouteTruncateTest, self).__init__()
        self.sender_host      = sender_host
        self.receiver_host    = receiver_host
        self.address          = address
        self.query_host       = query_host

        self.sender_conn   = None
        self.receiver_conn = None
        self.query_conn    = None
        self.error         = None
        self.sender1       = None
        self.receiver      = None
        self.poll_timer    = None
        self.streaming     = False
        self.delivery      = None
        self.data          = "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
        self.long_data     = ""

        self.sent_stream   = 0
        self.program       = ['Send_Short_1', 'Send_Long_Truncated']
        self.result        = []
        self.expected_result = ['Send_Short_1', 'Aborted_Delivery']

    def timeout(self):
        self.error = "Timeout Expired - Unprocessed Ops: %r, Result: %r" % (self.program, self.result)
        self.sender_conn.close()
        self.receiver_conn.close()
        self.query_conn.close()
        if self.poll_timer:
            self.poll_timer.cancel()

    def on_start(self, event):
        self.timer          = event.reactor.schedule(10.0, Timeout(self))
        self.sender_conn    = event.container.connect(self.sender_host)
        self.receiver_conn  = event.container.connect(self.receiver_host)
        self.query_conn     = event.container.connect(self.query_host)
        self.reply_receiver = event.container.create_receiver(self.query_conn, dynamic=True)
        self.agent_sender   = event.container.create_sender(self.query_conn, "$management")

    def setup_first_links(self, event):
        self.sender1 = event.container.create_sender(self.sender_conn, self.address, name="S1")

    def stream(self):
        self.sender1.stream(BINARY(self.long_data))
        self.sent_stream += len(self.long_data)
        if self.sent_stream >= 1000000:
            self.streaming = False
            self.sender1.close()

    def send(self):
        next_op = self.program.pop(0) if len(self.program) > 0 else None
        if next_op == 'Send_Short_1':
            m = Message(body="%s" % next_op)
            self.sender1.send(m)
        elif next_op == 'Send_Long_Truncated':
            for i in range(100):
                self.long_data += self.data
            self.delivery  = self.sender1.delivery(self.sender1.delivery_tag())
            self.streaming = True
            self.stream()

    def poll_timeout(self):
        self.poll()

    def poll(self):
        request = self.proxy.read_address('Clink')
        self.agent_sender.send(request)

    def on_sendable(self, event):
        if event.sender == self.sender1 and len(self.program) > 0 and self.program[0] == 'Send_Short_1':
            self.send()
        if event.sender == self.sender1 and self.streaming:
            self.stream()

    def on_link_opening(self, event):
        if event.receiver:
            self.receiver = event.receiver
            event.receiver.target.address = self.address
            event.receiver.open()

    def on_link_opened(self, event):
        if event.receiver == self.reply_receiver:
            self.proxy = RouterProxy(self.reply_receiver.remote_source.address)
            self.poll()

    def on_message(self, event):
        if event.receiver == self.reply_receiver:
            response = self.proxy.response(event.message)
            if response.status_code == 200 and (response.remoteCount + response.containerCount) > 0:
                if self.poll_timer:
                    self.poll_timer.cancel()
                    self.poll_timer = None
                self.setup_first_links(event)
            else:
                self.poll_timer = event.reactor.schedule(0.25, PollTimeout(self))
            return

        m = event.message
        self.result.append(m.body)
        if m.body == 'Send_Short_1':
            self.send()

    def on_aborted(self, event):
        self.result.append('Aborted_Delivery')
        if self.result != self.expected_result:
            self.error = "Expected: %r, Actual: %r" % (self.expected_result, self.result)
        self.sender_conn.close()
        self.receiver_conn.close()
        self.query_conn.close()
        self.timer.cancel()

    def run(self):
        container = Container(self)
        container.container_id="LRC"
        container.run()


class MessageRouteAbortTest(MessagingHandler):
    def __init__(self, sender_host, receiver_host, address):
        super(MessageRouteAbortTest, self).__init__()
        self.sender_host      = sender_host
        self.receiver_host    = receiver_host
        self.address          = address

        self.sender_conn   = None
        self.receiver_conn = None
        self.error         = None
        self.sender1       = None
        self.receiver      = None
        self.delivery      = None

        self.program       = [('D', 10), ('D', 10), ('A', 10), ('A', 10), ('D', 10), ('D', 10),
                              ('A', 100), ('D', 100),
                              ('A', 1000), ('A', 1000), ('A', 1000), ('A', 1000), ('A', 1000), ('D', 1000),
                              ('A', 10000), ('A', 10000), ('A', 10000), ('A', 10000), ('A', 10000), ('D', 10000),
                              ('A', 100000), ('A', 100000), ('A', 100000), ('A', 100000), ('A', 100000), ('D', 100000), ('F', 10)]
        self.result        = []
        self.expected_result = [10, 10, 10, 10, 100, 1000, 10000, 100000]

    def timeout(self):
        self.error = "Timeout Expired - Unprocessed Ops: %r, Result: %r" % (self.program, self.result)
        self.sender_conn.close()
        self.receiver_conn.close()

    def on_start(self, event):
        self.timer         = event.reactor.schedule(10.0, Timeout(self))
        self.sender_conn   = event.container.connect(self.sender_host)
        self.receiver_conn = event.container.connect(self.receiver_host)
        self.sender1       = event.container.create_sender(self.sender_conn, self.address, name="S1")
        self.receiver      = event.container.create_receiver(self.receiver_conn, self.address)

    def send(self):
        op, size = self.program.pop(0) if len(self.program) > 0 else (None, None)

        if op == None:
            return

        body = ""
        if op == 'F':
            body = "FINISH"
        else:
            for i in range(size // 10):
                body += "0123456789"
        msg = Message(body=body)
        
        if op in 'DF':
            delivery = self.sender1.send(msg)

        if op == 'A':
            self.delivery = self.sender1.delivery(self.sender1.delivery_tag())
            encoded = msg.encode()
            self.sender1.stream(encoded)

    def finish(self):
        if self.result != self.expected_result:
            self.error = "Expected: %r, Actual: %r" % (self.expected_result, self.result)
        self.sender_conn.close()
        self.receiver_conn.close()
        self.timer.cancel()
        
    def on_sendable(self, event):
        if event.sender == self.sender1:
            if self.delivery:
                self.delivery.abort()
                self.delivery = None
            else:
                self.send()

    def on_message(self, event):
        m = event.message
        if m.body == "FINISH":
            self.finish()
        else:
            self.result.append(len(m.body))
            self.send()

    def run(self):
        Container(self).run()


class MulticastTruncateTest(MessagingHandler):
    def __init__(self, sender_host, receiver_host1, receiver_host2, address):
        super(MulticastTruncateTest, self).__init__()
        self.sender_host      = sender_host
        self.receiver_host1   = receiver_host1
        self.receiver_host2   = receiver_host2
        self.address          = address

        self.sender_conn    = None
        self.receiver1_conn = None
        self.receiver2_conn = None
        self.error          = None
        self.sender1        = None
        self.sender2        = None
        self.sender3        = None
        self.receiver1      = None
        self.receiver2      = None
        self.streaming      = False
        self.delivery       = None
        self.data           = "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
        self.long_data      = ""

        self.completions     = 0
        self.sent_stream     = 0
        self.program         = ['Send_Short_1', 'Send_Long_Truncated', 'Send_Short_2', 'Send_Short_3']
        self.result1         = []
        self.result2         = []
        self.expected_result = ['Send_Short_1', 'Aborted_Delivery', '2', '2', '2', '2', '2',
                                '2', '2', '2', '2', '2', 'Send_Short_2', '3', '3', '3', '3',
                                '3', '3', '3', '3', '3', '3', 'Send_Short_3']

    def timeout(self):
        self.error = "Timeout Expired - Unprocessed Ops: %r, Result1: %r, Result2: %r" % (self.program, self.result1, self.result2)
        self.sender_conn.close()
        self.receiver1_conn.close()
        self.receiver2_conn.close()

    def on_start(self, event):
        self.timer          = event.reactor.schedule(10.0, Timeout(self))
        self.sender_conn    = event.container.connect(self.sender_host)
        self.receiver1_conn = event.container.connect(self.receiver_host1)
        self.receiver2_conn = event.container.connect(self.receiver_host2)
        self.sender1        = event.container.create_sender(self.sender_conn, self.address, name="S1")
        self.sender2        = event.container.create_sender(self.sender_conn, self.address, name="S2")
        self.sender3        = event.container.create_sender(self.sender_conn, self.address, name="S3")
        self.receiver1      = event.container.create_receiver(self.receiver1_conn, self.address)
        self.receiver2      = event.container.create_receiver(self.receiver2_conn, self.address)

    def stream(self):
        self.sender1.stream(BINARY(self.long_data))
        self.sent_stream += len(self.long_data)
        if self.sent_stream >= 1000000:
            self.streaming = False
            self.sender1.close()
            self.send()

    def send(self):
        if self.streaming:
            self.stream()
            return
        next_op = self.program.pop(0) if len(self.program) > 0 else None
        if next_op == 'Send_Short_1':
            m = Message(body="%s" % next_op)
            self.sender1.send(m)
        elif next_op == 'Send_Long_Truncated':
            for i in range(100):
                self.long_data += self.data
            self.delivery  = self.sender1.delivery(self.sender1.delivery_tag())
            self.streaming = True
            self.stream()
        elif next_op == 'Send_Short_2':
            m = Message(body="2")
            for i in range(10):
                self.sender2.send(m)
            m = Message(body="Send_Short_2")
            self.sender2.send(m)
            self.sender2.close()
        elif next_op == 'Send_Short_3':
            m = Message(body="3")
            for i in range(10):
                self.sender3.send(m)
            m = Message(body="%s" % next_op)
            self.sender3.send(m)
            self.sender_conn.close()

    def on_sendable(self, event):
        self.send()

    def on_message(self, event):
        m = event.message
        if event.receiver == self.receiver1:
            self.result1.append(m.body)
        elif event.receiver == self.receiver2:
            self.result2.append(m.body)
        if m.body == 'Send_Short_1':
            self.send()
        elif m.body == 'Send_Short_2':
            self.send()
        elif m.body == 'Send_Short_3':
            self.completions += 1
            if self.completions == 2:
                if self.result1 != self.expected_result or self.result2 != self.expected_result:
                    self.error = "Expected: %r, Actuals: %r, %r" % (self.expected_result, self.result1, self.result2)
                self.receiver1_conn.close()
                self.receiver2_conn.close()
                self.timer.cancel()

    def on_aborted(self, event):
        if event.receiver == self.receiver1:
            self.result1.append('Aborted_Delivery')
        elif event.receiver == self.receiver2:
            self.result2.append('Aborted_Delivery')
        self.send()

    def run(self):
        Container(self).run()


if __name__ == '__main__':
    unittest.main(main_module())

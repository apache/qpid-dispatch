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

from system_test import TestCase, Qdrouterd, main_module, TIMEOUT, TestTimeout, PollTimeout, unittest


class AddrTimer:
    def __init__(self, parent):
        self.parent = parent

    def on_timer_task(self, event):
        self.parent.check_address()


class RouterTest(TestCase):

    inter_router_port = None

    @classmethod
    def setUpClass(cls):
        """Start a router"""
        super(RouterTest, cls).setUpClass()

        def router(name, mode, connection, extra=None):
            config = [
                ('router', {'mode': mode, 'id': name}),
                ('listener', {'port': cls.tester.get_port(), 'stripAnnotations': 'no'}),
                ('listener', {'port': cls.tester.get_port(), 'role': 'route-container', 'stripAnnotations': 'no'}),
                ('linkRoute', {'prefix': 'queue', 'containerId': 'LRC_S', 'direction': 'out'}),
                ('linkRoute', {'prefix': 'queue', 'containerId': 'LRC_R', 'direction': 'in'}),
                ('address', {'prefix': 'closest', 'distribution': 'closest'}),
                ('address', {'prefix': 'spread', 'distribution': 'balanced'}),
                ('address', {'prefix': 'multicast', 'distribution': 'multicast'}),
                connection
            ]

            if extra:
                config.append(extra)
            config = Qdrouterd.Config(config)
            cls.routers.append(cls.tester.qdrouterd(name, config, wait=True))

        cls.routers = []

        inter_router_port = cls.tester.get_port()
        edge_port_A       = cls.tester.get_port()
        edge_port_B       = cls.tester.get_port()

        router('INT.A', 'interior',
               ('listener', {'role': 'inter-router', 'port': inter_router_port}),
               ('listener', {'role': 'edge', 'port': edge_port_A}))
        router('INT.B', 'interior',
               ('connector', {'name': 'connectorToA', 'role': 'inter-router', 'port': inter_router_port}),
               ('listener',  {'role': 'edge', 'port': edge_port_B}))
        router('EA1',   'edge',     ('connector', {'name': 'edge', 'role': 'edge', 'port': edge_port_A}))
        router('EA2',   'edge',     ('connector', {'name': 'edge', 'role': 'edge', 'port': edge_port_A}))
        router('EB1',   'edge',     ('connector', {'name': 'edge', 'role': 'edge', 'port': edge_port_B}))
        router('EB2',   'edge',     ('connector', {'name': 'edge', 'role': 'edge', 'port': edge_port_B}))

        cls.routers[0].wait_router_connected('INT.B')
        cls.routers[1].wait_router_connected('INT.A')

    def test_01_dest_sender_same_edge(self):
        test = LRDestSenderFlowTest(self.routers[2].addresses[0],
                                    self.routers[2].addresses[1],
                                    self.routers[2].addresses[0],
                                    'queue.01', 0)
        test.run()
        self.assertIsNone(test.error)

    def test_02_dest_sender_same_interior(self):
        test = LRDestSenderFlowTest(self.routers[0].addresses[0],
                                    self.routers[0].addresses[1],
                                    self.routers[0].addresses[0],
                                    'queue.02', 0)
        test.run()
        self.assertIsNone(test.error)

    def test_03_dest_sender_edge_edge(self):
        test = LRDestSenderFlowTest(self.routers[2].addresses[0],
                                    self.routers[3].addresses[1],
                                    self.routers[0].addresses[0],
                                    'queue.03', 0)
        test.run()
        self.assertIsNone(test.error)

    def test_04_dest_sender_interior_interior(self):
        test = LRDestSenderFlowTest(self.routers[0].addresses[0],
                                    self.routers[1].addresses[1],
                                    self.routers[0].addresses[0],
                                    'queue.04', 0)
        test.run()
        self.assertIsNone(test.error)

    def test_05_dest_sender_edge_interior(self):
        test = LRDestSenderFlowTest(self.routers[2].addresses[0],
                                    self.routers[0].addresses[1],
                                    self.routers[0].addresses[0],
                                    'queue.05', 0)
        test.run()
        self.assertIsNone(test.error)

    def test_06_dest_sender_interior_edge(self):
        test = LRDestSenderFlowTest(self.routers[0].addresses[0],
                                    self.routers[2].addresses[1],
                                    self.routers[0].addresses[0],
                                    'queue.06', 0)
        test.run()
        self.assertIsNone(test.error)

    def test_07_dest_sender_edge_interior_interior_edge(self):
        test = LRDestSenderFlowTest(self.routers[2].addresses[0],
                                    self.routers[4].addresses[1],
                                    self.routers[0].addresses[0],
                                    'queue.07', 0)
        test.run()
        self.assertIsNone(test.error)

    def test_08_dest_sender_initial_credit_same_edge(self):
        test = LRDestSenderFlowTest(self.routers[2].addresses[0],
                                    self.routers[2].addresses[1],
                                    self.routers[2].addresses[0],
                                    'queue.08', 5)
        test.run()
        self.assertIsNone(test.error)

    def test_09_dest_sender_initial_credit_same_interior(self):
        test = LRDestSenderFlowTest(self.routers[0].addresses[0],
                                    self.routers[0].addresses[1],
                                    self.routers[0].addresses[0],
                                    'queue.09', 5)
        test.run()
        self.assertIsNone(test.error)

    def test_10_dest_sender_initial_credit_edge_edge(self):
        test = LRDestSenderFlowTest(self.routers[2].addresses[0],
                                    self.routers[3].addresses[1],
                                    self.routers[0].addresses[0],
                                    'queue.10', 5)
        test.run()
        self.assertIsNone(test.error)

    def test_11_dest_sender_initial_credit_interior_interior(self):
        test = LRDestSenderFlowTest(self.routers[0].addresses[0],
                                    self.routers[1].addresses[1],
                                    self.routers[0].addresses[0],
                                    'queue.11', 5)
        test.run()
        self.assertIsNone(test.error)

    def test_12_dest_sender_initial_credit_edge_interior(self):
        test = LRDestSenderFlowTest(self.routers[2].addresses[0],
                                    self.routers[0].addresses[1],
                                    self.routers[0].addresses[0],
                                    'queue.12', 5)
        test.run()
        self.assertIsNone(test.error)

    def test_13_dest_sender_initial_credit_interior_edge(self):
        test = LRDestSenderFlowTest(self.routers[0].addresses[0],
                                    self.routers[2].addresses[1],
                                    self.routers[0].addresses[0],
                                    'queue.13', 5)
        test.run()
        self.assertIsNone(test.error)

    def test_14_dest_sender_initial_credit_edge_interior_interior_edge(self):
        test = LRDestSenderFlowTest(self.routers[2].addresses[0],
                                    self.routers[4].addresses[1],
                                    self.routers[0].addresses[0],
                                    'queue.14', 5)
        test.run()
        self.assertIsNone(test.error)

    def test_15_dest_receiver_same_edge(self):
        test = LRDestReceiverFlowTest(self.routers[2].addresses[1],
                                      self.routers[2].addresses[0],
                                      self.routers[2].addresses[0],
                                      'queue.15', 0)
        test.run()
        self.assertIsNone(test.error)

    def test_16_dest_receiver_same_interior(self):
        test = LRDestReceiverFlowTest(self.routers[0].addresses[1],
                                      self.routers[0].addresses[0],
                                      self.routers[0].addresses[0],
                                      'queue.16', 0)
        test.run()
        self.assertIsNone(test.error)

    def test_17_dest_receiver_edge_edge(self):
        test = LRDestReceiverFlowTest(self.routers[2].addresses[1],
                                      self.routers[3].addresses[0],
                                      self.routers[0].addresses[0],
                                      'queue.17', 0)
        test.run()
        self.assertIsNone(test.error)

    def test_18_dest_receiver_interior_interior(self):
        test = LRDestReceiverFlowTest(self.routers[0].addresses[1],
                                      self.routers[1].addresses[0],
                                      self.routers[0].addresses[0],
                                      'queue.18', 0)
        test.run()
        self.assertIsNone(test.error)

    def test_19_dest_receiver_edge_interior(self):
        test = LRDestReceiverFlowTest(self.routers[2].addresses[1],
                                      self.routers[0].addresses[0],
                                      self.routers[0].addresses[0],
                                      'queue.19', 0)
        test.run()
        self.assertIsNone(test.error)

    def test_20_dest_receiver_interior_edge(self):
        test = LRDestReceiverFlowTest(self.routers[0].addresses[1],
                                      self.routers[2].addresses[0],
                                      self.routers[0].addresses[0],
                                      'queue.20', 0)
        test.run()
        self.assertIsNone(test.error)

    def test_21_dest_receiver_edge_interior_interior_edge(self):
        test = LRDestReceiverFlowTest(self.routers[2].addresses[1],
                                      self.routers[4].addresses[0],
                                      self.routers[1].addresses[0],
                                      'queue.21', 0)
        test.run()
        self.assertIsNone(test.error)

    def test_22_dest_receiver_initial_credit_same_edge(self):
        test = LRDestReceiverFlowTest(self.routers[2].addresses[1],
                                      self.routers[2].addresses[0],
                                      self.routers[2].addresses[0],
                                      'queue.22', 5)
        test.run()
        self.assertIsNone(test.error)

    def test_23_dest_receiver_initial_credit_same_interior(self):
        test = LRDestReceiverFlowTest(self.routers[0].addresses[1],
                                      self.routers[0].addresses[0],
                                      self.routers[0].addresses[0],
                                      'queue.23', 5)
        test.run()
        self.assertIsNone(test.error)

    def test_24_dest_receiver_initial_credit_edge_edge(self):
        test = LRDestReceiverFlowTest(self.routers[2].addresses[1],
                                      self.routers[3].addresses[0],
                                      self.routers[0].addresses[0],
                                      'queue.24', 5)
        test.run()
        self.assertIsNone(test.error)

    def test_25_dest_receiver_initial_credit_interior_interior(self):
        test = LRDestReceiverFlowTest(self.routers[0].addresses[1],
                                      self.routers[1].addresses[0],
                                      self.routers[0].addresses[0],
                                      'queue.25', 5)
        test.run()
        self.assertIsNone(test.error)

    def test_26_dest_receiver_initial_credit_edge_interior(self):
        test = LRDestReceiverFlowTest(self.routers[2].addresses[1],
                                      self.routers[0].addresses[0],
                                      self.routers[0].addresses[0],
                                      'queue.26', 5)
        test.run()
        self.assertIsNone(test.error)

    def test_27_dest_receiver_initial_credit_interior_edge(self):
        test = LRDestReceiverFlowTest(self.routers[0].addresses[1],
                                      self.routers[2].addresses[0],
                                      self.routers[0].addresses[0],
                                      'queue.27', 5)
        test.run()
        self.assertIsNone(test.error)

    def test_28_dest_receiver_initial_credit_edge_interior_interior_edge(self):
        test = LRDestReceiverFlowTest(self.routers[2].addresses[1],
                                      self.routers[4].addresses[0],
                                      self.routers[1].addresses[0],
                                      'queue.28', 5)
        test.run()
        self.assertIsNone(test.error)

    def test_29_fast_teardown_test(self):
        test = LRFastTeardownTest(self.routers[2].addresses[0], "normal.29")
        test.run()
        self.assertIsNone(test.error)


class Entity:
    def __init__(self, status_code, status_description, attrs):
        self.status_code        = status_code
        self.status_description = status_description
        self.attrs              = attrs

    def __getattr__(self, key):
        return self.attrs[key]


class RouterProxy:
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


class LRDestSenderFlowTest(MessagingHandler):
    def __init__(self, receiver_host, sender_host, probe_host, address, initial_credit):
        super(LRDestSenderFlowTest, self).__init__(prefetch=0)
        self.receiver_host   = receiver_host
        self.sender_host     = sender_host
        self.probe_host      = probe_host
        self.address         = address
        self.initial_credit  = initial_credit
        self.delta_credit    = 7
        self.final_credit    = initial_credit + 2 * self.delta_credit
        self.expected_credit = initial_credit

        self.receiver_conn  = None
        self.sender_conn    = None
        self.probe_conn     = None
        self.probe_sender   = None
        self.probe_receiver = None
        self.probe_reply    = None
        self.receiver       = None
        self.sender         = None
        self.error          = None
        self.last_action    = "Test initialization"

    def fail(self, text):
        self.error = text
        self.receiver_conn.close()
        self.sender_conn.close()
        self.probe_conn.close()
        self.timer.cancel()

    def timeout(self):
        self.error = "Timeout Expired - last_action: %s" % (self.last_action)
        self.receiver_conn.close()
        self.sender_conn.close()
        self.probe_conn.close()

    def poll_timeout(self):
        self.probe()

    def on_start(self, event):
        self.reactor        = event.reactor
        self.timer          = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.receiver_conn  = event.container.connect(self.receiver_host)
        self.sender_conn    = event.container.connect(self.sender_host)
        self.probe_conn     = event.container.connect(self.probe_host)
        self.probe_receiver = event.container.create_receiver(self.probe_conn, dynamic=True)
        self.probe_receiver.flow(1000)
        self.last_action = "on_start"

    def probe(self):
        self.probe_sender.send(self.proxy.read_address('Dqueue'))

    def on_link_opened(self, event):
        if event.receiver == self.probe_receiver:
            self.probe_reply  = self.probe_receiver.remote_source.address
            self.proxy        = RouterProxy(self.probe_reply)
            self.probe_sender = event.container.create_sender(self.probe_conn, '$management')
        elif event.sender == self.probe_sender:
            self.probe()
            self.last_action = "probing"
        elif event.receiver == self.receiver:
            if self.initial_credit == 0:
                self.expected_credit += self.delta_credit
                self.receiver.flow(self.delta_credit)

    def on_link_opening(self, event):
        if event.sender:
            self.sender = event.sender
            if event.sender.remote_source.address == self.address:
                event.sender.source.address = self.address
                event.sender.open()
            else:
                self.fail("Incorrect address on incoming sender: got %s, expected %s" %
                          (event.sender.remote_source.address, self.address))

    def on_sendable(self, event):
        if event.sender == self.sender:
            if event.sender.credit == self.expected_credit:
                if self.expected_credit == self.final_credit:
                    self.fail(None)
                else:
                    self.expected_credit += self.delta_credit
                    self.receiver.flow(self.delta_credit)
            else:
                self.fail("Unexpected sender credit: got %d, expected %d" %
                          (event.sender.credit, self.expected_credit))

    def on_message(self, event):
        if event.receiver == self.probe_receiver:
            response = self.proxy.response(event.message)
            self.last_action = "Handling probe response: remote: %d container: %d" \
                               % (response.remoteCount, response.containerCount)
            if response.status_code == 200 and response.remoteCount + response.containerCount == 1:
                self.receiver = event.container.create_receiver(self.receiver_conn, self.address)
                if self.initial_credit > 0:
                    self.receiver.flow(self.initial_credit)
                    self.expected_credit = self.initial_credit
                self.last_action = "opening test receiver"
            else:
                self.poll_timer = self.reactor.schedule(0.5, PollTimeout(self))

    def run(self):
        container = Container(self)
        container.container_id = 'LRC_S'
        container.run()


class LRDestReceiverFlowTest(MessagingHandler):
    def __init__(self, receiver_host, sender_host, probe_host, address, initial_credit):
        super(LRDestReceiverFlowTest, self).__init__(prefetch=0)
        self.receiver_host   = receiver_host
        self.sender_host     = sender_host
        self.probe_host      = probe_host
        self.address         = address
        self.initial_credit  = initial_credit
        self.delta_credit    = 7
        self.final_credit    = initial_credit + 2 * self.delta_credit
        self.expected_credit = initial_credit

        self.receiver_conn  = None
        self.sender_conn    = None
        self.probe_conn     = None
        self.probe_sender   = None
        self.probe_receiver = None
        self.probe_reply    = None
        self.receiver       = None
        self.sender         = None
        self.error          = None
        self.last_action    = "Test initialization"

    def fail(self, text):
        self.error = text
        self.receiver_conn.close()
        self.sender_conn.close()
        self.probe_conn.close()
        self.timer.cancel()

    def timeout(self):
        self.error = "Timeout Expired - last_action: %s" % (self.last_action)
        self.receiver_conn.close()
        self.sender_conn.close()
        self.probe_conn.close()

    def poll_timeout(self):
        self.probe()

    def on_start(self, event):
        self.reactor        = event.reactor
        self.timer          = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.receiver_conn  = event.container.connect(self.receiver_host)
        self.sender_conn    = event.container.connect(self.sender_host)
        self.probe_conn     = event.container.connect(self.probe_host)
        self.probe_receiver = event.container.create_receiver(self.probe_conn, dynamic=True)
        self.probe_receiver.flow(1000)
        self.last_action = "on_start"

    def probe(self):
        self.probe_sender.send(self.proxy.read_address('Cqueue'))

    def on_link_opened(self, event):
        if event.receiver == self.probe_receiver:
            self.probe_reply  = self.probe_receiver.remote_source.address
            self.proxy        = RouterProxy(self.probe_reply)
            self.probe_sender = event.container.create_sender(self.probe_conn, '$management')
        elif event.sender == self.probe_sender:
            self.probe()
            self.last_action = "probing"
        elif event.sender == self.sender:
            if self.initial_credit == 0:
                self.expected_credit += self.delta_credit
                self.receiver.flow(self.delta_credit)

    def on_link_opening(self, event):
        if event.receiver:
            self.receiver = event.receiver
            if event.receiver.remote_target.address == self.address:
                event.receiver.target.address = self.address
                event.receiver.open()
                if self.initial_credit > 0:
                    self.receiver.flow(self.initial_credit)
                    self.expected_credit = self.initial_credit
            else:
                self.fail("Incorrect address on incoming receiver: got %s, expected %s" %
                          (event.receiver.remote_target.address, self.address))

    def on_sendable(self, event):
        if event.sender == self.sender:
            if event.sender.credit == self.expected_credit:
                if self.expected_credit == self.final_credit:
                    self.fail(None)
                else:
                    self.expected_credit += self.delta_credit
                    self.receiver.flow(self.delta_credit)
            else:
                self.fail("Unexpected sender credit: got %d, expected %d" %
                          (event.sender.credit, self.expected_credit))

    def on_message(self, event):
        if event.receiver == self.probe_receiver:
            response = self.proxy.response(event.message)
            self.last_action = "Handling probe response: remote: %d container: %d" \
                               % (response.remoteCount, response.containerCount)
            if response.status_code == 200 and response.remoteCount + response.containerCount == 1:
                self.sender = event.container.create_sender(self.receiver_conn, self.address)
                self.last_action = "opening test sender"
            else:
                self.poll_timer = self.reactor.schedule(0.5, PollTimeout(self))

    def run(self):
        container = Container(self)
        container.container_id = 'LRC_R'
        container.run()


class LRFastTeardownTest(MessagingHandler):
    def __init__(self, host, address):
        super(LRFastTeardownTest, self).__init__(prefetch=0)
        self.host    = host
        self.address = address

        self.conn        = None
        self.sender      = None
        self.error       = None
        self.last_action = "Test initialization"

    def fail(self, text):
        self.error = text
        self.conn.close()
        self.timer.cancel()

    def timeout(self):
        self.error = "Timeout Expired - last_action: %s" % (self.last_action)
        self.conn.close()

    def on_start(self, event):
        self.reactor     = event.reactor
        self.timer       = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.conn        = event.container.connect(self.host)
        self.last_action = "on_start"

    def on_connection_opened(self, event):
        self.sender = event.container.create_sender(self.conn, self.address)
        self.conn.close()
        self.timer.cancel()

    def run(self):
        container = Container(self)
        container.run()


if __name__ == '__main__':
    unittest.main(main_module())

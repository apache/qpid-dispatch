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

from proton import Message, symbol
from proton.handlers import MessagingHandler
from proton.reactor import Container

from system_test import TestCase, Qdrouterd, main_module, TIMEOUT, unittest, TestTimeout


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
                ('router', {'mode': mode, 'id': name, "helloMaxAgeSeconds": '10'}),
                ('listener', {'port': cls.tester.get_port(), 'stripAnnotations': 'no'}),
                ('address', {'prefix': 'queue', 'waypoint': 'yes'}),
                ('address', {'prefix': 'multi', 'ingressPhase': '0', 'egressPhase': '9'}),
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

        router('INT.A', 'interior', ('listener', {'role': 'inter-router', 'port': inter_router_port}),
               ('listener', {'role': 'edge', 'port': edge_port_A}))
        router('INT.B', 'interior', ('connector', {'name': 'connectorToA', 'role': 'inter-router', 'port': inter_router_port}),
               ('listener', {'role': 'edge', 'port': edge_port_B}))
        router('EA1',   'edge',     ('connector', {'name': 'edge', 'role': 'edge', 'port': edge_port_A}))
        router('EA2',   'edge',     ('connector', {'name': 'edge', 'role': 'edge', 'port': edge_port_A}))
        router('EB1',   'edge',     ('connector', {'name': 'edge', 'role': 'edge', 'port': edge_port_B}))
        router('EB2',   'edge',     ('connector', {'name': 'edge', 'role': 'edge', 'port': edge_port_B}))

        cls.routers[0].wait_router_connected('INT.B')
        cls.routers[1].wait_router_connected('INT.A')

    def test_01_waypoint_same_interior(self):
        test = WaypointTest(self.routers[0].addresses[0],
                            self.routers[0].addresses[0],
                            self.routers[0].addresses[0],
                            'queue.01')
        test.run()
        self.assertIsNone(test.error)

    def test_02_waypoint_same_edge(self):
        test = WaypointTest(self.routers[2].addresses[0],
                            self.routers[2].addresses[0],
                            self.routers[2].addresses[0],
                            'queue.02')
        test.run()
        self.assertIsNone(test.error)

    def test_03_waypoint_edge_interior(self):
        test = WaypointTest(self.routers[2].addresses[0],
                            self.routers[2].addresses[0],
                            self.routers[0].addresses[0],
                            'queue.03')
        test.run()
        self.assertIsNone(test.error)

    def test_04_waypoint_interior_edge(self):
        test = WaypointTest(self.routers[0].addresses[0],
                            self.routers[0].addresses[0],
                            self.routers[2].addresses[0],
                            'queue.04')
        test.run()
        self.assertIsNone(test.error)

    def test_05_waypoint_interior_interior(self):
        test = WaypointTest(self.routers[0].addresses[0],
                            self.routers[0].addresses[0],
                            self.routers[1].addresses[0],
                            'queue.05')
        test.run()
        self.assertIsNone(test.error)

    def test_06_waypoint_edge_edge(self):
        test = WaypointTest(self.routers[2].addresses[0],
                            self.routers[5].addresses[0],
                            self.routers[0].addresses[0],
                            'queue.06')
        test.run()
        self.assertIsNone(test.error)

    def test_07_waypoint_edge_endpoints_int_1(self):
        test = WaypointTest(self.routers[0].addresses[0],
                            self.routers[1].addresses[0],
                            self.routers[2].addresses[0],
                            'queue.07')
        test.run()
        self.assertIsNone(test.error)

    def test_08_waypoint_edge_endpoints_int_2(self):
        test = WaypointTest(self.routers[0].addresses[0],
                            self.routers[1].addresses[0],
                            self.routers[5].addresses[0],
                            'queue.08')
        test.run()
        self.assertIsNone(test.error)

    def test_09_waypoint_int_endpoints_edge_1(self):
        test = WaypointTest(self.routers[2].addresses[0],
                            self.routers[5].addresses[0],
                            self.routers[0].addresses[0],
                            'queue.09')
        test.run()
        self.assertIsNone(test.error)

    def test_10_waypoint_int_endpoints_edge_2(self):
        test = WaypointTest(self.routers[2].addresses[0],
                            self.routers[5].addresses[0],
                            self.routers[1].addresses[0],
                            'queue.10')
        test.run()
        self.assertIsNone(test.error)

    def test_11_waypoint_int_endpoints_int_1(self):
        test = WaypointTest(self.routers[0].addresses[0],
                            self.routers[1].addresses[0],
                            self.routers[0].addresses[0],
                            'queue.11')
        test.run()
        self.assertIsNone(test.error)

    def test_12_waypoint_int_endpoints_int_2(self):
        test = WaypointTest(self.routers[0].addresses[0],
                            self.routers[1].addresses[0],
                            self.routers[1].addresses[0],
                            'queue.12')
        test.run()
        self.assertIsNone(test.error)

    def test_13_waypoint_edge_endpoints_edge_1(self):
        test = WaypointTest(self.routers[2].addresses[0],
                            self.routers[5].addresses[0],
                            self.routers[3].addresses[0],
                            'queue.13')
        test.run()
        self.assertIsNone(test.error)

    def test_14_waypoint_edge_endpoints_edge_2(self):
        test = WaypointTest(self.routers[2].addresses[0],
                            self.routers[5].addresses[0],
                            self.routers[4].addresses[0],
                            'queue.14')
        test.run()
        self.assertIsNone(test.error)

    def test_15_multiphase_1(self):
        test = MultiPhaseTest(self.routers[2].addresses[0],
                              self.routers[5].addresses[0],
                              [
                                  self.routers[0].addresses[0],
                                  self.routers[1].addresses[0],
                                  self.routers[2].addresses[0],
                                  self.routers[3].addresses[0],
                                  self.routers[4].addresses[0],
                                  self.routers[5].addresses[0],
                                  self.routers[0].addresses[0],
                                  self.routers[1].addresses[0],
                                  self.routers[2].addresses[0]
        ],
            'multi.15')
        test.run()
        self.assertIsNone(test.error)

    def test_16_multiphase_2(self):
        test = MultiPhaseTest(self.routers[2].addresses[0],
                              self.routers[5].addresses[0],
                              [
                                  self.routers[5].addresses[0],
                                  self.routers[3].addresses[0],
                                  self.routers[1].addresses[0],
                                  self.routers[4].addresses[0],
                                  self.routers[2].addresses[0],
                                  self.routers[0].addresses[0],
                                  self.routers[5].addresses[0],
                                  self.routers[3].addresses[0],
                                  self.routers[1].addresses[0]
        ],
            'multi.16')
        test.run()
        self.assertIsNone(test.error)

    def test_17_multiphase_3(self):
        test = MultiPhaseTest(self.routers[1].addresses[0],
                              self.routers[0].addresses[0],
                              [
                                  self.routers[0].addresses[0],
                                  self.routers[1].addresses[0],
                                  self.routers[2].addresses[0],
                                  self.routers[3].addresses[0],
                                  self.routers[4].addresses[0],
                                  self.routers[5].addresses[0],
                                  self.routers[0].addresses[0],
                                  self.routers[1].addresses[0],
                                  self.routers[2].addresses[0]
        ],
            'multi.17')
        test.run()
        self.assertIsNone(test.error)


class WaypointTest(MessagingHandler):
    def __init__(self, sender_host, receiver_host, waypoint_host, addr):
        super(WaypointTest, self).__init__()
        self.sender_host   = sender_host
        self.receiver_host = receiver_host
        self.waypoint_host = waypoint_host
        self.addr          = addr
        self.count         = 300

        self.sender_conn   = None
        self.receiver_conn = None
        self.waypoint_conn = None
        self.error         = None
        self.n_tx          = 0
        self.n_rx          = 0
        self.n_thru        = 0

    def timeout(self):
        self.error = "Timeout Expired - n_tx=%d, n_rx=%d, n_thru=%d" % (self.n_tx, self.n_rx, self.n_thru)
        self.sender_conn.close()
        self.receiver_conn.close()
        self.waypoint_conn.close()

    def fail(self, error):
        self.error = error
        self.sender_conn.close()
        self.receiver_conn.close()
        self.waypoint_conn.close()
        self.timer.cancel()

    def on_start(self, event):
        self.timer          = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.sender_conn    = event.container.connect(self.sender_host)
        self.receiver_conn  = event.container.connect(self.receiver_host)
        self.waypoint_conn  = event.container.connect(self.waypoint_host)
        self.sender         = event.container.create_sender(self.sender_conn, self.addr)
        self.receiver       = event.container.create_receiver(self.receiver_conn, self.addr)
        self.wp_sender      = event.container.create_sender(self.waypoint_conn, self.addr)
        self.wp_receiver    = event.container.create_receiver(self.waypoint_conn, self.addr)
        self.wp_sender.target.capabilities.put_object(symbol("qd.waypoint"))
        self.wp_receiver.source.capabilities.put_object(symbol("qd.waypoint"))

    def on_sendable(self, event):
        if event.sender == self.sender:
            while self.sender.credit > 0 and self.n_tx < self.count:
                self.sender.send(Message("Message %d" % self.n_tx))
                self.n_tx += 1

    def on_message(self, event):
        if event.receiver == self.receiver:
            self.n_rx += 1
            if self.n_rx == self.count and self.n_thru == self.count:
                self.fail(None)
        elif event.receiver == self.wp_receiver:
            self.n_thru += 1
            self.wp_sender.send(Message(event.message.body))

    def run(self):
        Container(self).run()


class MultiPhaseTest(MessagingHandler):
    def __init__(self, sender_host, receiver_host, waypoint_hosts, addr):
        super(MultiPhaseTest, self).__init__()
        self.sender_host    = sender_host
        self.receiver_host  = receiver_host
        self.waypoint_hosts = waypoint_hosts
        self.addr           = addr
        self.count          = 300

        self.sender_conn    = None
        self.receiver_conn  = None
        self.waypoint_conns = []
        self.wp_senders     = []
        self.wp_receivers   = []
        self.error          = None
        self.n_tx           = 0
        self.n_rx           = 0
        self.n_thru         = [0, 0, 0, 0, 0, 0, 0, 0, 0]

    def timeout(self):
        self.error = "Timeout Expired - n_tx=%d, n_rx=%d, n_thru=%r" % (self.n_tx, self.n_rx, self.n_thru)
        self.sender_conn.close()
        self.receiver_conn.close()
        for c in self.waypoint_conns:
            c.close()

    def fail(self, error):
        self.error = error
        self.sender_conn.close()
        self.receiver_conn.close()
        for c in self.waypoint_conns:
            c.close()
        self.timer.cancel()

    def on_start(self, event):
        self.timer          = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.sender_conn    = event.container.connect(self.sender_host)
        self.receiver_conn  = event.container.connect(self.receiver_host)
        self.sender         = event.container.create_sender(self.sender_conn, self.addr)
        self.receiver       = event.container.create_receiver(self.receiver_conn, self.addr)
        for host in self.waypoint_hosts:
            self.waypoint_conns.append(event.container.connect(host))

        ordinal = 1
        for conn in self.waypoint_conns:
            sender   = event.container.create_sender(conn, self.addr)
            receiver = event.container.create_receiver(conn, self.addr)

            sender.target.capabilities.put_object(symbol("qd.waypoint.%d" % ordinal))
            receiver.source.capabilities.put_object(symbol("qd.waypoint.%d" % ordinal))

            self.wp_senders.append(sender)
            self.wp_receivers.append(receiver)
            ordinal += 1

    def on_sendable(self, event):
        if event.sender == self.sender:
            while self.sender.credit > 0 and self.n_tx < self.count:
                self.sender.send(Message("Message %d" % self.n_tx))
                self.n_tx += 1

    def on_message(self, event):
        if event.receiver == self.receiver:
            self.n_rx += 1
            if self.n_rx == self.count:
                self.fail(None)
        else:
            idx = 0
            for receiver in self.wp_receivers:
                if event.receiver == receiver:
                    self.n_thru[idx] += 1
                    self.wp_senders[idx].send(Message(event.message.body))
                    return
                idx += 1

    def run(self):
        Container(self).run()


if __name__ == '__main__':
    unittest.main(main_module())

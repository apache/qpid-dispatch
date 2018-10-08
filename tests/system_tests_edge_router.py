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
from proton import Message, Timeout
from system_test import TestCase, Qdrouterd, main_module, TIMEOUT, MgmtMsgProxy
from proton.handlers import MessagingHandler
from proton.reactor import Container, DynamicNodeProperties

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
                ('listener', {'port': cls.tester.get_port(), 'stripAnnotations': 'no', 'multiTenant': 'yes'}),
                ('listener', {'port': cls.tester.get_port(), 'stripAnnotations': 'no', 'role': 'route-container'}),
                ('linkRoute', {'prefix': '0.0.0.0/link', 'direction': 'in', 'containerId': 'LRC'}),
                ('linkRoute', {'prefix': '0.0.0.0/link', 'direction': 'out', 'containerId': 'LRC'}),
                ('autoLink', {'addr': '0.0.0.0/queue.waypoint', 'containerId': 'ALC', 'direction': 'in'}),
                ('autoLink', {'addr': '0.0.0.0/queue.waypoint', 'containerId': 'ALC', 'direction': 'out'}),
                ('address', {'prefix': 'closest', 'distribution': 'closest'}),
                ('address', {'prefix': 'spread', 'distribution': 'balanced'}),
                ('address', {'prefix': 'multicast', 'distribution': 'multicast'}),
                ('address', {'prefix': '0.0.0.0/queue', 'waypoint': 'yes'}),
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
               ('listener', {'role': 'edge-uplink', 'port': edge_port_A}))
        router('INT.B', 'interior', ('connector', {'name': 'connectorToA', 'role': 'inter-router', 'port': inter_router_port}),
               ('listener', {'role': 'edge-uplink', 'port': edge_port_B}))
        router('EA1',   'edge',     ('connector', {'name': 'uplink', 'role': 'edge-uplink', 'port': edge_port_A}))
        router('EA2',   'edge',     ('connector', {'name': 'uplink', 'role': 'edge-uplink', 'port': edge_port_A}))
        router('EB1',   'edge',     ('connector', {'name': 'uplink', 'role': 'edge-uplink', 'port': edge_port_B}))
        router('EB2',   'edge',     ('connector', {'name': 'uplink', 'role': 'edge-uplink', 'port': edge_port_B}))

        cls.routers[0].wait_router_connected('INT.B')
        cls.routers[1].wait_router_connected('INT.A')


    def test_01_connectivity_INTA_EA1(self):
        test = ConnectivityTest(self.routers[0].addresses[0],
                                self.routers[2].addresses[0],
                                'EA1')
        test.run()
        self.assertEqual(None, test.error)

    def test_02_connectivity_INTA_EA2(self):
        test = ConnectivityTest(self.routers[0].addresses[0],
                                self.routers[3].addresses[0],
                                'EA2')
        test.run()
        self.assertEqual(None, test.error)

    def test_03_connectivity_INTB_EB1(self):
        test = ConnectivityTest(self.routers[1].addresses[0],
                                self.routers[4].addresses[0],
                                'EB1')
        test.run()
        self.assertEqual(None, test.error)

    def test_04_connectivity_INTB_EB2(self):
        test = ConnectivityTest(self.routers[1].addresses[0],
                                self.routers[5].addresses[0],
                                'EB2')
        test.run()
        self.assertEqual(None, test.error)

    def test_05_dynamic_address_same_edge(self):
        test = DynamicAddressTest(self.routers[2].addresses[0],
                                  self.routers[2].addresses[0])
        test.run()
        self.assertEqual(None, test.error)

    def test_06_dynamic_address_interior_to_edge(self):
        test = DynamicAddressTest(self.routers[2].addresses[0],
                                  self.routers[0].addresses[0])
        test.run()
        self.assertEqual(None, test.error)

    def test_07_dynamic_address_edge_to_interior(self):
        test = DynamicAddressTest(self.routers[0].addresses[0],
                                  self.routers[2].addresses[0])
        test.run()
        self.assertEqual(None, test.error)

    def test_08_dynamic_address_edge_to_edge_one_interior(self):
        test = DynamicAddressTest(self.routers[2].addresses[0],
                                  self.routers[3].addresses[0])
        test.run()
        self.assertEqual(None, test.error)

    def test_09_dynamic_address_edge_to_edge_two_interior(self):
        test = DynamicAddressTest(self.routers[2].addresses[0],
                                  self.routers[4].addresses[0])
        test.run()
        self.assertEqual(None, test.error)

    def test_10_mobile_address_same_edge(self):
        test = MobileAddressTest(self.routers[2].addresses[0],
                                 self.routers[2].addresses[0],
                                 "test_10")
        test.run()
        self.assertEqual(None, test.error)

    def test_11_mobile_address_interior_to_edge(self):
        test = MobileAddressTest(self.routers[2].addresses[0],
                                 self.routers[0].addresses[0],
                                 "test_11")
        test.run()
        self.assertEqual(None, test.error)

    def test_12_mobile_address_edge_to_interior(self):
        self.skipTest("Temporarily disabled")
        test = MobileAddressTest(self.routers[0].addresses[0],
                                 self.routers[2].addresses[0],
                                 "test_12")
        test.run()
        self.assertEqual(None, test.error)

    def test_13_mobile_address_edge_to_edge_one_interior(self):
        self.skipTest("Temporarily disabled")
        test = MobileAddressTest(self.routers[2].addresses[0],
                                 self.routers[3].addresses[0],
                                 "test_13")
        test.run()
        self.assertEqual(None, test.error)

    def test_14_mobile_address_edge_to_edge_two_interior(self):
        self.skipTest("Temporarily disabled")
        test = MobileAddressTest(self.routers[2].addresses[0],
                                 self.routers[4].addresses[0],
                                 "test_14")
        test.run()
        self.assertEqual(None, test.error)


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


class ConnectivityTest(MessagingHandler):
    def __init__(self, interior_host, edge_host, edge_id):
        super(ConnectivityTest, self).__init__()
        self.interior_host = interior_host
        self.edge_host     = edge_host
        self.edge_id       = edge_id

        self.interior_conn = None
        self.edge_conn     = None
        self.error         = None
        self.proxy         = None
        self.query_sent    = False

    def timeout(self):
        self.error = "Timeout Expired"
        self.interior_conn.close()
        self.edge_conn.close()

    def on_start(self, event):
        self.timer          = event.reactor.schedule(10.0, Timeout(self))
        self.interior_conn  = event.container.connect(self.interior_host)
        self.edge_conn      = event.container.connect(self.edge_host)
        self.reply_receiver = event.container.create_receiver(self.interior_conn, dynamic=True)

    def on_link_opened(self, event):
        if event.receiver == self.reply_receiver:
            self.proxy        = MgmtMsgProxy(self.reply_receiver.remote_source.address)
            self.agent_sender = event.container.create_sender(self.interior_conn, "$management")

    def on_sendable(self, event):
        if not self.query_sent:
            self.query_sent = True
            self.agent_sender.send(self.proxy.query_connections())

    def on_message(self, event):
        if event.receiver == self.reply_receiver:
            response = self.proxy.response(event.message)
            if response.status_code != 200:
                self.error = "Unexpected error code from agent: %d - %s" % (response.status_code, response.status_description)
            connections = response.results
            count = 0
            for conn in connections:
                if conn.role == 'edge-uplink' and conn.container == self.edge_id:
                    count += 1
            if count != 1:
                self.error = "Incorrect uplink count for container-id.  Expected 1, got %d" % count
            self.interior_conn.close()
            self.edge_conn.close()
            self.timer.cancel()

    def run(self):
        Container(self).run()


class DynamicAddressTest(MessagingHandler):
    def __init__(self, receiver_host, sender_host):
        super(DynamicAddressTest, self).__init__()
        self.receiver_host = receiver_host
        self.sender_host   = sender_host

        self.receiver_conn = None
        self.sender_conn   = None
        self.receiver      = None
        self.address       = None
        self.count         = 300
        self.n_rcvd        = 0
        self.n_sent        = 0
        self.error         = None

    def timeout(self):
        self.error = "Timeout Expired - n_sent=%d n_rcvd=%d addr=%s" % (self.n_sent, self.n_rcvd, self.address)
        self.receiver_conn.close()
        self.sender_conn.close()

    def on_start(self, event):
        self.timer         = event.reactor.schedule(5.0, Timeout(self))
        self.receiver_conn = event.container.connect(self.receiver_host)
        self.sender_conn   = event.container.connect(self.sender_host)
        self.receiver      = event.container.create_receiver(self.receiver_conn, dynamic=True)

    def on_link_opened(self, event):
        if event.receiver == self.receiver:
            self.address = self.receiver.remote_source.address
            self.sender  = event.container.create_sender(self.sender_conn, self.address)

    def on_sendable(self, event):
        while self.n_sent < self.count:
            self.sender.send(Message(body="Message %d" % self.n_sent))
            self.n_sent += 1

    def on_message(self, event):
        self.n_rcvd += 1
        if self.n_rcvd == self.count:
            self.receiver_conn.close()
            self.sender_conn.close()
            self.timer.cancel()

    def run(self):
        Container(self).run()


class MobileAddressTest(MessagingHandler):
    def __init__(self, receiver_host, sender_host, address):
        super(MobileAddressTest, self).__init__()
        self.receiver_host = receiver_host
        self.sender_host   = sender_host
        self.address       = address

        self.receiver_conn = None
        self.sender_conn   = None
        self.receiver      = None
        self.count         = 300
        self.rel_count     = 50
        self.n_rcvd        = 0
        self.n_sent        = 0
        self.n_settled     = 0
        self.n_released    = 0
        self.error         = None

    def timeout(self):
        self.error = "Timeout Expired - n_sent=%d n_rcvd=%d n_settled=%d n_released=%d addr=%s" % \
                     (self.n_sent, self.n_rcvd, self.n_settled, self.n_released, self.address)
        self.receiver_conn.close()
        self.sender_conn.close()

    def on_start(self, event):
        self.timer         = event.reactor.schedule(5.0, Timeout(self))
        self.receiver_conn = event.container.connect(self.receiver_host)
        self.sender_conn   = event.container.connect(self.sender_host)
        self.receiver      = event.container.create_receiver(self.receiver_conn, self.address)
        self.sender        = event.container.create_sender(self.sender_conn, self.address)

    def on_sendable(self, event):
        while self.n_sent < self.count:
            self.sender.send(Message(body="Message %d" % self.n_sent))
            self.n_sent += 1

    def on_message(self, event):
        self.n_rcvd += 1

    def on_settled(self, event):
        self.n_settled += 1
        if self.n_settled == self.count:
            self.receiver.close()
            for i in range(self.rel_count):
                self.sender.send(Message(body="Message %d" % self.n_sent))
                self.n_sent += 1

    def on_released(self, event):
        self.n_released += 1
        if self.n_released == self.rel_count:
            self.receiver_conn.close()
            self.sender_conn.close()
            self.timer.cancel()

    def run(self):
        Container(self).run()


if __name__ == '__main__':
    unittest.main(main_module())

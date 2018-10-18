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
from qpid_dispatch.management.client import Node


class AddrTimer(object):
    def __init__(self, parent, check_addr=False):
        self.parent = parent
        self.check_addr = check_addr

    def on_timer_task(self, event):
        if self.check_addr:
            self.parent.check_address()
        else:
            self.parent.create_sndr()


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
               ('listener', {'role': 'edge', 'port': edge_port_A}))
        router('INT.B', 'interior', ('connector', {'name': 'connectorToA', 'role': 'inter-router', 'port': inter_router_port}),
               ('listener', {'role': 'edge', 'port': edge_port_B}))
        router('EA1',   'edge',     ('connector', {'name': 'edge', 'role': 'edge', 'port': edge_port_A}))
        router('EA2',   'edge',     ('connector', {'name': 'edge', 'role': 'edge', 'port': edge_port_A}))
        router('EB1',   'edge',     ('connector', {'name': 'edge', 'role': 'edge', 'port': edge_port_B}))
        router('EB2',   'edge',     ('connector', {'name': 'edge', 'role': 'edge', 'port': edge_port_B}))

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
        test = MobileAddressTest(self.routers[0].addresses[0],
                                 self.routers[2].addresses[0],
                                 "test_12")
        test.run()
        self.assertEqual(None, test.error)

    def test_13_mobile_address_edge_to_edge_one_interior(self):
        test = MobileAddressTest(self.routers[2].addresses[0],
                                 self.routers[3].addresses[0],
                                 "test_13")
        test.run()
        self.assertEqual(None, test.error)

    def test_14_mobile_address_edge_to_edge_two_interior(self):
        test = MobileAddressTest(self.routers[2].addresses[0],
                                 self.routers[4].addresses[0],
                                 "test_14")
        test.run()
        self.assertEqual(None, test.error)

    # One sender two receiver tests.
    # One sender and two receivers on the same edge
    def test_15_mobile_address_same_edge(self):
        test = MobileAddressOneSenderTwoReceiversTest(self.routers[2].addresses[0],
                                                      self.routers[2].addresses[0],
                                                      self.routers[2].addresses[0],
                                                      "test_15")
        test.run()
        self.assertEqual(None, test.error)

    # One sender and two receivers on the different edges. The edges are
    #  hanging off the  same interior router.
    def test_16_mobile_address_edge_to_another_edge_same_interior(self):
        test = MobileAddressOneSenderTwoReceiversTest(self.routers[2].addresses[0],
                                                      self.routers[2].addresses[0],
                                                      self.routers[3].addresses[0],
                                                      "test_16")
        test.run()
        self.assertEqual(None, test.error)

    # Two receivers on the interior and sender on the edge
    def test_17_mobile_address_edge_to_interior(self):
        test = MobileAddressOneSenderTwoReceiversTest(self.routers[0].addresses[0],
                                                      self.routers[0].addresses[0],
                                                      self.routers[2].addresses[0],
                                                      "test_17")
        test.run()
        self.assertEqual(None, test.error)

    # Two receivers on the edge and the sender on the interior
    def test_18_mobile_address_interior_to_edge(self):
        test = MobileAddressOneSenderTwoReceiversTest(self.routers[2].addresses[0],
                                                      self.routers[2].addresses[0],
                                                      self.routers[0].addresses[0],
                                                      "test_18")
        test.run()
        self.assertEqual(None, test.error)

    # Two receivers on the edge and the sender on the 'other' interior
    def test_19_mobile_address_other_interior_to_edge(self):
        test = MobileAddressOneSenderTwoReceiversTest(self.routers[2].addresses[0],
                                                      self.routers[2].addresses[0],
                                                      self.routers[1].addresses[0],
                                                      "test_19")
        test.run()
        self.assertEqual(None, test.error)

    # Two receivers on the edge and the sender on the edge of
    # the 'other' interior
    def test_20_mobile_address_edge_to_edge_two_interiors(self):
        test = MobileAddressOneSenderTwoReceiversTest(self.routers[2].addresses[0],
                                                      self.routers[2].addresses[0],
                                                      self.routers[5].addresses[0],
                                                      "test_20")
        test.run()
        self.assertEqual(None, test.error)

    # One receiver in an edge, another one in interior and the sender
    # is on the edge of another interior
    def test_21_mobile_address_edge_interior_receivers(self):
        test = MobileAddressOneSenderTwoReceiversTest(self.routers[4].addresses[0],
                                                      self.routers[1].addresses[0],
                                                      self.routers[2].addresses[0],
                                                      "test_21")
        test.run()
        self.assertEqual(None, test.error)

    # Two receivers one on each interior router and and an edge sender
    # connectoed to the first interior
    def test_22_mobile_address_edge_sender_two_interior_receivers(self):
        test = MobileAddressOneSenderTwoReceiversTest(self.routers[0].addresses[0],
                                                      self.routers[1].addresses[0],
                                                      self.routers[3].addresses[0],
                                                      "test_22")
        test.run()
        self.assertEqual(None, test.error)

    def test_23_mobile_address_edge_sender_two_edge_receivers(self):
        test = MobileAddressOneSenderTwoReceiversTest(self.routers[4].addresses[0],
                                                      self.routers[5].addresses[0],
                                                      self.routers[2].addresses[0],
                                                      "test_23")
        test.run()
        self.assertEqual(None, test.error)

    # 1 Sender and 3 receivers all on the same edge
    def test_24_multicast_mobile_address_same_edge(self):
        test = MobileAddressMulticastTest(self.routers[2].addresses[0],
                                          self.routers[2].addresses[0],
                                          self.routers[2].addresses[0],
                                          self.routers[2].addresses[0],
                                          "multicast.24")
        test.run()
        self.assertEqual(None, test.error)

    # 1 Sender and receiver on one edge and 2 receivers on another edge
    # all in the same  interior
    def test_25_multicast_mobile_address_different_edges_same_interior(self):
        test = MobileAddressMulticastTest(self.routers[2].addresses[0],
                                          self.routers[2].addresses[0],
                                          self.routers[3].addresses[0],
                                          self.routers[3].addresses[0],
                                          "multicast.25")
        test.run()
        self.assertEqual(None, test.error)

    # Two receivers on each edge, one receiver on interior and sender
    # on the edge
    def test_26_multicast_mobile_address_edge_to_interior(self):
        test = MobileAddressMulticastTest(self.routers[2].addresses[0],
                                          self.routers[3].addresses[0],
                                          self.routers[0].addresses[0],
                                          self.routers[2].addresses[0],
                                          "multicast.26")
        test.run()
        self.assertEqual(None, test.error)

    # Receivers on the edge and sender on the interior
    def test_27_multicast_mobile_address_interior_to_edge(self):
        test = MobileAddressMulticastTest(self.routers[2].addresses[0],
                                          self.routers[2].addresses[0],
                                          self.routers[3].addresses[0],
                                          self.routers[0].addresses[0],
                                          "multicast.27", check_addr=True)
        test.run()
        self.assertEqual(None, test.error)

    # Receivers on the edge and sender on an interior that is not connected
    # to the edges.
    def test_28_multicast_mobile_address_other_interior_to_edge(self):
        test = MobileAddressMulticastTest(self.routers[2].addresses[0],
                                          self.routers[2].addresses[0],
                                          self.routers[3].addresses[0],
                                          self.routers[1].addresses[0],
                                          "multicast.28", check_addr=True)
        test.run()
        self.assertEqual(None, test.error)

    # Sender on an interior and 3 receivers connected to three different edges
    def test_29_multicast_mobile_address_edge_to_edge_two_interiors(self):
        test = MobileAddressMulticastTest(self.routers[2].addresses[0],
                                          self.routers[3].addresses[0],
                                          self.routers[4].addresses[0],
                                          self.routers[0].addresses[0],
                                          "multicast.29", check_addr=True)
        test.run()
        self.assertEqual(None, test.error)

    def test_30_multicast_mobile_address_all_edges(self):
        test = MobileAddressMulticastTest(self.routers[2].addresses[0],
                                          self.routers[3].addresses[0],
                                          self.routers[4].addresses[0],
                                          self.routers[5].addresses[0],
                                          "multicast.30")
        test.run()
        self.assertEqual(None, test.error)



    ######### Multicast Large message tests
    # 1 Sender and 3 receivers all on the same edge
    def test_31_multicast_mobile_address_same_edge(self):
        test = MobileAddressMulticastTest(self.routers[2].addresses[0],
                                          self.routers[2].addresses[0],
                                          self.routers[2].addresses[0],
                                          self.routers[2].addresses[0],
                                          "multicast.31", True,
                                          check_addr=False)
        test.run()
        self.assertEqual(None, test.error)

    # 1 Sender on one edge and 3 receivers on another edge all in the same
    # interior
    def test_32_multicast_mobile_address_different_edges_same_interior(self):
        test = MobileAddressMulticastTest(self.routers[2].addresses[0],
                                          self.routers[2].addresses[0],
                                          self.routers[3].addresses[0],
                                          self.routers[3].addresses[0],
                                          "multicast.32", True)
        test.run()
        self.assertEqual(None, test.error)

    # Two receivers on each edge, one receiver on interior and sender
    # on the edge
    def test_33_multicast_mobile_address_edge_to_interior(self):
        test = MobileAddressMulticastTest(self.routers[2].addresses[0],
                                          self.routers[3].addresses[0],
                                          self.routers[0].addresses[0],
                                          self.routers[2].addresses[0],
                                          "multicast.33", True)
        test.run()
        self.assertEqual(None, test.error)

    # Receivers on the edge and sender on the interior
    def test_34_multicast_mobile_address_interior_to_edge(self):
        test = MobileAddressMulticastTest(self.routers[2].addresses[0],
                                          self.routers[2].addresses[0],
                                          self.routers[3].addresses[0],
                                          self.routers[0].addresses[0],
                                          "multicast.34", True,
                                          check_addr=True)
        test.run()
        self.assertEqual(None, test.error)

    # Receivers on the edge and sender on an interior that is not connected
    # to the edges.
    def test_35_multicast_mobile_address_other_interior_to_edge(self):
        test = MobileAddressMulticastTest(self.routers[2].addresses[0],
                                          self.routers[2].addresses[0],
                                          self.routers[3].addresses[0],
                                          self.routers[1].addresses[0],
                                          "multicast.35", large_msg=True,
                                          check_addr=True)
        test.run()
        self.assertEqual(None, test.error)

    # Sender on an interior and 3 receivers connected to three different edges
    def test_36_multicast_mobile_address_edge_to_edge_two_interiors(self):
        test = MobileAddressMulticastTest(self.routers[2].addresses[0],
                                          self.routers[3].addresses[0],
                                          self.routers[4].addresses[0],
                                          self.routers[0].addresses[0],
                                          "multicast.36", large_msg=True,
                                          check_addr=True)
        test.run()
        self.assertEqual(None, test.error)

    def test_37_multicast_mobile_address_all_edges(self):
        test = MobileAddressMulticastTest(self.routers[2].addresses[0],
                                          self.routers[3].addresses[0],
                                          self.routers[4].addresses[0],
                                          self.routers[5].addresses[0],
                                          "multicast.37", True)
        test.run()
        self.assertEqual(None, test.error)

    def test_38_mobile_addr_event_three_receivers_same_interior(self):
        test = MobileAddressEventTest(self.routers[2].addresses[0],
                                      self.routers[3].addresses[0],
                                      self.routers[3].addresses[0],
                                      self.routers[2].addresses[0],
                                      self.routers[0].addresses[0],
                                      "test_38", True)

        test.run()
        self.assertEqual(None, test.error)

    def test_39_mobile_addr_event_three_receivers_diff_interior(self):
        # This will test the QDRC_EVENT_ADDR_TWO_DEST event
        test = MobileAddressEventTest(self.routers[2].addresses[0],
                                      self.routers[4].addresses[0],
                                      self.routers[5].addresses[0],
                                      self.routers[2].addresses[0],
                                      self.routers[0].addresses[0],
                                      "test_39", True)

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
                if conn.role == 'edge' and conn.container == self.edge_id:
                    count += 1
            if count != 1:
                self.error = "Incorrect edge count for container-id.  Expected 1, got %d" % count
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
        self.sender        = None

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
            message = Message(body="Message %d" % self.n_sent)
            self.sender.send(message)
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


class MobileAddressOneSenderTwoReceiversTest(MessagingHandler):
    def __init__(self, receiver1_host, receiver2_host, sender_host, address):
        super(MobileAddressOneSenderTwoReceiversTest, self).__init__()
        self.receiver1_host = receiver1_host
        self.receiver2_host = receiver2_host
        self.sender_host = sender_host
        self.address = address

        # One sender connection and two receiver connections
        self.receiver1_conn = None
        self.receiver2_conn = None
        self.sender_conn   = None

        self.receiver1 = None
        self.receiver2 = None
        self.sender = None

        self.count = 300
        self.rel_count = 50
        self.n_rcvd1 = 0
        self.n_rcvd2 = 0
        self.n_sent = 0
        self.n_settled = 0
        self.n_released = 0
        self.error = None
        self.timer = None
        self.all_msgs_received = False
        self.recvd_msg_bodies = dict()
        self.dup_msg = None

    def timeout(self):
        if self.dup_msg:
            self.error = "Duplicate message %s received " % self.dup_msg
        else:
            self.error = "Timeout Expired - n_sent=%d n_rcvd=%d n_settled=%d n_released=%d addr=%s" % \
                         (self.n_sent, (self.n_rcvd1 + self.n_rcvd2), self.n_settled, self.n_released, self.address)

        self.receiver1_conn.close()
        self.receiver2_conn.close()
        self.sender_conn.close()

    def on_start(self, event):
        self.timer = event.reactor.schedule(5.0, Timeout(self))

        # Create two receivers
        self.receiver1_conn = event.container.connect(self.receiver1_host)
        self.receiver2_conn = event.container.connect(self.receiver2_host)
        self.receiver1 = event.container.create_receiver(self.receiver1_conn,
                                                         self.address)
        self.receiver2 = event.container.create_receiver(self.receiver2_conn,
                                                         self.address)

        # Create one sender
        self.sender_conn = event.container.connect(self.sender_host)
        self.sender = event.container.create_sender(self.sender_conn,
                                                    self.address)

    def on_sendable(self, event):
        while self.n_sent < self.count:
            self.sender.send(Message(body="Message %d" % self.n_sent))
            self.n_sent += 1

    def on_message(self, event):
        if self.recvd_msg_bodies.get(event.message.body):
            self.dup_msg = event.message.body
            self.timeout()
        else:
            self.recvd_msg_bodies[event.message.body] = event.message.body

        if event.receiver == self.receiver1:
            self.n_rcvd1 += 1
        if event.receiver == self.receiver2:
            self.n_rcvd2 += 1

        if self.n_sent == self.n_rcvd1 + self.n_rcvd2:
            self.all_msgs_received = True

    def on_settled(self, event):
        self.n_settled += 1
        if self.n_settled == self.count:
            self.receiver1.close()
            self.receiver2.close()
            for i in range(self.rel_count):
                self.sender.send(Message(body="Message %d" % self.n_sent))
                self.n_sent += 1

    def on_released(self, event):
        self.n_released += 1
        if self.n_released == self.rel_count and self.all_msgs_received:
            self.receiver1_conn.close()
            self.receiver2_conn.close()
            self.sender_conn.close()
            self.timer.cancel()

    def run(self):
        Container(self).run()


class MobileAddressMulticastTest(MessagingHandler):
    def __init__(self, receiver1_host, receiver2_host, receiver3_host,
                 sender_host, address, large_msg=False, check_addr=False):
        super(MobileAddressMulticastTest, self).__init__()
        self.receiver1_host = receiver1_host
        self.receiver2_host = receiver2_host
        self.receiver3_host = receiver3_host
        self.sender_host = sender_host
        self.address = address

        # One sender connection and two receiver connections
        self.receiver1_conn = None
        self.receiver2_conn = None
        self.receiver3_conn = None
        self.sender_conn = None

        self.receiver1 = None
        self.receiver2 = None
        self.receiver3 = None
        self.sender = None

        self.count = 200
        self.n_rcvd1 = 0
        self.n_rcvd2 = 0
        self.n_rcvd3 = 0
        self.n_sent = 0
        self.n_settled = 0
        self.n_released = 0
        self.error = None
        self.timer = None
        self.all_msgs_received = False
        self.recvd1_msgs = dict()
        self.recvd2_msgs = dict()
        self.recvd3_msgs = dict()
        self.dup_msg_rcvd = False
        self.dup_msg = None
        self.receiver_name = None
        self.large_msg = large_msg
        self.body = ""
        self.r_attaches = 0
        self.addr_timer = None
        self.num_attempts = 0
        self.container = None
        self.check_addr = check_addr

        if self.large_msg:
            for i in range(10000):
                self.body += "0123456789101112131415"

    def timeout(self):
        if self.dup_msg:
            self.error = "%s received  duplicate message %s" % \
                         (self.receiver_name, self.dup_msg)
        else:
            if not self.error:
                self.error = "Timeout Expired - n_sent=%d n_rcvd1=%d " \
                             "n_rcvd2=%d n_rcvd3=%d addr=%s" % \
                             (self.n_sent, self.n_rcvd1, self.n_rcvd2,
                              self.n_rcvd3, self.address)
        self.receiver1_conn.close()
        self.receiver2_conn.close()
        self.receiver3_conn.close()
        if self.sender_conn:
            self.sender_conn.close()

    def check_address(self):
        local_node = Node.connect(self.sender_host, timeout=TIMEOUT)
        outs = local_node.query(type='org.apache.qpid.dispatch.router.address')
        found = False
        for result in outs.results:
            if self.address in result[0]:
                found = True
                self.sender_conn = self.container.connect(self.sender_host)
                self.sender = self.container.create_sender(self.sender_conn,
                                                           self.address)
                local_node.close()
                break

        if not found:
            self.error = "Unable to create sender because of " \
                         "absence of address in the address table"
            self.addr_timer.cancel()
            self.timeout()
            local_node.close()

    def create_sndr(self):
        self.sender_conn = self.container.connect(self.sender_host)
        self.sender = self.container.create_sender(self.sender_conn,
                                                   self.address)

    def on_start(self, event):
        if self.large_msg:
            self.timer = event.reactor.schedule(10.0, Timeout(self))
        else:
            self.timer = event.reactor.schedule(20.0, Timeout(self))

        # Create two receivers
        self.receiver1_conn = event.container.connect(self.receiver1_host)
        self.receiver2_conn = event.container.connect(self.receiver2_host)
        self.receiver3_conn = event.container.connect(self.receiver3_host)
        self.receiver1 = event.container.create_receiver(self.receiver1_conn,
                                                         self.address)
        self.receiver2 = event.container.create_receiver(self.receiver2_conn,
                                                         self.address)
        self.receiver3 = event.container.create_receiver(self.receiver3_conn,
                                                         self.address)
        self.container = event.container

    def on_link_opened(self, event):
        if event.receiver == self.receiver1 or \
                event.receiver == self.receiver2 or \
                event.receiver == self.receiver3:
            self.r_attaches += 1
            if self.r_attaches == 3:
                self.addr_timer = event.reactor.schedule(4.0,
                                                             AddrTimer(self, self.check_addr))

    def on_sendable(self, event):
        while self.n_sent < self.count:
            msg = None
            if self.large_msg:
                msg = Message(body=self.body)
            else:
                msg = Message(body="Message %d" % self.n_sent)
            msg.correlation_id = self.n_sent
            self.sender.send(msg)
            self.n_sent += 1

    def on_message(self, event):
        if event.receiver == self.receiver1:
            if self.recvd1_msgs.get(event.message.correlation_id):
                self.dup_msg = event.message.correlation_id
                self.receiver_name = "Receiver 1"
                self.timeout()
            self.n_rcvd1 += 1
            self.recvd1_msgs[event.message.correlation_id] = event.message.correlation_id
        if event.receiver == self.receiver2:
            if self.recvd2_msgs.get(event.message.correlation_id):
                self.dup_msg = event.message.correlation_id
                self.receiver_name = "Receiver 2"
                self.timeout()
            self.n_rcvd2 += 1
            self.recvd2_msgs[event.message.correlation_id] = event.message.correlation_id
        if event.receiver == self.receiver3:
            if self.recvd3_msgs.get(event.message.correlation_id):
                self.dup_msg = event.message.correlation_id
                self.receiver_name = "Receiver 3"
                self.timeout()
            self.n_rcvd3 += 1
            self.recvd3_msgs[event.message.correlation_id] = event.message.correlation_id

        if self.n_rcvd1 == self.count and self.n_rcvd2 == self.count and \
                self.n_rcvd3 == self.count:
            self.timer.cancel()
            self.receiver1_conn.close()
            self.receiver2_conn.close()
            self.receiver3_conn.close()
            self.sender_conn.close()

    def run(self):
        Container(self).run()


class MobileAddressEventTest(MessagingHandler):
    def __init__(self, receiver1_host, receiver2_host, receiver3_host,
                 sender_host, interior_host, address, check_addr=False):
        super(MobileAddressEventTest, self).__init__(auto_accept=False)
        self.receiver1_host = receiver1_host
        self.receiver2_host = receiver2_host
        self.receiver3_host = receiver3_host
        self.sender_host = sender_host
        self.address = address
        self.receiver1_conn = None
        self.receiver2_conn = None
        self.receiver3_conn = None
        self.sender_conn = None
        self.recvd1_msgs = dict()
        self.recvd2_msgs = dict()
        self.recvd3_msgs = dict()
        self.n_rcvd1 = 0
        self.n_rcvd2 = 0
        self.n_rcvd3 = 0
        self.timer = None
        self.receiver1 = None
        self.receiver2 = None
        self.receiver3 = None
        self.sender = None
        self.interior_host = interior_host
        self.container = None
        self.check_addr = check_addr
        self.count = 600
        self.dup_msg = None
        self.receiver_name = None
        self.n_sent = 0
        self.error = None
        self.r_attaches = 0
        self.n_released = 0
        self.n_settled = 0
        self.addr_timer = None
        self.container = None



    def timeout(self):
        if self.dup_msg:
            self.error = "%s received  duplicate message %s" % \
                         (self.receiver_name, self.dup_msg)
        else:
            if not self.error:
                self.error = "Timeout Expired - n_sent=%d n_rcvd1=%d " \
                             "n_rcvd2=%d n_rcvd3=%d addr=%s" % \
                             (self.n_sent, self.n_rcvd1, self.n_rcvd2,
                              self.n_rcvd3, self.address)
        self.receiver1_conn.close()
        self.receiver2_conn.close()
        self.receiver3_conn.close()
        if self.sender_conn:
            self.sender_conn.close()

    def check_address(self):
        local_node = Node.connect(self.interior_host, timeout=TIMEOUT)
        outs = local_node.query(type='org.apache.qpid.dispatch.router.address')
        remote_count = outs.attribute_names.index("remoteCount")
        found = False
        for result in outs.results:

            if self.address in result[0]:
                found = True
                self.sender_conn = self.container.connect(self.sender_host)
                self.sender = self.container.create_sender(self.sender_conn,
                                                           self.address)
                break

        if not found:
            self.error = "Unable to create sender because of " \
                         "absence of address in the address table"
            self.addr_timer.cancel()
            self.timeout()

    def on_start(self, event):
        self.timer = event.reactor.schedule(10.0, Timeout(self))

        # Create two receivers
        self.receiver1_conn = event.container.connect(self.receiver1_host)
        self.receiver2_conn = event.container.connect(self.receiver2_host)
        self.receiver3_conn = event.container.connect(self.receiver3_host)

        # Create all 3 receivers first.
        self.receiver1 = event.container.create_receiver(self.receiver1_conn,
                                                         self.address)
        self.receiver2 = event.container.create_receiver(self.receiver2_conn,
                                                         self.address)
        self.receiver3 = event.container.create_receiver(self.receiver3_conn,
                                                         self.address)
        self.container = event.container

        self.addr_timer = event.reactor.schedule(4.0,
                                                 AddrTimer(self,
                                                           self.check_addr))

    def on_sendable(self, event):
        if self.n_sent < self.count:
            msg = Message(body="Message %d" % self.n_sent)
            msg.correlation_id = self.n_sent
            self.sender.send(msg)
            self.n_sent += 1

    def on_message(self, event):
        if event.receiver == self.receiver1:
            if self.recvd1_msgs.get(event.message.correlation_id):
                self.dup_msg = event.message.correlation_id
                self.receiver_name = "Receiver 1"
                self.timeout()
            self.n_rcvd1 += 1
            self.recvd1_msgs[
                event.message.correlation_id] = event.message.correlation_id

            event.delivery.settle()

        if event.receiver == self.receiver2:
            if self.recvd2_msgs.get(event.message.correlation_id):
                self.dup_msg = event.message.correlation_id
                self.receiver_name = "Receiver 2"
                self.timeout()
            self.n_rcvd2 += 1
            self.recvd2_msgs[
                event.message.correlation_id] = event.message.correlation_id

            event.delivery.settle()

        if event.receiver == self.receiver3:
            if self.recvd3_msgs.get(event.message.correlation_id):
                self.dup_msg = event.message.correlation_id
                self.receiver_name = "Receiver 3"
                self.timeout()
            self.n_rcvd3 += 1
            self.recvd3_msgs[
                event.message.correlation_id] = event.message.correlation_id

            event.delivery.settle()

    def on_settled(self, event):
        if self.n_rcvd1 + self.n_rcvd2 + self.n_rcvd3 == self.count and \
                self.n_rcvd2 !=0 and self.n_rcvd3 !=0:
            self.timer.cancel()
            self.receiver1_conn.close()
            self.receiver2_conn.close()
            self.receiver3_conn.close()
            self.sender_conn.close()

    def on_released(self, event):
        self.n_released += 1

    def run(self):
        Container(self).run()


if __name__== '__main__':
    unittest.main(main_module())

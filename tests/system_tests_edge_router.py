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

from time import sleep
from threading import Event
from threading import Timer

import unittest2 as unittest
from proton import Message, Timeout
from system_test import TestCase, Qdrouterd, main_module, TIMEOUT, MgmtMsgProxy
from system_test import AsyncTestReceiver
from system_test import AsyncTestSender
from system_test import QdManager
from system_tests_link_routes import ConnLinkRouteService
from test_broker import FakeService
from proton.handlers import MessagingHandler
from proton.reactor import Container, DynamicNodeProperties
from proton.utils import BlockingConnection
from qpid_dispatch.management.client import Node
from subprocess import PIPE, STDOUT
import re


class AddrTimer(object):
    def __init__(self, parent):
        self.parent = parent

    def on_timer_task(self, event):
            self.parent.check_address()


class EdgeRouterTest(TestCase):

    inter_router_port = None

    @classmethod
    def setUpClass(cls):
        """Start a router"""
        super(EdgeRouterTest, cls).setUpClass()

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
        edge_port_A = cls.tester.get_port()
        edge_port_B = cls.tester.get_port()

        router('INT.A', 'interior', ('listener', {'role': 'inter-router', 'port': inter_router_port}),
               ('listener', {'role': 'edge', 'port': edge_port_A}))
        router('INT.B', 'interior', ('connector', {'name': 'connectorToA', 'role': 'inter-router', 'port': inter_router_port}),
               ('listener', {'role': 'edge', 'port': edge_port_B}))
        router('EA1', 'edge', ('connector', {'name': 'edge', 'role': 'edge',
                                             'port': edge_port_A}
                               ),
               ('connector', {'name': 'edge.1', 'role': 'edge',
                              'port': edge_port_B}
                )
               )

        cls.routers[0].wait_router_connected('INT.B')
        cls.routers[1].wait_router_connected('INT.A')

    def __init__(self, test_method):
        TestCase.__init__(self, test_method)
        self.success = False
        self.timer_delay = 2
        self.max_attempts = 3
        self.attempts = 0

    def run_qdstat(self, args, regexp=None, address=None):
        p = self.popen(
            ['qdstat', '--bus', str(address or self.router.addresses[0]),
             '--timeout', str(TIMEOUT)] + args,
            name='qdstat-' + self.id(), stdout=PIPE, expect=None,
            universal_newlines=True)

        out = p.communicate()[0]
        assert p.returncode == 0, \
            "qdstat exit status %s, output:\n%s" % (p.returncode, out)
        if regexp: assert re.search(regexp, out,
                                    re.I), "Can't find '%s' in '%s'" % (
        regexp, out)
        return out

    def can_terminate(self):
        if self.attempts == self.max_attempts:
            return True

        if self.success:
            return True

        return False

    def run_int_b_edge_qdstat(self):
        outs = self.run_qdstat(['--edge'],
                               address=self.routers[2].addresses[0])
        lines = outs.split("\n")
        for line in lines:
            if "INT.B" in line and "yes" in line:
                self.success = True

    def run_int_a_edge_qdstat(self):
        outs = self.run_qdstat(['--edge'],
                               address=self.routers[2].addresses[0])
        lines = outs.split("\n")
        for line in lines:
            if "INT.A" in line and "yes" in line:
                self.success = True

    def schedule_int_a_qdstat_test(self):
        if self.attempts < self.max_attempts:
            if not self.success:
                Timer(self.timer_delay, self.run_int_a_edge_qdstat).start()
                self.attempts += 1

    def schedule_int_b_qdstat_test(self):
        if self.attempts < self.max_attempts:
            if not self.success:
                Timer(self.timer_delay, self.run_int_b_edge_qdstat).start()
                self.attempts += 1

    def test_01_active_flag(self):
        """
        In this test, we have one edge router connected to two interior
        routers. One connection is to INT.A and another connection is to
        INT.B . But only one of these connections is active. We use qdstat
        to make sure that only one of these connections is active.
        Then we kill the router with the active connection and make sure
        that the other connection is now the active one
        """
        success = False
        outs = self.run_qdstat(['--edge'],
                               address=self.routers[0].addresses[0])
        lines = outs.split("\n")
        for line in lines:
            if "EA1" in line and "yes" in line:
                success = True
        if not success:
            self.fail("Active edge connection not found not found for "
                      "interior router")

        outs = self.run_qdstat(['--edge'],
                               address=self.routers[2].addresses[0])
        conn_map_edge = dict()
        #
        # We dont know which interior router the edge will connect to.
        #
        conn_map_edge["INT.A"] = False
        conn_map_edge["INT.B"] = False
        lines = outs.split("\n")
        for line in lines:
            if "INT.A" in line and "yes" in line:
                conn_map_edge["INT.A"] = True
            if "INT.B" in line and "yes" in line:
                conn_map_edge["INT.B"] = True

        if conn_map_edge["INT.A"] and conn_map_edge["INT.B"]:
            self.fail("Edhe router has two active connections to interior "
                      "routers. Should have only one")

        if not conn_map_edge["INT.A"] and  not conn_map_edge["INT.B"]:
            self.fail("There are no active aconnections to interior routers")

        if conn_map_edge["INT.A"]:
            #
            # INT.A has the active connection. Let's kill INT.A and see
            # if the other connection becomes active
            #
            EdgeRouterTest.routers[0].teardown()
            self.schedule_int_b_qdstat_test()

            while not self.can_terminate():
                pass

            self.assertTrue(self.success)

        elif conn_map_edge["INT.B"]:
            #
            # INT.B has the active connection. Let's kill INT.B and see
            # if the other connection becomes active
            #
            EdgeRouterTest.routers[1].teardown()
            self.schedule_int_a_qdstat_test()

            while not self.can_terminate():
                pass

            self.assertTrue(self.success)


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
                                          "multicast.25",
                                          self.routers[0].addresses[0])
        test.run()
        self.assertEqual(None, test.error)

    # Two receivers on each edge, one receiver on interior and sender
    # on the edge
    def test_26_multicast_mobile_address_edge_to_interior(self):
        test = MobileAddressMulticastTest(self.routers[2].addresses[0],
                                          self.routers[3].addresses[0],
                                          self.routers[0].addresses[0],
                                          self.routers[2].addresses[0],
                                          "multicast.26",
                                          self.routers[0].addresses[0])
        test.run()
        self.assertEqual(None, test.error)

    # Receivers on the edge and sender on the interior
    def test_27_multicast_mobile_address_interior_to_edge(self):
        test = MobileAddressMulticastTest(self.routers[2].addresses[0],
                                          self.routers[2].addresses[0],
                                          self.routers[3].addresses[0],
                                          self.routers[0].addresses[0],
                                          "multicast.27",
                                          self.routers[0].addresses[0])
        test.run()
        self.assertEqual(None, test.error)

    # Receivers on the edge and sender on an interior that is not connected
    # to the edges.
    def test_28_multicast_mobile_address_other_interior_to_edge(self):
        test = MobileAddressMulticastTest(self.routers[2].addresses[0],
                                          self.routers[2].addresses[0],
                                          self.routers[3].addresses[0],
                                          self.routers[1].addresses[0],
                                          "multicast.28")
        test.run()
        self.assertEqual(None, test.error)

    # Sender on an interior and 3 receivers connected to three different edges
    def test_29_multicast_mobile_address_edge_to_edge_two_interiors(self):
        test = MobileAddressMulticastTest(self.routers[2].addresses[0],
                                          self.routers[3].addresses[0],
                                          self.routers[4].addresses[0],
                                          self.routers[0].addresses[0],
                                          "multicast.29")
        test.run()
        self.assertEqual(None, test.error)

    def test_30_multicast_mobile_address_all_edges(self):
        test = MobileAddressMulticastTest(self.routers[2].addresses[0],
                                          self.routers[3].addresses[0],
                                          self.routers[4].addresses[0],
                                          self.routers[5].addresses[0],
                                          "multicast.30",
                                          self.routers[0].addresses[0])
        test.run()
        self.assertEqual(None, test.error)



    ######### Multicast Large message tests ######################

    # 1 Sender and 3 receivers all on the same edge
    def test_31_multicast_mobile_address_same_edge(self):
        test = MobileAddressMulticastTest(self.routers[2].addresses[0],
                                          self.routers[2].addresses[0],
                                          self.routers[2].addresses[0],
                                          self.routers[2].addresses[0],
                                          "multicast.31", large_msg=True)
        test.run()
        self.assertEqual(None, test.error)

    # 1 Sender on one edge and 3 receivers on another edge all in the same
    # interior
    def test_32_multicast_mobile_address_different_edges_same_interior(self):
        test = MobileAddressMulticastTest(self.routers[2].addresses[0],
                                          self.routers[2].addresses[0],
                                          self.routers[3].addresses[0],
                                          self.routers[3].addresses[0],
                                          "multicast.32",
                                          self.routers[0].addresses[0],
                                          large_msg=True)
        test.run()
        self.assertEqual(None, test.error)

    # Two receivers on each edge, one receiver on interior and sender
    # on the edge
    def test_33_multicast_mobile_address_edge_to_interior(self):
        test = MobileAddressMulticastTest(self.routers[2].addresses[0],
                                          self.routers[3].addresses[0],
                                          self.routers[0].addresses[0],
                                          self.routers[2].addresses[0],
                                          "multicast.33", large_msg=True)
        test.run()
        self.assertEqual(None, test.error)

    # Receivers on the edge and sender on the interior
    def test_34_multicast_mobile_address_interior_to_edge(self):
        test = MobileAddressMulticastTest(self.routers[2].addresses[0],
                                          self.routers[2].addresses[0],
                                          self.routers[3].addresses[0],
                                          self.routers[0].addresses[0],
                                          "multicast.34", large_msg=True)
        test.run()
        self.assertEqual(None, test.error)

    # Receivers on the edge and sender on an interior that is not connected
    # to the edges.
    def test_35_multicast_mobile_address_other_interior_to_edge(self):
        test = MobileAddressMulticastTest(self.routers[2].addresses[0],
                                          self.routers[2].addresses[0],
                                          self.routers[3].addresses[0],
                                          self.routers[1].addresses[0],
                                          "multicast.35",
                                          self.routers[0].addresses[0],
                                          large_msg=True)
        test.run()
        self.assertEqual(None, test.error)

    # Sender on an interior and 3 receivers connected to three different edges
    def test_36_multicast_mobile_address_edge_to_edge_two_interiors(self):
        test = MobileAddressMulticastTest(self.routers[2].addresses[0],
                                          self.routers[3].addresses[0],
                                          self.routers[4].addresses[0],
                                          self.routers[0].addresses[0],
                                          "multicast.36", large_msg=True)
        test.run()
        self.assertEqual(None, test.error)

    def test_37_multicast_mobile_address_all_edges(self):
        test = MobileAddressMulticastTest(self.routers[2].addresses[0],
                                          self.routers[3].addresses[0],
                                          self.routers[4].addresses[0],
                                          self.routers[5].addresses[0],
                                          "multicast.37",
                                          self.routers[0].addresses[0],
                                          large_msg=True)
        test.run()
        self.assertEqual(None, test.error)

    def test_38_mobile_addr_event_three_receivers_same_interior(self):
        test = MobileAddressEventTest(self.routers[2].addresses[0],
                                      self.routers[3].addresses[0],
                                      self.routers[3].addresses[0],
                                      self.routers[2].addresses[0],
                                      self.routers[0].addresses[0],
                                      "test_38")

        test.run()
        self.assertEqual(None, test.error)

    def test_39_mobile_addr_event_three_receivers_diff_interior(self):
        # This will test the QDRC_EVENT_ADDR_TWO_DEST event
        test = MobileAddressEventTest(self.routers[2].addresses[0],
                                      self.routers[4].addresses[0],
                                      self.routers[5].addresses[0],
                                      self.routers[2].addresses[0],
                                      self.routers[0].addresses[0],
                                      "test_39")

        test.run()
        self.assertEqual(None, test.error)

    def test_40_drop_rx_client_multicast_large_message(self):
        # test what happens if some multicast receivers close in the middle of
        # a multiframe transfer
        test = MobileAddrMcastDroppedRxTest(self.routers[2].addresses[0],
                                            self.routers[2].addresses[0],
                                            self.routers[2].addresses[0],
                                            self.routers[2].addresses[0],
                                            "multicast.40")
        test.run()
        self.assertEqual(None, test.error)

    def test_41_drop_rx_client_multicast_small_message(self):
        # test what happens if some multicast receivers close in the middle of
        # a multiframe transfer
        test = MobileAddrMcastDroppedRxTest(self.routers[2].addresses[0],
                                            self.routers[2].addresses[0],
                                            self.routers[2].addresses[0],
                                            self.routers[2].addresses[0],
                                            "multicast.40",large_msg=False)
        test.run()
        self.assertEqual(None, test.error)


class LinkRouteProxyTest(TestCase):
    """
    Test edge router's ability to proxy configured and connection-scoped link
    routes into the interior
    """

    @classmethod
    def setUpClass(cls):
        """Start a router"""
        super(LinkRouteProxyTest, cls).setUpClass()

        def router(name, mode, extra):
            config = [
                ('router', {'mode': mode, 'id': name}),
                ('listener', {'role': 'normal', 'port': cls.tester.get_port()})
            ]

            if extra:
                config.extend(extra)
            config = Qdrouterd.Config(config)
            cls.routers.append(cls.tester.qdrouterd(name, config, wait=True))
            return cls.routers[-1]

        # configuration:
        # two edge routers connected via 2 interior routers.
        #
        #  +-------+    +---------+    +---------+    +-------+
        #  |  EA1  |<==>|  INT.A  |<==>|  INT.B  |<==>|  EB1  |
        #  +-------+    +---------+    +---------+    +-------+

        cls.routers = []

        interrouter_port = cls.tester.get_port()
        cls.INTA_edge_port   = cls.tester.get_port()
        cls.INTB_edge_port   = cls.tester.get_port()

        router('INT.A', 'interior',
               [('listener', {'role': 'inter-router', 'port': interrouter_port}),
                ('listener', {'role': 'edge', 'port': cls.INTA_edge_port})])
        cls.INT_A = cls.routers[0]
        cls.INT_A.listener = cls.INT_A.addresses[0]

        router('INT.B', 'interior',
               [('connector', {'name': 'connectorToA', 'role': 'inter-router',
                               'port': interrouter_port}),
                ('listener', {'role': 'edge', 'port': cls.INTB_edge_port})])
        cls.INT_B = cls.routers[1]
        cls.INT_B.listener = cls.INT_B.addresses[0]

        router('EA1', 'edge',
               [('listener', {'name': 'rc', 'role': 'route-container',
                              'port': cls.tester.get_port()}),
                ('connector', {'name': 'uplink', 'role': 'edge',
                               'port': cls.INTA_edge_port}),
                ('linkRoute', {'prefix': 'CfgLinkRoute1', 'containerId': 'FakeBroker', 'direction': 'in'}),
                ('linkRoute', {'prefix': 'CfgLinkRoute1', 'containerId': 'FakeBroker', 'direction': 'out'})])
        cls.EA1 = cls.routers[2]
        cls.EA1.listener = cls.EA1.addresses[0]
        cls.EA1.route_container = cls.EA1.addresses[1]

        router('EB1', 'edge',
               [('connector', {'name': 'uplink', 'role': 'edge',
                               'port': cls.INTB_edge_port}),
                ('listener', {'name': 'rc', 'role': 'route-container',
                              'port': cls.tester.get_port()}),
                ('linkRoute', {'pattern': '*.cfg.pattern.#', 'containerId': 'FakeBroker', 'direction': 'in'}),
                ('linkRoute', {'pattern': '*.cfg.pattern.#', 'containerId': 'FakeBroker', 'direction': 'out'})])
        cls.EB1 = cls.routers[3]
        cls.EB1.listener = cls.EB1.addresses[0]
        cls.EB1.route_container = cls.EB1.addresses[1]

        cls.INT_A.wait_router_connected('INT.B')
        cls.INT_B.wait_router_connected('INT.A')
        cls.EA1.wait_connectors()
        cls.EB1.wait_connectors()

        cls.CFG_LINK_ROUTE_TYPE = 'org.apache.qpid.dispatch.router.config.linkRoute'
        cls.CONN_LINK_ROUTE_TYPE = 'org.apache.qpid.dispatch.router.connection.linkRoute'
        cls.CONNECTOR_TYPE = 'org.apache.qpid.dispatch.connector'

    def _get_address(self, router, address):
        """Lookup address in route table"""
        a_type = 'org.apache.qpid.dispatch.router.address'
        addrs = router.management.query(a_type).get_dicts()
        return list(filter(lambda a: a['name'].find(address) != -1,
                           addrs))

    def _wait_address_gone(self, router, address):
        """Block until address is removed from the route table"""
        while self._get_address(router, address):
            sleep(0.1)

    def _test_traffic(self, sender, receiver, address, count=5):
        """Generate message traffic between two normal clients"""
        tr = AsyncTestReceiver(receiver, address)
        ts = AsyncTestSender(sender, address, count)
        ts.wait()  # wait until all sent
        for i in range(count):
            tr.queue.get(timeout=TIMEOUT)
        tr.stop()

    def test_01_immedate_detach_reattach(self):
        """
        Have a service for a link routed address abruptly detach
        in response to an incoming link attach

        The attaching client from EB1 will get an attach response then an
        immediate detach.  The client will immediately re-establish the link.
        """
        class AttachDropper(FakeService):
            def __init__(self, *args, **kwargs):
                super(AttachDropper, self).__init__(*args, **kwargs)
                self.link_dropped = Event()

            def on_link_remote_open(self, event):
                # drop it
                event.link.close()
                event.connection.close()
                self.link_dropped.set()

        ad = AttachDropper(self.EA1.route_container)
        self.INT_B.wait_address("CfgLinkRoute1")

        # create a consumer, do not wait for link to open, reattach
        # on received detach
        rx = AsyncTestReceiver(self.EB1.listener, 'CfgLinkRoute1/foo',
                               wait=False, recover_link=True)
        ad.link_dropped.wait(timeout=TIMEOUT)
        ad.join() # wait for thread exit

        # wait until prefix addresses are removed
        self._wait_address_gone(self.INT_B, "CCfgLinkRoute1")
        self._wait_address_gone(self.INT_B, "DCfgLinkRoute1")
        rx.stop()

        # now attach a working service to the same address,
        # make sure it all works
        fs = FakeService(self.EA1.route_container)
        self.INT_B.wait_address("CfgLinkRoute1")
        rx = AsyncTestReceiver(self.EB1.listener, 'CfgLinkRoute1/foo',
                               wait=False, recover_link=True)
        tx = AsyncTestSender(self.EA1.listener, 'CfgLinkRoute1/foo',
                             body="HEY HO LET'S GO!")
        tx.wait()

        msg = rx.queue.get(timeout=TIMEOUT)
        self.assertTrue(msg.body == "HEY HO LET'S GO!")
        rx.stop()
        fs.join()
        self.assertEqual(1, fs.in_count)
        self.assertEqual(1, fs.out_count)

        # wait until addresses are cleaned up
        self._wait_address_gone(self.INT_A, "CfgLinkRoute1")
        self._wait_address_gone(self.INT_B, "CfgLinkRoute1")

    def test_02_thrashing_link_routes(self):
        """
        Rapidly add and delete link routes at the edge
        """

        # activate the pre-configured link routes
        ea1_mgmt = self.EA1.management
        fs = FakeService(self.EA1.route_container)
        self.INT_B.wait_address("CfgLinkRoute1")

        for i in range(10):
            lr1 = ea1_mgmt.create(type=self.CFG_LINK_ROUTE_TYPE,
                                  name="TestLRout%d" % i,
                                  attributes={'pattern': 'Test/*/%d/#' % i,
                                              'containerId': 'FakeBroker',
                                              'direction': 'out'})
            lr2 = ea1_mgmt.create(type=self.CFG_LINK_ROUTE_TYPE,
                                  name="TestLRin%d" % i,
                                  attributes={'pattern': 'Test/*/%d/#' % i,
                                              'containerId': 'FakeBroker',
                                              'direction': 'in'})
            # verify that they are correctly propagated (once)
            if i == 9:
                self.INT_B.wait_address("Test/*/9/#")
            lr1.delete()
            lr2.delete()

        fs.join()
        self._wait_address_gone(self.INT_B, "CfgLinkRoute1")

    def _validate_topology(self, router, expected_links, address):
        """
        query existing links and verify they are set up as expected
        """
        mgmt = QdManager(self, address=router)
        # fetch all the connections
        cl = mgmt.query('org.apache.qpid.dispatch.connection')
        # map them by their identity
        conns = dict([(c['identity'], c) for c in cl])

        # now fetch all links for the address
        ll = mgmt.query('org.apache.qpid.dispatch.router.link')
        test_links = [l for l in ll if
                      l.get('owningAddr', '').find(address) != -1]
        self.assertEqual(len(expected_links), len(test_links))

        for elink in expected_links:
            matches = filter(lambda l: (l['linkDir'] == elink[0]
                                        and
                                        conns[l['connectionId']]['container'] == elink[1]
                                        and
                                        conns[l['connectionId']]['role'] == elink[2]),
                             test_links)
            self.assertTrue(len(list(matches)) == 1)

    def test_03_interior_conn_lost(self):
        """
        What happens when the interior connection bounces?
        """
        config = Qdrouterd.Config([('router', {'mode': 'edge',
                                               'id': 'Edge1'}),
                                   ('listener', {'role': 'normal',
                                                 'port': self.tester.get_port()}),
                                   ('listener', {'name': 'rc',
                                                 'role': 'route-container',
                                                 'port': self.tester.get_port()}),
                                   ('linkRoute', {'pattern': 'Edge1/*',
                                                  'containerId': 'FakeBroker',
                                                  'direction': 'in'}),
                                   ('linkRoute', {'pattern': 'Edge1/*',
                                                  'containerId': 'FakeBroker',
                                                  'direction': 'out'})])
        er = self.tester.qdrouterd('Edge1', config, wait=True)

        # activate the link routes before the connection exists
        fs = FakeService(er.addresses[1])
        er.wait_address("Edge1/*")

        # create the connection to interior
        er_mgmt = er.management
        ctor = er_mgmt.create(type=self.CONNECTOR_TYPE,
                              name='toA',
                              attributes={'role': 'edge',
                                          'port': self.INTA_edge_port})
        self.INT_B.wait_address("Edge1/*")

        # delete it, and verify the routes are removed
        ctor.delete()
        self._wait_address_gone(self.INT_B, "Edge1/*")

        # now recreate and verify routes re-appear
        ctor = er_mgmt.create(type=self.CONNECTOR_TYPE,
                              name='toA',
                              attributes={'role': 'edge',
                                          'port': self.INTA_edge_port})
        self.INT_B.wait_address("Edge1/*")
        self._test_traffic(self.INT_B.listener,
                           self.INT_B.listener,
                           "Edge1/One",
                           count=5)
        fs.join()
        self.assertEqual(5, fs.in_count)
        self.assertEqual(5, fs.out_count)

        er.teardown()
        self._wait_address_gone(self.INT_B, "Edge1/*")

    def test_50_link_topology(self):
        """
        Verify that the link topology that results from activating a link route
        and sending traffic is correct
        """
        fs = FakeService(self.EA1.route_container)
        self.INT_B.wait_address("CfgLinkRoute1")

        # create a sender on one edge and the receiver on another
        bc_b = BlockingConnection(self.EB1.listener, timeout=TIMEOUT)
        erx = bc_b.create_receiver(address="CfgLinkRoute1/buhbye", credit=10)
        bc_a = BlockingConnection(self.EA1.listener, timeout=TIMEOUT)
        etx = bc_a.create_sender(address="CfgLinkRoute1/buhbye")

        etx.send(Message(body="HI THERE"), timeout=TIMEOUT)
        self.assertEqual("HI THERE", erx.receive(timeout=TIMEOUT).body)
        erx.accept()

        # expect the following links have been established for the
        # "CfgLinkRoute1/buhbye" address:

        # EA1
        #   1 out link to   INT.A       (connection role: edge)
        #   1 in  link from bc_a        (normal)
        #   1 in  link from FakeBroker  (route-container)
        #   1 out link to   FakeBroker  (route-container)
        # INT.A
        #   1 in  link from EA1         (edge)
        #   1 out link to   INT.B       (inter-router)
        # INT.B
        #   1 out link to   EB1         (edge)
        #   1 in  link from INT.A       (inter-router)
        # EB1
        #   1 out link to   bc_b        (normal)
        #   1 in  link from INT.B       (edge)

        expect = {
            self.EA1.listener: [
                ('in',  bc_a.container.container_id, 'normal'),
                ('in',  'FakeBroker', 'route-container'),
                ('out', 'FakeBroker', 'route-container'),
                ('out', 'INT.A',      'edge')],
            self.INT_A.listener: [
                ('in',  'EA1',        'edge'),
                ('out', 'INT.B',      'inter-router')],
            self.INT_B.listener: [
                ('in',  'INT.A',      'inter-router'),
                ('out', 'EB1',        'edge')],
            self.EB1.listener: [
                ('in',  'INT.B',      'edge'),
                ('out', bc_b.container.container_id, 'normal')]
            }
        for router, expected_links in expect.items():
            self._validate_topology(router, expected_links,
                                    'CfgLinkRoute1/buhbye')

        fs.join()
        self.assertEqual(1, fs.in_count)
        self.assertEqual(1, fs.out_count)

    def test_51_link_route_proxy_configured(self):
        """
        Activate the configured link routes via a FakeService, verify proxies
        created by passing traffic from/to and interior router
        """
        a_type = 'org.apache.qpid.dispatch.router.address'

        fs = FakeService(self.EA1.route_container)
        self.INT_B.wait_address("CfgLinkRoute1")

        self._test_traffic(self.INT_B.listener,
                           self.INT_B.listener,
                           "CfgLinkRoute1/hi",
                           count=5)

        fs.join()
        self.assertEqual(5, fs.in_count)
        self.assertEqual(5, fs.out_count)

        # now that FakeService is gone, the link route should no longer be
        # active:
        self._wait_address_gone(self.INT_A, "CfgLinkRoute1")

        # repeat test, but this time with patterns:

        fs = FakeService(self.EB1.route_container)
        self.INT_A.wait_address("*.cfg.pattern.#")

        self._test_traffic(self.INT_A.listener,
                           self.INT_A.listener,
                           "MATCH.cfg.pattern",
                           count=5)

        fs.join()
        self.assertEqual(5, fs.in_count)
        self.assertEqual(5, fs.out_count)
        self._wait_address_gone(self.INT_A, "*.cfg.pattern.#")


    def test_52_conn_link_route_proxy(self):
        """
        Test connection scoped link routes by connecting a fake service to the
        Edge via the route-container connection.  Have the fake service
        configured some link routes.  Then have clients on the interior
        exchange messages via the fake service.
        """
        fs = ConnLinkRouteService(self.EA1.route_container,
                                  container_id="FakeService",
                                  config = [("ConnLinkRoute1",
                                             {"pattern": "Conn/*/One",
                                              "direction": "out"}),
                                            ("ConnLinkRoute2",
                                             {"pattern": "Conn/*/One",
                                              "direction": "in"})])
        self.assertEqual(2, len(fs.values))

        self.INT_B.wait_address("Conn/*/One")
        self.assertEqual(2, len(self._get_address(self.INT_A, "Conn/*/One")))

        # between interiors
        self._test_traffic(self.INT_B.listener,
                           self.INT_A.listener,
                           "Conn/BLAB/One",
                           count=5)

        # edge to edge
        self._test_traffic(self.EB1.listener,
                           self.EA1.listener,
                           "Conn/BLECH/One",
                           count=5)
        fs.join()
        self.assertEqual(10, fs.in_count)
        self.assertEqual(10, fs.out_count)

        self._wait_address_gone(self.INT_A, "Conn/*/One")


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
                 sender_host, address, check_addr_host=None, large_msg=False):
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
        self.reactor = None
        self.addr_timer = None

        # The maximum number of times we are going to try to check if the
        # address  has propagated.
        self.max_attempts = 5
        self.num_attempts = 0
        self.num_attempts = 0
        self.container = None
        self.check_addr_host = check_addr_host
        if not self.check_addr_host:
            self.check_addr_host = self.sender_host

        if self.large_msg:
            self.body = "0123456789101112131415" * 10000
            self.properties = {'big field': 'X' * 32000}

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

    def create_sndr(self):
        self.sender_conn = self.container.connect(self.sender_host)
        self.sender = self.container.create_sender(self.sender_conn,
                                                   self.address)

    def check_address(self):
        local_node = Node.connect(self.check_addr_host, timeout=TIMEOUT)
        outs = local_node.query(type='org.apache.qpid.dispatch.router.address')
        found = False
        self.num_attempts += 1
        for result in outs.results:
            if self.address in result[0]:
                found = True
                self.create_sndr()
                local_node.close()
                self.addr_timer.cancel()
                break

        if not found:

            if self.num_attempts < self.max_attempts:
                self.addr_timer = self.reactor.schedule(1.0, AddrTimer(self))
            else:
                self.error = "Unable to create sender because of " \
                             "absence of address in the address table"
                self.timeout()
                local_node.close()

    def on_start(self, event):
        self.timer = event.reactor.schedule(20.0 if self.large_msg else 10.0,
                                            Timeout(self))
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
                self.reactor = event.reactor
                self.addr_timer = self.reactor.schedule(1.0, AddrTimer(self))

    def on_sendable(self, event):
        while self.n_sent < self.count:
            msg = None
            if self.large_msg:
                msg = Message(body=self.body, properties=self.properties)
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

class MobileAddrMcastDroppedRxTest(MobileAddressMulticastTest):
    # failure scenario - cause some receiving clients to close while a large
    # message is in transit
    def __init__(self, receiver1_host, receiver2_host, receiver3_host,
                 sender_host, address, check_addr_host=None, large_msg=True):
        super(MobileAddrMcastDroppedRxTest, self).__init__(receiver1_host,
                                                           receiver2_host,
                                                           receiver3_host,
                                                           sender_host,
                                                           address,
                                                           check_addr_host=check_addr_host,
                                                           large_msg=large_msg)
        self.n_accepted = 0
        self.n_released = 0
        self.recv1_closed = False
        self.recv2_closed = False

    def _check_done(self):
        if self.n_accepted + self.n_released == self.count:
            self.receiver3_conn.close()
            self.sender_conn.close()
            self.timer.cancel()

    def on_message(self, event):
        super(MobileAddrMcastDroppedRxTest, self).on_message(event)

        # start closing receivers
        if self.n_rcvd1 == 50:
            if not self.recv1_closed:
                self.receiver1_conn.close()
                self.recv1_closed = True
        if self.n_rcvd2 == 75:
            if not self.recv2_closed:
                self.recv2_closed = True
                self.receiver2_conn.close()

    def on_accepted(self, event):
        self.n_accepted += 1
        self._check_done()

    def on_released(self, event):
        self.n_released += 1
        self._check_done()

class MobileAddressEventTest(MessagingHandler):
    def __init__(self, receiver1_host, receiver2_host, receiver3_host,
                 sender_host, interior_host, address):
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

        self.addr_timer = event.reactor.schedule(1.0, AddrTimer(self))

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

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

import os
from time import sleep
from threading import Event
from threading import Timer

from proton import Message, Timeout
from system_test import TestCase, Qdrouterd, main_module, TIMEOUT, MgmtMsgProxy, TestTimeout, PollTimeout
from system_test import AsyncTestReceiver
from system_test import AsyncTestSender
from system_test import Logger
from system_test import QdManager
from system_test import unittest
from system_test import Process
from system_tests_link_routes import ConnLinkRouteService
from test_broker import FakeBroker
from test_broker import FakeService
from proton.handlers import MessagingHandler
from proton.reactor import Container, DynamicNodeProperties
from proton.utils import BlockingConnection
from qpid_dispatch.management.client import Node
from qpid_dispatch_internal.tools.command import version_supports_mutually_exclusive_arguments
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
                ('autoLink', {'address': '0.0.0.0/queue.waypoint', 'containerId': 'ALC', 'direction': 'in'}),
                ('autoLink', {'address': '0.0.0.0/queue.waypoint', 'containerId': 'ALC', 'direction': 'out'}),
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

        # 1 means skip that test.
        cls.skip = { 'test_01' : 0
                   }

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
        if regexp:
            assert re.search(regexp, out,
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
        if self.skip [ 'test_01' ] :
            self.skipTest ( "Test skipped during development." )

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
                ('autoLink', {'address': '0.0.0.0/queue.waypoint', 'containerId': 'ALC', 'direction': 'in'}),
                ('autoLink', {'address': '0.0.0.0/queue.waypoint', 'containerId': 'ALC', 'direction': 'out'}),
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


        cls.skip = { 'test_01' : 0,
                     'test_02' : 0,
                     'test_03' : 0,
                     'test_04' : 0,
                     'test_05' : 0,
                     'test_06' : 0,
                     'test_07' : 0,
                     'test_08' : 0,
                     'test_09' : 0,
                     'test_10' : 0,
                     'test_11' : 0,
                     'test_12' : 0,
                     'test_13' : 0,
                     'test_14' : 0,
                     'test_15' : 0,
                     'test_16' : 0,
                     'test_17' : 0,
                     'test_18' : 0,
                     'test_19' : 0,
                     'test_20' : 0,
                     'test_21' : 0,
                     'test_22' : 0,
                     'test_23' : 0,
                     'test_24' : 0,
                     'test_25' : 0,
                     'test_26' : 0,
                     'test_27' : 0,
                     'test_28' : 0,
                     'test_29' : 0,
                     'test_30' : 0,
                     'test_31' : 0,
                     'test_32' : 0,
                     'test_33' : 0,
                     'test_34' : 0,
                     'test_35' : 0,
                     'test_36' : 0,
                     'test_37' : 0,
                     'test_38' : 0,
                     'test_39' : 0,
                     'test_40' : 0,
                     'test_41' : 0,
                     'test_42' : 0,
                     'test_43':  0,
                     'test_44':  0,
                     'test_45':  0,
                     'test_46':  0,
                     'test_47':  0,
                     'test_48':  0,
                     'test_49':  0,
                     'test_50':  0,
                     'test_51':  0,
                     'test_52':  0,
                     'test_53':  0,
                     'test_54':  0,
                     'test_55':  0,
                     'test_56':  0,
                     'test_57':  0,
                     'test_58':  0,
                     'test_59':  0,
                     'test_60':  0,
                     'test_61':  0,
                     'test_62':  0,
                     'test_63':  0,
                     'test_64':  0,
                     'test_65':  0,
                     'test_66':  0,
                     'test_67':  0,
                     'test_68':  0,
                     'test_69':  0,
                     'test_70':  0,
                     'test_71':  0,
                     'test_72':  0,
                     'test_73':  0
                   }

    def test_01_connectivity_INTA_EA1(self):
        if self.skip [ 'test_01' ] :
            self.skipTest ( "Test skipped during development." )

        test = ConnectivityTest(self.routers[0].addresses[0],
                                self.routers[2].addresses[0],
                                'EA1')
        test.run()
        self.assertEqual(None, test.error)

    def test_02_connectivity_INTA_EA2(self):
        if self.skip [ 'test_02' ] :
            self.skipTest ( "Test skipped during development." )

        test = ConnectivityTest(self.routers[0].addresses[0],
                                self.routers[3].addresses[0],
                                'EA2')
        test.run()
        self.assertEqual(None, test.error)

    def test_03_connectivity_INTB_EB1(self):
        if self.skip [ 'test_03' ] :
            self.skipTest ( "Test skipped during development." )

        test = ConnectivityTest(self.routers[1].addresses[0],
                                self.routers[4].addresses[0],
                                'EB1')
        test.run()
        self.assertEqual(None, test.error)

    def test_04_connectivity_INTB_EB2(self):
        if self.skip [ 'test_04' ] :
            self.skipTest ( "Test skipped during development." )

        test = ConnectivityTest(self.routers[1].addresses[0],
                                self.routers[5].addresses[0],
                                'EB2')
        test.run()
        self.assertEqual(None, test.error)

    def test_05_dynamic_address_same_edge(self):
        if self.skip [ 'test_05' ] :
            self.skipTest ( "Test skipped during development." )

        test = DynamicAddressTest(self.routers[2].addresses[0],
                                  self.routers[2].addresses[0])
        test.run()
        self.assertEqual(None, test.error)

    def test_06_dynamic_address_interior_to_edge(self):
        if self.skip [ 'test_06' ] :
            self.skipTest ( "Test skipped during development." )

        test = DynamicAddressTest(self.routers[2].addresses[0],
                                  self.routers[0].addresses[0])
        test.run()
        self.assertEqual(None, test.error)

    def test_07_dynamic_address_edge_to_interior(self):
        if self.skip [ 'test_07' ] :
            self.skipTest ( "Test skipped during development." )

        test = DynamicAddressTest(self.routers[0].addresses[0],
                                  self.routers[2].addresses[0])
        test.run()
        self.assertEqual(None, test.error)

    def test_08_dynamic_address_edge_to_edge_one_interior(self):
        if self.skip [ 'test_08' ] :
            self.skipTest ( "Test skipped during development." )

        test = DynamicAddressTest(self.routers[2].addresses[0],
                                  self.routers[3].addresses[0])
        test.run()
        self.assertEqual(None, test.error)

    def test_09_dynamic_address_edge_to_edge_two_interior(self):
        if self.skip [ 'test_09' ] :
            self.skipTest ( "Test skipped during development." )

        test = DynamicAddressTest(self.routers[2].addresses[0],
                                  self.routers[4].addresses[0])
        test.run()
        self.assertEqual(None, test.error)

    def test_10_mobile_address_same_edge(self):
        if self.skip [ 'test_10' ] :
            self.skipTest ( "Test skipped during development." )

        test = MobileAddressTest(self.routers[2].addresses[0],
                                 self.routers[2].addresses[0],
                                 "test_10")
        test.run()
        self.assertEqual(None, test.error)

    def test_11_mobile_address_interior_to_edge(self):
        if self.skip [ 'test_11' ] :
            self.skipTest ( "Test skipped during development." )

        test = MobileAddressTest(self.routers[2].addresses[0],
                                 self.routers[0].addresses[0],
                                 "test_11")
        test.run()
        self.assertEqual(None, test.error)

    def test_12_mobile_address_edge_to_interior(self):
        if self.skip [ 'test_12' ] :
            self.skipTest ( "Test skipped during development." )

        test = MobileAddressTest(self.routers[0].addresses[0],
                                 self.routers[2].addresses[0],
                                 "test_12")
        test.run()
        if test.error is not None:
            test.logger.dump()
        self.assertEqual(None, test.error)

    def test_13_mobile_address_edge_to_edge_one_interior(self):
        if self.skip [ 'test_13' ] :
            self.skipTest ( "Test skipped during development." )

        test = MobileAddressTest(self.routers[2].addresses[0],
                                 self.routers[3].addresses[0],
                                 "test_13")
        test.run()
        self.assertEqual(None, test.error)

    def test_14_mobile_address_edge_to_edge_two_interior(self):
        if self.skip [ 'test_14' ] :
            self.skipTest ( "Test skipped during development." )

        test = MobileAddressTest(self.routers[2].addresses[0],
                                 self.routers[4].addresses[0],
                                 "test_14")
        test.run()
        self.assertEqual(None, test.error)

    # One sender two receiver tests.
    # One sender and two receivers on the same edge
    def test_15_mobile_address_same_edge(self):
        if self.skip [ 'test_15' ] :
            self.skipTest ( "Test skipped during development." )

        test = MobileAddressOneSenderTwoReceiversTest(self.routers[2].addresses[0],
                                                      self.routers[2].addresses[0],
                                                      self.routers[2].addresses[0],
                                                      "test_15")
        test.run()
        self.assertEqual(None, test.error)

    # One sender and two receivers on the different edges. The edges are
    #  hanging off the  same interior router.
    def test_16_mobile_address_edge_to_another_edge_same_interior(self):
        if self.skip [ 'test_16' ] :
            self.skipTest ( "Test skipped during development." )

        test = MobileAddressOneSenderTwoReceiversTest(self.routers[2].addresses[0],
                                                      self.routers[2].addresses[0],
                                                      self.routers[3].addresses[0],
                                                      "test_16")
        test.run()
        self.assertEqual(None, test.error)

    # Two receivers on the interior and sender on the edge
    def test_17_mobile_address_edge_to_interior(self):
        if self.skip [ 'test_17' ] :
            self.skipTest ( "Test skipped during development." )

        test = MobileAddressOneSenderTwoReceiversTest(self.routers[0].addresses[0],
                                                      self.routers[0].addresses[0],
                                                      self.routers[2].addresses[0],
                                                      "test_17")
        test.run()
        self.assertEqual(None, test.error)

    # Two receivers on the edge and the sender on the interior
    def test_18_mobile_address_interior_to_edge(self):
        if self.skip [ 'test_18' ] :
            self.skipTest ( "Test skipped during development." )

        test = MobileAddressOneSenderTwoReceiversTest(self.routers[2].addresses[0],
                                                      self.routers[2].addresses[0],
                                                      self.routers[0].addresses[0],
                                                      "test_18")
        test.run()
        self.assertEqual(None, test.error)

    # Two receivers on the edge and the sender on the 'other' interior
    def test_19_mobile_address_other_interior_to_edge(self):
        if self.skip [ 'test_19' ] :
            self.skipTest ( "Test skipped during development." )

        test = MobileAddressOneSenderTwoReceiversTest(self.routers[2].addresses[0],
                                                      self.routers[2].addresses[0],
                                                      self.routers[1].addresses[0],
                                                      "test_19")
        test.run()
        self.assertEqual(None, test.error)

    # Two receivers on the edge and the sender on the edge of
    # the 'other' interior
    def test_20_mobile_address_edge_to_edge_two_interiors(self):
        if self.skip [ 'test_20' ] :
            self.skipTest ( "Test skipped during development." )

        test = MobileAddressOneSenderTwoReceiversTest(self.routers[2].addresses[0],
                                                      self.routers[2].addresses[0],
                                                      self.routers[5].addresses[0],
                                                      "test_20")
        test.run()
        self.assertEqual(None, test.error)

    # One receiver in an edge, another one in interior and the sender
    # is on the edge of another interior
    def test_21_mobile_address_edge_interior_receivers(self):
        if self.skip [ 'test_21' ] :
            self.skipTest ( "Test skipped during development." )

        test = MobileAddressOneSenderTwoReceiversTest(self.routers[4].addresses[0],
                                                      self.routers[1].addresses[0],
                                                      self.routers[2].addresses[0],
                                                      "test_21")
        test.run()
        self.assertEqual(None, test.error)

    # Two receivers one on each interior router and and an edge sender
    # connectoed to the first interior
    def test_22_mobile_address_edge_sender_two_interior_receivers(self):
        if self.skip [ 'test_22' ] :
            self.skipTest ( "Test skipped during development." )

        test = MobileAddressOneSenderTwoReceiversTest(self.routers[0].addresses[0],
                                                      self.routers[1].addresses[0],
                                                      self.routers[3].addresses[0],
                                                      "test_22")
        test.run()
        self.assertEqual(None, test.error)

    def test_23_mobile_address_edge_sender_two_edge_receivers(self):
        if self.skip [ 'test_23' ] :
            self.skipTest ( "Test skipped during development." )

        test = MobileAddressOneSenderTwoReceiversTest(self.routers[4].addresses[0],
                                                      self.routers[5].addresses[0],
                                                      self.routers[2].addresses[0],
                                                      "test_23")
        test.run()
        self.assertEqual(None, test.error)

    # 1 Sender and 3 receivers all on the same edge
    def test_24_multicast_mobile_address_same_edge(self):
        if self.skip [ 'test_24' ] :
            self.skipTest ( "Test skipped during development." )

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
        if self.skip [ 'test_25' ] :
            self.skipTest ( "Test skipped during development." )

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
        if self.skip [ 'test_26' ] :
            self.skipTest ( "Test skipped during development." )

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
        if self.skip [ 'test_27' ] :
            self.skipTest ( "Test skipped during development." )

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
        if self.skip [ 'test_28' ] :
            self.skipTest ( "Test skipped during development." )

        test = MobileAddressMulticastTest(self.routers[2].addresses[0],
                                          self.routers[2].addresses[0],
                                          self.routers[3].addresses[0],
                                          self.routers[1].addresses[0],
                                          "multicast.28")
        test.run()
        self.assertEqual(None, test.error)

    # Sender on an interior and 3 receivers connected to three different edges
    def test_29_multicast_mobile_address_edge_to_edge_two_interiors(self):
        if self.skip [ 'test_29' ] :
            self.skipTest ( "Test skipped during development." )

        test = MobileAddressMulticastTest(self.routers[2].addresses[0],
                                          self.routers[3].addresses[0],
                                          self.routers[4].addresses[0],
                                          self.routers[0].addresses[0],
                                          "multicast.29")
        test.run()
        self.assertEqual(None, test.error)

    def test_30_multicast_mobile_address_all_edges(self):
        if self.skip [ 'test_30' ] :
            self.skipTest ( "Test skipped during development." )

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
        if self.skip [ 'test_31' ] :
            self.skipTest ( "Test skipped during development." )

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
        if self.skip [ 'test_32' ] :
            self.skipTest ( "Test skipped during development." )

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
        if self.skip [ 'test_33' ] :
            self.skipTest ( "Test skipped during development." )

        test = MobileAddressMulticastTest(self.routers[2].addresses[0],
                                          self.routers[3].addresses[0],
                                          self.routers[0].addresses[0],
                                          self.routers[2].addresses[0],
                                          "multicast.33", large_msg=True)
        test.run()
        self.assertEqual(None, test.error)

    # Receivers on the edge and sender on the interior
    def test_34_multicast_mobile_address_interior_to_edge(self):
        if self.skip [ 'test_34' ] :
            self.skipTest ( "Test skipped during development." )

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
        if self.skip [ 'test_35' ] :
            self.skipTest ( "Test skipped during development." )

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
        if self.skip [ 'test_36' ] :
            self.skipTest ( "Test skipped during development." )

        test = MobileAddressMulticastTest(self.routers[2].addresses[0],
                                          self.routers[3].addresses[0],
                                          self.routers[4].addresses[0],
                                          self.routers[0].addresses[0],
                                          "multicast.36", large_msg=True)
        test.run()
        self.assertEqual(None, test.error)

    def test_37_multicast_mobile_address_all_edges(self):
        if self.skip [ 'test_37' ] :
            self.skipTest ( "Test skipped during development." )

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
        if self.skip [ 'test_38' ] :
            self.skipTest ( "Test skipped during development." )

        test = MobileAddressEventTest(self.routers[2].addresses[0],
                                      self.routers[3].addresses[0],
                                      self.routers[3].addresses[0],
                                      self.routers[2].addresses[0],
                                      self.routers[0].addresses[0],
                                      "test_38")

        test.run()
        self.assertEqual(None, test.error)

    def test_39_mobile_addr_event_three_receivers_diff_interior(self):
        if self.skip [ 'test_39' ] :
            self.skipTest ( "Test skipped during development." )

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
        if self.skip [ 'test_40' ] :
            self.skipTest ( "Test skipped during development." )

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
        if self.skip [ 'test_41' ] :
            self.skipTest ( "Test skipped during development." )

        # test what happens if some multicast receivers close in the middle of
        # a multiframe transfer
        test = MobileAddrMcastDroppedRxTest(self.routers[2].addresses[0],
                                            self.routers[2].addresses[0],
                                            self.routers[2].addresses[0],
                                            self.routers[2].addresses[0],
                                            "multicast.40",large_msg=False)
        test.run()
        self.assertEqual(None, test.error)

    def test_42_anon_sender_mobile_address_same_edge(self):
        if self.skip [ 'test_42' ] :
            self.skipTest ( "Test skipped during development." )

        test = MobileAddressAnonymousTest(self.routers[2].addresses[0],
                                          self.routers[2].addresses[0],
                                          "test_42")
        test.run()
        self.assertEqual(None, test.error)

    def test_43_anon_sender_mobile_address_interior_to_edge(self):
        if self.skip [ 'test_43' ] :
            self.skipTest ( "Test skipped during development." )

        test = MobileAddressAnonymousTest(self.routers[2].addresses[0],
                                          self.routers[0].addresses[0],
                                          "test_43")
        test.run()
        self.assertEqual(None, test.error)

    def test_44_anon_sender_mobile_address_edge_to_interior(self):
        if self.skip [ 'test_44' ] :
            self.skipTest ( "Test skipped during development." )

        test = MobileAddressAnonymousTest(self.routers[0].addresses[0],
                                          self.routers[2].addresses[0],
                                          "test_44")
        test.run()
        self.assertEqual(None, test.error)

    def test_45_anon_sender_mobile_address_edge_to_edge_one_interior(self):
        if self.skip [ 'test_45' ] :
            self.skipTest ( "Test skipped during development." )

        test = MobileAddressAnonymousTest(self.routers[2].addresses[0],
                                          self.routers[3].addresses[0],
                                          "test_45")
        test.run()
        self.assertEqual(None, test.error)

    def test_46_anon_sender_mobile_address_edge_to_edge_two_interior(self):
        if self.skip [ 'test_46' ] :
            self.skipTest ( "Test skipped during development." )

        test = MobileAddressAnonymousTest(self.routers[2].addresses[0],
                                          self.routers[4].addresses[0],
                                          "test_46")
        test.run()
        self.assertEqual(None, test.error)

    def test_47_anon_sender_mobile_address_large_msg_same_edge(self):
        if self.skip [ 'test_47' ] :
            self.skipTest ( "Test skipped during development." )

        test = MobileAddressAnonymousTest(self.routers[2].addresses[0],
                                          self.routers[2].addresses[0],
                                          "test_47", True)
        test.run()
        self.assertEqual(None, test.error)

    def test_48_anon_sender_mobile_address_large_msg_interior_to_edge(self):
        if self.skip [ 'test_48' ] :
            self.skipTest ( "Test skipped during development." )

        test = MobileAddressAnonymousTest(self.routers[2].addresses[0],
                                          self.routers[0].addresses[0],
                                          "test_48", True)
        test.run()
        self.assertEqual(None, test.error)

    def test_49_anon_sender_mobile_address_large_msg_edge_to_interior(self):
        if self.skip [ 'test_49' ] :
            self.skipTest ( "Test skipped during development." )

        test = MobileAddressAnonymousTest(self.routers[0].addresses[0],
                                          self.routers[2].addresses[0],
                                          "test_49", True)
        test.run()
        self.assertEqual(None, test.error)

    def test_50_anon_sender_mobile_address_large_msg_edge_to_edge_one_interior(self):
        if self.skip [ 'test_50' ] :
            self.skipTest ( "Test skipped during development." )

        test = MobileAddressAnonymousTest(self.routers[2].addresses[0],
                                          self.routers[3].addresses[0],
                                          "test_50", True)
        test.run()
        self.assertEqual(None, test.error)

    def test_51_anon_sender_mobile_address_large_msg_edge_to_edge_two_interior(self):
        if self.skip [ 'test_51' ] :
            self.skipTest ( "Test skipped during development." )

        test = MobileAddressAnonymousTest(self.routers[2].addresses[0],
                                          self.routers[4].addresses[0],
                                          "test_51", True)
        test.run()
        self.assertEqual(None, test.error)

    # 1 Sender and 3 receivers all on the same edge
    def test_52_anon_sender_multicast_mobile_address_same_edge(self):
        if self.skip [ 'test_52' ] :
            self.skipTest ( "Test skipped during development." )

        test = MobileAddressMulticastTest(self.routers[2].addresses[0],
                                          self.routers[2].addresses[0],
                                          self.routers[2].addresses[0],
                                          self.routers[2].addresses[0],
                                          "multicast.52", anon_sender=True)
        test.run()
        self.assertEqual(None, test.error)

    # 1 Sender and receiver on one edge and 2 receivers on another edge
    # all in the same  interior
    def test_53_anon_sender_multicast_mobile_address_different_edges_same_interior(self):
        if self.skip [ 'test_53' ] :
            self.skipTest ( "Test skipped during development." )

        test = MobileAddressMulticastTest(self.routers[2].addresses[0],
                                          self.routers[2].addresses[0],
                                          self.routers[3].addresses[0],
                                          self.routers[3].addresses[0],
                                          "multicast.53",
                                          self.routers[0].addresses[0],
                                          anon_sender=True)
        test.run()
        self.assertEqual(None, test.error)

    # Two receivers on each edge, one receiver on interior and sender
    # on the edge
    def test_54_anon_sender_multicast_mobile_address_edge_to_interior(self):
        if self.skip [ 'test_54' ] :
            self.skipTest ( "Test skipped during development." )

        test = MobileAddressMulticastTest(self.routers[2].addresses[0],
                                          self.routers[3].addresses[0],
                                          self.routers[0].addresses[0],
                                          self.routers[2].addresses[0],
                                          "multicast.54",
                                          self.routers[0].addresses[0],
                                          anon_sender=True)
        test.run()
        self.assertEqual(None, test.error)

    # Receivers on the edge and sender on the interior
    def test_55_anon_sender_multicast_mobile_address_interior_to_edge(self):
        if self.skip [ 'test_55' ] :
            self.skipTest ( "Test skipped during development." )

        test = MobileAddressMulticastTest(self.routers[2].addresses[0],
                                          self.routers[2].addresses[0],
                                          self.routers[3].addresses[0],
                                          self.routers[0].addresses[0],
                                          "multicast.55",
                                          self.routers[0].addresses[0],
                                          anon_sender=True)
        test.run()
        self.assertEqual(None, test.error)

    # Receivers on the edge and sender on an interior that is not connected
    # to the edges.
    def test_56_anon_sender_multicast_mobile_address_other_interior_to_edge(self):
        if self.skip [ 'test_56' ] :
            self.skipTest ( "Test skipped during development." )

        test = MobileAddressMulticastTest(self.routers[2].addresses[0],
                                          self.routers[2].addresses[0],
                                          self.routers[3].addresses[0],
                                          self.routers[1].addresses[0],
                                          "multicast.56",
                                          anon_sender=True)
        test.run()
        self.assertEqual(None, test.error)

    # Sender on an interior and 3 receivers connected to three different edges
    def test_57_anon_sender_multicast_mobile_address_edge_to_edge_two_interiors(self):
        if self.skip [ 'test_57' ] :
            self.skipTest ( "Test skipped during development." )

        test = MobileAddressMulticastTest(self.routers[2].addresses[0],
                                          self.routers[3].addresses[0],
                                          self.routers[4].addresses[0],
                                          self.routers[0].addresses[0],
                                          "multicast.57",
                                          anon_sender=True)
        test.run()
        self.assertEqual(None, test.error)

    def test_58_anon_sender_multicast_mobile_address_all_edges(self):
        if self.skip [ 'test_58' ] :
            self.skipTest ( "Test skipped during development." )

        test = MobileAddressMulticastTest(self.routers[2].addresses[0],
                                          self.routers[3].addresses[0],
                                          self.routers[4].addresses[0],
                                          self.routers[5].addresses[0],
                                          "multicast.58",
                                          self.routers[0].addresses[0],
                                          anon_sender=True)
        test.run()
        self.assertEqual(None, test.error)


    ######### Multicast Large message anon sender tests ####################

    # 1 Sender and 3 receivers all on the same edge
    def test_59_anon_sender__multicast_mobile_address_same_edge(self):
        if self.skip [ 'test_59' ] :
            self.skipTest ( "Test skipped during development." )

        test = MobileAddressMulticastTest(self.routers[2].addresses[0],
                                          self.routers[2].addresses[0],
                                          self.routers[2].addresses[0],
                                          self.routers[2].addresses[0],
                                          "multicast.59",
                                          large_msg=True,
                                          anon_sender=True)
        test.run()
        self.assertEqual(None, test.error)

    # 1 Sender on one edge and 3 receivers on another edge all in the same
    # interior
    def test_60_anon_sender_multicast_mobile_address_different_edges_same_interior(self):
        if self.skip [ 'test_60' ] :
            self.skipTest ( "Test skipped during development." )

        test = MobileAddressMulticastTest(self.routers[2].addresses[0],
                                          self.routers[2].addresses[0],
                                          self.routers[3].addresses[0],
                                          self.routers[3].addresses[0],
                                          "multicast.60",
                                          self.routers[0].addresses[0],
                                          large_msg=True,
                                          anon_sender=True)
        test.run()
        self.assertEqual(None, test.error)

    # Two receivers on each edge, one receiver on interior and sender
    # on the edge
    def test_61_anon_sender_multicast_mobile_address_edge_to_interior(self):
        if self.skip [ 'test_61' ] :
            self.skipTest ( "Test skipped during development." )

        test = MobileAddressMulticastTest(self.routers[2].addresses[0],
                                          self.routers[3].addresses[0],
                                          self.routers[0].addresses[0],
                                          self.routers[2].addresses[0],
                                          "multicast.61",
                                          self.routers[3].addresses[0],
                                          large_msg=True,
                                          anon_sender=True)
        test.run()
        self.assertEqual(None, test.error)

    # Receivers on the edge and sender on the interior
    def test_62_anon_sender_multicast_mobile_address_interior_to_edge(self):
        if self.skip [ 'test_62' ] :
            self.skipTest ( "Test skipped during development." )

        test = MobileAddressMulticastTest(self.routers[2].addresses[0],
                                          self.routers[2].addresses[0],
                                          self.routers[3].addresses[0],
                                          self.routers[0].addresses[0],
                                          "multicast.62",
                                          large_msg=True,
                                          anon_sender=True)
        test.run()
        self.assertEqual(None, test.error)

    # Receivers on the edge and sender on an interior that is not connected
    # to the edges.
    def test_63_anon_sender_multicast_mobile_address_other_interior_to_edge(self):
        if self.skip [ 'test_63' ] :
            self.skipTest ( "Test skipped during development." )

        test = MobileAddressMulticastTest(self.routers[2].addresses[0],
                                          self.routers[2].addresses[0],
                                          self.routers[3].addresses[0],
                                          self.routers[1].addresses[0],
                                          "multicast.63",
                                          self.routers[0].addresses[0],
                                          large_msg=True,
                                          anon_sender=True)
        test.run()
        self.assertEqual(None, test.error)

    # Sender on an interior and 3 receivers connected to three different edges
    def test_64_anon_sender_multicast_mobile_address_edge_to_edge_two_interiors(self):
        if self.skip [ 'test_64' ] :
            self.skipTest ( "Test skipped during development." )

        test = MobileAddressMulticastTest(self.routers[2].addresses[0],
                                          self.routers[3].addresses[0],
                                          self.routers[4].addresses[0],
                                          self.routers[0].addresses[0],
                                          "multicast.64",
                                          large_msg=True,
                                          anon_sender=True)
        test.run()
        self.assertEqual(None, test.error)

    def test_65_anon_sender_multicast_mobile_address_all_edges(self):
        if self.skip [ 'test_65' ] :
            self.skipTest ( "Test skipped during development." )

        test = MobileAddressMulticastTest(self.routers[2].addresses[0],
                                          self.routers[3].addresses[0],
                                          self.routers[4].addresses[0],
                                          self.routers[5].addresses[0],
                                          "multicast.65",
                                          self.routers[0].addresses[0],
                                          large_msg=True,
                                          anon_sender=True)
        test.run()
        self.assertEqual(None, test.error)


    def test_66_anon_sender_drop_rx_client_multicast_large_message(self):
        # test what happens if some multicast receivers close in the middle of
        # a multiframe transfer. The sender is an anonymous sender.
        if self.skip [ 'test_66' ] :
            self.skipTest ( "Test skipped during development." )

        test = MobileAddrMcastAnonSenderDroppedRxTest(self.routers[2].addresses[0],
                                                      self.routers[2].addresses[0],
                                                      self.routers[2].addresses[0],
                                                      self.routers[2].addresses[0],
                                                      "multicast.66")
        test.run()
        self.assertEqual(None, test.error)

    def test_67_drop_rx_client_multicast_small_message(self):
        # test what happens if some multicast receivers close in the middle of
        # a multiframe transfer. The sender is an anonymous sender.
        if self.skip [ 'test_67' ] :
            self.skipTest ( "Test skipped during development." )

        test = MobileAddrMcastAnonSenderDroppedRxTest(self.routers[2].addresses[0],
                                                      self.routers[2].addresses[0],
                                                      self.routers[2].addresses[0],
                                                      self.routers[2].addresses[0],
                                                      "multicast.67",
                                                      large_msg=False)
        test.run()
        self.assertEqual(None, test.error)

    def run_qdstat(self, args, regexp=None, address=None):
        if args:
            popen_arg = ['qdstat', '--bus', str(address or self.router.addresses[0]),
                 '--timeout', str(TIMEOUT)] + args
        else:
            popen_arg = ['qdstat', '--bus',
                         str(address or self.router.addresses[0]),
                         '--timeout', str(TIMEOUT)]

        p = self.popen(popen_arg,
                       name='qdstat-' + self.id(), stdout=PIPE, expect=None,
            universal_newlines=True)

        out = p.communicate()[0]
        assert p.returncode == 0, \
            "qdstat exit status %s, output:\n%s" % (p.returncode, out)
        if regexp:
            assert re.search(regexp, out,
                                    re.I), "Can't find '%s' in '%s'" % (
        regexp, out)
        return out

    def test_68_edge_qdstat_all_routers(self):
        # Connects to an edge router and runs "qdstat --all-routers"
        # "qdstat --all-routers" is same as "qdstat --all-routers --g"
        # Connecting to an edge router and running "qdstat --all-routers""will only yield the
        # summary statostics of the edge router. It will not show statistics of the interior routers.
        outs = self.run_qdstat(['--all-routers'],
                               address=self.routers[2].addresses[0])
        self.assertTrue("Router Id                        EA1" in outs)

        outs = self.run_qdstat(['--all-routers', '--all-entities'],
                               address=self.routers[2].addresses[0])
        # Check if each entity  section is showing
        self.assertTrue("Router Links" in outs)
        self.assertTrue("Router Addresses" in outs)
        self.assertTrue("Connections" in outs)
        self.assertTrue("AutoLinks" in outs)
        self.assertTrue("Auto Links" in outs)
        self.assertEqual(outs.count("Link Routes"), 2)
        self.assertTrue("Router Statistics" in outs)
        self.assertTrue("Router Id                        EA1" in outs)

        self.assertTrue("Memory Pools" in outs)

        outs = self.run_qdstat(['-c', '--all-routers'],
                               address=self.routers[2].addresses[0])

        # Verify that the the edhe uplink connection is showing
        self.assertTrue("INT.A" in outs)
        self.assertTrue("inter-router" not in outs)

        outs = self.run_qdstat(['--all-entities'],
                               address=self.routers[2].addresses[0])
        # Check if each entity  section is showing
        self.assertTrue("Router Links" in outs)
        self.assertTrue("Router Addresses" in outs)
        self.assertTrue("Connections" in outs)
        self.assertTrue("AutoLinks" in outs)
        self.assertTrue("Auto Links" in outs)
        self.assertEqual(outs.count("Link Routes"), 2)
        self.assertTrue("Router Statistics" in outs)
        self.assertTrue("Router Id                        EA1" in outs)

        self.assertTrue("Memory Pools" in outs)

    def test_69_interior_qdstat_all_routers(self):
        # Connects to an interior router and runs "qdstat --all-routers"
        # "qdstat --all-routers" is same as "qdstat --all-routers --all-entities"
        # Connecting to an interior router and running "qdstat --all-routers""will yield the
        # summary statostics of all the interior routers.
        outs = self.run_qdstat(['--all-routers'],
                               address=self.routers[0].addresses[0])
        self.assertEqual(outs.count("Router Statistics"), 2)

        outs = self.run_qdstat(['--all-routers', '-nv'],
                               address=self.routers[0].addresses[0])
        # 5 occurences including section headers
        self.assertEqual(outs.count("INT.A"), 5)
        self.assertEqual(outs.count("INT.B"), 5)

        outs = self.run_qdstat(['--all-routers', '--all-entities'],
                               address=self.routers[0].addresses[0])
        self.assertEqual(outs.count("Router Links"), 2)
        self.assertEqual(outs.count("Router Addresses"), 2)
        self.assertEqual(outs.count("Connections"), 12)
        self.assertEqual(outs.count("AutoLinks"), 2)
        self.assertEqual(outs.count("Auto Links"), 2)
        self.assertEqual(outs.count("Link Routes"), 4)
        self.assertEqual(outs.count("Router Statistics"), 2)
        self.assertEqual(outs.count("Memory Pools"), 2)

        outs = self.run_qdstat(['--all-routers', '-nv'],
                               address=self.routers[0].addresses[0])
        # 5 occurences including section headers
        self.assertEqual(outs.count("INT.A"), 5)
        self.assertEqual(outs.count("INT.B"), 5)

        outs = self.run_qdstat(['-c', '--all-routers'],
                               address=self.routers[0].addresses[0])
        self.assertEqual(outs.count("INT.A"), 2)
        self.assertEqual(outs.count("INT.B"), 2)

        outs = self.run_qdstat(['-l', '--all-routers'],
                               address=self.routers[0].addresses[0])

        # Two edge-downlinks from each interior to the two edges, 4 in total.
        self.assertEqual(outs.count("edge-downlink"), 4)

        # Gets all entity information of the interior router
        outs = self.run_qdstat(['--all-entities'],
                       address=self.routers[0].addresses[0])
        self.assertEqual(outs.count("Router Links"), 1)
        self.assertEqual(outs.count("Router Addresses"), 1)
        self.assertEqual(outs.count("AutoLinks"), 1)
        self.assertEqual(outs.count("Auto Links"), 1)
        self.assertEqual(outs.count("Router Statistics"), 1)
        self.assertEqual(outs.count("Link Routes"), 2)

        if version_supports_mutually_exclusive_arguments():
            has_error = False
            try:
                # You cannot combine --all-entities  with -c
                outs = self.run_qdstat(['-c', '--all-entities'],
                                   address=self.routers[0].addresses[0])
            except Exception as e:
                if "error: argument --all-entities: not allowed with argument -c/--connections" in str(e):
                    has_error=True

            self.assertTrue(has_error)

            has_error = False
            try:
                outs = self.run_qdstat(['-r', 'INT.A', '--all-routers'],
                                       address=self.routers[0].addresses[0])
            except Exception as e:
                if "error: argument --all-routers: not allowed with argument -r/--router" in str(e):
                    has_error=True

            self.assertTrue(has_error)

    def test_70_qdstat_edge_router_option(self):
        # Tests the --edge-router (-d) option of qdstat
        # The goal of this test is to connect to any router in the
        # network (interior or edge) and ask for details about a specific edge router
        # You could not do that before DISPATCH-1580

        # Makes a connection to an interior router INT.A and runs qdstat
        # asking for all connections of an edge router EA1
        outs = self.run_qdstat(['-d', 'EA1', '-c'],
                               address=self.routers[0].addresses[0])
        parts = outs.split("\n")
        conn_found = False
        for part in parts:
            if "INT.A" in part and "edge" in part and "out" in part:
                conn_found = True
                break

        self.assertTrue(conn_found)

        # Makes a connection to an edge router and runs qdstat
        # asking for all connections of an edge router EA1
        outs = self.run_qdstat(['-d', 'EA1', '-c'],
                               address=self.routers[2].addresses[0])
        parts = outs.split("\n")
        conn_found = False
        for part in parts:
            if "INT.A" in part and "edge" in part and "out" in part:
                conn_found = True
                break

        self.assertTrue(conn_found)

        # Makes a connection to an interior router INT.B and runs qdstat
        # asking for all connections of an edge router EA1. The interior
        # router INT.B is connected to edge router EA1 indirectly via
        # interior router INT.A
        outs = self.run_qdstat(['--edge-router', 'EA1', '-c'],
                               address=self.routers[1].addresses[0])
        parts = outs.split("\n")
        conn_found = False
        for part in parts:
            if "INT.A" in part and "edge" in part and "out" in part:
                conn_found = True
                break

        self.assertTrue(conn_found)

    def test_71_qdmanage_edge_router_option(self):
        # Makes a connection to an interior router INT.A and runs qdstat
        # asking for all connections of an edge router EA1
        mgmt = QdManager(self, address=self.routers[0].addresses[0],
                         edge_router_id='EA1')
        conn_found = False
        outs = mgmt.query('org.apache.qpid.dispatch.connection')
        for out in outs:
            if out['container'] == 'INT.A' and out['dir'] == "out" and out['role'] == "edge":
                conn_found = True
                break
        self.assertTrue(conn_found)

        # Makes a connection to an edge router and runs qdstat
        # asking for all connections of an edge router EA1
        mgmt = QdManager(self, address=self.routers[2].addresses[0],
                         edge_router_id='EA1')
        conn_found = False
        outs = mgmt.query('org.apache.qpid.dispatch.connection')

        for out in outs:
            if out['container'] == 'INT.A' and out['dir'] == "out" and out['role'] == "edge":
                conn_found = True
                break
        self.assertTrue(conn_found)

        # Makes a connection to an interior router INT.B and runs qdstat
        # asking for all connections of an edge router EA1. The interior
        # router INT.B is connected to edge router EA1 indirectly via
        # interior router INT.A
        mgmt = QdManager(self, address=self.routers[1].addresses[0],
                         edge_router_id='EA1')
        conn_found = False
        outs = mgmt.query('org.apache.qpid.dispatch.connection')

        for out in outs:
            if out['container'] == 'INT.A' and out['dir'] == "out" and out['role'] == "edge":
                conn_found = True
                break
        self.assertTrue(conn_found)

    def test_72_qdstat_query_interior_from_edge(self):

        # Connect to Edge Router EA1 and query the connections on
        # Interior Router INT.A
        outs = self.run_qdstat(['-r', 'INT.A', '-c'],
                               address=self.routers[2].addresses[0])

        # The Interior Router INT.A is connected to two edge routers
        # EA1 and EA2 and is also connected to another interior router INT.B
        # We will connect to edge router EA1 (which has an edge
        # uplink to INT.A) and query for connections on INT.A
        ea1_conn_found = False
        ea2_conn_found = False
        int_b_inter_router_conn_found = False
        parts = outs.split("\n")
        for part in parts:
            if "INT.B" in part and "inter-router" in part and "in" in part:
                int_b_inter_router_conn_found = True
            if "EA1" in part and "edge" in part and "in" in part:
                ea1_conn_found = True
            if "EA2" in part and "edge" in part and "in" in part:
                ea2_conn_found = True

        self.assertTrue(ea1_conn_found and ea2_conn_found and int_b_inter_router_conn_found)

        # The Interior Router INT.B is connected  indirectly to edge router
        # EA1 via INT.A
        # We will connect to edge router EA1 (which has an edge
        # uplink to INT.A) and query for connections on INT.B
        outs = self.run_qdstat(['-r', 'INT.B', '-c'],
                               address=self.routers[2].addresses[0])

        eb1_conn_found = False
        eb2_conn_found = False
        int_a_inter_router_conn_found = False
        parts = outs.split("\n")
        for part in parts:
            if "INT.A" in part and "inter-router" in part and "out" in part:
                int_a_inter_router_conn_found = True
            if "EB1" in part and "edge" in part and "in" in part:
                eb1_conn_found = True
            if "EB2" in part and "edge" in part and "in" in part:
                eb2_conn_found = True

        self.assertTrue(eb1_conn_found and eb2_conn_found and int_a_inter_router_conn_found)

    def test_73_qdmanage_query_interior_from_edge(self):
        # The Interior Router INT.A is connected to two edge routers
        # EA1 and EA2 and is also connected to another interior router INT.B
        # We will connect to edge router EA1 (which has an edge
        # uplink to INT.A) and query for connections on INT.A
        mgmt = QdManager(self, address=self.routers[2].addresses[0],
                         router_id='INT.A')
        outs = mgmt.query('org.apache.qpid.dispatch.connection')
        ea1_conn_found = False
        ea2_conn_found = False
        int_b_inter_router_conn_found = False
        for out in outs:
            if out['container'] == "INT.B" and out['role'] == "inter-router" and out['dir'] == "in":
                int_b_inter_router_conn_found = True
            if out['container'] == "EA1" and out['role'] == "edge" and out['dir'] == "in":
                ea1_conn_found = True
            if out['container'] == "EA2" and out['role'] == "edge" and out['dir'] == "in":
                ea2_conn_found = True

        self.assertTrue(ea1_conn_found and ea2_conn_found and int_b_inter_router_conn_found)

        # The Interior Router INT.B is connected  indirectly to edge router
        # EA1 via INT.A
        # We will connect to edge router EA1 (which has an edge
        # uplink to INT.A) and query for connections on INT.B
        mgmt = QdManager(self, address=self.routers[2].addresses[0],
                         router_id='INT.B')
        outs = mgmt.query('org.apache.qpid.dispatch.connection')
        eb1_conn_found = False
        eb2_conn_found = False
        int_a_inter_router_conn_found = False
        for out in outs:
            if out['container'] == "INT.A" and out['role'] == "inter-router" and out['dir'] == "out":
                int_a_inter_router_conn_found = True
            if out['container'] == "EB1" and out['role'] == "edge" and out['dir'] == "in":
                eb1_conn_found = True
            if out['container'] == "EB2" and out['role'] == "edge" and out['dir'] == "in":
                eb2_conn_found = True

        self.assertTrue(int_a_inter_router_conn_found and eb1_conn_found and eb2_conn_found)

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

        cls.skip = { 'test_01' : 0,
                     'test_02' : 0,
                     'test_03' : 0,
                     'test_50' : 0,
                     'test_51' : 0,
                     'test_52' : 0
                   }

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
        if self.skip [ 'test_01' ] :
            self.skipTest ( "Test skipped during development." )

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
                             message=Message(body="HEY HO LET'S GO!"))
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
        if self.skip [ 'test_02' ] :
            self.skipTest ( "Test skipped during development." )


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
        if self.skip [ 'test_03' ] :
            self.skipTest ( "Test skipped during development." )

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
        if self.skip [ 'test_50' ] :
            self.skipTest ( "Test skipped during development." )

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

        bc_a.close()
        bc_b.close()

    def test_51_link_route_proxy_configured(self):
        """
        Activate the configured link routes via a FakeService, verify proxies
        created by passing traffic from/to and interior router
        """
        if self.skip [ 'test_51' ] :
            self.skipTest ( "Test skipped during development." )

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
        if self.skip [ 'test_52' ] :
            self.skipTest ( "Test skipped during development." )

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
        self.timer          = event.reactor.schedule(TIMEOUT, TestTimeout(self))
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
        self.timer         = event.reactor.schedule(TIMEOUT, TestTimeout(self))
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


class CustomTimeout(object):
    def __init__(self, parent):
        self.parent = parent

    def on_timer_task(self, event):
        message = Message(body="Test Message")
        message.address = self.parent.address
        self.parent.sender.send(message)
        self.parent.cancel_custom()


class MobileAddressAnonymousTest(MessagingHandler):
    """
    Attach a receiver to the interior and an anonymous sender to the edge router
    In a non-anonymous sender scenario, the sender will never be given credit
    to send until a receiver on the same address shows up . Since this
    is an anonymous sender, credit is given instatnly and the sender starts
    sending immediately.

    This test will first send 3 messages with a one second interval to make
    sure receiver is available. Then it will fire off 300 messages
    After dispositions are received for the 300 messages, it will close the
    receiver and send 50 more messages. These 50 messages should be released
    or modified.
    """
    def __init__(self, receiver_host, sender_host, address, large_msg=False):
        super(MobileAddressAnonymousTest, self).__init__()
        self.receiver_host = receiver_host
        self.sender_host = sender_host
        self.receiver_conn = None
        self.sender_conn = None
        self.receiver = None
        self.sender = None
        self.error = None
        self.n_sent = 0
        self.n_rcvd = 0
        self.address = address
        self.ready = False
        self.custom_timer = None
        self.num_msgs = 300
        self.extra_msgs = 50
        self.n_accepted = 0
        self.n_modified = 0
        self.n_released = 0
        self.error = None
        self.max_attempts = 3
        self.num_attempts = 0
        self.test_started = False
        self.large_msg = large_msg
        if self.large_msg:
            self.body = "0123456789101112131415" * 10000
            self.properties = {'big field': 'X' * 32000}


    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.receiver_conn = event.container.connect(self.receiver_host)
        self.sender_conn   = event.container.connect(self.sender_host)
        self.receiver      = event.container.create_receiver(self.receiver_conn, self.address)
        # This is an anonymous sender.
        self.sender        = event.container.create_sender(self.sender_conn)

    def cancel_custom(self):
        self.custom_timer.cancel()

    def timeout(self):
        if self.ready:
            self.error = "Timeout Expired - n_sent=%d n_accepted=%d n_modified=%d n_released=%d" % (
            self.n_sent,  self.n_accepted, self.n_modified, self.n_released)
        else:
            self.error = "Did not get a settlement from the receiver. The test cannot be started until " \
                         "a settlement to a test message is received"
        self.receiver_conn.close()
        self.sender_conn.close()

    def on_sendable(self, event):
        if not self.test_started:
            message = Message(body="Test Message")
            message.address = self.address
            self.sender.send(message)
            self.num_attempts += 1
            self.test_started = True

    def on_message(self, event):
        if event.receiver == self.receiver:
            if self.ready:
                self.n_rcvd += 1

    def on_link_closed(self, event):
        # The receiver has closed. We will send messages again and
        # make sure they are released.
        if event.receiver == self.receiver:
            for i in range(self.extra_msgs):
                if self.large_msg:
                    message = Message(body=self.body, properties=self.properties)
                else:
                    message = Message(body="Message %d" % self.n_sent)
                message.address = self.address
                self.sender.send(message)
                self.n_sent += 1

    def on_settled(self, event):
        rdisp = str(event.delivery.remote_state)
        if rdisp == "RELEASED" and not self.ready:
            if self.num_attempts < self.max_attempts:
                self.custom_timer = event.reactor.schedule(1, CustomTimeout(self))
                self.num_attempts += 1
        elif rdisp == "ACCEPTED" and not self.ready:
            self.ready = True
            for i in range(self.num_msgs):
                if self.large_msg:
                    message = Message(body=self.body, properties=self.properties)
                else:
                    message = Message(body="Message %d" % self.n_sent)
                message.address = self.address
                self.sender.send(message)
                self.n_sent += 1
        elif rdisp == "ACCEPTED" and self.ready:
            self.n_accepted += 1
            if self.n_accepted == self.num_msgs:
                # Close the receiver after sending 300 messages
                self.receiver.close()
        elif rdisp == "RELEASED" and self.ready:
            self.n_released += 1
        elif rdisp == "MODIFIED" and self.ready:
            self.n_modified += 1

        if self.num_msgs == self.n_accepted and self.extra_msgs == self.n_released + self.n_modified:
            self.receiver_conn.close()
            self.sender_conn.close()
            self.timer.cancel()

    def run(self):
        Container(self).run()


class MobileAddressTest(MessagingHandler):
    """
    From a single container create a sender and a receiver connection.
    Send a batch of normal messages that should be accepted by the receiver.
    Close the receiver but not the receiver connection and then
      send an extra batch of messages that should be released or modified.
    Success is when message disposition counts add up correctly.
    """
    def __init__(self, receiver_host, sender_host, address):
        super(MobileAddressTest, self).__init__()
        self.receiver_host = receiver_host
        self.sender_host   = sender_host
        self.address       = address

        self.receiver_conn = None
        self.sender_conn   = None

        self.receiver      = None
        self.sender        = None

        self.logger        = Logger()

        self.normal_count  = 300
        self.extra_count   = 50
        self.n_rcvd        = 0
        self.n_sent        = 0
        self.n_accepted    = 0
        self.n_rel_or_mod  = 0
        self.error         = None
        self.warning       = False

    def fail_exit(self, title):
        self.error = title
        self.logger.log("MobileAddressTest result:ERROR: %s" % title)
        self.logger.log("address %s     " % self.address)
        self.logger.log("n_sent       = %d. Expected total:%d normal=%d, extra=%d" % \
            (self.n_sent, (self.normal_count + self.extra_count), self.normal_count, self.extra_count))
        self.logger.log("n_rcvd       = %d. Expected %d" % (self.n_rcvd,       self.normal_count))
        self.logger.log("n_accepted   = %d. Expected %d" % (self.n_accepted,   self.normal_count))
        self.logger.log("n_rel_or_mod = %d. Expected %d" % (self.n_rel_or_mod, self.extra_count))
        self.timer.cancel()
        self.receiver_conn.close()
        self.sender_conn.close()

    def on_timer_task(self, event):
        self.fail_exit("Timeout Expired")

    def on_start(self, event):
        self.logger.log("on_start address=%s" % self.address)
        self.timer         = event.reactor.schedule(TIMEOUT, self)
        self.receiver_conn = event.container.connect(self.receiver_host)
        self.sender_conn   = event.container.connect(self.sender_host)
        self.receiver      = event.container.create_receiver(self.receiver_conn, self.address)
        self.sender        = event.container.create_sender(self.sender_conn, self.address)

    def on_sendable(self, event):
        self.logger.log("on_sendable")
        if event.sender == self.sender:
            self.logger.log("on_sendable sender")
            while self.n_sent < self.normal_count:
                # send the normal messages
                message = Message(body="Message %d" % self.n_sent)
                self.sender.send(message)
                self.logger.log("on_sendable sender: send message %d: %s" % (self.n_sent, message))
                self.n_sent += 1
        elif event.receiver == self.receiver:
            self.logger.log("on_sendable receiver: WARNING unexpected callback for receiver")
            self.warning = True
        else:
            self.fail_exit("on_sendable not for sender nor for receiver")

    def on_message(self, event):
        self.logger.log("on_message")
        if event.receiver == self.receiver:
            self.n_rcvd += 1
            self.logger.log("on_message receiver: receiver message %d" % (self.n_rcvd))
        else:
            self.logger.log("on_message: WARNING callback not for test receiver.")

    def on_settled(self, event):
        # Expect all settlement events at sender as remote state
        self.logger.log("on_settled")
        rdisp = str(event.delivery.remote_state)
        ldisp = str(event.delivery.local_state)
        if event.sender == self.sender:
            if rdisp is None:
                self.logger.log("on_settled: WARNING: sender remote delivery state is None. Local state = %s." % ldisp)
            elif rdisp == "ACCEPTED":
                self.n_accepted += 1
                self.logger.log("on_settled sender: ACCEPTED %d (of %d)" %
                                (self.n_accepted, self.normal_count))
            elif rdisp == "RELEASED" or rdisp == "MODIFIED":
                self.n_rel_or_mod += 1
                self.logger.log("on_settled sender: %s %d (of %d)" %
                                (rdisp, self.n_rel_or_mod, self.extra_count))
            else:
                self.logger.log("on_settled sender: WARNING unexpected settlement: %s, n_accepted: %d, n_rel_or_mod: %d" %
                    (rdisp, self.n_accepted, self.n_rel_or_mod))
                self.warning = True

            if self.n_sent == self.normal_count and self.n_accepted == self.normal_count:
                # All normal messages are accounted.
                # Close receiver and launch extra messages into the router network.
                self.logger.log("on_settled sender: normal messages all accounted. receiver.close() then send extra messages")
                self.receiver.close()
                for i in range(self.extra_count):
                    message = Message(body="Message %d" % self.n_sent)
                    self.sender.send(message)
                    # Messages must be blasted to get them into the network before news
                    # of the receiver closure is propagated back to EA1.
                    # self.logger.log("on_settled sender: send extra message %d: %s" % (self.n_sent, message))
                    self.n_sent += 1

            if self.n_accepted > self.normal_count:
                self.fail_exit("Too many messages were accepted")
            if self.n_rel_or_mod > self.extra_count:
                self.fail_exit("Too many messages were released or modified")

            if self.n_rel_or_mod == self.extra_count:
                # All extra messages are accounted. Exit with success.
                result = "SUCCESS" if not self.warning else "WARNING"
                self.logger.log("MobileAddressTest result:%s" % result)
                self.timer.cancel()
                self.receiver_conn.close()
                self.sender_conn.close()

        elif event.receiver == self.receiver:
            self.logger.log("on_settled receiver: WARNING unexpected on_settled. remote: %s, local: %s" % (rdisp, ldisp))
            self.warning = True

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
        self.timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))

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
                 sender_host, address, check_addr_host=None, large_msg=False,
                 anon_sender=False):
        super(MobileAddressMulticastTest, self).__init__()
        self.receiver1_host = receiver1_host
        self.receiver2_host = receiver2_host
        self.receiver3_host = receiver3_host
        self.sender_host = sender_host
        self.address = address
        self.anon_sender = anon_sender

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
        if self.anon_sender:
            self.sender = self.container.create_sender(self.sender_conn)
        else:
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
        self.timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))
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
            if self.anon_sender:
                msg.address = self.address
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


class MobileAddrMcastAnonSenderDroppedRxTest(MobileAddressMulticastTest):
    # failure scenario - cause some receiving clients to close while a large
    # message is in transit
    def __init__(self, receiver1_host, receiver2_host, receiver3_host,
                 sender_host, address, check_addr_host=None, large_msg=True, anon_sender=True):
        super(MobileAddrMcastAnonSenderDroppedRxTest, self).__init__(receiver1_host,
                                                                     receiver2_host,
                                                                     receiver3_host,
                                                                     sender_host,
                                                                     address,
                                                                     check_addr_host=check_addr_host,
                                                                     large_msg=large_msg,
                                                                     anon_sender=anon_sender)
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
        super(MobileAddrMcastAnonSenderDroppedRxTest, self).on_message(event)

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
        self.timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))

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

class EdgeListenerSender(TestCase):

    inter_router_port = None

    @classmethod
    def setUpClass(cls):
        super(EdgeListenerSender, cls).setUpClass()

        def router(name, mode, connection, extra=None):
            config = [
                ('router', {'mode': mode, 'id': name}),
                ('address',
                 {'prefix': 'multicast', 'distribution': 'multicast'}),
                connection
            ]

            config = Qdrouterd.Config(config)
            cls.routers.append(cls.tester.qdrouterd(name, config, wait=True))

        cls.routers = []

        edge_port_A = cls.tester.get_port()
        router('INT.A', 'interior',  ('listener', {'role': 'edge', 'port': edge_port_A}))
        cls.routers[0].wait_ports()

    # Without the fix for DISPATCH-1492, this test will fail because
    # of the router crash.
    def test_edge_listener_sender_crash_DISPATCH_1492(self):
        addr = self.routers[0].addresses[0]
        blocking_connection = BlockingConnection(addr)
        blocking_sender = blocking_connection.create_sender(address="multicast")
        self.assertTrue(blocking_sender!=None)


class StreamingMessageTest(TestCase):
    """
    Test streaming message flows across edge and interior routers
    """

    SIG_TERM = -15  # Process.terminate() sets this exit value
    BODY_MAX = 4294967295  # AMQP 1.0 allows types of length 2^32-1

    @classmethod
    def setUpClass(cls):
        """Start a router"""
        super(StreamingMessageTest, cls).setUpClass()

        def router(name, mode, extra):
            config = [
                ('router', {'mode': mode, 'id': name}),
                ('listener', {'role': 'normal',
                              'port': cls.tester.get_port(),
                              'maxFrameSize': 65535}),

                ('address', {'prefix': 'closest',   'distribution': 'closest'}),
                ('address', {'prefix': 'balanced',  'distribution': 'balanced'}),
                ('address', {'prefix': 'multicast', 'distribution': 'multicast'})
            ]

            if extra:
                config.extend(extra)
            config = Qdrouterd.Config(config)
            cls.routers.append(cls.tester.qdrouterd(name, config, wait=False))
            return cls.routers[-1]

        # configuration:
        # two edge routers connected via 2 interior routers.
        # fake broker (route-container) on EB1
        #
        #  +-------+    +---------+    +---------+    +-------+
        #  |  EA1  |<==>|  INT.A  |<==>|  INT.B  |<==>|  EB1  |<-- Fake Broker
        #  +-------+    +---------+    +---------+    +-------+
        #

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
                               'port': cls.INTA_edge_port})
               ])
        cls.EA1 = cls.routers[2]
        cls.EA1.listener = cls.EA1.addresses[0]

        router('EB1', 'edge',
               [('connector', {'name': 'uplink', 'role': 'edge',
                               'port': cls.INTB_edge_port}),
                # to connect to the fake broker
                ('connector', {'name': 'broker',
                               'role': 'route-container',
                               'host': '127.0.0.1',
                               'port': cls.tester.get_port(),
                               'saslMechanisms': 'ANONYMOUS'}),
                ('linkRoute', {'pattern': 'MyLinkRoute.#', 'containerId':
                               'FakeBroker', 'direction': 'in'}),
                ('linkRoute', {'pattern': 'MyLinkRoute.#', 'containerId':
                               'FakeBroker', 'direction': 'out'})
               ])
        cls.EB1 = cls.routers[3]
        cls.EB1.listener = cls.EB1.addresses[0]
        cls.EB1.route_container = cls.EB1.connector_addresses[1];

        cls.INT_A.wait_router_connected('INT.B')
        cls.INT_B.wait_router_connected('INT.A')
        cls.EA1.wait_connectors()

        cls._container_index = 0

        cls.skip = { 'test_01' : 0,
                     'test_02' : 0,
                     'test_03' : 0,
                     'test_50' : 0,
                     'test_51' : 0,
                     'test_52' : 0
                   }

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

    def _start_broker_EB1(self):
        # start a new broker on EB1
        fake_broker = FakeBroker(self.EB1.route_container)
        # wait until the link route appears on the interior routers
        self.INT_B.wait_address("MyLinkRoute")
        self.INT_A.wait_address("MyLinkRoute")
        return fake_broker

    def spawn_receiver(self, router, count, address, expect=None):
        if expect is None:
            expect = Process.EXIT_OK
        cmd = ["test-receiver",
               "-i", "TestReceiver-%d" % self._container_index,
               "-a", router.listener,
               "-c", str(count),
               "-s", address]
        self._container_index += 1
        env = dict(os.environ, PN_TRACE_FRM="1")
        return self.popen(cmd, expect=expect, env=env)

    def spawn_sender(self, router, count, address, expect=None, size=None):
        if expect is None:
            expect = Process.EXIT_OK
        if size is None:
            size = "-sm"
        cmd = ["test-sender",
               "-i", "TestSender-%d" % self._container_index,
               "-a", router.listener,
               "-c", str(count),
               "-t", address,
               size]
        self._container_index += 1
        env = dict(os.environ, PN_TRACE_FRM="1")
        return self.popen(cmd, expect=expect, env=env)

    def spawn_clogger(self, router, count, address,
                      size, pause_ms, expect=None):
        if expect is None:
            expect = Process.EXIT_OK
        cmd = ["clogger",
               "-a", router.listener,
               "-c", str(count),
               "-t", address,
               "-s", str(size),
               "-D",
               "-P", str(pause_ms)]
        env = dict(os.environ, PN_TRACE_FRM="1")
        return self.popen(cmd, expect=expect, env=env)

    def test_01_streaming_link_route(self):
        """
        Verify that a streaming message can be delivered over a link route
        """

        fake_broker = self._start_broker_EB1()

        rx = self.spawn_receiver(self.EB1, count=1,
                                 address="MyLinkRoute/test-address")

        # sender a streaming message, "-sx" causes the sender to generate a
        # large streaming message
        tx = self.spawn_sender(self.EA1, count=1,
                               address="MyLinkRoute/test-address",
                               expect=Process.EXIT_OK,
                               size="-sx")

        out_text, out_error = tx.communicate(timeout=TIMEOUT)
        if tx.returncode:
            raise Exception("Sender failed: %s %s" % (out_text, out_error))

        out_text, out_error = rx.communicate(timeout=TIMEOUT)
        if rx.returncode:
            raise Exception("Receiver failed: %s %s" % (out_text, out_error))

        fake_broker.join()
        self.assertEqual(1, fake_broker.in_count)
        self.assertEqual(1, fake_broker.out_count)

        # cleanup - not EB1 since MyLinkRoute is configured
        self._wait_address_gone(self.EA1, "MyLinkRoute")
        self._wait_address_gone(self.INT_A, "MyLinkRoute")
        self._wait_address_gone(self.INT_B, "MyLinkRoute")

    def _streaming_test(self, address):

        # send a streaming message to address across the routers
        rx = self.spawn_receiver(self.EB1,
                                 count=1,
                                 address=address)
        self.INT_A.wait_address(address)

        tx = self.spawn_sender(self.EA1,
                               count=1,
                               address=address,
                               expect=Process.EXIT_OK,
                               size="-sx")
        out_text, out_error = tx.communicate(timeout=TIMEOUT)
        if tx.returncode:
            raise Exception("Sender failed: %s %s" % (out_text, out_error))

        out_text, out_error = rx.communicate(timeout=TIMEOUT)
        if rx.returncode:
            raise Exception("receiver failed: %s %s" % (out_text, out_error))

        self._wait_address_gone(self.INT_B, address)
        self._wait_address_gone(self.INT_A, address)
        self._wait_address_gone(self.EA1, address)
        self._wait_address_gone(self.EB1, address)

    def test_02_streaming_closest(self):
        """
        Verify that a streaming message with closest treatment is forwarded
        correctly.
        """
        self._streaming_test("closest/test-address")

    def test_03_streaming_multicast(self):
        """
        Verify a streaming multicast message is forwarded correctly
        """

        routers = [self.EB1, self.INT_B, self.INT_A]
        streaming_rx = [self.spawn_receiver(router,
                                            count=1,
                                            address="multicast/test-address")
                        for router in routers]
        self.EB1.wait_address("multicast/test-address", subscribers=1)
        self.INT_B.wait_address("multicast/test-address", subscribers=2, remotes=1)
        self.INT_A.wait_address("multicast/test-address", subscribers=1, remotes=1)

        # This sender will end up multicasting the message to ALL receivers.
        tx = self.spawn_sender(self.EA1,
                               count=1,
                               address="multicast/test-address",
                               expect=Process.EXIT_OK,
                               size="-sx")

        out_text, out_error = tx.communicate(timeout=TIMEOUT)
        if tx.returncode:
            raise Exception("sender failed: %s %s" % (out_text, out_error))

        for rx in streaming_rx:
            out_text, out_error = rx.communicate(timeout=TIMEOUT)
            if rx.returncode:
                raise Exception("receiver failed: %s %s" % (out_text, out_error))

        self._wait_address_gone(self.EA1, "multicast/test_address")
        self._wait_address_gone(self.EB1, "multicast/test_address")
        self._wait_address_gone(self.INT_A, "multicast/test_address")
        self._wait_address_gone(self.INT_B, "multicast/test_address")

    def test_04_streaming_balanced(self):
        """
        Verify streaming balanced messages are forwarded correctly.
        """
        balanced_rx = [self.spawn_receiver(self.EB1,
                                           count=1,
                                           address="balanced/test-address")
                       for _ in range(2)]
        self.EB1.wait_address("balanced/test-address", subscribers=2)

        tx = self.spawn_sender(self.EA1,
                               count=2,
                               address="balanced/test-address")
        out_text, out_error = tx.communicate(timeout=TIMEOUT)
        if tx.returncode:
            raise Exception("sender failed: %s %s" % (out_text, out_error))

        for rx in balanced_rx:
            out_text, out_error = rx.communicate(timeout=TIMEOUT)
            if rx.returncode:
                raise Exception("receiver failed: %s %s" % (out_text, out_error))

        self._wait_address_gone(self.EA1, "balanced/test-address")
        self._wait_address_gone(self.EB1,  "balanced/test-address")
        self._wait_address_gone(self.INT_A,  "balanced/test-address")
        self._wait_address_gone(self.INT_B,  "balanced/test-address")

    def test_10_streaming_link_route_parallel(self):
        """
        Ensure that a streaming message sent across a link route does not block other
        clients sending to the same container address.
        """

        fake_broker = self._start_broker_EB1()

        clogger = self.spawn_clogger(self.EA1,
                                     count=1,
                                     address="MyLinkRoute/clogger",
                                     size=self.BODY_MAX,
                                     pause_ms=100,
                                     expect=self.SIG_TERM)
        sleep(0.5)  # allow clogger to set up streaming links

        # start a sender in parallel
        tx = self.spawn_sender(self.EA1, count=100, address="MyLinkRoute/clogger")
        out_text, out_error = tx.communicate(timeout=TIMEOUT)
        if tx.returncode:
            raise Exception("Sender failed: %s %s" % (out_text, out_error))

        clogger.terminate()
        clogger.wait()

        fake_broker.join()
        self.assertEqual(100, fake_broker.in_count)

        # cleanup - not EB1 since MyLinkRoute is configured
        self._wait_address_gone(self.EA1, "MyLinkRoute")
        self._wait_address_gone(self.INT_A, "MyLinkRoute")
        self._wait_address_gone(self.INT_B, "MyLinkRoute")

    def test_11_streaming_closest_parallel(self):
        """
        Ensure that a streaming message of closest treatment does not block
        other non-streaming messages.
        """

        # this receiver should get the streaming message
        rx1 = self.spawn_receiver(self.EB1,
                                  count=1,
                                  address="closest/test-address",
                                  expect=self.SIG_TERM)

        self.INT_A.wait_address("closest/test-address");

        clogger = self.spawn_clogger(self.EA1,
                                     count=1,
                                     address="closest/test-address",
                                     size=self.BODY_MAX,
                                     pause_ms=100,
                                     expect=self.SIG_TERM)
        sleep(0.5)

        # this receiver has less cost than rx1 since it is 1 less hop from the
        # sender
        rx2 = self.spawn_receiver(self.INT_A,
                                  count=1,
                                  address="closest/test-address")

        # wait for rx2 to set up links to INT_A:
        self.INT_A.wait_address("closest/test-address", subscribers=1, remotes=1)

        # start a sender in parallel. Expect the message to arrive at rx1
        tx = self.spawn_sender(self.EA1, count=1, address="closest/test-address")
        out_text, out_error = tx.communicate(timeout=TIMEOUT)
        if tx.returncode:
            raise Exception("Sender failed: %s %s" % (out_text, out_error))

        out_text, out_error = rx2.communicate(timeout=TIMEOUT)
        if rx2.returncode:
            raise Exception("receiver failed: %s %s" % (out_text, out_error))

        rx1.terminate()
        rx1.wait()

        clogger.terminate()
        clogger.wait()

        self._wait_address_gone(self.EA1, "closest/test-address")
        self._wait_address_gone(self.EB1,  "closest/test-address")
        self._wait_address_gone(self.INT_A,  "closest/test-address")
        self._wait_address_gone(self.INT_B,  "closest/test-address")

    def test_12_streaming_multicast_parallel(self):
        """
        Verify a streaming multicast message does not block other non-streaming
        multicast messages

        Start a group of receivers to consume the streaming message.  Then
        start a separate group to consume the non-streaming message.  Ensure
        that the second group properly receives the non-streaming message.
        """

        routers = [self.EB1, self.INT_A, self.INT_B]
        streaming_rx = [self.spawn_receiver(router,
                                            count=1,
                                            address="multicast/test-address",
                                            expect=self.SIG_TERM)
                        for router in routers]

        self.EB1.wait_address("multicast/test-address", subscribers=1)
        self.INT_B.wait_address("multicast/test-address", subscribers=2, remotes=1)
        self.INT_A.wait_address("multicast/test-address", subscribers=1, remotes=1)

        # this will block all of the above receivers with a streaming message

        clogger = self.spawn_clogger(self.EA1,
                                     count=1,
                                     address="multicast/test-address",
                                     size=self.BODY_MAX,
                                     pause_ms=100,
                                     expect=self.SIG_TERM)
        sleep(0.5)

        # this second set of receivers should be able to receive multicast
        # messages sent _after_ the clogger's streaming message

        blocking_rx = [self.spawn_receiver(router,
                                           count=1,
                                           address="multicast/test-address")
                       for router in routers]

        self.EB1.wait_address("multicast/test-address", subscribers=2)
        self.INT_B.wait_address("multicast/test-address", subscribers=3, remotes=1)
        self.INT_A.wait_address("multicast/test-address", subscribers=2, remotes=1)

        # This sender will end up multicasting the message to ALL receivers.
        # Expect it to block since the first set of receivers will never get
        # around to acking the message
        tx = self.spawn_sender(self.EA1,
                               count=1,
                               address="multicast/test-address",
                               expect=self.SIG_TERM)

        # however the second set of receivers _should_ end up getting the
        # message, acking it and exit (count=1)
        for rx in blocking_rx:
            out_text, out_error = rx.communicate(timeout=TIMEOUT)
            if rx.returncode:
                raise Exception("receiver failed: %s %s" % (out_text, out_error))

        tx.terminate()
        tx.wait()

        for rx in streaming_rx:
            rx.terminate()
            rx.wait()

        clogger.terminate()
        clogger.wait()

        self._wait_address_gone(self.EA1, "multicast/test-address")
        self._wait_address_gone(self.EB1,  "multicast/test-address")
        self._wait_address_gone(self.INT_A,  "multicast/test-address")
        self._wait_address_gone(self.INT_B,  "multicast/test-address")

    def test_13_streaming_balanced_parallel(self):
        """
        Verify streaming does not block other balanced traffic.
        """

        # create 2 consumers on the balanced address. Since our Process class
        # requires the exit code to be known when the process is spawned and we
        # cannot predict which receiver will get the streaming message use
        # count=2 to force the receivers to run until we force termination
        balanced_rx = [self.spawn_receiver(self.EB1,
                                           count=2,
                                           address="balanced/test-address",
                                           expect=self.SIG_TERM)
                       for _ in range(2)]
        self.EB1.wait_address("balanced/test-address", subscribers=2)

        # this will block one of the above receivers with a streaming message

        clogger = self.spawn_clogger(self.EA1,
                                     count=1,
                                     address="balanced/test-address",
                                     size=self.BODY_MAX,
                                     pause_ms=100,
                                     expect=self.SIG_TERM)
        sleep(0.5)

        # This sender should get its message through to the other receiver
        tx = self.spawn_sender(self.EA1,
                               count=1,
                               address="balanced/test-address")
        out_text, out_error = tx.communicate(timeout=TIMEOUT)
        if tx.returncode:
            raise Exception("sender failed: %s %s" % (out_text, out_error))

        for rx in balanced_rx:
            rx.terminate()
            rx.wait()

        clogger.terminate()
        clogger.wait()

        self._wait_address_gone(self.EA1, "balanced/test-address")
        self._wait_address_gone(self.EB1,  "balanced/test-address")
        self._wait_address_gone(self.INT_A,  "balanced/test-address")
        self._wait_address_gone(self.INT_B,  "balanced/test-address")


if __name__== '__main__':
    unittest.main(main_module())

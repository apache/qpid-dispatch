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

import json
import os
from subprocess import PIPE, STDOUT
from time import sleep

from proton import Condition, Data, Delivery, Message, Terminus, Timeout, Url, symbol
from proton import VERSION as PROTON_VERSION
from proton.handlers import MessagingHandler, TransactionHandler
from proton.utils import BlockingConnection, SyncRequestResponse
from proton.reactor import Container, AtMostOnce, AtLeastOnce

from qpid_dispatch.management.client import Node

from system_test import TestCase, Qdrouterd, main_module, TIMEOUT, DIR, Process, unittest, QdManager, TestTimeout
from test_broker import FakeBroker


CONNECTION_PROPERTIES_UNICODE_STRING = {'connection': 'properties', 'int_property': 6451}
CONNECTION_PROPERTIES_SYMBOL = dict()
CONNECTION_PROPERTIES_SYMBOL[symbol("connection")] = symbol("properties")
CONNECTION_PROPERTIES_BINARY = {b'client_identifier': b'policy_server'}


class StandaloneRouterQdManageTest(TestCase):
    @classmethod
    def setUpClass(cls):
        super(StandaloneRouterQdManageTest, cls).setUpClass()
        name = "test-router"
        config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'QDR'}),
            ('listener', {'port': cls.tester.get_port(), 'role': 'normal', 'host': '0.0.0.0'})
        ])
        cls.router = cls.tester.qdrouterd(name, config, wait=True)

    def test_49_add_interrouter_connector_to_standalone_router(self):
        """
        This test tries adding an inter-router connector to a stanalone router.
        A standalone router can have a route container connector
        but never an inter-router connector. Inter router connectors
        are allowed only with interior routers.
        """
        mgmt = QdManager(address=self.router.addresses[0])
        test_pass = False
        try:
            out = mgmt.create("org.apache.qpid.dispatch.connector",
                              {"host": "0.0.0.0",
                               "port": "77777",
                               "role": "inter-router"})
        except Exception as e:
            if "BadRequestStatus: role='standalone' not allowed to connect to or accept connections from other routers." in str(e):
                test_pass = True

        self.assertTrue(test_pass)

    def test_50_add_edge_listener_to_standalone_router(self):
        """
        This test tries to add an edge listener to a standalone router.
        Since this is a standalone router, other routers (interior or edge routers)
        cannot connect to this router.
        """
        mgmt = QdManager(address=self.router.addresses[0])
        test_pass = False
        try:
            out = mgmt.create("org.apache.qpid.dispatch.listener",
                              {"host": "0.0.0.0",
                               "port": "77777",
                               "role": "edge",
                               "authenticatePeer": "no"})
        except Exception as e:
            if "BadRequestStatus: role='standalone' not allowed to connect to or accept connections from other routers." in str(e):
                test_pass = True

        self.assertTrue(test_pass)

    def test_51_add_interrouter_listener_to_standalone_router(self):
        """
        This test tries to add an inter-router listener to a standalone router.
        Since this is a standalone router, other routers (interior or edge routers)
        cannot connect to this router.
        """
        mgmt = QdManager(address=self.router.addresses[0])
        test_pass = False
        try:
            out = mgmt.create("org.apache.qpid.dispatch.listener",
                              {"host": "0.0.0.0",
                               "port": "77777",
                               "role": "inter-router",
                               "authenticatePeer": "no"})
        except Exception as e:
            if "BadRequestStatus: role='standalone' not allowed to connect to or accept connections from other routers." in str(e):
                test_pass = True

        self.assertTrue(test_pass)


class EdgeRouterQdManageTest(TestCase):
    @classmethod
    def setUpClass(cls):
        super(EdgeRouterQdManageTest, cls).setUpClass()
        name = "test-router"
        config = Qdrouterd.Config([
            ('router', {'mode': 'edge', 'id': 'QDR'}),
            ('listener', {'port': cls.tester.get_port(), 'role': 'normal', 'host': '0.0.0.0'})
        ])
        cls.router = cls.tester.qdrouterd(name, config, wait=True)

    def test_52_add_interrouter_connector_to_edge_router(self):
        """
        This test tries adding an inter-router connector to an edge router. An edge
        router can have an edge connector or route container connector
        but never an inter-router connector. Inter router connectors
        are allowed only with interior routers.
        """
        mgmt = QdManager(address=self.router.addresses[0])
        test_pass = False
        try:
            out = mgmt.create("org.apache.qpid.dispatch.connector",
                              {"host": "0.0.0.0",
                               "port": "77777",
                               "role": "inter-router"})
        except Exception as e:
            if "BadRequestStatus: role='inter-router' only allowed with router mode='interior'" in str(e):
                test_pass = True

        self.assertTrue(test_pass)

    def test_53_add_edge_listener_to_edge_router(self):
        """
        This test tries to add an edge listener to an edge router which means
        an edge router can connect to another edge router and that is not
        allowed.
        """
        mgmt = QdManager(address=self.router.addresses[0])
        test_pass = False
        try:
            out = mgmt.create("org.apache.qpid.dispatch.listener",
                              {"host": "0.0.0.0",
                               "port": "77777",
                               "role": "edge",
                               "authenticatePeer": "no"})
        except Exception as e:
            if "BadRequestStatus: role='edge' only allowed with router mode='interior'" in str(e):
                test_pass = True

        self.assertTrue(test_pass)

    def test_54_add_interrouter_listener_to_edge_router(self):
        """
        This test tries to add an edge listener to an edge router which means
        an edge router can connect to another edge router and that is not
        allowed.
        """
        mgmt = QdManager(address=self.router.addresses[0])
        test_pass = False
        try:
            out = mgmt.create("org.apache.qpid.dispatch.listener",
                              {"host": "0.0.0.0",
                               "port": "77777",
                               "role": "inter-router",
                               "authenticatePeer": "no"})
        except Exception as e:
            if "BadRequestStatus: role='inter-router' only allowed with router mode='interior'" in str(e):
                test_pass = True

        self.assertTrue(test_pass)


class StandaloneEdgeRouterConfigTest(TestCase):
    """
    Try to start the router with bad config and make sure the router
    does not start and scan the log files for appropriate error messages.
    """
    @classmethod
    def setUpClass(cls):
        super(StandaloneEdgeRouterConfigTest, cls).setUpClass()
        name = "test-router"

        # A standalone router cannot have an edge listener because it cannot accept edge connections.
        config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'QDR'}),
            ('listener', {'port': cls.tester.get_port(), 'role': 'edge', 'host': '0.0.0.0'})
        ])
        cls.router = cls.tester.qdrouterd(name, config, wait=False, perform_teardown=False)

        # A standalone router cannot have inter-router connectors.
        name = "test-router-1"
        config_1 = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'QDR'}),
            ('connector', {'port': cls.tester.get_port(), 'role': 'inter-router', 'host': '0.0.0.0'})
        ])
        cls.router_1 = cls.tester.qdrouterd(name, config_1, wait=False, perform_teardown=False)

        # An edge router cannot have edge listeners.
        # Edge routers can have connectors that connect to interior routers
        # or route-containers. One edge router cannot connect to another edge router.
        name = "test-router-2"
        config_2 = Qdrouterd.Config([
            ('router', {'mode': 'edge', 'id': 'QDR'}),
            ('listener', {'port': cls.tester.get_port(), 'role': 'edge', 'host': '0.0.0.0'})
        ])
        cls.router_2 = cls.tester.qdrouterd(name, config_2, wait=False, perform_teardown=False)

        # Edge routers cannot have inter-router listeners. Only interior
        # routers can have inter-router listeners.
        name = "test-router-3"
        config_3 = Qdrouterd.Config([
            ('router', {'mode': 'edge', 'id': 'QDR'}),
            ('listener', {'port': cls.tester.get_port(), 'role': 'inter-router', 'host': '0.0.0.0'})
        ])
        cls.router_3 = cls.tester.qdrouterd(name, config_3, wait=False, perform_teardown=False)

        # Edge routers cannot have inter-router connectors
        # Inter-router connectors are allowed only on interior routers.
        name = "test-router-4"
        config_4 = Qdrouterd.Config([
            ('router', {'mode': 'edge', 'id': 'QDR'}),
            ('connector', {'port': cls.tester.get_port(), 'role': 'inter-router', 'host': '0.0.0.0'})
        ])
        cls.router_4 = cls.tester.qdrouterd(name, config_4, wait=False, perform_teardown=False)

        # A standalone router cannot have an inter-router listener because
        # it cannot accept inter-router connections.
        name = "test-router-5"
        config_5 = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'QDR'}),
            ('listener', {'port': cls.tester.get_port(), 'role': 'inter-router', 'host': '0.0.0.0'})
        ])
        cls.router_5 = cls.tester.qdrouterd(name, config_5, wait=False, perform_teardown=False)

        # Give some time for the test to write to the .out file. Without
        # this sleep, the tests execute too
        # fast and find that nothing has yet been written to the .out files.
        sleep(3)

    def test_48_router_in_error(self):
        test_pass = False
        with open(self.router.outfile + '.out', 'r') as out_file:
            for line in out_file:
                if "Exception: Cannot load configuration file test-router.conf: role='standalone' not allowed to connect to or accept connections from other routers." in line:
                    test_pass = True
                    break
        self.assertTrue(test_pass)

        test_pass = False
        with open(self.router_1.outfile + '.out', 'r') as out_file:
            for line in out_file:
                if "Exception: Cannot load configuration file test-router-1.conf: role='standalone' not allowed to connect to or accept connections from other routers." in line:
                    test_pass = True
                    break
        self.assertTrue(test_pass)

        test_pass = False
        with open(self.router_2.outfile + '.out', 'r') as out_file:
            for line in out_file:
                if "Exception: Cannot load configuration file test-router-2.conf: role='edge' only allowed with router mode='interior'" in line:
                    test_pass = True
                    break
        self.assertTrue(test_pass)

        test_pass = False
        with open(self.router_3.outfile + '.out', 'r') as out_file:
            for line in out_file:
                if "Exception: Cannot load configuration file test-router-3.conf: role='inter-router' only allowed with router mode='interior'" in line:
                    test_pass = True
                    break
        self.assertTrue(test_pass)

        test_pass = False
        with open(self.router_4.outfile + '.out', 'r') as out_file:
            for line in out_file:
                if "Exception: Cannot load configuration file test-router-4.conf: role='inter-router' only allowed with router mode='interior'" in line:
                    test_pass = True
                    break
        self.assertTrue(test_pass)

        test_pass = False
        with open(self.router_5.outfile + '.out', 'r') as out_file:
            for line in out_file:
                if "Exception: Cannot load configuration file test-router-5.conf: role='standalone' not allowed to connect to or accept connections from other routers." in line:
                    test_pass = True
                    break
        self.assertTrue(test_pass)


class OneRouterTest(TestCase):
    """System tests involving a single router"""
    @classmethod
    def setUpClass(cls):
        """Start a router and a messenger"""
        super(OneRouterTest, cls).setUpClass()
        name = "test-router"
        policy_config_path = os.path.join(DIR, 'one-router-policy')
        OneRouterTest.listen_port = cls.tester.get_port()
        config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'QDR', 'allowUnsettledMulticast': 'yes'}),
            ('policy', {'policyDir': policy_config_path,
                        'enableVhostPolicy': 'true'}),

            # Setting the stripAnnotations to 'no' so that the existing tests will work.
            # Setting stripAnnotations to no will not strip the annotations and any tests that were already in this file
            # that were expecting the annotations to not be stripped will continue working.
            ('listener', {'port': OneRouterTest.listen_port, 'maxFrameSize': '2048', 'stripAnnotations': 'no'}),

            # The following listeners were exclusively added to test the stripAnnotations attribute in qdrouterd.conf file
            # Different listeners will be used to test all allowed values of stripAnnotations ('no', 'both', 'out', 'in')
            ('listener', {'port': cls.tester.get_port(), 'maxFrameSize': '2048', 'stripAnnotations': 'no'}),
            ('listener', {'port': cls.tester.get_port(), 'maxFrameSize': '2048', 'stripAnnotations': 'both'}),
            ('listener', {'port': cls.tester.get_port(), 'maxFrameSize': '2048', 'stripAnnotations': 'out'}),
            ('listener', {'port': cls.tester.get_port(), 'maxFrameSize': '2048', 'stripAnnotations': 'in'}),

            ('address', {'prefix': 'closest', 'distribution': 'closest'}),
            ('address', {'prefix': 'balanced', 'distribution': 'balanced'}),
            ('address', {'prefix': 'multicast', 'distribution': 'multicast'}),
            ('address', {'prefix': 'unavailable', 'distribution': 'unavailable'})
        ])
        cls.router = cls.tester.qdrouterd(name, config)
        cls.router.wait_ready()
        cls.address = cls.router.addresses[0]
        cls.closest_count = 1

        cls.no_strip_addr   = cls.router.addresses[1]
        cls.both_strip_addr = cls.router.addresses[2]
        cls.out_strip_addr  = cls.router.addresses[3]
        cls.in_strip_addr   = cls.router.addresses[4]

    def run_qdmanage(self, cmd, input=None, expect=Process.EXIT_OK, address=None):
        p = self.popen(
            ['qdmanage'] + cmd.split(' ') + ['--bus', address or self.address, '--indent=-1', '--timeout', str(TIMEOUT)],
            stdin=PIPE, stdout=PIPE, stderr=STDOUT, expect=expect,
            universal_newlines=True)
        out = p.communicate(input)[0]
        try:
            p.teardown()
        except Exception as e:
            raise Exception(out if out else str(e))
        return out

    def test_01_listen_error(self):
        # Make sure a router exits if a initial listener fails, doesn't hang.
        config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'bad'}),
            ('listener', {'port': OneRouterTest.listen_port})])
        r = Qdrouterd(name="expect_fail", config=config, wait=False)
        self.assertEqual(1, r.wait())

    def test_02_pre_settled(self):
        addr = self.address + '/closest/' + str(OneRouterTest.closest_count)
        OneRouterTest.closest_count += 1
        test = PreSettled(addr, n_messages=10)
        test.run()
        self.assertIsNone(test.error)

    def test_03_multicast_unsettled(self) :
        n_receivers = 5
        addr = self.address + '/multicast/1'
        test = MulticastUnsettled(addr, n_messages=10, n_receivers=5)
        test.run()
        self.assertIsNone(test.error)

    # DISPATCH-1277. This test will fail with a policy but without the fix in policy_local.py
    # In other words, if the max-frame-size was 2147483647 and not 16384, this
    # test would fail.
    def test_04_disposition_returns_to_closed_connection(self) :
        addr = self.address + '/closest/' + str(OneRouterTest.closest_count)
        OneRouterTest.closest_count += 1
        test = DispositionReturnsToClosedConnection(addr, n_messages=100)
        test.run()
        self.assertIsNone(test.error)

    def test_05_sender_settles_first(self) :
        addr = self.address + '/closest/' + str(OneRouterTest.closest_count)
        OneRouterTest.closest_count += 1
        test = SenderSettlesFirst(addr, n_messages=100)
        test.run()
        self.assertIsNone(test.error)

    def test_06_propagated_disposition(self) :
        addr = self.address + '/closest/' + str(OneRouterTest.closest_count)
        OneRouterTest.closest_count += 1
        test = PropagatedDisposition(addr, n_messages=10)
        test.run()
        self.assertIsNone(test.error)

    def test_07_no_consumer_no_credit(self):
        """
        Ensure a sending client never gets credit if there is no
        consumer present.
        """
        addr = self.address + '/closest/' + str(OneRouterTest.closest_count)
        OneRouterTest.closest_count += 1
        test = NoConsumerNoCredit(addr)
        test.run()
        self.assertIsNone(test.error)

    def test_08_three_ack(self) :
        addr = self.address + '/closest/' + str(OneRouterTest.closest_count)
        OneRouterTest.closest_count += 1
        test = ThreeAck(addr, n_messages=10)
        test.run()
        self.assertIsNone(test.error)

    def test_09_message_annotations(self) :
        addr = self.address + '/closest/' + str(OneRouterTest.closest_count)
        OneRouterTest.closest_count += 1
        test = MessageAnnotations(addr)
        test.run()
        self.assertIsNone(test.error)

    def test_09_1_bad_message_annotations(self) :
        addr = self.address + '/closest/' + str(OneRouterTest.closest_count)
        OneRouterTest.closest_count += 1
        test = BadMessageAnnotations(addr)
        test.run()
        self.assertIsNone(test.error)

    # Tests stripping of ingress and egress annotations.
    # There is a property in qdrouter.json called stripAnnotations with possible values of ["in", "out", "both", "no"]
    # The default for stripAnnotations is "both" (which means strip annotations on both ingress and egress)
    # This test will test the stripAnnotations = no option - meaning no annotations must be stripped.
    # We will send in a custom annotation and make sure that we get back 3 annotations on the received message

    def test_10_strip_message_annotations_custom(self):
        addr = self.no_strip_addr + "/strip_message_annotations_no_custom/1"
        OneRouterTest.closest_count += 1
        test = StripMessageAnnotationsCustom(addr, n_messages=10)
        test.run()
        self.assertIsNone(test.error)

    # stripAnnotations property is set to "no"

    def test_11_test_strip_message_annotations_no(self):
        addr = self.no_strip_addr + "/strip_message_annotations_no/1"
        test = StripMessageAnnotationsNo(addr, n_messages=10)
        test.run()
        self.assertIsNone(test.error)

    # stripAnnotations property is set to "no"

    def test_12_test_strip_message_annotations_no_add_trace(self):
        addr = self.no_strip_addr + "/strip_message_annotations_no_add_trace/1"
        test = StripMessageAnnotationsNoAddTrace(addr, n_messages=10)
        test.run()
        self.assertIsNone(test.error)

    # Dont send any pre-existing ingress or trace annotations. Make sure that there
    # are no outgoing message annotations stripAnnotations property is set to "both".
    # Custom annotations, however, are not stripped.

    def test_13_test_strip_message_annotations_both(self):
        addr = self.both_strip_addr + "/strip_message_annotations_both/1"
        test = StripMessageAnnotationsBoth(addr, n_messages=10)
        test.run()
        self.assertIsNone(test.error)

    # Dont send any pre-existing ingress or trace annotations. Make sure that there
    # are no outgoing message annotations
    # stripAnnotations property is set to "out"

    def test_14_test_strip_message_annotations_out(self):
        addr = self.out_strip_addr + "/strip_message_annotations_out/1"
        test = StripMessageAnnotationsOut(addr, n_messages=10)
        test.run()
        self.assertIsNone(test.error)

    # Send in pre-existing trace and ingress and annotations and make sure
    # that they are not in the outgoing annotations.
    # stripAnnotations property is set to "in"

    def test_15_test_strip_message_annotations_in(self):
        addr = self.in_strip_addr + "/strip_message_annotations_in/1"
        test = StripMessageAnnotationsIn(addr, n_messages=10)
        test.run()
        self.assertIsNone(test.error)

    def test_16_management(self):
        test = ManagementTest(self.address)
        test.run()
        self.assertIsNone(test.error)

    def test_17_management_get_operations(self):
        test = ManagementGetOperationsTest(self.address)
        test.run()
        self.assertIsNone(test.error)

    def test_18_management_not_implemented(self):
        test = ManagementNotImplemented(self.address)
        test.run()
        self.assertIsNone(test.error)

    def test_19_semantics_multicast(self):
        test = SemanticsMulticast(self.address)
        test.run()
        self.assertIsNone(test.error)

    def test_20_semantics_closest(self):
        test = SemanticsClosest(self.address)
        test.run()
        self.assertIsNone(test.error)

    def test_21_semantics_balanced(self):
        test = SemanticsBalanced(self.address)
        test.run()
        self.assertIsNone(test.error)

    def test_22_to_override(self):
        test = MessageAnnotaionsPreExistingOverride(self.address)
        test.run()

    def test_23_send_settle_mode_settled(self):
        """
        The receiver sets a snd-settle-mode of settle thus indicating that it wants to receive settled messages from
        the sender. This tests make sure that the delivery that comes to the receiver comes as already settled.
        """
        send_settle_mode_test = SndSettleModeTest(self.address)
        send_settle_mode_test.run()
        self.assertTrue(send_settle_mode_test.message_received)
        self.assertTrue(send_settle_mode_test.delivery_already_settled)

    def test_24_excess_deliveries_released(self):
        """
        Message-route a series of deliveries where the receiver provides credit for a subset and
        once received, closes the link.  The remaining deliveries should be released back to the sender.
        """
        test = ExcessDeliveriesReleasedTest(self.address)
        test.run()
        self.assertIsNone(test.error)

    def test_25_multicast_unsettled(self):
        test = MulticastUnsettledTest(self.address)
        test.run()
        self.assertIsNone(test.error)

    def test_17_multiframe_presettled(self):
        test = MultiframePresettledTest(self.address)
        test.run()
        self.assertIsNone(test.error)

    def test_27_released_vs_modified(self):
        test = ReleasedVsModifiedTest(self.address)
        test.run()
        self.assertIsNone(test.error)

    def test_28_appearance_of_balance(self):
        test = AppearanceOfBalanceTest(self.address)
        test.run()
        self.assertIsNone(test.error)

    def test_29_batched_settlement(self):
        test = BatchedSettlementTest(self.address)
        test.run()
        self.assertIsNone(test.error)
        self.assertTrue(test.accepted_count_match)

    def test_31_create_unavailable_sender(self):
        test = UnavailableSender(self.address)
        test.run()
        self.assertTrue(test.passed)

    def test_32_create_unavailable_receiver(self):
        test = UnavailableReceiver(self.address)
        test.run()
        self.assertTrue(test.passed)

    def test_33_large_streaming_test(self):
        test = LargeMessageStreamTest(self.address)
        test.run()
        self.assertIsNone(test.error)

    def test_34_reject_coordinator(self):
        test = RejectCoordinatorTest(self.address)
        test.run()
        self.assertTrue(test.passed)

    def test_35_reject_disposition(self):
        test = RejectDispositionTest(self.address)
        test.run()
        self.assertTrue(test.received_error)
        self.assertTrue(test.reject_count_match)

    def test_37_connection_properties_unicode_string(self):
        """
        Tests connection property that is a map of unicode strings and integers
        """
        connection = BlockingConnection(self.router.addresses[0],
                                        timeout=TIMEOUT,
                                        properties=CONNECTION_PROPERTIES_UNICODE_STRING)
        client = SyncRequestResponse(connection)

        node = Node.connect(self.router.addresses[0])

        results = node.query(type='org.apache.qpid.dispatch.connection', attribute_names=['properties']).results

        found = False
        for result in results:
            if 'connection' in result[0] and 'int_property' in result[0]:
                found = True
                self.assertEqual(result[0]['connection'], 'properties')
                self.assertEqual(result[0]['int_property'], 6451)

        self.assertTrue(found)
        client.connection.close()

    def test_38_connection_properties_symbols(self):
        """
        Tests connection property that is a map of symbols
        """
        connection = BlockingConnection(self.router.addresses[0],
                                        timeout=TIMEOUT,
                                        properties=CONNECTION_PROPERTIES_SYMBOL)
        client = SyncRequestResponse(connection)

        node = Node.connect(self.router.addresses[0])

        results = node.query(type='org.apache.qpid.dispatch.connection', attribute_names=['properties']).results

        found = False
        for result in results:
            if 'connection' in result[0]:
                if result[0]['connection'] == 'properties':
                    found = True
                    break

        self.assertTrue(found)

        client.connection.close()

    def test_40_anonymous_sender_no_receiver(self):
        test = AnonymousSenderNoRecvLargeMessagedTest(self.address)
        test.run()
        self.assertIsNone(test.error)

    def test_41_large_streaming_close_conn_test(self):
        test = LargeMessageStreamCloseConnTest(self.address)
        test.run()
        self.assertIsNone(test.error)

    def test_42_unsettled_large_message_test(self):
        test = UnsettledLargeMessageTest(self.address, 250)
        test.run()
        self.assertIsNone(test.error)

    def test_43_dropped_presettled_receiver_stops(self):
        local_node = Node.connect(self.address, timeout=TIMEOUT)
        res = local_node.query('org.apache.qpid.dispatch.router')
        presettled_dropped_count_index = res.attribute_names.index('droppedPresettledDeliveries')
        presettled_dropped_count = res.results[0][presettled_dropped_count_index]
        test = DroppedPresettledTest(self.address, 200, presettled_dropped_count)
        test.run()
        self.assertIsNone(test.error)

    def test_44_delete_connection_fail(self):
        """
        This test creates a blocking connection and tries to update the adminStatus on that connection to "deleted".
        Since the policy associated with this router set allowAdminStatusUpdate as false,
        the update operation will not be permitted.
        """

        # Create a connection with some properties so we can easily identify the connection
        connection = BlockingConnection(self.address,
                                        properties=CONNECTION_PROPERTIES_UNICODE_STRING)
        query_command = 'QUERY --type=connection'
        outputs = json.loads(self.run_qdmanage(query_command))
        identity = None
        passed = False

        for output in outputs:
            if output.get('properties'):
                conn_properties = output['properties']
                # Find the connection that has our properties - CONNECTION_PROPERTIES_UNICODE_STRING
                # Delete that connection and run another qdmanage to see
                # if the connection is gone.
                if conn_properties.get('int_property'):
                    identity = output.get("identity")
                    if identity:
                        update_command = 'UPDATE --type=connection adminStatus=deleted --id=' + identity
                        try:
                            outputs = json.loads(self.run_qdmanage(update_command))
                        except Exception as e:
                            if "Forbidden" in str(e):
                                passed = True

        # The test has passed since we were not allowed to delete a connection
        # because we do not have the policy permission to do so.
        self.assertTrue(passed)

    def test_45_q2_holdoff_drop_stalled_rx(self):
        """
        Verify that dropping a slow consumer while in Q2 flow control does
        not hang the router
        """
        test = Q2HoldoffDropTest(self.router)
        test.run()
        self.assertIsNone(test.error)

    def test_48_connection_uptime_last_dlv(self):
        test = ConnectionUptimeLastDlvTest(self.address, "test_48")
        test.run()
        self.assertIsNone(test.error)

    def test_49_unexpected_release_test(self):
        """
        Verify that the on_released function is only called once for every
        released message. Without the fix for DISPATCH-1626, the on_released
        might be called twice for the same delivery.
        This test will fail once in five runs without the fix for DISPATCH-1626
        """
        test = UnexpectedReleaseTest(self.address)
        test.run()
        self.assertIsNone(test.error)

    def test_50_extension_capabilities(self):
        """
        Test clients sending different offered capability values.  Verify the
        expected offered and desired capabilities sent by the router
        """
        client_caps = [symbol("single"),
                       ["A", "B", "C"],
                       [9, 10, 11],
                       [],
                       None]

        for cc in client_caps:
            test = ExtensionCapabilitiesTest(self.address, cc)
            test.run()
            self.assertIsNone(test.error)

            # check the caps sent by router.
            self.assertTrue(test.remote_offered is not None)
            self.assertTrue(test.remote_desired is not None)
            ro = list(test.remote_offered)
            rd = list(test.remote_desired)
            for rc in [ro, rd]:
                self.assertIn(symbol('ANONYMOUS-RELAY'), rc)
                self.assertIn(symbol('qd.streaming-links'), rc)


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
        bd = msg.body
        if isinstance(bd, dict) and 'results' in bd and 'attributeNames' in bd:
            ##
            # This is a query response
            ##
            response = []
            anames = bd['attributeNames']
            for row in bd['results']:
                cols = {}
                for i in range(len(row)):
                    cols[anames[i]] = row[i]
                response.append(Entity(ap['statusCode'], ap['statusDescription'], cols))
            return response

        return Entity(ap['statusCode'], ap['statusDescription'], msg.body)

    def read_address(self, name):
        ap = {'operation': 'READ', 'type': 'org.apache.qpid.dispatch.router.address', 'name': name}
        return Message(properties=ap, reply_to=self.reply_addr)

    def query_addresses(self):
        ap = {'operation': 'QUERY', 'type': 'org.apache.qpid.dispatch.router.address'}
        return Message(properties=ap, reply_to=self.reply_addr)

    def query_links(self):
        ap = {'operation': 'QUERY', 'type': 'org.apache.qpid.dispatch.router.link'}
        return Message(properties=ap, reply_to=self.reply_addr)


class ReleasedChecker:
    def __init__(self, parent):
        self.parent = parent

    def on_timer_task(self, event):
        self.parent.released_check_timeout()


class ExtensionCapabilitiesTest(MessagingHandler):
    def __init__(self, address, capabilities):
        """
        capabilities: sent by this client to the router
        """
        super(ExtensionCapabilitiesTest, self).__init__()
        self._addr = address
        self._caps = capabilities
        self._timer = None
        self._conn = None
        self.remote_offered = None
        self.remote_desired = None
        self.error = None

    def on_start(self, event):
        self._timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self._conn = event.container.connect(self._addr,
                                             offered_capabilities=self._caps,
                                             desired_capabilities=self._caps)

    def timeout(self):
        self.error = "Timeout Expired: connection failed"
        if self._conn:
            self._conn.close()

    def on_connection_opened(self, event):
        self.remote_offered = event.connection.remote_offered_capabilities
        self.remote_desired = event.connection.remote_desired_capabilities
        self._timer.cancel()
        self._conn.close()

    def run(self):
        Container(self).run()


class UnexpectedReleaseTest(MessagingHandler):
    def __init__(self, address):
        super(UnexpectedReleaseTest, self).__init__(auto_accept=False)
        self.address = address
        self.dest = "UnexpectedReleaseTest"
        self.timer = None
        self.sender_conn = None
        self.receiver_conn = None
        self.sender = None
        self.receiver = None
        self.num_messages = 250
        self.recv_messages_max = 200
        self.num_sent = 0
        self.num_received = 0
        self.num_released = 0
        self.num_accepted = 0
        # Send a large message
        self.body = "123456789" * 8192
        self.receiver_conn_closed = False
        self.error = None
        self.released_checker = None

    def timeout(self):
        self.error = "Timeout Expired: sent=%d accepted=%d released=%d number excpected to be released=%d" % \
                     (self.num_sent, self.num_accepted, self.num_released, self.num_messages - self.recv_messages_max)
        if not self.receiver_conn_closed:
            self.receiver_conn.close()
        self.sender_conn.close()

    def released_check_timeout(self):
        if not self.receiver_conn_closed:
            self.receiver_conn.close()
        self.sender_conn.close()
        self.timer.cancel()

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.sender_conn = event.container.connect(self.address)
        self.receiver_conn = event.container.connect(self.address)
        self.receiver = event.container.create_receiver(self.receiver_conn, self.dest)

    def on_link_opened(self, event):
        if event.receiver == self.receiver:
            # Wait for the receiver to be created and then create the sender.
            self.sender = event.container.create_sender(self.sender_conn, self.dest)

    def on_sendable(self, event):
        if self.num_sent < self.num_messages:
            msg = Message(body=self.body)
            self.sender.send(msg)
            self.num_sent += 1

    def on_released(self, event):
        self.num_released += 1
        if self.num_released == self.num_messages - self.recv_messages_max:
            # We have received the expected number of calls to on_released
            # but without the fix for DISPATCH-1626 we expect the on_released
            # to be called an additional one or more times. We will kick off
            # a 3 second timer after which we will check if we got more
            # calls to on_released.
            self.released_checker = event.reactor.schedule(3, ReleasedChecker(self))
        if self.num_released > self.num_messages - self.recv_messages_max:
            # This if statement will be true if the client receives a 2 part
            # dispostion from the router like the following
            #
            # [0x562a0083ed80]:0 <- @disposition(21) [role=true, first=981, state=@released(38) []]
            # [0x562a0083ed80]:0 <- @disposition(21) [role=true, first=981, last=982, settled=true, state=@released(38) []]
            #
            self.error = "Expected %d calls to on_released but got %d" % (self.num_messages - self.recv_messages_max, self.num_released)

    def on_accepted(self, event):
        self.num_accepted += 1

    def on_message(self, event):
        if event.receiver == self.receiver:
            self.num_received += 1
            if self.num_received <= self.recv_messages_max:
                event.delivery.settle()
            if self.num_received == self.recv_messages_max:
                self.receiver_conn.close()
                self.receiver_conn_closed = True

    def run(self):
        Container(self).run()


class SemanticsClosest(MessagingHandler):
    def __init__(self, address):
        super(SemanticsClosest, self).__init__()
        self.address = address
        self.dest = "closest.1"
        self.timer = None
        self.conn = None
        self.sender = None
        self.receiver_a = None
        self.receiver_b = None
        self.receiver_c = None
        self.num_messages = 100
        self.n_received_a = 0
        self.n_received_b = 0
        self.n_received_c = 0
        self.error = None
        self.n_sent = 0
        self.rx_set = []

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.conn = event.container.connect(self.address)
        self.sender = event.container.create_sender(self.conn, self.dest)
        # Receiver on same router as the sender must receive all the messages. The other two
        # receivers are on the other router
        self.receiver_a = event.container.create_receiver(self.conn, self.dest, name="A")
        self.receiver_b = event.container.create_receiver(self.conn, self.dest, name="B")
        self.receiver_c = event.container.create_receiver(self.conn, self.dest, name="C")

    def timeout(self):
        self.error = "Timeout Expired: sent=%d rcvd=%d/%d/%d" % \
                     (self.n_sent, self.n_received_a, self.n_received_b, self.n_received_c)
        self.conn.close()

    def check_if_done(self):
        if self.n_received_a + self.n_received_b + self.n_received_c == self.num_messages\
                and self.n_received_b != 0 and self.n_received_c != 0:
            self.rx_set.sort()
            # print self.rx_set
            all_messages_received = True
            for i in range(self.num_messages):
                if not i == self.rx_set[i]:
                    all_messages_received = False

            if all_messages_received:
                self.timer.cancel()
                self.conn.close()

    def on_sendable(self, event):
        if self.n_sent < self.num_messages:
            msg = Message(body={'number': self.n_sent})
            self.sender.send(msg)
            self.n_sent += 1

    def on_message(self, event):
        if event.receiver == self.receiver_a:
            self.n_received_a += 1
            self.rx_set.append(event.message.body['number'])
        if event.receiver == self.receiver_b:
            self.n_received_b += 1
            self.rx_set.append(event.message.body['number'])
        if event.receiver == self.receiver_c:
            self.n_received_c += 1
            self.rx_set.append(event.message.body['number'])

    def on_accepted(self, event):
        self.check_if_done()

    def run(self):
        Container(self).run()


class MessageAnnotaionsPreExistingOverride(MessagingHandler):
    def __init__(self, address):
        super(MessageAnnotaionsPreExistingOverride, self).__init__()
        self.address = address
        self.dest = "toov/1"
        self.error = "Pre-existing x-opt-qd.to has been stripped"
        self.timer = None
        self.conn = None
        self.sender = None
        self.receiver = None
        self.msg_not_sent = True

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.conn = event.container.connect(self.address)
        self.sender = event.container.create_sender(self.conn, self.dest)
        self.receiver = event.container.create_receiver(self.conn, self.dest)

    def timeout(self):
        self.error = "Timeout Expired: Sent message not received"
        self.conn.close()

    def bail(self, message):
        self.error = message
        self.conn.close()
        self.timer.cancel()

    def on_sendable(self, event):
        if self.msg_not_sent:
            msg = Message(body={'number': 0})
            msg.annotations = {'x-opt-qd.to': 'toov/1'}
            event.sender.send(msg)
            self.msg_not_sent = False

    def on_message(self, event):
        if 0 == event.message.body['number']:
            ma = event.message.annotations
            if ma['x-opt-qd.to'] == 'toov/1':
                self.bail(None)
            else:
                self.bail("Pre-existing x-opt-qd.to has been stripped")
        else:
            self.bail("body does not match with the sent message body")

    def run(self):
        Container(self).run()


class SemanticsMulticast(MessagingHandler):
    def __init__(self, address):
        """
        Verify that for every 1 unsettled mcast message received, N messages are sent
        out (where N == number of receivers).  Assert that multiple received
        dispositions are summarized to send out one disposition.
        """
        super(SemanticsMulticast, self).__init__(auto_accept=False)
        self.address = address
        self.dest = "multicast.2"
        self.error = None
        self.n_sent = 0
        self.n_settled = 0
        self.count = 3
        self.n_received_a = 0
        self.n_received_b = 0
        self.n_received_c = 0
        self.n_accepts = 0
        self.n_recv_ready = 0
        self.timer = None
        self.conn_1 = None
        self.conn_2 = None
        self.sender = None
        self.receiver_a = None
        self.receiver_b = None
        self.receiver_c = None

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.conn_1 = event.container.connect(self.address)
        self.conn_2 = event.container.connect(self.address)
        self.receiver_a = event.container.create_receiver(self.conn_2, self.dest, name="A")
        self.receiver_b = event.container.create_receiver(self.conn_1, self.dest, name="B")
        self.receiver_c = event.container.create_receiver(self.conn_2, self.dest, name="C")

    def timeout(self):
        self.error = "Timeout Expired: sent=%d rcvd=%d/%d/%d" % \
                     (self.n_sent, self.n_received_a, self.n_received_b, self.n_received_c)
        self.conn_1.close()
        self.conn_2.close()

    def check_if_done(self):
        c = self.n_received_a + self.n_received_b + self.n_received_c
        if (c == self.count
                and self.n_received_a == self.n_received_b
                and self.n_received_c == self.n_received_b
                and self.n_accepts == self.n_sent
                and self.n_settled == self.count):
            self.timer.cancel()
            self.conn_1.close()
            self.conn_2.close()

    def on_link_opened(self, event):
        if event.receiver:
            self.n_recv_ready += 1
            if self.n_recv_ready == self.count:
                self.sender = event.container.create_sender(self.conn_1, self.dest)

    def on_sendable(self, event):
        if self.n_sent == 0:
            msg = Message(body="SemanticsMulticast-Test")
            self.sender.send(msg)
            self.n_sent += 1

    def on_message(self, event):
        if event.receiver == self.receiver_a:
            self.n_received_a += 1
        if event.receiver == self.receiver_b:
            self.n_received_b += 1
        if event.receiver == self.receiver_c:
            self.n_received_c += 1
        event.delivery.update(Delivery.ACCEPTED)

    def on_accepted(self, event):
        self.n_accepts += 1
        event.delivery.settle()

    def on_settled(self, event):
        self.n_settled += 1
        self.check_if_done()

    def run(self):
        Container(self).run()


class ManagementNotImplemented(MessagingHandler):
    def __init__(self, address):
        super(ManagementNotImplemented, self).__init__()
        self.address = address
        self.timer = None
        self.conn = None
        self.sender = None
        self.receiver = None
        self.sent_count = 0
        self.error = None

    def timeout(self):
        self.error = "No response received for management request"
        self.conn.close()

    def bail(self, message):
        self.error = message
        self.conn.close()
        self.timer.cancel()

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.conn = event.container.connect(self.address)
        self.sender = event.container.create_sender(self.conn)
        self.receiver = event.container.create_receiver(self.conn, None, dynamic=True)

    def on_link_opened(self, event):
        if event.receiver == self.receiver:
            request = Message()
            request.address = "amqp:/_local/$management"
            request.reply_to = event.receiver.remote_source.address
            request.properties = {'type': 'org.amqp.management',
                                  'name': 'self',
                                  'operation': 'NOT-IMPL'}
            self.sender.send(request)

    def run(self):
        Container(self).run()

    def on_message(self, event):
        if event.receiver == self.receiver:
            if event.message.properties['statusCode'] == 501:
                self.bail(None)
            else:
                self.bail("The return status code is %s. It should be 501" % str(event.message.properties['statusCode']))


class ManagementGetOperationsTest(MessagingHandler):
    def __init__(self, address):
        super(ManagementGetOperationsTest, self).__init__()
        self.address = address
        self.timer = None
        self.conn = None
        self.sender = None
        self.receiver = None
        self.sent_count = 0
        self.error = None

    def timeout(self):
        self.error = "No response received for management request"
        self.conn.close()

    def bail(self, message):
        self.error = message
        self.conn.close()
        self.timer.cancel()

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.conn = event.container.connect(self.address)
        self.sender = event.container.create_sender(self.conn)
        self.receiver = event.container.create_receiver(self.conn, None, dynamic=True)

    def on_link_opened(self, event):
        if self.receiver == event.receiver:
            request = Message()
            request.address = "amqp:/_local/$management"
            request.reply_to = self.receiver.remote_source.address
            request.properties = {'type': 'org.amqp.management', 'name': 'self', 'operation': 'GET-OPERATIONS'}
            self.sender.send(request)

    def run(self):
        Container(self).run()

    def on_message(self, event):
        if event.receiver == self.receiver:
            if event.message.properties['statusCode'] == 200:
                if 'org.apache.qpid.dispatch.router' in event.message.body.keys():
                    if len(event.message.body.keys()) > 2:
                        self.bail(None)
                    else:
                        self.bail('size of keys in message body less than or equal 2')
                else:
                    self.bail('org.apache.qpid.dispatch.router is not in the keys')
            else:
                self.bail("The return status code is %s. It should be 200" % str(event.message.properties['statusCode']))


class ManagementTest(MessagingHandler):
    def __init__(self, address):
        super(ManagementTest, self).__init__()
        self.address = address
        self.timer = None
        self.conn = None
        self.sender = None
        self.receiver = None
        self.sent_count = 0
        self.msg_not_sent = True
        self.error = None
        self.response1 = False
        self.response2 = False

    def timeout(self):
        if not self.response1:
            self.error = "Incorrect response received for message with correlation id C1"
        if not self.response1:
            self.error = self.error + "and incorrect response received for message with correlation id C2"
        self.conn.close()

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.conn = event.container.connect(self.address)
        self.sender = event.container.create_sender(self.conn)
        self.receiver = event.container.create_receiver(self.conn, None, dynamic=True)

    def on_link_opened(self, event):
        if event.receiver == self.receiver:
            request = Message()
            request.address = "amqp:/$management"
            request.reply_to = self.receiver.remote_source.address
            request.correlation_id = "C1"
            request.properties = {'type': 'org.amqp.management', 'name': 'self', 'operation': 'GET-MGMT-NODES'}
            self.sender.send(request)

            request = Message()
            request.address = "amqp:/_topo/0/QDR.B/$management"
            request.correlation_id = "C2"
            request.reply_to = self.receiver.remote_source.address
            request.properties = {'type': 'org.amqp.management', 'name': 'self', 'operation': 'GET-MGMT-NODES'}
            self.sender.send(request)

    def on_message(self, event):
        if event.receiver == self.receiver:
            if event.message.correlation_id == "C1":
                if event.message.properties['statusCode'] == 200 and \
                        event.message.properties['statusDescription'] is not None \
                        and event.message.body == []:
                    self.response1 = True
            elif event.message.correlation_id == "C2":
                if event.message.properties['statusCode'] == 200 and \
                        event.message.properties['statusDescription'] is not None \
                        and event.message.body == []:
                    self.response2 = True

        if self.response1 and self.response2:
            self.error = None

        if self.error is None:
            self.timer.cancel()
            self.conn.close()

    def run(self):
        Container(self).run()


class CustomTimeout:
    def __init__(self, parent):
        self.parent = parent

    def addr_text(self, addr):
        if not addr:
            return ""
        if addr[0] == 'M':
            return addr[2:]
        else:
            return addr[1:]

    def on_timer_task(self, event):
        local_node = Node.connect(self.parent.address, timeout=TIMEOUT)

        res = local_node.query('org.apache.qpid.dispatch.router.address')
        name = res.attribute_names.index('name')
        found = False
        for results in res.results:
            if "balanced.1" == self.addr_text(results[name]):
                found = True
                break

        if found:
            self.parent.cancel_custom()
            self.parent.create_sender(event)

        else:
            event.reactor.schedule(2, self)


class SemanticsBalanced(MessagingHandler):
    def __init__(self, address):
        super(SemanticsBalanced, self).__init__(auto_accept=False, prefetch=0)
        self.address = address
        self.dest = "balanced.1"
        self.timer = None
        self.conn = None
        self.sender = None
        self.receiver_a = None
        self.receiver_b = None
        self.receiver_c = None
        self.num_messages = 250
        self.n_received_a = 0
        self.n_received_b = 0
        self.n_received_c = 0
        self.error = None
        self.n_sent = 0
        self.rx_set = []
        self.custom_timer = None

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.custom_timer = event.reactor.schedule(2, CustomTimeout(self))
        self.conn = event.container.connect(self.address)

        # This receiver is on the same router as the sender
        self.receiver_a = event.container.create_receiver(self.conn, self.dest, name="A")

        # These two receivers are connected to a different router than the sender
        self.receiver_b = event.container.create_receiver(self.conn, self.dest, name="B")
        self.receiver_c = event.container.create_receiver(self.conn, self.dest, name="C")

        self.receiver_a.flow(100)
        self.receiver_b.flow(100)
        self.receiver_c.flow(100)

    def cancel_custom(self):
        self.custom_timer.cancel()

    def create_sender(self, event):
        self.sender = event.container.create_sender(self.conn, self.dest)

    def timeout(self):
        self.error = "Timeout Expired: sent=%d rcvd=%d/%d/%d" % \
                     (self.n_sent, self.n_received_a, self.n_received_b, self.n_received_c)
        self.conn.close()

    def check_if_done(self):
        if self.n_received_a + self.n_received_b + self.n_received_c == self.num_messages and \
                self.n_received_a > 0 and self.n_received_b > 0 and self.n_received_c > 0:
            self.rx_set.sort()
            all_messages_received = True
            for i in range(self.num_messages):
                if not i == self.rx_set[i]:
                    all_messages_received = False

            if all_messages_received:
                self.timer.cancel()
                self.conn.close()

    def on_sendable(self, event):
        if self.n_sent < self.num_messages:
            msg = Message(body={'number': self.n_sent})
            self.sender.send(msg)
            self.n_sent += 1

    def on_message(self, event):
        if event.receiver == self.receiver_a:
            self.n_received_a += 1
            self.rx_set.append(event.message.body['number'])
        elif event.receiver == self.receiver_b:
            self.n_received_b += 1
            self.rx_set.append(event.message.body['number'])
        elif event.receiver == self.receiver_c:
            self.n_received_c += 1
            self.rx_set.append(event.message.body['number'])

        self.check_if_done()

    def run(self):
        Container(self).run()


class PreSettled (MessagingHandler) :
    def __init__(self,
                 addr,
                 n_messages
                 ) :
        super(PreSettled, self) . __init__(prefetch=n_messages)
        self.addr       = addr
        self.n_messages = n_messages

        self.sender     = None
        self.receiver   = None
        self.n_sent     = 0
        self.n_received = 0
        self.error      = None
        self.test_timer = None

    def run(self) :
        Container(self).run()

    def bail(self, travail) :
        self.error = travail
        self.send_conn.close()
        self.recv_conn.close()
        self.test_timer.cancel()

    def timeout(self):
        self.bail("Timeout Expired: %d messages received, %d expected." % (self.n_received, self.n_messages))

    def on_start(self, event):
        self.send_conn = event.container.connect(self.addr)
        self.recv_conn = event.container.connect(self.addr)
        self.sender   = event.container.create_sender(self.send_conn, self.addr)
        self.receiver = event.container.create_receiver(self.send_conn, self.addr)
        self.receiver.flow(self.n_messages)
        self.test_timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))

    def on_sendable(self, event) :
        while self.n_sent < self.n_messages :
            if event.sender.credit < 1 :
                break
            msg = Message(body=self.n_sent)
            # Presettle the delivery.
            dlv = self.sender.send(msg)
            dlv.settle()
            self.n_sent += 1

    def on_message(self, event) :
        self.n_received += 1
        if self.n_received >= self.n_messages :
            self.bail(None)


class SendPresettledAfterReceiverCloses(object):
    def __init__(self, parent):
        self.parent = parent
        self.num_tries = 0

    def on_timer_task(self, event):
        self.num_tries += 1
        local_node = Node.connect(self.parent.addr, timeout=TIMEOUT)
        res = local_node.query('org.apache.qpid.dispatch.router.link')
        owning_addr_index = res.attribute_names.index('owningAddr')
        has_address = False
        for out in res.results:
            owning_addr = out[owning_addr_index]
            # Check if the receiver's address is present in the router's address table.
            # If the address is still there, try one more time until self.parent.max_tries.
            if self.parent.addr in owning_addr:
                has_address = True
                break

        if has_address:
            if self.num_tries == self.parent.max_tries:
                self.parent.bail("Address %s is still in routing table" % owning_addr)
            else:
                self.parent.schedule_send_timer()
        else:
            # Address is not there in the address table anymore.
            # Send the remaining messages. These presettled messages must be
            # dropped by the router which we will verify using the router's
            # droppedPresettledDeliveries
            self.parent.send_remaining()


class PresettledCustomTimeout(object):
    def __init__(self, parent):
        self.parent = parent
        self.num_tries = 0

    def on_timer_task(self, event):
        self.num_tries += 1
        local_node = Node.connect(self.parent.addr, timeout=TIMEOUT)
        res = local_node.query('org.apache.qpid.dispatch.router')
        presettled_deliveries_dropped_index = res.attribute_names.index('droppedPresettledDeliveries')
        presettled_dropped_count =  res.results[0][presettled_deliveries_dropped_index]

        deliveries_dropped_diff = presettled_dropped_count - self.parent.begin_dropped_presettled_count

        # Without the fix for DISPATCH-1213  the ingress count will be less than
        # 200 because the sender link has stalled. The q2_holdoff happened
        # and so all the remaining messages are still in the
        # proton buffers.
        if deliveries_dropped_diff == self.parent.n_messages - self.parent.max_receive:
            self.parent.bail(None)
        else:
            if self.num_tries == self.parent.max_tries:
                self.parent.bail("Messages sent to the router is %d, "
                                 "Messages dropped by the router is %d" %
                                 (self.parent.n_messages,
                                  deliveries_dropped_diff))
            else:
                self.parent.schedule_custom_timer()


class DroppedPresettledTest(MessagingHandler):
    """
    First send 10 large messages and a receiver receives them all and exits.
    These first 10 messages are presettled messages and the router network did not
    drop them.
    Now send an additional 190 messages and make sure they are dropped by checking
    the droppedPresettledCount
    """
    def __init__(self, addr, n_messages, begin_dropped_presettled_count):
        super(DroppedPresettledTest, self).__init__()
        self.addr = addr
        self.n_messages = n_messages
        self.sender = None
        self.receiver = None
        self.sender_conn = None
        self.recv_conn = None
        self.n_sent = 0
        self.n_received = 0
        self.error = None
        self.test_timer = None
        self.max_receive = 10
        self.custom_timer = None
        self.send_timer = None
        self.timer = None
        self.begin_dropped_presettled_count = begin_dropped_presettled_count
        self.str1 = "0123456789abcdef"
        self.msg_str = ""
        self.max_tries = 10
        self.reactor = None
        for i in range(8192):
            self.msg_str += self.str1

    def schedule_custom_timer(self):
        self.custom_timer = self.reactor.schedule(0.5, PresettledCustomTimeout(self))

    def schedule_send_timer(self):
        self.send_timer = self.reactor.schedule(0.5, SendPresettledAfterReceiverCloses(self))

    def run(self):
        Container(self).run()

    def bail(self, travail):
        self.error = travail
        self.sender_conn.close()
        if self.recv_conn:
            self.recv_conn.close()
        self.timer.cancel()
        if self.custom_timer:
            self.custom_timer.cancel()
        if self.send_timer:
            self.send_timer.cancel()

    def timeout(self,):
        self.bail("Timeout Expired: %d messages received, %d expected." %
                  (self.n_received, self.n_messages))

    def on_start(self, event):
        self.reactor = event.reactor
        self.sender_conn = event.container.connect(self.addr)
        self.recv_conn = event.container.connect(self.addr)
        self.receiver = event.container.create_receiver(self.recv_conn,
                                                        "test_43")
        self.sender = event.container.create_sender(self.sender_conn,
                                                    "test_43")
        self.timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))

    def send(self):
        msg = Message(id=(self.n_sent + 1),
                      body={'sequence': (self.n_sent + 1),
                            'msg_str': self.msg_str})
        # Presettle the delivery.
        dlv = self.sender.send(msg)
        dlv.settle()
        self.n_sent += 1

    def send_remaining(self):
        while self.n_sent < self.n_messages:
            self.send()
        self.schedule_custom_timer()

    def on_sendable(self, event):
        # Send only self.max_receive messages.
        while self.n_sent < self.max_receive:
            self.send()

    def on_message(self, event):
        self.n_received += 1
        if self.n_received == self.max_receive:
            # Receiver bails after receiving max_receive messages.
            self.receiver.close()
            self.recv_conn.close()
            # When self.max_receive messages have been received and the receiver has been closed
            # we try to check to see if the receiver's address is gone from the address table.
            self.schedule_send_timer()


class MulticastUnsettled (MessagingHandler) :
    def __init__(self,
                 addr,
                 n_messages,
                 n_receivers
                 ) :
        super(MulticastUnsettled, self) . __init__(auto_accept=False, prefetch=n_messages)
        self.addr        = addr
        self.n_messages  = n_messages
        self.n_receivers = n_receivers

        self.sender     = None
        self.receivers  = list()
        self.n_sent     = 0
        self.n_received = list()
        self.error      = None
        self.test_timer = None
        self.bailing    = False

    def run(self) :
        Container(self).run()

    def bail(self, travail) :
        self.bailing = True
        self.error = travail
        self.send_conn.close()
        self.recv_conn.close()
        self.test_timer.cancel()

    def timeout(self):
        self.bail("Timeout Expired")

    def on_start(self, event):
        self.recv_conn = event.container.connect(self.addr)
        for i in range(self.n_receivers) :
            rcvr = event.container.create_receiver(self.recv_conn, self.addr, name="receiver_" + str(i))
            rcvr.flow(self.n_messages)
        self.test_timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))

    def on_link_opened(self, event):
        if event.receiver:
            self.receivers.append(event.receiver)
            self.n_received.append(0)
            # start the sender once all receivers links are up
            if len(self.receivers) == self.n_receivers:
                self.send_conn = event.container.connect(self.addr)
                self.sender = event.container.create_sender(self.send_conn, self.addr)

    def on_sendable(self, event) :
        while self.n_sent < self.n_messages :
            if event.sender.credit < 1 :
                break
            for i in range(self.n_messages) :
                msg = Message(body=i)
                # The sender does not settle, but the
                # receivers will..
                self.sender.send(msg)
                self.n_sent += 1

    def on_message(self, event) :
        if self.bailing :
            return
        event.delivery.settle()
        for i in range(self.n_receivers) :
            if event.receiver == self.receivers[i] :
                # Body conetnts of the messages count from 0 ... n,
                # so the contents of this message should be same as
                # the current number of messages received by this receiver.
                if self.n_received[i] != event.message.body :
                    self.bail("out of order or missed message: receiver %d got %d instead of %d" %
                              (i, event.message.body, self.n_received[i])
                              )
                    return
                self.n_received[i] += 1
                self.check_n_received()

    def check_n_received(self) :
        for i in range(self.n_receivers) :
            if self.n_received[i] < self.n_messages :
                return
        # All messages have been received by all receivers.
        self.bail(None)


class DispositionReturnsToClosedConnection (MessagingHandler) :

    def __init__(self,
                 addr,
                 n_messages
                 ) :
        super(DispositionReturnsToClosedConnection, self) . __init__(prefetch=n_messages)
        self.addr       = addr
        self.n_messages = n_messages

        self.n_sent     = 0
        self.n_received = 0

    def run(self) :
        Container(self).run()

    def bail(self, travail) :
        self.bailing = True
        self.test_timer.cancel()
        self.error = travail
        if self.send_conn :
            self.send_conn.close()
        self.recv_conn.close()

    def timeout(self) :
        self.bail("Timeout Expired")

    def on_start(self, event):
        self.send_conn = event.container.connect(self.addr)
        self.recv_conn = event.container.connect(self.addr)
        self.sender   = event.container.create_sender(self.send_conn, self.addr)
        self.receiver = event.container.create_receiver(self.recv_conn, self.addr)
        self.test_timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))

    def on_sendable(self, event) :

        if not self.send_conn :
            return

        while self.n_sent < self.n_messages :
            if event.sender.credit < 1 :
                break
            msg = Message(body=self.n_sent)
            self.sender.send(msg)
            self.n_sent += 1

        # Immediately upon finishing sending all the messages, the
        # sender closes its connection, so that when the dispositions
        # try to come back they will find no one who cares.
        # The only problem I can directly detect here is a test
        # timeout. And, indirectly, we are making sure that the router
        # does not blow sky high.
        if self.n_sent >= self.n_messages :
            self.send_conn.close()
            self.send_conn = None

    # On the receiver side, we keep accepting and settling
    # messages, tragically unaware that no one cares.

    def on_message(self, event) :
        event.delivery.update(Delivery.ACCEPTED)
        event.delivery.settle()
        self.n_received += 1
        if self.n_received >= self.n_messages :
            self.bail(None)


class SenderSettlesFirst (MessagingHandler) :
    def __init__(self,
                 addr,
                 n_messages
                 ) :
        super(SenderSettlesFirst, self) . __init__(prefetch=n_messages)
        self.addr        = addr
        self.n_messages  = n_messages

        self.test_timer = None
        self.sender     = None
        self.receiver   = None
        self.n_sent     = 0
        self.n_received = 0

    def run(self) :
        Container(self).run()

    def bail(self, travail) :
        self.bailing = True
        self.error = travail
        self.send_conn.close()
        self.recv_conn.close()
        self.test_timer.cancel()

    def timeout(self):
        self.bail("Timeout Expired")

    def on_start(self, event):
        self.send_conn = event.container.connect(self.addr)
        self.recv_conn = event.container.connect(self.addr)

        self.sender      = event.container.create_sender(self.send_conn, self.addr)
        self.receiver    = event.container.create_receiver(self.recv_conn, self.addr)
        self.test_timer  = event.reactor.schedule(TIMEOUT, TestTimeout(self))

    def on_sendable(self, event) :
        while self.n_sent < self.n_messages :
            if event.sender.credit < 1 :
                break
            msg = Message(body=self.n_sent)
            # Settle the delivery immediately after sending.
            dlv = self.sender.send(msg)
            dlv.settle()
            self.n_sent += 1

    def on_message(self, event) :
        self.n_received += 1
        event.delivery.settle()
        if self.n_received >= self.n_messages :
            self.bail(None)


class PropagatedDisposition (MessagingHandler) :
    def __init__(self,
                 addr,
                 n_messages
                 ) :
        super(PropagatedDisposition, self) . __init__(prefetch=n_messages)
        self.addr        = addr
        self.n_messages  = n_messages

        self.test_timer = None
        self.sender     = None
        self.receiver   = None
        self.n_sent     = 0
        self.n_received = 0
        self.n_accepted = 0
        self.bailing    = False

    def run(self) :
        Container(self).run()

    def bail(self, travail) :
        self.bailing = True
        self.error = travail
        self.send_conn.close()
        self.recv_conn.close()
        self.test_timer.cancel()

    def timeout(self):
        self.bail("Timeout Expired")

    def on_start(self, event):
        self.send_conn = event.container.connect(self.addr)
        self.recv_conn = event.container.connect(self.addr)

        self.sender      = event.container.create_sender(self.send_conn, self.addr)
        self.receiver    = event.container.create_receiver(self.recv_conn, self.addr)
        self.test_timer  = event.reactor.schedule(TIMEOUT, TestTimeout(self))

    # Sender Side ================================================
    def on_sendable(self, event) :
        if self.bailing :
            return
        while self.n_sent < self.n_messages :
            if event.sender.credit < 1 :
                break
            msg = Message(body=self.n_sent)
            dlv = self.sender.send(msg)
            if dlv.remote_state != 0 :
                self.bail("remote state nonzero on send.")
                break
            if not dlv.pending :
                self.bail("dlv not pending immediately after send.")
                break

            self.n_sent += 1

    def on_accepted(self, event) :
        if self.bailing :
            return
        dlv = event.delivery
        if dlv.pending :
            self.bail("Delivery still pending after accepted.")
            return
        if dlv.remote_state != Delivery.ACCEPTED :
            self.bail("Delivery remote state is not ACCEPTED after accept.")
            return
        self.n_accepted += 1
        if self.n_accepted >= self.n_messages :
            # Success!
            self.bail(None)

    # Receiver Side ================================================
    def on_message(self, event) :
        if self.bailing :
            return
        self.n_received += 1
        dlv = event.delivery
        if dlv.pending :
            self.bail('Delivery still pending at receiver.')
            return
        if dlv.local_state != 0 :
            self.bail('At receiver: delivery local state nonzero at receiver before accept.')
            return
        dlv.update(Delivery.ACCEPTED)


class NoConsumerNoCredit(MessagingHandler):
    def __init__(self, addr):
        super(NoConsumerNoCredit, self).__init__()
        self.addr         = addr
        self.test_timer   = None
        self.credit_timer = None
        self.sender       = None
        self.bailing      = False

    def run(self):
        Container(self).run()

    def bail(self, travail):
        self.bailing = True
        self.error = travail
        self.send_conn.close()
        self.test_timer and self.test_timer.cancel()
        self.credit_timer and self.credit_timer.cancel()

    def timeout(self):
        self.bail("Test timed out - should not happen!")

    def on_timer_task(self, event):
        # no credit arrived: success
        self.bail(None)

    def on_start(self, event):
        self.send_conn = event.container.connect(self.addr)
        self.sender    = event.container.create_sender(self.send_conn, self.addr)
        # Uh-oh. We are not creating a receiver!
        self.test_timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))

    def on_link_opened(self, event):
        # delay to ensure no credit arrives
        if event.link == self.sender:
            self.credit_timer = event.reactor.schedule(1.0, self)

    def on_sendable(self, event):
        if event.link == self.sender:
            self.bail("Received credit even though no consumer present")

    def on_message(self, event) :
        self.bail("on_message should never happen")


class ThreeAck (MessagingHandler) :
    def __init__(self,
                 addr,
                 n_messages
                 ) :
        super(ThreeAck, self) . __init__(prefetch=n_messages)
        self.addr        = addr
        self.n_messages  = n_messages

        self.test_timer = None
        self.sender     = None
        self.receiver   = None
        self.n_sent     = 0
        self.n_received = 0
        self.n_accepted = 0
        self.bailing    = False
        self.tmp_dlv    = None

    def run(self) :
        Container(self).run()

    def bail(self, travail) :
        self.bailing = True
        self.error = travail
        self.send_conn.close()
        self.recv_conn.close()
        self.test_timer.cancel()

    def timeout(self):
        self.bail("Timeout Expired")

    def on_start(self, event):
        self.send_conn = event.container.connect(self.addr)
        self.recv_conn = event.container.connect(self.addr)

        self.sender      = event.container.create_sender(self.send_conn, self.addr)
        self.receiver    = event.container.create_receiver(self.recv_conn, self.addr)
        self.test_timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))

    # Sender Side ================================================
    def on_sendable(self, event) :
        if self.bailing :
            return
        while self.n_sent < self.n_messages :
            if event.sender.credit < 1 :
                break
            msg = Message(body=self.n_sent)
            dlv = self.sender.send(msg)

            self.n_sent += 1

    def on_accepted(self, event) :
        if self.bailing :
            return
        dlv = event.delivery
        if dlv.remote_state != Delivery.ACCEPTED :
            self.bail("Delivery remote state is not ACCEPTED in on_accepted.")
            return
        # When sender knows that receiver has accepted, we settle.
        # That's two-ack.
        dlv.settle()
        self.n_accepted += 1
        if self.n_accepted >= self.n_messages :
            # Success!
            self.bail(None)

    # Receiver Side ================================================
    def on_message(self, event) :
        if self.bailing :
            return
        dlv = event.delivery
        dlv.update(Delivery.ACCEPTED)
        if event.message.body != self.n_received :
            self.bail("out-of-order message")
            return
        self.n_received += 1
        if self.tmp_dlv is None :
            self.tmp_dlv = dlv

    # We have no way, on receiver side, of tracking when sender settles.
    # See PROTON-395 .


class MessageAnnotations(MessagingHandler):
    def __init__(self, addr) :
        super(MessageAnnotations, self).__init__()
        self.addr       = addr
        self.test_timer = None
        self.sender     = None
        self.receiver   = None
        self.n_sent     = 0
        self.n_received = 0
        self.bailing    = False

    def run(self) :
        Container(self).run()

    def bail(self, travail):
        self.bailing = True
        self.error = travail
        if self.error:
            print("message annotations failure: %s" % self.error, flush=True)
        self.send_conn.close()
        self.recv_conn.close()
        self.test_timer.cancel()

    def timeout(self):
        self.bail("Timeout Expired")

    def on_start(self, event):
        self.send_conn = event.container.connect(self.addr)
        self.recv_conn = event.container.connect(self.addr)

        self.sender      = event.container.create_sender(self.send_conn, self.addr)
        self.receiver    = event.container.create_receiver(self.recv_conn, self.addr)
        self.test_timer  = event.reactor.schedule(TIMEOUT, TestTimeout(self))

    def on_sendable(self, event) :
        if self.n_sent == 5:
            return

        # No added annotations.
        msg = Message(body="Message [0]")
        self.n_sent += 1
        self.sender.send(msg)

        # Add an annotation.
        msg = Message(body="Message [1]")
        self.n_sent += 1
        msg.annotations = {'x-opt-qd.ingress':
                           'i_changed_the_annotation'}
        self.sender.send(msg)

        # Supply a trace list.
        msg = Message(body="Message [2]")
        self.n_sent += 1
        msg.annotations = {'x-opt-qd.trace': ['0/first-hop']}
        self.sender.send(msg)

        # ensure obsolete MA are not propagated
        msg = Message(body="Message [3]")
        self.n_sent += 1
        msg.annotations = {'userA': 'A',
                           'userB': 'B',
                           'x-opt-qd.class': 9,
                           'x-opt-qd.': 'X'}
        self.sender.send(msg)

        # supply a phase and stream
        msg = Message(body="Message [4]")
        self.n_sent += 1
        msg.annotations = {'x-opt-qd.phase': 7,
                           'x-opt-qd.stream': 1}
        self.sender.send(msg)

    def on_message(self, event) :
        ingress_router_name = '0/QDR'
        self.n_received += 1

        annotations = event.message.annotations
        body = event.message.body

        if body == "Message[0]":
            ingress = annotations.get('x-opt-qd.ingress')
            if ingress != ingress_router_name:
                self.bail('Msg[0] bad ingress router value: %s' % annotations)
                return
            trace = annotations.get('x-opt-qd.trace')
            if trace != [ingress_router_name]:
                self.bail('Msg[0] bad ingress trace value: %s' % annotations)
                return

        elif body == "Message[1]":
            ingress = annotations.get('x-opt-qd.ingress')
            if ingress != 'i_changed_the_annotation':
                self.bail('Msg[1] bad ingress router value: %s' % annotations)
                return

        elif body == "Message[2]":
            trace = annotations.get('x-opt-qd.trace')
            if trace != ['0/first-hop', ingress_router_name]:
                self.bail('Msg[2] bad ingress trace value: %s' % annotations)
                return

        elif body == "Message[3]":
            if 'x-opt-qd.class' in annotations:
                self.bail('Msg[3] unexpected class value: %s' % annotations)
                return
            if 'x-opt-qd.' in annotations:
                self.bail('Msg[3] unexpected null value: %s' % annotations)
                return
            if (annotations.get('userA') != 'A' or annotations.get('userB') != 'B'):
                self.bail('Msg[3] unexpected user values: %s' % annotations)
                return

        elif body == "Message[4]":
            if annotations.get('phase') != 7:
                self.bail('Msg[4] unexpected phase: %s' % annotations)
                return
            if annotations.get('stream') != 2:
                self.bail('Msg[4] unexpected streaming: %s' % annotations)
                return

        if self.n_received == 5:
            self.bail(None)


class BadMessageAnnotations(MessagingHandler):
    """
    Ensure the router can handle incorrectly formatted message annotations
    """
    def __init__(self, addr) :
        super(BadMessageAnnotations, self).__init__(auto_accept=False)
        self.addr       = addr
        self.test_timer = None
        self.sender     = None
        self.receiver   = None
        self.n_sent     = 0
        self.n_received = 0
        self.bailing    = False
        self.messages = [
            Message(body="Bad Message 1",
                    annotations={'x-opt-qd.to': 3.145}),
            Message(body="Bad Message 2",
                    annotations={'x-opt-qd.trace': 'stringy'}),
            Message(body="Bad Message 3",
                    annotations={'x-opt-qd.trace': ['0/rA', '0/rB', 12, '0/rC']}),
            Message(body="Bad Message 4",
                    annotations={'x-opt-qd.ingress': ['0/rA']}),
            Message(body="Good Message",
                    annotations={'key': ['value']})]

    def run(self) :
        Container(self).run()

    def bail(self, travail):
        self.bailing = True
        self.error = travail
        if self.error:
            print("test failure: %s" % self.error, flush=True)
        self.send_conn.close()
        self.recv_conn.close()
        self.test_timer.cancel()

    def timeout(self):
        self.bail("Timeout Expired")

    def on_start(self, event):
        self.send_conn = event.container.connect(self.addr)
        self.recv_conn = event.container.connect(self.addr)

        self.sender      = event.container.create_sender(self.send_conn, self.addr)
        self.receiver    = event.container.create_receiver(self.recv_conn, self.addr)
        self.test_timer  = event.reactor.schedule(TIMEOUT, TestTimeout(self))

    def on_sendable(self, event) :
        if self.n_sent == len(self.messages):
            return

        self.sender.send(self.messages[self.n_sent])
        self.n_sent += 1

    def on_message(self, event) :
        self.n_received += 1
        if event.message.body != "Good Message":
            self.bail("Got a bad message: %s" % event.message)
            return
        if event.message.annotations.get('key') != ['value']:
            self.bail("Got unexpected user annotations: %s" % event.message.annotations)
            return
        event.delivery.update(Delivery.ACCEPTED)
        event.delivery.settle()

        if self.n_received == len(self.messages):
            self.bail(None)

    def on_rejected(self, event):
        self.n_received += 1
        rc = event.delivery.remote.condition
        if rc.name != "amqp:invalid-field":
            self.bail("Unexpected rejection: %s:%s" % (rc.name,
                                                       rc.description))
            return
        if self.n_received == len(self.messages):
            self.bail(None)


class StripMessageAnnotationsCustom (MessagingHandler) :
    def __init__(self,
                 addr,
                 n_messages
                 ) :
        super(StripMessageAnnotationsCustom, self) . __init__(prefetch=n_messages)
        self.addr        = addr
        self.n_messages  = n_messages

        self.test_timer  = None
        self.sender      = None
        self.receiver    = None
        self.n_sent      = 0
        self.n_received  = 0

    def run(self) :
        Container(self).run()

    def bail(self, travail) :
        self.bailing = True
        self.error = travail
        self.send_conn.close()
        self.recv_conn.close()
        self.test_timer.cancel()

    def timeout(self):
        self.bail("Timeout Expired")

    def on_start(self, event):
        self.send_conn = event.container.connect(self.addr)
        self.recv_conn = event.container.connect(self.addr)

        self.sender      = event.container.create_sender(self.send_conn, self.addr)
        self.receiver    = event.container.create_receiver(self.recv_conn, self.addr)
        self.test_timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))

    def on_sendable(self, event) :
        while self.n_sent < self.n_messages :
            if event.sender.credit < 1 :
                break
            msg = Message(body=self.n_sent)
            self.n_sent += 1
            msg.annotations = {'custom-annotation' : '1/Custom_Annotation'}

            self.sender.send(msg)

    def on_message(self, event) :
        self.n_received += 1
        if 'custom-annotation' not in event.message.annotations :
            self.bail('custom annotation not found')
            return
        if event.message.annotations['custom-annotation'] != '1/Custom_Annotation' :
            self.bail('custom annotation bad value')
            return
        if self.n_received >= self.n_messages :
            # success
            self.bail(None)


class StripMessageAnnotationsNo (MessagingHandler) :
    def __init__(self,
                 addr,
                 n_messages
                 ) :
        super(StripMessageAnnotationsNo, self) . __init__(prefetch=n_messages)
        self.addr        = addr
        self.n_messages  = n_messages

        self.test_timer  = None
        self.sender      = None
        self.receiver    = None
        self.n_sent      = 0
        self.n_received  = 0

    def run(self) :
        Container(self).run()

    def bail(self, travail) :
        self.bailing = True
        self.error = travail
        self.send_conn.close()
        self.recv_conn.close()
        self.test_timer.cancel()

    def timeout(self):
        self.bail("Timeout Expired")

    def on_start(self, event):
        self.send_conn = event.container.connect(self.addr)
        self.recv_conn = event.container.connect(self.addr)

        self.sender      = event.container.create_sender(self.send_conn, self.addr)
        self.receiver    = event.container.create_receiver(self.recv_conn, self.addr)
        self.test_timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))

    def on_sendable(self, event) :
        while self.n_sent < self.n_messages :
            if event.sender.credit < 1 :
                break
            msg = Message(body=self.n_sent)
            self.n_sent += 1
            # This test has no added annotations.
            # The receiver should get the expected standard annotations anyway,
            # because the address we are using has 'stripAnnotations' set to 'no'.
            msg.annotations = {}
            self.sender.send(msg)

    def on_message(self, event) :
        self.n_received += 1

        if event.message.annotations['x-opt-qd.ingress'] != '0/QDR' :
            self.bail("x-opt-qd.ingress annotation has been stripped!")
            return

        if event.message.annotations['x-opt-qd.trace'] != ['0/QDR'] :
            self.bail("x-opt-qd.trace annotations has been stripped!")
            return

        if self.n_received >= self.n_messages :
            # success
            self.bail(None)


class StripMessageAnnotationsNoAddTrace (MessagingHandler) :
    def __init__(self,
                 addr,
                 n_messages
                 ) :
        super(StripMessageAnnotationsNoAddTrace, self) . __init__(prefetch=n_messages)
        self.addr        = addr
        self.n_messages  = n_messages

        self.test_timer  = None
        self.sender      = None
        self.receiver    = None
        self.n_sent      = 0
        self.n_received  = 0

    def run(self) :
        Container(self).run()

    def bail(self, travail) :
        self.bailing = True
        self.error = travail
        self.send_conn.close()
        self.recv_conn.close()
        self.test_timer.cancel()

    def timeout(self):
        self.bail("Timeout Expired")

    def on_start(self, event):
        self.send_conn = event.container.connect(self.addr)
        self.recv_conn = event.container.connect(self.addr)

        self.sender      = event.container.create_sender(self.send_conn, self.addr)
        self.receiver    = event.container.create_receiver(self.recv_conn, self.addr)
        self.test_timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))

    def on_sendable(self, event) :
        while self.n_sent < self.n_messages :
            if event.sender.credit < 1 :
                break
            msg = Message(body=self.n_sent)
            annotations = {'Canis_meus' : 'id_comedit',
                           'x-opt-qd.ingress': 'ingress-router',
                           'x-opt-qd.trace': ['0/QDR.1']
                           }
            self.n_sent += 1
            # This test has no added annotations.
            # The receiver should get the expected standard annotations anyway,
            # because the address we are using has 'stripAnnotations' set to 'no'.
            msg.annotations = annotations
            self.sender.send(msg)

    def on_message(self, event) :
        self.n_received += 1

        notes = event.message.annotations

        if not isinstance(notes, dict):
            self.bail("annotations are not a dictionary")
            return

        # No annotations should get stripped -- neither the
        # ones that the router adds, not the custome one that
        # I added.
        if 'x-opt-qd.ingress' not in notes :
            self.bail('x-opt-qd.ingress annotation missing')
            return
        if 'x-opt-qd.trace' not in notes :
            self.bail('x-opt-qd.trace annotation missing')
            return
        if 'Canis_meus' not in notes :
            self.bail('Canis_meus annotation missing')
            return

        if notes['x-opt-qd.ingress'] != 'ingress-router' :
            self.bail("x-opt-qd.ingress bad value")
            return
        if notes['x-opt-qd.trace'] != ['0/QDR.1', '0/QDR'] :
            self.bail("x-opt-qd.trace bad value")
            return
        if notes['Canis_meus'] != 'id_comedit' :
            self.bail("Canis_meus bad value")
            return

        if self.n_received >= self.n_messages :
            # success
            self.bail(None)


class StripMessageAnnotationsBoth (MessagingHandler) :
    def __init__(self,
                 addr,
                 n_messages
                 ) :
        super(StripMessageAnnotationsBoth, self) . __init__(prefetch=n_messages)
        self.addr        = addr
        self.n_messages  = n_messages

        self.test_timer  = None
        self.sender      = None
        self.receiver    = None
        self.n_sent      = 0
        self.n_received  = 0

    def run(self) :
        Container(self).run()

    def bail(self, travail) :
        self.bailing = True
        self.error = travail
        self.send_conn.close()
        self.recv_conn.close()
        self.test_timer.cancel()

    def timeout(self):
        self.bail("Timeout Expired")

    def on_start(self, event):
        self.send_conn = event.container.connect(self.addr)
        self.recv_conn = event.container.connect(self.addr)

        self.sender      = event.container.create_sender(self.send_conn, self.addr)
        self.receiver    = event.container.create_receiver(self.recv_conn, self.addr)
        self.test_timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))

    def on_sendable(self, event) :
        while self.n_sent < self.n_messages :
            if event.sender.credit < 1 :
                break
            msg = Message(body=self.n_sent)
            annotations = {'Canis_meus': 'id_comedit',
                           'x-opt-qd.ingress': 'ingress-router',
                           'x-opt-qd.trace': ['0/QDR.1'],
                           }
            self.n_sent += 1
            # This test has no added annotations.
            # The receiver should get the expected standard annotations anyway,
            # because the address we are using has 'stripAnnotations' set to 'no'.
            msg.annotations = annotations
            self.sender.send(msg)

    def on_message(self, event) :
        self.n_received += 1

        # The annotations that the router adds should get stripped,
        # but not the custom one that I added.
        notes = event.message.annotations
        if 'x-opt-qd.ingress' in notes :
            self.bail('x-opt-qd.ingress annotation not stripped')
            return
        if 'x-opt-qd.trace' in notes :
            self.bail('x-opt-qd.trace annotation not stripped')
            return
        if 'Canis_meus' not in notes :
            self.bail('Canis_meus annotation missing')
            return

        if notes['Canis_meus'] != 'id_comedit' :
            self.bail("Canis_meus bad value")
            return

        if self.n_received >= self.n_messages :
            # success
            self.bail(None)


class StripMessageAnnotationsOut (MessagingHandler) :
    def __init__(self,
                 addr,
                 n_messages
                 ) :
        super(StripMessageAnnotationsOut, self) . __init__(prefetch=n_messages)
        self.addr        = addr
        self.n_messages  = n_messages

        self.test_timer  = None
        self.sender      = None
        self.receiver    = None
        self.n_sent      = 0
        self.n_received  = 0

    def run(self) :
        Container(self).run()

    def bail(self, travail) :
        self.bailing = True
        self.error = travail
        self.send_conn.close()
        self.recv_conn.close()
        self.test_timer.cancel()

    def timeout(self):
        self.bail("Timeout Expired")

    def on_start(self, event):
        self.send_conn = event.container.connect(self.addr)
        self.recv_conn = event.container.connect(self.addr)

        self.sender      = event.container.create_sender(self.send_conn, self.addr)
        self.receiver    = event.container.create_receiver(self.recv_conn, self.addr)
        self.test_timer  = event.reactor.schedule(TIMEOUT, TestTimeout(self))

    def on_sendable(self, event) :
        while self.n_sent < self.n_messages :
            if event.sender.credit < 1 :
                break
            msg = Message(body=self.n_sent)
            self.n_sent += 1
            # This test has no added annotations.
            # The receiver should get the expected standard annotations anyway,
            # because the address we are using has 'stripAnnotations' set to 'no'.
            self.sender.send(msg)

    def on_message(self, event) :
        self.n_received += 1

        # The annotations that the router routinely adds
        # should all get stripped,
        if event.message.annotations is not None :
            self.bail("An annotation was not stripped in egress message.")
            return

        if self.n_received >= self.n_messages :
            # success
            self.bail(None)


class StripMessageAnnotationsIn (MessagingHandler) :
    def __init__(self,
                 addr,
                 n_messages
                 ) :
        super(StripMessageAnnotationsIn, self) . __init__(prefetch=n_messages)
        self.addr        = addr
        self.n_messages  = n_messages

        self.test_timer  = None
        self.sender      = None
        self.receiver    = None
        self.n_sent      = 0
        self.n_received  = 0

    def run(self) :
        Container(self).run()

    def bail(self, travail) :
        self.bailing = True
        self.error = travail
        self.send_conn.close()
        self.recv_conn.close()
        self.test_timer.cancel()

    def timeout(self):
        self.bail("Timeout Expired")

    def on_start(self, event):
        self.send_conn = event.container.connect(self.addr)
        self.recv_conn = event.container.connect(self.addr)

        self.sender      = event.container.create_sender(self.send_conn, self.addr)
        self.receiver    = event.container.create_receiver(self.recv_conn, self.addr)
        self.test_timer  = event.reactor.schedule(TIMEOUT, TestTimeout(self))

    def on_sendable(self, event) :
        while self.n_sent < self.n_messages :
            if event.sender.credit < 1 :
                break
            msg = Message(body=self.n_sent)
            # Attach some standard annotations to the message.
            # These are ingress annotations, and should get stripped.
            # These annotation-keys will then get values assigned by
            # the router.
            notes = {'x-opt-qd.ingress': 'ingress-router', 'x-opt-qd.trace': ['0/QDR.1']}
            self.sender.send(msg)
            self.n_sent += 1

    def on_message(self, event) :
        self.n_received += 1

        if event.message.annotations['x-opt-qd.ingress'] == 'ingress-router' :
            self.bail("x-opt-qd.ingress value was not stripped.")
            return

        if event.message.annotations['x-opt-qd.trace'] == ['0/QDR.1'] :
            self.bail("x-opt-qd.trace value was not stripped.")
            return

        if self.n_received >= self.n_messages :
            # success
            self.bail(None)


HELLO_WORLD = "Hello World!"


class SndSettleModeTest(MessagingHandler):
    def __init__(self, address):
        super(SndSettleModeTest, self).__init__()
        self.address = address
        self.sender = None
        self.receiver = None
        self.message_received = False
        self.delivery_already_settled = False

    def on_start(self, event):
        conn = event.container.connect(self.address)
        # The receiver sets link.snd_settle_mode = Link.SND_SETTLED. It wants to receive settled messages
        self.receiver = event.container.create_receiver(conn, "org/apache/dev", options=AtMostOnce())

        # With AtLeastOnce, the sender will not settle.
        self.sender = event.container.create_sender(conn, "org/apache/dev", options=AtLeastOnce())

    def on_sendable(self, event):
        msg = Message(body=HELLO_WORLD)
        event.sender.send(msg)
        event.sender.close()

    def on_message(self, event):
        self.delivery_already_settled = event.delivery.settled
        if HELLO_WORLD == event.message.body:
            self.message_received = True
        else:
            self.message_received = False
        event.connection.close()

    def run(self):
        Container(self).run()


class ExcessDeliveriesReleasedTest(MessagingHandler):
    def __init__(self, address):
        super(ExcessDeliveriesReleasedTest, self).__init__(prefetch=0)
        self.address = address
        self.dest = "closest.EDRtest"
        self.error = None
        self.sender = None
        self.receiver = None
        self.n_sent     = 0
        self.n_received = 0
        self.n_accepted = 0
        self.n_released = 0

    def on_start(self, event):
        conn = event.container.connect(self.address)
        self.sender   = event.container.create_sender(conn, self.dest)
        self.receiver = event.container.create_receiver(conn, self.dest)
        self.receiver.flow(6)

    def on_sendable(self, event):
        for i in range(10 - self.n_sent):
            msg = Message(body=i)
            event.sender.send(msg)
            self.n_sent += 1

    def on_accepted(self, event):
        self.n_accepted += 1

    def on_released(self, event):
        self.n_released += 1
        if self.n_released == 4:
            if self.n_accepted != 6:
                self.error = "Expected 6 accepted, got %d" % self.n_accepted
            if self.n_received != 6:
                self.error = "Expected 6 received, got %d" % self.n_received

            event.connection.close()

    def on_message(self, event):
        self.n_received += 1
        if self.n_received == 6:
            self.receiver.close()

    def run(self):
        Container(self).run()


class UnavailableBase(MessagingHandler):
    def __init__(self, address):
        super(UnavailableBase, self).__init__()
        self.address = address
        self.dest = "unavailable"
        self.conn = None
        self.sender = None
        self.receiver = None
        self.link_error = False
        self.link_closed = False
        self.passed = False
        self.timer = None
        self.link_name = "test_link"

    def check_if_done(self):
        if self.link_error and self.link_closed:
            self.passed = True
            self.conn.close()
            self.timer.cancel()

    def on_link_error(self, event):
        link = event.link
        if event.link.name == self.link_name and link.remote_condition.description \
                == "Node not found":
            self.link_error = True
        self.check_if_done()

    def on_link_remote_close(self, event):
        if event.link.name == self.link_name:
            self.link_closed = True
            self.check_if_done()

    def run(self):
        Container(self).run()


class UnavailableSender(UnavailableBase):
    def __init__(self, address):
        super(UnavailableSender, self).__init__(address)

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.conn = event.container.connect(self.address)
        # Creating a sender to an address with unavailable distribution
        # The router will not allow this link to be established. It will close the link with an error of
        # "Node not found"
        self.sender = event.container.create_sender(self.conn, self.dest, name=self.link_name)


class UnavailableReceiver(UnavailableBase):
    def __init__(self, address):
        super(UnavailableReceiver, self).__init__(address)

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.conn = event.container.connect(self.address)
        # Creating a receiver to an address with unavailable distribution
        # The router will not allow this link to be established. It will close the link with an error of
        # "Node not found"
        self.receiver = event.container.create_receiver(self.conn, self.dest, name=self.link_name)


class MulticastUnsettledTest(MessagingHandler):
    """
    Send N unsettled multicast messages to 2 receivers.  Ensure sender is
    notified of settlement and disposition changes from the receivers.
    """

    def __init__(self, address):
        super(MulticastUnsettledTest, self).__init__(auto_accept=False, prefetch=0)
        self.address = address
        self.dest = "multicast.MUtest"
        self.error = None
        self.count      = 10
        self.n_sent     = 0
        self.n_received = 0
        self.n_accepted = 0
        self.n_receivers = 0

    def check_if_done(self):
        if self.n_received == self.count * 2 and self.n_accepted == self.count:
            self.timer.cancel()
            self.conn.close()

    def timeout(self):
        self.error = "Timeout Expired: sent=%d, received=%d, accepted=%d" % (self.n_sent, self.n_received, self.n_accepted)
        self.conn.close()

    def on_start(self, event):
        self.timer     = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.conn      = event.container.connect(self.address)
        self.receiver1 = event.container.create_receiver(self.conn, self.dest,
                                                         name="A",
                                                         options=AtLeastOnce())
        self.receiver2 = event.container.create_receiver(self.conn, self.dest,
                                                         name="B",
                                                         options=AtLeastOnce())
        self.receiver1.flow(self.count)
        self.receiver2.flow(self.count)

    def on_link_opened(self, event):
        if event.receiver:
            self.n_receivers += 1
            # start the sender once all receivers links are up
            if self.n_receivers == 2:
                self.sender = event.container.create_sender(self.conn, self.dest,
                                                            options=AtLeastOnce())

    def on_sendable(self, event):
        for i in range(self.count - self.n_sent):
            msg = Message(body=i)
            event.sender.send(msg)
            self.n_sent += 1

    def on_accepted(self, event):
        self.n_accepted += 1
        self.check_if_done()

    def on_message(self, event):
        if event.delivery.settled:
            self.error = "Received settled delivery"
        event.delivery.update(Delivery.ACCEPTED)
        event.delivery.settle()
        self.n_received += 1
        self.check_if_done()

    def run(self):
        Container(self).run()


class LargeMessageStreamCloseConnTest(MessagingHandler):
    def __init__(self, address):
        super(LargeMessageStreamCloseConnTest, self).__init__()
        self.address = address
        self.dest = "LargeMessageStreamCloseConnTest"
        self.error = None
        self.timer = None
        self.sender_conn = None
        self.receiver_conn = None
        self.sender = None
        self.receiver = None
        self.body = ""
        self.aborted = False
        for i in range(20000):
            self.body += "0123456789101112131415"

    def timeout(self):
        if self.aborted:
            self.error = "Message has been aborted. Test failed"
        else:
            self.error = "Message not received. test failed"
        self.receiver_conn.close()

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.sender_conn = event.container.connect(self.address)
        self.receiver_conn = event.container.connect(self.address)
        self.sender = event.container.create_sender(self.sender_conn, self.dest)
        self.receiver = event.container.create_receiver(self.receiver_conn,
                                                        self.dest, name="A")

    def on_sendable(self, event):
        msg = Message(body=self.body)
        # send(msg) calls the stream function which streams data
        # from sender to the router
        event.sender.send(msg)

        # Close the connection immediately after sending the message
        # Without the fix for DISPATCH-1085, this test will fail
        # one in five times with an abort
        # With the fix in place, this test will never fail (the
        # on_aborted will never be called).
        self.sender_conn.close()

    def on_message(self, event):
        self.timer.cancel()
        self.receiver_conn.close()

    def on_aborted(self, event):
        self.aborted = True
        self.timer.cancel()
        self.timeout()

    def run(self):
        Container(self).run()


class LargeMessageStreamTest(MessagingHandler):
    def __init__(self, address):
        super(LargeMessageStreamTest, self).__init__()
        self.address = address
        self.dest = "LargeMessageStreamTest"
        self.error = None
        self.count = 10
        self.n_sent = 0
        self.timer = None
        self.conn = None
        self.sender = None
        self.receiver = None
        self.n_received = 0
        self.body = ""
        for i in range(10000):
            self.body += "0123456789101112131415"

    def check_if_done(self):
        if self.n_received == self.count:
            self.timer.cancel()
            self.conn.close()

    def timeout(self):
        self.error = "Timeout Expired: sent=%d, received=%d" % (self.n_sent, self.n_received)
        self.conn.close()

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.conn = event.container.connect(self.address)
        self.sender = event.container.create_sender(self.conn, self.dest)
        self.receiver = event.container.create_receiver(self.conn, self.dest, name="A")
        self.receiver.flow(self.count)

    def on_sendable(self, event):
        for i in range(self.count):
            msg = Message(body=self.body)
            # send(msg) calls the stream function which streams data from sender to the router
            event.sender.send(msg)
            self.n_sent += 1

    def on_message(self, event):
        self.n_received += 1
        self.check_if_done()

    def run(self):
        Container(self).run()


class MultiframePresettledTest(MessagingHandler):
    def __init__(self, address):
        super(MultiframePresettledTest, self).__init__(prefetch=0)
        self.address = address
        self.dest = "closest.MFPtest"
        self.error = None
        self.count      = 10
        self.n_sent     = 0
        self.n_received = 0

        self.body = ""
        for i in range(10000):
            self.body += "0123456789"

    def check_if_done(self):
        if self.n_received == self.count:
            self.timer.cancel()
            self.conn.close()

    def timeout(self):
        self.error = "Timeout Expired: sent=%d, received=%d" % (self.n_sent, self.n_received)
        self.conn.close()

    def on_start(self, event):
        self.timer     = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.conn      = event.container.connect(self.address)
        self.sender    = event.container.create_sender(self.conn, self.dest)
        self.receiver  = event.container.create_receiver(self.conn, self.dest, name="A")
        self.receiver.flow(self.count)

    def on_sendable(self, event):
        for i in range(self.count - self.n_sent):
            msg = Message(body=self.body)
            dlv = event.sender.send(msg)
            dlv.settle()
            self.n_sent += 1

    def on_message(self, event):
        if not event.delivery.settled:
            self.error = "Received unsettled delivery"
        self.n_received += 1
        self.check_if_done()

    def run(self):
        Container(self).run()


class UptimeLastDlvChecker:
    def __init__(self, parent, lastDlv=None, uptime=0):
        self.parent = parent
        self.uptime = uptime
        self.lastDlv = lastDlv
        self.expected_num_connections = 2
        self.num_connections = 0

    def on_timer_task(self, event):
        local_node = Node.connect(self.parent.address, timeout=TIMEOUT)
        result = local_node.query('org.apache.qpid.dispatch.connection')
        container_id_index = result.attribute_names.index('container')
        uptime_seconds_index = result.attribute_names.index('uptimeSeconds')
        last_dlv_seconds_index = result.attribute_names.index('lastDlvSeconds')
        for res in result.results:
            container_id = res[container_id_index]

            # We only care if the container_id is "UPTIME-TEST"
            if container_id == self.parent.container_id:
                uptime_seconds = res[uptime_seconds_index]
                if self.uptime != 0 and uptime_seconds < self.uptime:
                    self.parent.error = "The connection uptime should be greater than or equal to %d seconds but instead is %d seconds" % (self.uptime, uptime_seconds)
                last_dlv_seconds = res[last_dlv_seconds_index]
                if self.lastDlv is None:
                    if last_dlv_seconds is not None:
                        self.parent.error = "Expected lastDlvSeconds to be empty"
                else:
                    if not last_dlv_seconds >= self.lastDlv:
                        self.parent.error = "Connection lastDeliverySeconds must be greater than or equal to %d but is %d" % (self.lastDlv, last_dlv_seconds)
                    else:
                        self.parent.success = True
                self.num_connections += 1

        if self.expected_num_connections != self.num_connections:
            self.parent.error = "Number of client connections expected=%d, but got %d" % (self.expected_num_connections, self.num_connections)

        self.parent.cancel_custom()


class ConnectionUptimeLastDlvTest(MessagingHandler):
    def __init__(self, address, dest):
        super(ConnectionUptimeLastDlvTest, self).__init__()
        self.timer = None
        self.sender_conn = None
        self.receiver_conn = None
        self.address = address
        self.sender = None
        self.receiver = None
        self.error = None
        self.custom_timer = None
        self.container_id = "UPTIME-TEST"
        self.dest = dest
        self.reactor = None
        self.success = False
        self.receiver_link_opened = False
        self.sender_link_opened = False
        self.custom_timer_created = False

    def cancel_custom(self):
        self.custom_timer.cancel()
        if self.error or self.success:
            self.timer.cancel()
            self.sender_conn.close()
            self.receiver_conn.close()
        else:
            msg = Message(body=self.container_id)
            self.sender.send(msg)

            # We have now sent a message that the router must have sent to the
            # receiver. We will wait for 2 seconds and once again check
            # uptime and lastDlv.
            # Allow for some slop in the calculation of uptime and last delivery:
            # * reactor.schedule needs leeway in calculating the time delta and delivering the callback
            # * dispatch needs leeway rounding stats to whole seconds
            self.custom_timer = self.reactor.schedule(2, UptimeLastDlvChecker(self, uptime=2, lastDlv=1))

    def timeout(self):
        self.error = "Timeout Expired:, Test took too long to execute. "
        self.sender_conn.close()
        self.receiver_conn.close()

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))

        # Create separate sender and receiver connections.
        self.sender_conn = event.container.connect(self.address)
        self.receiver_conn = event.container.connect(self.address)

        # Let's create a sender and receiver but not send any messages.
        self.sender = event.container.create_sender(self.sender_conn, self.dest)
        self.receiver = event.container.create_receiver(self.receiver_conn, self.dest)

        # Execute a management query for connections after 1 second
        # This will help us check the uptime and lastDlv time
        # No deliveries were sent on any link yet, so the lastDlv must be "-"
        self.reactor = event.reactor

    def on_link_opened(self, event):
        # Schedule the UptimeLastDlvChecker only after the sender and
        # receiver links have been opened on those connections. This
        # will help the test pass 100% of the time in slow systems.
        if self.receiver == event.receiver:
            self.receiver_link_opened = True
        if event.sender == self.sender:
            self.sender_link_opened = True

        if self.receiver_link_opened and self.sender_link_opened:
            if not self.custom_timer_created:
                self.custom_timer_created = True
                self.custom_timer = event.reactor.schedule(1, UptimeLastDlvChecker(self, uptime=1,
                                                                                   lastDlv=None))

    def run(self):
        container = Container(self)
        container.container_id = self.container_id
        container.run()


class AnonymousSenderNoRecvLargeMessagedTest(MessagingHandler):
    def __init__(self, address):
        super(AnonymousSenderNoRecvLargeMessagedTest, self).__init__(auto_accept=False)
        self.timer = None
        self.conn = None
        self.sender = None
        self.address = address
        self.released = False
        self.error = None
        self.body = ""
        for i in range(20000):
            self.body += "0123456789101112131415"

    def timeout(self):
        self.error = "Timeout Expired:, delivery not released. "
        self.conn.close()

    def check_if_done(self):
        if self.released:
            self.sender.close()
            self.conn.close()
            self.timer.cancel()

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.conn = event.container.connect(self.address)
        # This sender is an anonymous sender
        self.sender = event.container.create_sender(self.conn)

    def on_sendable(self, event):
        msg = Message(body=self.body, address="someaddress")
        # send(msg) calls the stream function which streams data from sender to the router
        event.sender.send(msg)

    def on_released(self, event):
        self.released = True
        self.check_if_done()

    def run(self):
        Container(self).run()


class ReleasedVsModifiedTest(MessagingHandler):
    def __init__(self, address):
        super(ReleasedVsModifiedTest, self).__init__(prefetch=0, auto_accept=False)
        self.address = address
        self.dest = "closest.RVMtest"
        self.error = None
        self.count      = 10
        self.accept     = 6
        self.n_sent     = 0
        self.n_received = 0
        self.n_released = 0
        self.n_modified = 0
        self.node_modified_at_start = 0

    def get_modified_deliveries(self) :
        local_node = Node.connect(self.address, timeout=TIMEOUT)
        outs = local_node.query(type='org.apache.qpid.dispatch.router')
        pos = outs.attribute_names.index("modifiedDeliveries")
        results = outs.results[0]
        n_modified_deliveries = results[pos]
        return n_modified_deliveries

    def check_if_done(self):
        if self.n_received == self.accept and self.n_released == self.count - self.accept and self.n_modified == self.accept:
            node_modified_now = self.get_modified_deliveries()
            this_test_modified_deliveries = node_modified_now - self.node_modified_at_start
            if this_test_modified_deliveries == self.accept:
                self.timer.cancel()
                self.conn.close()

    def timeout(self):
        self.error = "Timeout Expired: sent=%d, received=%d, released=%d, modified=%d" % \
                     (self.n_sent, self.n_received, self.n_released, self.n_modified)
        self.conn.close()

    def on_start(self, event):
        self.timer     = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.conn      = event.container.connect(self.address)
        self.sender    = event.container.create_sender(self.conn, self.dest)
        self.receiver  = event.container.create_receiver(self.conn, self.dest, name="A")
        self.receiver.flow(self.accept)
        self.node_modified_at_start = self.get_modified_deliveries()

    def on_sendable(self, event):
        for i in range(self.count - self.n_sent):
            msg = Message(body="RvM-Test")
            event.sender.send(msg)
            self.n_sent += 1

    def on_message(self, event):
        self.n_received += 1
        if self.n_received == self.accept:
            self.receiver.close()

    def on_released(self, event):
        if event.delivery.remote_state == Delivery.MODIFIED:
            self.n_modified += 1
        else:
            self.n_released += 1
        self.check_if_done()

    def run(self):
        Container(self).run()


class AppearanceOfBalanceTest(MessagingHandler):
    def __init__(self, address):
        super(AppearanceOfBalanceTest, self).__init__()
        self.address = address
        self.dest = "balanced.AppearanceTest"
        self.error = None
        self.count        = 9
        self.n_sent       = 0
        self.n_received_a = 0
        self.n_received_b = 0
        self.n_received_c = 0

    def check_if_done(self):
        if self.n_received_a + self.n_received_b + self.n_received_c == self.count:
            if self.n_received_a != 3 or self.n_received_b != 3 or self.n_received_c != 3:
                self.error = "Incorrect Distribution: %d/%d/%d" % (self.n_received_a, self.n_received_b, self.n_received_c)
            self.timer.cancel()
            self.conn.close()

    def timeout(self):
        self.error = "Timeout Expired: sent=%d rcvd=%d/%d/%d" % \
                     (self.n_sent, self.n_received_a, self.n_received_b, self.n_received_c)
        self.conn.close()

    def on_start(self, event):
        self.timer      = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.conn       = event.container.connect(self.address)
        self.sender     = event.container.create_sender(self.conn, self.dest)
        self.receiver_a = event.container.create_receiver(self.conn, self.dest, name="A")
        self.receiver_b = event.container.create_receiver(self.conn, self.dest, name="B")
        self.receiver_c = event.container.create_receiver(self.conn, self.dest, name="C")

    def send(self):
        if self.n_sent < self.count:
            msg = Message(body="Appearance-Test")
            self.sender.send(msg)
            self.n_sent += 1

    def on_sendable(self, event):
        if self.n_sent == 0:
            self.send()

    def on_message(self, event):
        if event.receiver == self.receiver_a:
            self.n_received_a += 1
        if event.receiver == self.receiver_b:
            self.n_received_b += 1
        if event.receiver == self.receiver_c:
            self.n_received_c += 1

    def on_accepted(self, event):
        self.send()
        self.check_if_done()

    def run(self):
        Container(self).run()


class BatchedSettlementTest(MessagingHandler):
    def __init__(self, address):
        super(BatchedSettlementTest, self).__init__(auto_accept=False)
        self.address = address
        self.dest = "balanced.BatchedSettlement"
        self.error = None
        self.count       = 200
        self.batch_count = 20
        self.n_sent      = 0
        self.n_received  = 0
        self.n_settled   = 0
        self.batch       = []
        self.accepted_count_match = False

    def check_if_done(self):
        if self.n_settled == self.count:
            local_node = Node.connect(self.address, timeout=TIMEOUT)
            outs = local_node.query(type='org.apache.qpid.dispatch.router')
            pos = outs.attribute_names.index("acceptedDeliveries")
            results = outs.results[0]
            if results[pos] >= self.count:
                self.accepted_count_match = True

            self.timer.cancel()
            self.conn.close()

    def timeout(self):
        self.error = "Timeout Expired: sent=%d rcvd=%d settled=%d" % \
                     (self.n_sent, self.n_received, self.n_settled)
        self.conn.close()

    def on_start(self, event):
        self.timer    = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.conn     = event.container.connect(self.address)
        self.sender   = event.container.create_sender(self.conn, self.dest)
        self.receiver = event.container.create_receiver(self.conn, self.dest)

    def send(self):
        while self.n_sent < self.count and self.sender.credit > 0:
            msg = Message(body="Batch-Test")
            self.sender.send(msg)
            self.n_sent += 1

    def on_sendable(self, event):
        self.send()

    def on_message(self, event):
        self.n_received += 1
        self.batch.insert(0, event.delivery)
        if len(self.batch) == self.batch_count:
            while len(self.batch) > 0:
                self.accept(self.batch.pop())

    def on_accepted(self, event):
        self.n_settled += 1
        self.check_if_done()

    def run(self):
        Container(self).run()


class RejectCoordinatorTest(MessagingHandler, TransactionHandler):
    def __init__(self, url):
        super(RejectCoordinatorTest, self).__init__(prefetch=0)
        self.url = Url(url)
        self.error = "The router can't coordinate transactions by itself, a linkRoute to a coordinator must be " \
                     "configured to use transactions."
        self.container = None
        self.conn = None
        self.sender = None
        self.timer = None
        self.passed = False
        self.link_error = False
        self.link_remote_close = False

    def timeout(self):
        self.conn.close()

    def check_if_done(self):
        if self.link_remote_close and self.link_error:
            self.passed = True
            self.conn.close()
            self.timer.cancel()

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.container = event.container
        self.conn = self.container.connect(self.url)
        self.sender = self.container.create_sender(self.conn, self.url.path)
        # declare_transaction tries to create a link with name "txn-ctrl" to the
        # transaction coordinator which has its own target, it has no address
        # The router cannot coordinate transactions itself and so there will be a link error when this
        # link is attempted to be created
        self.container.declare_transaction(self.conn, handler=self)

    def on_link_error(self, event):
        link = event.link
        # If the link name is 'txn-ctrl' and there is a link error and it matches self.error, then we know
        # that the router has rejected the link because it cannot coordinate transactions itself
        if link.name == "txn-ctrl" and link.remote_condition.description == self.error and \
                        link.remote_condition.name == 'amqp:precondition-failed':
            self.link_error = True
            self.check_if_done()

    def on_link_remote_close(self, event):
        link = event.link
        if link.name == "txn-ctrl":
            self.link_remote_close = True
            self.check_if_done()

    def run(self):
        Container(self).run()


class RejectDispositionTest(MessagingHandler):
    def __init__(self, address):
        super(RejectDispositionTest, self).__init__(auto_accept=False)
        self.address = address
        self.sent = False
        self.received_error = False
        self.dest = "rejectDispositionTest"
        # explicitly convert to str due to
        # https://issues.apache.org/jira/browse/PROTON-1843
        self.error_description = str('you were out of luck this time!')
        self.error_name = 'amqp:internal-error'
        self.reject_count_match = False
        self.rejects_at_start = 0

    def count_rejects(self) :
        local_node = Node.connect(self.address, timeout=TIMEOUT)
        outs = local_node.query(type='org.apache.qpid.dispatch.router')
        pos = outs.attribute_names.index("rejectedDeliveries")
        results = outs.results[0]
        return results[pos]

    def on_start(self, event):
        conn = event.container.connect(self.address)
        event.container.create_sender(conn, self.dest)
        event.container.create_receiver(conn, self.dest)
        self.rejects_at_start = self.count_rejects()

    def on_sendable(self, event):
        if not self.sent:
            event.sender.send(Message(body="Hello World!"))
            self.sent = True

    def on_rejected(self, event):
        if event.delivery.remote.condition.description == self.error_description \
                and event.delivery.remote.condition.name == self.error_name:
            self.received_error = True
        rejects_now = self.count_rejects()
        rejects_for_this_test = rejects_now - self.rejects_at_start
        if rejects_for_this_test == 1:
            self.reject_count_match = True
        event.connection.close()

    def on_message(self, event):
        event.delivery.local.condition = Condition(self.error_name, self.error_description)
        self.reject(event.delivery)

    def run(self):
        Container(self).run()


class UnsettledLargeMessageTest(MessagingHandler):
    def __init__(self, addr, n_messages):
        super(UnsettledLargeMessageTest, self).__init__()
        self.addr = addr
        self.n_messages = n_messages
        self.sender = None
        self.receiver = None
        self.sender_conn = None
        self.recv_conn = None
        self.n_sent = 0
        self.n_received = 0
        self.error = None
        self.test_timer = None
        self.max_receive = 1
        self.custom_timer = None
        self.timer = None
        self.n_accepted = 0
        self.n_modified = 0
        self.n_released = 0
        self.str1 = "0123456789abcdef"
        self.msg_str = ""
        for i in range(16384):
            self.msg_str += self.str1

    def run(self):
        Container(self).run()

    def check_if_done(self):
        # self.n_accepted + self.n_modified + self.n_released will never
        # equal self.n_messages without the fix for DISPATCH-1197 because
        # the router will never pull the data from the proton buffers once
        # the router hits q2_holdoff
        if self.n_accepted + self.n_modified + \
                self.n_released == self.n_messages:
            self.timer.cancel()
            self.sender_conn.close()

    def timeout(self):
        self.error = "Timeout Expired: sent=%d accepted=%d " \
                     "released=%d modified=%d" % (self.n_messages,
                                                  self.n_accepted,
                                                  self.n_released,
                                                  self.n_modified)

    def on_start(self, event):
        self.sender_conn = event.container.connect(self.addr)
        self.recv_conn = event.container.connect(self.addr)
        self.receiver = event.container.create_receiver(self.recv_conn,
                                                        "test_42")
        self.sender = event.container.create_sender(self.sender_conn,
                                                    "test_42")
        self.timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))

    def on_accepted(self, event):
        self.n_accepted += 1

    def on_released(self, event):
        if event.delivery.remote_state == Delivery.MODIFIED:
            self.n_modified += 1
        else:
            self.n_released += 1

        self.check_if_done()

    def on_sendable(self, event):
        while self.n_sent < self.n_messages:
            msg = Message(id=(self.n_sent + 1),
                          body={'sequence': (self.n_sent + 1),
                                'msg_str': self.msg_str})
            # Presettle the delivery.
            self.sender.send(msg)
            self.n_sent += 1

    def on_message(self, event):
        self.n_received += 1
        if self.n_received == self.max_receive:
            # Close the receiver connection after receiving just one message
            # This will cause the release of multi-frame deliveries.
            # Meanwhile the sender will keep sending but will run into
            # the q2_holodd situation and never recover.
            # The sender link will be stalled
            # This test will NEVER pass without the fix to DISPATCH-1197
            # Receiver bails after receiving max_receive messages.
            self.receiver.close()
            self.recv_conn.close()


class OneRouterUnavailableCoordinatorTest(TestCase):
    @classmethod
    def setUpClass(cls):
        super(OneRouterUnavailableCoordinatorTest, cls).setUpClass()
        name = "test-router"
        OneRouterTest.listen_port = cls.tester.get_port()
        config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'QDR', 'defaultDistribution': 'unavailable'}),
            ('listener', {'port': cls.tester.get_port()}),
            ('address', {'prefix': 'closest', 'distribution': 'closest'}),
            ('address', {'prefix': 'balanced', 'distribution': 'balanced'}),
            ('address', {'prefix': 'multicast', 'distribution': 'multicast'}),
        ])
        cls.router = cls.tester.qdrouterd(name, config)
        cls.router.wait_ready()
        cls.address = cls.router.addresses[0]

    def test_46_coordinator_linkroute_unavailable_DISPATCH_1453(self):
        # The defaultDistribution on the router is unavailable. We try to connect a tx sender
        # to make sure a good detailed message saying "the link route to a coordinator must be
        # configured" is sent back.
        test = RejectCoordinatorGoodMessageTest(self.address)
        test.run()
        self.assertTrue(test.passed)

    def test_47_coordinator_linkroute_available_DISPATCH_1453(self):
        # The defaultDistribution on the router is unavailable. We create a link route with $coordinator address
        # The link route is not attached to any broker. When the attach comes in, the reject message must be
        # condition=:"qd:no-route-to-dest", description="No route to the destination node"
        COORDINATOR = "$coordinator"
        long_type = 'org.apache.qpid.dispatch.router.config.linkRoute'
        qd_manager = QdManager(address=self.address)
        args = {"prefix": COORDINATOR, "connection": "broker", "dir": "in"}
        qd_manager.create(long_type, args)
        link_route_created = False

        # Verify that the link route was created by querying for it.
        outs = qd_manager.query(long_type)[0]
        if outs:
            try:
                if outs['prefix'] == COORDINATOR:
                    link_route_created = True
            except:
                pass

        self.assertTrue(link_route_created)

        # We have verified that the link route has been created but there is no broker connections.
        # Now let's try to open a transaction. We should get a no route to destination error
        test = RejectCoordinatorGoodMessageTest(self.address, link_route_present=True)
        test.run()
        self.assertTrue(test.passed)


class RejectCoordinatorGoodMessageTest(RejectCoordinatorTest):
    def __init__(self, url, link_route_present=False):
        super(RejectCoordinatorGoodMessageTest, self).__init__(url)
        self.link_route_present = link_route_present
        self.error_with_link_route = "No route to the destination node"

    def on_link_error(self, event):
        link = event.link
        # If the link name is 'txn-ctrl' and there is a link error and it matches self.error, then we know
        # that the router has rejected the link because it cannot coordinate transactions itself
        if link.name == "txn-ctrl":
            if self.link_route_present:
                if link.remote_condition.description == self.error_with_link_route and link.remote_condition.name == 'qd:no-route-to-dest':
                    self.link_error = True
            else:
                if link.remote_condition.description == self.error and link.remote_condition.name == 'amqp:precondition-failed':
                    self.link_error = True

            self.check_if_done()

    def run(self):
        Container(self).run()


class Q2HoldoffDropTest(MessagingHandler):
    """
    Create 3 multicast receivers, two which grant 2 credits and one that grants
    only one.  Send enough data to force Q2 holdoff (since one rx is blocked)
    Close the stalled rx connection, verify the remaining receivers get the
    message (Q2 holdoff disabled)
    """

    def __init__(self, router):
        super(Q2HoldoffDropTest, self).__init__(prefetch=0,
                                                auto_accept=False,
                                                auto_settle=False)
        self.router = router
        self.rx_fast1_conn = None
        self.rx_fast1 = None
        self.rx_fast2_conn = None
        self.rx_fast2 = None
        self.rx_slow_conn = None
        self.rx_slow = None
        self.tx_conn = None
        self.tx = None
        self.timer = None
        self.reactor = None
        self.error = None
        self.n_attached = 0
        self.n_rx = 0
        self.n_tx = 0
        self.close_timer = 0

        # currently the router buffer size is 512 bytes and the Q2 holdoff
        # buffer chain high watermark is 256 buffers.  We need to send a
        # message that will be big enough to trigger Q2 holdoff
        self.big_msg = Message(body=["DISPATCH-1330" * (512 * 256 * 4)])

    def done(self):
        if self.timer:
            self.timer.cancel()
        if self.close_timer:
            self.close_timer.cancel()
        if self.tx_conn:
            self.tx_conn.close()
        if self.rx_fast1_conn:
            self.rx_fast1_conn.close()
        if self.rx_fast2_conn:
            self.rx_fast2_conn.close()
        if self.rx_slow_conn:
            self.rx_slow_conn.close()

    def timeout(self):
        self.error = "Timeout Expired"
        self.done()

    def on_start(self, event):
        self.reactor = event.reactor
        self.timer = self.reactor.schedule(TIMEOUT, TestTimeout(self))

        self.rx_slow_conn = event.container.connect(self.router.addresses[0])
        self.rx_fast1_conn = event.container.connect(self.router.addresses[0])
        self.rx_fast2_conn = event.container.connect(self.router.addresses[0])

        self.rx_slow = event.container.create_receiver(self.rx_slow_conn,
                                                       source="multicast.dispatch-1330",
                                                       name="rx_slow")
        self.rx_fast1 = event.container.create_receiver(self.rx_fast1_conn,
                                                        source="multicast.dispatch-1330",
                                                        name="rx_fast1")
        self.rx_fast2 = event.container.create_receiver(self.rx_fast2_conn,
                                                        source="multicast.dispatch-1330",
                                                        name="rx_fast2")

    def on_link_opened(self, event):
        if event.receiver:
            self.n_attached += 1
            if self.n_attached == 3:
                self.rx_fast1.flow(2)
                self.rx_fast2.flow(2)
                self.rx_slow.flow(1)   # stall on 2nd msg

                self.tx_conn = event.container.connect(self.router.addresses[0])
                self.tx = event.container.create_sender(self.tx_conn,
                                                        target="multicast.dispatch-1330",
                                                        name="tx")

    def on_sendable(self, event):
        if self.n_tx == 0:
            # wait until all subscribers present
            self.router.wait_address("multicast.dispatch-1330", subscribers=3)
            for i in range(2):
                dlv = self.tx.send(self.big_msg)
                dlv.settle()
                self.n_tx += 1

    def close_rx_slow(self, event):
        if self.rx_slow_conn:
            self.rx_slow_conn.close()
            self.rx_slow_conn = None
            self.rx_slow = None

    def on_message(self, event):
        self.n_rx += 1
        if self.n_rx == 3:  # first will arrive, second is blocked

            class CloseTimer(Timeout):
                def __init__(self, parent):
                    self.parent = parent

                def on_timer_task(self, event):
                    self.parent.close_rx_slow(event)

            # 2 second wait for Q2 to fill up
            self.close_timer = self.reactor.schedule(2.0, CloseTimer(self))

        if self.n_rx == 5:
            # succesfully received on last two receivers
            self.done()

    def run(self):
        Container(self).run()

        # wait until the router has cleaned up the route table
        clean = False
        while not clean:
            clean = True
            atype = 'org.apache.qpid.dispatch.router.address'
            addrs = self.router.management.query(type=atype).get_dicts()
            if any("dispatch-1330" in a['name'] for a in addrs):
                clean = False
                break
            if not clean:
                sleep(0.1)


class OneRouterTransactionalAttachTest(TestCase):
    """
    Verify that a transaction is properly forwarded through the router
    """

    class FakeTxnBroker(FakeBroker):
        """
        A FakeBroker that tracks Transaction declaration.
        Note well: Proton python does not provide the ability to set a delivery
        state to DECLARED (0x0033), so this broker cannot simulate a full
        transactional delivery.  At best we ensure that the router properly
        forwards the target capabilities and the declare message.
        """

        def __init__(self, url, container_id=None, **handler_kwargs):
            super(OneRouterTransactionalAttachTest.FakeTxnBroker,
                  self).__init__(url, container_id, **handler_kwargs)
            self.txn_link = None
            self.remote_caps = None
            self.declare_body = None

        def on_link_opening(self, event):
            if event.link.remote_target.type == Terminus.COORDINATOR:
                self.txn_link = event.link
                self.txn_link.source.copy(event.link.remote_source)
                self.txn_link.target.copy(event.link.remote_target)
                self.remote_caps = self.txn_link.remote_target.capabilities
                self.txn_link.flow(1)
            else:
                super(OneRouterTransactionalAttachTest.FakeTxnBroker,
                      self).on_link_opening(event)

        def on_message(self, event):
            if event.link == self.txn_link:
                self.declare_body = event.message.body
                event.delivery.update(Delivery.REJECTED)
                event.delivery.settle()
            else:
                super(OneRouterTransactionalAttachTest.FakeTxnBroker,
                      self).on_message(event)

    class TxSender(MessagingHandler, TransactionHandler):
        """
        Transactional publisher client.  The transaction will fail since the
        fake broker cannot declare the transaction properly
        """

        def __init__(self, url, messages=1):
            super(OneRouterTransactionalAttachTest.TxSender, self).__init__()
            self.url = Url(url)
            self.sent = 0
            self.declare_failed = False
            self.total = messages

        def on_start(self, event):
            self.container = event.container
            self.conn = self.container.connect(self.url)
            self.sender = self.container.create_sender(self.conn, self.url.path)
            self.container.declare_transaction(self.conn, handler=self)
            self.transaction = None

        def on_transaction_declared(self, event):
            self.transaction = event.transaction
            self.declare_failed = False
            self.send()

        def on_sendable(self, event):
            self.send()

        def send(self):
            if self.transaction and self.sender.credit > 0 and self.sent < self.total:
                seq = self.sent
                self.sent -= 1
                msg = Message(id=seq, body={'sequence': seq})
                self.transaction.send(self.sender, msg)
                self.transaction.commit()
                self.transaction = None

        def on_transaction_declare_failed(self, event):
            # expected to fail, since the FakeBroker cannot declare a transaction
            self.declare_failed = True
            self.conn.close()

    @classmethod
    def setUpClass(cls):
        super(OneRouterTransactionalAttachTest, cls).setUpClass()
        config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'TxnRouter'}),
            ('listener', {'port': cls.tester.get_port()}),
            ('connector', {'port': cls.tester.get_port(),
                           'role': 'route-container'}),

            ('linkRoute', {'prefix': "$coordinator",
                           'containerId': "FakeBroker",
                           'direction': "in"}),

            ('linkRoute', {'prefix': 'closest/queue01',
                           'containerId': 'FakeBroker',
                           'direction': 'in'}),
            ('linkRoute', {'prefix': 'closest/queue01',
                           'containerId': 'FakeBroker',
                           'direction': 'out'}),

            ('address', {'prefix': 'closest', 'distribution': 'closest'}),
            ('address', {'prefix': 'balanced', 'distribution': 'balanced'}),
            ('address', {'prefix': 'multicast', 'distribution': 'multicast'}),
        ])
        cls.router = cls.tester.qdrouterd('TxnRouter', config, wait=False)
        cls.listener = cls.router.addresses[0]
        cls.connector = cls.router.connector_addresses[0]

        cls.broker = cls.FakeTxnBroker(url=cls.connector,
                                       prefetch=0,
                                       auto_accept=False,
                                       auto_settle=False)
        cls.router.wait_connectors()
        cls.router.wait_address("closest/queue01", count=2)

    def test_01_verify_attach(self):
        """
        Verify the transaction link attach is correctly forwarded to the broker
        """
        client = self.TxSender(url=self.listener)
        Container(client).run()
        self.assertTrue(client.declare_failed)
        self.assertTrue(self.broker.txn_link is not None)
        self.assertTrue(self.broker.declare_body is not None)
        self.assertEqual(symbol('amqp:declare:list'),
                         self.broker.declare_body.descriptor)
        if PROTON_VERSION >= (0, 30, 0):
            # prior to proton 0.30.0 capabilities were not provided
            # see PROTON-2138
            self.assertTrue(self.broker.remote_caps is not None)
            # capabilities should be a list with a txn-capability type
            # verify router has forwarded this correctly:
            rc = self.broker.remote_caps
            rc.rewind()
            count = 0
            while rc.next() == Data.SYMBOL:
                s = rc.get_symbol()
                self.assertIn(s, [symbol('amqp:local-transactions'),
                                  symbol('amqp:distributed-transactions'),
                                  symbol('amqp:promotable-transactions'),
                                  symbol('amqp:multi-txns-per-ssn'),
                                  symbol('amqp:multi-ssns-per-txn')])
                count += 1
            self.assertTrue(count > 0)


if __name__ == '__main__':
    unittest.main(main_module())

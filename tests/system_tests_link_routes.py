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

from time import sleep, time
from threading import Event
from subprocess import PIPE, STDOUT
import socket
from typing import Optional

from system_test import TestCase, Qdrouterd, main_module, TIMEOUT, Process, TestTimeout, \
    AsyncTestSender, AsyncTestReceiver, MgmtMsgProxy, unittest, QdManager
from test_broker import FakeBroker
from test_broker import FakeService

from proton import Delivery, symbol, Data, Described
from proton import Message, Condition
from proton.handlers import MessagingHandler
from proton.reactor import AtMostOnce, Container, DynamicNodeProperties, LinkOption, AtLeastOnce
from proton.reactor import ApplicationEvent
from proton.reactor import EventInjector
from proton.utils import BlockingConnection
from system_tests_drain_support import DrainMessagesHandler, DrainOneMessageHandler, DrainNoMessagesHandler, DrainNoMoreMessagesHandler

from qpid_dispatch.management.client import Node
from qpid_dispatch.management.error import NotFoundStatus, BadRequestStatus


class LinkRouteTest(TestCase):
    """
    Tests the linkRoute property of the dispatch router.

    Sets up 4 routers (two of which are acting as brokers (QDR.A, QDR.D)). The other two routers have linkRoutes
    configured such that matching traffic will be directed to/from the 'fake' brokers.

    (please see configs in the setUpClass method to get a sense of how the routers and their connections are configured)
    The tests in this class send and receive messages across this network of routers to link routable addresses.
    Uses the Python Blocking API to send/receive messages. The blocking api plays neatly into the synchronous nature
    of system tests.

        QDR.A acting broker #1
             +---------+         +---------+         +---------+     +-----------------+
             |         | <------ |         | <-----  |         |<----| blocking_sender |
             |  QDR.A  |         |  QDR.B  |         |  QDR.C  |     +-----------------+
             |         | ------> |         | ------> |         |     +-------------------+
             +---------+         +---------+         +---------+---->| blocking_receiver |
                                    ^  |                             +-------------------+
                                    |  |
                                    |  V
                                 +---------+
                                 |         |
                                 |  QDR.D  |
                                 |         |
                                 +---------+
                            QDR.D acting broker #2

    """
    @classmethod
    def get_router(cls, index):
        return cls.routers[index]

    @classmethod
    def setUpClass(cls):
        """Start three routers"""
        super(LinkRouteTest, cls).setUpClass()

        def router(name, connection):

            config = [
                ('router', {'mode': 'interior', 'id': 'QDR.%s' % name}),
            ] + connection

            config = Qdrouterd.Config(config)
            cls.routers.append(cls.tester.qdrouterd(name, config, wait=False))

        cls.routers = []
        a_listener_port = cls.tester.get_port()
        b_listener_port = cls.tester.get_port()
        c_listener_port = cls.tester.get_port()
        d_listener_port = cls.tester.get_port()
        test_tag_listener_port = cls.tester.get_port()

        router('A',
               [
                   ('listener', {'role': 'normal', 'host': '0.0.0.0', 'port': a_listener_port, 'saslMechanisms': 'ANONYMOUS'}),
               ])
        router('B',
               [
                   # Listener for clients, note that the tests assume this listener is first in this list:
                   ('listener', {'role': 'normal', 'host': '0.0.0.0', 'port': b_listener_port, 'saslMechanisms': 'ANONYMOUS'}),
                   ('listener', {'name': 'test-tag', 'role': 'route-container', 'host': '0.0.0.0', 'port': test_tag_listener_port, 'saslMechanisms': 'ANONYMOUS'}),

                   # This is an route-container connection made from QDR.B's ephemeral port to a_listener_port
                   ('connector', {'name': 'broker', 'role': 'route-container', 'host': '0.0.0.0', 'port': a_listener_port, 'saslMechanisms': 'ANONYMOUS'}),
                   # Only inter router communication must happen on 'inter-router' connectors. This connector makes
                   # a connection from the router B's ephemeral port to c_listener_port
                   ('connector', {'name': 'routerC', 'role': 'inter-router', 'host': '0.0.0.0', 'port': c_listener_port}),
                   # This is an on-demand connection made from QDR.B's ephemeral port to d_listener_port
                   ('connector', {'name': 'routerD', 'role': 'route-container', 'host': '0.0.0.0', 'port': d_listener_port, 'saslMechanisms': 'ANONYMOUS'}),

                   #('linkRoute', {'prefix': 'org.apache', 'connection': 'broker', 'direction': 'in'}),
                   ('linkRoute', {'prefix': 'org.apache', 'containerId': 'QDR.A', 'direction': 'in'}),
                   ('linkRoute', {'prefix': 'org.apache', 'containerId': 'QDR.A', 'direction': 'out'}),

                   ('linkRoute', {'prefix': 'pulp.task', 'connection': 'test-tag', 'direction': 'in'}),
                   ('linkRoute', {'prefix': 'pulp.task', 'connection': 'test-tag', 'direction': 'out'}),

                   # addresses matching pattern 'a.*.toA.#' route to QDR.A
                   ('linkRoute', {'pattern': 'a.*.toA.#', 'containerId': 'QDR.A', 'direction': 'in'}),
                   ('linkRoute', {'pattern': 'a.*.toA.#', 'containerId': 'QDR.A', 'direction': 'out'}),

                   # addresses matching pattern 'a.*.toD.#' route to QDR.D
                   # Dont change dir to direction here so we can make sure that the dir attribute is still working.
                   ('linkRoute', {'pattern': 'a.*.toD.#', 'containerId': 'QDR.D', 'dir': 'in'}),
                   ('linkRoute', {'pattern': 'a.*.toD.#', 'containerId': 'QDR.D', 'dir': 'out'})

               ]
               )
        router('C',
               [
                   # The client will exclusively use the following listener to
                   # connect to QDR.C, the tests assume this is the first entry
                   # in the list
                   ('listener', {'host': '0.0.0.0', 'role': 'normal', 'port': cls.tester.get_port(), 'saslMechanisms': 'ANONYMOUS'}),
                   ('listener', {'host': '0.0.0.0', 'role': 'inter-router', 'port': c_listener_port, 'saslMechanisms': 'ANONYMOUS'}),
                   # The dot(.) at the end is ignored by the address hashing scheme.

                   ('linkRoute', {'prefix': 'org.apache.', 'direction': 'in'}),
                   ('linkRoute', {'prefix': 'org.apache.', 'direction': 'out'}),

                   ('linkRoute', {'prefix': 'pulp.task', 'direction': 'in'}),
                   ('linkRoute', {'prefix': 'pulp.task', 'direction': 'out'}),

                   ('linkRoute', {'pattern': 'a.*.toA.#', 'direction': 'in'}),
                   ('linkRoute', {'pattern': 'a.*.toA.#', 'direction': 'out'}),

                   ('linkRoute', {'pattern': 'a.*.toD.#', 'direction': 'in'}),
                   ('linkRoute', {'pattern': 'a.*.toD.#', 'direction': 'out'})

               ]
               )
        router('D',  # sink for QDR.D routes
               [
                   ('listener', {'role': 'normal', 'host': '0.0.0.0', 'port': d_listener_port, 'saslMechanisms': 'ANONYMOUS'}),
               ])

        # Wait for the routers to locate each other, and for route propagation
        # to settle
        cls.routers[1].wait_router_connected('QDR.C')
        cls.routers[2].wait_router_connected('QDR.B')
        cls.routers[2].wait_address("org.apache", remotes=1, delay=0.5, count=2)

        # This is not a classic router network in the sense that QDR.A and D are acting as brokers. We allow a little
        # bit more time for the routers to stabilize.
        sleep(2)

    def run_qdstat_linkRoute(self, address, args=None):
        cmd = ['qdstat', '--bus', str(address), '--timeout', str(TIMEOUT)] + ['--linkroute']
        if args:
            cmd = cmd + args
        p = self.popen(
            cmd,
            name='qdstat-' + self.id(), stdout=PIPE, expect=None,
            universal_newlines=True)

        out = p.communicate()[0]
        assert p.returncode == 0, "qdstat exit status %s, output:\n%s" % (p.returncode, out)
        return out

    def run_qdmanage(self, cmd, input=None, expect=Process.EXIT_OK, address=None):
        p = self.popen(
            ['qdmanage'] + cmd.split(' ') + ['--bus', address or self.address(), '--indent=-1', '--timeout', str(TIMEOUT)],
            stdin=PIPE, stdout=PIPE, stderr=STDOUT, expect=expect,
            universal_newlines=True)
        out = p.communicate(input)[0]
        try:
            p.teardown()
        except Exception as e:
            raise Exception("%s\n%s" % (e, out))
        return out

    def test_aaa_qdmanage_query_link_route(self):
        """
        qdmanage converts short type to long type and this test specifically tests if qdmanage is actually doing
        the type conversion correctly by querying with short type and long type.
        """
        cmd = 'QUERY --type=linkRoute'
        out = self.run_qdmanage(cmd=cmd, address=self.routers[1].addresses[0])

        # Make sure there is a dir of in and out.
        self.assertIn('"direction": "in"', out)
        self.assertIn('"direction": "out"', out)
        self.assertIn('"containerId": "QDR.A"', out)

        # Use the long type and make sure that qdmanage does not mess up the long type
        cmd = 'QUERY --type=org.apache.qpid.dispatch.router.config.linkRoute'
        out = self.run_qdmanage(cmd=cmd, address=self.routers[1].addresses[0])

        # Make sure there is a dir of in and out.
        self.assertIn('"direction": "in"', out)
        self.assertIn('"direction": "out"', out)
        self.assertIn('"containerId": "QDR.A"', out)

        identity = out[out.find("identity") + 12: out.find("identity") + 13]
        cmd = 'READ --type=linkRoute --identity=' + identity
        out = self.run_qdmanage(cmd=cmd, address=self.routers[1].addresses[0])
        self.assertIn(identity, out)

        exception_occurred = False
        try:
            # This identity should not be found
            cmd = 'READ --type=linkRoute --identity=9999'
            out = self.run_qdmanage(cmd=cmd, address=self.routers[1].addresses[0])
        except Exception as e:
            exception_occurred = True
            self.assertIn("NotFoundStatus: Not Found", str(e))

        self.assertTrue(exception_occurred)

        exception_occurred = False
        try:
            # There is no identity specified, this is a bad request
            cmd = 'READ --type=linkRoute'
            out = self.run_qdmanage(cmd=cmd, address=self.routers[1].addresses[0])
        except Exception as e:
            exception_occurred = True
            self.assertIn("BadRequestStatus: No name or identity provided", str(e))

        self.assertTrue(exception_occurred)

        cmd = 'CREATE --type=autoLink address=127.0.0.1 direction=in connection=routerC'
        out = self.run_qdmanage(cmd=cmd, address=self.routers[1].addresses[0])

        identity = out[out.find("identity") + 12: out.find("identity") + 14]
        cmd = 'READ --type=autoLink --identity=' + identity
        out = self.run_qdmanage(cmd=cmd, address=self.routers[1].addresses[0])
        self.assertIn(identity, out)

    def test_bbb_qdstat_link_routes_routerB(self):
        """
        Runs qdstat on router B to make sure that router B has 4 link routes,
        each having one 'in' and one 'out' entry

        """
        out = self.run_qdstat_linkRoute(self.routers[1].addresses[0])
        for route in ['a.*.toA.#', 'a.*.toD.#', 'org.apache', 'pulp.task']:
            self.assertIn(route, out)

        out_list = out.split()
        self.assertEqual(out_list.count('in'), 4)
        self.assertEqual(out_list.count('out'), 4)

        parts = out.split("\n")
        self.assertEqual(len(parts), 15)

        out = self.run_qdstat_linkRoute(self.routers[1].addresses[0], args=['--limit=1'])
        parts = out.split("\n")
        self.assertEqual(len(parts), 8)

    def test_ccc_qdstat_link_routes_routerC(self):
        """
        Runs qdstat on router C to make sure that router C has 4 link routes,
        each having one 'in' and one 'out' entry

        """
        out = self.run_qdstat_linkRoute(self.routers[2].addresses[0])
        out_list = out.split()

        self.assertEqual(out_list.count('in'), 4)
        self.assertEqual(out_list.count('out'), 4)

    def test_ddd_partial_link_route_match(self):
        """
        The linkRoute on Routers C and B is set to org.apache.
        Creates a receiver listening on the address 'org.apache.dev' and a sender that sends to address 'org.apache.dev'.
        Sends a message to org.apache.dev via router QDR.C and makes sure that the message was successfully
        routed (using partial address matching) and received using pre-created links that were created as a
        result of specifying addresses in the linkRoute attribute('org.apache.').
        """
        hello_world_1 = "Hello World_1!"

        # Connects to listener #2 on QDR.C
        addr = self.routers[2].addresses[0]

        blocking_connection = BlockingConnection(addr)

        # Receive on org.apache.dev
        blocking_receiver = blocking_connection.create_receiver(address="org.apache.dev")

        apply_options = AtMostOnce()

        # Sender to org.apache.dev
        blocking_sender = blocking_connection.create_sender(address="org.apache.dev", options=apply_options)
        msg = Message(body=hello_world_1)
        # Send a message
        blocking_sender.send(msg)

        received_message = blocking_receiver.receive()

        self.assertEqual(hello_world_1, received_message.body)

        # Connect to the router acting like the broker (QDR.A) and check the deliveriesIngress and deliveriesEgress
        local_node = Node.connect(self.routers[0].addresses[0], timeout=TIMEOUT)

        self.assertEqual('QDR.A', local_node.query(type='org.apache.qpid.dispatch.router',
                                                   attribute_names=['id']).results[0][0])

        self.assertEqual(1, local_node.read(type='org.apache.qpid.dispatch.router.address',
                                            name='M0org.apache.dev').deliveriesEgress)
        self.assertEqual(1, local_node.read(type='org.apache.qpid.dispatch.router.address',
                                            name='M0org.apache.dev').deliveriesIngress)

        # There should be 4 links -
        # 1. outbound receiver link on org.apache.dev
        # 2. inbound sender link on blocking_sender
        # 3. inbound link to the $management
        # 4. outbound link to $management
        # self.assertEqual(4, len()
        self.assertEqual(4, len(local_node.query(type='org.apache.qpid.dispatch.router.link').results))

        blocking_connection.close()

    def test_partial_link_route_match_1(self):
        """
        This test is pretty much the same as the previous test (test_partial_link_route_match) but the connection is
        made to router QDR.B instead of QDR.C and we expect to see the same behavior.
        """
        hello_world_2 = "Hello World_2!"
        addr = self.routers[1].addresses[0]

        blocking_connection = BlockingConnection(addr)

        # Receive on org.apache.dev
        blocking_receiver = blocking_connection.create_receiver(address="org.apache.dev.1")

        apply_options = AtMostOnce()

        # Sender to  to org.apache.dev
        blocking_sender = blocking_connection.create_sender(address="org.apache.dev.1", options=apply_options)
        msg = Message(body=hello_world_2)
        # Send a message
        blocking_sender.send(msg)

        received_message = blocking_receiver.receive()

        self.assertEqual(hello_world_2, received_message.body)

        local_node = Node.connect(self.routers[0].addresses[0], timeout=TIMEOUT)

        # Make sure that the router node acting as the broker (QDR.A) had one message routed through it. This confirms
        # that the message was link routed
        self.assertEqual(1, local_node.read(type='org.apache.qpid.dispatch.router.address',
                                            name='M0org.apache.dev.1').deliveriesEgress)

        self.assertEqual(1, local_node.read(type='org.apache.qpid.dispatch.router.address',
                                            name='M0org.apache.dev.1').deliveriesIngress)

        blocking_connection.close()

    def test_full_link_route_match(self):
        """
        The linkRoute on Routers C and B is set to org.apache.
        Creates a receiver listening on the address 'org.apache' and a sender that sends to address 'org.apache'.
        Sends a message to org.apache via router QDR.C and makes sure that the message was successfully
        routed (using full address matching) and received using pre-created links that were created as a
        result of specifying addresses in the linkRoute attribute('org.apache.').
        """
        hello_world_3 = "Hello World_3!"
        # Connects to listener #2 on QDR.C
        addr = self.routers[2].addresses[0]

        blocking_connection = BlockingConnection(addr)

        # Receive on org.apache
        blocking_receiver = blocking_connection.create_receiver(address="org.apache")

        apply_options = AtMostOnce()

        # Sender to  to org.apache
        blocking_sender = blocking_connection.create_sender(address="org.apache", options=apply_options)
        msg = Message(body=hello_world_3)
        # Send a message
        blocking_sender.send(msg)

        received_message = blocking_receiver.receive()

        self.assertEqual(hello_world_3, received_message.body)

        local_node = Node.connect(self.routers[0].addresses[0], timeout=TIMEOUT)

        # Make sure that the router node acting as the broker (QDR.A) had one message routed through it. This confirms
        # that the message was link routed
        self.assertEqual(1, local_node.read(type='org.apache.qpid.dispatch.router.address',
                                            name='M0org.apache').deliveriesEgress)

        self.assertEqual(1, local_node.read(type='org.apache.qpid.dispatch.router.address',
                                            name='M0org.apache').deliveriesIngress)

        blocking_connection.close()

    def _link_route_pattern_match(self, connect_node, include_host,
                                  exclude_host, test_address,
                                  expected_pattern):
        """
        This helper function ensures that messages sent to 'test_address' pass
        through 'include_host', and are *not* routed to 'exclude_host'

        """
        hello_pattern = "Hello Pattern!"
        route = 'M0' + test_address

        # Connect to the two 'waypoints', ensure the route is not present on
        # either

        node_A = Node.connect(include_host, timeout=TIMEOUT)
        node_B = Node.connect(exclude_host, timeout=TIMEOUT)

        for node in [node_A, node_B]:
            self.assertRaises(NotFoundStatus,
                              node.read,
                              type='org.apache.qpid.dispatch.router.address',
                              name=route)

        # wait until the host we're connecting to gets its next hop for the
        # pattern we're connecting to
        connect_node.wait_address(expected_pattern, remotes=1, delay=0.1, count=2)

        # Connect to 'connect_node' and send message to 'address'

        blocking_connection = BlockingConnection(connect_node.addresses[0])
        blocking_receiver = blocking_connection.create_receiver(address=test_address)
        blocking_sender = blocking_connection.create_sender(address=test_address,
                                                            options=AtMostOnce())
        msg = Message(body=hello_pattern)
        blocking_sender.send(msg)
        received_message = blocking_receiver.receive()
        self.assertEqual(hello_pattern, received_message.body)

        # verify test_address is only present on include_host and not on exclude_host

        self.assertRaises(NotFoundStatus,
                          node_B.read,
                          type='org.apache.qpid.dispatch.router.address',
                          name=route)

        self.assertEqual(1, node_A.read(type='org.apache.qpid.dispatch.router.address',
                                        name=route).deliveriesIngress)
        self.assertEqual(1, node_A.read(type='org.apache.qpid.dispatch.router.address',
                                        name=route).deliveriesIngress)

        # drop the connection and verify that test_address is no longer on include_host

        blocking_connection.close()

        timeout = time() + TIMEOUT
        while True:
            try:
                node_A.read(type='org.apache.qpid.dispatch.router.address',
                            name=route)
                if time() > timeout:
                    raise Exception("Expected route '%s' to expire!" % route)
                sleep(0.1)
            except NotFoundStatus:
                break

        node_A.close()
        node_B.close()

    def test_link_route_pattern_match(self):
        """
        Verify the addresses match the proper patterns and are routed to the
        proper 'waypoint' only
        """
        qdr_A = self.routers[0].addresses[0]
        qdr_D = self.routers[3].addresses[0]
        qdr_C = self.routers[2]  # note: the node, not the address!

        self._link_route_pattern_match(connect_node=qdr_C,
                                       include_host=qdr_A,
                                       exclude_host=qdr_D,
                                       test_address='a.notD.toA',
                                       expected_pattern='a.*.toA.#')
        self._link_route_pattern_match(connect_node=qdr_C,
                                       include_host=qdr_D,
                                       exclude_host=qdr_A,
                                       test_address='a.notA.toD',
                                       expected_pattern='a.*.toD.#')
        self._link_route_pattern_match(connect_node=qdr_C,
                                       include_host=qdr_A,
                                       exclude_host=qdr_D,
                                       test_address='a.toD.toA.xyz',
                                       expected_pattern='a.*.toA.#')
        self._link_route_pattern_match(connect_node=qdr_C,
                                       include_host=qdr_D,
                                       exclude_host=qdr_A,
                                       test_address='a.toA.toD.abc',
                                       expected_pattern='a.*.toD.#')

    def test_custom_annotations_match(self):
        """
        The linkRoute on Routers C and B is set to org.apache.
        Creates a receiver listening on the address 'org.apache' and a sender that sends to address 'org.apache'.
        Sends a message with custom annotations to org.apache via router QDR.C and makes sure that the message was successfully
        routed (using full address matching) and received using pre-created links that were created as a
        result of specifying addresses in the linkRoute attribute('org.apache.'). Make sure custom annotations arrived as well.
        """
        hello_world_3 = "Hello World_3!"
        # Connects to listener #2 on QDR.C
        addr = self.routers[2].addresses[0]

        blocking_connection = BlockingConnection(addr)

        # Receive on org.apache
        blocking_receiver = blocking_connection.create_receiver(address="org.apache.2")

        apply_options = AtMostOnce()

        # Sender to  to org.apache
        blocking_sender = blocking_connection.create_sender(address="org.apache.2", options=apply_options)
        msg = Message(body=hello_world_3)
        annotations = {'custom-annotation': '1/Custom_Annotation'}
        msg.annotations = annotations

        # Send a message
        blocking_sender.send(msg)

        received_message = blocking_receiver.receive()

        self.assertEqual(hello_world_3, received_message.body)
        self.assertEqual(received_message.annotations, annotations)

        blocking_connection.close()

    def test_full_link_route_match_1(self):
        """
        This test is pretty much the same as the previous test (test_full_link_route_match) but the connection is
        made to router QDR.B instead of QDR.C and we expect the message to be link routed successfully.
        """
        hello_world_4 = "Hello World_4!"
        addr = self.routers[1].addresses[0]

        blocking_connection = BlockingConnection(addr)

        # Receive on org.apache
        blocking_receiver = blocking_connection.create_receiver(address="org.apache.1")

        apply_options = AtMostOnce()

        # Sender to  to org.apache
        blocking_sender = blocking_connection.create_sender(address="org.apache.1", options=apply_options)

        msg = Message(body=hello_world_4)
        # Send a message
        blocking_sender.send(msg)

        received_message = blocking_receiver.receive()

        self.assertEqual(hello_world_4, received_message.body)

        local_node = Node.connect(self.routers[0].addresses[0], timeout=TIMEOUT)

        # Make sure that the router node acting as the broker (QDR.A) had one message routed through it. This confirms
        # that the message was link routed
        self.assertEqual(1, local_node.read(type='org.apache.qpid.dispatch.router.address',
                                            name='M0org.apache.1').deliveriesEgress)

        self.assertEqual(1, local_node.read(type='org.apache.qpid.dispatch.router.address',
                                            name='M0org.apache.1').deliveriesIngress)

        blocking_connection.close()

    def test_zzz_qdmanage_delete_link_route(self):
        """
        We are deleting the link route using qdmanage short name. This should be the last test to run
        """

        local_node = Node.connect(self.routers[1].addresses[0], timeout=TIMEOUT)
        res = local_node.query(type='org.apache.qpid.dispatch.router')
        results = res.results[0]
        attribute_list = res.attribute_names

        result_list = local_node.query(type='org.apache.qpid.dispatch.router.config.linkRoute').results
        self.assertEqual(results[attribute_list.index('linkRouteCount')], len(result_list))

        # First delete linkRoutes on QDR.B
        for rid in range(8):
            cmd = 'DELETE --type=linkRoute --identity=' + result_list[rid][1]
            self.run_qdmanage(cmd=cmd, address=self.routers[1].addresses[0])

        cmd = 'QUERY --type=linkRoute'
        out = self.run_qdmanage(cmd=cmd, address=self.routers[1].addresses[0])
        self.assertEqual(out.rstrip(), '[]')

        # linkRoutes now gone on QDR.B but remember that it still exist on QDR.C
        # We will now try to create a receiver on address org.apache.dev on QDR.C.
        # Since the linkRoute on QDR.B is gone, QDR.C
        # will not allow a receiver to be created since there is no route to destination.

        # Connects to listener #2 on QDR.C
        addr = self.routers[2].addresses[0]

        # Now delete linkRoutes on QDR.C to eradicate linkRoutes completely
        local_node = Node.connect(addr, timeout=TIMEOUT)
        result_list = local_node.query(type='org.apache.qpid.dispatch.router.config.linkRoute').results

        # QDR.C has 8 link routes configured, nuke 'em:
        self.assertEqual(8, len(result_list))
        for rid in range(8):
            cmd = 'DELETE --type=linkRoute --identity=' + result_list[rid][1]
            self.run_qdmanage(cmd=cmd, address=addr)

        cmd = 'QUERY --type=linkRoute'
        out = self.run_qdmanage(cmd=cmd, address=addr)
        self.assertEqual(out.rstrip(), '[]')

        res = local_node.query(type='org.apache.qpid.dispatch.router')
        results = res.results[0]
        attribute_list = res.attribute_names
        self.assertEqual(results[attribute_list.index('linkRouteCount')], 0)

        blocking_connection = BlockingConnection(addr, timeout=3)

        # Receive on org.apache.dev (this address used to be linkRouted but not anymore since we deleted linkRoutes
        # on both QDR.C and QDR.B)
        blocking_receiver = blocking_connection.create_receiver(address="org.apache.dev")

        apply_options = AtMostOnce()
        hello_world_1 = "Hello World_1!"
        # Sender to org.apache.dev
        blocking_sender = blocking_connection.create_sender(address="org.apache.dev", options=apply_options)
        msg = Message(body=hello_world_1)

        # Send a message
        blocking_sender.send(msg)
        received_message = blocking_receiver.receive(timeout=5)
        self.assertEqual(hello_world_1, received_message.body)

    def test_yyy_delivery_tag(self):
        """
        Tests that the router carries over the delivery tag on a link routed delivery
        """
        listening_address = self.routers[1].addresses[1]
        sender_address = self.routers[2].addresses[0]
        qdstat_address = self.routers[2].addresses[0]
        test = DeliveryTagsTest(sender_address, listening_address, qdstat_address)
        test.run()
        self.assertIsNone(test.error)

    def test_yyy_invalid_delivery_tag(self):
        test = InvalidTagTest(self.routers[2].addresses[0])
        test.run()
        self.assertIsNone(test.error)

    def test_close_with_unsettled(self):
        test = CloseWithUnsettledTest(self.routers[1].addresses[0], self.routers[1].addresses[1])
        test.run()
        self.assertIsNone(test.error)

    def test_www_drain_support_all_messages(self):
        drain_support = DrainMessagesHandler(self.routers[2].addresses[0])
        drain_support.run()
        self.assertIsNone(drain_support.error)

    def test_www_drain_support_one_message(self):
        drain_support = DrainOneMessageHandler(self.routers[2].addresses[0])
        drain_support.run()
        self.assertIsNone(drain_support.error)

    def test_www_drain_support_no_messages(self):
        drain_support = DrainNoMessagesHandler(self.routers[2].addresses[0])
        drain_support.run()
        self.assertIsNone(drain_support.error)

    def test_www_drain_support_no_more_messages(self):
        drain_support = DrainNoMoreMessagesHandler(self.routers[2].addresses[0])
        drain_support.run()
        self.assertIsNone(drain_support.error)

    def test_link_route_terminus_address(self):
        # The receiver is attaching to router B to a listener that has link route for address 'pulp.task' setup.
        listening_address = self.routers[1].addresses[1]
        # Run the query on a normal port
        query_address_listening = self.routers[1].addresses[0]

        # Sender is attaching to router C
        sender_address = self.routers[2].addresses[0]
        query_address_sending = self.routers[2].addresses[0]

        test = TerminusAddrTest(sender_address, listening_address, query_address_sending, query_address_listening)
        test.run()

        self.assertTrue(test.in_receiver_found)
        self.assertTrue(test.out_receiver_found)
        self.assertTrue(test.in_sender_found)
        self.assertTrue(test.out_sender_found)

    def test_dynamic_source(self):
        test = DynamicSourceTest(self.routers[1].addresses[0], self.routers[1].addresses[1])
        test.run()
        self.assertIsNone(test.error)

    def test_dynamic_target(self):
        test = DynamicTargetTest(self.routers[1].addresses[0], self.routers[1].addresses[1])
        test.run()
        self.assertIsNone(test.error)

    def test_detach_without_close(self):
        test = DetachNoCloseTest(self.routers[1].addresses[0], self.routers[1].addresses[1])
        test.run()
        self.assertIsNone(test.error)

    def test_detach_mixed_close(self):
        test = DetachMixedCloseTest(self.routers[1].addresses[0], self.routers[1].addresses[1])
        test.run()
        self.assertIsNone(test.error)

    def _multi_link_send_receive(self, send_host, receive_host, name):
        senders = ["%s/%s" % (send_host, address) for address in ["org.apache.foo", "org.apache.bar"]]
        receivers = ["%s/%s" % (receive_host, address) for address in ["org.apache.foo", "org.apache.bar"]]
        test = MultiLinkSendReceive(senders, receivers, name)
        test.run()
        self.assertIsNone(test.error)

    def test_same_name_route_receivers_through_B(self):
        self._multi_link_send_receive(self.routers[0].addresses[0], self.routers[1].addresses[0], "recv_through_B")

    def test_same_name_route_senders_through_B(self):
        self._multi_link_send_receive(self.routers[1].addresses[0], self.routers[0].addresses[0], "send_through_B")

    def test_same_name_route_receivers_through_C(self):
        self._multi_link_send_receive(self.routers[0].addresses[0], self.routers[2].addresses[0], "recv_through_C")

    def test_same_name_route_senders_through_C(self):
        self._multi_link_send_receive(self.routers[2].addresses[0], self.routers[0].addresses[0], "send_through_C")

    def test_echo_detach_received(self):
        """
        Create two receivers to link routed address org.apache.dev
        Create a sender to the same address that the receiver is listening on and send 100 messages.
        After the receivers receive 10 messages each, the receivers will detach and expect to receive ten
        detaches in response.

        """
        test = EchoDetachReceived(self.routers[2].addresses[0], self.routers[2].addresses[0])
        test.run()
        self.assertIsNone(test.error)

    def test_bad_link_route_config(self):
        """
        What happens when the link route create request is malformed?
        """
        mgmt = self.routers[1].management

        # zero length prefix
        self.assertRaises(BadRequestStatus,
                          mgmt.create,
                          type="org.apache.qpid.dispatch.router.config.linkRoute",
                          name="bad-1",
                          attributes={'prefix': '',
                                      'containerId': 'FakeBroker',
                                      'direction': 'in'})
        # pattern wrong type
        self.assertRaises(BadRequestStatus,
                          mgmt.create,
                          type="org.apache.qpid.dispatch.router.config.linkRoute",
                          name="bad-2",
                          attributes={'pattern': 666,
                                      'containerId': 'FakeBroker',
                                      'direction': 'in'})
        # invalid pattern (no tokens)
        self.assertRaises(BadRequestStatus,
                          mgmt.create,
                          type="org.apache.qpid.dispatch.router.config.linkRoute",
                          name="bad-3",
                          attributes={'pattern': '///',
                                      'containerId': 'FakeBroker',
                                      'direction': 'in'})
        # empty attributes
        self.assertRaises(BadRequestStatus,
                          mgmt.create,
                          type="org.apache.qpid.dispatch.router.config.linkRoute",
                          name="bad-4",
                          attributes={})

        # both pattern and prefix
        self.assertRaises(BadRequestStatus,
                          mgmt.create,
                          type="org.apache.qpid.dispatch.router.config.linkRoute",
                          name="bad-5",
                          attributes={'prefix': 'a1',
                                      'pattern': 'b2',
                                      'containerId': 'FakeBroker',
                                      'direction': 'in'})
        # bad direction
        self.assertRaises(BadRequestStatus,
                          mgmt.create,
                          type="org.apache.qpid.dispatch.router.config.linkRoute",
                          name="bad-6",
                          attributes={'pattern': 'b2',
                                      'containerId': 'FakeBroker',
                                      'direction': 'nowhere'})
        # bad distribution
        self.assertRaises(BadRequestStatus,
                          mgmt.create,
                          type="org.apache.qpid.dispatch.router.config.linkRoute",
                          name="bad-7",
                          attributes={'pattern': 'b2',
                                      'containerId': 'FakeBroker',
                                      'direction': 'in',
                                      "distribution": "dilly dilly"})

        # no direction
        self.assertRaises(BadRequestStatus,
                          mgmt.create,
                          type="org.apache.qpid.dispatch.router.config.linkRoute",
                          name="bad-8",
                          attributes={'prefix': 'b2',
                                      'containerId': 'FakeBroker'})

        # neither pattern nor prefix
        self.assertRaises(BadRequestStatus,
                          mgmt.create,
                          type="org.apache.qpid.dispatch.router.config.linkRoute",
                          name="bad-9",
                          attributes={'direction': 'out',
                                      'containerId': 'FakeBroker'})


class DeliveryTagsTest(MessagingHandler):
    def __init__(self, sender_address, listening_address, qdstat_address):
        super(DeliveryTagsTest, self).__init__()
        self.sender_address = sender_address
        self.listening_address = listening_address
        self.sender = None
        self.receiver_connection = None
        self.sender_connection = None
        self.qdstat_address = qdstat_address
        self.id = '1235'
        self.times = 1
        self.sent = 0
        self.rcvd = 0
        self.delivery_tag_verified = False
        # The delivery tag we are going to send in the transfer frame
        # We will later make sure that the same delivery tag shows up on the receiving end in the link routed case.
        # KAG: force the literal to type 'str' due to SWIG weirdness: on 2.X a
        # delivery tag cannot be unicode (must be binary), but on 3.X it must
        # be unicode!  See https://issues.apache.org/jira/browse/PROTON-1843
        self.delivery_tag = str('92319')
        self.error = None

    def timeout(self):
        self.error = "Timeout expired: sent=%d rcvd=%d" % (self.sent, self.rcvd)
        if self.receiver_connection:
            self.receiver_connection.close()
        if self.sender_connection:
            self.sender_connection.close()

    def on_start(self, event):
        self.timer               = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.receiver_connection = event.container.connect(self.listening_address)

    def on_connection_remote_open(self, event):
        if event.connection == self.receiver_connection:
            continue_loop = True
            # Don't open the sender connection unless we can make sure that there is a remote receiver ready to
            # accept the message.
            # If there is no remote receiver, the router will throw a 'No route to destination' error when
            # creating sender connection.
            # The following loops introduces a wait before creating the sender connection. It gives time to the
            # router so that the address Dpulp.task can show up on the remoteCount
            i = 0
            while continue_loop:
                if i > 100:  # If we have run the read command for more than hundred times and we still do not have
                    # the remoteCount set to 1, there is a problem, just exit out of the function instead
                    # of looping to infinity.
                    self.receiver_connection.close()
                    return
                local_node = Node.connect(self.qdstat_address, timeout=TIMEOUT)
                out = local_node.read(type='org.apache.qpid.dispatch.router.address', name='Dpulp.task').remoteCount
                if out == 1:
                    continue_loop = False
                else:
                    i += 1
                    sleep(0.25)

            self.sender_connection = event.container.connect(self.sender_address)
            self.sender = event.container.create_sender(self.sender_connection, "pulp.task", options=AtMostOnce())

    def on_sendable(self, event):
        if self.times == 1:
            msg = Message(body="Hello World")
            self.sender.send(msg, tag=self.delivery_tag)
            self.times += 1
            self.sent += 1

    def on_message(self, event):
        if "Hello World" == event.message.body:
            self.rcvd += 1

        # If the tag on the delivery is the same as the tag we sent with the initial transfer, it means
        # that the router has propagated the delivery tag successfully because of link routing.
        if self.delivery_tag != event.delivery.tag:
            self.error = "Delivery-tag: expected:%r got:%r" % (self.delivery_tag, event.delivery.tag)
        self.receiver_connection.close()
        self.sender_connection.close()
        self.timer.cancel()

    def run(self):
        Container(self).run()


class CloseWithUnsettledTest(MessagingHandler):
    ##
    # This test sends a message across an attach-routed link.  While the message
    # is unsettled, the client link is closed.  The test is ensuring that the
    # router does not crash during the closing of the links.
    ##
    def __init__(self, normal_addr, route_addr):
        super(CloseWithUnsettledTest, self).__init__(prefetch=0, auto_accept=False)
        self.normal_addr = normal_addr
        self.route_addr  = route_addr
        self.dest = "pulp.task.CWUtest"
        self.error = None

    def timeout(self):
        self.error = "Timeout Expired - Check for cores"
        self.conn_normal.close()
        self.conn_route.close()

    def on_start(self, event):
        self.timer      = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.conn_route = event.container.connect(self.route_addr)

    def on_connection_opened(self, event):
        if event.connection == self.conn_route:
            self.conn_normal = event.container.connect(self.normal_addr)
        elif event.connection == self.conn_normal:
            self.sender = event.container.create_sender(self.conn_normal, self.dest)

    def on_connection_closed(self, event):
        self.conn_route.close()
        self.timer.cancel()

    def on_link_opened(self, event):
        if event.receiver:
            self.receiver = event.receiver
            self.receiver.flow(1)

    def on_sendable(self, event):
        msg = Message(body="CloseWithUnsettled")
        event.sender.send(msg)

    def on_message(self, event):
        self.conn_normal.close()

    def run(self):
        Container(self).run()


class DynamicSourceTest(MessagingHandler):
    ##
    # This test verifies that a dynamic source can be propagated via link-route to
    # a route-container.
    ##
    def __init__(self, normal_addr, route_addr):
        super(DynamicSourceTest, self).__init__(prefetch=0, auto_accept=False)
        self.normal_addr = normal_addr
        self.route_addr  = route_addr
        self.dest = "pulp.task.DynamicSource"
        self.address = "DynamicSourceAddress"
        self.error = None

    def timeout(self):
        self.error = "Timeout Expired - Check for cores"
        self.conn_normal.close()
        self.conn_route.close()

    def on_start(self, event):
        self.timer      = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.conn_route = event.container.connect(self.route_addr)

    def on_connection_opened(self, event):
        if event.connection == self.conn_route:
            self.conn_normal = event.container.connect(self.normal_addr)
        elif event.connection == self.conn_normal:
            self.receiver = event.container.create_receiver(self.conn_normal, None, dynamic=True, options=DynamicNodeProperties({"x-opt-qd.address": "pulp.task.abc"}))

    def on_link_opened(self, event):
        if event.receiver == self.receiver:
            if self.receiver.remote_source.address != self.address:
                self.error = "Expected %s, got %s" % (self.address, self.receiver.remote_source.address)
            self.conn_normal.close()
            self.conn_route.close()
            self.timer.cancel()

    def on_link_opening(self, event):
        if event.sender:
            self.sender = event.sender
            if not self.sender.remote_source.dynamic:
                self.error = "Expected sender with dynamic source"
                self.conn_normal.close()
                self.conn_route.close()
                self.timer.cancel()
            self.sender.source.address = self.address
            self.sender.open()

    def run(self):
        Container(self).run()


class DynamicTarget(LinkOption):
    def apply(self, link):
        link.target.dynamic = True
        link.target.address = None


class DynamicTargetTest(MessagingHandler):
    ##
    # This test verifies that a dynamic source can be propagated via link-route to
    # a route-container.
    ##
    def __init__(self, normal_addr, route_addr):
        super(DynamicTargetTest, self).__init__(prefetch=0, auto_accept=False)
        self.normal_addr = normal_addr
        self.route_addr  = route_addr
        self.dest = "pulp.task.DynamicTarget"
        self.address = "DynamicTargetAddress"
        self.error = None

    def timeout(self):
        self.error = "Timeout Expired - Check for cores"
        self.conn_normal.close()
        self.conn_route.close()

    def on_start(self, event):
        self.timer      = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.conn_route = event.container.connect(self.route_addr)

    def on_connection_opened(self, event):
        if event.connection == self.conn_route:
            self.conn_normal = event.container.connect(self.normal_addr)
        elif event.connection == self.conn_normal:
            self.sender = event.container.create_sender(self.conn_normal, None, options=[DynamicTarget(), DynamicNodeProperties({"x-opt-qd.address": "pulp.task.abc"})])

    def on_link_opened(self, event):
        if event.sender == self.sender:
            if self.sender.remote_target.address != self.address:
                self.error = "Expected %s, got %s" % (self.address, self.receiver.remote_source.address)
            self.conn_normal.close()
            self.conn_route.close()
            self.timer.cancel()

    def on_link_opening(self, event):
        if event.receiver:
            self.receiver = event.receiver
            if not self.receiver.remote_target.dynamic:
                self.error = "Expected receiver with dynamic source"
                self.conn_normal.close()
                self.conn_route.close()
                self.timer.cancel()
            self.receiver.target.address = self.address
            self.receiver.open()

    def run(self):
        Container(self).run()


class DetachNoCloseTest(MessagingHandler):
    ##
    # This test verifies that link-detach (not close) is propagated properly
    ##
    def __init__(self, normal_addr, route_addr):
        super(DetachNoCloseTest, self).__init__(prefetch=0, auto_accept=False)
        self.normal_addr = normal_addr
        self.route_addr  = route_addr
        self.dest = "pulp.task.DetachNoClose"
        self.error = None

    def timeout(self):
        self.error = "Timeout Expired - Check for cores"
        self.conn_normal.close()
        self.conn_route.close()

    def stop(self):
        self.conn_normal.close()
        self.conn_route.close()
        self.timer.cancel()

    def on_start(self, event):
        self.timer      = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.conn_route = event.container.connect(self.route_addr)

    def on_connection_opened(self, event):
        if event.connection == self.conn_route:
            self.conn_normal = event.container.connect(self.normal_addr)
        elif event.connection == self.conn_normal:
            self.receiver = event.container.create_receiver(self.conn_normal, self.dest)

    def on_link_opened(self, event):
        if event.receiver == self.receiver:
            self.receiver.detach()

    def on_link_remote_detach(self, event):
        if event.sender == self.sender:
            self.sender.detach()
        if event.receiver == self.receiver:
            ##
            # Test passed, we expected a detach on the propagated sender and back
            ##
            self.stop()

    def on_link_closing(self, event):
        if event.sender == self.sender:
            self.error = 'Propagated link was closed.  Expected it to be detached'
            self.stop()

        if event.receiver == self.receiver:
            self.error = 'Client link was closed.  Expected it to be detached'
            self.stop()

    def on_link_opening(self, event):
        if event.sender:
            self.sender = event.sender
            self.sender.source.address = self.sender.remote_source.address
            self.sender.open()

    def run(self):
        Container(self).run()


class DetachMixedCloseTest(MessagingHandler):
    ##
    # This test verifies that link-detach (not close) is propagated properly
    ##
    def __init__(self, normal_addr, route_addr):
        super(DetachMixedCloseTest, self).__init__(prefetch=0, auto_accept=False)
        self.normal_addr = normal_addr
        self.route_addr  = route_addr
        self.dest = "pulp.task.DetachMixedClose"
        self.error = None

    def timeout(self):
        self.error = "Timeout Expired - Check for cores"
        self.conn_normal.close()
        self.conn_route.close()

    def stop(self):
        self.conn_normal.close()
        self.conn_route.close()
        self.timer.cancel()

    def on_start(self, event):
        self.timer      = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.conn_route = event.container.connect(self.route_addr)

    def on_connection_opened(self, event):
        if event.connection == self.conn_route:
            self.conn_normal = event.container.connect(self.normal_addr)
        elif event.connection == self.conn_normal:
            self.receiver = event.container.create_receiver(self.conn_normal, self.dest)

    def on_link_opened(self, event):
        if event.receiver == self.receiver:
            self.receiver.detach()

    def on_link_remote_detach(self, event):
        if event.sender == self.sender:
            self.sender.close()
        if event.receiver == self.receiver:
            self.error = 'Client link was detached.  Expected it to be closed'
            self.stop()

    def on_link_closing(self, event):
        if event.sender == self.sender:
            self.error = 'Propagated link was closed.  Expected it to be detached'
            self.stop()

        if event.receiver == self.receiver:
            ##
            # Test Passed
            ##
            self.stop()

    def on_link_opening(self, event):
        if event.sender:
            self.sender = event.sender
            self.sender.source.address = self.sender.remote_source.address
            self.sender.open()

    def run(self):
        Container(self).run()


# Test to validate fix for DISPATCH-927
class EchoDetachReceived(MessagingHandler):
    def __init__(self, sender_address, recv_address):
        super(EchoDetachReceived, self).__init__()
        self.sender_address = sender_address
        self.recv_address = recv_address
        self.dest = "org.apache.dev"
        self.num_msgs = 100
        self.num_receivers = 10
        self.msgs_sent = 0
        self.receiver_conn = None
        self.sender_conn = None
        self.sender = None
        self.receiver_dict = {}
        self.error = None
        self.receiver_attaches = 0
        self.timer = None
        self.sender_attached = False
        self.received_msgs_dict = {}
        self.receiver_detach_dict = {}
        self.num_detaches_echoed = 0

    @property
    def msgs_received(self):
        return sum(self.received_msgs_dict.values())

    def timeout(self):

        self.bail("Timeout Expired: msgs_sent=%d msgs_received=%d, number of detaches received=%d"
                  % (self.msgs_sent, self.msgs_received, self.num_detaches_echoed))

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))

        # Create two separate connections for sender and receivers
        self.receiver_conn = event.container.connect(self.recv_address)
        self.sender_conn = event.container.connect(self.sender_address)
        for i in range(self.num_receivers):
            name = "R%d" % i
            self.receiver_dict[name] = event.container.create_receiver(self.receiver_conn, self.dest, name=name)
            self.received_msgs_dict[name] = 0

    def bail(self, text=None):
        self.error = text
        self.sender_conn.close()
        self.receiver_conn.close()
        self.timer.cancel()

    def on_link_opened(self, event):
        if event.receiver:
            if event.receiver.name in list(self.receiver_dict):
                self.receiver_attaches += 1
            # The response receiver attaches have been received. The receiver sent attaches which was link routed
            # all the way to the 'broker' router and the response attaches have come back.
            # It is now time to create the sender.
            if self.receiver_attaches == self.num_receivers:
                self.sender = event.container.create_sender(self.sender_conn, self.dest)

        elif event.sender:
            if not self.sender_attached:
                if event.sender == self.sender:
                    # The sender attaches were link routed as well and the response attach has been received.
                    self.sender_attached = True

    def on_sendable(self, event):
        # The sender will send 100 messages
        if self.receiver_attaches == self.num_receivers and self.sender_attached:
            if self.msgs_sent < self.num_msgs:
                msg = Message(body="Hello World")
                self.sender.send(msg)
                self.msgs_sent += 1

    def on_message(self, event):
        if event.receiver and event.receiver.name in list(self.receiver_dict):
            self.received_msgs_dict[event.receiver.name] += 1

        if sum(self.received_msgs_dict.values()) == self.num_msgs:
            # The receivers have received a total of 100 messages. Close the receivers. The detach sent by these
            # receivers will travel all the way over the link route and the 'broker' router will respond with a
            # detach
            for receiver in list(self.receiver_dict):
                self.receiver_dict[receiver].close()

    def on_link_closed(self, event):
        if event.receiver.name in list(self.receiver_dict) and event.receiver.name not in list(self.receiver_detach_dict):
            self.receiver_detach_dict[event.receiver.name] = event.receiver
            self.num_detaches_echoed += 1

        # Terminate the test only if both detach frames have been received.
        if all(receiver in list(self.receiver_detach_dict) for receiver in list(self.receiver_dict)):
            self.bail()

    def run(self):
        Container(self).run()


class TerminusAddrTest(MessagingHandler):
    """
    This tests makes sure that the link route address is visible in the output of qdstat -l command.

    Sets up a sender on address pulp.task.terminusTestSender and a receiver on pulp.task.terminusTestReceiver.
    Connects to the router to which the sender is attached and makes sure that the pulp.task.terminusTestSender address
    shows up with an 'in' and 'out'
    Similarly connects to the router to which the receiver is attached and makes sure that the
    pulp.task.terminusTestReceiver address shows up with an 'in' and 'out'

    """

    def __init__(self, sender_address, listening_address, query_address_sending, query_address_listening):
        super(TerminusAddrTest, self).__init__()
        self.sender_address = sender_address
        self.listening_address = listening_address
        self.sender = None
        self.receiver = None
        self.message_received = False
        self.receiver_connection = None
        self.sender_connection = None
        # We will run a query on the same router where the sender is attached
        self.query_address_sending = query_address_sending

        # We will run a query on the same router where the receiver is attached
        self.query_address_listening = query_address_listening
        self.count = 0

        self.in_receiver_found = False
        self.out_receiver_found = False
        self.in_sender_found = False
        self.out_sender_found = False

        self.receiver_link_opened = False
        self.sender_link_opened = False

    def on_start(self, event):
        self.receiver_connection = event.container.connect(self.listening_address)

    def on_connection_remote_open(self, event):
        if event.connection == self.receiver_connection:
            continue_loop = True
            # The following loops introduces a wait. It gives time to the
            # router so that the address Dpulp.task can show up on the remoteCount
            i = 0
            while continue_loop:
                if i > 100:  # If we have run the read command for more than hundred times and we still do not have
                    # the remoteCount set to 1, there is a problem, just exit out of the function instead
                    # of looping to infinity.
                    self.receiver_connection.close()
                    return
                local_node = Node.connect(self.query_address_sending, timeout=TIMEOUT)
                out = local_node.read(type='org.apache.qpid.dispatch.router.address', name='Dpulp.task').remoteCount
                if out == 1:
                    continue_loop = False
                i += 1
                sleep(0.25)

            self.sender_connection = event.container.connect(self.sender_address)

            # Notice here that the receiver and sender are listening on different addresses. Receiver on
            # pulp.task.terminusTestReceiver and the sender on pulp.task.terminusTestSender
            self.receiver = event.container.create_receiver(self.receiver_connection, "pulp.task.terminusTestReceiver")
            self.sender = event.container.create_sender(self.sender_connection, "pulp.task.terminusTestSender", options=AtMostOnce())

    def on_link_opened(self, event):
        if event.receiver == self.receiver:
            self.receiver_link_opened = True

            local_node = Node.connect(self.query_address_listening, timeout=TIMEOUT)
            out = local_node.query(type='org.apache.qpid.dispatch.router.link')

            link_dir_index = out.attribute_names.index("linkDir")
            owning_addr_index = out.attribute_names.index("owningAddr")

            # Make sure that the owningAddr M0pulp.task.terminusTestReceiver shows up on both in and out.
            # The 'out' link is on address M0pulp.task.terminusTestReceiver outgoing from the router B to the receiver
            # The 'in' link is on address M0pulp.task.terminusTestReceiver incoming from router C to router B
            for result in out.results:
                if result[link_dir_index] == 'in' and result[owning_addr_index] == 'M0pulp.task.terminusTestReceiver':
                    self.in_receiver_found = True
                if result[link_dir_index] == 'out' and result[owning_addr_index] == 'M0pulp.task.terminusTestReceiver':
                    self.out_receiver_found = True

        if event.sender == self.sender:
            self.sender_link_opened = True

            local_node = Node.connect(self.query_address_sending, timeout=TIMEOUT)
            out = local_node.query(type='org.apache.qpid.dispatch.router.link')

            link_dir_index = out.attribute_names.index("linkDir")
            owning_addr_index = out.attribute_names.index("owningAddr")

            # Make sure that the owningAddr M0pulp.task.terminusTestSender shows up on both in and out.
            # The 'in' link is on address M0pulp.task.terminusTestSender incoming from sender to router
            # The 'out' link is on address M0pulp.task.terminusTestSender outgoing from router C to router B
            for result in out.results:
                if result[link_dir_index] == 'in' and result[owning_addr_index] == 'M0pulp.task.terminusTestSender':
                    self.in_sender_found = True
                if result[link_dir_index] == 'out' and result[owning_addr_index] == 'M0pulp.task.terminusTestSender':
                    self.out_sender_found = True

        # Shutdown the connections only if the on_link_opened has been called for sender and receiver links.
        if self.sender_link_opened and self.receiver_link_opened:
            self.sender.close()
            self.receiver.close()
            self.sender_connection.close()
            self.receiver_connection.close()

    def run(self):
        Container(self).run()


class MultiLinkSendReceive(MessagingHandler):
    class SendState:
        def __init__(self, link):
            self.link = link
            self.sent = False
            self.accepted = False
            self.done = False
            self.closed = False

        def send(self, subject, body):
            if not self.sent:
                self.link.send(Message(subject=subject, body=body, address=self.link.target.address))
                self.sent = True

        def on_accepted(self):
            self.accepted = True
            self.done = True

        def close(self):
            if not self.closed:
                self.closed = True
                self.link.close()
                self.link.connection.close()

    class RecvState:
        def __init__(self, link):
            self.link = link
            self.received = False
            self.done = False
            self.closed = False

        def on_message(self):
            self.received = True
            self.done = True

        def close(self):
            if not self.closed:
                self.closed = True
                self.link.close()
                self.link.connection.close()

    def __init__(self, send_urls, recv_urls, name, message=None):
        super(MultiLinkSendReceive, self).__init__()
        self.send_urls = send_urls
        self.recv_urls = recv_urls
        self.senders = {}
        self.receivers = {}
        self.message = message or "SendReceiveTest"
        self.sent = False
        self.error = None
        self.name = name

    def close(self):
        for sender in self.senders.values():
            sender.close()
        for receiver in self.receivers.values():
            receiver.close()

    def all_done(self):
        for sender in self.senders.values():
            if not sender.done:
                return False
        for receiver in self.receivers.values():
            if not receiver.done:
                return False
        return True

    def timeout(self):
        self.error = "Timeout Expired"
        self.close()

    def stop_if_all_done(self):
        if self.all_done():
            self.stop()

    def stop(self):
        self.close()
        self.timer.cancel()

    def on_start(self, event):
        self.timer      = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        event.container.container_id = None
        for u in self.send_urls:
            s = self.SendState(event.container.create_sender(u, name=self.name))
            self.senders[s.link.connection.container] = s
        for u in self.recv_urls:
            r = self.RecvState(event.container.create_receiver(u, name=self.name))
            self.receivers[r.link.connection.container] = r

    def on_sendable(self, event):
        self.senders[event.connection.container].send(self.name, self.message)

    def on_message(self, event):
        if self.message != event.message.body:
            error = "Incorrect message. Got %s, expected %s" % (event.message.body, self.message.body)
        self.receivers[event.connection.container].on_message()
        self.stop_if_all_done()

    def on_accepted(self, event):
        self.senders[event.connection.container].on_accepted()
        self.stop_if_all_done()

    def run(self):
        Container(self).run()


class LinkRouteProtocolTest(TestCase):
    """
    Test link route implementation against "misbehaving" containers

    Uses a custom fake broker (not a router) that can do weird things at the
    protocol level.

             +-------------+         +---------+         +-----------------+
             |             | <------ |         | <-----  | blocking_sender |
             | fake broker |         |  QDR.A  |         +-----------------+
             |             | ------> |         | ------> +-------------------+
             +-------------+         +---------+         | blocking_receiver |
                                                         +-------------------+
    """
    @classmethod
    def setUpClass(cls):
        """Configure and start QDR.A"""
        super(LinkRouteProtocolTest, cls).setUpClass()
        config = [
            ('router', {'mode': 'standalone', 'id': 'QDR.A'}),
            # for client connections:
            ('listener', {'role': 'normal',
                          'host': '0.0.0.0',
                          'port': cls.tester.get_port(),
                          'saslMechanisms': 'ANONYMOUS'}),
            # to connect to the fake broker
            ('connector', {'name': 'broker',
                           'role': 'route-container',
                           'host': '127.0.0.1',
                           'port': cls.tester.get_port(),
                           'saslMechanisms': 'ANONYMOUS'}),

            # forward 'org.apache' messages to + from fake broker:
            ('linkRoute', {'prefix': 'org.apache', 'containerId': 'FakeBroker', 'direction': 'in'}),
            ('linkRoute', {'prefix': 'org.apache', 'containerId': 'FakeBroker', 'direction': 'out'})
        ]
        config = Qdrouterd.Config(config)
        cls.router = cls.tester.qdrouterd('A', config, wait=False)

    def _fake_broker(self, cls):
        """Spawn a fake broker listening on the broker's connector
        """
        fake_broker = cls(self.router.connector_addresses[0])
        # wait until the connection to the fake broker activates
        self.router.wait_connectors()
        return fake_broker

    def test_DISPATCH_1092(self):
        # This fake broker will force the session closed after the link
        # detaches.  Verify that the session comes back up correctly when the
        # next client attaches
        killer = self._fake_broker(SessionKiller)
        for i in range(2):
            bconn = BlockingConnection(self.router.addresses[0])
            bsender = bconn.create_sender(address="org.apache",
                                          options=AtLeastOnce())
            msg = Message(body="Hey!")
            bsender.send(msg)
            bsender.close()
            bconn.close()
        killer.join()


class SessionKiller(FakeBroker):
    """DISPATCH-1092: force a session close when the link closes.  This should
    cause the router to re-create the session when the next client attaches.
    """

    def __init__(self, url):
        super(SessionKiller, self).__init__(url)

    def on_link_closing(self, event):
        event.link.close()
        event.session.close()


class FakeBrokerDrain(FakeBroker):
    """
    DISPATCH-1496 - Make sure that the router does not grant additional credit
    when drain is issued by a receiver connected to the router on a
    link routed address
    """

    def __init__(self, url):
        super(FakeBrokerDrain, self).__init__(url)
        self.first_flow_received = False
        self.first_drain_mode = False
        self.second_drain_mode = False
        self.error = None
        self.num_flows = 0
        self.success = False

    def on_link_flow(self, event):
        if event.link.is_sender:
            if event.sender.drain_mode:
                if not self.first_drain_mode:
                    self.first_drain_mode = True
                    event.sender.drained()
                elif not self.second_drain_mode:
                    self.second_drain_mode = True
                    if event.link.credit == 1000:
                        # Without the patch for DISPATCH-1496,
                        # the event.link.credit value would be 2000
                        self.success = True
                    else:
                        self.success = False
                    event.sender.drained()
            else:
                if not self.first_flow_received:
                    self.first_flow_received = True
                    msg = Message(body="First Drain Transfer")
                    event.link.send(msg)


class DrainReceiver(MessagingHandler):
    def __init__(self, url, fake_broker):
        super(DrainReceiver, self).__init__(prefetch=0, auto_accept=False)
        self.url = url
        self.received = 0
        self.receiver = None
        self.first_drain_sent = False
        self.second_drain_sent = False
        self.first_flow_sent = False
        self.receiver_conn = None
        self.error = None
        self.num_flows = 0
        self.fake_broker = fake_broker

    def on_start(self, event):
        self.receiver_conn = event.container.connect(self.url)
        self.receiver = event.container.create_receiver(self.receiver_conn, "org.apache")

        # Step 1: Send a flow of 1000 to the router. The router will forward this
        #   flow to the FakeBroker
        self.receiver.flow(1000)
        self.first_flow_sent = True

    def on_link_flow(self, event):
        if event.receiver == self.receiver:
            self.num_flows += 1
            if self.num_flows == 1:
                # Step 4: The response drain received from the FakeBroker
                # Step 5: Send second flow of 1000 credits. This is forwarded to the FakeBroker
                self.receiver.flow(1000)
                self.timer = event.reactor.schedule(3, TestTimeout(self))
            elif self.num_flows == 2:
                if not self.fake_broker.success:
                    self.error = "The FakeBroker did not receive correct credit of 1000"
                self.receiver_conn.close()

    def timeout(self):
        # Step 6: The second drain is sent to the router. The router was forwarding the wrong credit (2000) to the FakeBroker
        # but with the fix for DISPATCH-1496, the correct credit is forwarded (1000)
        self.receiver.drain(0)
        self.second_drain_sent = True

    def on_message(self, event):
        if event.receiver == self.receiver:
            self.received += 1

            # Step 2: In response to Step 1, the broker has sent the only message in its queue
            if self.received == 1:
                self.first_drain_sent = True
                #print ("First message received. Doing first drain")
                # Step 3: The receiver drains after receiving the first message.
                # This drain is forwarded to the FakeBroker
                self.receiver.drain(0)

    def run(self):
        Container(self).run()


class LinkRouteDrainTest(TestCase):
    """
    Test link route drain implementation.
    DISPATCH-1496 alleges that the router is granting extra credit when
    forwarding the drain.

    Uses a router which connects to a FakeBroker (FB)

             +-------------+         +---------+
             |             | <------ |         |
             | fake broker |         |  QDR.A  |
             |             | ------> |         | ------> +-------------------+
             +-------------+         +---------+         | receiver          |
                                                         +-------------------+
    The router will grant extra credit when the following sequence is used
    1. The receiver attaches to the router on a a link routed address called "org.apache"
    2. Receiver issues a flow of 1000. The FakeBroker has only one message in its
       "examples" queue and it sends it over to the router which forwards it to the receiver
    3. After receiving the message the receiver issues a drain(0). This drain is
       forwarded to the FakeBroker by the router and the FB responds. There
       is not problem with this drain
    4. The receiver again gives a flow of 1000 and it is forwarded to the FB. There
       are no messages in the broker queue, so the FB sends no messages
    5. The receiver again issues a drain(0). At this time, without the fix for
       DISPATCH-1496, the router issues double the credit to the FB. Instead
       of issuing a credit of 1000, it issues a credit of 2000.
    """
    @classmethod
    def setUpClass(cls):
        """Configure and start QDR.A"""
        super(LinkRouteDrainTest, cls).setUpClass()
        config = [
            ('router', {'mode': 'standalone', 'id': 'QDR.A'}),
            # for client connections:
            ('listener', {'role': 'normal',
                          'host': '0.0.0.0',
                          'port': cls.tester.get_port(),
                          'saslMechanisms': 'ANONYMOUS'}),
            # to connect to the fake broker
            ('connector', {'name': 'broker',
                           'role': 'route-container',
                           'host': '127.0.0.1',
                           'port': cls.tester.get_port(),
                           'saslMechanisms': 'ANONYMOUS'}),

            # forward 'org.apache' messages to + from fake broker:
            ('linkRoute', {'prefix': 'org.apache', 'containerId': 'FakeBroker', 'direction': 'in'}),
            ('linkRoute', {'prefix': 'org.apache', 'containerId': 'FakeBroker', 'direction': 'out'})
        ]
        config = Qdrouterd.Config(config)
        cls.router = cls.tester.qdrouterd('A', config, wait=False)

    def _fake_broker(self, cls):
        """Spawn a fake broker listening on the broker's connector
        """
        fake_broker = cls(self.router.connector_addresses[0])
        # wait until the connection to the fake broker activates
        self.router.wait_connectors()
        return fake_broker

    def test_DISPATCH_1496(self):
        fake_broker = self._fake_broker(FakeBrokerDrain)
        drain_receiver = DrainReceiver(self.router.addresses[0], fake_broker)
        drain_receiver.run()
        self.assertEqual(drain_receiver.error, None)


class EmptyTransferTest(TestCase):
    """Verify empty tranfer frames (no body) do not crash the router.  See
    DISPATCH-1988.
    """

    # various identifiers defined by AMQP 1.0
    OPEN_DESCRIPTOR = 0x10
    BEGIN_DESCRIPTOR = 0x11
    ATTACH_DESCRIPTOR = 0x12
    FLOW_DESCRIPTOR = 0x13
    TRANSFER_DESCRIPTOR = 0x14
    DISPO_DESCRIPTOR = 0x15
    ACCEPTED_OUTCOME = 0x24
    REJECTED_OUTCOME = 0x25
    TARGET_DESCRIPTOR = 0x29
    MA_SECTION_DESCRIPTOR = 0x73
    BODY_SECTION_DESCRIPTOR = 0x77

    @classmethod
    def setUpClass(cls):
        super(EmptyTransferTest, cls).setUpClass()
        cls.ROUTER_LISTEN_PORT = cls.tester.get_port()

        config = [
            ('router', {'mode': 'standalone', 'id': 'QDR.A'}),
            # the client will connect to this listener
            ('listener', {'role': 'normal',
                          'port': cls.ROUTER_LISTEN_PORT,
                          'saslMechanisms': 'ANONYMOUS'}),
            # to connect to the fake broker
            ('connector', {'name': 'broker',
                           'role': 'route-container',
                           'host': '127.0.0.1',
                           'port': cls.tester.get_port(),
                           'saslMechanisms': 'ANONYMOUS'}),
            ('linkRoute',
             {'prefix': 'examples', 'containerId': 'FakeBroker',
              'direction': 'in'}),
            ('linkRoute',
             {'prefix': 'examples', 'containerId': 'FakeBroker',
              'direction': 'out'})
        ]
        config = Qdrouterd.Config(config)
        cls.router = cls.tester.qdrouterd('A', config, wait=False)

    def _fake_broker(self, cls):
        """
        Spawn a fake broker listening on the broker's connector
        """
        fake_broker = cls(self.router.connector_addresses[0])
        # wait until the connection to the fake broker activates
        self.router.wait_connectors()
        return fake_broker

    def _find_frame(self, data: bytes, code: int) -> Optional[list]:
        """Scan a byte sequence for performatives that match code.
        Return the frame body (list) if match else None
        """
        while data:
            # starts at frame header (8 bytes)
            frame_len = int.from_bytes(data[:4], "big")
            if frame_len == 0 or frame_len > len(data):
                return None
            desc = Data()
            desc.decode(data[8:frame_len])  # skip frame header
            data = data[frame_len:]    # advance to next frame
            desc.rewind()
            if desc.next() is None:
                return None
            if not desc.is_described():
                return None
            py_desc = desc.get_py_described()
            if py_desc.descriptor == code:
                return py_desc.value
        return None

    def _send_frame(self, frame: Data, sock: socket.socket):
        """Encode and send frame over sock
        """
        frame.rewind()
        fbytes = frame.encode()
        flen = len(fbytes) + 8
        # AMQP FRAME HEADER: 4 byte length, DOFF, TYPE, CHANNEL
        sock.sendall(flen.to_bytes(4, "big"))
        sock.sendall(bytes([2, 0, 0, 0]))
        sock.sendall(fbytes)

    def _construct_transfer(self, delivery_id, tag, more=False, add_ma=False,
                            add_body=False) -> Data:
        """Construct a Transfer frame in a proton Data object
        """
        t1_frame = Data()
        t1_frame.put_described()
        t1_frame.enter()
        t1_frame.put_ulong(self.TRANSFER_DESCRIPTOR)
        t1_frame.put_list()
        t1_frame.enter()
        t1_frame.put_uint(0)  # handle
        t1_frame.put_uint(delivery_id)
        t1_frame.put_binary(tag)
        t1_frame.put_uint(0)           # msg format
        t1_frame.put_bool(False)       # settled
        t1_frame.put_bool(more)
        t1_frame.exit()   # transfer list
        t1_frame.exit()   # transfer described type
        if add_ma:
            t1_frame.put_described()
            t1_frame.enter()
            t1_frame.put_ulong(self.MA_SECTION_DESCRIPTOR)
            t1_frame.put_list()
            t1_frame.enter()
            t1_frame.put_ulong(9)
            t1_frame.exit()  # list
            t1_frame.exit()  # described
        if add_body:
            t1_frame.put_described()
            t1_frame.enter()
            t1_frame.put_ulong(self.BODY_SECTION_DESCRIPTOR)
            t1_frame.put_string("I'm a small body!")
            t1_frame.exit()
            t1_frame.exit()

        return t1_frame

    def _get_outcome(self, dispo_frame: list) -> Optional[int]:
        """Extract the outcome from a raw disposition frame"""
        outcome = None
        if len(dispo_frame) >= 5:  # list[5] == state
            if isinstance(dispo_frame[4], Described):
                outcome = dispo_frame[4].descriptor
        return outcome

    def _read_socket(self, sock: socket.socket,
                     timeout: float = 1.0) -> bytes:
        """Read all available data from the socket, waiting up to 1 second for
        data to arrive
        """
        old_timeout = sock.gettimeout()
        sock.settimeout(timeout)
        data = b''
        while True:
            try:
                incoming = sock.recv(4096)
                if not incoming:
                    break
                data += incoming
            except OSError:  # timeout
                break
        sock.settimeout(old_timeout)
        return data

    def test_DISPATCH_1988(self):
        fake_broker = self._fake_broker(FakeBroker)

        self.router.wait_ready()
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(TIMEOUT)
        # Connect to the router listening port and send an amqp, open,
        # begin, attach. The attach is sent on the link
        # routed address, "examples"
        s.connect(("127.0.0.1", EmptyTransferTest.ROUTER_LISTEN_PORT))

        # send 'AMQP 1 0' preamble
        s.sendall(b'\x41\x4d\x51\x50\x00\x01\x00\x00')

        # send Open/Begin/Attach
        open_frame = Data()
        open_frame.put_described()
        open_frame.enter()
        open_frame.put_ulong(self.OPEN_DESCRIPTOR)
        open_frame.put_list()
        open_frame.enter()
        open_frame.put_string("TestContainer")
        open_frame.exit()
        open_frame.exit()
        open_frame.rewind()

        begin_frame = Data()
        begin_frame.put_described()
        begin_frame.enter()
        begin_frame.put_ulong(self.BEGIN_DESCRIPTOR)
        begin_frame.put_list()
        begin_frame.enter()
        begin_frame.put_null()
        begin_frame.put_uint(0)   # next out id
        begin_frame.put_uint(0xfffff)  # in/out window
        begin_frame.put_uint(0xfffff)
        begin_frame.exit()
        begin_frame.exit()
        begin_frame.rewind()

        attach_frame = Data()
        attach_frame.put_described()
        attach_frame.enter()
        attach_frame.put_ulong(self.ATTACH_DESCRIPTOR)
        attach_frame.put_list()
        attach_frame.enter()
        attach_frame.put_string("test-link-name")
        attach_frame.put_uint(0)      # handle
        attach_frame.put_bool(False)  # sender
        attach_frame.put_null()
        attach_frame.put_null()
        attach_frame.put_null()
        # target:
        attach_frame.put_described()
        attach_frame.enter()
        attach_frame.put_ulong(self.TARGET_DESCRIPTOR)
        attach_frame.put_list()
        attach_frame.enter()
        attach_frame.put_string("examples/foo")
        attach_frame.exit()  # target list
        attach_frame.exit()  # target descriptor
        attach_frame.exit()    # attach list
        attach_frame.exit()    # attach descriptor
        attach_frame.rewind()

        for frame in [open_frame, begin_frame, attach_frame]:
            self._send_frame(frame, s)

        # Give time for the attach to propagate to the broker and
        # for the broker to send a response attach and flow:
        data = self._read_socket(s, timeout=2.0)
        self.assertTrue(len(data) > 8)
        self.assertEqual(data[:8], b'AMQP\x00\x01\x00\x00')
        # expect that the connection was accepted: check for a flow frame:
        flow_frame = self._find_frame(data[8:], self.FLOW_DESCRIPTOR)
        self.assertIsNotNone(flow_frame, "no flow frame received: %s" % data)

        # First send a message on link routed address "examples" with a small
        # message body. Verify the the sent message has been accepted.
        t1_frame = self._construct_transfer(0, b'\x01', add_ma=True, add_body=True)
        self._send_frame(t1_frame, s)

        # We expect to get a disposition frame that accepted the message
        data = self._read_socket(s)
        self.assertTrue(len(data) > 0)
        dispo_frame = self._find_frame(data, self.DISPO_DESCRIPTOR)
        self.assertIsNotNone(dispo_frame,
                             "expected a disposition (none arrived!): %s"
                             % data)

        outcome = self._get_outcome(dispo_frame)
        self.assertEqual(self.ACCEPTED_OUTCOME, outcome,
                         "Transfer not accepted (unexpected!) actual=%s"
                         % outcome)

        # Test case 1
        #
        # Send an empty transfer frame to the router and you should receive a
        # rejected disposition from the router.  Without the fix for
        # DISPATCH_1988, upon sending this EMPTY_TRANSFER the router crashes
        # with the following assert
        #
        # qpid-dispatch/src/message.c:1260: qd_message_add_fanout: Assertion `content->pending && qd_buffer_size(content->pending) > 0' failed.

        t2_frame = self._construct_transfer(1, b'\x02')
        self._send_frame(t2_frame, s)

        data = self._read_socket(s)
        self.assertTrue(len(data) > 0)
        dispo_frame = self._find_frame(data, self.DISPO_DESCRIPTOR)
        self.assertIsNotNone(dispo_frame,
                             "expected a disposition (none arrived!): %s"
                             % data)
        outcome = self._get_outcome(dispo_frame)
        self.assertEqual(self.REJECTED_OUTCOME, outcome,
                         "Transfer not rejected (unexpected!) actual=%s"
                         % outcome)

        # Test case 2
        # Now, send two empty transfer frames, first transfer has more=true and
        # the next transfer has more=false.  This will again be rejected by the
        # router.

        t3_frame = self._construct_transfer(2, b'\x03', more=True)
        self._send_frame(t3_frame, s)
        t4_frame = self._construct_transfer(2, b'\x03')
        self._send_frame(t4_frame, s)

        data = self._read_socket(s)
        self.assertTrue(len(data) > 0)
        dispo_frame = self._find_frame(data, self.DISPO_DESCRIPTOR)
        self.assertIsNotNone(dispo_frame,
                             "expected a disposition (none arrived!): %s"
                             % data)
        outcome = self._get_outcome(dispo_frame)
        self.assertEqual(self.REJECTED_OUTCOME, outcome,
                         "Transfer not rejected (unexpected!) actual: %s"
                         % outcome)

        # Now send a good transfer and ensure the router accepts it
        t5_frame = self._construct_transfer(3, b'\x04', add_ma=True, add_body=True)
        self._send_frame(t5_frame, s)

        data = self._read_socket(s)
        self.assertTrue(len(data) > 0)
        dispo_frame = self._find_frame(data, self.DISPO_DESCRIPTOR)
        self.assertIsNotNone(dispo_frame,
                             "expected a disposition (none arrived!): %s"
                             % data)
        outcome = self._get_outcome(dispo_frame)
        self.assertEqual(self.ACCEPTED_OUTCOME, outcome,
                         "Transfer not accepted (unexpected!) actual: %s"
                         % outcome)

        s.close()
        fake_broker.join()


class ConnectionLinkRouteTest(TestCase):
    """
    Test connection scoped link route implementation

    Base configuration:

                                                        +-----------------+
                           +---------+    +---------+<--| blocking_sender |
    +-----------------+    |         |    |         |   +-----------------+
    | Fake LR Service |<==>|  QDR.A  |<==>|  QDR.B  |
    +-----------------+    |         |    |         |   +-------------------+
                           +---------+    +---------+-->| blocking_receiver |
                                                        +-------------------+

    The Fake Link Route Service will create connection-scoped link routes to
    QDR.A, while blocking sender/receivers on QDR.B will send/receive messages
    via the link route.
    """

    _AS_TYPE = "org.apache.qpid.dispatch.router.connection.linkRoute"

    @classmethod
    def setUpClass(cls):
        super(ConnectionLinkRouteTest, cls).setUpClass()

        b_port = cls.tester.get_port()
        configs = [
            # QDR.A:
            [('router', {'mode': 'interior', 'id': 'QDR.A'}),
             # for fake connection-scoped LRs:
             ('listener', {'role': 'normal',
                           'host': '0.0.0.0',
                           'port': cls.tester.get_port(),
                           'saslMechanisms': 'ANONYMOUS'}),
             # for fake route-container LR connections:
             ('listener', {'role': 'route-container',
                           'host': '0.0.0.0',
                           'port': cls.tester.get_port(),
                           'saslMechanisms': 'ANONYMOUS'}),
             # to connect to the QDR.B
             ('connector', {'role': 'inter-router',
                            'host': '127.0.0.1',
                            'port': b_port,
                            'saslMechanisms': 'ANONYMOUS'})],
            # QDR.B:
            [('router', {'mode': 'interior', 'id': 'QDR.B'}),
             # for client connections
             ('listener', {'role': 'normal',
                           'host': '0.0.0.0',
                           'port': cls.tester.get_port(),
                           'saslMechanisms': 'ANONYMOUS'}),
             # for connection to QDR.A
             ('listener', {'role': 'inter-router',
                           'host': '0.0.0.0',
                           'port': b_port,
                           'saslMechanisms': 'ANONYMOUS'})]
        ]

        cls.routers = []
        for c in configs:
            config = Qdrouterd.Config(c)
            cls.routers.append(cls.tester.qdrouterd(config=config, wait=False))
        cls.QDR_A = cls.routers[0]
        cls.QDR_B = cls.routers[1]
        cls.QDR_A.wait_router_connected('QDR.B')
        cls.QDR_B.wait_router_connected('QDR.A')

    def _get_address(self, mgmt, addr):
        a_type = 'org.apache.qpid.dispatch.router.address'
        return [a for a in mgmt.query(a_type) if a['name'].endswith(addr)]

    def test_config_file_bad(self):
        # verify that specifying a connection link route in the configuration
        # file fails
        config = [('router', {'mode': 'interior', 'id': 'QDR.X'}),
                  ('listener', {'role': 'normal',
                                'host': '0.0.0.0',
                                'port': self.tester.get_port(),
                                'saslMechanisms': 'ANONYMOUS'}),

                  ('connection.linkRoute',
                   {'pattern': "i/am/bad",
                    'direction': "out"})
                  ]

        cfg = Qdrouterd.Config(config)
        # we expect the router to fail
        router = self.tester.qdrouterd("X", cfg, wait=False, expect=Process.EXIT_FAIL)  # type: Qdrouterd
        self.assertEqual(router.wait(TIMEOUT), Process.EXIT_FAIL)

    def test_mgmt(self):
        # test create, delete, and query
        mgmt_conn = BlockingConnection(self.QDR_A.addresses[0])
        mgmt_proxy = ConnLinkRouteMgmtProxy(mgmt_conn)

        for i in range(10):
            rsp = mgmt_proxy.create_conn_link_route("lr1-%d" % i,
                                                    {'pattern': "*/hi/there/%d" % i,
                                                     'direction':
                                                     'out' if i % 2 else 'in'})
            self.assertEqual(201, rsp.status_code)

        # test query
        rsp = mgmt_proxy.query_conn_link_routes()
        self.assertEqual(200, rsp.status_code)
        self.assertEqual(10, len(rsp.results))
        entities = rsp.results

        # test read
        rsp = mgmt_proxy.read_conn_link_route('lr1-5')
        self.assertEqual(200, rsp.status_code)
        self.assertEqual("lr1-5", rsp.attrs['name'])
        self.assertEqual("*/hi/there/5", rsp.attrs['pattern'])
        self.assertEqual(mgmt_conn.container.container_id,
                         rsp.attrs['containerId'])

        # bad creates
        attrs = [{'pattern': "bad", 'direction': "bad"},
                 {'direction': 'in'},
                 {},
                 {'pattern': ''},
                 {'pattern': 7}]
        for a in attrs:
            rsp = mgmt_proxy.create_conn_link_route("iamnoone", a)
            self.assertEqual(400, rsp.status_code)

        # bad read
        rsp = mgmt_proxy.read_conn_link_route('iamnoone')
        self.assertEqual(404, rsp.status_code)

        # bad delete
        rsp = mgmt_proxy.delete_conn_link_route('iamnoone')
        self.assertEqual(404, rsp.status_code)

        # delete all
        for r in entities:
            self.assertEqual(200, r.status_code)
            rsp = mgmt_proxy.delete_conn_link_route(r.attrs['name'])
            self.assertEqual(204, rsp.status_code)

        # query - should be none left
        rsp = mgmt_proxy.query_conn_link_routes()
        self.assertEqual(200, rsp.status_code)
        self.assertEqual(0, len(rsp.results))

    def test_address_propagation(self):
        # test service that creates and deletes connection link routes
        fs = ConnLinkRouteService(self.QDR_A.addresses[1], container_id="FakeService",
                                  config=[("clr1",
                                           {"pattern": "flea.*",
                                            "direction": "out"}),
                                          ("clr2",
                                           {"pattern": "flea.*",
                                            "direction": "in"})])
        self.assertEqual(2, len(fs.values))

        # the address should propagate to A and B
        self.QDR_A.wait_address(address="flea.*", count=2)
        self.QDR_B.wait_address(address="flea.*", count=2)

        # now have the service delete the config
        fs.delete_config()

        # eventually the addresses will be un-published
        mgmt_A = QdManager(address=self.QDR_A.addresses[0])
        mgmt_B = QdManager(address=self.QDR_B.addresses[0])
        deadline = time() + TIMEOUT
        while (self._get_address(mgmt_A, "flea.*")
               or self._get_address(mgmt_B, "flea.*")):
            self.assertTrue(time() < deadline)
            sleep(0.1)

        fs.join()

    # simple forwarding tests with auto delete
    def test_send_receive(self):
        COUNT = 5
        mgmt_A = QdManager(address=self.QDR_A.addresses[0])
        mgmt_B = QdManager(address=self.QDR_B.addresses[0])

        # connect broker to A route-container
        fs = ConnLinkRouteService(self.QDR_A.addresses[1], container_id="FakeService",
                                  config=[("clr1",
                                           {"pattern": "flea.*",
                                            "direction": "out"}),
                                          ("clr2",
                                           {"pattern": "flea.*",
                                            "direction": "in"})])
        self.assertEqual(2, len(fs.values))

        # wait for the address to propagate to B
        self.QDR_B.wait_address(address="flea.*", count=2)

        # ensure the link routes are not visible via other connections
        clrs = mgmt_A.query(self._AS_TYPE)
        self.assertEqual(0, len(clrs))

        # send from A to B
        r = AsyncTestReceiver(self.QDR_B.addresses[0],
                              "flea.B",
                              container_id="flea.BReceiver")
        s = AsyncTestSender(self.QDR_A.addresses[0],
                            "flea.B",
                            container_id="flea.BSender",
                            message=Message(body="SENDING TO flea.B"),
                            count=COUNT)
        s.wait()   # for sender to complete
        for i in range(COUNT):
            self.assertEqual("SENDING TO flea.B",
                             r.queue.get(timeout=TIMEOUT).body)
        r.stop()
        self.assertEqual(COUNT, fs.in_count)

        # send from B to A
        r = AsyncTestReceiver(self.QDR_A.addresses[0],
                              "flea.A",
                              container_id="flea.AReceiver")
        s = AsyncTestSender(self.QDR_B.addresses[0],
                            "flea.A",
                            container_id="flea.ASender",
                            message=Message(body="SENDING TO flea.A"),
                            count=COUNT)
        s.wait()
        for i in range(COUNT):
            self.assertEqual("SENDING TO flea.A",
                             r.queue.get(timeout=TIMEOUT).body)
        r.stop()
        self.assertEqual(2 * COUNT, fs.in_count)

        # once the fake service closes its conn the link routes
        # are removed so the link route addresses must be gone
        fs.join()

        mgmt_A = QdManager(address=self.QDR_A.addresses[0])
        mgmt_B = QdManager(address=self.QDR_B.addresses[0])
        deadline = time() + TIMEOUT
        while (self._get_address(mgmt_A, "flea.*")
               or self._get_address(mgmt_B, "flea.*")):
            self.assertTrue(time() < deadline)
            sleep(0.1)


class ConnLinkRouteService(FakeBroker):
    def __init__(self, url, container_id, config, timeout=TIMEOUT):
        self.conn = None
        self.mgmt_proxy = None
        self.mgmt_sender = None
        self.mgmt_receiver = None
        self._config = config
        self._config_index = 0
        self._config_done = Event()
        self._config_error = None
        self._config_values = []
        self._cleaning_up = False
        self._delete_done = Event()
        self._delete_count = 0
        self._event_injector = EventInjector()
        self._delete_event = ApplicationEvent("delete_config")
        super(ConnLinkRouteService, self).__init__(url, container_id)
        if self._config_done.wait(timeout) is False:
            raise Exception("Timed out waiting for configuration setup")
        if self._config_error is not None:
            raise Exception("Error: %s" % self._config_error)

    @property
    def values(self):
        return self._config_values

    def delete_config(self):
        self._event_injector.trigger(self._delete_event)
        if self._delete_done.wait(TIMEOUT) is False:
            raise Exception("Timed out waiting for configuration delete")

    def on_start(self, event):
        """
        Do not create an acceptor, actively connect instead
        """
        event.container.selectable(self._event_injector)
        self.conn = event.container.connect(self.url)

    def on_connection_opened(self, event):
        if event.connection == self.conn:
            if self.mgmt_receiver is None:
                self.mgmt_receiver = event.container.create_receiver(self.conn,
                                                                     dynamic=True)
        super(ConnLinkRouteService, self).on_connection_opened(event)

    def on_connection_closed(self, event):
        if self._event_injector:
            self._event_injector.close()
            self._event_injector = None
        super(ConnLinkRouteService, self).on_connection_closed(event)

    def on_link_opened(self, event):
        if event.link == self.mgmt_receiver:
            self.mgmt_proxy = MgmtMsgProxy(self.mgmt_receiver.remote_source.address)
            self.mgmt_sender = event.container.create_sender(self.conn,
                                                             target="$management")

    def on_link_error(self, event):
        # when a remote client disconnects the service will get a link error
        # that is expected - simply clean up the link
        self.on_link_closing(event)

    def on_sendable(self, event):
        if event.sender == self.mgmt_sender:
            if not self._cleaning_up:
                if self._config_index < len(self._config):
                    cfg = self._config[self._config_index]
                    msg = self.mgmt_proxy.create_conn_link_route(cfg[0], cfg[1])
                    self.mgmt_sender.send(msg)
                    self._config_index += 1
            elif self._config_values:
                cv = self._config_values.pop()
                msg = self.mgmt_proxy.delete_conn_link_route(cv['name'])
                self._delete_count += 1
        else:
            super(ConnLinkRouteService, self).on_sendable(event)

    def on_message(self, event):
        if event.receiver == self.mgmt_receiver:
            response = self.mgmt_proxy.response(event.message)
            if response.status_code == 201:
                # created:
                self._config_values.append(response.attrs)
                if len(self._config_values) == len(self._config):
                    self._config_done.set()
            elif response.status_code == 204:
                # deleted
                self._delete_count -= 1
                if (not self._config_values) and self._delete_count == 0:
                    self._delete_done.set()
            else:
                # error
                self._config_error = ("mgmt failed: %s" %
                                      response.status_description)
                self._config_done.set()
                self._delete_done.set()
        else:
            super(ConnLinkRouteService, self).on_message(event)

    def on_delete_config(self, event):
        if not self._cleaning_up:
            self._cleaning_up = True
            if not self._config_values:
                self._delete_done.set()
            else:
                try:
                    while self.mgmt_sender.credit > 0:
                        cv = self._config_values.pop()
                        msg = self.mgmt_proxy.delete_conn_link_route(cv["name"])
                        self.mgmt_sender.send(msg)
                        self._delete_count += 1
                except IndexError:
                    pass


class ConnLinkRouteMgmtProxy:
    """
    Manage connection scoped link routes over a given connection.
    While the connection remains open the connection scoped links will remain
    configured and active
    """

    def __init__(self, bconn, credit=250):
        self._receiver = bconn.create_receiver(address=None, dynamic=True, credit=credit)
        self._sender = bconn.create_sender(address="$management")
        self._proxy = MgmtMsgProxy(self._receiver.link.remote_source.address)

    def __getattr__(self, key):
        # wrap accesses to the management message functions so we can send and
        # receive the messages using the blocking links
        f = getattr(self._proxy, key)
        if not callable(f):
            return f

        def _func(*args, **kwargs):
            self._sender.send(f(*args, **kwargs))
            return self._proxy.response(self._receiver.receive())
        return _func


class InvalidTagTest(MessagingHandler):
    """Verify that a message with an invalid tag length is rejected
    """

    def __init__(self, router_addr):
        super(InvalidTagTest, self).__init__(auto_accept=False, auto_settle=False)
        self.test_conn = None
        self.test_address = router_addr
        self.tx_ct = 0
        self.accept_ct = 0
        self.reject_ct = 0
        self.error = None

    def timeout(self):
        self.error = "Timeout expired: sent=%d rcvd=%d" % (self.tx_ct,
                                                           self.accept_ct
                                                           + self.reject_ct)
        if self.test_conn:
            self.test_conn.close()

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.test_conn = event.container.connect(self.test_address)
        rx = event.container.create_receiver(self.test_conn, "org.apache.foo")

    def on_link_opened(self, event):
        if event.receiver:
            event.receiver.flow(100)
            event.container.create_sender(event.connection, "org.apache.foo")

    def on_sendable(self, event):
        if self.tx_ct < 10:
            self.tx_ct += 1
            if self.tx_ct == 5:
                event.sender.send(Message(body="YO"), tag=str("X" * 64))
            else:
                event.sender.send(Message(body="YO"), tag=str("BLAH%d" %
                                                              self.tx_ct))

    def on_accepted(self, event):
        self.accept_ct += 1
        event.delivery.settle()
        if self.accept_ct == 9 and self.reject_ct == 1:
            event.connection.close()
            self.timer.cancel()

    def on_rejected(self, event):
        self.reject_ct += 1
        event.delivery.settle()

    def on_message(self, event):
        event.delivery.update(Delivery.ACCEPTED)
        event.delivery.settle()

    def run(self):
        Container(self).run()


class Dispatch1428(TestCase):
    """
    Sets up 2 routers (one of which are acting as brokers (QDR.A)).

        QDR.A acting broker #1
             +---------+         +---------+
             |         | <------ |         |
             |  QDR.A  |         |  QDR.B  |
             |         | ------> |         |
             +---------+         +---------+

    """
    @classmethod
    def get_router(cls, index):
        return cls.routers[index]

    @classmethod
    def setUpClass(cls):
        """Start two routers"""
        super(Dispatch1428, cls).setUpClass()

        def router(name, connection):

            config = [
                ('router', {'mode': 'interior', 'id': 'QDR.%s' % name}),
            ] + connection

            config = Qdrouterd.Config(config)
            cls.routers.append(cls.tester.qdrouterd(name, config, wait=False))

        cls.routers = []
        a_listener_port = cls.tester.get_port()
        b_listener_port = cls.tester.get_port()

        router('A',
               [
                   ('listener', {'role': 'normal', 'host': '0.0.0.0', 'port': a_listener_port, 'saslMechanisms': 'ANONYMOUS'}),
               ])
        router('B',
               [
                   ('listener', {'role': 'normal', 'host': '0.0.0.0', 'port': b_listener_port, 'saslMechanisms': 'ANONYMOUS'}),
                   ('connector', {'name': 'one', 'role': 'route-container', 'host': '0.0.0.0', 'port': a_listener_port, 'saslMechanisms': 'ANONYMOUS'}),
                   ('connector', {'name': 'two', 'role': 'route-container', 'host': '0.0.0.0', 'port': a_listener_port, 'saslMechanisms': 'ANONYMOUS'})
               ]
               )
        sleep(2)

    def run_qdmanage(self, cmd, input=None, expect=Process.EXIT_OK, address=None):
        p = self.popen(
            ['qdmanage'] + cmd.split(' ') + ['--bus', address or self.address(), '--indent=-1', '--timeout', str(TIMEOUT)],
            stdin=PIPE, stdout=PIPE, stderr=STDOUT, expect=expect,
            universal_newlines=True)
        out = p.communicate(input)[0]
        try:
            p.teardown()
        except Exception as e:
            raise Exception("%s\n%s" % (e, out))
        return out

    def test_both_link_routes_active(self):
        cmds = [
            'CREATE --type=linkRoute name=foo prefix=foo direction=in connection=one',
            'CREATE --type=linkRoute name=bar prefix=bar direction=in connection=two',
            'CREATE --type=linkRoute name=baz prefix=baz direction=in containerId=QDR.A'
        ]
        for c in cmds:
            self.run_qdmanage(cmd=c, address=self.routers[1].addresses[0])

        # Now that the qdmanage has run, query the link routes and make sure that their "operStatus" is "active" before
        # running any of the tests.
        long_type = 'org.apache.qpid.dispatch.router.config.linkRoute'
        qd_manager = QdManager(address=self.routers[1].addresses[0])

        for i in range(5):
            all_link_routes_activated = True
            link_routes = qd_manager.query(long_type)
            for link_route in link_routes:
                oper_status = link_route['operStatus']
                if oper_status != "active":
                    all_link_routes_activated = False
                    break
            if not all_link_routes_activated:
                # One or more of the link routes have not been activated.
                # Check after one second.
                sleep(1)
            else:
                break

        # All link routes created in this test MUST be activated before
        # we can continue further testing.
        self.assertTrue(all_link_routes_activated)

        first = SendReceive("%s/foo" % self.routers[1].addresses[0], "%s/foo" % self.routers[0].addresses[0])
        first.run()
        self.assertIsNone(first.error)
        second = SendReceive("%s/bar" % self.routers[1].addresses[0], "%s/bar" % self.routers[0].addresses[0])
        second.run()
        self.assertIsNone(second.error)
        third = SendReceive("%s/baz" % self.routers[1].addresses[0], "%s/baz" % self.routers[0].addresses[0])
        third.run()
        self.assertIsNone(third.error)


class SendReceive(MessagingHandler):
    def __init__(self, send_url, recv_url, message=None):
        super(SendReceive, self).__init__()
        self.send_url = send_url
        self.recv_url = recv_url
        self.message = message or Message(body="SendReceiveTest")
        self.sent = False
        self.error = None

    def close(self):
        self.sender.close()
        self.receiver.close()
        self.sender.connection.close()
        self.receiver.connection.close()

    def timeout(self):
        self.error = "Timeout Expired - Check for cores"
        self.close()

    def stop(self):
        self.close()
        self.timer.cancel()

    def on_start(self, event):
        self.timer      = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        event.container.container_id = "SendReceiveTestClient"
        self.sender = event.container.create_sender(self.send_url)
        self.receiver = event.container.create_receiver(self.recv_url)

    def on_sendable(self, event):
        if not self.sent:
            event.sender.send(self.message)
            self.sent = True

    def on_message(self, event):
        if self.message.body != event.message.body:
            self.error = "Incorrect message. Got %s, expected %s" % (event.message.body, self.message.body)

    def on_accepted(self, event):
        self.stop()

    def run(self):
        Container(self).run()


class DispositionSniffer(MessagingHandler):
    """
    Capture the outgoing delivery after the remote has set its terminal
    outcome.  Used by tests that need to examine the delivery state
    """

    def __init__(self, send_url):
        super(DispositionSniffer, self).__init__(auto_accept=False,
                                                 auto_settle=False)
        self.send_url = send_url
        self.sender = None
        self.timer = None
        self.error = None
        self.sent = False
        self.delivery = None

    def close(self):
        if self.timer:
            self.timer.cancel()
        if self.sender:
            self.sender.close()
            self.sender.connection.close()

    def timeout(self):
        self.error = "Timeout Expired - Check for cores"
        self.close()

    def stop(self):
        self.close()

    def on_start(self, event):
        self.timer  = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.sender = event.container.create_sender(self.send_url)

    def on_sendable(self, event):
        if not self.sent:
            event.sender.send(Message(body="HI"))
            self.sent = True

    def on_accepted(self, event):
        self.stop()

    def on_released(self, event):
        self.delivery = event.delivery
        self.close()

    def on_modified(self, event):
        self.delivery = event.delivery
        self.close()

    def on_rejected(self, event):
        self.delivery = event.delivery
        self.close()

    def run(self):
        Container(self).run()


class LinkRoute3Hop(TestCase):
    """
    Sets up a linear 3 hop router network for testing multi-hop link routes.

             +---------+         +---------+         +---------+     +------------------+
             |         | <------ |         | <-----  |         |<----| blocking_senders |
             |  QDR.A  |         |  QDR.B  |         |  QDR.C  |     +------------------+
             |         | ------> |         | ------> |         |     +--------------------+
             +---------+         +---------+         +---------+---->| blocking_receivers |
                  ^                                                  +--------------------+
                  |
                  V
           +-------------+
           | FakeService |
           +-------------+

    """

    @classmethod
    def setUpClass(cls):
        super(LinkRoute3Hop, cls).setUpClass()

        b_port = cls.tester.get_port()
        configs = [
            # QDR.A:
            [('router', {'mode': 'interior', 'id': 'QDR.A'}),
             # for client access
             ('listener', {'role': 'normal',
                           'host': '0.0.0.0',
                           'port': cls.tester.get_port(),
                           'saslMechanisms': 'ANONYMOUS'}),
             # for fake service:
             ('listener', {'role': 'route-container',
                           'host': '0.0.0.0',
                           'port': cls.tester.get_port(),
                           'saslMechanisms': 'ANONYMOUS'}),
             # to connect to the QDR.B
             ('connector', {'role': 'inter-router',
                            'host': '127.0.0.1',
                            'port': b_port,
                            'saslMechanisms': 'ANONYMOUS'}),
             # the routes
             ('linkRoute', {'prefix': 'closest/test-client', 'containerId': 'FakeService', 'direction': 'in'}),
             ('linkRoute', {'prefix': 'closest/test-client', 'containerId': 'FakeService', 'direction': 'out'})
             ],
            # QDR.B:
            [('router', {'mode': 'interior', 'id': 'QDR.B'}),
             # for client connections
             ('listener', {'role': 'normal',
                           'host': '0.0.0.0',
                           'port': cls.tester.get_port(),
                           'saslMechanisms': 'ANONYMOUS'}),
             # for inter-router connections from QDR.A and QDR.C
             ('listener', {'role': 'inter-router',
                           'host': '0.0.0.0',
                           'port': b_port,
                           'saslMechanisms': 'ANONYMOUS'}),
             ('linkRoute', {'prefix': 'closest/test-client', 'direction': 'in'}),
             ('linkRoute', {'prefix': 'closest/test-client', 'direction': 'out'})
             ],
            # QDR.C
            [('router', {'mode': 'interior', 'id': 'QDR.C'}),
             # for client connections
             ('listener', {'role': 'normal',
                           'host': '0.0.0.0',
                           'port': cls.tester.get_port(),
                           'saslMechanisms': 'ANONYMOUS'}),
             # to connect to the QDR.B
             ('connector', {'role': 'inter-router',
                            'host': '127.0.0.1',
                            'port': b_port,
                            'saslMechanisms': 'ANONYMOUS'}),
             ('linkRoute', {'prefix': 'closest/test-client', 'direction': 'in'}),
             ('linkRoute', {'prefix': 'closest/test-client', 'direction': 'out'})
             ]
        ]

        cls.routers = []
        for c in configs:
            config = Qdrouterd.Config(c)
            cls.routers.append(cls.tester.qdrouterd(config=config, wait=False))
        cls.QDR_A = cls.routers[0]
        cls.QDR_B = cls.routers[1]
        cls.QDR_C = cls.routers[2]

        cls.QDR_A.wait_router_connected('QDR.B')
        cls.QDR_B.wait_router_connected('QDR.A')
        cls.QDR_B.wait_router_connected('QDR.C')
        cls.QDR_C.wait_router_connected('QDR.B')
        cls.QDR_C.wait_router_connected('QDR.A')
        cls.QDR_A.wait_router_connected('QDR.C')

    def test_01_parallel_link_routes(self):
        """
        Verify Q2/Q3 recovery in the case of multiple link-routes sharing the
        same session.
        """
        send_clients = 10
        send_batch = 5
        total = send_clients * send_batch

        fake_service = FakeService(self.QDR_A.addresses[1],
                                   container_id="FakeService")
        self.QDR_C.wait_address("closest/test-client",
                                remotes=1)

        env = None
        rx = self.popen(["test-receiver",
                         "-a", self.QDR_C.addresses[0],
                         "-c", str(total),
                         "-s", "closest/test-client",
                         "-d"],
                        env=env,
                        expect=Process.EXIT_OK)

        def _spawn_sender(x):
            return self.popen(["test-sender",
                               "-a", self.QDR_C.addresses[0],
                               "-c", str(send_batch),
                               "-i", "TestSender-%s" % x,
                               "-sx",   # huge message size to trigger Q2/Q3
                               "-t", "closest/test-client",
                               "-d"],
                              env=env,
                              expect=Process.EXIT_OK)

        senders = [_spawn_sender(s) for s in range(send_clients)]

        for tx in senders:
            out_text, out_err = tx.communicate(timeout=TIMEOUT)
            if tx.returncode:
                raise Exception(f"Sender failed: {out_text} {out_err}")

        if rx.wait(timeout=TIMEOUT):
            raise Exception(
                f"Receiver failed to consume all messages in={fake_service.in_count} out={fake_service.out_count}")

        fake_service.join()
        self.assertEqual(total, fake_service.in_count)
        self.assertEqual(total, fake_service.out_count)

        self.QDR_C.wait_address_unsubscribed("closest/test-client")

    def test_02_modified_outcome(self):
        """
        Ensure all elements of a Modified disposition are passed thru the link
        route
        """

        class FakeServiceModified(FakeService):
            def on_message(self, event):
                # set non-default values for delivery state for delivery to
                # remote endpoint
                dlv = event.delivery
                dlv.local.failed = True
                dlv.local.undeliverable = True
                dlv.local.annotations = {symbol("Key"): "Value"}
                dlv.update(Delivery.MODIFIED)
                dlv.settle()

        fake_service = FakeServiceModified(self.QDR_A.addresses[1],
                                           container_id="FakeService",
                                           auto_accept=False,
                                           auto_settle=False)
        self.QDR_C.wait_address("closest/test-client",
                                remotes=1)

        sniffer = DispositionSniffer("%s/closest/test-client" %
                                     self.QDR_C.addresses[0])
        sniffer.run()
        self.assertIsNone(sniffer.error)
        state = sniffer.delivery.remote
        self.assertTrue(state.failed)
        self.assertTrue(state.undeliverable)
        self.assertTrue(state.annotations is not None)
        self.assertTrue(symbol('Key') in state.annotations)
        self.assertEqual('Value', state.annotations[symbol('Key')])

        fake_service.join()
        self.QDR_C.wait_address_unsubscribed("closest/test-client")

    def test_03_rejected_outcome(self):
        """
        Ensure all elements of a Rejected disposition are passed thru the link
        route
        """

        class FakeServiceReject(FakeService):
            def on_message(self, event):
                # set non-default values for delivery state for delivery to
                # remote endpoint
                dlv = event.delivery
                dlv.local.condition = Condition("condition-name",
                                                str("condition-description"),
                                                {symbol("condition"): "info"})
                dlv.update(Delivery.REJECTED)
                dlv.settle()

        fake_service = FakeServiceReject(self.QDR_A.addresses[1],
                                         container_id="FakeService",
                                         auto_accept=False,
                                         auto_settle=False)
        self.QDR_C.wait_address("closest/test-client",
                                remotes=1)

        sniffer = DispositionSniffer("%s/closest/test-client" %
                                     self.QDR_C.addresses[0])
        sniffer.run()
        self.assertIsNone(sniffer.error)
        state = sniffer.delivery.remote
        self.assertTrue(state.condition is not None)
        self.assertEqual("condition-name", state.condition.name)
        self.assertEqual("condition-description", state.condition.description)
        self.assertTrue(state.condition.info is not None)
        self.assertTrue(symbol("condition") in state.condition.info)
        self.assertEqual('info', state.condition.info[symbol("condition")])

        fake_service.join()
        self.QDR_C.wait_address_unsubscribed("closest/test-client")

    def test_04_extension_state(self):
        """
        system_tests_two_routers.TwoRouterExtensionsStateTest() already tests
        sending extended state via a link route.
        """
        pass


if __name__ == '__main__':
    unittest.main(main_module())

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

import unittest
from time import sleep
from subprocess import PIPE, STDOUT

from system_test import TestCase, Qdrouterd, main_module, TIMEOUT, Process

from proton import Message, Endpoint
from proton.handlers import MessagingHandler
from proton.reactor import AtMostOnce, Container, DynamicNodeProperties
from proton.utils import BlockingConnection, LinkDetached

from system_tests_drain_support import DrainMessagesHandler, DrainOneMessageHandler, DrainNoMessagesHandler, DrainNoMoreMessagesHandler

from qpid_dispatch.management.client import Node

class LinkRouteTest(TestCase):
    """
    Tests the linkRoute property of the dispatch router.

    Sets up 3 routers (one of which is acting as a broker(QDR.A)). 2 routers have linkRoute set to 'org.apache.'
    (please see configs in the setUpClass method to get a sense of how the routers and their connections are configured)
    The tests in this class send and receive messages across this network of routers to link routable addresses.
    Uses the Python Blocking API to send/receive messages. The blocking api plays neatly into the synchronous nature
    of system tests.

        QDR.A acting broker
             +---------+         +---------+         +---------+     +-----------------+
             |         | <------ |         | <-----  |         |<----| blocking_sender |
             |  QDR.A  |         |  QDR.B  |         |  QDR.C  |     +-----------------+
             |         | ------> |         | ------> |         |     +-------------------+
             +---------+         +---------+         +---------+---->| blocking_receiver |
                                                                     +-------------------+
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
                ('router', {'mode': 'interior', 'id': 'QDR.%s'%name}),
            ] + connection

            config = Qdrouterd.Config(config)
            cls.routers.append(cls.tester.qdrouterd(name, config, wait=False))

        cls.routers = []
        a_listener_port = cls.tester.get_port()
        b_listener_port = cls.tester.get_port()
        c_listener_port = cls.tester.get_port()
        test_tag_listener_port = cls.tester.get_port()

        router('A',
               [
                   ('listener', {'role': 'normal', 'host': '0.0.0.0', 'port': a_listener_port, 'saslMechanisms': 'ANONYMOUS'}),
               ])
        router('B',
               [
                   ('listener', {'role': 'normal', 'host': '0.0.0.0', 'port': b_listener_port, 'saslMechanisms': 'ANONYMOUS'}),
                   ('listener', {'name': 'test-tag', 'role': 'route-container', 'host': '0.0.0.0', 'port': test_tag_listener_port, 'saslMechanisms': 'ANONYMOUS'}),

                   # This is an on-demand connection made from QDR.B's ephemeral port to a_listener_port
                   ('connector', {'name': 'broker', 'role': 'route-container', 'host': '0.0.0.0', 'port': a_listener_port, 'saslMechanisms': 'ANONYMOUS'}),
                   # Only inter router communication must happen on 'inter-router' connectors. This connector makes
                   # a connection from the router B's ephemeral port to c_listener_port
                   ('connector', {'name': 'routerC', 'role': 'inter-router', 'host': '0.0.0.0', 'port': c_listener_port}),

                   ('linkRoute', {'prefix': 'org.apache', 'connection': 'broker', 'dir': 'in'}),
                   ('linkRoute', {'prefix': 'org.apache', 'connection': 'broker', 'dir': 'out'}),

                   ('linkRoute', {'prefix': 'pulp.task', 'connection': 'test-tag', 'dir': 'in'}),
                   ('linkRoute', {'prefix': 'pulp.task', 'connection': 'test-tag', 'dir': 'out'})
                ]
               )
        router('C',
               [
                   # The client will exclusively use the following listener to connect to QDR.C
                   ('listener', {'host': '0.0.0.0', 'role': 'normal', 'port': cls.tester.get_port(), 'saslMechanisms': 'ANONYMOUS'}),
                   ('listener', {'host': '0.0.0.0', 'role': 'inter-router', 'port': c_listener_port, 'saslMechanisms': 'ANONYMOUS'}),
                   # The dot(.) at the end is ignored by the address hashing scheme.

                   ('linkRoute', {'prefix': 'org.apache.', 'dir': 'in'}),
                   ('linkRoute', {'prefix': 'org.apache.', 'dir': 'out'}),

                   ('linkRoute', {'prefix': 'pulp.task', 'dir': 'in'}),
                   ('linkRoute', {'prefix': 'pulp.task', 'dir': 'out'})
                ]
               )

        # Wait for the routers to locate each other
        cls.routers[1].wait_router_connected('QDR.C')
        cls.routers[2].wait_router_connected('QDR.B')

        # This is not a classic router network in the sense that one router is acting as a broker. We allow a little
        # bit more time for the routers to stabilize.
        sleep(2)

    def run_qdstat_linkRoute(self, address):
        p = self.popen(
            ['qdstat', '--bus', str(address), '--timeout', str(TIMEOUT) ] + ['--linkroute'],
            name='qdstat-'+self.id(), stdout=PIPE, expect=None)

        out = p.communicate()[0]
        assert p.returncode == 0, "qdstat exit status %s, output:\n%s" % (p.returncode, out)
        return out

    def run_qdmanage(self, cmd, input=None, expect=Process.EXIT_OK, address=None):
        p = self.popen(
            ['qdmanage'] + cmd.split(' ') + ['--bus', address or self.address(), '--indent=-1', '--timeout', str(TIMEOUT)],
            stdin=PIPE, stdout=PIPE, stderr=STDOUT, expect=expect)
        out = p.communicate(input)[0]
        try:
            p.teardown()
        except Exception, e:
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
        self.assertTrue('"dir": "in"' in out)
        self.assertTrue('"dir": "out"' in out)
        self.assertTrue('"connection": "broker"' in out)

        # Use the long type and make sure that qdmanage does not mess up the long type
        cmd = 'QUERY --type=org.apache.qpid.dispatch.router.config.linkRoute'
        out = self.run_qdmanage(cmd=cmd, address=self.routers[1].addresses[0])

        # Make sure there is a dir of in and out.
        self.assertTrue('"dir": "in"' in out)
        self.assertTrue('"dir": "out"' in out)
        self.assertTrue('"connection": "broker"' in out)

        identity = out[out.find("identity") + 12: out.find("identity") + 13]
        cmd = 'READ --type=linkRoute --identity=' + identity
        out = self.run_qdmanage(cmd=cmd, address=self.routers[1].addresses[0])
        self.assertTrue(identity in out)

        exception_occurred = False
        try:
            # This identity should not be found
            cmd = 'READ --type=linkRoute --identity=9999'
            out = self.run_qdmanage(cmd=cmd, address=self.routers[1].addresses[0])
        except Exception, e:
            exception_occurred = True
            self.assertTrue("NotFoundStatus: Not Found" in e.message)

        self.assertTrue(exception_occurred)

        exception_occurred = False
        try:
            # There is no identity specified, this is a bad request
            cmd = 'READ --type=linkRoute'
            out = self.run_qdmanage(cmd=cmd, address=self.routers[1].addresses[0])
        except Exception, e:
            exception_occurred = True
            self.assertTrue("BadRequestStatus: No name or identity provided" in e.message)

        self.assertTrue(exception_occurred)

        cmd = 'CREATE --type=autoLink addr=127.0.0.1 dir=in connection=routerC'
        out = self.run_qdmanage(cmd=cmd, address=self.routers[1].addresses[0])

        identity = out[out.find("identity") + 12: out.find("identity") + 14]
        cmd = 'READ --type=autoLink --identity=' + identity
        out = self.run_qdmanage(cmd=cmd, address=self.routers[1].addresses[0])
        self.assertTrue(identity in out)




    def test_bbb_qdstat_link_routes_routerB(self):
        """
        Runs qdstat on router B to make sure that router B has two link routes, one 'in' and one 'out'

        """
        out = self.run_qdstat_linkRoute(self.routers[1].addresses[0])
        out_list = out.split()
        self.assertEqual(out_list.count('in'), 2)
        self.assertEqual(out_list.count('out'), 2)

    def test_ccc_qdstat_link_routes_routerC(self):
        """
        Runs qdstat on router C to make sure that router C has two link routes, one 'in' and one 'out'

        """
        out = self.run_qdstat_linkRoute(self.routers[2].addresses[0])
        out_list = out.split()

        self.assertEqual(out_list.count('in'), 2)
        self.assertEqual(out_list.count('out'), 2)

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
        self.assertEqual(u'QDR.A', local_node.query(type='org.apache.qpid.dispatch.router',
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
        self.assertEquals(4, len(local_node.query(type='org.apache.qpid.dispatch.router.link').results))

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
        blocking_receiver = blocking_connection.create_receiver(address="org.apache.dev")

        apply_options = AtMostOnce()

        # Sender to  to org.apache.dev
        blocking_sender = blocking_connection.create_sender(address="org.apache.dev", options=apply_options)
        msg = Message(body=hello_world_2)
        # Send a message
        blocking_sender.send(msg)

        received_message = blocking_receiver.receive()

        self.assertEqual(hello_world_2, received_message.body)

        local_node = Node.connect(self.routers[0].addresses[0], timeout=TIMEOUT)

        # Make sure that the router node acting as the broker (QDR.A) had one message routed through it. This confirms
        # that the message was link routed
        self.assertEqual(1, local_node.read(type='org.apache.qpid.dispatch.router.address',
                                            name='M0org.apache.dev').deliveriesEgress)

        self.assertEqual(1, local_node.read(type='org.apache.qpid.dispatch.router.address',
                                            name='M0org.apache.dev').deliveriesIngress)

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

    def test_full_link_route_match_1(self):
        """
        This test is pretty much the same as the previous test (test_full_link_route_match) but the connection is
        made to router QDR.B instead of QDR.C and we expect the message to be link routed successfully.
        """
        hello_world_4 = "Hello World_4!"
        addr = self.routers[1].addresses[0]

        blocking_connection = BlockingConnection(addr)

        # Receive on org.apache
        blocking_receiver = blocking_connection.create_receiver(address="org.apache")

        apply_options = AtMostOnce()

        # Sender to  to org.apache
        blocking_sender = blocking_connection.create_sender(address="org.apache", options=apply_options)

        msg = Message(body=hello_world_4)
        # Send a message
        blocking_sender.send(msg)

        received_message = blocking_receiver.receive()

        self.assertEqual(hello_world_4, received_message.body)

        local_node = Node.connect(self.routers[0].addresses[0], timeout=TIMEOUT)

        # Make sure that the router node acting as the broker (QDR.A) had one message routed through it. This confirms
        # that the message was link routed
        self.assertEqual(1, local_node.read(type='org.apache.qpid.dispatch.router.address',
                                            name='M0org.apache').deliveriesEgress)

        self.assertEqual(1, local_node.read(type='org.apache.qpid.dispatch.router.address',
                                            name='M0org.apache').deliveriesIngress)

        blocking_connection.close()

    def test_zzz_qdmanage_delete_link_route(self):
        """
        We are deleting the link route using qdmanage short name. This should be the last test to run
        """

        # First delete linkRoutes on QDR.B
        local_node = Node.connect(self.routers[1].addresses[0], timeout=TIMEOUT)
        result_list = local_node.query(type='org.apache.qpid.dispatch.router.config.linkRoute').results

        identity_1 = result_list[0][1]
        identity_2 = result_list[1][1]
        identity_3 = result_list[2][1]
        identity_4 = result_list[3][1]

        cmd = 'DELETE --type=linkRoute --identity=' + identity_1
        self.run_qdmanage(cmd=cmd, address=self.routers[1].addresses[0])

        cmd = 'DELETE --type=linkRoute --identity=' + identity_2
        self.run_qdmanage(cmd=cmd, address=self.routers[1].addresses[0])

        cmd = 'DELETE --type=linkRoute --identity=' + identity_3
        self.run_qdmanage(cmd=cmd, address=self.routers[1].addresses[0])

        cmd = 'DELETE --type=linkRoute --identity=' + identity_4
        self.run_qdmanage(cmd=cmd, address=self.routers[1].addresses[0])

        cmd = 'QUERY --type=linkRoute'
        out = self.run_qdmanage(cmd=cmd, address=self.routers[1].addresses[0])
        self.assertEquals(out.rstrip(), '[]')

        # linkRoutes now gone on QDR.B but remember that it still exist on QDR.C
        # We will now try to create a receiver on address org.apache.dev on QDR.C.
        # Since the linkRoute on QDR.B is gone, QDR.C
        # will not allow a receiver to be created since there is no route to destination.

        # Connects to listener #2 on QDR.C
        addr = self.routers[2].addresses[0]

        # Now delete linkRoutes on QDR.C to eradicate linkRoutes completely
        local_node = Node.connect(addr, timeout=TIMEOUT)
        result_list = local_node.query(type='org.apache.qpid.dispatch.router.config.linkRoute').results

        identity_1 = result_list[0][1]
        identity_2 = result_list[1][1]
        identity_3 = result_list[2][1]
        identity_4 = result_list[3][1]
        identity_4 = result_list[3][1]

        cmd = 'DELETE --type=linkRoute --identity=' + identity_1
        self.run_qdmanage(cmd=cmd, address=addr)

        cmd = 'DELETE --type=linkRoute --identity=' + identity_2
        self.run_qdmanage(cmd=cmd, address=addr)

        cmd = 'DELETE --type=linkRoute --identity=' + identity_3
        self.run_qdmanage(cmd=cmd, address=addr)

        cmd = 'DELETE --type=linkRoute --identity=' + identity_4
        self.run_qdmanage(cmd=cmd, address=addr)

        cmd = 'QUERY --type=linkRoute'
        out = self.run_qdmanage(cmd=cmd, address=addr)
        self.assertEquals(out.rstrip(), '[]')

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
        self.assertEqual(None, test.error)

    def test_close_with_unsettled(self):
        test = CloseWithUnsettledTest(self.routers[1].addresses[0], self.routers[1].addresses[1])
        test.run()
        self.assertEqual(None, test.error)

    def test_www_drain_support_all_messages(self):
        drain_support = DrainMessagesHandler(self.routers[2].addresses[0])
        drain_support.run()
        self.assertEqual(None, drain_support.error)

    def test_www_drain_support_one_message(self):
        drain_support = DrainOneMessageHandler(self.routers[2].addresses[0])
        drain_support.run()
        self.assertEqual(None, drain_support.error)

    def test_www_drain_support_no_messages(self):
        drain_support = DrainNoMessagesHandler(self.routers[2].addresses[0])
        drain_support.run()
        self.assertEqual(None, drain_support.error)

    def test_www_drain_support_no_more_messages(self):
        drain_support = DrainNoMoreMessagesHandler(self.routers[2].addresses[0])
        drain_support.run()
        self.assertEqual(None, drain_support.error)

    def test_dynamic_source(self):
        test = DynamicSourceTest(self.routers[1].addresses[0], self.routers[1].addresses[1])
        test.run()
        self.assertEqual(None, test.error)


class Timeout(object):
    def __init__(self, parent):
        self.parent = parent

    def on_timer_task(self, event):
        self.parent.timeout()


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
        self.delivery_tag = '92319'
        self.error = None

    def timeout(self):
        self.error = "Timeout expired: sent=%d rcvd=%d" % (self.sent, self.rcvd)
        if self.receiver_connection:
            self.receiver_connection.close()
        if self.sender_connection:
            self.sender_connection.close()

    def on_start(self, event):
        self.timer               = event.reactor.schedule(5, Timeout(self))
        self.receiver_connection = event.container.connect(self.listening_address)

    def on_connection_remote_open(self, event):
        if event.connection == self.receiver_connection:
            continue_loop = True
            # Dont open the sender connection unless we can make sure that there is a remote receiver ready to
            # accept the message.
            # If there is no remote receiver, the router will throw a 'No route to destination' error when
            # creating sender connection.
            # The following loops introduces a wait before creating the sender connection. It gives time to the
            # router so that the address Dpulp.task can show up on the remoteCount
            i = 0
            while continue_loop:
                if i > 100: # If we have run the read command for more than hundred times and we still do not have
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
    ## This test sends a message across an attach-routed link.  While the message
    ## is unsettled, the client link is closed.  The test is ensuring that the
    ## router does not crash during the closing of the links.
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
        self.timer      = event.reactor.schedule(5, Timeout(self))
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
    ## This test verifies that a dynamic source can be propagated via link-route to
    ## a route-container.
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
        self.timer      = event.reactor.schedule(5, Timeout(self))
        self.conn_route = event.container.connect(self.route_addr)

    def on_connection_opened(self, event):
        if event.connection == self.conn_route:
            self.conn_normal = event.container.connect(self.normal_addr)
        elif event.connection == self.conn_normal:
            self.receiver = event.container.create_receiver(self.conn_normal, None, dynamic=True,options=DynamicNodeProperties({"x-opt-qd.address":u"pulp.task.abc"}))

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

if __name__ == '__main__':
    unittest.main(main_module())


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
import json
from proton import Message
from system_test import TestCase, Qdrouterd, main_module, TIMEOUT, Process
from proton.handlers import MessagingHandler
from proton.reactor import Container
from subprocess import PIPE, STDOUT
from qpid_dispatch.management.client import Node

CONNECTION_PROPERTIES = {u'connection': u'properties', u'int_property': 6451}


class AutolinkTest(TestCase):
    """System tests involving a single router"""
    @classmethod
    def setUpClass(cls):
        """Start a router and a messenger"""
        super(AutolinkTest, cls).setUpClass()
        name = "test-router"
        config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'QDR'}),

            #
            # Create a general-purpose listener for sending and receiving deliveries
            #
            ('listener', {'port': cls.tester.get_port()}),

            #
            # Create a route-container listener for the autolinks
            #
            ('listener', {'port': cls.tester.get_port(), 'role': 'route-container'}),

            #
            # Create a route-container listener and give it a name myListener.
            # Later on we will create an autoLink which has a connection property of myListener.
            #
            ('listener', {'role': 'route-container', 'name': 'myListener', 'port': cls.tester.get_port()}),

            #
            # Set up the prefix 'node' as a prefix for waypoint addresses
            #
            ('address',  {'prefix': 'node', 'waypoint': 'yes'}),

            #
            # Create a pair of default auto-links for 'node.1'
            #
            ('autoLink', {'addr': 'node.1', 'containerId': 'container.1', 'direction': 'in'}),
            ('autoLink', {'addr': 'node.1', 'containerId': 'container.1', 'direction': 'out'}),

            #
            # Create a pair of auto-links on non-default phases for container-to-container transfers
            #
            ('autoLink', {'addr': 'xfer.2', 'containerId': 'container.2', 'direction': 'in',  'phase': '4'}),
            ('autoLink', {'addr': 'xfer.2', 'containerId': 'container.3', 'direction': 'out', 'phase': '4'}),

            #
            # Create a pair of auto-links with a different external address
            # Leave the direction as dir to test backward compatibility.
            #
            ('autoLink', {'addr': 'node.2', 'externalAddr': 'ext.2', 'containerId': 'container.4', 'dir': 'in'}),
            ('autoLink', {'addr': 'node.2', 'externalAddr': 'ext.2', 'containerId': 'container.4', 'dir': 'out'}),

            #
            # Note here that the connection is set to a previously declared 'myListener'
            #
            ('autoLink', {'addr': 'myListener.1', 'connection': 'myListener', 'direction': 'in'}),
            ('autoLink', {'addr': 'myListener.1', 'connection': 'myListener', 'direction': 'out'}),

        ])

        cls.router = cls.tester.qdrouterd(name, config)
        cls.router.wait_ready()
        cls.normal_address = cls.router.addresses[0]
        cls.route_address = cls.router.addresses[1]
        cls.ls_route_address = cls.router.addresses[2]

    def run_qdstat_general(self):
        cmd = ['qdstat', '--bus', str(AutolinkTest.normal_address), '--timeout', str(TIMEOUT)] + ['-g']
        p = self.popen(
            cmd,
            name='qdstat-'+self.id(), stdout=PIPE, expect=None,
            universal_newlines=True)

        out = p.communicate()[0]
        assert p.returncode == 0, "qdstat exit status %s, output:\n%s" % (p.returncode, out)
        return out

    def run_qdmanage(self, cmd, input=None, expect=Process.EXIT_OK):
        p = self.popen(
            ['qdmanage'] + cmd.split(' ') + ['--bus', AutolinkTest.normal_address, '--indent=-1', '--timeout', str(TIMEOUT)],
            stdin=PIPE, stdout=PIPE, stderr=STDOUT, expect=expect,
            universal_newlines=True)
        out = p.communicate(input)[0]
        try:
            p.teardown()
        except Exception as e:
            raise Exception("%s\n%s" % (e, out))
        return out

    def test_01_autolink_attach(self):
        """
        Create the route-container connection and verify that the appropriate links are attached.
        Disconnect, reconnect, and verify that the links are re-attached.
        """
        test = AutolinkAttachTest('container.1', self.route_address, 'node.1')
        test.run()
        self.assertEqual(None, test.error)

    def test_02_autolink_credit(self):
        """
        Create a normal connection and a sender to the autolink address.  Then create the route-container
        connection and ensure that the on_sendable did not arrive until after the autolinks were created.
        """
        test = AutolinkCreditTest(self.normal_address, self.route_address)
        test.run()
        self.assertEqual(None, test.error)
        self.assertTrue(test.autolink_count_ok)

    def test_03_autolink_sender(self):
        """
        Create a route-container connection and a normal sender.  Ensure that messages sent on the sender
        link are received by the route container and that settlement propagates back to the sender.
        """
        test = AutolinkSenderTest('container.1', self.normal_address, self.route_address, 'node.1', 'node.1')
        test.run()
        self.assertEqual(None, test.error)

        long_type = 'org.apache.qpid.dispatch.router'
        query_command = 'QUERY --type=' + long_type
        output = json.loads(self.run_qdmanage(query_command))
        self.assertEqual(output[0]['deliveriesEgressRouteContainer'], 275)
        self.assertEqual(output[0]['deliveriesIngressRouteContainer'], 0)
        self.assertEqual(output[0]['deliveriesTransit'], 0)

        self.assertEqual(output[0]['deliveriesIngress'], 277)
        self.assertEqual(output[0]['deliveriesEgress'], 276)

    def test_04_autolink_receiver(self):
        """
        Create a route-container connection and a normal receiver.  Ensure that messages sent from the
        route-container are received by the receiver and that settlement propagates back to the sender.
        """
        test = AutolinkReceiverTest('container.1', self.normal_address, self.route_address, 'node.1', 'node.1')
        test.run()
        self.assertEqual(None, test.error)

        long_type = 'org.apache.qpid.dispatch.router'
        query_command = 'QUERY --type=' + long_type
        output = json.loads(self.run_qdmanage(query_command))
        self.assertEqual(output[0]['deliveriesEgressRouteContainer'], 275)
        self.assertEqual(output[0]['deliveriesIngressRouteContainer'], 275)
        self.assertEqual(output[0]['deliveriesTransit'], 0)

        self.assertEqual(output[0]['deliveriesIngress'], 553)
        self.assertEqual(output[0]['deliveriesEgress'], 552)

    def test_05_inter_container_transfer(self):
        """
        Create a route-container connection and a normal receiver.  Ensure that messages sent from the
        route-container are received by the receiver and that settlement propagates back to the sender.
        """
        test = InterContainerTransferTest(self.normal_address, self.route_address)
        test.run()
        self.assertEqual(None, test.error)

    def test_06_manage_autolinks(self):
        """
        Create a route-container connection and a normal receiver.  Use the management API to create
        autolinks to the route container.  Verify that the links are created.  Delete the autolinks.
        Verify that the links are closed without error.
        """
        test = ManageAutolinksTest(self.normal_address, self.route_address)
        test.run()
        self.assertEqual(None, test.error)

    def test_07_autolink_attach_with_ext_addr(self):
        """
        Create the route-container connection and verify that the appropriate links are attached.
        Disconnect, reconnect, and verify that the links are re-attached.  Verify that the node addresses
        in the links are the configured external address.
        """
        test = AutolinkAttachTest('container.4', self.route_address, 'ext.2')
        test.run()
        self.assertEqual(None, test.error)

    def test_08_autolink_sender_with_ext_addr(self):
        """
        Create a route-container connection and a normal sender.  Ensure that messages sent on the sender
        link are received by the route container and that settlement propagates back to the sender.
        """
        test = AutolinkSenderTest('container.4', self.normal_address, self.route_address, 'node.2', 'ext.2')
        test.run()
        self.assertEqual(None, test.error)

    def test_09_autolink_receiver_with_ext_addr(self):
        """
        Create a route-container connection and a normal receiver.  Ensure that messages sent from the
        route-container are received by the receiver and that settlement propagates back to the sender.
        """
        test = AutolinkReceiverTest('container.4', self.normal_address, self.route_address, 'node.2', 'ext.2')
        test.run()
        self.assertEqual(None, test.error)

    def test_10_autolink_attach_to_listener(self):
        """
        Create two route-container receivers with the same connection name (myListener) and verify that the appropriate
        links over both connections are attached. Disconnect, reconnect, and verify that the links are re-attached.
        """
        test = AutolinkAttachTestWithListenerName(self.ls_route_address, 'myListener.1')
        test.run()
        self.assertEqual(None, test.error)

    def test_11_autolink_multiple_receivers_on_listener(self):
        """
        Create two receivers connecting into a route container listener. Create one sender to a normal listener
        Have the sender send two messages to the address on which the route container listeners are listening and
        make sure that each receiver gets one message.
        """
        test = AutolinkMultipleReceiverUsingMyListenerTest(self.normal_address, self.ls_route_address, 'myListener.1')
        test.run()
        self.assertEqual(None, test.error)


class Timeout(object):
    def __init__(self, parent):
        self.parent = parent

    def on_timer_task(self, event):
        self.parent.timeout()


class AutolinkAttachTestWithListenerName(MessagingHandler):
    def __init__(self, address, node_addr):
        super(AutolinkAttachTestWithListenerName, self).__init__(prefetch=0)
        self.address = address
        self.node_addr = node_addr
        self.error = None
        self.sender = None
        self.receiver = None
        self.timer = None
        self.n_rx_attach = 0
        self.n_tx_attach = 0
        self.conn = None
        self.conn1 = None
        self.conns_reopened = False

    def timeout(self):
        self.error = "Timeout Expired: n_rx_attach=%d n_tx_attach=%d" % (self.n_rx_attach, self.n_tx_attach)
        self.conn.close()
        self.conn1.close()

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))

        # We create teo connections to the same listener here and we expect attaches to be sent on both connections
        self.conn = event.container.connect(self.address)
        self.conn1 = event.container.connect(self.address)

    def on_connection_closed(self, event):
        if self.n_tx_attach == 2 and not self.conns_reopened:
            self.conns_reopened = True
            # Re-connect on connection closure
            self.conn = event.container.connect(self.address)
            self.conn1 = event.container.connect(self.address)

    def on_link_opened(self, event):
        if event.sender:
            self.n_tx_attach += 1
            if event.sender.remote_source.address != self.node_addr:
                self.error = "Expected sender address '%s', got '%s'" % (self.node_addr, event.sender.remote_source.address)
                self.timer.cancel()
                self.conn.close()
        elif event.receiver:
            self.n_rx_attach += 1
            if event.receiver.remote_target.address != self.node_addr:
                self.error = "Expected receiver address '%s', got '%s'" % (self.node_addr, event.receiver.remote_target.address)
                self.timer.cancel()
                self.conn.close()

        if self.n_tx_attach == 2 and self.n_rx_attach == 2:
            self.conn.close()
            self.conn1.close()

        if self.n_tx_attach == 4 and self.n_rx_attach == 4:
            self.timer.cancel()
            self.conn.close()
            self.conn1.close()

    def run(self):
        Container(self).run()


class AutolinkAttachTest(MessagingHandler):
    def __init__(self, cid, address, node_addr):
        super(AutolinkAttachTest, self).__init__(prefetch=0)
        self.cid       = cid
        self.address   = address
        self.node_addr = node_addr
        self.error     = None
        self.sender    = None
        self.receiver  = None

        self.n_rx_attach = 0
        self.n_tx_attach = 0

    def timeout(self):
        self.error = "Timeout Expired: n_rx_attach=%d n_tx_attach=%d" % (self.n_rx_attach, self.n_tx_attach)
        self.conn.close()

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.conn  = event.container.connect(self.address)

    def on_connection_closed(self, event):
        if self.n_tx_attach == 1:
            self.conn  = event.container.connect(self.address)

    def on_link_opened(self, event):
        if event.sender:
            self.n_tx_attach += 1
            if event.sender.remote_source.address != self.node_addr:
                self.error = "Expected sender address '%s', got '%s'" % (self.node_addr, event.sender.remote_source.address)
                self.timer.cancel()
                self.conn.close()
        elif event.receiver:
            self.n_rx_attach += 1
            if event.receiver.remote_target.address != self.node_addr:
                self.error = "Expected receiver address '%s', got '%s'" % (self.node_addr, event.receiver.remote_target.address)
                self.timer.cancel()
                self.conn.close()
        if self.n_tx_attach == 1 and self.n_rx_attach == 1:
            self.conn.close()
        if self.n_tx_attach == 2 and self.n_rx_attach == 2:
            self.conn.close()
            self.timer.cancel()

    def run(self):
        container = Container(self)
        container.container_id = self.cid
        container.run()


class AutolinkCreditTest(MessagingHandler):
    def __init__(self, normal_address, route_address):
        super(AutolinkCreditTest, self).__init__(prefetch=0)
        self.normal_address = normal_address
        self.route_address  = route_address
        self.dest           = 'node.1'
        self.normal_conn    = None
        self.route_conn     = None
        self.error          = None
        self.last_action    = "None"
        self.autolink_count_ok = False

    def timeout(self):
        self.error = "Timeout Expired: last_action=%s" % self.last_action
        if self.normal_conn:
            self.normal_conn.close()
        if self.route_conn:
            self.route_conn.close()

    def on_start(self, event):
        self.timer       = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.normal_conn = event.container.connect(self.normal_address)
        self.sender      = event.container.create_sender(self.normal_conn, self.dest)
        self.last_action = "Attached normal sender"

        local_node = Node.connect(self.normal_address, timeout=TIMEOUT)
        res = local_node.query(type='org.apache.qpid.dispatch.router')
        results = res.results[0]
        attribute_names = res.attribute_names
        if 8 == results[attribute_names.index('autoLinkCount')]:
            self.autolink_count_ok = True

    def on_link_opening(self, event):
        if event.sender:
            event.sender.source.address = event.sender.remote_source.address
        if event.receiver:
            event.receiver.target.address = event.receiver.remote_target.address

    def on_link_opened(self, event):
        if event.sender == self.sender:
            self.route_conn = event.container.connect(self.route_address)
            self.last_action = "Opened route connection"

    def on_sendable(self, event):
        if event.sender == self.sender:
            if self.last_action != "Opened route connection":
                self.error = "Events out of sequence:  last_action=%s" % self.last_action
            self.timer.cancel()
            self.route_conn.close()
            self.normal_conn.close()

    def run(self):
        container = Container(self)
        container.container_id = 'container.1'
        container.run()


class AutolinkSenderTest(MessagingHandler):
    def __init__(self, cid, normal_address, route_address, addr, ext_addr):
        super(AutolinkSenderTest, self).__init__()
        self.cid            = cid
        self.normal_address = normal_address
        self.route_address  = route_address
        self.dest           = addr
        self.ext_addr       = ext_addr
        self.count          = 275
        self.normal_conn    = None
        self.route_conn     = None
        self.error          = None
        self.last_action    = "None"
        self.n_sent         = 0
        self.n_received     = 0
        self.n_settled      = 0

    def timeout(self):
        self.error = "Timeout Expired: last_action=%s n_sent=%d n_received=%d n_settled=%d" % \
                     (self.last_action, self.n_sent, self.n_received, self.n_settled)
        if self.normal_conn:
            self.normal_conn.close()
        if self.route_conn:
            self.route_conn.close()

    def on_start(self, event):
        self.timer       = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.route_conn  = event.container.connect(self.route_address)
        self.last_action = "Connected route container"

    def on_link_opening(self, event):
        if event.sender:
            event.sender.source.address = event.sender.remote_source.address
        if event.receiver:
            event.receiver.target.address = event.receiver.remote_target.address

    def on_link_opened(self, event):
        if event.receiver and not self.normal_conn:
            self.normal_conn = event.container.connect(self.normal_address)
            self.sender      = event.container.create_sender(self.normal_conn, self.dest)
            self.last_action = "Attached normal sender"

    def on_sendable(self, event):
        if event.sender == self.sender:
            while self.n_sent < self.count and event.sender.credit > 0:
                msg = Message(body="AutoLinkTest")
                self.sender.send(msg)
                self.n_sent += 1

    def on_message(self, event):
        self.n_received += 1
        self.accept(event.delivery)

    def on_settled(self, event):
        self.n_settled += 1
        if self.n_settled == self.count:
            self.timer.cancel()
            self.normal_conn.close()
            self.route_conn.close()

    def run(self):
        container = Container(self)
        container.container_id = self.cid
        container.run()


class AutolinkReceiverTest(MessagingHandler):
    def __init__(self, cid, normal_address, route_address, addr, ext_addr):
        super(AutolinkReceiverTest, self).__init__()
        self.cid            = cid
        self.normal_address = normal_address
        self.route_address  = route_address
        self.dest           = addr
        self.ext_addr       = ext_addr
        self.count          = 275
        self.normal_conn    = None
        self.route_conn     = None
        self.error          = None
        self.last_action    = "None"
        self.n_sent         = 0
        self.n_received     = 0
        self.n_settled      = 0

    def timeout(self):
        self.error = "Timeout Expired: last_action=%s n_sent=%d n_received=%d n_settled=%d" % \
                     (self.last_action, self.n_sent, self.n_received, self.n_settled)
        if self.normal_conn:
            self.normal_conn.close()
        if self.route_conn:
            self.route_conn.close()

    def on_start(self, event):
        self.timer       = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.route_conn  = event.container.connect(self.route_address)
        self.last_action = "Connected route container"

    def on_link_opening(self, event):
        if event.sender:
            event.sender.source.address = event.sender.remote_source.address
            self.sender = event.sender
        if event.receiver:
            event.receiver.target.address = event.receiver.remote_target.address

    def on_link_opened(self, event):
        if event.sender and not self.normal_conn:
            self.normal_conn = event.container.connect(self.normal_address)
            self.receiver    = event.container.create_receiver(self.normal_conn, self.dest)
            self.last_action = "Attached normal receiver"

    def on_sendable(self, event):
        if event.sender == self.sender:
            while self.n_sent < self.count and event.sender.credit > 0:
                msg = Message(body="AutoLinkTest")
                self.sender.send(msg)
                self.n_sent += 1

    def on_message(self, event):
        self.n_received += 1
        self.accept(event.delivery)

    def on_settled(self, event):
        self.n_settled += 1
        if self.n_settled == self.count:
            self.timer.cancel()
            self.normal_conn.close()
            self.route_conn.close()

    def run(self):
        container = Container(self)
        container.container_id = self.cid
        container.run()


class AutolinkMultipleReceiverUsingMyListenerTest(MessagingHandler):
    def __init__(self, normal_address, route_address, addr):
        super(AutolinkMultipleReceiverUsingMyListenerTest, self).__init__()
        self.normal_address = normal_address
        self.route_address = route_address
        self.dest = addr
        self.count = 10
        self.normal_conn = None
        self.route_conn1 = None
        self.route_conn2 = None
        self.error = None
        self.last_action = "None"
        self.n_sent = 0
        self.rcv1_received = 0
        self.rcv2_received = 0
        self.n_settled = 0
        self.timer = None
        self.route_conn_rcv1 = None
        self.route_conn_rcv2 = None
        self.n_rx_attach1 = 0
        self.n_rx_attach2 = 0
        self.sender = None
        self.ready_to_send = False

    def timeout(self):
        self.error = "Timeout Expired: messages received by receiver 1=%d messages received by " \
                     "receiver 2=%d" % (self.rcv1_received, self.rcv2_received)
        self.normal_conn.close()
        self.route_conn1.close()
        self.route_conn2.close()

    def on_start(self, event):
        if self.count % 2 != 0:
            self.error = "Count must be a multiple of 2"
            return
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.normal_conn = event.container.connect(self.normal_address)
        self.route_conn1 = event.container.connect(self.route_address)
        self.route_conn2 = event.container.connect(self.route_address)
        self.route_conn_rcv1 = event.container.create_receiver(self.route_conn1, self.dest, name="R1")
        self.route_conn_rcv2 = event.container.create_receiver(self.route_conn2, self.dest, name="R2")

    def on_link_opened(self, event):
        if event.receiver == self.route_conn_rcv1:
            self.n_rx_attach1 += 1
        if event.receiver == self.route_conn_rcv2:
            self.n_rx_attach2 += 1

        if event.sender and event.sender == self.sender:
            self.ready_to_send = True

        if self.n_rx_attach1 == 1 and self.n_rx_attach2 == 1:
            # Both attaches have been received, create a sender
            if not self.sender:
                self.sender = event.container.create_sender(self.normal_conn, self.dest)

    def on_sendable(self, event):
        if self.ready_to_send:
            if self.n_sent < self.count:
                msg = Message(body="AutolinkMultipleReceiverUsingMyListenerTest")
                self.sender.send(msg)
                self.n_sent += 1

    def on_message(self, event):
        if event.receiver == self.route_conn_rcv1:
            self.rcv1_received += 1
        if event.receiver == self.route_conn_rcv2:
            self.rcv2_received += 1

        if (self.rcv1_received + self.rcv2_received == self.count) and \
                self.rcv2_received > 0 and self.rcv1_received > 0:
            self.timer.cancel()
            self.normal_conn.close()
            self.route_conn1.close()
            self.route_conn2.close()

    def run(self):
        container = Container(self)
        container.run()


class InterContainerTransferTest(MessagingHandler):
    def __init__(self, normal_address, route_address):
        super(InterContainerTransferTest, self).__init__()
        self.normal_address = normal_address
        self.route_address  = route_address
        self.count          = 275
        self.conn_1         = None
        self.conn_2         = None
        self.error          = None
        self.n_sent         = 0
        self.n_received     = 0
        self.n_settled      = 0

    def timeout(self):
        self.error = "Timeout Expired:  n_sent=%d n_received=%d n_settled=%d" % \
                     (self.n_sent, self.n_received, self.n_settled)
        self.conn_1.close()
        self.conn_2.close()

    def on_start(self, event):
        self.timer  = event.reactor.schedule(TIMEOUT, Timeout(self))
        event.container.container_id = 'container.2'
        self.conn_1 = event.container.connect(self.route_address)
        event.container.container_id = 'container.3'
        self.conn_2 = event.container.connect(self.route_address)

    def on_link_opening(self, event):
        if event.sender:
            event.sender.source.address = event.sender.remote_source.address
            self.sender = event.sender
        if event.receiver:
            event.receiver.target.address = event.receiver.remote_target.address

    def on_sendable(self, event):
        if event.sender == self.sender:
            while self.n_sent < self.count and event.sender.credit > 0:
                msg = Message(body="AutoLinkTest")
                self.sender.send(msg)
                self.n_sent += 1

    def on_message(self, event):
        self.n_received += 1
        self.accept(event.delivery)

    def on_settled(self, event):
        self.n_settled += 1
        if self.n_settled == self.count:
            self.timer.cancel()
            self.conn_1.close()
            self.conn_2.close()

    def run(self):
        container = Container(self)
        container.run()


class ManageAutolinksTest(MessagingHandler):
    def __init__(self, normal_address, route_address):
        super(ManageAutolinksTest, self).__init__()
        self.normal_address = normal_address
        self.route_address  = route_address
        self.count          = 5
        self.normal_conn    = None
        self.route_conn     = None
        self.error          = None
        self.n_created      = 0
        self.n_attached     = 0
        self.n_deleted      = 0
        self.n_detached     = 0

    def timeout(self):
        self.error = "Timeout Expired: n_created=%d n_attached=%d n_deleted=%d n_detached=%d" % \
                     (self.n_created, self.n_attached, self.n_deleted, self.n_detached)
        self.normal_conn.close()
        self.route_conn.close()

    def on_start(self, event):
        self.timer  = event.reactor.schedule(TIMEOUT, Timeout(self))
        event.container.container_id = 'container.new'
        self.route_conn  = event.container.connect(self.route_address)
        self.normal_conn = event.container.connect(self.normal_address)
        self.reply       = event.container.create_receiver(self.normal_conn, dynamic=True)
        self.agent       = event.container.create_sender(self.normal_conn, "$management");

    def on_link_opening(self, event):
        if event.sender:
            event.sender.source.address = event.sender.remote_source.address
        if event.receiver:
            event.receiver.target.address = event.receiver.remote_target.address

    def on_link_opened(self, event):
        if event.receiver == self.reply:
            self.reply_to = self.reply.remote_source.address
        if event.connection == self.route_conn:
            self.n_attached += 1
            if self.n_attached == self.count:
                self.send_ops()

    def on_link_remote_close(self, event):
        if event.link.remote_condition != None:
            self.error = "Received unexpected error on link-close: %s" % event.link.remote_condition.name
            self.timer.cancel()
            self.normal_conn.close()
            self.route_conn.close()

        self.n_detached += 1
        if self.n_detached == self.count:
            self.timer.cancel()
            self.normal_conn.close()
            self.route_conn.close()

    def send_ops(self):
        if self.n_created < self.count:
            while self.n_created < self.count and self.agent.credit > 0:
                props = {'operation': 'CREATE',
                         'type': 'org.apache.qpid.dispatch.router.config.autoLink',
                         'name': 'AL.%d' % self.n_created }
                body  = {'direction': 'out',
                         'containerId': 'container.new',
                         'addr': 'node.%d' % self.n_created }
                msg = Message(properties=props, body=body, reply_to=self.reply_to)
                self.agent.send(msg)
                self.n_created += 1
        elif self.n_attached == self.count and self.n_deleted < self.count:
            while self.n_deleted < self.count and self.agent.credit > 0:
                props = {'operation': 'DELETE',
                         'type': 'org.apache.qpid.dispatch.router.config.autoLink',
                         'name': 'AL.%d' % self.n_deleted }
                body  = {}
                msg = Message(properties=props, body=body, reply_to=self.reply_to)
                self.agent.send(msg)
                self.n_deleted += 1

    def on_sendable(self, event):
        if event.sender == self.agent:
            self.send_ops()

    def on_message(self, event):
        if event.message.properties['statusCode'] // 100 != 2:
            self.error = 'Op Error: %d %s' % (event.message.properties['statusCode'],
                                              event.message.properties['statusDescription'])
            self.timer.cancel()
            self.normal_conn.close()
            self.route_conn.close()

    def run(self):
        container = Container(self)
        container.run()


if __name__ == '__main__':
    unittest.main(main_module())

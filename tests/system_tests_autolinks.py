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
from proton import Message, Delivery, PENDING, ACCEPTED, REJECTED
from system_test import TestCase, Qdrouterd, main_module
from proton.handlers import MessagingHandler
from proton.reactor import Container, AtMostOnce, AtLeastOnce
from proton.utils import BlockingConnection, SyncRequestResponse
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
            # Create a pair of default auto-links for 'node.1'
            #
            ('autoLink', {'addr': 'node.1', 'containerId': 'container.1', 'dir': 'in'}),
            ('autoLink', {'addr': 'node.1', 'containerId': 'container.1', 'dir': 'out'}),
            ('address',  {'prefix': 'node', 'waypoint': 'yes'}),

            #
            # Create a pair of auto-links on non-default phases for container-to-container transfers
            #
            ('autoLink', {'addr': 'xfer.2', 'containerId': 'container.2', 'dir': 'in',  'phase': '4'}),
            ('autoLink', {'addr': 'xfer.2', 'containerId': 'container.3', 'dir': 'out', 'phase': '4'}),
        ])

        cls.router = cls.tester.qdrouterd(name, config)
        cls.router.wait_ready()
        cls.normal_address = cls.router.addresses[0]
        cls.route_address  = cls.router.addresses[1]


    def test_01_autolink_attach(self):
        """
        Create the route-container connection and verify that the appropriate links are attached.
        Disconnect, reconnect, and verify that the links are re-attached.
        """
        test = AutolinkAttachTest(self.route_address)
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


class Timeout(object):
    def __init__(self, parent):
        self.parent = parent

    def on_timer_task(self, event):
        self.parent.timeout()


class AutolinkAttachTest(MessagingHandler):
    def __init__(self, address):
        super(AutolinkAttachTest, self).__init__(prefetch=0)
        self.address  = address
        self.error    = None
        self.sender   = None
        self.receiver = None

        self.n_rx_attach = 0
        self.n_tx_attach = 0

    def timeout(self):
        self.error = "Timeout Expired: n_rx_attach=%d n_tx_attach=%d" % (self.n_rx_attach, self.n_tx_attach)
        self.conn.close()

    def on_start(self, event):
        self.timer = event.reactor.schedule(5, Timeout(self))
        self.conn  = event.container.connect(self.address)

    def on_connection_closed(self, event):
        if self.n_tx_attach == 1:
            self.conn  = event.container.connect(self.address)

    def on_link_opened(self, event):
        if event.sender:
            self.n_tx_attach += 1
            if event.sender.remote_source.address != 'node.1':
                self.error = "Expected sender address 'node.1', got '%s'" % event.sender.remote_source.address
                self.timer.cancel()
                self.conn.close()
        elif event.receiver:
            self.n_rx_attach += 1
            if event.receiver.remote_target.address != 'node.1':
                self.error = "Expected receiver address 'node.1', got '%s'" % event.receiver.remote_target.address
                self.timer.cancel()
                self.conn.close()
        if self.n_tx_attach == 1 and self.n_rx_attach == 1:
            self.conn.close()
        if self.n_tx_attach == 2 and self.n_rx_attach == 2:
            self.conn.close()
            self.timer.cancel()

    def run(self):
        container = Container(self)
        container.container_id = 'container.1'
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

    def timeout(self):
        self.error = "Timeout Expired: last_action=%s" % self.last_action
        if self.normal_conn:
            self.normal_conn.close()
        if self.route_conn:
            self.route_conn.close()

    def on_start(self, event):
        self.timer       = event.reactor.schedule(5, Timeout(self))
        self.normal_conn = event.container.connect(self.normal_address)
        self.sender      = event.container.create_sender(self.normal_conn, self.dest)
        self.last_action = "Attached normal sender"

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


if __name__ == '__main__':
    unittest.main(main_module())

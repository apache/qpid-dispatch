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

from proton import Message, Timeout, symbol
from system_test import TestCase, Qdrouterd, main_module, TIMEOUT, MgmtMsgProxy
from system_test import AsyncTestReceiver
from system_test import AsyncTestSender
from system_test import QdManager
from system_test import unittest
from system_tests_link_routes import ConnLinkRouteService
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


    def test_01_initial_credit_1x7_same_interior(self):
        test = InitialCreditTest(self.routers[0].addresses[0],
                                 [self.routers[0].addresses[0]],
                                 'address.01',
                                 7,  # receiver_credit
                                 7)  # expected_sender_credit
        test.run()
        self.assertEqual(None, test.error)

    def test_02_initial_credit_1x500_same_interior(self):
        test = InitialCreditTest(self.routers[0].addresses[0],
                                 [self.routers[0].addresses[0]],
                                 'address.02',
                                 500,  # receiver_credit
                                 250)  # expected_sender_credit
        test.run()
        self.assertEqual(None, test.error)

    def test_03_initial_credit_4x50_same_interior(self):
        test = InitialCreditTest(self.routers[0].addresses[0],
                                 [self.routers[0].addresses[0], self.routers[0].addresses[0],
                                  self.routers[0].addresses[0], self.routers[0].addresses[0]],
                                 'address.03',
                                 50,  # receiver_credit
                                 200)  # expected_sender_credit
        test.run()
        self.assertEqual(None, test.error)

    def test_04_initial_credit_4x80_same_interior(self):
        test = InitialCreditTest(self.routers[0].addresses[0],
                                 [self.routers[0].addresses[0], self.routers[0].addresses[0],
                                  self.routers[0].addresses[0], self.routers[0].addresses[0]],
                                 'address.04',
                                 80,  # receiver_credit
                                 250)  # expected_sender_credit
        test.run()
        self.assertEqual(None, test.error)


class Timeout(object):
    def __init__(self, parent):
        self.parent = parent

    def on_timer_task(self, event):
        self.parent.timeout()


class InitialCreditTest(MessagingHandler):
    def __init__(self, sender_host, receiver_hosts, addr, receiver_credit, expected_sender_credit):
        """
        Open a sender and a set of receivers for an address.  Once all receivers are open, ensure that the sender
        remains unsendable.  Issue credit to all of the receivers and ensure that the credit given to the sender
        is equal to the expected amount of credit.
        """
        super(InitialCreditTest, self).__init__(prefetch=0)
        self.sender_host     = sender_host
        self.receiver_hosts  = receiver_hosts
        self.receiver_credit = receiver_credit
        self.sender_credit   = expected_sender_credit
        self.addr            = addr

        self.credit_issued        = False
        self.n_sender_credit_seen = 0
        self.open_rx_count        = 0
        self.receiver_conns       = []
        self.receivers            = []
        self.sender_conn          = None
        self.receiver_conn        = None
        self.error                = None

    def timeout(self):
        self.error = "Timeout Expired (credit_issued: %r, sender_credit_seen=%d)" % (self.credit_issued, self.n_sender_credit_seen)
        self.sender_conn.close()
        for conn in self.receiver_conns:
            conn.close()

    def fail(self, error):
        self.error = error
        self.sender_conn.close()
        for R in self.receiver_conns:
            R.close()
        self.timer.cancel()

    def on_start(self, event):
        self.timer       = event.reactor.schedule(10.0, Timeout(self))
        self.sender_conn = event.container.connect(self.sender_host)
        self.sender      = event.container.create_sender(self.sender_conn, self.addr)
        for host in self.receiver_hosts:
            conn = event.container.connect(host)
            self.receiver_conns.append(conn)
            self.receivers.append(event.container.create_receiver(conn, self.addr))

    def on_sendable(self, event):
        if event.sender == self.sender:
            if not self.credit_issued:
                self.fail("Received sender credit before receiver credit was issued")
            else:
                self.n_sender_credit_seen = self.sender.credit
                if self.n_sender_credit_seen == self.sender_credit:
                    self.fail(None)
                if self.n_sender_credit_seen > self.sender_credit:
                    self.fail("Excessive sender credit issued: %d, expected %d" % (self.n_sender_credit_seen, self.sender_credit))

    def on_link_opened(self, event):
        if event.receiver in self.receivers:
            self.open_rx_count += 1
            if self.open_rx_count == len(self.receiver_hosts):
                self.credit_issued = True
                for rx in self.receivers:
                    rx.flow(self.receiver_credit)

    def run(self):
        Container(self).run()


if __name__== '__main__':
    unittest.main(main_module())

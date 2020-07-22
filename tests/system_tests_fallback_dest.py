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


from proton import Message, symbol
from system_test import TestCase, Qdrouterd, main_module, TIMEOUT, MgmtMsgProxy, TestTimeout
from system_test import unittest
from proton.handlers import MessagingHandler
from proton.reactor import Container


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
                ('listener', {'port': cls.tester.get_port(), 'role': 'route-container', 'name': 'WP'}),
                ('address',  {'prefix': 'dest', 'enableFallback': 'yes'}),
                ('autoLink', {'connection': 'WP', 'address': 'dest.al', 'dir': 'out', 'fallback': 'yes'}),
                ('autoLink', {'connection': 'WP', 'address': 'dest.al', 'dir': 'in',  'fallback': 'yes'}),
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

        cls.ROUTER_INTA = cls.routers[0].addresses[0]
        cls.ROUTER_INTB = cls.routers[1].addresses[0]
        cls.ROUTER_EA1 = cls.routers[2].addresses[0]
        cls.ROUTER_EA2 = cls.routers[3].addresses[0]
        cls.ROUTER_EB1 = cls.routers[4].addresses[0]
        cls.ROUTER_EB2 = cls.routers[5].addresses[0]

        cls.ROUTER_INTA_WP = cls.routers[0].addresses[1]
        cls.ROUTER_INTB_WP = cls.routers[1].addresses[1]
        cls.ROUTER_EA1_WP = cls.routers[2].addresses[1]
        cls.ROUTER_EA2_WP = cls.routers[3].addresses[1]
        cls.ROUTER_EB1_WP = cls.routers[4].addresses[1]
        cls.ROUTER_EB2_WP = cls.routers[5].addresses[1]


    def test_01_sender_first_primary_same_interior(self):
        test = SenderFirstTest(self.ROUTER_INTA,
                               self.ROUTER_INTA,
                               'dest.01', False)
        test.run()
        self.assertEqual(None, test.error)

    def test_02_sender_first_fallback_same_interior(self):
        test = SenderFirstTest(self.ROUTER_INTA,
                               self.ROUTER_INTA,
                               'dest.02', True)
        test.run()
        self.assertEqual(None, test.error)

    def test_03_sender_first_primary_same_edge(self):
        test = SenderFirstTest(self.ROUTER_EA1,
                               self.ROUTER_EA1,
                               'dest.03', False)
        test.run()
        self.assertEqual(None, test.error)

    def test_04_sender_first_fallback_same_edge(self):
        test = SenderFirstTest(self.ROUTER_EA1,
                               self.ROUTER_EA1,
                               'dest.04', True)
        test.run()
        self.assertEqual(None, test.error)

    def test_05_sender_first_primary_interior_interior(self):
        test = SenderFirstTest(self.ROUTER_INTA,
                               self.ROUTER_INTB,
                               'dest.05', False)
        test.run()
        self.assertEqual(None, test.error)

    def test_06_sender_first_fallback_interior_interior(self):
        test = SenderFirstTest(self.ROUTER_INTA,
                               self.ROUTER_INTB,
                               'dest.06', True)
        test.run()
        self.assertEqual(None, test.error)

    def test_07_sender_first_primary_edge_interior(self):
        test = SenderFirstTest(self.ROUTER_EA1,
                               self.ROUTER_INTB,
                               'dest.07', False)
        test.run()
        self.assertEqual(None, test.error)

    def test_08_sender_first_fallback_edge_interior(self):
        test = SenderFirstTest(self.ROUTER_EA1,
                               self.ROUTER_INTB,
                               'dest.08', True)
        test.run()
        self.assertEqual(None, test.error)

    def test_09_sender_first_primary_interior_edge(self):
        test = SenderFirstTest(self.ROUTER_INTB,
                               self.ROUTER_EA1,
                               'dest.09', False)
        test.run()
        self.assertEqual(None, test.error)

    def test_10_sender_first_fallback_interior_edge(self):
        test = SenderFirstTest(self.ROUTER_INTB,
                               self.ROUTER_EA1,
                               'dest.10', True)
        test.run()
        self.assertEqual(None, test.error)

    def test_11_sender_first_primary_edge_edge(self):
        test = SenderFirstTest(self.ROUTER_EA1,
                               self.ROUTER_EB1,
                               'dest.11', False)
        test.run()
        self.assertEqual(None, test.error)

    def test_12_sender_first_fallback_edge_edge(self):
        test = SenderFirstTest(self.ROUTER_EA1,
                               self.ROUTER_EB1,
                               'dest.12', True)
        test.run()
        self.assertEqual(None, test.error)

    def test_13_receiver_first_primary_same_interior(self):
        test = ReceiverFirstTest(self.ROUTER_INTA,
                                 self.ROUTER_INTA,
                                 'dest.13', False)
        test.run()
        self.assertEqual(None, test.error)

    def test_14_receiver_first_fallback_same_interior(self):
        test = ReceiverFirstTest(self.ROUTER_INTA,
                                 self.ROUTER_INTA,
                                 'dest.14', True)
        test.run()
        self.assertEqual(None, test.error)

    def test_15_receiver_first_primary_same_edge(self):
        test = ReceiverFirstTest(self.ROUTER_EA1,
                                 self.ROUTER_EA1,
                                 'dest.15', False)
        test.run()
        self.assertEqual(None, test.error)

    def test_16_receiver_first_fallback_same_edge(self):
        test = ReceiverFirstTest(self.ROUTER_EA1,
                                 self.ROUTER_EA1,
                                 'dest.16', True)
        test.run()
        self.assertEqual(None, test.error)

    def test_17_receiver_first_primary_interior_interior(self):
        test = ReceiverFirstTest(self.ROUTER_INTA,
                                 self.ROUTER_INTB,
                                 'dest.17', False)
        test.run()
        self.assertEqual(None, test.error)

    def test_18_receiver_first_fallback_interior_interior(self):
        test = ReceiverFirstTest(self.ROUTER_INTA,
                                 self.ROUTER_INTB,
                                 'dest.18', True)
        test.run()
        self.assertEqual(None, test.error)

    def test_19_receiver_first_primary_edge_interior(self):
        test = ReceiverFirstTest(self.ROUTER_EA1,
                                 self.ROUTER_INTB,
                                 'dest.19', False)
        test.run()
        self.assertEqual(None, test.error)

    def test_20_receiver_first_fallback_edge_interior(self):
        test = ReceiverFirstTest(self.ROUTER_EA1,
                                 self.ROUTER_INTB,
                                 'dest.20', True)
        test.run()
        self.assertEqual(None, test.error)

    def test_21_receiver_first_primary_interior_edge(self):
        test = ReceiverFirstTest(self.ROUTER_INTB,
                                 self.ROUTER_EA1,
                                 'dest.21', False)
        test.run()
        self.assertEqual(None, test.error)

    def test_22_receiver_first_fallback_interior_edge(self):
        test = ReceiverFirstTest(self.ROUTER_INTB,
                                 self.ROUTER_EA1,
                                 'dest.22', True)
        test.run()
        self.assertEqual(None, test.error)

    def test_23_receiver_first_primary_edge_edge(self):
        test = ReceiverFirstTest(self.ROUTER_EA1,
                                 self.ROUTER_EB1,
                                 'dest.23', False)
        test.run()
        self.assertEqual(None, test.error)

    def test_24_receiver_first_fallback_edge_edge(self):
        test = ReceiverFirstTest(self.ROUTER_EA1,
                                 self.ROUTER_EB1,
                                 'dest.24', True)
        test.run()
        self.assertEqual(None, test.error)

    def test_25_switchover_same_edge(self):
        test = SwitchoverTest(self.ROUTER_EA1,
                              self.ROUTER_EA1,
                              self.ROUTER_EA1,
                              'dest.25')
        test.run()
        self.assertEqual(None, test.error)

    def test_26_switchover_same_interior(self):
        test = SwitchoverTest(self.ROUTER_INTA,
                              self.ROUTER_INTA,
                              self.ROUTER_INTA,
                              'dest.26')
        test.run()
        self.assertEqual(None, test.error)

    def test_27_switchover_local_edge_alt_remote_interior(self):
        test = SwitchoverTest(self.ROUTER_EA1,
                              self.ROUTER_INTA,
                              self.ROUTER_EA1,
                              'dest.27')
        test.run()
        self.assertEqual(None, test.error)

    def test_28_switchover_local_edge_alt_remote_edge(self):
        test = SwitchoverTest(self.ROUTER_EA1,
                              self.ROUTER_EB1,
                              self.ROUTER_EA1,
                              'dest.28')
        test.run()
        self.assertEqual(None, test.error)

    def test_29_switchover_local_edge_pri_remote_interior(self):
        test = SwitchoverTest(self.ROUTER_EA1,
                              self.ROUTER_EA1,
                              self.ROUTER_INTA,
                              'dest.29')
        test.run()
        self.assertEqual(None, test.error)

    def test_30_switchover_local_interior_pri_remote_edge(self):
        test = SwitchoverTest(self.ROUTER_EA1,
                              self.ROUTER_EA1,
                              self.ROUTER_EB1,
                              'dest.30')
        test.run()
        self.assertEqual(None, test.error)

    def test_31_switchover_local_interior_alt_remote_interior(self):
        test = SwitchoverTest(self.ROUTER_INTB,
                              self.ROUTER_INTA,
                              self.ROUTER_INTB,
                              'dest.31')
        test.run()
        self.assertEqual(None, test.error)

    def test_32_switchover_local_interior_alt_remote_edge(self):
        test = SwitchoverTest(self.ROUTER_INTB,
                              self.ROUTER_EA2,
                              self.ROUTER_INTB,
                              'dest.32')
        test.run()
        self.assertEqual(None, test.error)

    def test_33_switchover_local_interior_pri_remote_interior(self):
        test = SwitchoverTest(self.ROUTER_INTB,
                              self.ROUTER_INTB,
                              self.ROUTER_INTA,
                              'dest.33')
        test.run()
        self.assertEqual(None, test.error)

    def test_34_switchover_local_interior_pri_remote_edge(self):
        test = SwitchoverTest(self.ROUTER_INTB,
                              self.ROUTER_INTB,
                              self.ROUTER_EB1,
                              'dest.34')
        test.run()
        self.assertEqual(None, test.error)

    def test_35_switchover_mix_1(self):
        test = SwitchoverTest(self.ROUTER_INTA,
                              self.ROUTER_INTB,
                              self.ROUTER_EA1,
                              'dest.35')
        test.run()
        self.assertEqual(None, test.error)

    def test_36_switchover_mix_2(self):
        test = SwitchoverTest(self.ROUTER_EA1,
                              self.ROUTER_INTB,
                              self.ROUTER_INTA,
                              'dest.36')
        test.run()
        self.assertEqual(None, test.error)

    def test_37_switchover_mix_3(self):
        test = SwitchoverTest(self.ROUTER_EA1,
                              self.ROUTER_INTB,
                              self.ROUTER_EB1,
                              'dest.37')
        test.run()
        self.assertEqual(None, test.error)

    def test_38_switchover_mix_4(self):
        test = SwitchoverTest(self.ROUTER_EA1,
                              self.ROUTER_EA2,
                              self.ROUTER_EB1,
                              'dest.38')
        test.run()
        self.assertEqual(None, test.error)

    def test_39_auto_link_sender_first_fallback_same_interior(self):
        test = SenderFirstAutoLinkTest(self.ROUTER_INTA,
                                       self.ROUTER_INTA_WP)
        test.run()
        self.assertEqual(None, test.error)

    def test_40_auto_link_sender_first_fallback_same_edge(self):
        test = SenderFirstAutoLinkTest(self.ROUTER_EA1,
                                       self.ROUTER_EA1_WP)
        test.run()
        self.assertEqual(None, test.error)

    def test_41_auto_link_sender_first_fallback_interior_interior(self):
        test = SenderFirstAutoLinkTest(self.ROUTER_INTA,
                                       self.ROUTER_INTB_WP)
        test.run()
        self.assertEqual(None, test.error)

    def test_42_auto_link_sender_first_fallback_edge_interior(self):
        test = SenderFirstAutoLinkTest(self.ROUTER_EA1,
                                       self.ROUTER_INTA_WP)
        test.run()
        self.assertEqual(None, test.error)

    def test_43_auto_link_sender_first_fallback_interior_edge(self):
        test = SenderFirstAutoLinkTest(self.ROUTER_INTB,
                                       self.ROUTER_EA1_WP)
        test.run()
        self.assertEqual(None, test.error)

    def test_44_auto_link_sender_first_fallback_edge_edge(self):
        test = SenderFirstAutoLinkTest(self.ROUTER_EA1,
                                       self.ROUTER_EB1_WP)
        test.run()
        self.assertEqual(None, test.error)

    def test_45_auto_link_receiver_first_fallback_same_interior(self):
        test = ReceiverFirstAutoLinkTest(self.ROUTER_INTA,
                                         self.ROUTER_INTA_WP)
        test.run()
        self.assertEqual(None, test.error)

    def test_46_auto_link_receiver_first_fallback_same_edge(self):
        test = ReceiverFirstAutoLinkTest(self.ROUTER_EA1,
                                         self.ROUTER_EA1_WP)
        test.run()
        self.assertEqual(None, test.error)

    def test_47_auto_link_receiver_first_fallback_interior_interior(self):
        test = ReceiverFirstAutoLinkTest(self.ROUTER_INTA,
                                         self.ROUTER_INTB_WP)
        test.run()
        self.assertEqual(None, test.error)

    def test_48_auto_link_receiver_first_fallback_edge_interior(self):
        test = ReceiverFirstAutoLinkTest(self.ROUTER_EA1,
                                         self.ROUTER_INTB_WP)
        test.run()
        self.assertEqual(None, test.error)

    def test_49_auto_link_receiver_first_fallback_interior_edge(self):
        test = ReceiverFirstAutoLinkTest(self.ROUTER_INTB,
                                         self.ROUTER_EA1_WP)
        test.run()
        self.assertEqual(None, test.error)

    def test_50_auto_link_receiver_first_fallback_edge_edge(self):
        test = ReceiverFirstAutoLinkTest(self.ROUTER_EA1,
                                         self.ROUTER_EB1_WP)
        test.run()
        self.assertEqual(None, test.error)


class SenderFirstTest(MessagingHandler):
    def __init__(self, sender_host, receiver_host, addr, rx_fallback):
        super(SenderFirstTest, self).__init__()
        self.sender_host   = sender_host
        self.receiver_host = receiver_host
        self.addr          = addr
        self.rx_fallback   = rx_fallback
        self.count         = 300

        self.sender_conn   = None
        self.receiver_conn = None
        self.error         = None
        self.n_tx          = 0
        self.n_rx          = 0
        self.n_rel         = 0
        self.tx_seq        = 0

    def timeout(self):
        self.error = "Timeout Expired - n_tx=%d, n_rx=%d, n_rel=%d" % (self.n_tx, self.n_rx, self.n_rel)
        self.sender_conn.close()
        self.receiver_conn.close()

    def fail(self, error):
        self.error = error
        self.sender_conn.close()
        self.receiver_conn.close()
        self.timer.cancel()

    def on_start(self, event):
        self.timer         = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.sender_conn   = event.container.connect(self.sender_host)
        self.receiver_conn = event.container.connect(self.receiver_host)
        self.sender        = event.container.create_sender(self.sender_conn, self.addr, name=(self.addr + "_sender"))

    def on_link_opened(self, event):
        if event.sender == self.sender:
            rname = self.addr + "_receiver_fallback_" + ("true" if self.rx_fallback else "false")
            self.receiver = event.container.create_receiver(self.receiver_conn, self.addr, name=rname)
            if self.rx_fallback:
                self.receiver.source.capabilities.put_symbol("qd.fallback")

    def on_sendable(self, event):
        if event.sender == self.sender:
            while self.sender.credit > 0 and self.n_tx < self.count:
                self.sender.send(Message("Msg %s %d %d" % (self.addr, self.tx_seq, self.n_tx)))
                self.n_tx += 1
                self.tx_seq += 1

    def on_message(self, event):
        if event.receiver == self.receiver:
            self.n_rx += 1
            if self.n_rx == self.count:
                self.fail(None)

    def on_released(self, event):
        self.n_rel += 1

    def run(self):
        Container(self).run()


class ReceiverFirstTest(MessagingHandler):
    def __init__(self, sender_host, receiver_host, addr, rx_fallback):
        super(ReceiverFirstTest, self).__init__()
        self.sender_host   = sender_host
        self.receiver_host = receiver_host
        self.addr          = addr
        self.rx_fallback   = rx_fallback
        self.count         = 300

        self.sender_conn   = None
        self.receiver_conn = None
        self.error         = None
        self.n_tx          = 0
        self.n_rx          = 0
        self.n_rel         = 0
        self.tx_seq        = 0

    def timeout(self):
        self.error = "Timeout Expired - n_tx=%d, n_rx=%d, n_rel=%d" % (self.n_tx, self.n_rx, self.n_rel)
        self.sender_conn.close()
        self.receiver_conn.close()

    def fail(self, error):
        self.error = error
        self.sender_conn.close()
        self.receiver_conn.close()
        self.timer.cancel()

    def on_start(self, event):
        self.timer         = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.sender_conn   = event.container.connect(self.sender_host)
        self.receiver_conn = event.container.connect(self.receiver_host)
        rname = self.addr + "_receiver_fallback_" + ("true" if self.rx_fallback else "false")
        self.receiver      = event.container.create_receiver(self.receiver_conn, self.addr, name=rname)
        if self.rx_fallback:
            self.receiver.source.capabilities.put_symbol("qd.fallback")

    def on_link_opened(self, event):
        if event.receiver == self.receiver:
            self.sender = event.container.create_sender(self.sender_conn, self.addr, name=(self.addr + "_sender"))

    def on_sendable(self, event):
        if event.sender == self.sender:
            while self.sender.credit > 0 and self.n_tx < self.count:
                self.sender.send(Message("Msg %s %d %d" % (self.addr, self.tx_seq, self.n_tx)))
                self.n_tx += 1
                self.tx_seq += 1

    def on_message(self, event):
        if event.receiver == self.receiver:
            self.n_rx += 1
            if self.n_rx == self.count:
                self.fail(None)

    def on_released(self, event):
        self.n_rel += 1

    def run(self):
        Container(self).run()


class SwitchoverTest(MessagingHandler):
    def __init__(self, sender_host, primary_host, fallback_host, addr):
        super(SwitchoverTest, self).__init__()
        self.sender_host    = sender_host
        self.primary_host   = primary_host
        self.fallback_host  = fallback_host
        self.addr           = addr
        self.count          = 300

        self.sender_conn    = None
        self.primary_conn   = None
        self.fallback_conn  = None
        self.error          = None
        self.n_tx           = 0
        self.n_rx           = 0
        self.n_rel          = 0
        self.phase          = 0
        self.tx_seq         = 0
        self.local_rel      = 0

    def timeout(self):
        self.error = "Timeout Expired - n_tx=%d, n_rx=%d, n_rel=%d, phase=%d, local_rel=%d" % \
                     (self.n_tx, self.n_rx, self.n_rel, self.phase, self.local_rel)
        self.sender_conn.close()
        self.primary_conn.close()
        self.fallback_conn.close()

    def fail(self, error):
        self.error = error
        self.sender_conn.close()
        self.primary_conn.close()
        self.fallback_conn.close()
        self.timer.cancel()

    def on_start(self, event):
        self.timer              = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.sender_conn        = event.container.connect(self.sender_host)
        self.primary_conn       = event.container.connect(self.primary_host)
        self.fallback_conn      = event.container.connect(self.fallback_host)
        self.primary_receiver   = event.container.create_receiver(self.primary_conn, self.addr, name=(self.addr + "_primary_receiver"))
        self.fallback_receiver  = event.container.create_receiver(self.fallback_conn, self.addr, name=(self.addr + "fallback_receiver"))
        self.fallback_receiver.source.capabilities.put_object(symbol("qd.fallback"))

    def on_link_opened(self, event):
        if event.receiver == self.primary_receiver:
            self.sender = event.container.create_sender(self.sender_conn, self.addr, name=(self.addr + "_sender"))

    def on_link_closed(self, event):
        if event.receiver == self.primary_receiver:
            self.n_rx = 0
            self.n_tx = 0
            self.send()

    def send(self):
        while self.sender.credit > 0 and self.n_tx < self.count:
            self.sender.send(Message("Msg %s %d %d" % (self.addr, self.tx_seq, self.n_tx)))
            self.n_tx += 1
            self.tx_seq += 1

    def on_sendable(self, event):
        if event.sender == self.sender:
            self.send()

    def on_message(self, event):
        if not (self.phase == 0 and event.receiver == self.fallback_receiver):
            # Phase 0 message over primary receiver. Phase 1 can come in only on primary.
            self.n_rx += 1
            if self.n_rx == self.count:
                if self.phase == 0:
                    self.phase = 1
                    self.primary_receiver.close()
                else:
                    self.fail(None)
        else:
            # Phase 0 message over fallback receiver. This may happen because
            # primary receiver is on a distant router and the fallback receiver is local.
            # Release the message to keep trying until the primary receiver kicks in.
            self.release(event.delivery)
            self.n_rel += 1
            self.n_tx -= 1
            self.local_rel += 1

    def on_released(self, event):
        self.n_rel += 1
        self.n_tx  -= 1

    def run(self):
        Container(self).run()


class SenderFirstAutoLinkTest(MessagingHandler):
    def __init__(self, sender_host, receiver_host):
        super(SenderFirstAutoLinkTest, self).__init__()
        self.sender_host   = sender_host
        self.receiver_host = receiver_host
        self.addr          = "dest.al"
        self.count         = 300

        self.sender_conn   = None
        self.receiver_conn = None
        self.error         = None
        self.n_tx          = 0
        self.n_rx          = 0
        self.n_rel         = 0
        self.tx_seq        = 0

    def timeout(self):
        self.error = "Timeout Expired - n_tx=%d, n_rx=%d, n_rel=%d" % (self.n_tx, self.n_rx, self.n_rel)
        self.sender_conn.close()
        self.receiver_conn.close()

    def fail(self, error):
        self.error = error
        self.sender_conn.close()
        self.receiver_conn.close()
        self.timer.cancel()

    def on_start(self, event):
        self.timer       = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.sender_conn = event.container.connect(self.sender_host)
        self.sender      = event.container.create_sender(self.sender_conn, self.addr, name=(self.addr + "_sender"))

    def on_link_opening(self, event):
        if event.sender:
            self.alt_sender = event.sender
            event.sender.source.address = self.addr
            event.sender.open()

        elif event.receiver:
            self.alt_receiver = event.receiver
            event.receiver.target.address = self.addr
            event.receiver.open()

    def on_link_opened(self, event):
        if event.sender == self.sender:
            self.receiver_conn = event.container.connect(self.receiver_host)

    def send(self):
        while self.sender.credit > 0 and self.n_tx < self.count:
            self.sender.send(Message("Msg %s %d %d" % (self.addr, self.tx_seq, self.n_tx)))
            self.n_tx += 1
            self.tx_seq += 1

    def on_sendable(self, event):
        if event.sender == self.sender:
            self.send()

    def on_message(self, event):
        self.n_rx += 1
        if self.n_rx == self.count:
            self.fail(None)

    def on_released(self, event):
        self.n_rel += 1
        self.n_tx -= 1
        self.send()

    def run(self):
        Container(self).run()


class ReceiverFirstAutoLinkTest(MessagingHandler):
    def __init__(self, sender_host, receiver_host):
        super(ReceiverFirstAutoLinkTest, self).__init__()
        self.sender_host   = sender_host
        self.receiver_host = receiver_host
        self.addr          = "dest.al"
        self.count         = 300

        self.sender_conn   = None
        self.receiver_conn = None
        self.alt_receiver  = None
        self.error         = None
        self.n_tx          = 0
        self.n_rx          = 0
        self.n_rel         = 0
        self.tx_seq        = 0

    def timeout(self):
        self.error = "Timeout Expired - n_tx=%d, n_rx=%d, n_rel=%d" % (self.n_tx, self.n_rx, self.n_rel)
        self.sender_conn.close()
        self.receiver_conn.close()

    def fail(self, error):
        self.error = error
        self.sender_conn.close()
        self.receiver_conn.close()
        self.timer.cancel()

    def on_start(self, event):
        self.timer         = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.receiver_conn = event.container.connect(self.receiver_host)

    def on_link_opening(self, event):
        if event.sender:
            self.alt_sender = event.sender
            event.sender.source.address = self.addr
            event.sender.open()

        elif event.receiver:
            self.alt_receiver = event.receiver
            event.receiver.target.address = self.addr
            event.receiver.open()

    def on_link_opened(self, event):
        if event.receiver == self.alt_receiver and not self.sender_conn:
            self.sender_conn = event.container.connect(self.sender_host)
            self.sender      = event.container.create_sender(self.sender_conn, self.addr)

    def send(self):
        while self.sender.credit > 0 and self.n_tx < self.count:
            self.sender.send(Message("Msg %s %d %d" % (self.addr, self.tx_seq, self.n_tx)))
            self.n_tx += 1
            self.tx_seq += 1

    def on_sendable(self, event):
        if event.sender == self.sender:
            self.send()

    def on_message(self, event):
        self.n_rx += 1
        if self.n_rx == self.count:
            self.fail(None)

    def on_released(self, event):
        self.n_rel += 1
        self.n_tx -= 1
        self.send()

    def run(self):
        Container(self).run()


if __name__== '__main__':
    unittest.main(main_module())

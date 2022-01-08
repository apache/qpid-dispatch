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

from proton import Message
from proton.handlers import MessagingHandler
from proton.reactor import Container

from system_test import unittest
from system_test import TestCase, Qdrouterd, main_module, TIMEOUT, MgmtMsgProxy, TestTimeout, PollTimeout


class AddrTimer:
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

        def router(name, mode, connection, extra=None, args=None):
            config = [
                ('router', {'mode': mode, 'id': name}),
                ('listener', {'port': cls.tester.get_port(), 'stripAnnotations': 'no'}),
                connection
            ]

            if extra:
                config.append(extra)
            config = Qdrouterd.Config(config)
            cls.routers.append(cls.tester.qdrouterd(name, config, wait=True, cl_args=args or []))

        cls.routers = []

        inter_router_port = cls.tester.get_port()
        edge_port_A       = cls.tester.get_port()
        edge_port_B       = cls.tester.get_port()

        router('INT.A', 'interior', ('listener', {'role': 'inter-router', 'port': inter_router_port}),
               ('listener', {'role': 'edge', 'port': edge_port_A}), ["-T"])
        router('INT.B', 'interior', ('connector', {'name': 'connectorToA', 'role': 'inter-router', 'port': inter_router_port}),
               ('listener', {'role': 'edge', 'port': edge_port_B}), ["-T"])
        router('EA1',   'edge',     ('connector', {'name': 'edge', 'role': 'edge', 'port': edge_port_A}), None, ["-T"])
        router('EA2',   'edge',     ('connector', {'name': 'edge', 'role': 'edge', 'port': edge_port_A}), None, ["-T"])
        router('EB1',   'edge',     ('connector', {'name': 'edge', 'role': 'edge', 'port': edge_port_B}), None, ["-T"])
        router('EB2',   'edge',     ('connector', {'name': 'edge', 'role': 'edge', 'port': edge_port_B}), None, ["-T"])

        cls.routers[0].wait_router_connected('INT.B')
        cls.routers[1].wait_router_connected('INT.A')

    def test_01_delayed_settlement_same_interior(self):
        test = DelayedSettlementTest(self.routers[0].addresses[0],
                                     self.routers[0].addresses[0],
                                     self.routers[0].addresses[0],
                                     'dest.01', 10, [2], False)
        test.run()
        self.assertIsNone(test.error)

    def test_02_delayed_settlement_different_edges_check_sender(self):
        test = DelayedSettlementTest(self.routers[2].addresses[0],
                                     self.routers[5].addresses[0],
                                     self.routers[2].addresses[0],
                                     'dest.02', 10, [2, 3, 8], False)
        test.run()
        self.assertIsNone(test.error)

    def test_03_delayed_settlement_different_edges_check_receiver(self):
        test = DelayedSettlementTest(self.routers[2].addresses[0],
                                     self.routers[5].addresses[0],
                                     self.routers[5].addresses[0],
                                     'dest.03', 10, [2, 4, 9], False)
        test.run()
        self.assertIsNone(test.error)

    def test_04_delayed_settlement_different_edges_check_interior(self):
        test = DelayedSettlementTest(self.routers[2].addresses[0],
                                     self.routers[5].addresses[0],
                                     self.routers[0].addresses[0],
                                     'dest.04', 10, [0, 2, 3, 8], False)
        test.run()
        self.assertIsNone(test.error)

    def test_05_no_settlement_same_interior(self):
        test = DelayedSettlementTest(self.routers[0].addresses[0],
                                     self.routers[0].addresses[0],
                                     self.routers[0].addresses[0],
                                     'dest.05', 10, [0, 2, 4, 9], True)
        test.run()
        self.assertIsNone(test.error)

    def test_06_no_settlement_different_edges_check_sender(self):
        test = DelayedSettlementTest(self.routers[2].addresses[0],
                                     self.routers[5].addresses[0],
                                     self.routers[2].addresses[0],
                                     'dest.06', 10, [9], True)
        test.run()
        self.assertIsNone(test.error)

    def test_07_no_settlement_different_edges_check_receiver(self):
        test = DelayedSettlementTest(self.routers[2].addresses[0],
                                     self.routers[5].addresses[0],
                                     self.routers[5].addresses[0],
                                     'dest.07', 10, [0, 9], True)
        test.run()
        self.assertIsNone(test.error)

    def test_08_no_settlement_different_edges_check_interior(self):
        test = DelayedSettlementTest(self.routers[2].addresses[0],
                                     self.routers[5].addresses[0],
                                     self.routers[0].addresses[0],
                                     'dest.08', 10, [1, 2, 3, 4, 5, 6, 7, 8], True)
        test.run()
        self.assertIsNone(test.error)

    def test_09_receiver_link_credit_test(self):
        test = RxLinkCreditTest(self.routers[0].addresses[0])
        test.run()
        self.assertIsNone(test.error)

    def test_10_sender_link_credit_test(self):
        test = TxLinkCreditTest(self.routers[0].addresses[0])
        test.run()
        self.assertIsNone(test.error)


class DelayedSettlementTest(MessagingHandler):
    def __init__(self, sender_host, receiver_host, query_host, addr, dlv_count, stuck_list, close_link):
        super(DelayedSettlementTest, self).__init__(auto_accept=False)
        self.sender_host   = sender_host
        self.receiver_host = receiver_host
        self.query_host    = query_host
        self.addr          = addr
        self.dlv_count     = dlv_count
        self.stuck_list    = stuck_list
        self.close_link    = close_link
        self.stuck_dlvs    = []

        self.sender_conn    = None
        self.receiver_conn  = None
        self.query_conn     = None
        self.error          = None
        self.n_tx           = 0
        self.n_rx           = 0
        self.expected_stuck = 0
        self.last_stuck     = 0

    def timeout(self):
        self.error = "Timeout Expired - n_tx=%d, n_rx=%d, expected_stuck=%d last_stuck=%d" %\
            (self.n_tx, self.n_rx, self.expected_stuck, self.last_stuck)
        self.sender_conn.close()
        self.receiver_conn.close()
        self.query_conn.close()
        if self.poll_timer:
            self.poll_timer.cancel()

    def fail(self, error):
        self.error = error
        self.sender_conn.close()
        self.receiver_conn.close()
        self.query_conn.close()
        if self.poll_timer:
            self.poll_timer.cancel()
        self.timer.cancel()

    def on_start(self, event):
        self.timer          = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.poll_timer     = None
        self.receiver_conn  = event.container.connect(self.receiver_host)
        self.query_conn     = event.container.connect(self.query_host)
        self.receiver       = event.container.create_receiver(self.receiver_conn, self.addr)
        self.reply_receiver = event.container.create_receiver(self.query_conn, None, dynamic=True)
        self.query_sender   = event.container.create_sender(self.query_conn, "$management")
        self.proxy          = None

    def on_link_opened(self, event):
        if event.receiver == self.reply_receiver:
            self.reply_addr = event.receiver.remote_source.address
            self.proxy      = MgmtMsgProxy(self.reply_addr)

            # Create the sender only after the self.proxy is populated.
            # If the sender was created in the on_start, in some cases, the on_sendable
            # is called before the on_link_opened and the self.proxy remains empty.
            # see DISPATCH-1675.
            # The second error in test_04_delayed_settlement_different_edges_check_interior
            # is caused due to the first test failure, so this fix will
            # fix the second failure
            self.sender_conn = event.container.connect(self.sender_host)
            self.sender = event.container.create_sender(self.sender_conn,
                                                        self.addr)

    def on_sendable(self, event):
        if event.sender == self.sender:
            while self.sender.credit > 0 and self.n_tx < self.dlv_count:
                self.sender.send(Message("Message %d" % self.n_tx))
                self.n_tx += 1

    def on_message(self, event):
        if event.receiver == self.receiver:
            if self.n_rx not in self.stuck_list:
                self.accept(event.delivery)
            else:
                self.stuck_dlvs.append(event.delivery)
            self.n_rx += 1
            if self.n_rx == self.dlv_count:
                self.query_stats(len(self.stuck_list) * 2)
        elif event.receiver == self.reply_receiver:
            response = self.proxy.response(event.message)
            self.accept(event.delivery)
            self.last_stuck = response.results[0].deliveriesStuck
            if self.last_stuck == self.expected_stuck:
                if self.close_link:
                    self.receiver.close()
                else:
                    for dlv in self.stuck_dlvs:
                        self.accept(dlv)
                    self.stuck_dlvs = []
                if self.expected_stuck > 0:
                    self.query_stats(0)
                else:
                    self.fail(None)
            else:
                self.poll_timer = event.reactor.schedule(0.5, PollTimeout(self))

    def query_stats(self, expected_stuck):
        self.expected_stuck = expected_stuck
        msg = self.proxy.query_router()
        self.query_sender.send(msg)

    def poll_timeout(self):
        self.query_stats(self.expected_stuck)

    def run(self):
        Container(self).run()


class RxLinkCreditTest(MessagingHandler):
    def __init__(self, host):
        super(RxLinkCreditTest, self).__init__(prefetch=0)
        self.host = host

        self.receiver_conn = None
        self.query_conn    = None
        self.addr          = "rx/link/credit/test"
        self.credit_issued = 0
        self.error         = None

        self.stages = ['Setup', 'LinkBlocked', 'LinkUnblocked', '10Credits', '20Credits']
        self.stage  = 0

    def timeout(self):
        self.error = "Timeout Expired - stage: %s" % self.stages[self.stage]
        self.receiver_conn.close()
        self.query_conn.close()
        if self.poll_timer:
            self.poll_timer.cancel()

    def fail(self, error):
        self.error = error
        self.receiver_conn.close()
        self.query_conn.close()
        if self.poll_timer:
            self.poll_timer.cancel()
        self.timer.cancel()

    def on_start(self, event):
        self.timer          = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.poll_timer     = None
        self.receiver_conn  = event.container.connect(self.host)
        self.query_conn     = event.container.connect(self.host)
        self.reply_receiver = event.container.create_receiver(self.query_conn, None, dynamic=True)
        self.query_sender   = event.container.create_sender(self.query_conn, "$management")
        self.receiver       = None

    def on_link_opened(self, event):
        if event.receiver == self.reply_receiver:
            self.reply_addr = event.receiver.remote_source.address
            self.proxy      = MgmtMsgProxy(self.reply_addr)
            self.receiver   = event.container.create_receiver(self.receiver_conn, self.addr)
            self.reply_receiver.flow(1)
        elif event.receiver == self.receiver:
            self.stage = 1
            self.process()

    def process(self):
        if self.stage == 1:
            #
            # LinkBlocked
            #
            msg = self.proxy.query_router()
            self.query_sender.send(msg)

        elif self.stage == 2:
            #
            # LinkUnblocked
            #
            msg = self.proxy.query_router()
            self.query_sender.send(msg)

        elif self.stage == 3:
            #
            # 10Credits
            #
            msg = self.proxy.query_links()
            self.query_sender.send(msg)

        elif self.stage == 4:
            #
            # 20Credits
            #
            msg = self.proxy.query_links()
            self.query_sender.send(msg)

    def on_message(self, event):
        if event.receiver == self.reply_receiver:
            response = self.proxy.response(event.message)
            self.reply_receiver.flow(1)
            if self.stage == 1:
                #
                # LinkBlocked
                #
                if response.results[0].linksBlocked == 1:
                    self.receiver.flow(10)
                    self.stage = 2
                    self.process()
                    return

            elif self.stage == 2:
                #
                # LinkUnblocked
                #
                if response.results[0].linksBlocked == 0:
                    self.stage = 3
                    self.process()
                    return

            elif self.stage == 3:
                #
                # 10Credits
                #
                for link in response.results:
                    if 'M0' + self.addr == link.owningAddr:
                        if link.creditAvailable == 10:
                            self.receiver.flow(10)
                            self.stage = 4
                            self.process()
                            return

            elif self.stage == 4:
                #
                # 20Credits
                #
                for link in response.results:
                    if 'M0' + self.addr == link.owningAddr:
                        if link.creditAvailable == 20:
                            self.fail(None)
                            return

            self.poll_timer = event.reactor.schedule(0.5, PollTimeout(self))

    def poll_timeout(self):
        self.process()

    def run(self):
        Container(self).run()


class TxLinkCreditTest(MessagingHandler):
    def __init__(self, host):
        super(TxLinkCreditTest, self).__init__()
        self.host = host

        self.sender_conn   = None
        self.query_conn    = None
        self.addr          = "rx/link/credit/test"
        self.credit_issued = 0
        self.error         = None

        self.stages = ['Setup', 'LinkBlocked', 'LinkUnblocked', '250Credits']
        self.stage  = 0

    def timeout(self):
        self.error = "Timeout Expired - stage: %s" % self.stages[self.stage]
        self.sender_conn.close()
        self.query_conn.close()
        if self.poll_timer:
            self.poll_timer.cancel()

    def fail(self, error):
        self.error = error
        self.sender_conn.close()
        self.query_conn.close()
        if self.poll_timer:
            self.poll_timer.cancel()
        self.timer.cancel()

    def on_start(self, event):
        self.timer          = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.poll_timer     = None
        self.sender_conn    = event.container.connect(self.host)
        self.query_conn     = event.container.connect(self.host)
        self.reply_receiver = event.container.create_receiver(self.query_conn, None, dynamic=True)
        self.query_sender   = event.container.create_sender(self.query_conn, "$management")
        self.sender         = None
        self.receiver       = None

    def on_link_opened(self, event):
        if event.receiver == self.reply_receiver:
            self.reply_addr = event.receiver.remote_source.address
            self.proxy      = MgmtMsgProxy(self.reply_addr)
            self.sender     = event.container.create_sender(self.sender_conn, self.addr)
        elif event.sender == self.sender:
            self.stage = 1
            self.process()

    def process(self):
        if self.stage == 1:
            #
            # LinkBlocked
            #
            msg = self.proxy.query_router()
            self.query_sender.send(msg)

        elif self.stage == 2:
            #
            # LinkUnblocked
            #
            msg = self.proxy.query_router()
            self.query_sender.send(msg)

        elif self.stage == 3:
            #
            # 250Credits
            #
            msg = self.proxy.query_links()
            self.query_sender.send(msg)

    def on_message(self, event):
        if event.receiver == self.reply_receiver:
            response = self.proxy.response(event.message)
            if self.stage == 1:
                #
                # LinkBlocked
                #
                if response.results[0].linksBlocked == 1:
                    self.receiver = event.container.create_receiver(self.sender_conn, self.addr)
                    self.stage = 2
                    self.process()
                    return

            elif self.stage == 2:
                #
                # LinkUnblocked
                #
                if response.results[0].linksBlocked == 0:
                    self.stage = 3
                    self.process()
                    return

            elif self.stage == 3:
                #
                # 250Credits
                #
                for link in response.results:
                    if 'M0' + self.addr == link.owningAddr:
                        if link.creditAvailable == 250:
                            self.fail(None)
                            return

            self.poll_timer = event.reactor.schedule(0.5, PollTimeout(self))

    def poll_timeout(self):
        self.process()

    def run(self):
        Container(self).run()


if __name__ == '__main__':
    unittest.main(main_module())

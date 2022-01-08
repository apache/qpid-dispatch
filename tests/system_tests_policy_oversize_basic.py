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

from system_test import TestCase, Qdrouterd, main_module, TIMEOUT, Logger, TestTimeout
from proton import Message
from proton.handlers import MessagingHandler
from proton.reactor import Container

# How many worker threads?
W_THREADS = 2

# Define oversize denial condition
OVERSIZE_CONDITION_NAME = "amqp:connection:forced"
OVERSIZE_CONDITION_DESC = "Message size exceeded"

#
# DISPATCH-975 Detect that an oversize message is blocked.
# These tests check basic blocking where the the sender is blocked by
# the ingress routers. It does not check compound blocking where
# oversize is allowed or denied by an ingress edge router but also
# denied by the uplink interior router.


class OversizeMessageTransferTest(MessagingHandler):
    """
    This test connects a sender and a receiver. Then it tries to send _count_
    number of messages of the given size through the router or router network.

    With expect_block=True the ingress router should detect the sender's oversize
    message and close the sender connection. The receiver may receive
    aborted message indications but that is not guaranteed. If any aborted
    messages are received then the count must be at most one.
    The test is a success when the sender receives a connection error with
    oversize indication and the receiver has not received too many aborts.

    With expect_block=False sender messages should be received normally.
    The test is a success when n_accepted == count.
    """

    def __init__(self, sender_host, receiver_host, test_address,
                 message_size=100000, count=10, expect_block=True, print_to_console=False):
        super(OversizeMessageTransferTest, self).__init__()
        self.sender_host = sender_host
        self.receiver_host = receiver_host
        self.test_address = test_address
        self.msg_size = message_size
        self.count = count
        self.expect_block = expect_block

        self.sender_conn = None
        self.receiver_conn = None
        self.error = None
        self.sender = None
        self.receiver = None
        self.proxy = None

        self.n_sent = 0
        self.n_rcvd = 0
        self.n_accepted = 0
        self.n_rejected = 0
        self.n_aborted = 0
        self.n_connection_error = 0
        self.shut_down = False

        self.logger = Logger(title=("OversizeMessageTransferTest - %s" % (self.test_address)), print_to_console=print_to_console)
        self.log_unhandled = False

    def timeout(self):
        self.error = "Timeout Expired: n_sent=%d n_rcvd=%d n_rejected=%d n_aborted=%d" % \
                     (self.n_sent, self.n_rcvd, self.n_rejected, self.n_aborted)
        self.logger.log("self.timeout " + self.error)
        self._shut_down_test()

    def on_start(self, event):
        self.logger.log("on_start")
        self.timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.logger.log("on_start: opening receiver connection to %s" % (self.receiver_host.addresses[0]))
        self.receiver_conn = event.container.connect(self.receiver_host.addresses[0])
        self.logger.log("on_start: opening   sender connection to %s" % (self.sender_host.addresses[0]))
        self.sender_conn = event.container.connect(self.sender_host.addresses[0])
        self.logger.log("on_start: Creating receiver")
        self.receiver = event.container.create_receiver(self.receiver_conn, self.test_address)
        self.logger.log("on_start: Creating sender")
        self.sender = event.container.create_sender(self.sender_conn, self.test_address)
        self.logger.log("on_start: done")

    def send(self):
        while self.sender.credit > 0 and self.n_sent < self.count:
            # construct message in indentifiable chunks
            body_msg = ""
            padchar = "abcdefghijklmnopqrstuvwxyz@#$%"[self.n_sent % 30]
            while len(body_msg) < self.msg_size:
                chunk = "[%s:%d:%d" % (self.test_address, self.n_sent, len(body_msg))
                padlen = 50 - len(chunk)
                chunk += padchar * padlen
                body_msg += chunk
            if len(body_msg) > self.msg_size:
                body_msg = body_msg[:self.msg_size]
            self.logger.log("send. address:%s message:%d of %s length=%d" %
                            (self.test_address, self.n_sent, self.count, self.msg_size))
            m = Message(body=body_msg)
            self.sender.send(m)
            self.n_sent += 1

    def on_sendable(self, event):
        if event.sender == self.sender:
            self.logger.log("on_sendable")
            self.send()

    def on_message(self, event):
        if self.expect_block:
            # All messages should violate maxMessageSize.
            # Receiving any is an error.
            self.error = "Received a message. Expected to receive no messages."
            self.logger.log(self.error)
            self._shut_down_test()
        else:
            self.n_rcvd += 1
            self.accept(event.delivery)
            self._check_done()

    def on_connection_remote_close(self, event):
        if self.shut_down:
            return
        if event.connection == self.sender_conn:
            if event.connection.remote_condition is not None:
                if event.connection.remote_condition.name == OVERSIZE_CONDITION_NAME and \
                   event.connection.remote_condition.description == OVERSIZE_CONDITION_DESC:
                    self.logger.log("on_connection_remote_close: sender closed with correct condition")
                    self.n_connection_error += 1
                    self.sender_conn.close()
                    self.sender_conn = None
                else:
                    # sender closed but for wrong reason
                    self.error = "sender close error: Expected name: %s, description: %s, but received name: %s, description: %s" % (
                                 OVERSIZE_CONDITION_NAME, OVERSIZE_CONDITION_DESC,
                                 event.connection.remote_condition.name, event.connection.remote_condition.description)
                    self.logger.log(self.error)
            else:
                self.error = "sender close error: Expected a remote_condition but there was none."
                self.logger.log(self.error)
        else:
            # connection error but not for sender
            self.error = "unexpected connection close error: wrong connection closed."
            self.logger.log(self.error)
        self._check_done()

    def _shut_down_test(self):
        self.shut_down = True
        if self.timer:
            self.timer.cancel()
            self.timer = None
        if self.sender:
            self.sender.close()
            self.sender = None
        if self.receiver:
            self.receiver.close()
            self.receiver = None
        if self.sender_conn:
            self.sender_conn.close()
            self.sender_conn = None
        if self.receiver_conn:
            self.receiver_conn.close()
            self.receiver_conn = None

    def _check_done(self):
        current = ("check_done: sent=%d rcvd=%d rejected=%d aborted=%d connection_error:%d" %
                   (self.n_sent, self.n_rcvd, self.n_rejected, self.n_aborted, self.n_connection_error))
        self.logger.log(current)
        if self.error is not None:
            self.logger.log("TEST FAIL")
            self._shut_down_test()
        else:
            done = (self.n_connection_error == 1) \
                if self.expect_block else \
                (self.n_sent == self.count and self.n_rcvd == self.count)

            if done:
                self.logger.log("TEST DONE!!!")
                # self.log_unhandled = True # verbose debugging
                self._shut_down_test()

    def on_rejected(self, event):
        self.n_rejected += 1
        if self.expect_block:
            self.logger.log("on_rejected: entry")
            self._check_done()
        else:
            self.error = "Unexpected on_reject"
            self.logger.log(self.error)
            self._check_done()

    def on_aborted(self, event):
        self.logger.log("on_aborted")
        self.n_aborted += 1
        self._check_done()

    def on_error(self, event):
        self.error = "Container error"
        self.logger.log(self.error)
        self._shut_down_test()

    def on_unhandled(self, method, *args):
        if self.log_unhandled:
            self.logger.log("on_unhandled: method: %s, args: %s" % (method, args))

    def run(self):
        try:
            Container(self).run()
        except Exception as e:
            self.error = "Container run exception: %s" % (e)
            self.logger.log(self.error)
            self.logger.dump()


# For the next test case define max sizes for each router.
# These are the configured maxMessageSize values
EA1_MAX_SIZE = 50000
INTA_MAX_SIZE = 100000
INTB_MAX_SIZE = 150000
EB1_MAX_SIZE = 200000

# DISPATCH-1645 S32 max size is chosen to expose signed 32-bit
# wraparound bug. Sizes with bit 31 set look negative when used as
# C 'int' and prevent any message from passing policy checks.
S32_MAX_SIZE = 2**31

# Interior routers enforce max size directly.
# Edge routers are also checked by the attached interior router.

# Block tests that use edge routers that send messages to the network must
# account for the fact that the attached interior router will apply
# another max size. These tests do not check against EB1 max for the
# sender if the receiver is on EA1, INTA, or INTB since INTB's max
# would kick an and cause a false positive.

# Tests that check for allowing near-max sizes use the minimum of
# the edge router's max and the attached interior router's max.

# The bytes-over and bytes-under max that should trigger allow or deny.
# Messages with content this much over should be blocked while
# messages with content this much under should be allowed.
# * client overhead is typically 16 bytes or so
# * interrouter overhead is much larger with annotations
OVER_UNDER = 200


class MaxMessageSizeBlockOversize(TestCase):
    """verify that maxMessageSize blocks oversize messages"""
    @classmethod
    def setUpClass(cls):
        """Start the router"""
        super(MaxMessageSizeBlockOversize, cls).setUpClass()

        def router(name, mode, max_size, extra):
            config = [
                ('router', {'mode': mode,
                            'id': name,
                            'allowUnsettledMulticast': 'yes',
                            'workerThreads': W_THREADS}),
                ('listener', {'role': 'normal',
                              'port': cls.tester.get_port()}),
                ('address', {'prefix': 'multicast', 'distribution': 'multicast'}),
                ('policy', {'maxConnections': 100, 'enableVhostPolicy': 'true', 'maxMessageSize': max_size, 'defaultVhost': '$default'}),
                ('vhost', {'hostname': '$default',
                           'allowUnknownUser': 'true',
                           'groups': {
                               '$default': {
                                   'users': '*',
                                   'maxConnections': 100,
                                   'remoteHosts': '*',
                                   'sources': '*',
                                   'targets': '*',
                                   'allowAnonymousSender': 'true',
                                   'allowWaypointLinks': 'true',
                                   'allowDynamicSource': 'true'
                               }
                           }
                           })
            ]

            if extra:
                config.extend(extra)
            config = Qdrouterd.Config(config)
            cls.routers.append(cls.tester.qdrouterd(name, config, wait=True))
            return cls.routers[-1]

        # configuration:
        # two edge routers connected via 2 interior routers with max sizes
        #
        #  +-------+    +---------+    +---------+    +-------+
        #  |  EA1  |<==>|  INT.A  |<==>|  INT.B  |<==>|  EB1  |
        #  | 50,000|    | 100,000 |    | 150,000 |    |200,000|
        #  +-------+    +---------+    +---------+    +-------+
        #
        # Note:
        #  * Messages whose senders connect to INT.A or INT.B are subject to max message size
        #    defined for the ingress router only.
        #  * Message whose senders connect to EA1 or EA2 are subject to max message size
        #    defined for the ingress router. If the message is forwarded through the
        #    connected interior router then the message is subject to another max message size
        #    defined by the interior router.

        cls.routers = []

        interrouter_port = cls.tester.get_port()
        cls.INTA_edge_port   = cls.tester.get_port()
        cls.INTB_edge_port   = cls.tester.get_port()

        router('INT.A', 'interior', INTA_MAX_SIZE,
               [('listener', {'role': 'inter-router',
                              'port': interrouter_port}),
                ('listener', {'role': 'edge', 'port': cls.INTA_edge_port})])
        cls.INT_A = cls.routers[0]
        cls.INT_A.listener = cls.INT_A.addresses[0]

        router('INT.B', 'interior', INTB_MAX_SIZE,
               [('connector', {'name': 'connectorToA',
                               'role': 'inter-router',
                               'port': interrouter_port}),
                ('listener', {'role': 'edge',
                              'port': cls.INTB_edge_port})])
        cls.INT_B = cls.routers[1]
        cls.INT_B.listener = cls.INT_B.addresses[0]

        router('EA1', 'edge', EA1_MAX_SIZE,
               [('listener', {'name': 'rc', 'role': 'route-container',
                              'port': cls.tester.get_port()}),
                ('connector', {'name': 'uplink', 'role': 'edge',
                               'port': cls.INTA_edge_port})])
        cls.EA1 = cls.routers[2]
        cls.EA1.listener = cls.EA1.addresses[0]

        router('EB1', 'edge', EB1_MAX_SIZE,
               [('connector', {'name': 'uplink',
                               'role': 'edge',
                               'port': cls.INTB_edge_port,
                               'maxFrameSize': 1024}),
                ('listener', {'name': 'rc', 'role': 'route-container',
                              'port': cls.tester.get_port()})])
        cls.EB1 = cls.routers[3]
        cls.EB1.listener = cls.EB1.addresses[0]

        router('S32', 'standalone', S32_MAX_SIZE, [])
        cls.S32 = cls.routers[4]
        cls.S32.listener = cls.S32.addresses[0]

        cls.INT_A.wait_router_connected('INT.B')
        cls.INT_B.wait_router_connected('INT.A')
        cls.EA1.wait_connectors()
        cls.EB1.wait_connectors()

    def test_40_block_oversize_INTA_INTA(self):
        test = OversizeMessageTransferTest(MaxMessageSizeBlockOversize.INT_A,
                                           MaxMessageSizeBlockOversize.INT_A,
                                           "e40",
                                           message_size=INTA_MAX_SIZE + OVER_UNDER,
                                           expect_block=True,
                                           print_to_console=False)
        test.run()
        if test.error is not None:
            test.logger.log("test_40 test error: %s" % (test.error))
            test.logger.dump()
        self.assertTrue(test.error is None)

    def test_41_block_oversize_INTA_INTB(self):
        test = OversizeMessageTransferTest(MaxMessageSizeBlockOversize.INT_A,
                                           MaxMessageSizeBlockOversize.INT_B,
                                           "e41",
                                           message_size=INTA_MAX_SIZE + OVER_UNDER,
                                           expect_block=True)
        test.run()
        if test.error is not None:
            test.logger.log("test_41 test error: %s" % (test.error))
            test.logger.dump()
        self.assertTrue(test.error is None)

    def test_42_block_oversize_INTA_EA1(self):
        test = OversizeMessageTransferTest(MaxMessageSizeBlockOversize.INT_A,
                                           MaxMessageSizeBlockOversize.EA1,
                                           "e42",
                                           message_size=INTA_MAX_SIZE + OVER_UNDER,
                                           expect_block=True)
        test.run()
        if test.error is not None:
            test.logger.log("test_42 test error: %s" % (test.error))
            test.logger.dump()
        self.assertTrue(test.error is None)

    def test_43_block_oversize_INTA_EB1(self):
        test = OversizeMessageTransferTest(MaxMessageSizeBlockOversize.INT_A,
                                           MaxMessageSizeBlockOversize.EB1,
                                           "e43",
                                           message_size=INTA_MAX_SIZE + OVER_UNDER,
                                           expect_block=True)
        test.run()
        if test.error is not None:
            test.logger.log("test_43 test error: %s" % (test.error))
            test.logger.dump()
        self.assertTrue(test.error is None)

    def test_44_block_oversize_INTB_INTA(self):
        test = OversizeMessageTransferTest(MaxMessageSizeBlockOversize.INT_B,
                                           MaxMessageSizeBlockOversize.INT_A,
                                           "e44",
                                           message_size=INTB_MAX_SIZE + OVER_UNDER,
                                           expect_block=True)
        test.run()
        if test.error is not None:
            test.logger.log("test_44 test error: %s" % (test.error))
            test.logger.dump()
        self.assertTrue(test.error is None)

    def test_45_block_oversize_INTB_INTB(self):
        test = OversizeMessageTransferTest(MaxMessageSizeBlockOversize.INT_B,
                                           MaxMessageSizeBlockOversize.INT_B,
                                           "e45",
                                           message_size=INTB_MAX_SIZE + OVER_UNDER,
                                           expect_block=True)
        test.run()
        if test.error is not None:
            test.logger.log("test_45 test error: %s" % (test.error))
            test.logger.dump()
        self.assertTrue(test.error is None)

    def test_46_block_oversize_INTB_EA1(self):
        test = OversizeMessageTransferTest(MaxMessageSizeBlockOversize.INT_B,
                                           MaxMessageSizeBlockOversize.EA1,
                                           "e46",
                                           message_size=INTB_MAX_SIZE + OVER_UNDER,
                                           expect_block=True)
        test.run()
        if test.error is not None:
            test.logger.log("test_46 test error: %s" % (test.error))
            test.logger.dump()
        self.assertTrue(test.error is None)

    def test_47_block_oversize_INTB_EB1(self):
        test = OversizeMessageTransferTest(MaxMessageSizeBlockOversize.INT_B,
                                           MaxMessageSizeBlockOversize.EB1,
                                           "e47",
                                           message_size=INTB_MAX_SIZE + OVER_UNDER,
                                           expect_block=True)
        test.run()
        if test.error is not None:
            test.logger.log("test_47 test error: %s" % (test.error))
            test.logger.dump()
        self.assertTrue(test.error is None)

    def test_48_block_oversize_EA1_INTA(self):
        if EA1_MAX_SIZE >= INTA_MAX_SIZE:
            self.skipTest("EA1 sending to INT.A may be blocked by EA1 limit and also by INT.A limit. That condition is tested in compound test.")
        test = OversizeMessageTransferTest(MaxMessageSizeBlockOversize.EA1,
                                           MaxMessageSizeBlockOversize.INT_A,
                                           "e48",
                                           message_size=EA1_MAX_SIZE + OVER_UNDER,
                                           expect_block=True)
        test.run()
        if test.error is not None:
            test.logger.log("test_48 test error: %s" % (test.error))
            test.logger.dump()
        self.assertTrue(test.error is None)

    def test_49_block_oversize_EA1_INTB(self):
        if EA1_MAX_SIZE >= INTA_MAX_SIZE:
            self.skipTest("EA1 sending to INT.B may be blocked by EA1 limit and also by INT.A limit. That condition is tested in compound test.")
        test = OversizeMessageTransferTest(MaxMessageSizeBlockOversize.EA1,
                                           MaxMessageSizeBlockOversize.INT_B,
                                           "e49",
                                           message_size=EA1_MAX_SIZE + OVER_UNDER,
                                           expect_block=True)
        test.run()
        if test.error is not None:
            test.logger.log("test_49 test error: %s" % (test.error))
            test.logger.dump()
        self.assertTrue(test.error is None)

    def test_4a_block_oversize_EA1_EA1(self):
        test = OversizeMessageTransferTest(MaxMessageSizeBlockOversize.EA1,
                                           MaxMessageSizeBlockOversize.EA1,
                                           "e4a",
                                           message_size=EA1_MAX_SIZE + OVER_UNDER,
                                           expect_block=True)
        test.run()
        if test.error is not None:
            test.logger.log("test_4a test error: %s" % (test.error))
            test.logger.dump()
        self.assertTrue(test.error is None)

    def test_4b_block_oversize_EA1_EB1(self):
        if EA1_MAX_SIZE >= INTA_MAX_SIZE:
            self.skipTest("EA1 sending to EB1 may be blocked by EA1 limit and also by INT.A limit. That condition is tested in compound test.")
        test = OversizeMessageTransferTest(MaxMessageSizeBlockOversize.EA1,
                                           MaxMessageSizeBlockOversize.EB1,
                                           "e4b",
                                           message_size=EA1_MAX_SIZE + OVER_UNDER,
                                           expect_block=True)
        test.run()
        if test.error is not None:
            test.logger.log("test_4b test error: %s" % (test.error))
            test.logger.dump()
        self.assertTrue(test.error is None)

    def test_4c_block_oversize_EB1_INTA(self):
        if EB1_MAX_SIZE > INTB_MAX_SIZE:
            self.skipTest("EB1 sending to INT.A may be blocked by EB1 limit and also by INT.B limit. That condition is tested in compound test.")
        test = OversizeMessageTransferTest(MaxMessageSizeBlockOversize.EB1,
                                           MaxMessageSizeBlockOversize.INT_A,
                                           "e4c",
                                           message_size=EB1_MAX_SIZE + OVER_UNDER,
                                           expect_block=True)
        test.run()
        if test.error is not None:
            test.logger.log("test_4c test error: %s" % (test.error))
            test.logger.dump()
        self.assertTrue(test.error is None)

    def test_4d_block_oversize_EB1_INTB(self):
        if EB1_MAX_SIZE > INTB_MAX_SIZE:
            self.skipTest("EB1 sending to INT.B may be blocked by EB1 limit and also by INT.B limit. That condition is tested in compound test.")
        test = OversizeMessageTransferTest(MaxMessageSizeBlockOversize.EB1,
                                           MaxMessageSizeBlockOversize.INT_B,
                                           "e4d",
                                           message_size=EB1_MAX_SIZE + OVER_UNDER,
                                           expect_block=True)
        test.run()
        if test.error is not None:
            test.logger.log("test_4d test error: %s" % (test.error))
            test.logger.dump()
        self.assertTrue(test.error is None)

    def test_4e_block_oversize_EB1_EA1(self):
        if EB1_MAX_SIZE > INTB_MAX_SIZE:
            self.skipTest("EB1 sending to EA1 may be blocked by EB1 limit and also by INT.B limit. That condition is tested in compound test.")
        test = OversizeMessageTransferTest(MaxMessageSizeBlockOversize.EB1,
                                           MaxMessageSizeBlockOversize.EA1,
                                           "e4e",
                                           message_size=EB1_MAX_SIZE + OVER_UNDER,
                                           expect_block=True)
        test.run()
        if test.error is not None:
            test.logger.log("test_4e test error: %s" % (test.error))
            test.logger.dump()
        self.assertTrue(test.error is None)

    def test_4f_block_oversize_EB1_EB1(self):
        test = OversizeMessageTransferTest(MaxMessageSizeBlockOversize.EB1,
                                           MaxMessageSizeBlockOversize.EB1,
                                           "e4f",
                                           message_size=EB1_MAX_SIZE + OVER_UNDER,
                                           expect_block=True)
        test.run()
        if test.error is not None:
            test.logger.log("test_4f test error: %s" % (test.error))
            test.logger.dump()
        self.assertTrue(test.error is None)

    #
    # tests under maxMessageSize should not block
    #
    def test_50_allow_undersize_INTA_INTA(self):
        test = OversizeMessageTransferTest(MaxMessageSizeBlockOversize.INT_A,
                                           MaxMessageSizeBlockOversize.INT_A,
                                           "e50",
                                           message_size=INTA_MAX_SIZE - OVER_UNDER,
                                           expect_block=False)
        test.run()
        if test.error is not None:
            test.logger.log("test_50 test error: %s" % (test.error))
            test.logger.dump()
        self.assertTrue(test.error is None)

    def test_51_allow_undersize_INTA_INTB(self):
        test = OversizeMessageTransferTest(MaxMessageSizeBlockOversize.INT_A,
                                           MaxMessageSizeBlockOversize.INT_B,
                                           "e51",
                                           message_size=INTA_MAX_SIZE - OVER_UNDER,
                                           expect_block=False)
        test.run()
        if test.error is not None:
            test.logger.log("test_51 test error: %s" % (test.error))
            test.logger.dump()
        self.assertTrue(test.error is None)

    def test_52_allow_undersize_INTA_EA1(self):
        test = OversizeMessageTransferTest(MaxMessageSizeBlockOversize.INT_A,
                                           MaxMessageSizeBlockOversize.EA1,
                                           "e52",
                                           message_size=INTA_MAX_SIZE - OVER_UNDER,
                                           expect_block=False)
        test.run()
        if test.error is not None:
            test.logger.log("test_52 test error: %s" % (test.error))
            test.logger.dump()
        self.assertTrue(test.error is None)

    def test_53_allow_undersize_INTA_EB1(self):
        test = OversizeMessageTransferTest(MaxMessageSizeBlockOversize.INT_A,
                                           MaxMessageSizeBlockOversize.EB1,
                                           "e53",
                                           message_size=INTA_MAX_SIZE - OVER_UNDER,
                                           expect_block=False)
        test.run()
        if test.error is not None:
            test.logger.log("test_53 test error: %s" % (test.error))
            test.logger.dump()
        self.assertTrue(test.error is None)

    def test_54_allow_undersize_INTB_INTA(self):
        test = OversizeMessageTransferTest(MaxMessageSizeBlockOversize.INT_B,
                                           MaxMessageSizeBlockOversize.INT_A,
                                           "e54",
                                           message_size=INTB_MAX_SIZE - OVER_UNDER,
                                           expect_block=False)
        test.run()
        if test.error is not None:
            test.logger.log("test_54 test error: %s" % (test.error))
            test.logger.dump()
        self.assertTrue(test.error is None)

    def test_55_allow_undersize_INTB_INTB(self):
        test = OversizeMessageTransferTest(MaxMessageSizeBlockOversize.INT_B,
                                           MaxMessageSizeBlockOversize.INT_B,
                                           "e55",
                                           message_size=INTB_MAX_SIZE - OVER_UNDER,
                                           expect_block=False)
        test.run()
        if test.error is not None:
            test.logger.log("test_55 test error: %s" % (test.error))
            test.logger.dump()
        self.assertTrue(test.error is None)

    def test_56_allow_undersize_INTB_EA1(self):
        test = OversizeMessageTransferTest(MaxMessageSizeBlockOversize.INT_B,
                                           MaxMessageSizeBlockOversize.EA1,
                                           "e56",
                                           message_size=INTB_MAX_SIZE - OVER_UNDER,
                                           expect_block=False)
        test.run()
        if test.error is not None:
            test.logger.log("test_56 test error: %s" % (test.error))
            test.logger.dump()
        self.assertTrue(test.error is None)

    def test_57_allow_undersize_INTB_EB1(self):
        test = OversizeMessageTransferTest(MaxMessageSizeBlockOversize.INT_B,
                                           MaxMessageSizeBlockOversize.EB1,
                                           "e57",
                                           message_size=INTB_MAX_SIZE - OVER_UNDER,
                                           expect_block=False)
        test.run()
        if test.error is not None:
            test.logger.log("test_57 test error: %s" % (test.error))
            test.logger.dump()
        self.assertTrue(test.error is None)

    def test_58_allow_undersize_EA1_INTA(self):
        test = OversizeMessageTransferTest(MaxMessageSizeBlockOversize.EA1,
                                           MaxMessageSizeBlockOversize.INT_A,
                                           "e58",
                                           message_size=min(EA1_MAX_SIZE, INTA_MAX_SIZE) - OVER_UNDER,
                                           expect_block=False)
        test.run()
        if test.error is not None:
            test.logger.log("test_58 test error: %s" % (test.error))
            test.logger.dump()
        self.assertTrue(test.error is None)

    def test_59_allow_undersize_EA1_INTB(self):
        test = OversizeMessageTransferTest(MaxMessageSizeBlockOversize.EA1,
                                           MaxMessageSizeBlockOversize.INT_B,
                                           "e59",
                                           message_size=min(EA1_MAX_SIZE, INTA_MAX_SIZE) - OVER_UNDER,
                                           expect_block=False)
        test.run()
        if test.error is not None:
            test.logger.log("test_59 test error: %s" % (test.error))
            test.logger.dump()
        self.assertTrue(test.error is None)

    def test_5a_allow_undersize_EA1_EA1(self):
        test = OversizeMessageTransferTest(MaxMessageSizeBlockOversize.EA1,
                                           MaxMessageSizeBlockOversize.EA1,
                                           "e5a",
                                           message_size=min(EA1_MAX_SIZE, INTA_MAX_SIZE) - OVER_UNDER,
                                           expect_block=False)
        test.run()
        if test.error is not None:
            test.logger.log("test_5a test error: %s" % (test.error))
            test.logger.dump()
        self.assertTrue(test.error is None)

    def test_5b_allow_undersize_EA1_EB1(self):
        test = OversizeMessageTransferTest(MaxMessageSizeBlockOversize.EA1,
                                           MaxMessageSizeBlockOversize.EB1,
                                           "e5b",
                                           message_size=min(EA1_MAX_SIZE, INTA_MAX_SIZE) - OVER_UNDER,
                                           expect_block=False)
        test.run()
        if test.error is not None:
            test.logger.log("test_5b test error: %s" % (test.error))
            test.logger.dump()
        self.assertTrue(test.error is None)

    def test_5c_allow_undersize_EB1_INTA(self):
        test = OversizeMessageTransferTest(MaxMessageSizeBlockOversize.EB1,
                                           MaxMessageSizeBlockOversize.INT_A,
                                           "e5c",
                                           message_size=min(EB1_MAX_SIZE, INTB_MAX_SIZE) - OVER_UNDER,
                                           expect_block=False)
        test.run()
        if test.error is not None:
            test.logger.log("test_5c test error: %s" % (test.error))
            test.logger.dump()
        self.assertTrue(test.error is None)

    def test_5d_allow_undersize_EB1_INTB(self):
        test = OversizeMessageTransferTest(MaxMessageSizeBlockOversize.EB1,
                                           MaxMessageSizeBlockOversize.INT_B,
                                           "e5d",
                                           message_size=min(EB1_MAX_SIZE, INTB_MAX_SIZE) - OVER_UNDER,
                                           expect_block=False)
        test.run()
        if test.error is not None:
            test.logger.log("test_5d test error: %s" % (test.error))
            test.logger.dump()
        self.assertTrue(test.error is None)

    def test_5e_allow_undersize_EB1_EA1(self):
        test = OversizeMessageTransferTest(MaxMessageSizeBlockOversize.EB1,
                                           MaxMessageSizeBlockOversize.EA1,
                                           "e5e",
                                           message_size=min(EB1_MAX_SIZE, INTB_MAX_SIZE) - OVER_UNDER,
                                           expect_block=False)
        test.run()
        if test.error is not None:
            test.logger.log("test_5e test error: %s" % (test.error))
            test.logger.dump()
        self.assertTrue(test.error is None)

    def test_5f_allow_undersize_EB1_EB1(self):
        test = OversizeMessageTransferTest(MaxMessageSizeBlockOversize.EB1,
                                           MaxMessageSizeBlockOversize.EB1,
                                           "e5f",
                                           message_size=min(EB1_MAX_SIZE, INTB_MAX_SIZE) - OVER_UNDER,
                                           expect_block=False)
        test.run()
        if test.error is not None:
            test.logger.log("test_5f test error: %s" % (test.error))
            test.logger.dump()
        self.assertTrue(test.error is None)

    def test_s32_allow_gt_signed_32bit_max(self):
        test = OversizeMessageTransferTest(MaxMessageSizeBlockOversize.S32,
                                           MaxMessageSizeBlockOversize.S32,
                                           "s32",
                                           message_size=200,
                                           expect_block=False)
        test.run()
        if test.error is not None:
            test.logger.log("test_s32 test error: %s" % (test.error))
            test.logger.dump()
        self.assertTrue(test.error is None)


if __name__ == '__main__':
    unittest.main(main_module())

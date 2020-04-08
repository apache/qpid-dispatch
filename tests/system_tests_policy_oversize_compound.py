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

import unittest as unittest
import os, json, re, signal
import sys
import time

from system_test import TestCase, Qdrouterd, main_module, Process, TIMEOUT, DIR, QdManager, Logger
from subprocess import PIPE, STDOUT
from proton import ConnectionException, Timeout, Url, symbol, Message
from proton.handlers import MessagingHandler
from proton.reactor import Container, ReceiverOption
from proton.utils import BlockingConnection, LinkDetached, SyncRequestResponse
from qpid_dispatch_internal.policy.policy_util import is_ipv6_enabled
from qpid_dispatch_internal.compat import dict_iteritems
from test_broker import FakeBroker

# How many worker threads?
W_THREADS = 2

class Timeout(object):
    def __init__(self, parent):
        self.parent = parent

    def on_timer_task(self, event):
        self.parent.timeout()


# DISPATCH-975 Detect that an oversize message is blocked.
# These tests check compound blocking where the the sender is blocked by
# the ingress edge routers and/or by the uplink interior router.

class OversizeMessageTransferTest(MessagingHandler):
    """
    This test connects a sender and a receiver. Then it tries to send _count_
    number of messages of the given size through the router or router network.

    Messages are to pass through an edge router and get blocked by an interior
    or messages are to be blocked by both the edge and the interior.

    When 'blocked_by_both' is false then:

    * The ingress router should allow the sender's oversize message.
    * The message is blocked by the uplink router by rejecting the message
    and closing the connection between the interior and edge routers.
    * The receiver may receive aborted message indications but that is
    not guaranteed.
    * If any aborted messages are received then the count must be at most one.

    When 'blocked_by_both' is true then:
    * The ingress edge router will reject and close the connection on the first message
    * The second message may be aborted because the connection between the
    edge router and the interior router was closed
    * The remainder of the messages are going into a closed connection and
    will receive no settlement.
    """
    def __init__(self, sender_host, receiver_host, test_address,
                 message_size=100000, count=10, blocked_by_both=False, print_to_console=False):
        super(OversizeMessageTransferTest, self).__init__()
        self.sender_host = sender_host
        self.receiver_host = receiver_host
        self.test_address = test_address
        self.msg_size = message_size
        self.count = count
        self.blocked_by_both = blocked_by_both
        self.expect_block = True
        self.messages = []

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
        self.n_modified = 0
        self.n_released = 0
        self.n_send_settled = 0
        self.n_aborted = 0
        self.n_connection_error = 0

        self.logger = Logger(title=("OversizeMessageTransferTest - %s" % (self.test_address)), print_to_console=print_to_console)
        self.log_unhandled = False # verbose diagnostics of proton callbacks

    def timeout(self):
        current = ("check_done: sent=%d rcvd=%d rejected=%d aborted=%d connection_error:%d send_settled:%d" %
                   (self.n_sent, self.n_rcvd, self.n_rejected, self.n_aborted, self.n_connection_error, self.n_send_settled))
        self.error = "Timeout Expired " + current
        self.logger.log("self.timeout " + self.error)
        self._shut_down_test()

    def on_start(self, event):
        self.logger.log("on_start")
        self.logger.log("on_start: generating messages")
        for idx in range(self.count):
            # construct message in indentifiable chunks
            body_msg = ""
            padchar = "abcdefghijklmnopqrstuvwxyz@#$%"[idx % 30]
            while len(body_msg) < self.msg_size:
                chunk = "[%s:%d:%d" % (self.test_address, idx, len(body_msg))
                padlen = 50 - len(chunk)
                chunk += padchar * padlen
                body_msg += chunk
            if len(body_msg) > self.msg_size:
                body_msg = body_msg[:self.msg_size]
            m = Message(body=body_msg)
            self.messages.append(m)
        self.logger.log("on_start: secheduling reactor timeout")
        self.timer = event.reactor.schedule(10, Timeout(self))
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
            m = self.messages[self.n_sent]
            self.logger.log("send. address:%s message:%d of %s length=%d" % (
                            self.test_address, self.n_sent, self.count, self.msg_size))
            self.sender.send(m)
            self.n_sent += 1
        #if self.n_sent == self.count:
        #    self.log_unhandled = True

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

    def on_connection_error(self, event):
        if event.connection == self.sender_conn:
            if event.connection.remote_condition.name == "amqp:connection:message-size-exceeded":
                self.logger.log("on_connection_error: sender closed with correct condition")
                self.n_connection_error += 1
                self.sender_conn.close()
                self.sender_conn = None
            else:
                # sender closed but for wrong reason
                self.error = "sender close error: Expected amqp:connection:message-size-exceeded but received %s" % \
                             event.connection.remote_condition.name
                self.logger.log(self.error)
        else:
            # connection error but not for sender
            self.error = "unexpected connection close error: wrong connection closed. condition= %s" % \
                         event.connection.remote_condition.name
            self.logger.log(self.error)
        self._check_done()

    def _shut_down_test(self):
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
        current = ("check_done: sent=%d rcvd=%d rejected=%d aborted=%d connection_error:%d send_settled:%d" %
                   (self.n_sent, self.n_rcvd, self.n_rejected, self.n_aborted, self.n_connection_error, self.n_send_settled))
        self.logger.log(current)
        if self.error is not None:
            self.logger.log("TEST FAIL")
            self._shut_down_test()
        else:
            if not self.blocked_by_both:
                # Blocked by interior only. Connection to edge stays up
                # and all messages must be accounted for.
                done = self.n_rejected == 1 and \
                       self.n_send_settled == self.count
            else:
                # Blocked by interior and edge. Expect edge connection to go down
                # and some of our messaages arrive at edge after it has sent
                # AMQP close. Those messages are never settled. TODO: Is that OK?
                done = self.n_rejected == 1 and \
                       self.n_connection_error == 1
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

    def on_settled(self, event):
        self.logger.log("on_settled")
        if event.connection == self.sender_conn:
            self.logger.log("on_settled: sender connection")
            self.n_send_settled += 1
        self._check_done()

    def on_error(self, event):
        self.error = "Container error"
        self.logger.log(self.error)
        self.sender_conn.close()
        self.receiver_conn.close()
        self.timer.cancel()

    def on_link_error(self, event):
        self.error = event.link.remote_condition.name
        self.logger.log("on_link_error: %s" % (self.error))
        # Link errors may prevent normal test shutdown so don't even try.
        raise Exception(self.error)

    def on_reactor_final(self, event):
        self.logger.log("on_reactor_final: I'd expect the Container.run function to return real soon now")

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
    """
    verify that maxMessageSize blocks oversize messages
    """
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
                ('vhost', {'hostname': '$default', 'allowUnknownUser': 'true',
                    'groups': [(
                        '$default', {
                            'users': '*',
                            'maxConnections': 100,
                            'remoteHosts': '*',
                            'sources': '*',
                            'targets': '*',
                            'allowAnonymousSender': 'true',
                            'allowWaypointLinks': 'true',
                            'allowDynamicSource': 'true'
                        }
                    )]}
                )
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

        cls.INT_A.wait_router_connected('INT.B')
        cls.INT_B.wait_router_connected('INT.A')
        cls.EA1.wait_connectors()
        cls.EB1.wait_connectors()

    def run_qdmanage(self, cmd, input=None, expect=Process.EXIT_OK, address=None):
        p = self.popen(
            ['qdmanage'] +
            cmd.split(' ') +
            ['--bus',
             address or self.address(),
             '--indent=-1', '--timeout', str(TIMEOUT)],
            stdin=PIPE, stdout=PIPE, stderr=STDOUT, expect=expect,
            universal_newlines=True)
        out = p.communicate(input)[0]
        try:
            p.teardown()
        except Exception as e:
            raise Exception("%s\n%s" % (e, out))
        return out

    def sense_n_closed_lines(self, routername):
        """
        Read a router's log file and count how many size-exceeded lines are in it.
        :param routername:
        :return: (int, int) tuple with counts of lines in and lines out
        """
        with  open("../setUpClass/%s.log" % routername, 'r') as router_log:
            log_lines = router_log.read().split("\n")
        i_closed_lines = [s for s in log_lines if "amqp:connection:message-size-exceeded" in s and "<-" in s]
        o_closed_lines = [s for s in log_lines if "amqp:connection:message-size-exceeded" in s and "->" in s]
        return (len(i_closed_lines), len(o_closed_lines))

    # verify that a message can go through an edge EB1 and get blocked by interior INT.B
    #
    #  +-------+    +---------+    +---------+    +-------+
    #  |  EA1  |<==>|  INT.A  |<==>|  INT.B  |<==>|  EB1  |
    #  | 50,000|    | 100,000 |    | 150,000 |    |200,000|
    #  +-------+    +---------+    +---------+    +-------+
    #                                    |             ^
    #                                    V             |
    #                               +--------+    +-------+
    #                               |receiver|    |sender |
    #                               |        |    |199,800|
    #                               +--------+    +-------+
    #
    def test_60_block_oversize_EB1_INTB_at_INTB(self):
        ibefore, obefore = self.sense_n_closed_lines("EB1")
        test = OversizeMessageTransferTest(MaxMessageSizeBlockOversize.EB1,
                                           MaxMessageSizeBlockOversize.INT_B,
                                           "e60",
                                           message_size=EB1_MAX_SIZE - OVER_UNDER,
                                           print_to_console=False)
        test.run()
        if test.error is not None:
            test.logger.log("test_60 test error: %s" % (test.error))
            test.logger.dump()
        self.assertTrue(test.error is None)

        # Verify that interrouter link was shut down
        iafter, oafter = self.sense_n_closed_lines("EB1")
        idelta = iafter - ibefore
        odelta = oafter - obefore
        success = odelta == 0 and idelta == 1
        if (not success):
            test.logger.log("FAIL: N closed events in log file did not increment by 1. oBefore: %d, oAfter: %d, iBefore:%d, iAfter:%d" %
                            (obefore, oafter, ibefore, iafter))
            test.logger.dump()
            self.assertTrue(success), "Expected router to generate close with condition: message size exceeded"

    # verify that a message can go through an edge EB1 and get blocked by interior INT.B
    #
    #  +-------+    +---------+    +---------+    +-------+
    #  |  EA1  |<==>|  INT.A  |<==>|  INT.B  |<==>|  EB1  |
    #  | 50,000|    | 100,000 |    | 150,000 |    |200,000|
    #  +-------+    +---------+    +---------+    +-------+
    #      |                                           ^
    #      V                                           |
    #   +--------+                                +-------+
    #   |receiver|                                |sender |
    #   |        |                                |199,800|
    #   +--------+                                +-------+
    #
    def test_61_block_oversize_EB1_EA1_at_INTB(self):
        ibefore, obefore = self.sense_n_closed_lines("EB1")
        test = OversizeMessageTransferTest(MaxMessageSizeBlockOversize.EB1,
                                           MaxMessageSizeBlockOversize.EA1,
                                           "e61",
                                           message_size=EB1_MAX_SIZE - OVER_UNDER,
                                           print_to_console=False)
        test.run()
        if test.error is not None:
            test.logger.log("test_61 test error: %s" % (test.error))
            test.logger.dump()
        self.assertTrue(test.error is None)

        # Verify that interrouter link was shut down
        iafter, oafter = self.sense_n_closed_lines("EB1")
        idelta = iafter - ibefore
        odelta = oafter - obefore
        success = odelta == 0 and idelta == 1
        if (not success):
            test.logger.log("FAIL: N closed events in log file did not increment by 1. oBefore: %d, oAfter: %d, iBefore:%d, iAfter:%d" %
                            (obefore, oafter, ibefore, iafter))
            test.logger.dump()
            self.assertTrue(success), "Expected router to generate close with condition: message size exceeded"

    # see what happens when a message must be blocked by edge and also by interior
    #
    #  +-------+    +---------+    +---------+    +-------+
    #  |  EA1  |<==>|  INT.A  |<==>|  INT.B  |<==>|  EB1  |
    #  | 50,000|    | 100,000 |    | 150,000 |    |200,000|
    #  +-------+    +---------+    +---------+    +-------+
    #                                    |             ^
    #                                    V             |
    #                               +--------+    +-------+
    #                               |receiver|    |sender |
    #                               |        |    |200,200|
    #                               +--------+    +-------+
    #
    def test_70_block_oversize_EB1_INTB_at_both(self):
        ibefore, obefore = self.sense_n_closed_lines("EB1")
        test = OversizeMessageTransferTest(MaxMessageSizeBlockOversize.EB1,
                                           MaxMessageSizeBlockOversize.INT_B,
                                           "e70",
                                           message_size=EB1_MAX_SIZE + OVER_UNDER,
                                           blocked_by_both=True,
                                           print_to_console=False)
        test.run()
        if test.error is not None:
            test.logger.log("test_70 test error: %s" % (test.error))
            test.logger.dump()
        self.assertTrue(test.error is None)

        time.sleep(0.2) # let routers clean up resources that otherwise appear leaked

        # Verify that interrouter link was shut down
        # EB1 must close connection to sender (odelta == 1) but
        # INT.B may or may not close the edge-interior link. Sometimes EB1 senses the
        # oversize condition before it has forwarded too many bytes of the first message
        # to INT.B. Then EB1 aborts the first message to INT.B and INT.B never
        # detects an oversize condition.
        iafter, oafter = self.sense_n_closed_lines("EB1")
        idelta = iafter - ibefore
        odelta = oafter - obefore
        success = odelta == 1 and (idelta == 0 or idelta == 1)
        if (not success):
            test.logger.log("FAIL: N closed events in log file did not increment by 1. oBefore: %d, oAfter: %d, iBefore:%d, iAfter:%d" %
                            (obefore, oafter, ibefore, iafter))
            test.logger.dump()
            self.assertTrue(success), "Expected router to generate close with condition: message size exceeded"

        #if (not closed_after == (closed_before + 2)):
        #    print("FAIL: N closed events in log file did not increment by 2. Before: %d, After: %d" % (closed_before, closed_after))
        #    sys.stdout.flush()
        #    self.assertTrue(closed_after == (closed_before + 2), "Expected to receive and also to send close-with-condition: message size exceeded")


if __name__ == '__main__':
    unittest.main(main_module())

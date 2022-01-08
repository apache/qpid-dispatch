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
import sys
import time
from subprocess import PIPE, STDOUT

from proton import Message
from proton.handlers import MessagingHandler
from proton.reactor import Container

from system_test import TestCase, Qdrouterd, main_module, Process, TIMEOUT, Logger, TestTimeout
from test_broker import FakeBroker

# How many worker threads?
W_THREADS = 2

# Define oversize denial condition
OVERSIZE_CONDITION_NAME = "amqp:connection:forced"
OVERSIZE_CONDITION_DESC = "Message size exceeded"

OVERSIZE_LINK_CONDITION_NAME = "amqp:link:message-size-exceeded"

# For the next test case define max sizes for each router.
# These are the configured maxMessageSize values
EA1_MAX_SIZE = 50000
INTA_MAX_SIZE = 100000
INTB_MAX_SIZE = 150000
EB1_MAX_SIZE = 200000

# Interior routers check for max message size at message ingress over all connections
#   except interrouter connections.
# Edge routers check for max message size at message ingress over all connections
#   except interior router connections.
# Edge routers may check a max message size and allow the message to be delivered
#   to destinations on that edge router. However, if the message is passed to an
#   interior router then the message is subject to the interior router's max size
#   before the message is forwarded by the interior router network.

# The bytes-over and bytes-under max that should trigger allow or deny.
# Messages with content this much over should be blocked while
# messages with content this much under should be allowed.
# * client overhead is typically 16 bytes or so
# * interrouter overhead is much larger with annotations
OVER_UNDER = 200

# Alert:
# This module has two large classes that are laid out about the same:
#    OversizeMessageTransferTest
#    OversizeMulticastTransferTest
# The MessageTransfer test does a single sender and single receiver while
# the MulticastTransfer test does a single sender and four receivers.
# Much of the logic between tests is duplicated. Remember to fix things in both tests.


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

    def __init__(self, test_class, sender_host, receiver_host, test_address,
                 message_size=100000, count=10, blocked_by_both=False, print_to_console=False):
        """
        Construct an instance of the unicast test
        :param test_class:    test class - has wait-connection function
        :param sender_host:   router for sender connection
        :param receiver_host: router for receiver connection or None for link route test
        :param test_address:  sender/receiver AMQP address
        :param message_size:  in bytes
        :param count:         how many messages to send
        :param blocked_by_both:  true if edge router messages are also blocked by interior
        :param print_to_console: print logs as they happen
        """
        super(OversizeMessageTransferTest, self).__init__()
        self.test_class = test_class
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

        self.network_stable = False
        self.n_sent = 0
        self.n_rcvd = 0
        self.n_accepted = 0
        self.n_rejected = 0
        self.n_modified = 0
        self.n_released = 0
        self.n_send_settled = 0
        self.n_aborted = 0
        self.n_connection_error = 0
        self.shut_down = False

        self.logger = Logger(title=("OversizeMessageTransferTest - %s" % (self.test_address)), print_to_console=print_to_console)
        self.log_unhandled = False  # verbose diagnostics of proton callbacks

    def timeout(self):
        current = ("check_done: sent=%d rcvd=%d rejected=%d aborted=%d connection_error:%d send_settled:%d" %
                   (self.n_sent, self.n_rcvd, self.n_rejected, self.n_aborted, self.n_connection_error, self.n_send_settled))
        self.error = "Timeout Expired " + current
        self.logger.log("self.timeout " + self.error)
        self._shut_down_test()

    def on_start(self, event):
        self.logger.log("on_start")

        self.logger.log("on_start: secheduling reactor timeout")
        self.timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))

        self.logger.log("Waiting for router network to stabilize")
        self.test_class.wait_router_network_connected()
        self.network_stable = True

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

        if self.receiver_host is not None:
            self.logger.log("on_start: opening receiver connection to %s" % (self.receiver_host.addresses[0]))
            self.receiver_conn = event.container.connect(self.receiver_host.addresses[0])

            self.logger.log("on_start: Creating receiver")
            self.receiver = event.container.create_receiver(self.receiver_conn, self.test_address)

        self.logger.log("on_start: opening   sender connection to %s" % (self.sender_host.addresses[0]))
        self.sender_conn = event.container.connect(self.sender_host.addresses[0])

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
        # if self.n_sent == self.count:
        #    self.log_unhandled = True

    def on_sendable(self, event):
        if event.sender == self.sender:
            self.logger.log("on_sendable")
            self.send()

    def on_message(self, event):
        self.logger.log("on_message: entry")
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

    def _current(self):
        return ("net_stable=%s sent=%d rcvd=%d rejected=%d aborted=%d connection_error:%d send_settled:%d" %
                (self.network_stable, self.n_sent, self.n_rcvd, self.n_rejected, self.n_aborted, self.n_connection_error, self.n_send_settled))

    def _check_done(self):
        self.logger.log("check_done: " + self._current())
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
        if self.receiver is not None:
            self.receiver_conn.close()
        self.timer.cancel()

    def on_link_error(self, event):
        self.error = event.link.remote_condition.name
        self.logger.log("on_link_error: %s" % (self.error))
        # Link errors may prevent normal test shutdown so don't even try.
        raise Exception(self.error)

    def on_reactor_final(self, event):
        self.logger.log("on_reactor_final:")

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
        time.sleep(0.2)


#
# DISPATCH-975 Detect that an oversize message is blocked.
# These tests check simple and compound blocking for multicast messages.
#
# Indexes into router arrays for receivers and receiver stats
IDX_INTA = 0
IDX_INTB = 1
IDX_EA1 = 2
IDX_EB1 = 3


class OversizeMulticastTransferTest(MessagingHandler):
    """
    This test connects a sender and four receivers. Then it tries to send _count_
    number of messages of the given size through the router or router network.
    """

    def __init__(self, test_class, sender_host, routers, test_address, expect_receives,
                 blocked_by_ingress, blocked_by_interior,
                 message_size=100000, count=10, print_to_console=False):
        """
        Construct an instance of the multicast test
        :param test_class:    test class - has wait-connection function
        :param sender_host:         router for the sender connection
        :param routers:             a list of all the routers for receiver connections
        :param test_address:        sender/receiver AMQP address
        :param expect_receives:     array of expected receive counts
        :param blocked_by_ingress:  true if ingress router blocks
        :param blocked_by_interior: true if edge router messages also blocked by interior
        :param message_size:        in bytes
        :param count:               how many messages to send
        :param print_to_console:    print logs as they happen
        """
        super(OversizeMulticastTransferTest, self).__init__()
        self.test_class = test_class
        self.sender_host = sender_host
        self.routers = routers
        self.test_address = test_address
        self.msg_size = message_size
        self.count = count
        self.expect_receives = expect_receives  # router array
        self.blocked_by_ingress = blocked_by_ingress
        self.blocked_by_interior = blocked_by_interior
        self.messages = []

        self.sender_conn = None
        self.receiver_conns = [None, None, None, None]  # router array
        self.error = None
        self.sender = None
        self.receivers = [None, None, None, None]  # router array
        self.proxy = None

        self.network_stable = False
        self.n_sent = 0
        self.n_rcvds = [0, 0, 0, 0]  # router array
        self.n_accepted = 0
        self.n_rejected = 0
        self.n_modified = 0
        self.n_released = 0
        self.n_send_settled = 0
        self.n_aborteds = [0, 0, 0, 0]  # router array
        self.n_connection_error = 0
        self.shut_down = False

        self.logger = Logger(title=("OversizeMulticastTransferTest - %s" % (self.test_address)), print_to_console=print_to_console)
        self.log_unhandled = False  # verbose diagnostics of proton callbacks

    def timeout(self):
        current = self._current()
        self.error = "Timeout Expired " + current
        self.logger.log("self.timeout " + self.error)
        self._shut_down_test()

    def on_start(self, event):
        self.logger.log("on_start")

        self.logger.log("on_start: secheduling reactor timeout")
        self.timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))

        self.logger.log("Waiting for router network to stabilize")
        self.test_class.wait_router_network_connected()
        self.network_stable = True

        for idx in [IDX_INTA, IDX_INTB, IDX_EA1, IDX_EB1]:
            self.logger.log("on_start: opening receiver connection to %s" % (self.routers[idx].addresses[0]))
            self.receiver_conns[idx] = event.container.connect(self.routers[idx].addresses[0])
        for idx in [IDX_INTA, IDX_INTB, IDX_EA1, IDX_EB1]:
            self.logger.log("on_start: Creating receiver %d" % idx)
            self.receivers[idx] = event.container.create_receiver(self.receiver_conns[idx], self.test_address)

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

        self.logger.log("on_start: opening   sender connection to %s" % (self.sender_host.addresses[0]))
        self.sender_conn = event.container.connect(self.sender_host.addresses[0])

        self.logger.log("on_start: Creating sender")
        self.sender = event.container.create_sender(self.sender_conn, self.test_address)

        self.logger.log("on_start: done")

    def rcvr_idx_of(self, rcvr):
        """
        Given a receiver, as in event.receiver, return
        the router array index of that receiver's router
        :param rcvr:
        :return: integer index of receiver
        """
        for idx in [IDX_INTA, IDX_INTB, IDX_EA1, IDX_EB1]:
            if rcvr == self.receivers[idx]:
                return idx
        self.error = "Receiver not found in receivers array."
        self.logger.log(self.error)
        self.logger.dump()
        self._shut_down_test()
        raise Exception(self.error)

    def send(self):
        while self.sender.credit > 0 and self.n_sent < self.count:
            m = self.messages[self.n_sent]
            self.logger.log("send. address:%s message:%d of %s length=%d" % (
                            self.test_address, self.n_sent, self.count, self.msg_size))
            self.sender.send(m)
            self.n_sent += 1
        # if self.n_sent == self.count:
        #    self.log_unhandled = True

    def on_sendable(self, event):
        if event.sender == self.sender:
            self.logger.log("on_sendable")
            self.send()

    def on_message(self, event):
        self.logger.log("on_message")
        if self.shut_down:
            return
        idx = self.rcvr_idx_of(event.receiver)
        if self.expect_receives[idx] == 0:
            # Receiving any is an error.
            self.error = "Received a message. Expected to receive no messages."
            self.logger.log(self.error)
            self._shut_down_test()
        else:
            self.n_rcvds[idx] += 1
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
        for idx in [IDX_INTA, IDX_INTB, IDX_EA1, IDX_EB1]:
            if self.receivers[idx]:
                self.receivers[idx].close()
                self.receivers[idx] = None
        if self.sender_conn:
            self.sender_conn.close()
            self.sender_conn = None
        for idx in [IDX_INTA, IDX_INTB, IDX_EA1, IDX_EB1]:
            if self.receiver_conns[idx]:
                self.receiver_conns[idx].close()
                self.receiver_conns[idx] = None

    def _current(self):
        return ("net_stable:%s sent=%d rcvd=%s rejected=%d aborted=%s connection_error:%d send_settled:%d" %
                (self.network_stable, self.n_sent, str(self.n_rcvds), self.n_rejected, str(self.n_aborteds), self.n_connection_error, self.n_send_settled))

    def _check_done(self):
        self.logger.log("check_done: " + self._current())
        if self.error is not None:
            self.logger.log("TEST FAIL")
            self._shut_down_test()
        else:
            if self.blocked_by_interior:
                if self.blocked_by_ingress:
                    # Blocked by interior and edge. Expect edge connection to go down
                    # and some of our messaages arrive at edge after it has sent
                    # AMQP close. Those messages are never settled. TODO: Is that OK?
                    done = self.n_rejected == 1 and \
                        self.n_connection_error == 1
                else:
                    # Blocked by interior only. Connection to edge stays up
                    # and all messages must be accounted for.
                    all_received = True
                    for idx in [IDX_INTA, IDX_INTB, IDX_EA1, IDX_EB1]:
                        if self.expect_receives[idx] > 0:
                            if not self.n_rcvds[idx] == self.expect_receives[idx]:
                                all_received = False
                    done = self.n_rejected <= 1 and \
                        self.n_send_settled == self.count and \
                        all_received
            else:
                # Blocked by edge should never deliver to interior
                done = self.n_rejected == 1 and \
                    self.n_connection_error == 1

            if done:
                self.logger.log("TEST DONE!!!")
                # self.log_unhandled = True # verbose debugging
                self._shut_down_test()

    def on_rejected(self, event):
        self.n_rejected += 1
        if self.reject:
            self.logger.log("on_rejected: entry")
            self._check_done()
        else:
            self.error = "Unexpected on_reject"
            self.logger.log(self.error)
            self._check_done()

    def on_aborted(self, event):
        self.logger.log("on_aborted")
        if self.shut_down:
            return
        idx = self.rcvr_idx_of(event.receiver)
        self.n_aborteds[idx] += 1
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
        self._shut_down_test()

    def on_link_error(self, event):
        self.error = event.link.remote_condition.name
        self.logger.log("on_link_error: %s" % (self.error))
        # Link errors may prevent normal test shutdown so don't even try.
        raise Exception(self.error)

    def on_reactor_final(self, event):
        self.logger.log("on_reactor_final:")

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
        time.sleep(0.2)


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
                ('address', {'prefix': 'multicast', 'distribution': 'multicast'}),
                ('vhost', {'hostname': '$default', 'allowUnknownUser': 'true',
                           'groups': {
                               '$default': {
                                   'users': '*',
                                   'maxConnections': 100,
                                   'remoteHosts': '*',
                                   'sources': '*',
                                   'targets': '*',
                                   'allowAnonymousSender': True,
                                   'allowWaypointLinks': True,
                                   'allowDynamicSource': True
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

        cls.wait_router_network_connected()

    @classmethod
    def wait_router_network_connected(cls):
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

    def sense_n_closed_lines(self, router, pattern=OVERSIZE_CONDITION_NAME):
        """
        Read a router's log file and count how many size-exceeded lines are in it.
        :param routername:
        :return: (int, int) tuple with counts of lines in and lines out
        """
        with open(router.logfile_path, 'r') as router_log:
            log_lines = router_log.read().split("\n")
        i_closed_lines = [s for s in log_lines if pattern in s and "<-" in s]
        o_closed_lines = [s for s in log_lines if pattern in s and "->" in s]
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
        ibefore, obefore = self.sense_n_closed_lines(self.EB1)
        test = OversizeMessageTransferTest(MaxMessageSizeBlockOversize,
                                           MaxMessageSizeBlockOversize.EB1,
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
        iafter, oafter = self.sense_n_closed_lines(self.EB1)
        idelta = iafter - ibefore
        odelta = oafter - obefore
        success = odelta == 0 and idelta == 1
        if not success:
            test.logger.log("FAIL: N closed events in log file did not increment by 1. oBefore: %d, oAfter: %d, iBefore:%d, iAfter:%d" %
                            (obefore, oafter, ibefore, iafter))
            test.logger.dump()
            self.assertTrue(success, "Expected router to generate close with condition: message size exceeded")

        # Verfiy that a link was closed with the expected pattern(s)
        ilink1, olink1 = self.sense_n_closed_lines(self.EB1, pattern=OVERSIZE_LINK_CONDITION_NAME)
        success = olink1 > 0
        if not success:
            test.logger.log("FAIL: Did not see link close in log file. oBefore: %d, oAfter: %d, iBefore:%d, iAfter:%d" %
                            (obefore, oafter, ibefore, iafter))
            test.logger.dump()
            self.assertTrue(success, "Expected router to generate link close with condition: amqp:link:message-size-exceeded")

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
        ibefore, obefore = self.sense_n_closed_lines(self.EB1)
        test = OversizeMessageTransferTest(MaxMessageSizeBlockOversize,
                                           MaxMessageSizeBlockOversize.EB1,
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
        iafter, oafter = self.sense_n_closed_lines(self.EB1)
        idelta = iafter - ibefore
        odelta = oafter - obefore
        success = odelta == 0 and idelta == 1
        if not success:
            test.logger.log("FAIL: N closed events in log file did not increment by 1. oBefore: %d, oAfter: %d, iBefore:%d, iAfter:%d" %
                            (obefore, oafter, ibefore, iafter))
            test.logger.dump()
            self.assertTrue(success, "Expected router to generate close with condition: message size exceeded")

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
        ibefore, obefore = self.sense_n_closed_lines(self.EB1)
        test = OversizeMessageTransferTest(MaxMessageSizeBlockOversize,
                                           MaxMessageSizeBlockOversize.EB1,
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

        # Verify that interrouter link was shut down
        # EB1 must close connection to sender (odelta == 1) but
        # INT.B may or may not close the edge-interior link. Sometimes EB1 senses the
        # oversize condition before it has forwarded too many bytes of the first message
        # to INT.B. Then EB1 aborts the first message to INT.B and INT.B never
        # detects an oversize condition.
        iafter, oafter = self.sense_n_closed_lines(self.EB1)
        idelta = iafter - ibefore
        odelta = oafter - obefore
        success = odelta == 1 and idelta in (0, 1)
        if not success:
            test.logger.log("FAIL: N closed events in log file did not increment by 1. oBefore: %d, oAfter: %d, iBefore:%d, iAfter:%d" %
                            (obefore, oafter, ibefore, iafter))
            test.logger.dump()
            self.assertTrue(success, "Expected router to generate close with condition: message size exceeded")

    # Verify that a multicast can go through an edge EB1 and get blocked by interior INT.B
    #
    #  +-------+    +---------+    +---------+    +-------+
    #  | rcvr  |    |  rcvr   |    |  rcvr   |    | rcvr  |
    #  |  no   |    |   no    |    |   no    |    | yes   |
    #  +-------+    +---------+    +---------+    +-------+
    #      ^            ^              ^             ^
    #      |            |              |             |
    #  +-------+    +---------+    +---------+    +-------+
    #  |  EA1  |<==>|  INT.A  |<==>|  INT.B  |<==>|  EB1  |
    #  | 50,000|    | 100,000 |    | 150,000 |    |200,000|
    #  +-------+    +---------+    +---------+    +-------+
    #                                                ^
    #                                                |
    #                                             +-------+
    #                                             |sender |
    #                                             |199,800|
    #                                             +-------+
    #

    def test_80_block_multicast_EB1_INTB_at_INTB(self):
        ibefore, obefore = self.sense_n_closed_lines(self.EB1)
        count = 10
        test = OversizeMulticastTransferTest(MaxMessageSizeBlockOversize,
                                             MaxMessageSizeBlockOversize.EB1,
                                             MaxMessageSizeBlockOversize.routers,
                                             "multicast/e80",
                                             [0, 0, 0, count],
                                             blocked_by_ingress=False,
                                             blocked_by_interior=True,
                                             message_size=EB1_MAX_SIZE - OVER_UNDER,
                                             count=count,
                                             print_to_console=False)
        test.run()
        if test.error is not None:
            test.logger.log("test_80 test error: %s" % (test.error))
            test.logger.dump()
        self.assertTrue(test.error is None)

        # Verify that interrouter link was shut down
        iafter, oafter = self.sense_n_closed_lines(self.EB1)
        idelta = iafter - ibefore
        odelta = oafter - obefore
        success = odelta == 0 and idelta == 1
        if not success:
            test.logger.log("FAIL: N closed events in log file did not increment by 1. oBefore: %d, oAfter: %d, iBefore:%d, iAfter:%d" %
                            (obefore, oafter, ibefore, iafter))
            test.logger.dump()
            self.assertTrue(success, "Expected router to generate close with condition: message size exceeded")

    # Verify that a multicast blocked by edge ingress goes to no receivers
    #
    #  +-------+    +---------+    +---------+    +-------+
    #  | rcvr  |    |  rcvr   |    |  rcvr   |    | rcvr  |
    #  |  no   |    |   no    |    |   no    |    | no   |
    #  +-------+    +---------+    +---------+    +-------+
    #      ^            ^              ^             ^
    #      |            |              |             |
    #  +-------+    +---------+    +---------+    +-------+
    #  |  EA1  |<==>|  INT.A  |<==>|  INT.B  |<==>|  EB1  |
    #  | 50,000|    | 100,000 |    | 150,000 |    |200,000|
    #  +-------+    +---------+    +---------+    +-------+
    #      ^
    #      |
    #  +-------+
    #  |sender |
    #  | 50,200|
    #  +-------+
    #

    def test_81_block_multicast_EA1(self):
        ibefore, obefore = self.sense_n_closed_lines(self.EA1)
        count = 10
        test = OversizeMulticastTransferTest(MaxMessageSizeBlockOversize,
                                             MaxMessageSizeBlockOversize.EA1,
                                             MaxMessageSizeBlockOversize.routers,
                                             "multicast/e81",
                                             [0, 0, 0, 0],
                                             blocked_by_ingress=True,
                                             blocked_by_interior=False,
                                             message_size=EA1_MAX_SIZE + OVER_UNDER,
                                             count=count,
                                             print_to_console=False)
        test.run()
        if test.error is not None:
            test.logger.log("test_81 test error: %s" % (test.error))
            test.logger.dump()
        self.assertTrue(test.error is None)

        # Verify that interrouter link was shut down
        iafter, oafter = self.sense_n_closed_lines(self.EA1)
        idelta = iafter - ibefore
        odelta = oafter - obefore
        success = odelta == 1 and idelta == 0
        if not success:
            test.logger.log("FAIL: N closed events in log file did not increment by 1. oBefore: %d, oAfter: %d, iBefore:%d, iAfter:%d" %
                            (obefore, oafter, ibefore, iafter))
            test.logger.dump()
            self.assertTrue(success, "Expected router to generate close with condition: message size exceeded")

    # Verify that a multicast blocked by interior ingress goes to no receivers
    #
    #  +-------+    +---------+    +---------+    +-------+
    #  | rcvr  |    |  rcvr   |    |  rcvr   |    | rcvr  |
    #  |  no   |    |   no    |    |   no    |    | no    |
    #  +-------+    +---------+    +---------+    +-------+
    #      ^            ^              ^             ^
    #      |            |              |             |
    #  +-------+    +---------+    +---------+    +-------+
    #  |  EA1  |<==>|  INT.A  |<==>|  INT.B  |<==>|  EB1  |
    #  | 50,000|    | 100,000 |    | 150,000 |    |200,000|
    #  +-------+    +---------+    +---------+    +-------+
    #                   ^
    #                   |
    #               +-------+
    #               |sender |
    #               |100,200|
    #               +-------+
    #

    def test_82_block_multicast_INTA(self):
        ibefore, obefore = self.sense_n_closed_lines(self.INT_A)
        count = 10
        test = OversizeMulticastTransferTest(MaxMessageSizeBlockOversize,
                                             MaxMessageSizeBlockOversize.INT_A,
                                             MaxMessageSizeBlockOversize.routers,
                                             "multicast/e82",
                                             [0, 0, 0, 0],
                                             blocked_by_ingress=True,
                                             blocked_by_interior=False,
                                             message_size=INTA_MAX_SIZE + OVER_UNDER,
                                             count=count,
                                             print_to_console=False)
        test.run()
        if test.error is not None:
            test.logger.log("test_82 test error: %s" % (test.error))
            test.logger.dump()
        self.assertTrue(test.error is None)

        # Verify that interrouter link was shut down
        iafter, oafter = self.sense_n_closed_lines(self.INT_A)
        idelta = iafter - ibefore
        odelta = oafter - obefore
        success = odelta == 1 and idelta == 0
        if not success:
            test.logger.log(
                "FAIL: N closed events in log file did not increment by 1. oBefore: %d, oAfter: %d, iBefore:%d, iAfter:%d" %
                (obefore, oafter, ibefore, iafter))
            test.logger.dump()
            self.assertTrue(success, "Expected router to generate close with condition: message size exceeded")

#
# Link route
#


class Dummy(FakeBroker):
    """Open a link and sit there. No traffic is expected to reach this broker"""

    def __init__(self, url, container_id):
        super(Dummy, self).__init__(url, container_id)

    def on_message(self, event):
        print("ERROR did not expect a message")
        sys.stdout.flush()


class MaxMessageSizeLinkRouteOversize(TestCase):
    """verify that maxMessageSize blocks oversize messages over link route"""
    @classmethod
    def setUpClass(cls):
        """Start the router"""
        super(MaxMessageSizeLinkRouteOversize, cls).setUpClass()

        cls.fb_port = cls.tester.get_port()
        cls.logger = Logger(print_to_console=True)

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
                ('address', {'prefix': 'multicast', 'distribution': 'multicast'}),
                ('linkRoute', {'prefix': 'oversize', 'containerId': 'FakeBroker', 'direction': 'in'}),
                ('linkRoute', {'prefix': 'oversize', 'containerId': 'FakeBroker', 'direction': 'out'}),
                ('vhost', {'hostname': '$default', 'allowUnknownUser': 'true',
                           'groups': {
                               '$default': {
                                   'users': '*',
                                   'maxConnections': 100,
                                   'remoteHosts': '*',
                                   'sources': '*',
                                   'targets': '*',
                                   'allowAnonymousSender': True,
                                   'allowWaypointLinks': True,
                                   'allowDynamicSource': True
                               }
                           }
                           })
            ]

            if extra:
                config.extend(extra)
            config = Qdrouterd.Config(config)
            cls.routers.append(cls.tester.qdrouterd(name, config, wait=False))
            return cls.routers[-1]

        # configuration:
        # two edge routers connected via 2 interior routers with max sizes
        #
        #  +-------+    +---------+    +---------+    +-------+
        #  |  EA1  |<==>|  INT.A  |<==>|  INT.B  |<==>|  EB1  |
        #  | 50,000|    | 100,000 |    | 150,000 |    |200,000|
        #  +-------+    +---------+    +---------+    +-------+
        #                                    ^
        #                                    #
        #                                    v
        #                               +--------+
        #                               |  fake  |
        #                               | broker |
        #                               +--------+
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
                              'port': cls.INTB_edge_port}),
                ('connector', {'name': 'FakeBroker',
                               'role': 'route-container',
                               'host': '127.0.0.1',
                               'port': cls.fb_port,
                               'saslMechanisms': 'ANONYMOUS'}),
                ])
        cls.INT_B = cls.routers[1]
        cls.INT_B.listener = cls.INT_B.addresses[0]
        cls.INT_B.fb_port = cls.INT_B.connector_addresses[0]

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

        cls.wait_router_network_connected()

        cls.fake_broker = Dummy("amqp://127.0.0.1:" + str(cls.fb_port),
                                container_id="FakeBroker")
        cls.INT_B.wait_address("oversize",
                               containers=1, count=2)

    @classmethod
    def tearDownClass(cls):
        """Stop the fake broker"""
        cls.fake_broker.join()
        # time.sleep(0.25) # Sleeping a bit here lets INT_B clean up connectors and timers
        super(MaxMessageSizeLinkRouteOversize, cls).tearDownClass()

    @classmethod
    def wait_router_network_connected(cls):
        cls.INT_A.wait_router_connected('INT.B')
        cls.INT_B.wait_router_connected('INT.A')
        cls.EA1.wait_connectors()
        cls.EB1.wait_connectors()

    def sense_n_closed_lines(self, router):
        """
        Read a router's log file and count how many size-exceeded lines are in it.
        :param routername:
        :return: (int, int) tuple with counts of lines in and lines out
        """
        with open(router.logfile_path, 'r') as router_log:
            log_lines = router_log.read().split("\n")
        i_closed_lines = [s for s in log_lines if OVERSIZE_CONDITION_NAME in s and "<-" in s]
        o_closed_lines = [s for s in log_lines if OVERSIZE_CONDITION_NAME in s and "->" in s]
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
    #                               |  fake  |    |sender |
    #                               | broker |    |200,200|
    #                               +--------+    +-------+
    #

    def test_90_block_link_route_EB1_INTB(self):
        ibefore, obefore = self.sense_n_closed_lines(self.EB1)
        test = OversizeMessageTransferTest(MaxMessageSizeLinkRouteOversize,
                                           MaxMessageSizeLinkRouteOversize.EB1,
                                           None,
                                           "oversize.e90",
                                           message_size=EB1_MAX_SIZE + OVER_UNDER,
                                           blocked_by_both=True,
                                           print_to_console=False)
        test.run()
        if test.error is not None:
            test.logger.log("test_90 test error: %s" % (test.error))
            test.logger.dump()
        self.assertTrue(test.error is None)

        # Verify that interrouter link was shut down
        iafter, oafter = self.sense_n_closed_lines(self.EB1)
        idelta = iafter - ibefore
        odelta = oafter - obefore
        success = odelta == 1 and idelta in (0, 1)
        if not success:
            test.logger.log("FAIL: N closed events in log file did not increment by 1. oBefore: %d, oAfter: %d, iBefore:%d, iAfter:%d" %
                            (obefore, oafter, ibefore, iafter))
            test.logger.dump()
            self.assertTrue(success, "Expected router to generate close with condition: message size exceeded")
            MaxMessageSizeLinkRouteOversize.wait_router_network_connected()


if __name__ == '__main__':
    unittest.main(main_module())

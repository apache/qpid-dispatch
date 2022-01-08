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

from time import sleep

from proton import Message
from proton.handlers import MessagingHandler
from proton.reactor import Container

from system_test import TestCase, Qdrouterd, main_module, TestTimeout, unittest, TIMEOUT
from system_tests_drain_support import DrainMessagesHandler, DrainOneMessageHandler
from system_tests_drain_support import DrainNoMessagesHandler, DrainNoMoreMessagesHandler
from system_tests_drain_support import DrainMessagesMoreHandler


class DrainSupportTest(TestCase):

    @classmethod
    def setUpClass(cls):
        """
        Set up two routers:
          Router 'test-router' is the system under test.
          Router 'broker' acts as a link route sink/source.
        The link route uses prefix 'abc'.
        """
        super(DrainSupportTest, cls).setUpClass()

        test_listener_port   = cls.tester.get_port()
        broker_listener_port = cls.tester.get_port()

        # Configure and start 'broker'
        bname = "broker"
        bconfig = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'Broker'}),
            ('listener', {'role': 'normal',
                          'host': '0.0.0.0', 'port': broker_listener_port, 'linkCapacity': 100, 'saslMechanisms': 'ANONYMOUS'}),
        ])
        cls.broker = cls.tester.qdrouterd(bname, bconfig, wait=True)

        # Configure and start test-router
        name = "test-router"
        config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'QDR'}),

            # Setting the linkCapacity to 10 will allow the sender to send a burst of 10 messages
            ('listener', {'role': 'normal',
                          'host': '0.0.0.0', 'port': test_listener_port,
                          'linkCapacity': 10, 'saslMechanisms': 'ANONYMOUS'}),

            # The DrainMessagesMoreHandler accepts a src/tgt address that may be link-routed.
            # This defines the link route to 'broker' and the 'abc' prefix.
            ('connector', {'name': 'broker1-conn', 'role': 'route-container',
                           'host': '0.0.0.0', 'port': broker_listener_port,
                           'saslMechanisms': 'ANONYMOUS'}),
            ('linkRoute', {'prefix': 'abc', 'direction': 'out', 'connection': 'broker1-conn'}),
            ('linkRoute', {'prefix': 'abc', 'direction': 'in', 'connection': 'broker1-conn'}),
        ])

        cls.router = cls.tester.qdrouterd(name, config, wait=False)
        cls.address = cls.router.addresses[0]

        sleep(4)  # starting router with wait=True hangs. sleep for now

    def test_drain_support_1_all_messages(self):
        drain_support = DrainMessagesHandler(self.address)
        drain_support.run()
        self.assertEqual(drain_support.error, None)

    def test_drain_support_2_one_message(self):
        drain_support = DrainOneMessageHandler(self.address)
        drain_support.run()
        self.assertEqual(drain_support.error, None)

    def test_drain_support_3_no_messages(self):
        drain_support = DrainNoMessagesHandler(self.address)
        drain_support.run()
        self.assertEqual(drain_support.error, None)

    def test_drain_support_4_no_more_messages(self):
        drain_support = DrainNoMoreMessagesHandler(self.address)
        drain_support.run()
        self.assertEqual(drain_support.error, None)

    def test_drain_support_5_drain_then_more_messages_local(self):
        drain_support = DrainMessagesMoreHandler(self.address, "org.apache.dev")
        drain_support.run()
        self.assertEqual(drain_support.error, None)

    def test_drain_support_5_drain_then_more_messages_routed(self):
        drain_support = DrainMessagesMoreHandler(self.address, "abc")
        drain_support.run()
        self.assertEqual(drain_support.error, None)


class ReceiverDropsOffDrainTest(TestCase):

    @classmethod
    def setUpClass(cls):
        super(ReceiverDropsOffDrainTest, cls).setUpClass()
        config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'Broker'}),
            ('listener', {'role': 'normal',
                          'port': cls.tester.get_port(),
                          'saslMechanisms': 'ANONYMOUS'}),
        ])

        cls.router = cls.tester.qdrouterd("A", config, wait=True)
        cls.address = cls.router.addresses[0]

    def test_receiver_drops_off_sender_receives_drain(self):
        test = ReceiverDropsOffSenderDrain(self.address, "examples")
        test.run()
        self.assertIsNone(test.error)


class ReceiverDropsOffSenderDrain(MessagingHandler):

    def __init__(self, address, dest):
        super(ReceiverDropsOffSenderDrain, self).__init__()
        self.sender_conn = None
        self.receiver_conn = None
        self.sender = None
        self.receiver = None
        self.error = None
        self.sender_drained = False
        self.address = address
        self.dest = dest
        self.num_msgs = 0
        self.receiver_closed = False
        self.drained = 0
        self.expected_drained = 249
        self.sender_drained = False
        self.timer = None

        # Second receiver link opened.
        self.sec_recv_link_opened = False

    def timeout(self):
        if not self.error:
            self.error = "Timeout Expired: Sender was not drained. Expected " \
                         "drained=%s, actual drain=%s " % \
                         (self.expected_drained, self.drained)
        self.sender_conn.close()
        self.receiver_conn.close()

    def on_start(self, event):
        # Create sender and receiver in two separate connections
        self.timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.sender_conn = event.container.connect(self.address)
        self.receiver_conn = event.container.connect(self.address)
        self.sender = event.container.create_sender(self.sender_conn,
                                                    self.dest)
        self.receiver = event.container.create_receiver(self.receiver_conn,
                                                        self.dest)

    def on_sendable(self, event):
        # Send just one message for now
        if self.num_msgs < 1:
            self.num_msgs += 1
            msg = Message(body={'number': 1})
            self.sender.send(msg)

    def on_message(self, event):
        # As soon as the receiver receives the message, close the receiver
        if event.receiver == self.receiver:
            if self.sec_recv_link_opened and self.sender_drained:

                # Make sure this is the same message body that was
                # sent by the newly created receiver
                if event.message.body['number'] == 3:
                    self.receiver.close()
                    self.error = None
                    self.sender_conn.close()
                    self.timer.cancel()
                    self.receiver_conn.close()
            else:
                self.receiver.close()
                self.receiver_closed = True

    def on_link_opened(self, event):
        if self.sender_drained:
            if event.receiver == self.receiver:
                self.sec_recv_link_opened = True

                if self.num_msgs < 3:
                    # Send a message after the sender has been drained
                    # and a new receiver has been created
                    # and make sure that the message has reached the receiver
                    self.num_msgs += 1
                    msg = Message(body={'number': 3})
                    self.sender.send(msg)

    def on_link_closed(self, event):
        if event.receiver == self.receiver:
            # The receiver link is closed. The sender still have a credit
            # of 249. Now send a message. The receiver is already gone at
            # this point. The router will receive this message and see that
            # there are no receivers and it will send a drain to the sender
            # This test will not work without the fix for DISPATCH-1090
            self.num_msgs += 1
            msg = Message(body={'number': 2})
            self.sender.send(msg)

    def on_link_flow(self, event):
        if self.receiver_closed:
            if event.sender:
                self.drained = event.sender.drained()
                if self.drained == self.expected_drained:
                    # The sender has been drained. Now create another receiver
                    # to the same address as the sender
                    # and use the sender to send a message to see
                    # if flow is re-issued by the router to the sender and if
                    # the message reaches this newly created receiver
                    self.sender_drained = True

                    # Create a new receiver
                    self.receiver = event.container.create_receiver(
                        self.receiver_conn,
                        self.dest,
                        name="A")

    def run(self):
        Container(self).run()


if __name__ == '__main__':
    unittest.main(main_module())

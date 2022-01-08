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

from proton.handlers import MessagingHandler
from proton.reactor import Container
from proton import Message, Endpoint
from system_test import main_module, TIMEOUT, TestTimeout
from system_test import unittest


class DrainMessagesHandler(MessagingHandler):

    def __init__(self, address):
        # prefetch is set to zero so that proton does not automatically issue 10 credits.
        super(DrainMessagesHandler, self).__init__(prefetch=0)
        self.conn = None
        self.sender = None
        self.receiver = None
        self.sent_count = 0
        self.received_count = 0
        self.address = address
        self.error = "Unexpected Exit"

    def timeout(self):
        self.error = "Timeout Expired: sent: %d rcvd: %d" % (self.sent_count, self.received_count)
        self.conn.close()

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.conn = event.container.connect(self.address)

        # Create a sender and a receiver. They are both listening on the same address
        self.receiver = event.container.create_receiver(self.conn, "org.apache.dev")
        self.sender = event.container.create_sender(self.conn, "org.apache.dev")
        self.receiver.flow(1)

    def on_link_flow(self, event):
        if event.link.is_sender and event.link.credit \
           and event.link.state & Endpoint.LOCAL_ACTIVE \
           and event.link.state & Endpoint.REMOTE_ACTIVE :
            self.on_sendable(event)

        # The fact that the event.link.credit is 0 means that the receiver will not be receiving any more
        # messages. That along with 10 messages received indicates that the drain worked and we can
        # declare that the test is successful
        if self.received_count == 10 and event.link.credit == 0:
            self.error = None
            self.timer.cancel()
            self.receiver.close()
            self.sender.close()
            self.conn.close()

    def on_sendable(self, event):
        if self.sent_count < 10:
            msg = Message(body="Hello World", properties={'seq': self.sent_count})
            dlv = event.sender.send(msg)
            dlv.settle()
            self.sent_count += 1

    def on_message(self, event):
        if event.receiver == self.receiver:
            if "Hello World" == event.message.body:
                self.received_count += 1

            if self.received_count < 4:
                event.receiver.flow(1)
            elif self.received_count == 4:
                # We are issuing a drain of 20. This means that we will receive all the 10 messages
                # that the sender is sending. The router will also send back a response flow frame with
                # drain=True but I don't have any way of making sure that the response frame reached the
                # receiver
                event.receiver.drain(20)

    def run(self):
        Container(self).run()


class DrainOneMessageHandler(DrainMessagesHandler):

    def __init__(self, address):
        super(DrainOneMessageHandler, self).__init__(address)

    def on_message(self, event):
        if event.receiver == self.receiver:
            if "Hello World" == event.message.body:
                self.received_count += 1

            if self.received_count < 4:
                event.receiver.flow(1)
            elif self.received_count == 4:
                # We are issuing a drain of 1 after we receive the 4th message.
                # This means that going forward, we will receive only one more message.
                event.receiver.drain(1)

            # The fact that the event.link.credit is 0 means that the receiver will not be receiving any more
            # messages. That along with 5 messages received (4 earlier messages and 1 extra message for drain=1)
            # indicates that the drain worked and we can declare that the test is successful
            if self.received_count == 5 and event.link.credit == 0:
                self.error = None
                self.timer.cancel()
                self.receiver.close()
                self.sender.close()
                self.conn.close()


class DrainNoMessagesHandler(MessagingHandler):

    def __init__(self, address):
        # prefetch is set to zero so that proton does not automatically issue 10 credits.
        super(DrainNoMessagesHandler, self).__init__(prefetch=0)
        self.conn = None
        self.sender = None
        self.receiver = None
        self.address = address
        self.error = "Unexpected Exit"

    def timeout(self):
        self.error = "Timeout Expired"
        self.conn.close()

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.conn = event.container.connect(self.address)

        # Create a sender and a receiver. They are both listening on the same address
        self.receiver = event.container.create_receiver(self.conn, "org.apache.dev")
        self.sender = event.container.create_sender(self.conn, "org.apache.dev")
        self.receiver.flow(1)

    def on_sendable(self, event):
        self.receiver.drain(1)

    def on_link_flow(self, event):
        if self.receiver.credit == 0:
            self.error = None
            self.timer.cancel()
            self.conn.close()

    def run(self):
        Container(self).run()


class DrainNoMoreMessagesHandler(MessagingHandler):

    def __init__(self, address):
        # prefetch is set to zero so that proton does not automatically issue 10 credits.
        super(DrainNoMoreMessagesHandler, self).__init__(prefetch=0)
        self.conn = None
        self.sender = None
        self.receiver = None
        self.address = address
        self.sent = 0
        self.rcvd = 0
        self.error = "Unexpected Exit"

    def timeout(self):
        self.error = "Timeout Expired: sent=%d rcvd=%d" % (self.sent, self.rcvd)
        self.conn.close()

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.conn = event.container.connect(self.address)

        # Create a sender and a receiver. They are both listening on the same address
        self.receiver = event.container.create_receiver(self.conn, "org.apache.dev")
        self.sender = event.container.create_sender(self.conn, "org.apache.dev")
        self.receiver.flow(1)

    def on_sendable(self, event):
        if self.sent == 0:
            msg = Message(body="Hello World")
            event.sender.send(msg)
            self.sent += 1

    def on_message(self, event):
        self.rcvd += 1

    def on_settled(self, event):
        self.receiver.drain(1)

    def on_link_flow(self, event):
        if self.receiver.credit == 0:
            self.error = None
            self.timer.cancel()
            self.conn.close()

    def run(self):
        Container(self).run()


class DrainMessagesMoreHandler(MessagingHandler):
    """
    Make sure the clients can send/receive after going through a drain cycle.

      Send phase
    1. Sender sending first 10 messages
    2. Sender paused waiting for drain to finish
    3. Sender is sending second 10 messages
    4. Sender is done.

      Receive phase
    1. Receiver receiving first four messages; At #4 receiver issues drain 4,20
    2. Reciever receives messages 5..10.
       When 10 messages have been received and link credit =0 the drain is done
       Receiver issues 10 credits
    3. Receiver recieves messages 11..20.
    4. Receiver is done

    At issue in DISPATCH-1055 is that the 10 credits issued in Receive step 2
    are never propagated across a link route to the 'broker'.

    This code is instantiated with and without the link route to demonstrate that
    it works properly when the 'test-router' is handling the drain by itself
    and that it fails only on the link route.
    """

    def __init__(self, address, route_name):
        # prefetch is set to zero so that proton does not automatically issue 10 credits.
        super(DrainMessagesMoreHandler, self).__init__(prefetch=0)
        self.conn = None
        self.sender = None
        self.receiver = None
        self.sent_count = 0
        self.received_count = 0
        self.address = address
        self.error = "Unexpected Exit"
        self.send_phase = 1
        self.recv_phase = 1
        self.route_name = route_name
        self.verbose_printing = False

    def show_state(self):
        return str("send_phase:" + str(self.send_phase)
                   + ", sent_count:" + str(self.sent_count)
                   + ", recv_phase:" + str(self.recv_phase)
                   + ", receive_count:" + str(self.received_count)
                   + ", receiver_credit:" + str(self.receiver.credit)
                   + ", sender_credit:" + str(self.sender.credit))

    def printme(self, str):
        if self.verbose_printing:
            print(str + " " + self.show_state())

    def timeout(self):
        self.error = "Timeout Expired: sent: %d rcvd: %d" % (self.sent_count, self.received_count)
        self.conn.close()

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.conn = event.container.connect(self.address)

        # Create a sender and a receiver. They are both listening on the same address
        self.receiver = event.container.create_receiver(self.conn, source=self.route_name)
        self.sender = event.container.create_sender(self.conn, target=self.route_name)
        self.receiver.flow(1)

    def on_link_flow(self, event):
        if event.link.is_sender and event.link.credit \
           and event.link.state & Endpoint.LOCAL_ACTIVE \
           and event.link.state & Endpoint.REMOTE_ACTIVE :
            self.on_sendable(event)

        # The fact that the event.link.credit is 0 means that the receiver will not be receiving any more
        # messages. That along with 10 messages received indicates that the drain worked.
        if self.send_phase == 2 and self.received_count == 10 and event.link.credit == 0:
            self.printme("sender transitions to phase 3 - drain completed, send new flow now")
            self.receiver.flow(10)
            self.send_phase = 3

        if event.link.is_sender and event.link.credit \
           and event.link.state & Endpoint.LOCAL_ACTIVE \
           and event.link.state & Endpoint.REMOTE_ACTIVE :
            self.on_sendable(event)
        self.printme(("sender " if event.link.is_sender else "receiver ") + "exit on_link_flow:")

    def on_sendable(self, event):
        if event.link.is_sender and self.send_phase == 1 and self.sent_count < 10:
            msg = Message(body="Hello World", properties={'seq': self.sent_count})
            dlv = event.sender.send(msg)
            dlv.settle()
            self.sent_count += 1
            if self.sent_count == 10:
                self.printme("sender transitions to phase 2 - wait for drain to finish")
                self.send_phase = 2
        elif event.link.is_sender and self.send_phase == 3 and self.sent_count < 20:
            msg = Message(body="Hello World", properties={'seq': self.sent_count})
            dlv = event.sender.send(msg)
            dlv.settle()
            self.sent_count += 1
            if self.sent_count == 20:
                self.printme("sender transitions to phase 4 - done sending")
                self.send_phase = 4
        self.printme(("sender " if event.link.is_sender else "receiver ") + "exit on_sendable:")

    def on_message(self, event):
        if event.receiver == self.receiver:
            if "Hello World" == event.message.body:
                self.received_count += 1

            if self.recv_phase == 1 and self.received_count < 4:
                event.receiver.flow(1)
            elif self.recv_phase == 1 and self.received_count == 4:
                # We are issuing a drain of 20. This means that we will receive all the 10 messages
                # that the sender is sending. The router will also send back a response flow frame with
                # drain=True but I don't have any way of making sure that the response frame reached the
                # receiver
                self.printme("receiver transitions to phase 2 - sending drain now")
                event.receiver.drain(20)
                self.recv_phase = 2
            elif self.recv_phase == 2 and self.received_count == 10:
                self.printme("receiver transitions to phase 3")
                self.recv_phase = 3
                msg = Message(body="Hello World", properties={'seq': self.sent_count})
                dlv = self.sender.send(msg)
                dlv.settle()
                self.sent_count += 1
            elif self.recv_phase == 3 and self.received_count == 20:
                self.printme("receiver transitions to phase 4 - test is completed successfully")
                self.recv_phase = 4
                self.error = None
                self.timer.cancel()
                self.receiver.close()
                self.sender.close()
                self.conn.close()
            self.printme("exit on_message:")

    def run(self):
        Container(self).run()


if __name__ == '__main__':
    unittest.main(main_module())

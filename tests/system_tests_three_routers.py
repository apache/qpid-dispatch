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

import unittest, os, json
from subprocess import PIPE, STDOUT
from proton import Message, PENDING, ACCEPTED, REJECTED, RELEASED, SSLDomain, SSLUnavailable, Timeout
from system_test import TestCase, Qdrouterd, main_module, DIR, TIMEOUT, Process
from proton.handlers import MessagingHandler
from proton.reactor import Container, AtMostOnce, AtLeastOnce
import time


# PROTON-828:
try:
    from proton import MODIFIED
except ImportError:
    from proton import PN_STATUS_MODIFIED as MODIFIED


class RouterTest(TestCase):

    inter_router_port = None

    @classmethod
    def setUpClass(cls):
        """Start a router and a messenger"""
        super(RouterTest, cls).setUpClass()

        def router(name, connection_1, connection_2=None):
            
            config = [
                ('router', {'mode': 'interior', 'id': 'QDR.%s'%name}),
                
                ('listener', {'port': cls.tester.get_port(), 'stripAnnotations': 'no'}),
                
                ('address', {'prefix': 'closest',   'distribution': 'closest'}),
                ('address', {'prefix': 'spread',    'distribution': 'balanced'}),
                ('address', {'prefix': 'multicast', 'distribution': 'multicast'})
            ]
            config.append(connection_1)
            if None != connection_2:
                config.append(connection_2)
            
            config = Qdrouterd.Config(config)

            cls.routers.append(cls.tester.qdrouterd(name, config, wait=True))

        cls.routers = []
        
        inter_router_port_1 = cls.tester.get_port()
        inter_router_port_2 = cls.tester.get_port()

        #   A <--- B <--- C 
        router('A', ('listener', {'role': 'inter-router', 'port': inter_router_port_1}) )

        router('B', ('listener', {'role': 'inter-router', 'port': inter_router_port_2}),
                    ('connector', {'name': 'connectorToA', 'role': 'inter-router', 'port': inter_router_port_1, 'verifyHostName': 'no'}))

        router('C', ('connector', {'name': 'connectorToB', 'role': 'inter-router', 'port': inter_router_port_2, 'verifyHostName': 'no'}))

        cls.routers[0].wait_router_connected('QDR.C')
        cls.routers[1].wait_router_connected('QDR.B')
        cls.routers[2].wait_router_connected('QDR.A')



    def test_01_targeted_sender(self):
        test = TargetedSenderTest(self.routers[0].addresses[0], self.routers[2].addresses[0])
        test.run()
        self.assertEqual(None, test.error)

#    def test_02_anonymous_sender(self):
#        test = AnonymousSenderTest(self.routers[0].addresses[0], self.routers[2].addresses[0])
#        test.run()
#        self.assertEqual(None, test.error)


class Timeout(object):
    def __init__(self, parent):
        self.parent = parent

    def on_timer_task(self, event):
        self.parent.timeout()


class TargetedSenderTest(MessagingHandler):
    def __init__(self, address1, address2):
        super(TargetedSenderTest, self).__init__(prefetch=0)
        self.address1 = address1
        self.address2 = address2
        self.dest = "closest.Targeted"
        self.error      = None
        self.sender     = None
        self.receiver   = None
        self.n_expected = 10
        self.n_sent     = 0
        self.n_received = 0
        self.n_accepted = 0

    def timeout(self):
        self.error = "Timeout Expired"
        self.conn1.close()
        self.conn2.close()

    def on_start(self, event):
        # receiver <--- A <--- B <---- C <--- sender
        self.timer = event.reactor.schedule(5, Timeout(self))
        self.conn1 = event.container.connect(self.address1)
        self.conn2 = event.container.connect(self.address2)
        self.sender   = event.container.create_sender(self.conn1, self.dest)
        self.receiver = event.container.create_receiver(self.conn2, self.dest)
        self.receiver.flow(self.n_expected)

    def on_sendable(self, event):
        if self.n_sent < self.n_expected:
            msg = Message(body=self.n_sent)
            event.sender.send(msg)
            self.n_sent += 1

    def on_accepted(self, event):
        self.n_accepted += 1

    def on_message(self, event):
        self.n_received += 1
        if self.n_received == self.n_expected:
            self.receiver.close()
            self.conn1.close()
            self.conn2.close()
            self.timer.cancel()

    def run(self):
        Container(self).run()
 
 

class AnonymousSenderTest(MessagingHandler):
    def __init__(self, address1, address2):
        super(AnonymousSenderTest, self).__init__(prefetch=0)
        self.address1 = address1
        self.address2 = address2
        self.dest = "closest.Anonymous"
        self.error      = None
        self.sender     = None
        self.receiver   = None
        self.n_expected = 10
        self.n_sent     = 0
        self.n_received = 0
        self.n_accepted = 0
 
    def timeout(self):
        self.error = "Timeout Expired %d messages received." % self.n_received
        self.conn1.close()
        self.conn2.close()
 
    # The problem with using an anonymous sender in a router 
    # network is that it takes finite time for endpoint information
    # to propagate around the network.  It is possible for me to
    # start sending before my router knows how to route my messages,
    # which will cause them to get released, and my test will hang,
    # doomed to wait eternally for the tenth message to be received.
    # To fix this, we will detect released messages here, and decrement
    # the sent message counter, forcing a resend for each drop.
    # And also pause for a moment, since we know that the network is 
    # not yet ready.
    def on_released(self, event):
        self.n_sent -= 1
        time.sleep(0.1)

    def on_link_opened(self, event):
        if event.receiver:
            # This sender has no destination addr, so we will have to
            # address each message individually.
            # Also -- Create the sender here, when we know that the
            # receiver link has opened, because then we are at least 
            # close to being able to send.  (See comment above.)
            self.sender = event.container.create_sender(self.conn1, None)

    def on_start(self, event):
        self.timer = event.reactor.schedule(5, Timeout(self))
        self.conn1 = event.container.connect(self.address1)
        self.conn2 = event.container.connect(self.address2)
        self.receiver = event.container.create_receiver(self.conn2, self.dest)
        self.receiver.flow(self.n_expected)
 
    def on_sendable(self, event):
        if self.n_sent < self.n_expected:
            # Add the destination addr to each message.
            msg = Message(body=self.n_sent, address=self.dest)
            event.sender.send(msg)
            self.n_sent += 1
 
    def on_accepted(self, event):
        self.n_accepted += 1
 
    def on_message(self, event):
        self.n_received += 1
        if self.n_received == self.n_expected:
            self.receiver.close()
            self.conn1.close()
            self.conn2.close()
            self.timer.cancel()

    def run(self):
        Container(self).run()
 
 
 
if __name__ == '__main__':
    unittest.main(main_module())

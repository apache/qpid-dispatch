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
from proton.reactor import Container, AtMostOnce, AtLeastOnce, DynamicNodeProperties, LinkOption
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
        """Start a router and a sender-listener client"""
        super(RouterTest, cls).setUpClass()

        def router ( name, connection_1, connection_2=None ):

            config = [
                ('router',
                  {'mode' : 'interior',
                   'id'   : 'QDR.%s' % name
                  }
                ),
                ('listener',
                  {'port'             : cls.tester.get_port(),
                   'stripAnnotations' : 'no'
                  }
                ),
                ('address',
                    { 'prefix'       : 'closest',
                      'distribution' : 'closest'
                    }
                ),
            ]
            config.append ( connection_1 )
            if None != connection_2:
                config.append ( connection_2 )

            config = Qdrouterd.Config ( config )

            cls.routers.append ( cls.tester.qdrouterd(name, config, wait=True) )

        cls.routers = []

        inter_router_port_A = cls.tester.get_port()
        inter_router_port_B = cls.tester.get_port()
        port_for_sender     = cls.tester.get_port()


        #   receiver <--- A <--- B <--- C <--- sender
        router ( 'A',
                   ( 'listener',
                       {'role': 'inter-router',
                        'port': inter_router_port_A
                       }
                   )
               )

        router ( 'B',
                   ( 'listener',
                       { 'role': 'inter-router',
                         'port': inter_router_port_B
                       }
                   ),
                   ( 'connector',
                       { 'name': 'connectorToA',
                         'role': 'inter-router',
                         'port': inter_router_port_A,
                         'verifyHostName': 'no'
                       }
                   )
               )

        router ( 'C',
                   ( 'connector',
                       { 'name': 'connectorToB',
                         'role': 'inter-router',
                         'port': inter_router_port_B,
                         'verifyHostName': 'no'
                       }
                   ),
                   ( 'listener',
                       { 'role': 'normal',
                         'port': port_for_sender
                       }
                   )
               )


        cls.router_A = cls.routers[0]
        cls.router_B = cls.routers[1]
        cls.router_C = cls.routers[2]

        #----------------------------------------------
        # Wait until everybody can see everybody,
        # to minimize the time when the network
        # doesn't know how to route my messages.
        #----------------------------------------------
        cls.router_C.wait_router_connected('QDR.B')
        cls.router_B.wait_router_connected('QDR.A')
        cls.router_A.wait_router_connected('QDR.C')

        cls.send_addr = cls.router_C.addresses[1]
        cls.recv_addr = cls.router_A.addresses[0]


    #------------------------------------------------
    # In these tests, first address will be used
    # by the sender, second by the receiver.
    #------------------------------------------------
    def test_01_targeted_sender(self):
        test = TargetedSenderTest ( self.send_addr, self.recv_addr )
        test.run()
        self.assertEqual(None, test.error)

    def test_02_anonymous_sender(self):
        test = AnonymousSenderTest ( self.send_addr, self.recv_addr )
        test.run()
        self.assertEqual(None, test.error)


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
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.conn1 = event.container.connect(self.address1)
        self.conn2 = event.container.connect(self.address2)
        self.sender   = event.container.create_sender(self.conn1, self.dest)
        self.receiver = event.container.create_receiver(self.conn2, self.dest)
        self.receiver.flow(self.n_expected)


    def send(self):
      while self.sender.credit > 0 and self.n_sent < self.n_expected:
        msg = Message(body=self.n_sent)
        self.sender.send(msg)
        self.n_sent += 1

    def on_sendable(self, event):
        if self.n_sent < self.n_expected:
            self.send()

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


class DynamicTarget(LinkOption):
    def apply(self, link):
        link.target.dynamic = True
        link.target.address = None


class AnonymousSenderTest(MessagingHandler):

    def __init__(self, send_addr, recv_addr):
        super(AnonymousSenderTest, self).__init__()
        self.send_addr = send_addr
        self.recv_addr = recv_addr

        self.error     = None
        self.recv_conn = None
        self.send_conn = None
        self.sender    = None
        self.receiver  = None
        self.address   = None

        self.expected   = 10
        self.n_sent     = 0
        self.n_received = 0
        self.n_accepted = 0


    def timeout ( self ):
        self.error = "Timeout Expired: n_sent=%d n_received=%d n_accepted=%d" % (self.n_sent, self.n_received, self.n_accepted)
        self.send_conn.close()
        self.recv_conn.close()


    def on_start(self, event):
        self.timer     = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.send_conn = event.container.connect(self.send_addr)
        self.recv_conn = event.container.connect(self.recv_addr)
        self.sender    = event.container.create_sender(self.send_conn, options=DynamicTarget())


    def send(self):
        while self.sender.credit > 0 and self.n_sent < self.expected:
            self.n_sent += 1
            m = Message(address=self.address, body="Message %d of %d" % (self.n_sent, self.expected))
            self.sender.send(m)


    def on_link_opened(self, event):
        if event.sender == self.sender:
            self.address = self.sender.remote_target.address
            self.receiver = event.container.create_receiver(self.recv_conn, self.address)


    def on_sendable(self, event):
        self.send()


    def on_message(self, event):
        if event.receiver == self.receiver:
            self.n_received += 1


    def on_accepted(self, event):
        self.n_accepted += 1
        if self.n_accepted == self.expected:
            self.send_conn.close()
            self.recv_conn.close()
            self.timer.cancel()


    def run(self):
        Container(self).run()



if __name__ == '__main__':
    unittest.main(main_module())

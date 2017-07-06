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
from subprocess      import PIPE, STDOUT
from proton          import Message, PENDING, ACCEPTED, REJECTED, RELEASED, SSLDomain, SSLUnavailable, Timeout
from system_test     import TestCase, Qdrouterd, main_module, DIR, TIMEOUT, Process
from proton.handlers import MessagingHandler
from proton.reactor  import Container, AtMostOnce, AtLeastOnce, DynamicNodeProperties, LinkOption
from proton.utils    import BlockingConnection
from qpid_dispatch.management.client import Node

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

        def router ( name, more_config ):

            config = [
                ('router',
                  {'mode' : 'interior',
                   'id'   : 'QDR.%s' % name
                  }
                ),
                ('listener',
                  {'port'             : cls.tester.get_port(),
                   'role'             : 'normal',
                   'stripAnnotations' : 'no'
                  }
                ),
                ('address',
                    { 'prefix'       : 'closest',
                      'distribution' : 'closest'
                    }
                ),
            ] + more_config

            config = Qdrouterd.Config ( config )

            cls.routers.append ( cls.tester.qdrouterd(name, config, wait=True) )

        cls.routers = []

        inter_router_port_A = cls.tester.get_port()
        inter_router_port_B = cls.tester.get_port()
        port_for_sender     = cls.tester.get_port()
        A_linkroute_listener_port = cls.tester.get_port()

        cls.linkroute_prefix = '0.0.0.0/link'


        router ( 'A',
                 [
                   ( 'listener',
                       {'role': 'inter-router',
                        'port': inter_router_port_A
                       }
                   ),
                   ( 'listener',
                       { 'name': 'linkroute-listener',
                         'role': 'route-container',
                         'host': '0.0.0.0',
                         'port': A_linkroute_listener_port,
                         'saslMechanisms': 'ANONYMOUS'
                       }
                   ),
                   ( 'linkRoute',
                     { 'prefix': cls.linkroute_prefix,
                       'connection': 'linkroute-listener',
                       'dir': 'in'
                     }
                   ),
                   ( 'linkRoute',
                     { 'prefix': cls.linkroute_prefix,
                       'connection': 'linkroute-listener',
                       'dir': 'out'
                     }
                   )
                 ]
               )

        router ( 'B',
                 [
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
                   ),
                   ( 'linkRoute',
                     { 'prefix': cls.linkroute_prefix,
                       'dir': 'in'
                     }
                   ),
                   ( 'linkRoute',
                     { 'prefix': cls.linkroute_prefix,
                       'dir': 'out'
                     }
                   )
                 ]
               )

        router ( 'C',
                 [
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
                   ),
                   ( 'linkRoute',
                       { 'prefix': cls.linkroute_prefix,
                         'dir': 'in'
                       }
                    ),
                   ( 'linkRoute',
                       { 'prefix': cls.linkroute_prefix,
                         'dir': 'out'
                       }
                   )
                 ]
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

        #------------------------------------------------
        # In these tests, first address will be used
        # by the sender, second by the receiver.
        #
        #   receiver <--- A <--- B <--- C <--- sender
        #
        #------------------------------------------------
        cls.C_normal_addr = cls.router_C.addresses[1]
        cls.send_addr = cls.router_C.addresses[1]
        cls.recv_addr = cls.router_A.addresses[0]
        cls.route_check_address = cls.router_A.addresses[0]
        cls.route_container_addr = cls.router_A.addresses[2]


    def test_01_targeted_sender(self):
        test = TargetedSenderTest ( self.send_addr, self.recv_addr )
        test.run()
        self.assertEqual(None, test.error)


    def test_02_anonymous_sender(self):
        test = AnonymousSenderTest ( self.send_addr, self.recv_addr )
        test.run()
        self.assertEqual(None, test.error)


    def test_03_dynamic_reply_to(self):
        test = DynamicReplyTo ( self.send_addr, self.recv_addr )
        test.run()
        self.assertEqual(None, test.error)


    def test_04_link_route ( self ):
        test = LinkRoute ( self.route_container_addr,
                           self.linkroute_prefix,
                           self.route_check_address,
                           self.C_normal_addr
                         )
        test.run()
        self.assertEqual(None, test.error)



class TargetedSenderTest(MessagingHandler):
    def __init__(self, send_addr, recv_addr):
        super(TargetedSenderTest, self).__init__(prefetch=0)
        self.send_addr = send_addr
        self.recv_addr = recv_addr
        self.dest = "closest.Targeted"
        self.error      = None
        self.sender     = None
        self.receiver   = None
        self.n_expected = 10
        self.n_sent     = 0
        self.n_received = 0
        self.n_accepted = 0

    def timeout(self):
        self.error = "Timeout Expired: n_sent=%d n_received=%d n_accepted=%d" % \
                     (self.n_sent, self.n_received, self.n_accepted)
        self.send_conn.close()
        self.recv_conn.close()

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.send_conn = event.container.connect(self.send_addr)
        self.recv_conn = event.container.connect(self.recv_addr)
        self.sender   = event.container.create_sender(self.send_conn, self.dest)
        self.receiver = event.container.create_receiver(self.recv_conn, self.dest)
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
            self.send_conn.close()
            self.recv_conn.close()
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
        self.error = "Timeout Expired: n_sent=%d n_received=%d n_accepted=%d" % \
                     (self.n_sent, self.n_received, self.n_accepted)
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




#=======================================================================
# In this test we have a separate 'client' and 'server' with separate
# connections.  The client sends requests to the server, and embeds in
# them its desired reply-to address.  The server uses that address to
# send back ambiguous and noncommittal messages.  The tests ends with
# success if the client receives the expected number of replies, or
# with failure if we time out.
#=======================================================================
class DynamicReplyTo(MessagingHandler):
    def __init__(self, client_addr, server_addr):
        super(DynamicReplyTo, self).__init__(prefetch=10)
        self.client_addr        = client_addr
        self.server_addr        = server_addr
        self.dest               = "closest.dynamicRequestResponse"
        self.error              = None
        self.server_receiver    = None
        self.client_receiver    = None
        self.sender             = None
        self.server_sender      = None
        self.n_expected         = 10
        self.n_sent             = 0
        self.received_by_server = 0
        self.received_by_client = 0


    def timeout(self):
        self.error = "Timeout Expired: n_sent=%d received_by_server=%d received_by_client=%d" % \
                     (self.n_sent, self.received_by_server, self.received_by_client)
        self.client_connection.close()
        self.server_connection.close()


    def on_start ( self, event ):
        self.timer             = event.reactor.schedule ( TIMEOUT, Timeout(self) )

        # separate connections to simulate client and server.
        self.client_connection = event.container.connect(self.client_addr)
        self.server_connection = event.container.connect(self.server_addr)

        self.sender            = event.container.create_sender(self.client_connection, self.dest)
        self.server_sender     = event.container.create_sender(self.server_connection, None)

        self.server_receiver   = event.container.create_receiver(self.server_connection, self.dest)
        self.client_receiver   = event.container.create_receiver(self.client_connection, None, dynamic=True)



    def on_sendable(self, event):
        reply_to_addr = self.client_receiver.remote_source.address

        if reply_to_addr == None:
          return

        while event.sender.credit > 0 and self.n_sent < self.n_expected:
            # We send to server, and tell it how to reply to the client.

            request = Message ( body=self.n_sent,
                                address=self.dest,
                                reply_to = reply_to_addr )
            event.sender.send ( request )
            self.n_sent += 1


    def on_message(self, event):
        # Server gets a request and responds to
        # the address that is embedded in the message.
        if event.receiver == self.server_receiver :
            self.server_sender.send ( Message(address=event.message.reply_to,
                                      body="Reply hazy, try again later.") )
            self.received_by_server += 1

        # Client gets a response and counts it.
        elif event.receiver == self.client_receiver :
            self.received_by_client += 1
            if self.received_by_client == self.n_expected:
                self.timer.cancel()
                self.server_receiver.close()
                self.client_receiver.close()
                self.client_connection.close()
                self.server_connection.close()


    def run(self):
        Container(self).run()





class Entity(object):
    def __init__(self, status_code, status_description, attrs):
        self.status_code        = status_code
        self.status_description = status_description
        self.attrs              = attrs

    def __getattr__(self, key):
        return self.attrs[key]




class RouterProxy(object):
    def __init__(self, reply_addr):
        self.reply_addr = reply_addr

    def response(self, msg):
        ap = msg.properties
        return Entity(ap['statusCode'], ap['statusDescription'], msg.body)

    def read_address(self, name):
        ap = {'operation': 'READ', 'type': 'org.apache.qpid.dispatch.router.address', 'name': name}
        return Message(properties=ap, reply_to=self.reply_addr)

    def query_addresses(self):
        ap = {'operation': 'QUERY', 'type': 'org.apache.qpid.dispatch.router.address'}
        return Message(properties=ap, reply_to=self.reply_addr)




class PollTimeout(object):
    def __init__(self, parent):
        self.parent = parent

    def on_timer_task(self, event):
        self.parent.poll_timeout()



class Timeout(object):
    def __init__(self, parent):
        self.parent = parent

    def on_timer_task(self, event):
        self.parent.timeout()


class LinkRoute ( MessagingHandler ):

    """
        Set up and use a link-route, to send a message this way:
            receiver <--- A <--- B <--- C <--- sender

        Check for appearance of the link-route at router A with a
        technique that is appropriate to the 'proactor' style,
        i.e. do not take control away from the proactor for an extended
        period of time.
    """

    def __init__ ( self, route_container, linkroute_prefix, route_check_address, send_address ):
        super(LinkRoute, self).__init__(prefetch=0)
        self.route_container        = route_container
        self.send_address           = send_address
        self.linkroute_prefix       = linkroute_prefix
        self.route_check_address    = route_check_address
        self.true_statement         = "This IS the message you are looking for."
        self.error                  = None
        self.send_connection        = None
        self.recv_connection        = None
        self.route_check_connection = None
        self.route_check_sender     = None
        self.sender                 = None
        self.route_check_timer      = None
        self.route_check_receiver   = None
        self.count                  = 1
        self.sent                   = 0
        self.route_checks_sent      = 0


    def bail ( self, error_text ) :
        self.error = error_text
        self.recv_connection.close()
        self.route_check_connection.close()
        self.send_connection.close()
        self.timer.cancel()
        if self.route_check_timer :
            self.route_check_timer.cancel()
            self.route_check_timer = None


    def timeout(self):
        self.bail ( "Timeout Expired" )


    def poll_timeout ( self ) :
        self.poll()


    def on_start(self, event):
        # Iff this timer expires, the test fails.
        self.timer = event.reactor.schedule ( TIMEOUT, Timeout(self) )

        # At startup, create only the receivers and the sender that checks for
        # the route being ready.  Creation of the payload message sender
        # has to wait until we know the address is available, to avoid a
        # 'no route to destination' error.
        self.recv_connection = event.container.connect ( self.route_container )

        self.route_check_connection  = event.container.connect ( self.route_check_address )
        self.route_check_receiver = event.container.create_receiver ( self.route_check_connection,
                                                                dynamic=True
                                                              )
        self.route_check_sender = event.container.create_sender ( self.route_check_connection,
                                                                  "$management"
                                                                )



    def poll ( self ):
        # The router prepends this 'D' to the address.  If we want
        # to look for it down at router A, we have to do likewise.
        doctored_linkroute_prefix = 'D' + self.linkroute_prefix
        request = self.proxy.read_address ( doctored_linkroute_prefix )
        self.route_check_sender.send ( request )



    def on_link_opened ( self, event ):

        if event.receiver:
            event.receiver.flow ( self.count )

        if event.receiver == self.route_check_receiver:
            self.proxy = RouterProxy ( self.route_check_receiver.remote_source.address )
            self.poll()


    def on_link_opening ( self, event ):
        if event.receiver:
            event.receiver.target.address = self.route_container
            event.receiver.flow(1)


    def on_sendable ( self, event ):
        if event.sender == self.sender and self.sent < self.count :
            self.sender.send ( Message ( address = self.linkroute_prefix, body = self.true_statement ) )
            self.sent += 1


    def on_message ( self, event ):
        # Check that the incoming message is the one we sent.
        if ( event.message.body == self.true_statement ) :
            self.bail ( None )

        if event.receiver == self.route_check_receiver:
            response = self.proxy.response(event.message)
            if response.status_code == 200 and (response.remoteCount + response.containerCount) > 0:
                if self.route_check_timer :
                    self.route_check_timer.cancel()
                    self.route_check_timer = None
                # After address appears at the far end of my router line, (router A)
                # give more time to let it finish propagating.
                time.sleep ( 2 )
                self.send_connection = event.container.connect ( self.send_address )
                self.sender = event.container.create_sender ( self.send_connection, self.linkroute_prefix )
            else:
                # Try again.
                self.route_checks_sent += 1
                if self.route_checks_sent > 10 :
                    self.bail ( "can't find address" )
                else :
                    self.route_check_timer = event.reactor.schedule(0.50, PollTimeout(self))




    def run(self):
        Container(self).run()




if __name__ == '__main__':
    unittest.main(main_module())

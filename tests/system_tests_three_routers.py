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

        def router(name, more_config):


            config = [ ('router', {'mode': 'interior', 'id': name}) ] + more_config

            config = Qdrouterd.Config(config)

            cls.routers.append(cls.tester.qdrouterd(name, config, wait=True))

        cls.routers = []

        inter_router_port_B = cls.tester.get_port()

        A_normal_port          = cls.tester.get_port()
        A_route_container_port = cls.tester.get_port()
        A_inter_router_port    = cls.tester.get_port()

        cls.linkroute_prefix = "0.0.0.0/link"

        router ( 'A',
                 [
                    ( 'listener',
                      { 'port': A_normal_port,
                        'stripAnnotations': 'no'
                      }
                    ),
                    ( 'listener',
                      { 'port': A_route_container_port,
                        'stripAnnotations': 'no',
                        'role': 'route-container'
                      }
                    ),
                   ( 'listener',
                     {  'role': 'inter-router',
                        'port': A_inter_router_port,
                     }
                   ),
                   ( 'linkRoute',
                     { 'prefix': cls.linkroute_prefix,
                       'dir': 'in',
                       'containerId': 'LinkRouteTest'
                     }
                   ),
                   ( 'linkRoute',
                     { 'prefix': cls.linkroute_prefix,
                       'dir': 'out',
                       'containerId': 'LinkRouteTest'
                     }
                   )
                 ]
               )

        B_normal_port       = cls.tester.get_port()
        B_inter_router_port = cls.tester.get_port()

        router ( 'B',
                 [
                    ( 'listener',
                      { 'port': B_normal_port,
                        'stripAnnotations': 'no'
                      }
                    ),
                    ( 'listener',
                      {  'role': 'inter-router',
                         'port': B_inter_router_port
                      }
                    ),
                    ( 'connector',
                      {  'name': 'connectorToA',
                         'role': 'inter-router',
                         'port': A_inter_router_port,
                         'verifyHostName': 'no'
                      }
                   ),
                   ( 'linkRoute',
                     { 'prefix': cls.linkroute_prefix,
                       'dir': 'in',
                       'containerId': 'LinkRouteTest'
                     }
                   ),
                   ( 'linkRoute',
                     { 'prefix': cls.linkroute_prefix,
                       'dir': 'out',
                       'containerId': 'LinkRouteTest'
                     }
                   )
                 ]
               )

        C_normal_port          = cls.tester.get_port()
        C_route_container_port = cls.tester.get_port()

        router ( 'C',
                 [
                    ( 'listener',
                      { 'port': C_normal_port,
                        'stripAnnotations': 'no'
                      }
                    ),
                    ( 'listener',
                      { 'port': C_route_container_port,
                        'stripAnnotations': 'no',
                        'role': 'route-container'
                      }
                    ),
                    ( 'connector',
                      {  'name': 'connectorToB',
                         'role': 'inter-router',
                         'port': B_inter_router_port,
                         'verifyHostName': 'no'
                      }
                   ),
                   ( 'linkRoute',
                     { 'prefix': cls.linkroute_prefix,
                       'dir': 'in',
                       'containerId': 'LinkRouteTest'
                     }
                   ),
                   ( 'linkRoute',
                     { 'prefix': cls.linkroute_prefix,
                       'dir': 'out',
                       'containerId': 'LinkRouteTest'
                     }
                   )
                 ]
               )

        cls.routers[0].wait_router_connected('B')
        cls.routers[0].wait_router_connected('C')
        cls.routers[1].wait_router_connected('A')
        cls.routers[1].wait_router_connected('C')
        cls.routers[2].wait_router_connected('A')
        cls.routers[2].wait_router_connected('B')

        cls.A_normal_addr      = cls.routers[0].addresses[0]
        cls.C_normal_addr      = cls.routers[2].addresses[0]

        cls.sender_addr        = cls.C_normal_addr
        cls.receiver_addr      = cls.A_normal_addr

        # Whatever container connects to this addr
        # will be the route-container for link-attach routing tests..
        cls.C_route_container_addr = cls.routers[2].addresses[1]

    def test_01_targeted_sender ( self ):
        test = TargetedSenderTest ( self.sender_addr, self.receiver_addr )
        test.run()
        self.assertEqual ( None, test.error )


    def test_02_anonymous_sender(self):
        test = AnonymousSenderTest ( self.sender_addr, self.receiver_addr )
        test.run()
        self.assertEqual ( None, test.error )


    def test_03_dynamic_reply_to(self):
        test = DynamicReplyTo ( self.sender_addr, self.receiver_addr )
        test.run()
        self.assertEqual ( None, test.error )

    def test_04_linkroute ( self ):
        test = LinkAttachRouting ( self.A_normal_addr,
                                   self.C_route_container_addr,
                                   self.linkroute_prefix,
                                   "addr_04"
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




class AddressChecker(object):
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




class LinkRouteCheckTimeout(object):
    def __init__(self, parent):
        self.parent = parent

    def on_timer_task(self, event):
        self.parent.linkroute_check_timeout()



class Timeout(object):
    def __init__(self, parent):
        self.parent = parent

    def on_timer_task(self, event):
        self.parent.timeout()



class LinkAttachRouting ( MessagingHandler ):
    """
    There are two hosts: near, and far.  The far host is the one that
    the route container will connect to, and it will receive our messages.
    The near host is what our sender will attach to.
    """
    def __init__ ( self, nearside_host, farside_host, linkroute_prefix, addr_suffix ):
        super ( LinkAttachRouting, self ).__init__(prefetch=0)
        self.nearside_host         = nearside_host
        self.farside_host          = farside_host
        self.linkroute_prefix      = linkroute_prefix
        self.link_routable_address = self.linkroute_prefix + '.' + addr_suffix

        self.nearside_cnx             = None
        self.farside_cnx              = None
        self.error                    = None
        self.nearside_sender          = None
        self.farside_receiver         = None
        self.linkroute_check_timer    = None
        self.linkroute_check_receiver = None
        self.linkroute_check_agent    = None

        self.count     = 10
        self.n_sent    = 0
        self.n_rcvd    = 0
        self.n_settled = 0


    def timeout ( self ):
        self.bail ( "Timeout Expired: n_sent=%d n_rcvd=%d n_settled=%d" %
                    (self.n_sent, self.n_rcvd, self.n_settled) )

    def linkroute_check_timeout(self):
        self.linkroute_check()


    def bail ( self, text ):
        self.error = text
        self.farside_cnx.close()
        self.nearside_cnx.close()
        self.timer.cancel()
        if self.linkroute_check_timer:
            self.linkroute_check_timer.cancel()


    def on_start(self, event):
        self.timer        = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.nearside_cnx = event.container.connect(self.nearside_host)

        # Step 1: I make the far cnx.  Once this is done, if we later attach
        # anywhere with a link whose address matches the link-attach routable
        # prefix, the link-attach route will be formed.
        self.farside_cnx = event.container.connect(self.farside_host)

        # Since the route container will be connected to Farside, and
        # my router network is linear, I make the linkroute checker attach
        # to Nearside.
        self.linkroute_check_receiver = event.container.create_receiver(self.nearside_cnx, dynamic=True)
        self.linkroute_check_agent    = event.container.create_sender(self.nearside_cnx, "$management")


    def on_link_opened(self, event):
        if event.receiver:
            event.receiver.flow(self.count)
        if event.receiver == self.linkroute_check_receiver:
            # Step 2. my linkroute check-link has opened: make the linkroute_checker
            self.linkroute_checker = AddressChecker(self.linkroute_check_receiver.remote_source.address)
            self.linkroute_check()


    def on_message(self, event):
        if event.receiver == self.farside_receiver:
            # This is a payload message.
            self.n_rcvd += 1

        elif event.receiver == self.linkroute_check_receiver:
            # This is one of my route-readiness checking messages.
            response = self.linkroute_checker.response(event.message)
            if response.status_code == 200 and (response.remoteCount + response.containerCount) > 0:
                # Step 3: got confirmation of link-attach knowledge fully propagated
                # to Nearside router.  Now we can make the nearside sender without getting
                # a No Path To Destination error.
                self.nearside_sender = event.container.create_sender(self.nearside_cnx, self.link_routable_address)
                # And we can quit checking.
                if self.linkroute_check_timer:
                    self.linkroute_check_timer.cancel()
                    self.linkroute_check_timer = None
            else:
                # If the latest check did not find the link-attack route ready,
                # schedule another check a little while from now.
                self.linkroute_check_timer = event.reactor.schedule(0.25, LinkRouteCheckTimeout(self))


    def on_link_opening ( self, event ):
        if event.receiver:
            # Step 4.  At start-up, I connected to the route-container listener on
            # Farside, which makes me the route container.  So when a sender attaches
            # to the network and wants to send to the linkroutable address, the router
            # network creates the link-attach route, and then hands me a receiver for it.
            if event.receiver.remote_target.address == self.link_routable_address:
                self.farside_receiver = event.receiver
                event.receiver.target.address = self.link_routable_address
                event.receiver.open()
            else:
                self.bail("Incorrect address on incoming receiver: got %s, expected %s" %
                          (event.receiver.remote_target.address, self.link_routable_address))


    def on_sendable ( self, event ):
        # Step 5: once there is someone on the network who can receive
        # my messages, I get the go-ahead for my sender.
        if event.sender == self.nearside_sender:
            self.send()


    def send ( self ):
        while self.nearside_sender.credit > 0 and self.n_sent < self.count:
            self.n_sent += 1
            m = Message(body="Message %d of %d" % (self.n_sent, self.count))
            self.nearside_sender.send(m)


    def linkroute_check ( self ):
        # BUGALERT: We have to prepend the 'D' to this linkroute prefix
        # because that's what the router does internally.  Someday this
        # may change.
        request = self.linkroute_checker.read_address("D" + self.linkroute_prefix )
        self.linkroute_check_agent.send(request)


    def on_settled(self, event):
        if event.sender == self.nearside_sender:
            self.n_settled += 1
            if self.n_settled == self.count:
                self.bail ( None )


    def run(self):
        container = Container(self)
        container.container_id = 'LinkRouteTest'
        container.run()


if __name__ == '__main__':
    unittest.main(main_module())

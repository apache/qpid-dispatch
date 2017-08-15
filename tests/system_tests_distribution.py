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




#------------------------------------------------
# Helper classes for all tests.
#------------------------------------------------

class Timeout(object):
    def __init__(self, parent):
        self.parent = parent

    def on_timer_task(self, event):
        self.parent.timeout()



class AddressCheckResponse(object):
    """
    Convenience class for the responses returned by an AddressChecker.
    """
    def __init__(self, status_code, status_description, attrs):
        self.status_code        = status_code
        self.status_description = status_description
        self.attrs              = attrs

    def __getattr__(self, key):
        return self.attrs[key]



class AddressChecker ( object ):
    """
    Format address-query messages and parse the responses.
    """
    def __init__ ( self, reply_addr ):
        self.reply_addr = reply_addr

    def parse_address_query_response ( self, msg ):
        ap = msg.properties
        return AddressCheckResponse ( ap['statusCode'], ap['statusDescription'], msg.body )

    def make_address_query ( self, name ):
        ap = {'operation': 'READ', 'type': 'org.apache.qpid.dispatch.router.address', 'name': name}
        return Message ( properties=ap, reply_to=self.reply_addr )

    def make_addresses_query ( self ):
        ap = {'operation': 'QUERY', 'type': 'org.apache.qpid.dispatch.router.address'}
        return Message ( properties=ap, reply_to=self.reply_addr )



class AddressCheckerTimeout ( object ):
    def __init__(self, parent):
        self.parent = parent

    def on_timer_task(self, event):
        self.parent.address_check_timeout()

#------------------------------------------------
# END Helper classes for all tests.
#------------------------------------------------




#================================================================
#     Setup
#================================================================

class DistributionTests ( TestCase ):

    @classmethod
    def setUpClass(cls):
        """
        Create a router topology that is a superset of the topologies we will
        need for various tests.  So far, we have only two types of tests:
        3-router linear, and 3-router triangular.  The various tests simply
        attach their senders and receivers appropriately to 'see' their
        desired topology.
        """
        super(DistributionTests, cls).setUpClass()


        def router(name, more_config):

            config = [ ('router',  {'mode': 'interior', 'id': name}),
                       ('address', {'prefix': 'closest',  'distribution': 'closest'}),
                       ('address', {'prefix': 'balanced', 'distribution': 'balanced'})
                     ] + more_config

            config = Qdrouterd.Config(config)

            cls.routers.append(cls.tester.qdrouterd(name, config, wait=True))

        cls.routers = []



        #
        #     Connection picture
        #
        #           1           1
        #         A <-------- B <------ C
        #          ^ 2       ^ 2
        #           \       /
        #            \     /
        #             \   /
        #              \ /
        #               D
        #
        #

        A_client_port          = cls.tester.get_port()
        B_client_port          = cls.tester.get_port()
        C_client_port          = cls.tester.get_port()
        D_client_port          = cls.tester.get_port()

        A_inter_router_port_1  = cls.tester.get_port()
        A_inter_router_port_2  = cls.tester.get_port()
        B_inter_router_port_1  = cls.tester.get_port()
        B_inter_router_port_2  = cls.tester.get_port()

        # "Route-container port" does not mean that the port
        # contains a route.  It means that any client that
        # connectsd to the port is considered to be a route-
        # container.
        A_route_container_port = cls.tester.get_port()
        B_route_container_port = cls.tester.get_port()
        C_route_container_port = cls.tester.get_port()
        D_route_container_port = cls.tester.get_port()

        # Costs for balanced tests. The 'balanced' distribution
        # takes these costs into account in its algorithm.
        # Costs are associated not with routers, but with the
        # connections between routers.  In the config, they may
        # be attached to the inter-router listener, or the connector,
        # or both.  If both the inter-router listener and the
        # connector have associated costs, the higher of the two
        # will be used.
        cls.A_B_cost =   10
        cls.B_C_cost =   20
        cls.A_D_cost =   50
        cls.B_D_cost =  100

        cls.linkroute_prefix = "0.0.0.0/linkroute"

        router ( 'A',
                 [
                    ( 'listener',
                      { 'port': A_client_port,
                        'role': 'normal',
                        'stripAnnotations': 'no'
                      }
                    ),
                    ( 'listener',
                      {  'role': 'inter-router',
                         'port': A_inter_router_port_1
                      }
                    ),
                    ( 'listener',
                      {  'role': 'inter-router',
                         'port': A_inter_router_port_2
                      }
                    ),
                    ( 'listener',
                      { 'port': A_route_container_port,
                        'stripAnnotations': 'no',
                        'role': 'route-container'
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

        router ( 'B',
                 [
                    ( 'listener',
                      { 'port': B_client_port,
                        'role': 'normal',
                        'stripAnnotations': 'no'
                      }
                    ),
                    ( 'listener',
                      {  'role': 'inter-router',
                         'port': B_inter_router_port_1
                      }
                    ),
                    ( 'listener',
                      {  'role': 'inter-router',
                         'port': B_inter_router_port_2
                      }
                    ),
                    ( 'listener',
                      { 'port': B_route_container_port,
                        'stripAnnotations': 'no',
                        'role': 'route-container'
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
                    ),
                    ( 'connector',
                      {  'name': 'connectorToA',
                         'role': 'inter-router',
                         'port': A_inter_router_port_1,
                         'verifyHostName': 'no',
                         'cost':  cls.A_B_cost
                      }
                    )
                 ]
               )

        router ( 'C',
                 [
                    ( 'listener',
                      { 'port': C_client_port,
                        'role': 'normal',
                        'stripAnnotations': 'no'
                      }
                    ),
                    ( 'listener',
                       { 'port': C_route_container_port,
                         'stripAnnotations': 'no',
                         'role': 'route-container'
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
                    ),
                    ( 'connector',
                      {  'name': 'connectorToB',
                         'role': 'inter-router',
                         'port': B_inter_router_port_1,
                         'verifyHostName': 'no',
                         'cost' : cls.B_C_cost
                      }
                    )
                 ]
               )

        router ( 'D',
                 [
                    ( 'listener',
                      { 'port': D_client_port,
                        'role': 'normal',
                        'stripAnnotations': 'no'
                      }
                    ),
                    ( 'listener',
                       { 'port': D_route_container_port,
                         'stripAnnotations': 'no',
                         'role': 'route-container'
                       }
                    ),
                    ( 'connector',
                      {  'name': 'connectorToA',
                         'role': 'inter-router',
                         'port': A_inter_router_port_2,
                         'verifyHostName': 'no',
                         'cost' : cls.A_D_cost
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
                    ),
                    ( 'connector',
                      {  'name': 'connectorToB',
                         'role': 'inter-router',
                         'port': B_inter_router_port_2,
                         'verifyHostName': 'no',
                         'cost' : cls.B_D_cost
                      }
                    )
                 ]
               )

        router_A = cls.routers[0]
        router_B = cls.routers[1]
        router_C = cls.routers[2]
        router_D = cls.routers[3]

        cls.A_route_container_addr = router_A.addresses[3]
        cls.B_route_container_addr = router_B.addresses[3]
        cls.C_route_container_addr = router_B.addresses[1]
        cls.D_route_container_addr = router_B.addresses[1]

        router_A.wait_router_connected('B')
        router_A.wait_router_connected('C')
        router_A.wait_router_connected('D')

        cls.A_addr = router_A.addresses[0]
        cls.B_addr = router_B.addresses[0]
        cls.C_addr = router_C.addresses[0]
        cls.D_addr = router_D.addresses[0]

    def test_01_targeted_sender_AC ( self ):
        test = TargetedSenderTest ( self.A_addr, self.C_addr, "closest/01" )
        test.run()
        self.assertEqual ( None, test.error )

    def test_02_targeted_sender_DC ( self ):
        test = TargetedSenderTest ( self.D_addr, self.C_addr, "closest/02" )
        test.run()
        self.assertEqual ( None, test.error )

    def test_03_anonymous_sender_AC ( self ):
        test = AnonymousSenderTest ( self.A_addr, self.C_addr )
        test.run()
        self.assertEqual ( None, test.error )

    def test_04_anonymous_sender_DC ( self ):
        test = AnonymousSenderTest ( self.D_addr, self.C_addr )
        test.run()
        self.assertEqual ( None, test.error )

    def test_05_dynamic_reply_to_AC ( self ):
        test = DynamicReplyTo ( self.A_addr, self.C_addr )
        test.run()
        self.assertEqual ( None, test.error )


    def test_06_dynamic_reply_to_DC ( self ):
        test = DynamicReplyTo ( self.D_addr, self.C_addr )
        test.run()
        self.assertEqual ( None, test.error )


    def test_07_linkroute ( self ):
        test = LinkAttachRouting ( self.C_addr,
                                   self.A_route_container_addr,
                                   self.linkroute_prefix,
                                   "addr_07"
                                 )
        test.run()
        self.assertEqual(None, test.error)


    def test_08_closest ( self ):
        test = ClosestTest ( self.A_addr,
                             self.B_addr,
                             self.C_addr,
                             "addr_08"
                           )
        test.run()
        self.assertEqual(None, test.error)

        #
        #     Cost picture for balanced distribution tests.
        #
        #              10          20
        #         A <-------- B <------ C
        #          ^         ^
        #           \       /
        #       50   \     /  100
        #             \   /
        #              \ /
        #               D
        #
        #
        #
        #  Here is how the message balancing should work for
        #  various total number of messages, up to 100:
        #
        #  NOTE: remember these messages are all unsettled.
        #        And will stay that way.  This is not a realistic
        #        usage scenario, but it the best way to test the
        #        balanced distribution algorithm.
        #
        #  1. Messages start flowing in at A.  They will all
        #     be used by A (sent to its receiver) until the
        #     total == cost ( A, B ).
        #
        #  2. At that point, A will start sharing with B,
        #     one-for-me-one-for-you. (So A will go to 11 before
        #     B gets its first message.)
        #
        #  3. A and B will count up until B reaches
        #     cost ( B, C )
        #     B will then start sharings its messages with C,
        #     one-for-me-one-for-you.  (So B will go to 21 before
        #     C gets its first message.)
        #
        #  4. However note: it is NOT round-robin at this point.
        #     A is still taking every other message, B is only getting
        #     A's overflow, and now B is sharing half of that with C.
        #     So at this point B will start falling farther behind A.
        #
        #  5. The totals here are completely deterministic, so we pass
        #     to the test a 'slop' amount of 0.
        #
        #    total   near --10--> mid ---20--> far
        #
        #     1        1            0            0
        #     10      10            0            0
        #     11      11            0            0
        #     12      11            1            0
        #     13      12            1            0
        #     14      12            2            0
        #     ...
        #     50      30           20            0
        #     51      31           20            0
        #     52      31           21            0
        #     53      32           21            0
        #     54      32           21            1
        #     55      33           21            1
        #     56      33           22            1
        #     57      34           22            1
        #     58      34           22            2
        #     59      35           22            2
        #     60      35           23            2
        #     ...
        #     100     55           33           12
        #


    def test_09_balanced_linear ( self ):
        # slop is how much the second two values may diverge from
        # the expected.  But they still must sum to total - A.
        total      = 100
        expected_A = 55
        expected_B = 33
        expected_C = 12
        slop       = 0
        omit_middle_receiver = False
        test = BalancedTest ( self.A_addr,
                              self.B_addr,
                              self.C_addr,
                              "addr_09",
                              total,
                              expected_A,
                              expected_B,
                              expected_C,
                              slop,
                              omit_middle_receiver
                            )
        test.run()
        self.assertEqual(None, test.error)


    def test_10_balanced_linear_omit_middle_receiver ( self ):
        # If we omit the middle receiver, then router A will count
        # up to cost ( A, B ) and the keep counting up a further
        # cost ( B, C ) before it starts to spill over.
        # That is, it will count up to
        #    cost ( A, B ) + cost ( B, C ) == 30
        # After that it will start sharing downstream (router C)
        # one-for-me-one-for-you.  So when the number of total messages
        # is odd, A will be 31 ahead of C.  When total message count is
        # even, A will be 30 ahead.
        # As in the other linear scenario, there is no 'slop' here.
        total      = 100
        expected_A = 65
        expected_B = 0
        expected_C = 35
        slop       = 0
        omit_middle_receiver = True
        test = BalancedTest ( self.A_addr,
                              self.B_addr,
                              self.C_addr,
                              "addr_09",
                              total,
                              expected_A,
                              expected_B,
                              expected_C,
                              slop,
                              omit_middle_receiver
                            )
        test.run()
        self.assertEqual(None, test.error)


        #     Reasoning for the triangular balanced case:

        #     Cost picture
        #
        #              10          20
        #         A <-------- B <------ C
        #          ^         ^
        #           \       /
        #       50   \     /  100
        #             \   /
        #              \ /
        #               D
        #
        # We are doing  ( A, B, D ), with the sender attached at A.
        # All these messages are unsettled, which is what allows us to
        # see how the balanced distribution algorithm works.
        #
        #  1. total unsettled msgs at A cannot be more than B_cost + 1,
        #     and also cannot be more than D_cost + 1
        #
        #  2. A will always keep the message for itself (for its own receiver)
        #     if it can do so without violating rule (1).
        #
        #  3. So, A will count up to 11, and then it will start alternating
        #     with B.
        #
        #  4. When A counts up to 51, it must also start sharing with D.
        #     It will alternate between B and D.
        #
        #  5. As long as B does not yet have 100 messages, it will not
        #     share with D.
        #
        #  6. So! at 100 messages total, A must be above both of its
        #     neighbors by that neighbor's cost, or 1 more -- and the total
        #     of all 3 must sum to 100.
        #
        #     A = B + 10      B = A - 10
        #     A = D + 50      D = A - 50
        #     A + B + D == 100
        #     -->
        #     A + (A - 10) + (A - 50) == 100
        #     3A - 60 == 100
        #     A == 53.333...
        #     A == 54
        #
        #     so B + D == 46
        #     A is 10 or 11 > B --> B == 44 or 43
        #     A is 50 or 51 > D --> D ==  4 or  3
        #     B == 43 and D == 3

        #     So pass these values in to the test: (54, 43, 3)
        #     and test that:
        #       1. A is exactly that value.
        #       2. B and D sum to 100 - A
        #       3. B and D are both with 1 of their expected values.
        #
    def test_11_balanced_triangle ( self ):
        total      = 100
        expected_A = 54
        expected_B = 43
        expected_C = 3
        slop       = 1
        omit_middle_receiver = False
        test = BalancedTest ( self.A_addr,
                              self.B_addr,
                              self.D_addr,
                              "addr_10",
                              total,
                              expected_A,
                              expected_B,
                              expected_C,
                              slop,
                              omit_middle_receiver
                            )
        test.run()
        self.assertEqual(None, test.error)











#================================================================
#     Tests
#================================================================


class TargetedSenderTest ( MessagingHandler ):
    """
    A 'targeted' sender is one in which we tell the router what
    address we want to send to. (As opposed to letting the router
    pass back an address to us.)
    """
    def __init__ ( self, send_addr, recv_addr, destination ):
        super(TargetedSenderTest, self).__init__(prefetch=0)
        self.send_addr  = send_addr
        self.recv_addr  = recv_addr
        self.dest       = destination
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



class AnonymousSenderTest ( MessagingHandler ):
    """
    An 'anonymous' sender is one in which we let the router tell
    us what address the sender should use.  It will pass back this
    information to us when we get the on_link_opened event.
    """

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
            # Here we are told the address that we will use for the sender.
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
#=======================================================================
class DynamicReplyTo(MessagingHandler):
    """
    In this test we have a separate 'client' and 'server' with separate
    connections.  The client sends requests to the server, and embeds in
    them its desired reply-to address.  The server uses that address to
    send back messages.  The tests ends with success if the client receives
    the expected number of replies, or with failure if we time out before
    that happens.
    """
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
        self.linkroute_check_sender   = None

        self.count     = 10
        self.n_sent    = 0
        self.n_rcvd    = 0
        self.n_settled = 0


    def timeout ( self ):
        self.bail ( "Timeout Expired: n_sent=%d n_rcvd=%d n_settled=%d" %
                    (self.n_sent, self.n_rcvd, self.n_settled) )


    def address_check_timeout(self):
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
        self.linkroute_check_sender   = event.container.create_sender(self.nearside_cnx, "$management")


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
            response = self.linkroute_checker.parse_address_query_response(event.message)
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
                self.linkroute_check_timer = event.reactor.schedule(0.25, AddressCheckerTimeout(self))


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
        # Send the message that will query the management code to discover
        # information about our destination address. We cannot make our payload
        # sender until the network is ready.
        #
        # BUGALERT: We have to prepend the 'D' to this linkroute prefix
        # because that's what the router does internally.  Someday this
        # may change.
        self.linkroute_check_sender.send ( self.linkroute_checker.make_address_query("D" + self.linkroute_prefix) )


    def on_settled(self, event):
        if event.sender == self.nearside_sender:
            self.n_settled += 1
            if self.n_settled == self.count:
                self.bail ( None )


    def run(self):
        container = Container(self)
        container.container_id = 'LinkRouteTest'
        container.run()



class ClosestTest ( MessagingHandler ):
    """
    Test whether distance-based message routing works in a
    linear 3-router network.

    sender -----> NEAR -----> MID -----> FAR
                   |           |          |
                   v           v          v
                  near        mid        far
                  rcvrs       rcvrs      rcvrs

    With a linear network of 3 routers, set up a sender on the
    near one, and then 2 receivers each on the near, middle, and
    far routers.
    After the first 10 messages have been received, close the
    near routers and check results so far.  All 10 messages should
    have gone to the near receivers, and none to the mid or far
    receivers.
    After the next 10 messages have been received, close the two
    middle routers and check again.  All 10 messages should have
    gone to the middle receivers, and none to the far ones.
    Finally, after another 10 messages have been received, check
    that they went to the far receivers.
    """
    def __init__ ( self, near_router, mid_router, far_router, addr_suffix ):
        super ( ClosestTest, self ).__init__(prefetch=0)
        self.error         = None
        self.near_router   = near_router
        self.mid_router    = mid_router
        self.far_router    = far_router
        self.addr_suffix   = addr_suffix
        self.dest          = "closest/" + addr_suffix

        # This n_expected is actually the minimum number of messages
        # I will send.  The real number will be higher because some
        # will be released when I close the near and middle receivers.
        self.n_expected = 300
        self.one_third  = self.n_expected / 3

        # n_received -- the grand total -- is used to decide when to
        # close the near receivers and later the middle ones.
        self.n_received    = 0

        # Counters for the near, middle, and far receivers are used
        # to determine whether there has been an error.
        self.near_1_count = 0
        self.near_2_count = 0
        self.mid_1_count  = 0
        self.mid_2_count  = 0
        self.far_1_count  = 0
        self.far_2_count  = 0

        self.addr_check_timer    = None
        self.addr_check_receiver = None
        self.addr_check_sender   = None

    def timeout ( self ):
        self.check_results ( )
        self.bail ( "Timeout Expired " )


    def address_check_timeout(self):
        self.addr_check()


    def bail ( self, text ):
        self.timer.cancel()
        self.error = text
        self.send_cnx.close()
        self.near_cnx.close()
        self.mid_cnx.close()
        self.far_cnx.close()
        if self.addr_check_timer:
            self.addr_check_timer.cancel()


    def on_start ( self, event ):
        self.timer    = event.reactor.schedule  ( TIMEOUT, Timeout(self) )
        self.send_cnx = event.container.connect ( self.near_router )
        self.near_cnx = event.container.connect ( self.near_router )
        self.mid_cnx  = event.container.connect ( self.mid_router )
        self.far_cnx  = event.container.connect ( self.far_router )

        # Warning!
        # The two receiver-links on each router must be given
        # explicit distinct names, or we will in fact only get
        # one link.  And then wonder why receiver 2 on each
        # router isn't getting any messages.
        self.near_recv_1 = event.container.create_receiver  ( self.near_cnx, self.dest, name="1" )
        self.near_recv_2 = event.container.create_receiver  ( self.near_cnx, self.dest, name="2" )

        self.mid_recv_1  = event.container.create_receiver  ( self.mid_cnx,  self.dest, name="1" )
        self.mid_recv_2  = event.container.create_receiver  ( self.mid_cnx,  self.dest, name="2" )

        self.far_recv_1  = event.container.create_receiver  ( self.far_cnx,  self.dest, name="1" )
        self.far_recv_2  = event.container.create_receiver  ( self.far_cnx,  self.dest, name="2" )

        self.near_recv_1.flow ( self.n_expected )
        self.mid_recv_1.flow  ( self.n_expected )
        self.far_recv_1.flow  ( self.n_expected )

        self.near_recv_2.flow ( self.n_expected )
        self.mid_recv_2.flow  ( self.n_expected )
        self.far_recv_2.flow  ( self.n_expected )

        self.addr_check_receiver = event.container.create_receiver ( self.near_cnx, dynamic=True )
        self.addr_check_sender   = event.container.create_sender ( self.near_cnx, "$management" )


    def on_link_opened(self, event):
        if event.receiver:
            event.receiver.flow ( self.n_expected )
        if event.receiver == self.addr_check_receiver:
            # Step 2. my addr-check link has opened: make the addr_checker with the given address.
            self.addr_checker = AddressChecker ( self.addr_check_receiver.remote_source.address )
            self.addr_check()


    def on_sendable ( self, event ):
        msg = Message ( body     = "Hello, closest.",
                        address  = self.dest
                      )
        event.sender.send ( msg )


    def on_message ( self, event ):

        if event.receiver == self.addr_check_receiver:
            # This is a response to one of my address-readiness checking messages.
            response = self.addr_checker.parse_address_query_response(event.message)
            if response.status_code == 200 and response.subscriberCount == 2 and response.remoteCount == 2:
                # now we know that we have two subscribers on nearside router, and two remote
                # routers that know about the address. The network is ready.
                # Now we can make the nearside sender without getting a
                # "No Path To Destination" error.
                self.sender = event.container.create_sender ( self.send_cnx, self.dest )

                # And we can quit checking.
                if self.addr_check_timer:
                    self.addr_check_timer.cancel()
                    self.addr_check_timer = None
            else:
                # If the latest check did not find the link-attack route ready,
                # schedule another check a little while from now.
                self.addr_check_timer = event.reactor.schedule(0.25, AddressCheckerTimeout(self))
        else :
            # This is a payload message.
            self.n_received += 1

            # Increment the near, mid, or far counts, depending on
            # which receiver the message came in on.
            if event.receiver == self.near_recv_1:
                self.near_1_count        += 1
            elif event.receiver == self.near_recv_2:
                self.near_2_count        += 1
            elif event.receiver == self.mid_recv_1:
                self.mid_1_count         += 1
            elif event.receiver == self.mid_recv_2:
                self.mid_2_count         += 1
            elif event.receiver == self.far_recv_1:
                self.far_1_count         += 1
            elif event.receiver == self.far_recv_2:
                self.far_2_count         += 1

            if self.n_received == self.one_third:
                # The first one-third of messages should have gone exclusively
                # to the near receivers.  At this point we should have
                # no messages in the mid or far receivers.
                self.near_recv_1.close()
                self.near_recv_2.close()
                if self.mid_1_count + self.mid_2_count + self.far_1_count + self.far_2_count > 0 :
                    self.bail ( "error: mid or far receivers got messages before near were closed." )
                # Make sure we got one third of the messages.
                if self.near_1_count + self.near_2_count < self.one_third:
                    self.bail ( "error: the near receivers got too few messages." )
                # Make sure both receivers got some messages.
                if self.near_1_count * self.near_2_count == 0:
                    self.bail ( "error: one of the near receivers got no messages." )

            elif self.n_received == 2 * self.one_third:
                # The next one-third of messages should have gone exclusively
                # to the mid receivers.  At this point we should have
                # no messages in the far receivers.
                self.mid_recv_1.close()
                self.mid_recv_2.close()
                if self.far_1_count + self.far_2_count > 0 :
                    self.bail ( "error: far receivers got messages before mid were closed." )
                # Make sure we got one third of the messages.
                if self.mid_1_count + self.mid_2_count < self.one_third:
                    self.bail ( "error: the mid receivers got too few messages." )
                # Make sure both receivers got some messages.
                if self.mid_1_count * self.mid_2_count == 0:
                    self.bail ( "error: one of the mid receivers got no messages." )

            # By the time we reach the expected number of messages
            # we have closed the near and middle receivers.  If the far
            # receivers are empty at this point, something is wrong.
            if self.n_received >= self.n_expected :
                if self.far_1_count + self.far_2_count < self.one_third:
                    self.bail ( "error: the far receivers got too few messages." )
                if self.far_1_count * self.far_2_count == 0:
                    self.bail ( "error: one of the far receivers got no messages." )
                else:
                    self.bail ( None )


    def addr_check ( self ):
        # Send the message that will query the management code to discover
        # information about our destination address. We cannot make our payload
        # sender until the network is ready.
        #
        # BUGALERT: We have to prepend the 'M0' to this address prefix
        # because that's what the router does internally.  Someday this
        # may change.
        self.addr_check_sender.send ( self.addr_checker.make_address_query("M0" + self.dest) )


    def run(self):
        container = Container(self)
        container.run()





class BalancedTest ( MessagingHandler ):
    """
    This test is topology-agnostic. This code thinks of its nodes as 1, 2, 3.
    The caller knows if they are linear or triangular, or a tree.  It calculates
    the expected results for nodes 1, 2, and 3, and also tells me if there can be
    a little 'slop' in the results.
    ( Slop can happen in some topologies when you can't tell whether spillover
    will happen first to node 2, or to node 3.
    """
    def __init__ ( self, router_1, router_2, router_3, addr_suffix, total_messages, expected_1, expected_2, expected_3, slop, omit_middle_receiver ):
        super ( BalancedTest, self ).__init__(prefetch=0, auto_accept=False)
        self.error       = None
        self.router_3    = router_3
        self.router_2    = router_2
        self.router_1    = router_1
        self.addr_suffix = addr_suffix
        self.dest        = "balanced/" + addr_suffix

        self.total_messages  = total_messages
        self.n_sent          = 0
        self.n_received      = 0

        self.recv_1 = None
        self.recv_2 = None
        self.recv_3 = None

        self.count_3 = 0
        self.count_2 = 0
        self.count_1 = 0

        self.expected_1 = expected_1
        self.expected_2 = expected_2
        self.expected_3 = expected_3
        self.slop       = slop
        self.omit_middle_receiver = omit_middle_receiver

        self.address_check_timer    = None
        self.address_check_receiver = None
        self.address_check_sender   = None

        self.payload_sender = None


    def timeout ( self ):
        self.bail ( "Timeout Expired " )


    def address_check_timeout(self):
        self.address_check()


    def bail ( self, text ):
        self.timer.cancel()
        self.error = text
        self.cnx_3.close()
        self.cnx_2.close()
        self.cnx_1.close()
        if self.address_check_timer:
            self.address_check_timer.cancel()


    def on_start ( self, event ):
        self.timer    = event.reactor.schedule  ( TIMEOUT, Timeout(self) )
        self.cnx_3    = event.container.connect ( self.router_3 )
        self.cnx_2    = event.container.connect ( self.router_2 )
        self.cnx_1    = event.container.connect ( self.router_1 )

        self.recv_3  = event.container.create_receiver ( self.cnx_3,  self.dest )
        if self.omit_middle_receiver is False :
            self.recv_2 = event.container.create_receiver ( self.cnx_2,  self.dest )
        self.recv_1  = event.container.create_receiver ( self.cnx_1,  self.dest )

        self.recv_3.flow ( self.total_messages )
        if self.omit_middle_receiver is False :
            self.recv_2.flow ( self.total_messages )
        self.recv_1.flow ( self.total_messages )

        self.address_check_receiver = event.container.create_receiver ( self.cnx_1, dynamic=True )
        self.address_check_sender   = event.container.create_sender   ( self.cnx_1, "$management" )


    def on_link_opened(self, event):
        if event.receiver:
            event.receiver.flow ( self.total_messages )
        if event.receiver == self.address_check_receiver:
            # My address check-link has opened: make the address_checker
            self.address_checker = AddressChecker ( self.address_check_receiver.remote_source.address )
            self.address_check()


    def on_message ( self, event ):

        if self.n_received >= self.total_messages:
            return   # Sometimes you can get a message or two even after you have called bail().

        if event.receiver == self.address_check_receiver:
            # This is one of my route-readiness checking messages.
            response = self.address_checker.parse_address_query_response(event.message)
            if self.omit_middle_receiver is True :
                expected_remotes = 1
            else :
                expected_remotes = 2

            if response.status_code == 200 and response.subscriberCount == 1 and response.remoteCount == expected_remotes:
                # Got confirmation of dest addr fully propagated through network.
                # Since I have 3 nodes, I want to see 1 subscriber (which is on the local router) and
                # 2 remote routers that know about my destination address.
                # Now we can safely make the payload sender without getting a 'No Path To Destination' error.
                self.payload_sender = event.container.create_sender ( self.cnx_1, self.dest )
                # And we can quit checking.
                if self.address_check_timer:
                    self.address_check_timer.cancel()
                    self.address_check_timer = None
            else:
                # If the latest check did not find the link-attack route ready,
                # schedule another check a little while from now.
                self.address_check_timer = event.reactor.schedule(0.50, AddressCheckerTimeout(self))

        else:
            self.n_received += 1

            if   event.receiver == self.recv_1: self.count_1 += 1
            elif event.receiver == self.recv_2: self.count_2 += 1
            elif event.receiver == self.recv_3: self.count_3 += 1

            # I do not check for count_1 + count_2 + count_3 == total,
            # because it always will be due to how the code counts things.
            if self.n_received == self.total_messages:
                if self.count_1 != self.expected_1:
                    self.bail ( "bad count 1: count %d != expected %d" % (self.count_1, self.expected_1) )
                elif abs(self.count_2 - self.expected_2) > self.slop:
                    self.bail ( "count_2 %d is more than %d different from expectation %d" % (self.count_2, self.slop, self.expected_2) )
                elif abs(self.count_3 - self.expected_3) > self.slop:
                    self.bail ( "count_3 %d is more than %d different from expectation %d" % (self.count_3, self.slop, self.expected_3) )
                else:
                    self.bail ( None) # All is well.


    def on_sendable ( self, event ):
        if self.n_sent < self.total_messages and event.sender == self.payload_sender :
            msg = Message ( body     = "Hello, balanced.",
                            address  = self.dest
                          )
            self.payload_sender.send ( msg )
            self.n_sent += 1


    def address_check ( self ):
        # Send the message that will query the management code to discover
        # information about our destination address. We cannot make our payload
        # sender until the network is ready.
        #
        # BUGALERT: We have to prepend the 'M0' to this address prefix
        # because that's what the router does internally.  Someday this
        # may change.
        self.address_check_sender.send ( self.address_checker.make_address_query("M0" + self.dest) )


    def run(self):
        container = Container(self)
        container.run()





if __name__ == '__main__':
    unittest.main(main_module())

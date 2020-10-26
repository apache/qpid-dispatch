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

import sys
from proton          import Message
from system_test     import TestCase, Qdrouterd, main_module, TIMEOUT, SkipIfNeeded, TestTimeout
from system_test     import unittest
from proton.handlers import MessagingHandler
from proton.reactor  import Container, LinkOption, ApplicationEvent, EventInjector


#------------------------------------------------
# Helper classes for all tests.
#------------------------------------------------


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


class DistributionSkipMapper(object):
    # 1 means skip that test.
    skip = {'test_01': 0,
            'test_02': 0,
            'test_03': 0,
            'test_04': 0,
            'test_05': 0,
            'test_06': 0,
            'test_07': 0,
            'test_08': 0,
            'test_09': 0,
            'test_10': 0,
            'test_11': 0,
            'test_12': 0,
            'test_13': 0,
            'test_14': 0,
            'test_15': 0,
            'test_16': 1,
            'test_17': 1,
            'test_18': 1,
            'test_19': 1,
            'test_20': 1,
            'test_21': 1,
            'test_22': 1,
            'test_23': 0,
            'test_24': 0,
            'test_25': 0
            }


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


        cls.linkroute_prefix   = "0.0.0.0/linkroute"
        cls.waypoint_prefix_1  = "0.0.0.0/process_1"
        cls.waypoint_prefix_2  = "0.0.0.0/process_2"
        cls.waypoint_prefix_3  = "0.0.0.0/process_3"

        #-----------------------------------------------------
        # Container IDs are what associate route containers
        # with links -- for the linkroute tests and the
        # waypoint tests.
        #-----------------------------------------------------
        cls.container_ids = [ 'ethics_gradient',
                              'honest_mistake',
                              'frank_exchange_of_views',
                              'zero_gravitas',
                              'yawning_angel'
                            ]

        #-----------------------------------------------------
        # Here are some chunks of configuration that will be
        # the same on all routers.
        #-----------------------------------------------------

        linkroute_configuration =             \
        [
            ( 'linkRoute',
              { 'prefix': cls.linkroute_prefix,
                'direction': 'in',
                'containerId': cls.container_ids[0]
              }
            ),
            ( 'linkRoute',
              { 'prefix': cls.linkroute_prefix,
                'direction': 'out',
                'containerId': cls.container_ids[0]
              }
            )
        ]


        single_waypoint_configuration =        \
        [
            ( 'address',
              { 'prefix': cls.waypoint_prefix_1,
                'waypoint': 'yes'
              }
            ),
            ( 'autoLink',
              { 'address': cls.waypoint_prefix_1 + '.waypoint',
                'containerId': cls.container_ids[1],
                'direction': 'in'
              }
            ),
            ( 'autoLink',
              { 'address': cls.waypoint_prefix_1 + '.waypoint',
                'containerId': cls.container_ids[1],
                'direction': 'out'
              }
            )
        ]

        #-------------------------------------------------------------------
        # The phase-number is used by the router as an addition
        # to the address for the link. To chain these two waypoints
        # together in a serial fashion, we explicitly declare their
        # phase numbers:
        #    Waypoint 1
        #        out of router to process:    phase 0
        #        back from process to router: phase 1
        #    Waypoint 2
        #        out of router to process:    phase 1
        #        back from process to router: phase 2
        #
        # Because of those two "phase 1" markings, messages coming back
        # into the router from Waypoint 1 get routed back outbound to 
        # Waypoint 2.
        #
        # Because the address configuration specifies that phase 2 is
        # the egress phase, messages coming into the router from that 
        # autolink are finally routed to the client receiver.
        #-------------------------------------------------------------------
        serial_waypoint_configuration =  \
        [
            ( 'address',
              { 'prefix': cls.waypoint_prefix_2,
                'ingressPhase' : 0,    # into the waypoint-process
                'egressPhase'  : 2,    # out of the waypoint process
              }
            ),

            # Waypoint 1 configuration --------------------------
            ( 'autoLink',
              { 'address': cls.waypoint_prefix_2 + '.waypoint',
                'phase' : 0,
                'containerId': cls.container_ids[2],
                'direction': 'out'    # out-of-router
              }
            ),
            ( 'autoLink',
              { 'address': cls.waypoint_prefix_2 + '.waypoint',
                'phase' : 1,
                'containerId': cls.container_ids[2],
                'direction': 'in'    # into-router
              }
            ),

            # Waypoint 2 configuration --------------------------
            ( 'autoLink',
              { 'address': cls.waypoint_prefix_2 + '.waypoint',
                'phase' : 1,   # out-of-router
                'containerId': cls.container_ids[2],
                'direction': 'out'
              }
            ),
            ( 'autoLink',
              { 'address': cls.waypoint_prefix_2 + '.waypoint',
                'phase' : 2,   # into-router
                'containerId': cls.container_ids[2],
                'direction': 'in'
              }
            )
        ]


        #-------------------------------------------------------------
        # In a parallel waypoint configuration, we use the default
        # phase numbers: toward the waypoint is phase 0, back from
        # the waypoint into the router is phase 1.
        # The address configuration, by saying 'waypoint: yes' is
        # shorthand for "ingress is phase 0, egress is phase 1"
        # By making two identical waypoints, they will be used in
        # parallel rather than serial.
        #-------------------------------------------------------------
        parallel_waypoint_configuration =  \
        [
            ( 'address',
              { 'prefix': cls.waypoint_prefix_3,
                'waypoint': 'yes'
              }
            ),

            #  Waypoint 1 configuration ----------------------
            ( 'autoLink',
              { 'address': cls.waypoint_prefix_3 + '.waypoint',
                'containerId': cls.container_ids[3],
                'direction': 'in'
              }
            ),
            ( 'autoLink',
              { 'address': cls.waypoint_prefix_3 + '.waypoint',
                'containerId': cls.container_ids[3],
                'direction': 'out'
              }
            ),

            #  Waypoint 2 configuration ----------------------
            ( 'autoLink',
              { 'address': cls.waypoint_prefix_3 + '.waypoint',
                'containerId': cls.container_ids[3],
                'direction': 'in'
              }
            ),
            ( 'autoLink',
              { 'address': cls.waypoint_prefix_3 + '.waypoint',
                'containerId': cls.container_ids[3],
                'direction': 'out'
              }
            )
        ]


        def router(name, more_config):

            config = [ ('router',  {'mode': 'interior', 'id': name}),
                       ('address', {'prefix': 'closest',   'distribution': 'closest'}),
                       ('address', {'prefix': 'balanced',  'distribution': 'balanced'}),
                       ('address', {'prefix': 'multicast', 'distribution': 'multicast'})
                     ]                                 \
                     + linkroute_configuration         \
                     + single_waypoint_configuration   \
                     + serial_waypoint_configuration   \
                     + parallel_waypoint_configuration \
                     + more_config

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
        #  Note: in the above picture, an arrow from, i.e., B to A
        #        means that B initiates the connection from itself to A.
        #        So if you see "B ----> A" in the picture, you should also
        #        see a connector block in the configuration of B that
        #        connects to an inter-router port on A.
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
                      { 'port': A_route_container_port,  # route-container is listener number 3
                        'stripAnnotations': 'no',
                        'role': 'route-container'
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
                      { 'port': B_route_container_port,  # route-container is number 3
                        'stripAnnotations': 'no',
                        'role': 'route-container'
                      }
                    ),
                    ( 'connector',
                      {  'name': 'connectorToA',
                         'role': 'inter-router',
                         'port': A_inter_router_port_1,
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
                       { 'port': C_route_container_port,  # route-container is number 1
                         'stripAnnotations': 'no',
                         'role': 'route-container'
                       }
                    ),
                    ( 'connector',
                      {  'name': 'connectorToB',
                         'role': 'inter-router',
                         'port': B_inter_router_port_1,
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
                       { 'port': D_route_container_port,  # route-container is number 1
                         'stripAnnotations': 'no',
                         'role': 'route-container'
                       }
                    ),
                    ( 'connector',
                      {  'name': 'connectorToA',
                         'role': 'inter-router',
                         'port': A_inter_router_port_2,
                         'cost' : cls.A_D_cost
                      }
                    ),
                    ( 'connector',
                      {  'name': 'connectorToB',
                         'role': 'inter-router',
                         'port': B_inter_router_port_2,
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
        cls.C_route_container_addr = router_C.addresses[1]
        cls.D_route_container_addr = router_D.addresses[1]

        router_A.wait_router_connected('B')
        router_A.wait_router_connected('C')
        router_A.wait_router_connected('D')

        cls.A_addr = router_A.addresses[0]
        cls.B_addr = router_B.addresses[0]
        cls.C_addr = router_C.addresses[0]
        cls.D_addr = router_D.addresses[0]


    @SkipIfNeeded(DistributionSkipMapper.skip['test_01'], 'Test skipped during development.')
    def test_01_targeted_sender_AC ( self ):
        name='test_01'
        test = TargetedSenderTest ( name, self.A_addr, self.C_addr, "closest/01" )
        test.run()
        self.assertEqual ( None, test.error )


    @SkipIfNeeded(DistributionSkipMapper.skip['test_02'], 'Test skipped during development.')
    def test_02_targeted_sender_DC ( self ):
        name='test_02'
        test = TargetedSenderTest ( name, self.D_addr, self.C_addr, "closest/02" )
        test.run()
        self.assertEqual ( None, test.error )


    @SkipIfNeeded(DistributionSkipMapper.skip['test_03'], 'Test skipped during development.')
    def test_03_anonymous_sender_AC ( self ):
        name='test_03'
        test = AnonymousSenderTest ( name, self.A_addr, self.C_addr )
        test.run()
        self.assertEqual ( None, test.error )


    @SkipIfNeeded(DistributionSkipMapper.skip['test_04'], 'Test skipped during development.')
    def test_04_anonymous_sender_DC ( self ):
        name='test_04'
        test = AnonymousSenderTest ( name, self.D_addr, self.C_addr )
        test.run()
        self.assertEqual ( None, test.error )


    @SkipIfNeeded(DistributionSkipMapper.skip['test_05'], 'Test skipped during development.')
    def test_05_dynamic_reply_to_AC ( self ):
        name='test_05'
        test = DynamicReplyTo ( name, self.A_addr, self.C_addr )
        test.run()
        self.assertEqual ( None, test.error )


    @SkipIfNeeded(DistributionSkipMapper.skip['test_06'], 'Test skipped during development.')
    def test_06_dynamic_reply_to_DC ( self ):
        name='test_06'
        test = DynamicReplyTo ( name, self.D_addr, self.C_addr )
        test.run()
        self.assertEqual ( None, test.error )


    @SkipIfNeeded(DistributionSkipMapper.skip['test_07'], 'Test skipped during development.')
    def test_07_linkroute ( self ):
        name='test_07'
        test = LinkAttachRouting ( name,
                                   self.container_ids[0],
                                   self.C_addr,
                                   self.A_route_container_addr,
                                   self.linkroute_prefix,
                                   "addr_07"
                                 )
        test.run()
        self.assertEqual ( None, test.error )


    @SkipIfNeeded(DistributionSkipMapper.skip['test_08'], 'Test skipped during development.')
    def test_08_linkroute_check_only ( self ):
        name='test_08'
        test = LinkAttachRoutingCheckOnly ( name,
                                            self.container_ids[0],
                                            self.C_addr,
                                            self.A_route_container_addr,
                                            self.linkroute_prefix,
                                            "addr_08"
                                          )
        test.run()
        self.assertEqual ( None, test.error )


    @SkipIfNeeded(DistributionSkipMapper.skip['test_09'], 'Test skipped during development.')
    def test_09_closest_linear ( self ):
        name='test_09'
        test = ClosestTest ( name,
                             self.A_addr,
                             self.B_addr,
                             self.C_addr,
                             "addr_09",
                             print_debug=False
                           )
        test.run()
        self.assertEqual ( None, test.error )


    @SkipIfNeeded(DistributionSkipMapper.skip['test_10'], 'Test skipped during development.')
    def test_10_closest_mesh ( self ):
        name='test_10'
        test = ClosestTest ( name,
                             self.A_addr,
                             self.B_addr,
                             self.D_addr,
                             "addr_10"
                           )
        test.run()
        self.assertEqual ( None, test.error )

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
        #      C gets its first message.)
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

    @SkipIfNeeded(DistributionSkipMapper.skip['test_11'], 'Test skipped during development.')
    def test_11_balanced_linear ( self ):
        name='test_11'
        # slop is how much the second two values may diverge from
        # the expected.  But they still must sum to total - A.
        total      = 100
        expected_A = 55
        expected_B = 33
        expected_C = 12
        # FIXME - or investigate -- I believe this slop
        # should not be necessary -- the distribution
        # algorithm should be perfectly deterministic.
        # But -- without it, I am getting 0.3% failure rate on this test.
        slop = 1
        omit_middle_receiver = False

        test = BalancedTest ( name,
                              self.A_addr,
                              self.B_addr,
                              self.C_addr,
                              "addr_11",
                              total,
                              expected_A,
                              expected_B,
                              expected_C,
                              slop,
                              omit_middle_receiver
                            )
        test.run()
        self.assertEqual ( None, test.error )


    @SkipIfNeeded(DistributionSkipMapper.skip['test_12'], 'Test skipped during development.')
    def test_12_balanced_linear_omit_middle_receiver ( self ):
        name='test_12'
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
        # FIXME - or investigate -- I believe this slop
        # should not be necessary -- the distribution
        # algorithm should be perfectly deterministic.
        # But -- without it, I am getting 0.2% failure rate on this test.
        slop = 1
        omit_middle_receiver = True

        test = BalancedTest ( name,
                              self.A_addr,
                              self.B_addr,
                              self.C_addr,
                              "addr_12",
                              total,
                              expected_A,
                              expected_B,
                              expected_C,
                              slop,
                              omit_middle_receiver
                            )
        test.run()
        self.assertEqual ( None, test.error )


        #     Reasoning for the triangular balanced case:
        #
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
        #
        #     So pass these values in to the test: (54, 43, 3)
        #     and test that:
        #       1. A is exactly that value.
        #       2. B and D sum to 100 - A
        #       3. B and D are both with 1 of their expected values.
        #
    @SkipIfNeeded(DistributionSkipMapper.skip['test_13'], 'Test skipped during development.')
    def test_13_balanced_mesh ( self ):
        name='test_13'
        total      = 100
        expected_A = 54
        expected_B = 43
        expected_D = 3
        slop       = 1
        omit_middle_receiver = False
        test = BalancedTest ( name,
                              self.A_addr,
                              self.B_addr,
                              self.D_addr,
                              "addr_13",
                              total,
                              expected_A,
                              expected_B,
                              expected_D,
                              slop,
                              omit_middle_receiver
                            )
        test.run()
        self.assertEqual ( None, test.error )


    @SkipIfNeeded(DistributionSkipMapper.skip['test_14'], 'Test skipped during development.')
    def test_14_multicast_linear ( self ):
        name='test_14'
        test = MulticastTest ( name,
                               self.A_addr,
                               self.B_addr,
                               self.C_addr,
                               "addr_14"
                             )
        test.run()
        self.assertEqual ( None, test.error )


    @SkipIfNeeded(DistributionSkipMapper.skip['test_15'], 'Test skipped during development.')
    def test_15_multicast_mesh ( self ):
        name='test_15'
        test = MulticastTest ( name,
                               self.A_addr,
                               self.B_addr,
                               self.D_addr,
                               "addr_15"
                             )
        test.run()
        self.assertEqual ( None, test.error )


    @SkipIfNeeded(DistributionSkipMapper.skip['test_16'], 'Test skipped during development.')
    def test_16_linkroute_linear_all_local ( self ) :
        name='test_16'
        """
        This test should route all senders' link-attaches
        to the local containers on router A.
        """

        addr_suffix = "addr_16"

        # Choose which routers to give the test.
        # This choice controls topology.  ABC is linear.
        routers = ( self.A_route_container_addr,
                    self.B_route_container_addr,
                    self.C_route_container_addr
                  )

        # NOTE : about these 3-tuples.
        # The positions in these tuples correspond to the routers passed
        # in to the test: ( router_1, router_2, router_3 )
        # router_1 is always the 'local' one -- the one where the
        # test make its senders.

        # Tell the test on which routers to make its link-container cnxs.
        where_to_make_connections                = ( 2, 2, 2 )
        where_the_routed_link_attaches_should_go = ( 4, 0, 0 )

        # Tell the test how to check for the address being ready.
        n_local_containers = 2
        n_remote_routers   = 2

        #-----------------------------------------------------------------------
        # This is the instruction-list that the test looks at as various
        # milestones are met during testing. If a given event happens,
        # and if it matches the event in the current step of the instructions,
        # then the test will execute the action in the current step, and
        # advance to the next.
        # These instructions lists make the test more flexible, so I can get
        # different behavior without writing *almost* the same code mutiple
        # times.
        #-----------------------------------------------------------------------

        # note: if 'done' is present in an action, it always means 'succeed now'.
        # If there had been a failure, that would have been caught in an
        # earlier part of the action.

        instructions = [
                         # Once the link-routable address is ready to use in
                         # the router network, create 4 senders.
                         {
                           'event'  : 'address_ready',
                           'action' : { 'fn'   : 'make_senders',
                                         'arg' : 4
                                      }
                         },
                         # In this action, the list-argument to the function
                         # shows how we expect link-attach routes to be
                         # distributed: 4 to the first router,
                         # none to the other two.
                         {
                           'event'  : 'got_receivers',
                           'action' : { 'fn'   : 'check_receiver_distribution',
                                        'arg'  : where_the_routed_link_attaches_should_go,
                                      }
                         },
                         {
                           'event'  : 'receiver_distribution_ok',
                           'action' : {'fn'    : 'none',
                                       'done'  : 'succeed'
                                      }
                         }
                       ]

        test = RoutingTest ( name,
                             self.container_ids[0],
                             self.A_addr,  # all senders are attached here
                             routers,
                             self.linkroute_prefix,
                             addr_suffix,
                             instructions,
                             where_to_make_connections,
                             n_local_containers,
                             n_remote_routers
                           )
        test.run ( )
        self.assertEqual ( None, test.error )



    @SkipIfNeeded(DistributionSkipMapper.skip['test_17'], 'Test skipped during development.')
    def test_17_linkroute_linear_all_B ( self ) :
        name='test_17'
        """
        This test should route all senders' link-attaches
        to the remote connections on router B.
        """

        addr_suffix = "addr_17"

        # Choose which routers to give the test.
        # This choice controls topology.  ABC is linear.
        routers = ( self.A_route_container_addr,
                    self.B_route_container_addr,
                    self.C_route_container_addr
                  )

        # NOTE : about these 3-tuples.
        # The positions in these tuples correspond to the routers passed
        # in to the test: ( router_1, router_2, router_3 )
        # router_1 is always the 'local' one -- the one where the
        # test make its senders.

        # Tell the test on which routers to make its link-container cnxs.
        where_to_make_connections                = ( 0, 2, 2 )
        where_the_routed_link_attaches_should_go = ( 0, 4, 0 )

        # Tell the test how to check for the address being ready.
        n_local_containers = 0
        n_remote_routers   = 2

        #-----------------------------------------------------------------------
        # This is the instruction-list that the test looks at as various
        # milestones are met during testing. If a given event happens,
        # and if it matches the event in the current step of the instructions,
        # then the test will execute the action in the current step, and
        # advance to the next.
        # These instructions lists make the test more flexible, so I can get
        # different behavior without writing *almost* the same code mutiple
        # times.
        #-----------------------------------------------------------------------

        # note: if 'done' is present in an action, it always means 'succeed now'.
        # If there had been a failure, that would have been caught in an
        # earlier part of the action.

        instructions = [
                         # Once the link-routable address is ready to use in
                         # the router network, create 4 senders.
                         {
                           'event'  : 'address_ready',
                           'action' : { 'fn'   : 'make_senders',
                                         'arg' : 4
                                      }
                         },
                         # In this action, the list-argument to the function
                         # shows how we expect link-attach routes to be
                         # distributed: 4 to router B,
                         # none anywhere else.
                         {
                           'event'  : 'got_receivers',
                           'action' : { 'fn'   : 'check_receiver_distribution',
                                        'arg'  : where_the_routed_link_attaches_should_go,
                                      }
                         },
                         {
                           'event'  : 'receiver_distribution_ok',
                           'action' : {'fn'    : 'none',
                                       'done'  : 'succeed'
                                      }
                         }
                       ]

        test = RoutingTest ( name,
                             self.container_ids[0],
                             self.A_addr,  # all senders are attached here
                             routers,
                             self.linkroute_prefix,
                             addr_suffix,
                             instructions,
                             where_to_make_connections,
                             n_local_containers,
                             n_remote_routers
                           )
        test.run ( )
        self.assertEqual ( None, test.error )



    @SkipIfNeeded(DistributionSkipMapper.skip['test_18'], 'Test skipped during development.')
    def test_18_linkroute_linear_all_C ( self ) :
        name='test_18'
        """
        This test should route all senders' link-attaches
        to the remote connections on router C.
        """

        addr_suffix = "addr_18"

        # Choose which routers to give the test.
        # This choice controls topology.  ABC is linear.
        routers = ( self.A_route_container_addr,
                    self.B_route_container_addr,
                    self.C_route_container_addr
                  )

        # NOTE : about these 3-tuples.
        # The positions in these tuples correspond to the routers passed
        # in to the test: ( router_1, router_2, router_3 )
        # router_1 is always the 'local' one -- the one where the
        # test make its senders.

        # Tell the test on which routers to make its link-container cnxs.
        where_to_make_connections                = ( 0, 0, 2 )
        where_the_routed_link_attaches_should_go = ( 0, 0, 4 )

        # Tell the test how to check for the address being ready.
        n_local_containers = 0
        n_remote_routers   = 1

        #-----------------------------------------------------------------------
        # This is the instruction-list that the test looks at as various
        # milestones are met during testing. If a given event happens,
        # and if it matches the event in the current step of the instructions,
        # then the test will execute the action in the current step, and
        # advance to the next.
        # These instructions lists make the test more flexible, so I can get
        # different behavior without writing *almost* the same code mutiple
        # times.
        #-----------------------------------------------------------------------

        # note: if 'done' is present in an action, it always means 'succeed now'.
        # If there had been a failure, that would have been caught in an
        # earlier part of the action.

        instructions = [
                         # Once the link-routable address is ready to use in
                         # the router network, create 4 senders.
                         {
                           'event'  : 'address_ready',
                           'action' : { 'fn'   : 'make_senders',
                                         'arg' : 4
                                      }
                         },
                         # In this action, the list-argument to the function
                         # shows how we expect link-attach routes to be
                         # distributed: 4 to router B,
                         # none anywhere else.
                         {
                           'event'  : 'got_receivers',
                           'action' : { 'fn'   : 'check_receiver_distribution',
                                        'arg'  : where_the_routed_link_attaches_should_go
                                      }
                         },
                         {
                           'event'  : 'receiver_distribution_ok',
                           'action' : {'fn'    : 'none',
                                       'done'  : 'succeed'
                                      }
                         }
                       ]

        test = RoutingTest ( name,
                             self.container_ids[0],
                             self.A_addr,  # all senders are attached here
                             routers,
                             self.linkroute_prefix,
                             addr_suffix,
                             instructions,
                             where_to_make_connections,
                             n_local_containers,
                             n_remote_routers
                           )
        test.run ( )
        self.assertEqual ( None, test.error )


    @SkipIfNeeded(DistributionSkipMapper.skip['test_19'], 'Test skipped during development.')
    def test_19_linkroute_linear_kill ( self ) :
        name='test_19'
        """
        Start out as usual, making four senders and seeing their link-attaches
        routed to router A (local). But then kill the two route-container
        connections to router A, and make four more senders.  Their link-attaches
        should get routed to router B.
        """

        addr_suffix = "addr_19"

        # Choose which routers to give the test.
        # This choice controls topology.  ABC is linear.
        routers = ( self.A_route_container_addr,
                    self.B_route_container_addr,
                    self.C_route_container_addr
                  )

        # NOTE : about these 3-tuples.
        # The positions in these tuples correspond to the routers passed
        # in to the test: ( router_1, router_2, router_3 )
        # router_1 is always the 'local' one -- the one where the
        # test make its senders.

        # Tell the test on which routers to make its link-container cnxs.
        where_to_make_connections = ( 2, 2, 2 )

        # And where to expect the resulting link-attaches to end up.
        first_4                   = ( 4, 0, 0 )   # All go to A
        second_4                  = ( 4, 4, 0 )   # New ones go to B
        third_4                   = ( 4, 4, 4 )   # New ones go to C

        # Tell the test how to check for the address being ready.
        n_local_containers = 0
        n_remote_routers   = 2

        #-----------------------------------------------------------------------
        # This is the instruction-list that the test looks at as various
        # milestones are met during testing. If a given event happens,
        # and if it matches the event in the current step of the instructions,
        # then the test will execute the action in the current step, and
        # advance to the next.
        # These instructions lists make the test more flexible, so I can get
        # different behavior without writing *almost* the same code mutiple
        # times.
        #-----------------------------------------------------------------------

        # note: if 'done' is present in an action, it always means 'succeed now'.
        # If there had been a failure, that would have been caught in an
        # earlier part of the action.

        instructions = [
                         # Once the link-routable address is ready to use in
                         # the router network, create 4 senders.
                         {
                           'event'  : 'address_ready',
                           'action' : { 'fn'   : 'make_senders',
                                        'arg'  : 4
                                      }
                         },
                         # Check the distribution of the first four
                         # link-attach routings, then go immediately
                         # to the next instruction step.
                         {
                           'event'  : 'got_receivers',
                           'action' : { 'fn'   : 'check_receiver_distribution',
                                        'arg'  : first_4
                                      }
                         },
                         # After we see that the first 4 senders have
                         # had their link-attaches routed to the right place,
                         # (which will be router A), close all route-container
                         # connections to that router.
                         {
                           'event'  : 'receiver_distribution_ok',
                           'action' : { 'fn'   : 'kill_connections',
                                        'arg'  : 0
                                      }
                         },
                         # Once the route-container connections on A are
                         # closed, make 4 new senders
                         {
                           'event'  : 'connections_closed',
                           'action' : { 'fn'   : 'make_senders',
                                        'arg'  : 4
                                      }
                         },
                         # The link-attaches from these 4 new senders
                         # should now all have gone to the route-container
                         # connections on router B.
                         {
                           'event'  : 'got_receivers',
                           'action' : { 'fn'   : 'check_receiver_distribution',
                                        'arg'  : second_4
                                      }
                         },
                         # If we receive confirmation that the link-attaches
                         # have gone to the right place, now we kill
                         # connections on router B.
                         {
                           'event'  : 'receiver_distribution_ok',
                           'action' : { 'fn'   : 'kill_connections',
                                        'arg'  : 1
                                      }
                         },
                         # Once the route-container connections on B are
                         # closed, make 4 new senders
                         {
                           'event'  : 'connections_closed',
                           'action' : { 'fn'   : 'make_senders',
                                        'arg'  : 4
                                      }
                         },
                         # The link-attaches from these 4 new senders
                         # should now all have gone to the route-container
                         # connections on router C.
                         {
                           'event'  : 'got_receivers',
                           'action' : { 'fn'   : 'check_receiver_distribution',
                                        'arg'  : third_4
                                      }
                         },
                         # If we receive confirmation that the link-attaches
                         # have gone to the right place, we succeed.
                         {
                           'event'  : 'receiver_distribution_ok',
                           'action' : { 'fn'   : 'none',
                                        'done' : 'succeed'
                                      }
                         }
                       ]

        test = RoutingTest ( name,
                             self.container_ids[0],
                             self.A_addr,  # all senders are attached here
                             routers,
                             self.linkroute_prefix,
                             addr_suffix,
                             instructions,
                             where_to_make_connections,
                             n_local_containers,
                             n_remote_routers
                           )
        test.run ( )
        self.assertEqual ( None, test.error )



    @SkipIfNeeded(DistributionSkipMapper.skip['test_20'], 'Test skipped during development.')
    def test_20_linkroute_mesh_all_local ( self ) :
        name='test_20'
        #
        #                c           c
        # senders --->   A --------- B
        #                \         /
        #                 \       /
        #                  \     /
        #                   \   /
        #                    \ /
        #                     D
        #                     c

        # 'c' indicates that I make connections to the route-container
        # listeners at the marked routers.

        # This test should route all senders' link-attaches
        # to the local containers on router A.

        addr_suffix = "addr_20"

        # Choose which routers to give the test.
        # This choice controls topology.  ABD is triangular,
        # i.e. 3-mesh.
        routers = ( self.A_route_container_addr,
                    self.B_route_container_addr,
                    self.D_route_container_addr
                  )

        # NOTE : about these 3-tuples.
        # The positions in these tuples correspond to the routers passed
        # in to the test: ( router_1, router_2, router_3 )
        # router_1 is always the 'local' one -- the one where the
        # test make its senders.

        # Tell the test on which routers to make its link-container cnxs.
        where_to_make_connections                = ( 2, 2, 2 )
        where_the_routed_link_attaches_should_go = ( 4, 0, 0 )

        # Tell the test how to check for the address being ready.
        n_local_containers = 2
        n_remote_routers   = 2

        #-----------------------------------------------------------------------
        # This is the instruction-list that the test looks at as various
        # milestones are met during testing. If a given event happens,
        # and if it matches the event in the current step of the instructions,
        # then the test will execute the action in the current step, and
        # advance to the next.
        # These instructions lists make the test more flexible, so I can get
        # different behavior without writing *almost* the same code mutiple
        # times.
        #-----------------------------------------------------------------------

        # note: if 'done' is present in an action, it always means 'succeed now'.
        # If there had been a failure, that would have been caught in an
        # earlier part of the action.

        instructions = [
                         # Once the link-routable address is ready to use in
                         # the router network, create 4 senders.
                         {
                           'event'  : 'address_ready',
                           'action' : { 'fn'   : 'make_senders',
                                         'arg' : 4
                                      }
                         },
                         # In this action, the list-argument to the function
                         # shows how we expect link-attach routes to be
                         # distributed: 4 to the first router,
                         # none to the other two.
                         {
                           'event'  : 'got_receivers',
                           'action' : { 'fn'   : 'check_receiver_distribution',
                                        'arg'  : where_the_routed_link_attaches_should_go,
                                      }
                         },
                         {
                           'event'  : 'receiver_distribution_ok',
                           'action' : {'fn'    : 'none',
                                       'done'  : 'succeed'
                                      }
                         }
                       ]

        test = RoutingTest ( name,
                             self.container_ids[0],
                             self.A_addr,  # all senders are attached here
                             routers,
                             self.linkroute_prefix,
                             addr_suffix,
                             instructions,
                             where_to_make_connections,
                             n_local_containers,
                             n_remote_routers
                           )
        test.run ( )
        self.assertEqual ( None, test.error )


    @SkipIfNeeded(DistributionSkipMapper.skip['test_21'], 'Test skipped during development.')
    def test_21_linkroute_mesh_nonlocal ( self ) :
        name='test_21'
        #
        #                            c
        # senders --->   A --------- B
        #                 \         /
        #                  \       /
        #                   \     /
        #                    \   /
        #                     \ /
        #                      D
        #                      c

        # 'c' indicates that I make connections to the route-container
        # listeners at the marked routers.

        # This test should split all senders' link-attaches
        # between the connections on routers B and D.

        addr_suffix = "addr_21"

        # Choose which routers to give the test.
        # This choice controls topology.  ABD is triangular
        # i.e. 3-mesh.
        routers = ( self.A_route_container_addr,
                    self.B_route_container_addr,
                    self.D_route_container_addr
                  )

        # NOTE : about these 3-tuples.
        # The positions in these tuples correspond to the routers passed
        # in to the test: ( router_1, router_2, router_3 )
        # router_1 is always the 'local' one -- the one where the
        # test make its senders.

        # Tell the test on which routers to make its link-container cnxs.
        where_to_make_connections                = ( 0, 2, 2 )
        where_the_routed_link_attaches_should_go = ( 0, 2, 2 )

        # Tell the test how to check for the address being ready.
        n_local_containers = 0
        n_remote_routers   = 2

        #-----------------------------------------------------------------------
        # This is the instruction-list that the test looks at as various
        # milestones are met during testing. If a given event happens,
        # and if it matches the event in the current step of the instructions,
        # then the test will execute the action in the current step, and
        # advance to the next.
        # These instructions lists make the test more flexible, so I can get
        # different behavior without writing *almost* the same code mutiple
        # times.
        #-----------------------------------------------------------------------

        # note: if 'done' is present in an action, it always means 'succeed now'.
        # If there had been a failure, that would have been caught in an
        # earlier part of the action.

        instructions = [
                         # Once the link-routable address is ready to use in
                         # the router network, create 4 senders.
                         {
                           'event'  : 'address_ready',
                           'action' : { 'fn'   : 'make_senders',
                                         'arg' : 4
                                      }
                         },
                         # In this action, the list-argument to the function
                         # shows how we expect link-attach routes to be
                         # distributed: 4 to router B,
                         # none anywhere else.
                         {
                           'event'  : 'got_receivers',
                           'action' : { 'fn'   : 'check_receiver_distribution',
                                        'arg'  : where_the_routed_link_attaches_should_go,
                                      }
                         },
                         {
                           'event'  : 'receiver_distribution_ok',
                           'action' : {'fn'    : 'none',
                                       'done'  : 'succeed'
                                      }
                         }
                       ]

        test = RoutingTest ( name,
                             self.container_ids[0],
                             self.A_addr,  # all senders are attached here
                             routers,
                             self.linkroute_prefix,
                             addr_suffix,
                             instructions,
                             where_to_make_connections,
                             n_local_containers,
                             n_remote_routers
                           )
        test.run ( )
        self.assertEqual ( None, test.error )




    @SkipIfNeeded(DistributionSkipMapper.skip['test_22'], 'Test skipped during development.')
    def test_22_linkroute_mesh_kill ( self ) :
        name='test_22'

        #               c           c
        # senders --->   A --------- B
        #                 \         /
        #                  \       /
        #                   \     /
        #                    \   /
        #                     \ /
        #                      D
        #                      c

        # 'c' indicates that I make connections to the route-container
        # listeners at the marked routers.

        addr_suffix = "addr_22"

        # Choose which routers to give the test.
        # This choice controls topology.  ABD is triangular
        # i.e. 3-mesh.
        routers = ( self.A_route_container_addr,
                    self.B_route_container_addr,
                    self.D_route_container_addr
                  )

        # NOTE : about these 3-tuples.
        # The positions in these tuples correspond to the routers passed
        # in to the test: ( router_1, router_2, router_3 )
        # router_1 is always the 'local' one -- the one where the
        # test make its senders.

        # Tell the test on which routers to make its link-container cnxs.
        where_to_make_connections = ( 2, 2, 2 )
        first_four                = ( 4, 0, 0 )
        second_four               = ( 4, 2, 2 )
        third_four                = ( 4, 2, 6 )

        # Tell the test how to check for the address being ready.
        n_local_containers = 1
        n_remote_routers   = 2

        #-----------------------------------------------------------------------
        # This is the instruction-list that the test looks at as various
        # milestones are met during testing. If a given event happens,
        # and if it matches the event in the current step of the instructions,
        # then the test will execute the action in the current step, and
        # advance to the next.
        # These instructions lists make the test more flexible, so I can get
        # different behavior without writing *almost* the same code mutiple
        # times.
        #-----------------------------------------------------------------------

        # note: if 'done' is present in an action, it always means 'succeed now'.
        # If there had been a failure, that would have been caught in an
        # earlier part of the action.

        instructions = [
                         # Once the link-routable address is ready to use in
                         # the router network, create 4 senders.
                         {
                           'event'  : 'address_ready',
                           'action' : { 'fn'   : 'make_senders',
                                         'arg' : 4
                                      }
                         },
                         # In this action, the list-argument to the function
                         # shows how we expect link-attach routes to be
                         # distributed: 4 to router B,
                         # none anywhere else.
                         {
                           'event'  : 'got_receivers',
                           'action' : { 'fn'   : 'check_receiver_distribution',
                                        'arg'  : first_four,
                                      }
                         },
                         # After we see that the first 4 senders have
                         # had their link-attaches routed to the right place,
                         # (which will be router A), close all route-container
                         # connections to that router.
                         {
                           'event'  : 'receiver_distribution_ok',
                           'action' : { 'fn'   : 'kill_connections',
                                        'arg'  : 0
                                      }
                         },
                         # Once the route-container connections on A are
                         # closed, make 4 new senders
                         {
                           'event'  : 'connections_closed',
                           'action' : { 'fn'   : 'make_senders',
                                        'arg'  : 4
                                      }
                         },
                         # The link-attaches from these 4 new senders
                         # should now all have gone to the route-container
                         # connections on router B.
                         {
                           'event'  : 'got_receivers',
                           'action' : { 'fn'   : 'check_receiver_distribution',
                                        'arg'  : second_four
                                      }
                         },
                         # If we receive confirmation that the link-attaches
                         # have gone to the right place, we ruthlessly
                         # kill the next set of connections. Will we stop at
                         # nothing to defeat this code ?!??!?
                         {
                           'event'  : 'receiver_distribution_ok',
                           'action' : { 'fn'   : 'kill_connections',
                                        'arg'  : 1
                                      }
                         },
                         # Once the route-container connections on B are
                         # closed, make 4 new senders
                         {
                           'event'  : 'connections_closed',
                           'action' : { 'fn'   : 'make_senders',
                                        'arg'  : 4
                                      }
                         },
                         # The link-attaches from these 4 new senders
                         # should now all have gone to the route-container
                         # connections on router C.
                         {
                           'event'  : 'got_receivers',
                           'action' : { 'fn'   : 'check_receiver_distribution',
                                        'arg'  : third_four
                                      }
                         },
                         # If we receive confirmation that the link-attaches
                         # have gone to the right place, we succeed.
                         {
                           'event'  : 'receiver_distribution_ok',
                           'action' : { 'fn'   : 'none',
                                        'done' : 'succeed'
                                      }
                         }
                       ]

        test = RoutingTest ( name,
                             self.container_ids[0],
                             self.A_addr,  # all senders are attached here
                             routers,
                             self.linkroute_prefix,
                             addr_suffix,
                             instructions,
                             where_to_make_connections,
                             n_local_containers,
                             n_remote_routers
                           )
        test.run ( )
        self.assertEqual ( None, test.error )


    @SkipIfNeeded(DistributionSkipMapper.skip['test_23'], 'Test skipped during development.')
    def test_23_waypoint ( self ) :
        name='test_23'
        test = WaypointTest ( name,
                              self.container_ids[1],
                              self.A_addr,
                              self.B_addr,
                              self.C_route_container_addr,
                              self.waypoint_prefix_1 + '.waypoint'
                            )
        test.run()
        self.assertEqual(None, test.error)


    @SkipIfNeeded(DistributionSkipMapper.skip['test_24'], 'Test skipped during development.')
    def test_24_serial_waypoint_test ( self ):
        name='test_24'
        test = SerialWaypointTest ( name,
                                    self.container_ids[2],
                                    self.A_addr,
                                    self.B_addr,
                                    self.C_route_container_addr,
                                    self.waypoint_prefix_2 + '.waypoint'
                                  )
        test.run()
        self.assertEqual(None, test.error)

    @SkipIfNeeded(DistributionSkipMapper.skip['test_25'], 'Test skipped during development.')
    def test_25_parallel_waypoint_test ( self ):
        name='test_25'
        test = ParallelWaypointTest ( name,
                                      self.container_ids[3],
                                      self.A_addr,
                                      self.B_addr,
                                      self.C_route_container_addr,
                                      self.waypoint_prefix_3 + '.waypoint'
                                    )
        test.run()
        self.assertEqual(None, test.error)





#================================================================
#     Tests
#================================================================

next_link_sequence = 1
def link_name():
    global next_link_sequence
    name = "link-name.%d" % next_link_sequence
    next_link_sequence += 1
    return name

class TargetedSenderTest ( MessagingHandler ):
    """
    A 'targeted' sender is one in which we tell the router what
    address we want to send to. (As opposed to letting the router
    pass back an address to us.)
    """
    def __init__ ( self, test_name, send_addr, recv_addr, destination ):
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
        self.test_name = test_name


    def timeout(self):
        self.error = "Timeout Expired: n_sent=%d n_received=%d n_accepted=%d" % \
                     (self.n_sent, self.n_received, self.n_accepted)
        self.send_conn.close()
        self.recv_conn.close()


    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))
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

    def __init__(self, test_name, send_addr, recv_addr):
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
        self.test_name  = test_name


    def timeout ( self ):
        self.error = "Timeout Expired: n_sent=%d n_received=%d n_accepted=%d" % \
                     (self.n_sent, self.n_received, self.n_accepted)
        self.send_conn.close()
        self.recv_conn.close()


    def on_start(self, event):
        self.timer     = event.reactor.schedule(TIMEOUT, TestTimeout(self))
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
    def __init__(self, test_name, client_addr, server_addr):
        super(DynamicReplyTo, self).__init__(prefetch=10)
        self.client_addr        = client_addr
        self.server_addr        = server_addr
        self.dest               = "closest.dynamicRequestResponse"
        self.error              = None
        self.server_receiver    = None
        self.client_receiver    = None
        self.client_sender      = None
        self.server_sender      = None
        self.n_expected         = 10
        self.n_sent             = 0
        self.received_by_server = 0
        self.received_by_client = 0
        self.test_name          = test_name
        self.server_receiver_ready = False
        self.client_receiver_ready = False
        self.reply_to_addr = None
        self.senders_created = False
        self.addr_check_timer = None
        self.addr_check_sender = None
        self.container = None
        self.num_attempts = 0
        self.addr_check_receiver = None


    def timeout(self):
        self.error = "Timeout Expired: n_sent=%d received_by_server=%d received_by_client=%d" % \
                     (self.n_sent, self.received_by_server, self.received_by_client)
        self.client_connection.close()
        self.server_connection.close()

    def address_check_timeout(self):
        self.addr_check_sender.send(self.addr_checker.make_address_query("M0" + self.dest))

    def bail(self):
        self.timer.cancel()
        self.server_receiver.close()
        self.client_receiver.close()
        self.addr_check_sender.close()
        self.addr_check_receiver.close()
        self.server_sender.close()
        self.client_sender.close()
        self.client_connection.close()
        self.server_connection.close()

    def on_start( self, event):
        self.timer             = event.reactor.schedule (TIMEOUT, TestTimeout(self))
        # separate connections to simulate client and server.
        self.client_connection = event.container.connect(self.client_addr)
        self.server_connection = event.container.connect(self.server_addr)
        self.server_receiver   = event.container.create_receiver(self.server_connection, self.dest)
        self.client_receiver   = event.container.create_receiver(self.client_connection, None, dynamic=True)
        self.addr_check_sender = event.container.create_sender(self.client_connection, "$management")
        self.container         = event.container
        self.addr_check_receiver = event.container.create_receiver(self.client_connection, dynamic=True)

    def create_senders(self):
        if not self.senders_created:
            self.senders_created = True
            self.client_sender = self.container.create_sender(self.client_connection, self.dest)
            self.server_sender = self.container.create_sender(self.server_connection, None)

    def on_link_opened(self, event):
        if event.receiver == self.addr_check_receiver:
            self.addr_checker = AddressChecker(self.addr_check_receiver.remote_source.address)
        if not self.server_receiver_ready and event.receiver == self.server_receiver:
            self.server_receiver_ready = True
        if not self.client_receiver_ready and event.receiver == self.client_receiver:
            self.client_receiver_ready = True
        if self.server_receiver_ready and self.client_receiver_ready:
            if self.num_attempts == 0:
                self.reply_to_addr = self.client_receiver.remote_source.address
                self.num_attempts += 1
                self.addr_check_timer = event.reactor.schedule(3, AddressCheckerTimeout(self))

    def on_sendable(self, event):
        if self.reply_to_addr == None:
            return

        if event.sender == self.client_sender:
            while event.sender.credit > 0 and self.n_sent < self.n_expected:
                # We send to server, and tell it how to reply to the client.
                request = Message ( body=self.n_sent,
                                    address=self.dest,
                                    reply_to=self.reply_to_addr )
                event.sender.send ( request )
                self.n_sent += 1


    def on_message(self, event):
        if event.receiver == self.addr_check_receiver:
            response = self.addr_checker.parse_address_query_response(event.message)
            # Create the senders if the address has propagated.
            if response.status_code == 200 and response.remoteCount == 1:
                self.create_senders()
            else:
                if self.num_attempts < 2:
                    self.num_attempts += 1
                    self.addr_check_timer = event.reactor.schedule(3, AddressCheckerTimeout(self))
                else:
                    self.error = "Address %s did not propagate to the router to which the sender is attached" % self.dest
                    self.bail()
                    return

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
                self.bail()


    def run(self):
        Container(self).run()




class LinkAttachRoutingCheckOnly ( MessagingHandler ):
    """
    """
    def __init__ ( self,
                   test_name,
                   container_id,
                   client_host,
                   linkroute_container_host,
                   linkroute_prefix,
                   addr_suffix
                 ):
        super ( LinkAttachRoutingCheckOnly, self ).__init__(prefetch=0)
        self.client_host               = client_host
        self.linkroute_container_host  = linkroute_container_host
        self.linkroute_prefix          = linkroute_prefix
        self.link_routable_address     = self.linkroute_prefix + '.' + addr_suffix

        self.client_cnx               = None
        self.linkroute_container_cnx  = None
        self.error                    = None
        self.client_sender            = None
        self.linkroute_check_timer    = None
        self.linkroute_check_receiver = None
        self.linkroute_check_sender   = None
        self.test_name                = test_name
        self.container_id             = container_id

        self.debug = False


    def debug_print ( self, message ) :
        if self.debug :
            print(message)


    def timeout ( self ):
        self.bail ( "Timeout Expired" )


    def address_check_timeout(self):
        self.debug_print ( "address_check_timeout -------------" )
        self.linkroute_check()


    def bail ( self, text ):
        self.debug_print ( "bail -------------" )
        self.error = text
        self.linkroute_container_cnx.close()
        self.client_cnx.close()
        self.timer.cancel()
        if self.linkroute_check_timer:
            self.linkroute_check_timer.cancel()


    def on_start(self, event):
        self.debug_print ( "on_start -------------" )
        self.timer        = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.client_cnx = event.container.connect(self.client_host)
        self.linkroute_container_cnx  = event.container.connect(self.linkroute_container_host)

        # The linkroute_check_receiver will receive the replies to my management queries
        # that check whether the network is ready. The way this works is, I declare the
        # receiver dynamic here.  That means that when the link for this receiver opens,
        # I will get a remote_source address for it. I then pass that address to the
        # Address Checker object, which uses that as the reply-to address for the queries
        # that it sends.
        self.linkroute_check_receiver = event.container.create_receiver(self.client_cnx, dynamic=True)
        self.linkroute_check_sender   = event.container.create_sender(self.client_cnx, "$management")


    def on_link_opened ( self, event ) :
        self.debug_print ( "on_link_opened -------------" )
        if event.receiver == self.linkroute_check_receiver:
            event.receiver.flow(30)
            # Because we created the linkroute_check_receiver 'dynamic', when it opens
            # it will have its address filled in. That is the address we want our
            # AddressChecker replies to go to.
            self.linkroute_checker = AddressChecker ( self.linkroute_check_receiver.remote_source.address )
            self.linkroute_check()


    def on_message(self, event):
        self.debug_print ( "on_message -------------" )
        if event.receiver == self.linkroute_check_receiver:
            # This is one of my route-readiness checking messages.
            response = self.linkroute_checker.parse_address_query_response(event.message)
            self.debug_print ( "    status_code: %d   remoteCount: %d   containerCount: %d" % ( response.status_code, response.remoteCount, response.containerCount ) )
            if response.status_code == 200 and (response.remoteCount + response.containerCount) > 0:
                # Step 3: got confirmation of link-attach knowledge fully propagated
                # to Nearside router.  Now we can make the client sender without getting
                # a No Path To Destination error.
                self.client_sender = event.container.create_sender(self.client_cnx, self.link_routable_address)
                # And we can quit checking.
                self.bail ( None )
            else:
                # If the latest check did not find the link-attack route ready,
                # schedule another check a little while from now.
                self.linkroute_check_timer = event.reactor.schedule(0.25, AddressCheckerTimeout(self))


    def linkroute_check ( self ):
        self.debug_print ( "linkroute_check -------------" )
        # Send the message that will query the management code to discover
        # information about our destination address. We cannot make our payload
        # sender until the network is ready.
        #
        # BUGALERT: We have to prepend the 'D' to this linkroute prefix
        # because that's what the router does internally.  Someday this
        # may change.
        self.linkroute_check_sender.send ( self.linkroute_checker.make_address_query("D" + self.linkroute_prefix) )


    def run(self):
        container = Container(self)
        container.container_id = self.container_id
        container.run()




class LinkAttachRouting ( MessagingHandler ):
    """
    There are two hosts: near, and far.  The far host is the one that
    the route container will connect to, and it will receive our messages.
    The near host is what our sender will attach to.
    """
    def __init__ ( self,
                   test_name,
                   container_id,
                   nearside_host,
                   farside_host,
                   linkroute_prefix,
                   addr_suffix
                 ):
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

        self.count        = 10
        self.n_sent       = 0
        self.n_rcvd       = 0
        self.n_settled    = 0
        self.test_name    = test_name
        self.container_id = container_id


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
        self.timer        = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.nearside_cnx = event.container.connect(self.nearside_host)

        self.farside_cnx = event.container.connect(self.farside_host)

        # The linkroute_check_receiver will receive the replies to my management queries
        # that check whether the network is ready. The way this works is, I declare the
        # receiver dynamic here.  That means that when the link for this receiver opens,
        # I will get a remote_source address for it. I then pass that address to the
        # Address Checker object, which uses that as the reply-to address for the queries
        # that it sends.
        self.linkroute_check_receiver = event.container.create_receiver(self.nearside_cnx, dynamic=True)
        self.linkroute_check_sender   = event.container.create_sender(self.nearside_cnx, "$management")


    def on_link_opened(self, event):
        if event.receiver:
            event.receiver.flow(self.count)
        if event.receiver == self.linkroute_check_receiver:
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
        container.container_id = self.container_id
        container.run()





class ClosestTest ( MessagingHandler ):
    """
    Test whether distance-based message routing works in a
    3-router network. The network may be linear or mesh,
    depending on which routers the caller gives us.

    (Illustration is a linear network.)

    sender -----> Router_1 -----> Router_2 -----> Router_3
                     |              |                |
                     v              v                v
                  rcvr_1_a       rcvr_2_a         rcvr_3_a
                  rcvr_1_b       rcvr_2_b         rcvr_3_b

    With a linear network of 3 routers, set up a sender on
    router_1, and then 2 receivers each on all 3 routers.

    """
    def __init__ ( self, test_name, router_1, router_2, router_3, addr_suffix,
                   print_debug=False ):
        super ( ClosestTest, self ).__init__(prefetch=0)
        self.error       = None
        self.router_1    = router_1
        self.router_2    = router_2
        self.router_3    = router_3
        self.addr_suffix = addr_suffix
        self.dest        = "closest/" + addr_suffix

        # This n_expected is actually the minimum number of messages
        # I will send.  The real number will be higher because some
        # will be released when I close some receivers.
        self.n_expected = 300
        self.one_third  = self.n_expected / 3

        self.n_received = 0

        self.count_1_a = 0
        self.count_1_b = 0
        self.count_2_a = 0
        self.count_2_b = 0
        self.count_3_a = 0
        self.count_3_b = 0

        self.addr_check_timer    = None
        self.addr_check_receiver = None
        self.addr_check_sender   = None
        self.bailed              = False
        self.test_name           = test_name

        self.sender = None
        self.n_sent_1 = 0
        self.n_sent_2 = 0
        self.n_sent_3 = 0

        self.recv_1_a_closed = False
        self.recv_1_b_closed = False
        self.recv_2_a_closed = False
        self.recv_2_b_closed = False

        self.first_check = True
        self.send_on_sendable = True

        self.print_debug = print_debug

    def timeout ( self ):
        sys.stdout.flush()
        self.bail ( "Timeout Expired " )


    def address_check_timeout(self):
        self.addr_check()


    def bail ( self, text ):
        self.timer.cancel()
        self.error = text
        self.send_cnx.close()
        self.cnx_1.close()
        self.cnx_2.close()
        self.cnx_3.close()
        if self.addr_check_timer:
            self.addr_check_timer.cancel()


    def on_start ( self, event ):
        self.timer    = event.reactor.schedule  ( TIMEOUT, TestTimeout(self) )
        self.send_cnx = event.container.connect ( self.router_1 )
        self.cnx_1    = event.container.connect ( self.router_1 )
        self.cnx_2    = event.container.connect ( self.router_2 )
        self.cnx_3    = event.container.connect ( self.router_3 )

        # Warning!
        # The two receiver-links on each router must be given
        # explicit distinct names, or we will in fact only get
        # one link.  And then wonder why receiver 2 on each
        # router isn't getting any messages.
        self.recv_1_a  = event.container.create_receiver  ( self.cnx_1, self.dest, name=link_name() )
        self.recv_1_b  = event.container.create_receiver  ( self.cnx_1, self.dest, name=link_name() )

        self.recv_2_a  = event.container.create_receiver  ( self.cnx_2,  self.dest, name=link_name() )
        self.recv_2_b  = event.container.create_receiver  ( self.cnx_2,  self.dest, name=link_name() )

        self.recv_3_a  = event.container.create_receiver  ( self.cnx_3,  self.dest, name=link_name() )
        self.recv_3_b  = event.container.create_receiver  ( self.cnx_3,  self.dest, name=link_name() )

        self.recv_1_a.flow ( self.n_expected )
        self.recv_2_a.flow ( self.n_expected )
        self.recv_3_a.flow ( self.n_expected )

        self.recv_1_b.flow ( self.n_expected )
        self.recv_2_b.flow ( self.n_expected )
        self.recv_3_b.flow ( self.n_expected )

        self.addr_check_receiver = event.container.create_receiver ( self.cnx_1, dynamic=True )
        self.addr_check_receiver.flow(100)
        self.addr_check_sender   = event.container.create_sender ( self.cnx_1, "$management" )

        self.m_sent_1 = False
        self.m_sent_2 = False
        self.m_sent_3 = False


    def on_link_opened(self, event):
        if event.receiver == self.addr_check_receiver:
            # my addr-check link has opened: make the addr_checker with the given address.
            self.addr_checker = AddressChecker ( self.addr_check_receiver.remote_source.address )
            self.addr_check()

    def on_message ( self, event ):
        if event.receiver == self.addr_check_receiver:
            # This is a response to one of my address-readiness checking messages.
            response = self.addr_checker.parse_address_query_response(event.message)
            if self.first_check:
                if response.status_code == 200 and response.subscriberCount == 2 and response.remoteCount == 2:
                    # now we know that we have two subscribers on attached router, and two remote
                    # routers that know about the address. The network is ready.
                    # Now we can make the sender without getting a
                    # "No Path To Destination" error.
                    self.sender = event.container.create_sender ( self.send_cnx, self.dest )

                    if not self.m_sent_1:
                        self.m_sent_1 = True
                        while self.n_sent_1 < self.one_third:
                            msg = Message(body="Hello, closest.",
                                          address=self.dest)
                            self.sender.send(msg)
                            self.n_sent_1 += 1
                        if self.print_debug:
                            print ("First hundred sent")

                    # And we can quit checking.
                    if self.addr_check_timer:
                        self.addr_check_timer.cancel()
                        self.addr_check_timer = None
                else:
                    # If the latest check did not find the link-attack route ready,
                    # schedule another check a little while from now.
                    self.addr_check_timer = event.reactor.schedule(3, AddressCheckerTimeout(self))
            else:
                if response.status_code == 200 and response.subscriberCount == 0 and response.remoteCount == 1:
                    if not self.m_sent_3:
                        self.m_sent_3 = True
                        while self.n_sent_2 < self.one_third:
                            msg = Message(body="Hello, closest.",
                                          address=self.dest)
                            self.sender.send(msg)
                            self.n_sent_2 += 1

                        if self.print_debug:
                            print("Third hundred sent")

                    if self.addr_check_timer:
                        self.addr_check_timer.cancel()
                        self.addr_check_timer = None
                else:
                    # If the latest check did not find the link-attack route ready,
                    # schedule another check a little while from now.
                    self.addr_check_timer = event.reactor.schedule(3,
                                                                   AddressCheckerTimeout(
                                                                       self))
        else :
            # This is a payload message.
            self.n_received += 1

            # Count the messages that have come in for
            # each receiver.
            if event.receiver == self.recv_1_a:
                self.count_1_a += 1
            elif event.receiver == self.recv_1_b:
                self.count_1_b += 1
            elif event.receiver == self.recv_2_a:
                self.count_2_a += 1
            elif event.receiver == self.recv_2_b:
                self.count_2_b += 1
            elif event.receiver == self.recv_3_a:
                self.count_3_a += 1
                if self.print_debug:
                    print ("self.count_3_a", self.count_3_a)
            elif event.receiver == self.recv_3_b:
                self.count_3_b += 1
                if self.print_debug:
                    print("self.count_3_b", self.count_3_b)

            if self.n_received == self.one_third:
                if self.print_debug:
                    print("First one third received")
                # The first one-third of messages should have gone exclusively
                # to the near receivers.  At this point we should have
                # no messages in the mid or far receivers.
                self.recv_1_a.close()
                self.recv_1_b.close()
                if (self.count_2_a + self.count_2_b + self.count_3_a + self.count_3_b) > 0 :
                    self.bail ( "error: routers 2 or 3 got messages before router 1 receivers were closed." )
                # Make sure both receivers got some messages.
                if (self.count_1_a < self.one_third/2 or  self.count_1_b < self.one_third/2) or (self.count_1_b != self.count_1_a):
                    self.bail ( "error: recv_1_a and  recv_1_b did not get equal number of messages" )

            elif self.n_received == 2 * self.one_third:
                if self.print_debug:
                    print("Second one third received")
                # The next one-third of messages should have gone exclusively
                # to the router_2 receivers.  At this point we should have
                # no messages in the far receivers.
                self.recv_2_a.close()
                self.recv_2_b.close()
                if (self.count_3_a + self.count_3_b) > 0 :
                    self.bail ( "error: router 3 got messages before 2 was closed." )
                # Make sure both receivers got some messages.
                if (self.count_2_a < self.one_third/2 or  self.count_2_b < self.one_third/2) or (self.count_2_b != self.count_2_a):
                    self.bail ( "error: recv_2_a and  recv_2_b did not get equal number of messages" )

            # By the time we reach the expected number of messages
            # we have closed the router_1 and router_2 receivers.  If the
            # router_3 receivers are empty at this point, something is wrong.
            if self.n_received >= self.n_expected :
                if self.print_debug:
                    print("Third one third received")
                if (self.count_3_a < self.one_third/2 or  self.count_3_b < self.one_third/2) or (self.count_3_b != self.count_3_a):
                    self.bail ( "error: recv_3_a and  recv_3_b did not get equal number of messages" )
                else:
                    self.bail ( None )

    def on_link_closed(self, event):
        if event.receiver == self.recv_1_b or event.receiver == self.recv_1_a:
            if event.receiver == self.recv_1_a:
                self.recv_1_a_closed = True
            if event.receiver == self.recv_1_b:
                self.recv_1_b_closed = True
            if self.recv_1_a_closed and self.recv_1_b_closed and not self.m_sent_2:
                if self.print_debug:
                    print ("self.recv_1_a_closed and self.recv_1_b_closed")

                self.m_sent_2 = True
                while self.n_sent_3 < self.one_third:
                    msg = Message(body="Hello, closest.",
                                  address=self.dest)
                    self.sender.send(msg)
                    self.n_sent_3 += 1
                if self.print_debug:
                    print("Second hundred sent")

        if event.receiver == self.recv_2_a or event.receiver == self.recv_2_b:
            if event.receiver == self.recv_2_a:
                self.recv_2_a_closed = True
            if event.receiver == self.recv_2_b:
                self.recv_2_b_closed = True
            if self.recv_2_a_closed and self.recv_2_b_closed:
                self.first_check = False
                if self.print_debug:
                    print ("self.recv_2_a_closed and self.recv_2_b_closed")
                self.addr_check()

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
    def __init__ ( self,
                   test_name,
                   router_1,
                   router_2,
                   router_3,
                   addr_suffix,
                   total_messages,
                   expected_1,
                   expected_2,
                   expected_3,
                   slop,
                   omit_middle_receiver
                ):
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
        self.test_name      = test_name


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
        self.timer    = event.reactor.schedule  ( TIMEOUT, TestTimeout(self) )
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

            if   event.receiver == self.recv_1:
                self.count_1 += 1
            elif event.receiver == self.recv_2:
                self.count_2 += 1
            elif event.receiver == self.recv_3:
                self.count_3 += 1

            # I do not check for count_1 + count_2 + count_3 == total,
            # because it always will be due to how the code counts things.
            if self.n_received == self.total_messages:
                if abs(self.count_1 - self.expected_1) > self.slop or \
                   abs(self.count_2 - self.expected_2) > self.slop or \
                   abs(self.count_3 - self.expected_3) > self.slop  :
                    self.bail ( "expected: ( %d, %d, %d )  got: ( %d, %d, %d )" % (self.expected_1, self.expected_2, self.expected_3, self.count_1, self.count_2, self.count_3) )
                else:
                    self.bail ( None ) # All is well.


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





class MulticastTest ( MessagingHandler ):
    """
    Using multicast, we should see all receivers get everything,
    whether the topology is linear or mesh.
    """
    def __init__ ( self,
                   test_name,
                   router_1,
                   router_2,
                   router_3,
                   addr_suffix
                 ):
        super ( MulticastTest, self ).__init__(prefetch=0)
        self.error       = None
        self.router_1    = router_1
        self.router_2    = router_2
        self.router_3    = router_3
        self.addr_suffix = addr_suffix
        self.dest        = "multicast/" + addr_suffix

        self.n_to_send = 100
        self.n_sent    = 0

        self.n_received = 0

        self.count_1_a = 0
        self.count_1_b = 0
        self.count_2_a = 0
        self.count_2_b = 0
        self.count_3_a = 0
        self.count_3_b = 0

        self.addr_check_timer    = None
        self.addr_check_receiver = None
        self.addr_check_sender   = None
        self.sender              = None
        self.bailed              = False
        self.test_name           = test_name

    def timeout ( self ):
        self.bail ( "Timeout Expired " )


    def address_check_timeout(self):
        self.addr_check()


    def bail ( self, text ):
        self.timer.cancel()
        self.error = text
        self.send_cnx.close()
        self.cnx_1.close()
        self.cnx_2.close()
        self.cnx_3.close()
        if self.addr_check_timer:
            self.addr_check_timer.cancel()


    def on_start ( self, event ):
        self.timer    = event.reactor.schedule  ( TIMEOUT, TestTimeout(self) )
        self.send_cnx = event.container.connect ( self.router_1 )
        self.cnx_1    = event.container.connect ( self.router_1 )
        self.cnx_2    = event.container.connect ( self.router_2 )
        self.cnx_3    = event.container.connect ( self.router_3 )

        # Warning!
        # The two receiver-links on each router must be given
        # explicit distinct names, or we will in fact only get
        # one link.  And then wonder why receiver 2 on each
        # router isn't getting any messages.
        self.recv_1_a  = event.container.create_receiver  ( self.cnx_1, self.dest, name=link_name() )
        self.recv_1_b  = event.container.create_receiver  ( self.cnx_1, self.dest, name=link_name() )

        self.recv_2_a  = event.container.create_receiver  ( self.cnx_2,  self.dest, name=link_name() )
        self.recv_2_b  = event.container.create_receiver  ( self.cnx_2,  self.dest, name=link_name() )

        self.recv_3_a  = event.container.create_receiver  ( self.cnx_3,  self.dest, name=link_name() )
        self.recv_3_b  = event.container.create_receiver  ( self.cnx_3,  self.dest, name=link_name() )

        self.recv_1_a.flow ( self.n_to_send )
        self.recv_2_a.flow ( self.n_to_send )
        self.recv_3_a.flow ( self.n_to_send )

        self.recv_1_b.flow ( self.n_to_send )
        self.recv_2_b.flow ( self.n_to_send )
        self.recv_3_b.flow ( self.n_to_send )

        self.addr_check_receiver = event.container.create_receiver ( self.cnx_1, dynamic=True )
        self.addr_check_sender   = event.container.create_sender ( self.cnx_1, "$management" )


    def on_link_opened(self, event):
        if event.receiver:
            event.receiver.flow ( self.n_to_send )
        if event.receiver == self.addr_check_receiver:
            # my addr-check link has opened: make the addr_checker with the given address.
            self.addr_checker = AddressChecker ( self.addr_check_receiver.remote_source.address )
            self.addr_check()


    def on_sendable ( self, event ):
        if self.sender and self.n_sent < self.n_to_send :
            msg = Message ( body     = "Hello, closest.",
                            address  = self.dest
                          )
            dlv = self.sender.send ( msg )
            self.n_sent += 1
            dlv.settle()


    def on_message ( self, event ):

        #if self.bailed is True :
        #    return

        if event.receiver == self.addr_check_receiver:
            # This is a response to one of my address-readiness checking messages.
            response = self.addr_checker.parse_address_query_response(event.message)
            if response.status_code == 200 and response.subscriberCount == 2 and response.remoteCount == 2:
                # now we know that we have two subscribers on attached router, and two remote
                # routers that know about the address. The network is ready.
                # Now we can make the sender without getting a
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

            # Count the messages that have come in for
            # each receiver.
            if   event.receiver == self.recv_1_a:
                self.count_1_a += 1
            elif event.receiver == self.recv_1_b:
                self.count_1_b += 1
            elif event.receiver == self.recv_2_a:
                self.count_2_a += 1
            elif event.receiver == self.recv_2_b:
                self.count_2_b += 1
            elif event.receiver == self.recv_3_a:
                self.count_3_a += 1
            elif event.receiver == self.recv_3_b:
                self.count_3_b += 1

            if self.n_received >= 6 * self.n_to_send :
                # In multicast, everybody gets everything.
                # Our reception count should be 6x our send-count,
                # and all receiver-counts should be equal.
                if self.count_1_a == self.count_1_b and self.count_1_b == self.count_2_a and self.count_2_a == self.count_2_b and self.count_2_b == self.count_3_a and self.count_3_a == self.count_3_b :
                    self.bail ( None )
                    self.bailed = True
                else:
                    self.bail ( "receivers not equal: %d %d %d %d %d %d" % (self.count_1_a, self.count_1_b, self.count_2_a, self.count_2_b, self.count_3_a, self.count_3_b) )
                    self.bailed = True



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




class RoutingTest ( MessagingHandler ):
    """
    Accept a network of three routers -- either linear or triangular,
    depending on what the caller chooses -- make some senders, and see
    where the links go. This test may also kill some connections, make
    some more senders, and then see where *their* link-attaches get
    routed. This test's exact behavior is determined by the list of
    instructions that are passed in by the caller, each instruction being
    executed when some milestone in the test is met.

    """
    # NOTE that no payload messages are sent in this test! I send some
    #      management messages to see when the router network is ready for
    #      me, but other than that, all I care about is the link-attaches
    #      that happen each time I make a sender -- and where they are
    #      routed to.

    # NOTE about STEP comments: take a look at comments marked with the
    #      word STEP. These will show you the order in which things happen,
    #      up to the point where it becomes dependent on the instruction
    #      list that is passed in from the caller.

    def __init__ ( self,
                   test_name,
                   container_id,
                   sender_host,
                   route_container_addrs,
                   linkroute_prefix,
                   addr_suffix,
                   instructions,
                   where_to_make_connections,
                   n_local_containers,
                   n_remote_routers
                 ):
        super ( RoutingTest, self ).__init__(prefetch=0)

        self.debug     = False
        self.test_name = test_name

        self.sender_host           = sender_host
        self.route_container_addrs = route_container_addrs
        self.linkroute_prefix      = linkroute_prefix
        self.link_routable_address = self.linkroute_prefix + '.' + addr_suffix

        self.instructions = instructions
        self.current_step_index = 0
        self.event_injector = EventInjector()

        # This test uses the event injector feature of the reactor
        # to raise its own events, which then interact with the list
        # of instructions sent to us by the caller -- allowing this
        # code to execute several different test behaviors.
        self.address_ready_event            = ApplicationEvent("address_ready")
        self.got_receivers_event            = ApplicationEvent("got_receivers")
        self.receiver_distribution_ok_event = ApplicationEvent("receiver_distribution_ok")
        self.connections_closed_event       = ApplicationEvent("connections_closed")

        self.where_to_make_connections = where_to_make_connections
        self.sender_cnx                = None
        self.error                     = None
        self.linkroute_check_timer     = None
        self.linkroute_check_receiver  = None
        self.linkroute_check_sender    = None

        # These numbers tell me how to know when the
        # link-attach routable address is ready to use
        # in the router network.
        self.n_local_containers = n_local_containers
        self.n_remote_routers   = n_remote_routers

        self.receiver_count           = 0
        self.connections_closed       = 0
        self.connections_to_be_closed = 0
        self.expected_receivers       = 0
        self.linkroute_check_count    = 0
        self.done                     = False
        self.my_senders               = []

        # This list of dicts stores the number of route-container
        # connections that have been made to each of the three routers.
        # Each dict will hold one of these:
        #    < cnx : receiver_count >
        # for each cnx on that router.
        self.router_cnx_counts = [ dict(), dict(), dict() ]
        self.cnx_status        = dict()
        self.waiting_for_address_to_go_away = False
        self.sent_address_ready = False

        self.status = 'start up'
        self.container_id = container_id


    def debug_print ( self, message ) :
        if self.debug :
            print(message)


    # If this happens, the test is hanging.
    def timeout ( self ):
        self.start_shutting_down ( "Timeout Expired while: %s" % self.status )


    # This helps us periodically send management queries
    # to learn when our address os ready to be used on the
    # router network.
    def address_check_timeout(self):
        self.linkroute_check()


    #=================================================================
    # The address-checker is always running.
    # When this function gets us into the mode of starting
    # to shut down, then we look for the linkroutable address
    # to go away -- until there are no local or remote receivers
    # for it.  Only then will we finish.
    # If we do not do this, then this test will often interfere
    # with later tests (if one is run immediately after).
    # The next test will often (like, 20% of the time) get spurious
    # no route to destination errors.
    #=================================================================
    def start_shutting_down ( self, text ):
        self.done = True
        self.error = text
        self.close_route_container_connections()
        self.waiting_for_address_to_go_away = True


    def finish ( self ):
        self.done = True
        self.sender_cnx.close()
        self.timer.cancel()
        if self.linkroute_check_timer:
            self.linkroute_check_timer.cancel()
        self.event_injector.close()


    def on_start ( self, event ):
        self.debug_print ( "\n\n%s ===========================================\n\n" % self.test_name )
        self.debug_print ( "on_start -------------" )
        self.timer = event.reactor.schedule ( TIMEOUT, TestTimeout(self) )
        event.reactor.selectable(self.event_injector)
        self.sender_cnx = event.container.connect(self.sender_host)

        # Instructions from on high tell us how many route-container
        # connections to make on each router. For each one that we
        # make, we store it in a dict for that router, and associate
        # the number 0 with it. That number will be incremented every
        # time that connection is awarded a receiver. (Every time it
        # gets a sender's link-attach routed to it.)
        self.status = "making route-container connections"

        # STEP 1 : make connection to the route-container listeners where
        #          we are told to by the tuple passed in from above.
        #          Also, prepare to count how many receiver-links come in
        #          on each of these connections.
        for router in range(len(self.where_to_make_connections)) :
            how_many_for_this_router = self.where_to_make_connections[router]
            for j in range(how_many_for_this_router) :
                route_container_addr = self.route_container_addrs[router]
                cnx = event.container.connect ( route_container_addr )
                # In the dict of connections and actual receiver
                # counts, store this cnx, and 0.
                self.router_cnx_counts[router][cnx] = 0
                self.cnx_status[cnx] = 1
                self.debug_print ( "on_start: made cnx %s on router %d" % ( str(cnx), router ) )

        # STEP 2 : Make a sender and receiver that we will use to tell when the router
        #          network is ready to handle our reoutable address. This sender will
        #          send management queries, and the receiver will receive the responses.
        #          BUT! we also don't want to sending these management messages before
        #          something is ready to receive them, So, we declare the receiver to be
        #          dynamic.  That means that when we receive the on_link_opened event for
        #          it, we will be handed its address -- which we will then use as the reply-to
        #          address for the management queries we send.
        self.linkroute_check_receiver = event.container.create_receiver ( self.sender_cnx, dynamic=True )
        self.linkroute_check_sender   = event.container.create_sender   ( self.sender_cnx, "$management" )


    #=================================================
    # custom event
    # The link-attach-routable address is ready
    # for use in the router network.
    #=================================================
    def on_address_ready ( self, event ):
        # STEP 5 : Our link-attach routable address now has the expected
        #          number of local and remote receivers. Open for business!
        #          Time to start making sender-links to this address, and
        #          see where their link-attaches get routed to.
        self.debug_print ( "on_address_ready -------------" )
        current_step = self.instructions [ self.current_step_index ]
        if current_step['event'] != 'address_ready' :
            self.start_shutting_down ( "out-of-sequence event: address_ready while expecting %s" % current_step['event'] )
        else :
            action = current_step['action']
            if action['fn'] == 'make_senders' :
                self.status = 'making senders'
                arg = int(action['arg'])
                self.make_senders ( arg )
                self.expected_receivers = arg
                self.receiver_count = 0
                self.current_step_index += 1
                self.debug_print ( "current step advance to %d" % self.current_step_index )
            else :
                self.start_shutting_down ( "on_address_ready: unexpected action fn %s" % action['fn'] )


    #=======================================================
    # custom event
    # STEP 7 : The correct number of receiver-links,
    #          corresponding to the number of senders that
    #          was created, have been received.
    #
    #          NOTE: this is the last STEP comment, because
    #                after this the behavior of the test
    #                changes based on the instruction list
    #                that it received from the caller.
    #=======================================================
    def on_got_receivers ( self, event ):

        if self.done :
            return

        self.debug_print ( "on_got_receivers -------------" )
        current_step = self.instructions [ self.current_step_index ]

        if current_step['event'] != 'got_receivers' :
            self.start_shutting_down ( "out-of-sequence event: got_receivers while expecting %s" % current_step['event'] )
        else :
          action = current_step['action']
          if action['fn'] != 'check_receiver_distribution' :
              self.start_shutting_down ( "on_got_receivers: unexpected action fn %s" % action['fn'] )
          else :
              self.status = "checking receiver distribution"
              error = self.check_receiver_distribution ( action['arg'] )
              if error :
                  self.debug_print ( "check_receiver_distribution error" )
                  self.start_shutting_down ( error )
              else:
                  self.debug_print ( "receiver_distribution_ok" )
                  self.event_injector.trigger ( self.receiver_distribution_ok_event )
                  self.current_step_index += 1
                  self.debug_print ( "current step advance to %d" % self.current_step_index )



    #=======================================================
    # custom event
    # The receiver links that we got after creating some
    # senders went to the right place.
    #=======================================================
    def on_receiver_distribution_ok ( self, event ) :
        self.debug_print ( "on_receiver_distribution_ok ------------" )
        current_step = self.instructions [ self.current_step_index ]

        if current_step['event'] != 'receiver_distribution_ok' :
            self.start_shutting_down ( "out-of-sequence event: receiver_distribution_ok while expecting %s" % current_step['event'] )
        else :
            action = current_step['action']
            if action['fn'] == 'none' :
                self.debug_print ( "on_receiver_distribution_ok: test succeeding." )
                self.start_shutting_down ( None )
            elif action['fn'] == 'kill_connections' :
                router = int(action['arg'])
                self.connections_to_be_closed = 2
                self.connections_closed = 0
                self.debug_print ( "on_receiver_distribution_ok: killing %d connections on router %d" % (self.connections_to_be_closed, router ) )
                self.close_route_container_connections_on_router_n ( router )
                self.current_step_index += 1
                self.debug_print ( "current step advance to %d" % self.current_step_index )
            else :
                self.start_shutting_down ( "on_receiver_distribution_ok: unexpected action fn %s" % action['fn'] )


    #=======================================================
    # custom event
    # We were told to close the connections on a router
    # and now all those connections have been closed.
    #=======================================================
    def on_connections_closed ( self, event ) :
        self.debug_print ( "on_connections_closed ------------" )
        current_step = self.instructions [ self.current_step_index ]

        if current_step['event'] != 'connections_closed' :
            self.start_shutting_down ( "out-of-sequence event: connections_closed while expecting %s" % current_step['event'] )
        else :
            action = current_step['action']
            if action['fn'] == 'make_senders' :
                self.status = 'making senders'
                arg = int(action['arg'])
                self.make_senders ( arg )
                self.expected_receivers = arg
                self.receiver_count = 0
                self.debug_print ( "now expecting %d new receivers." % self.expected_receivers )
                self.current_step_index += 1
                self.debug_print ( "current step advance to %d" % self.current_step_index )
            else :
                self.start_shutting_down ( "on_connections_closed: unexpected action fn %s" % action['fn'] )


    def print_receiver_distribution ( self ) :
        print("receiver distribution:")
        for router in range(len(self.router_cnx_counts)) :
            print("    router %s" % router)
            cnx_dict = self.router_cnx_counts[router]
            for cnx in cnx_dict :
                print("        cnx: %s receivers: %s"
                      % (cnx, cnx_dict[cnx]))


    def get_receiver_distribution ( self ) :
        threeple = ()
        for router in range(len(self.router_cnx_counts)) :
            cnx_dict = self.router_cnx_counts[router]
            sum_for_this_router = 0
            for cnx in cnx_dict :
                sum_for_this_router += cnx_dict[cnx]
            threeple = threeple + ( sum_for_this_router, )
        return threeple

    #=====================================================
    # Check the count of how many receivers came in for
    # each connection compared to what was expected.
    #=====================================================
    def check_receiver_distribution ( self, expected_receiver_counts ) :
        self.debug_print ( "check_receiver_distribution expecting: %s" % str(expected_receiver_counts) )
        if self.debug :
            self.print_receiver_distribution()
        for router in range(len(self.router_cnx_counts)) :
            cnx_dict = self.router_cnx_counts[router]
            # Sum up all receivers for this router.
            actual = 0
            for cnx in cnx_dict :
                receiver_count = cnx_dict[cnx]
                actual += receiver_count

            expected = expected_receiver_counts[router]
            if actual != expected :
                return "expected: %s -- got: %s" % ( str(expected_receiver_counts), str(self.get_receiver_distribution()) )
            router += 1
        self.debug_print ( "expected: %s -- got: %s" % ( str(expected_receiver_counts), str(self.get_receiver_distribution()) ) )
        return None


    def close_route_container_connections ( self ) :
        self.status = "closing route container connections"
        for router in range(len(self.router_cnx_counts)) :
            cnx_dict = self.router_cnx_counts[router]
            for cnx in cnx_dict :
                if self.cnx_status[cnx] :
                    cnx.close()


    def close_route_container_connections_on_router_n ( self, n ) :
        self.status = "closing route container connections on router %d" % n
        self.debug_print ( "close_route_container_connections_on_router_n %d" % n )
        cnx_dict = self.router_cnx_counts[n]
        for cnx in cnx_dict :
            if self.cnx_status[cnx] :
                cnx.close()


    #=====================================================================
    # When a new receiver is handed to us (because a link-attach from a
    # sender has been routed to one of our route-container connections)
    # increment the number associated with that connection.
    # Also indicate to the caller whether this was indeed one of the
    # route-container connections that we made.
    #=====================================================================
    def increment_router_cnx_receiver_count ( self, new_cnx ) :
        for router in range(len(self.router_cnx_counts)) :
            cnx_dict = self.router_cnx_counts[router]
            for cnx in cnx_dict :
                if cnx == new_cnx :
                    # This cnx has been awarded a new receiver.
                    cnx_dict[cnx] += 1
                    self.debug_print ( "receiver went to router %d" % router )
                    return True
        return False


    def this_is_one_of_my_connections ( self, test_cnx ) :
        for router in range(len((self.router_cnx_counts))) :
            cnx_dict = self.router_cnx_counts[router]
            for cnx in cnx_dict :
                if cnx == test_cnx :
                    return True
        return False


    def on_link_opened ( self, event ):
        self.debug_print ( "on_link_opened -------------" )
        if self.done :
          return

        if event.receiver == self.linkroute_check_receiver:
            # STEP 3 : the link for our address-checker is now opening and
            #          ready to do business. Store its remote source address
            #          in the Address Checker gadget. That is the reply-to
            #          address for our queries.  Also -- launch the first
            #          query.

            # If the linkroute readiness checker can't strike oil in 30
            # tries, we are seriously out of luck, and will soon time out.
            event.receiver.flow ( 30 )
            self.linkroute_checker = AddressChecker(self.linkroute_check_receiver.remote_source.address)
            self.linkroute_check()
        else :
          if event.receiver :
              # STEP 6 : This receiver-link has been given to us because
              # a link-attach from one of our senders got routed somewhere.
              # Note where it got routed to, and count it. This count will
              # be compared to what was expected. This comparison is the
              # purpose of this test.
              this_is_one_of_mine = self.increment_router_cnx_receiver_count ( event.receiver.connection )
              if this_is_one_of_mine :
                  self.receiver_count += 1
                  self.debug_print ( "on_link_opened: got %d of %d expected receivers." % (self.receiver_count, self.expected_receivers) )
                  if self.receiver_count == self.expected_receivers :
                      self.event_injector.trigger ( self.got_receivers_event )


    def on_connection_closed ( self, event ):
        self.debug_print ( "on_connection_closed -------------" )
        if self.this_is_one_of_my_connections ( event.connection ) :
            self.cnx_status[event.connection] = 0
            self.connections_closed += 1
            if self.connections_to_be_closed :
                self.debug_print ( "on_connection_closed : %d of %d closed : %s" % (self.connections_closed, self.connections_to_be_closed, str(event.connection)) )
                if self.connections_closed == self.connections_to_be_closed :
                    # Reset both of these counters here, because
                    # they are only used each time we get a 'close connections'
                    # instruction, to keep track of its progress.
                    self.connections_to_be_closed = 0
                    self.cconnections_closed      = 0
                    self.event_injector.trigger ( self.connections_closed_event )


    #=================================================
    # All senders get attached to the first router.
    #=================================================
    def make_senders ( self, n ):
        self.debug_print ( "making %d senders" % n )
        for i in range(n):
            sender = self.sender_container.create_sender ( self.sender_cnx,
                                                           self.link_routable_address,
                                                           name=link_name()
                                                         )
            self.my_senders.append ( sender )


    #=================================================================
    # The only messages I care about in this test are the management
    # ones I send to determine when the router network is ready
    # to start routing my sender-attaches.
    #=================================================================
    def on_message ( self, event ):
        self.debug_print ( "on_message -------------" )
        if event.receiver == self.linkroute_check_receiver:

            # STEP 4 : we have a response to our management query, to see
            #          whether our link-attach routable address is ready.
            #          The checker will parse it for us, and the caller of
            #          this test has told us how many local containers and
            #          how many remote receivers to expect for our address.
            response = self.linkroute_checker.parse_address_query_response ( event.message )
            self.linkroute_check_count += 1

            self.debug_print ( "on_message: got %d local %d remote" % (response.containerCount, response.remoteCount) )

            if self.done != True                                  and \
               response.status_code == 200                        and \
               response.containerCount >= self.n_local_containers and \
               response.remoteCount >= self.n_remote_routers :
                # We are at the start of the test, looking for the
                # address to be ready to use all over the network.
                # But we are going to keep running this checker until
                # the end of the test, so make sure we only send this
                # event once.
                if not self.sent_address_ready :
                    self.sender_container = event.container
                    self.event_injector.trigger ( self.address_ready_event )
                    self.status = "address ready"
                    self.sent_address_ready = True;
            elif self.done == True                   and \
                 response.status_code == 200         and \
                 self.waiting_for_address_to_go_away and \
                 response.containerCount == 0        and \
                 response.remoteCount == 0 :
                # We are at the end of the test, looking for the
                # address to be forgotten to use all over the network.
                self.finish ( )

            self.linkroute_check_timer = event.reactor.schedule ( 0.25, AddressCheckerTimeout(self))


    #==========================================================================
    # Send the message that will query the management code to discover
    # information about our destination address. We cannot make our payload
    # sender until the network is ready.
    #
    # BUGALERT: We have to prepend the 'D' to this linkroute prefix
    # because that's what the router does internally.  Someday this
    # may change.
    #==========================================================================
    def linkroute_check ( self ):
        self.status = "waiting for address to be ready"
        self.linkroute_check_sender.send ( self.linkroute_checker.make_address_query("D" + self.linkroute_prefix) )


    def run(self):
        container = Container(self)
        container.container_id = self.container_id
        container.run()






class WaypointTest ( MessagingHandler ):
    """
    Messages from a client sender to a client receiver are first
    diverted out of the router into a separate waypoint receiver,
    which stores them in a fifo.  This simulates reception by a
    broker or some other arbitrary process.
    The message then returns from the waypoint sender back to the
    router, and then arrives at the client receiver.
    """
    def __init__ ( self,
                   test_name,
                   container_id,
                   client_host_1,
                   client_host_2,
                   route_container_host,
                   destination
                 ):
        super(WaypointTest, self).__init__()
        self.client_host_1        = client_host_1
        self.client_host_2        = client_host_2
        self.route_container_host = route_container_host
        self.destination          = destination

        self.client_connection          = None
        self.route_container_connection = None
        self.error                      = None
        self.waypoint_sender            = None
        self.waypoint_receiver          = None
        self.waypoint_queue             = []
        self.container_id               = container_id

        self.messages_per_sender = 10

        self.senders   = [
                           {
                             'sender'  : None,
                             'to_send' : 0,
                             'n_sent'  : 0
                           },

                           {
                             'sender'  : None,
                             'to_send' : 0,
                             'n_sent'  : 0
                           }
                         ]
        self.receivers = [
                           {
                             'receiver'   : None,
                             'n_received' : 0
                           },

                           {
                             'receiver'   : None,
                             'n_received' : 0
                           }
                         ]

        self.n_sent     = 0
        self.n_rcvd     = 0
        self.n_thru     = 0
        self.n_senders  = 2
        self.n_expected = self.messages_per_sender * self.n_senders

        self.n_transitions = 0

        # Each message goes out from the client sender (1)
        # and finally in to the client receiver (2).
        # But in the meantime it goes into (3) and out of (4)
        # the waypoint.
        self.n_expected_transitions = self.messages_per_sender * self.n_senders * 4

        self.debug     = False
        self.test_name = test_name


    def timeout(self):
        self.bail ( "Timeout Expired: n_sent=%d n_rcvd=%d n_thru=%d" % (self.n_sent, self.n_rcvd, self.n_thru) )



    def bail ( self, text ):
        self.error = text
        self.route_container_connection.close()
        self.client_connection.close()
        self.timer.cancel()



    def debug_print ( self, message ) :
        if self.debug :
            print(message)



    def send_from_client ( self, sender, n_messages, sender_index ):
        n_sent = 0
        self.debug_print ( "send_from_client -------------------" );
        while sender.credit > 0 and n_sent < n_messages:
            n_sent      += 1
            self.n_sent += 1
            msg = Message ( body=n_sent )
            self.debug_print ( "    send_from_client -- sender: %d n_sent: %d" % ( sender_index, n_sent ) )
            sender.send ( msg )
            # We send from a client
            self.n_transitions += 1



    def send_from_waypoint ( self ):
        self.debug_print ( "send_from_waypoint ---------------------" )

        if len(self.waypoint_queue) <= 0 :
          self.debug_print ( "    waypoint queue is empty." )
          return

        while self.waypoint_sender.credit > 0 and len(self.waypoint_queue) > 0:
            m = self.waypoint_queue.pop()
            message_content_number = m.body
            self.debug_print ( "    send_from_waypoint num is %d " % message_content_number )
            self.waypoint_sender.send ( m )
            self.n_thru += 1
            # We send from a waypoint.
            self.n_transitions += 1



    def on_start ( self, event ):
        self.timer = event.reactor.schedule ( TIMEOUT, TestTimeout(self) )
        self.client_connection = event.container.connect ( self.client_host_1 )

        # Creating this connection is what gets things started.  When we make this
        # connection to a route container address, the router will look at our
        # containerId, and will at that time instantiate any associated autolinks.
        self.route_container_connection = event.container.connect ( self.route_container_host )

        self.debug_print ( "    creating clients for connection" )
        for i in range(len(self.senders)) :
            sender   = self.senders[i]
            receiver = self.receivers[i]

            sender['sender'] = event.container.create_sender ( self.client_connection,
                                                               self.destination,
                                                               name=link_name() )
            sender['to_send'] = self.messages_per_sender
            sender['n_sent']  = 0

            receiver['receiver'] = event.container.create_receiver ( self.client_connection,
                                                                     self.destination,
                                                                     name=link_name() )
            receiver['n_received'] = 0



    def on_link_opening(self, event):
        self.debug_print ( "on_link_opening -------------------------- " )
        if event.sender:
            self.debug_print ( "    sender: %s" % event.sender.remote_source.address )
            event.sender.source.address = event.sender.remote_source.address
            event.sender.open()
            if event.sender.remote_source.address == self.destination:
                self.debug_print ( "    that's one of my waypoint senders: %s" % self.destination )
                self.waypoint_sender = event.sender

        elif event.receiver:
            self.debug_print ( "    receiver: %s" % event.receiver.remote_target.address )
            event.receiver.target.address = event.receiver.remote_target.address
            event.receiver.open()
            if event.receiver.remote_target.address == self.destination:
                self.debug_print ( "    that's one of mine." )
                self.waypoint_receiver = event.receiver
                self.waypoint_receiver.flow ( 1000 )



    def on_sendable ( self, event ):
        self.debug_print ( "on_sendable ---------------------------- " )
        for i in range(len(self.senders)) :
            sender = self.senders[i]
            if sender['sender'] == event.sender :
                if sender['n_sent'] < sender['to_send'] :
                    self.send_from_client ( sender['sender'], sender['to_send'], i )
                    sender['n_sent'] = sender['to_send']
                    self.debug_print ( "    sent %d" % sender['n_sent'] )
                    return
        if event.sender == self.waypoint_sender :
            self.send_from_waypoint ( )


    def on_message ( self, event ):
        self.debug_print ( "on_message ---------------------------- " )
        if event.receiver == self.waypoint_receiver :
            self.debug_print ( "    waypoint receiver" )
            m = Message(body=event.message.body)
            # We receive to a waypoint.
            self.n_transitions += 1
            self.waypoint_queue.append(m)
            self.debug_print ( "    queue depth is now %d" % len(self.waypoint_queue) )
            self.send_from_waypoint ( )
        else :
            for i in range(len(self.receivers)) :
                self.debug_print ( "    client receiver" )
                receiver = self.receivers[i]
                if event.receiver == receiver['receiver'] :
                    message_content_number = event.message.body
                    receiver['n_received'] += 1
                    # We receive to a client.
                    self.n_transitions += 1
                    self.debug_print ( "    client receiver %d has %d messages." % ( i, receiver['n_received'] ) )
                    self.n_rcvd += 1
                    if self.n_rcvd >= self.n_expected and self.n_thru >= self.n_expected:
                        if self.n_transitions != self.n_expected_transitions :
                            self.bail ( "expected %d transitions, but got %d" % ( self.n_expected_transitions, self.n_transitions ) )
                        else :
                            self.debug_print ( "    success" )
                            self.bail ( None )


    def run(self):
        container = Container(self)
        container.container_id = self.container_id
        container.run()





class SerialWaypointTest ( MessagingHandler ):
    """
    Messages from a client sender on their way to a client receiver are
    first re-routed to two separate waypoint 'processes', in serial.
    The waypoint processes are simulated in this test by separate 'waypoint'
    receivers that store the messages in fifo lists, and separate 'waypoint'
    senders that pop them off the fifos and send them.  This simulates
    either a broker, or some arbitrary processing on the message.
    """
    def __init__ ( self,
                   test_name,
                   container_id,
                   client_host_1,
                   client_host_2,
                   route_container_host,
                   destination
                 ):
        super(SerialWaypointTest, self).__init__()
        self.client_host_1        = client_host_1
        self.client_host_2        = client_host_2
        self.route_container_host = route_container_host
        self.destination          = destination
        self.sender_connections   = []
        self.error                = None
        self.messages_per_sender  = 100
        self.container_id         = container_id

        self.route_container_connection = None


        # There are 2 sending clients and 2 receiving clients
        # only because I wanted to have more than 1, and 2
        # appeared to be the next available integer.
        # This has nothing to do with the fact that there are
        # 2 waypoints.
        self.senders = [
                         { 'sender'  : None,
                           'to_send' : self.messages_per_sender,
                           'n_sent'  : 0
                         },
                         { 'sender'  : None,
                           'to_send' : self.messages_per_sender,
                           'n_sent'  : 0
                         }
                       ]

        self.receivers = [
                           { 'receiver'   : None,
                             'n_received' : 0
                           },
                           { 'receiver'   : None,
                             'n_received' : 0
                           }
                         ]

        self.n_waypoint_senders   = 0
        self.n_waypoint_receivers = 0

        self.waypoints = [
                           { 'sender'     : None,
                             'n_sent'     : 0,
                             'receiver'   : None,
                             'n_received' : 0,
                             'queue'      : [],
                             'n_sent'     : 0,
                             'name'       : '1'
                           },
                           { 'sender'     : None,
                             'n_sent'     : 0,
                             'receiver'   : None,
                             'n_received' : 0,
                             'queue'      : [],
                             'n_sent'     : 0,
                             'name'       : '2'
                           }
                         ]

        self.n_sent = 0
        self.n_rcvd = 0
        self.n_thru = 0
        self.n_transitions = 0
        self.n_expected_received    = self.messages_per_sender * len(self.senders)

        # Each message is sent from one client sender, and finally received
        # by one client receiver.  In the meantime in goes into, and then
        # comes back out of, two separate waypoints.  That's a total of
        # six links -- or 'transitions' -- for each message.
        n_links_per_message = 2 + 2 * len(self.waypoints)
        self.n_expected_transitions = len(self.senders) * self.messages_per_sender * n_links_per_message

        self.debug     = False
        self.test_name = test_name


    def timeout(self):
        self.bail ( "Timeout Expired: n_sent=%d n_rcvd=%d n_thru=%d" % (self.n_sent, self.n_rcvd, self.n_thru) )


    def bail ( self, text ):
        self.error = text
        self.route_container_connection.close()
        for cnx in self.sender_connections :
          cnx.close()
        self.timer.cancel()


    def debug_print ( self, message ) :
        if self.debug :
            print(message)


    def send_from_client ( self, sender, n_messages, sender_index ):
        n_sent = 0
        while sender.credit > 0 and n_sent < n_messages:
            msg = Message ( body=n_sent )
            sender.send ( msg )
            n_sent             += 1
            self.n_sent        += 1
            self.n_transitions += 1
            self.debug_print ( "send_from_client -- sender: %d n_sent: %d" % ( sender_index, n_sent ) )



    def send_from_waypoint ( self, waypoint ):
        self.debug_print ( "send_from_waypoint ------------------------------" )

        while waypoint['sender'].credit > 0 and len(waypoint['queue']) > 0:
            m = waypoint['queue'].pop()
            message_content_number = m.body
            waypoint['sender'].send ( m )
            waypoint['n_sent'] += 1
            self.n_thru        += 1
            self.n_transitions += 1
            self.debug_print ( "send_from_waypoint %s is %d " % ( waypoint['name'], message_content_number) )



    def on_start ( self, event ):
        self.timer = event.reactor.schedule ( TIMEOUT, TestTimeout(self) )
        self.sender_connections.append ( event.container.connect(self.client_host_1) )
        self.sender_connections.append ( event.container.connect(self.client_host_2) )
        # Creating this connection is what gets things started.  When we make this
        # connection to a route container address, the router will look at our
        # containerId, and will at that time instantiate any associated autolinks.
        self.route_container_connection = event.container.connect ( self.route_container_host )


        for i in range(len(self.sender_connections)) :
            cnx = self.sender_connections[i]
            sender   = self.senders[i]
            receiver = self.receivers[i]

            sender['sender'] = event.container.create_sender ( cnx,
                                                               self.destination,
                                                               name=link_name() )
            sender['to_send'] = self.messages_per_sender
            sender['n_sent']  = 0

            receiver['receiver'] = event.container.create_receiver ( cnx,
                                                                     self.destination,
                                                                     name=link_name() )
            receiver['n_received'] = 0


    def on_link_opening ( self, event ):

        self.debug_print ( "on_link_opening -------------------------- " )

        if event.sender:
            self.debug_print ( "    sender: %s" % event.sender.remote_source.address )
            event.sender.source.address = event.sender.remote_source.address
            event.sender.open()
            if event.sender.remote_source.address == self.destination:
                if self.n_waypoint_senders < 2 :
                    self.debug_print ( "    store this as one of my waypoint senders." )
                    self.waypoints[self.n_waypoint_senders]['sender'] = event.sender
                    self.n_waypoint_senders += 1

        elif event.receiver:
            self.debug_print ( "    receiver: %s" % event.receiver.remote_target.address )
            event.receiver.target.address = event.receiver.remote_target.address
            event.receiver.open()
            if event.receiver.remote_target.address == self.destination:
                self.debug_print ( "    store this as one of my waypoint receivers." )
                if self.n_waypoint_receivers < 2 :
                    self.waypoints[self.n_waypoint_receivers]['receiver'] = event.receiver
                    self.n_waypoint_receivers += 1



    def on_sendable ( self, event ):
        self.debug_print ( "on_sendable ------------------------------" )
        for index in range(len(self.senders)) :
            sender = self.senders[index]
            if event.sender == sender['sender'] :
                self.debug_print ( "    client sender %d" % index )
                if sender['n_sent'] < sender['to_send'] :
                    self.debug_print ( "    sending %d" % sender['to_send'] )
                    self.send_from_client ( sender['sender'], sender['to_send'], index )
                    sender['n_sent'] = sender['to_send']  # n_sent = n_to_send
                else :
                    self.debug_print ( "    this sender is already finished." )
                return

        for j in range(len(self.waypoints)) :
            sender = self.waypoints[j]['sender']
            if event.sender == sender :
                self.debug_print ( "    waypoint_sender %d" % j )
                self.send_from_waypoint ( self.waypoints[j] )
                return


    def on_message(self, event):

        self.debug_print ( "on_message --------------------------- " )

        # Is this one of our client receivers ?
        for i in range(len(self.receivers)) :
            receiver = self.receivers[i]
            if event.receiver == receiver['receiver'] :
                receiver['n_received'] += 1
                self.n_transitions     += 1
                self.debug_print ("    client receiver %d has %d messages." % ( i, receiver['n_received'] ) )
                message_content_number = event.message.body
                self.n_rcvd += 1
                if self.n_rcvd >= self.n_expected_received and self.n_thru >= self.n_expected_received:
                    self.debug_print ( "DONE -- self.n_rcvd: %d   self.n_thru: %d" % ( self.n_rcvd, self.n_thru ) )
                    if self.debug :
                        self.report ( )
                    self.check_results_and_bail ( )
                    return

        # Is this one of our waypoint receivers ?
        for j in range(len(self.waypoints)) :
            waypoint = self.waypoints[j]
            if event.receiver == waypoint['receiver'] :
                m = Message ( body=event.message.body )
                waypoint [ 'queue' ].append ( m )
                waypoint [ 'n_received' ] += 1
                self.n_transitions        += 1
                self.debug_print ( "    message received at waypoint %d, queue depth is now %d" % (j, len(waypoint['queue'])))
                self.send_from_waypoint ( waypoint )



    def check_results_and_bail ( self ) :

        if self.n_expected_transitions != self.n_transitions :
            self.bail ( "total transitions were %d, but %d were expected." % ( self.n_transitions, self.n_expected_transitions ) )
            return

        mps                 = self.messages_per_sender
        n_senders           = len(self.senders)
        total_messages_sent = mps * n_senders

        # For total messages sent, the expected value and
        # the actual value must be the same.  The two receivers
        # may receive different numbers (although the total should
        # be correct) but each of the senders must send the expected
        # number of messages or something is wrong.
        for i in range(n_senders) :
            sndr = self.senders[i]
            if sndr['n_sent'] != mps :
              self.bail ( "sender %d sent %d messages instead of %d" % ( i, sndr['n_sent'], mps ) )
              return

        n_waypoints = len(self.waypoints)
        total_expected_waypoint_receptions = total_messages_sent * n_waypoints
        total_actual_waypoint_receptions   = 0

        for i in range(n_waypoints) :
            total_actual_waypoint_receptions += self.waypoints[i]['n_received']

        if total_actual_waypoint_receptions != total_expected_waypoint_receptions :
            self.bail ( "total waypoint receptions were %d, but %d were expected." % ( total_actual_waypoint_receptions, total_expected_waypoint_receptions) )
            return

        total_messages_received = 0
        for i in range(len(self.receivers)) :
            this_receiver_got = self.receivers[i]['n_received']
            total_messages_received += this_receiver_got

        if total_messages_received != total_messages_sent :
            self.bail ( "total_messages_received: %d but %d were expected." % (total_messages_received, total_messages_sent) )
            return

        self.debug_print ( "\nsuccess\n" )
        self.bail ( None )



    def report ( self ) :
        print("\n\n==========================================================\nreport\n")

        for i in range(len(self.senders)) :
            print("    client sender %d sent %d messages." % ( i, self.senders[i]['n_sent']))

        print("\n")

        for i in range(len(self.waypoints)) :
            print("    waypoint %d received %d messages." % ( i, self.waypoints[i]['n_received']))
            print("    waypoint %d sent     %d messages." % ( i, self.waypoints[i]['n_sent']))

        print("\n")

        for i in range(len(self.receivers)) :
            print("    client receiver %d received %d messages." % ( i, self.receivers[i]['n_received'] ))

        print("\nend report\n=========================================================\n\n")



    def run(self):
        container = Container(self)
        container.container_id = self.container_id
        container.run()




class ParallelWaypointTest ( MessagingHandler ):
    """
    Messages from a client sender on their way to a client receiver are
    first re-routed to one of two separate waypoint 'processes', in parallel.
    The waypoint processes are simulated in this test by separate 'waypoint'
    receivers that store the messages in fifo lists, and separate 'waypoint'
    senders that pop them off the fifos and send them.  This simulates
    either a broker, or some arbitrary processing on the message.
    """
    def __init__ ( self,
                   test_name,
                   container_id,
                   client_host_1,
                   client_host_2,
                   route_container_host,
                   destination
                 ):
        super ( ParallelWaypointTest, self ). __init__()
        self.client_host_1        = client_host_1
        self.client_host_2        = client_host_2
        self.route_container_host = route_container_host
        self.destination          = destination
        self.sender_connections   = []
        self.error                = None
        self.messages_per_sender  = 100
        self.container_id         = container_id

        self.route_container_connection = None

        self.senders = [
                         { 'sender'  : None,
                           'to_send' : self.messages_per_sender,
                           'n_sent'  : 0
                         },
                         { 'sender'  : None,
                           'to_send' : self.messages_per_sender,
                           'n_sent'  : 0
                         }
                       ]

        self.receivers = [
                           { 'receiver'   : None,
                             'n_received' : 0
                           },
                           { 'receiver'   : None,
                             'n_received' : 0
                           }
                         ]

        self.n_waypoint_senders   = 0
        self.n_waypoint_receivers = 0

        self.waypoints = [
                           { 'sender'     : None,
                             'n_sent'     : 0,
                             'receiver'   : None,
                             'n_received' : 0,
                             'queue'      : [],
                             'n_sent'     : 0,
                             'name'       : '1'
                           },
                           { 'sender'     : None,
                             'n_sent'     : 0,
                             'receiver'   : None,
                             'n_received' : 0,
                             'queue'      : [],
                             'n_sent'     : 0,
                             'name'       : '2'
                           }
                         ]

        self.n_sent = 0
        self.n_rcvd = 0
        self.n_thru = 0
        self.n_transitions = 0
        self.n_expected_received = self.messages_per_sender * len(self.senders)

        # Each message is sent from one client sender, and finally received
        # by one client receiver.  In the meantime in goes into, and then
        # comes back out of, ONE waypoint.  That's a total of
        # four links -- or 'transitions' -- for each message.
        n_links_per_message = 4
        self.n_expected_transitions = len(self.senders) * self.messages_per_sender * n_links_per_message

        self.debug = False

        self.test_name = test_name


    def timeout(self):
        self.bail ( "Timeout Expired: n_sent=%d n_rcvd=%d n_thru=%d" % (self.n_sent, self.n_rcvd, self.n_thru) )


    def bail ( self, text ):
        self.error = text
        self.route_container_connection.close()
        for cnx in self.sender_connections :
          cnx.close()
        self.timer.cancel()


    def debug_print ( self, message ) :
        if self.debug :
            print(message)


    def send_from_client ( self, sender, n_messages, sender_index ):
        n_sent = 0
        while sender.credit > 0 and n_sent < n_messages:
            msg = Message ( body=n_sent )
            sender.send ( msg )
            n_sent             += 1
            self.n_sent        += 1
            self.n_transitions += 1
            self.debug_print ( "send_from_client -- sender: %d n_sent: %d" % ( sender_index, n_sent ) )



    def send_from_waypoint ( self, waypoint ):
        self.debug_print ( "send_from_waypoint ------------------------------" )

        while waypoint['sender'].credit > 0 and len(waypoint['queue']) > 0:
            m = waypoint['queue'].pop()
            message_content_number = m.body
            waypoint['sender'].send ( m )
            waypoint['n_sent'] += 1
            self.n_thru        += 1
            self.n_transitions += 1
            self.debug_print ( "send_from_waypoint %s is %d " % ( waypoint['name'], message_content_number) )



    def on_start ( self, event ):
        self.timer = event.reactor.schedule ( TIMEOUT, TestTimeout(self) )
        self.sender_connections.append ( event.container.connect(self.client_host_1) )
        self.sender_connections.append ( event.container.connect(self.client_host_2) )
        # Creating this connection is what gets things started.  When we make this
        # connection to a route container address, the router will look at our
        # containerId, and will at that time instantiate any associated autolinks.
        # We will get an 'on_link_opening' for each of them.
        self.route_container_connection = event.container.connect ( self.route_container_host )

        for i in range(len(self.sender_connections)) :
            cnx = self.sender_connections[i]
            sender   = self.senders[i]
            receiver = self.receivers[i]

            sender['sender'] = event.container.create_sender ( cnx,
                                                               self.destination,
                                                               name=link_name() )
            sender['to_send'] = self.messages_per_sender
            sender['n_sent']  = 0
            receiver['receiver'] = event.container.create_receiver ( cnx,
                                                                     self.destination,
                                                                     name=link_name() )
            receiver['n_received'] = 0


    def on_link_opening ( self, event ):

        self.debug_print ( "on_link_opening -------------------------- " )

        if event.sender:
            self.debug_print ( "    sender: %s" % event.sender.remote_source.address )
            event.sender.source.address = event.sender.remote_source.address
            event.sender.open()
            if event.sender.remote_source.address == self.destination:
                if self.n_waypoint_senders < 2 :
                    self.debug_print ( "    store this as one of my waypoint senders." )
                    self.waypoints[self.n_waypoint_senders]['sender'] = event.sender
                    self.n_waypoint_senders += 1

        elif event.receiver:
            self.debug_print ( "    receiver: %s" % event.receiver.remote_target.address )
            event.receiver.target.address = event.receiver.remote_target.address
            event.receiver.open()
            if event.receiver.remote_target.address == self.destination:
                self.debug_print ( "    store this as one of my waypoint receivers." )
                if self.n_waypoint_receivers < 2 :
                    self.waypoints[self.n_waypoint_receivers]['receiver'] = event.receiver
                    self.n_waypoint_receivers += 1



    def on_sendable ( self, event ):
        self.debug_print ( "on_sendable ------------------------------" )
        for index in range(len(self.senders)) :
            sender = self.senders[index]
            if event.sender == sender['sender'] :
                self.debug_print ( "    client sender %d" % index )
                if sender['n_sent'] < sender['to_send'] :
                    self.debug_print ( "    sending %d" % sender['to_send'] )
                    self.send_from_client ( sender['sender'], sender['to_send'], index )
                    sender['n_sent'] = sender['to_send']  # n_sent = n_to_send
                else :
                    self.debug_print ( "    this sender is already finished." )
                return

        for j in range(len(self.waypoints)) :
            sender = self.waypoints[j]['sender']
            if event.sender == sender :
                self.debug_print ( "    waypoint_sender %d" % j )
                self.send_from_waypoint ( self.waypoints[j] )
                return


    def on_message(self, event):

        self.debug_print ( "on_message --------------------------- " )

        # Is this one of our client receivers ?
        for i in range(len(self.receivers)) :
            receiver = self.receivers[i]
            if event.receiver == receiver['receiver'] :
                receiver['n_received'] += 1
                self.n_transitions     += 1
                self.debug_print ("    client receiver %d has %d messages." % ( i, receiver['n_received'] ) )
                message_content_number = event.message.body
                self.n_rcvd += 1
                if self.n_rcvd >= self.n_expected_received and self.n_thru >= self.n_expected_received:
                    self.debug_print ( "DONE -- self.n_rcvd: %d   self.n_thru: %d" % ( self.n_rcvd, self.n_thru ) )
                    if self.debug :
                        self.report ( )
                    self.check_results_and_bail ( )
                    return

        # Is this one of our waypoint receivers ?
        for j in range(len(self.waypoints)) :
            waypoint = self.waypoints[j]
            if event.receiver == waypoint['receiver'] :
                m = Message ( body=event.message.body )
                waypoint [ 'queue' ].append ( m )
                waypoint [ 'n_received' ] += 1
                self.n_transitions        += 1
                self.debug_print ( "    message received at waypoint %d, queue depth is now %d" % (j, len(waypoint['queue'])))
                self.send_from_waypoint ( waypoint )



    def check_results_and_bail ( self ) :

        if self.n_expected_transitions != self.n_transitions :
            self.bail ( "total transitions were %d, but %d were expected." % ( self.n_transitions, self.n_expected_transitions ) )
            return

        mps                 = self.messages_per_sender
        n_senders           = len(self.senders)
        total_messages_sent = mps * n_senders

        # For total messages sent, the expected value and
        # the actual value must be the same.  The two receivers
        # may receive different numbers (although the total should
        # be correct) but each of the senders must send the expected
        # number of messages or something is wrong.
        for i in range(n_senders) :
            sndr = self.senders[i]
            if sndr['n_sent'] != mps :
              self.bail ( "sender %d sent %d messages instead of %d" % ( i, sndr['n_sent'], mps ) )
              return

        n_waypoints = len(self.waypoints)

        # In this test, each message only hits one waypoint, not both.
        # So the expected number of waypoint receptions is the same
        # as the total number of messages sent.
        total_expected_waypoint_receptions = total_messages_sent
        total_actual_waypoint_receptions   = 0

        for i in range(n_waypoints) :
            total_actual_waypoint_receptions += self.waypoints[i]['n_received']

        if total_actual_waypoint_receptions != total_expected_waypoint_receptions :
            self.bail ( "total waypoint receptions were %d, but %d were expected." % ( total_actual_waypoint_receptions, total_expected_waypoint_receptions) )
            return

        # Finally, our client receivers must receiv one message
        # for every one that was originally sent out by the client senders.
        total_messages_received = 0
        for i in range(len(self.receivers)) :
            this_receiver_got = self.receivers[i]['n_received']
            total_messages_received += this_receiver_got

        if total_messages_received != total_messages_sent :
            self.bail ( "total_messages_received: %d but %d were expected." % (total_messages_received, total_messages_sent) )
            return

        self.debug_print ( "\nsuccess\n" )
        self.bail ( None )



    def report ( self ) :
        print("\n\n==========================================================\nreport\n")

        for i in range(len(self.senders)) :
            print("    client sender %d sent %d messages." % ( i, self.senders[i]['n_sent'] ))

        print("\n")

        for i in range(len(self.waypoints)) :
            print("    waypoint %d received %d messages." % ( i, self.waypoints[i]['n_received'] ))
            print("    waypoint %d sent     %d messages." % ( i, self.waypoints[i]['n_sent'] ))

        print("\n")

        for i in range(len(self.receivers)) :
            print( "    client receiver %d received %d messages." % ( i, self.receivers[i]['n_received'] ))

        print("\nend report\n=========================================================\n\n")



    def run(self):
        container = Container(self)
        container.container_id = self.container_id
        container.run()


if __name__ == '__main__':
    unittest.main(main_module())

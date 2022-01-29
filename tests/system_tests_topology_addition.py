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

from proton import Message
from proton.handlers import MessagingHandler
from proton.reactor import Container

from system_test import TestCase, Qdrouterd, main_module, TIMEOUT


# ====================================================
# Helper classes for all tests.
# ====================================================


# Named timers allow test code to distinguish between several
# simultaneous timers, going off at different rates.
class Timeout:
    def __init__(self, parent, name):
        self.parent = parent
        self.name   = name

    def on_timer_task(self, event):
        self.parent.timeout(self.name)


# ================================================================
#     Setup
# ================================================================

class TopologyAdditionTests (TestCase):

    @classmethod
    def setUpClass(cls):
        super(TopologyAdditionTests, cls).setUpClass()

        def router(name, more_config):

            config = [('router',  {'mode': 'interior', 'id': name}),
                      ('address', {'prefix': 'closest',   'distribution': 'closest'}),
                      ('address', {'prefix': 'balanced',  'distribution': 'balanced'}),
                      ('address', {'prefix': 'multicast', 'distribution': 'multicast'})
                      ]      \
                + more_config

            config = Qdrouterd.Config(config)

            cls.routers.append(cls.tester.qdrouterd(name, config, wait=True))

        cls.routers = []

        client_ports = dict()
        client_ports['A'] = cls.tester.get_port()
        client_ports['B'] = cls.tester.get_port()
        client_ports['C'] = cls.tester.get_port()
        client_ports['D'] = cls.tester.get_port()

        cls.inter_router_ports = dict()
        cls.inter_router_ports['A'] = cls.tester.get_port()
        cls.inter_router_ports['B'] = cls.tester.get_port()

        initial_cost = 10
        lower_cost   =  8
        higher_cost  = 12

        # Only routers A and B are set up initially by this class.
        # Routers C and D are started by the test itself.
        router_A_config = [
            ('listener',
             {'port': client_ports['A'],
              'role': 'normal',
              'stripAnnotations': 'no'
              }
             ),
            ('listener',
             {'role': 'inter-router',
              'port': cls.inter_router_ports['A']
              }
             )
        ]

        router_B_config = [
            ('listener',
             {'port': client_ports['B'],
              'role': 'normal',
              'stripAnnotations': 'no'
              }
             ),
            ('listener',
             {'role': 'inter-router',
              'port': cls.inter_router_ports['B'],
              'stripAnnotations': 'no'
              }
             ),
            ('connector',
             {'name': 'AB_connector',
              'role': 'inter-router',
              'port': cls.inter_router_ports['A'],
              'cost': initial_cost,
              'stripAnnotations': 'no'
              }
             )
        ]

        router('A', router_A_config)
        router('B', router_B_config)

        router_A = cls.routers[0]
        router_B = cls.routers[1]

        router_A.wait_router_connected('B')

        cls.A_addr = router_A.addresses[0]
        cls.B_addr = router_B.addresses[0]

        # The two connections that this router will make, AC and BC,
        # will be lower cost than the direct AB route that the network
        # already has.
        cls.router_C_config = [
            ('listener',
             {'port': client_ports['C'],
              'role': 'normal',
              'stripAnnotations': 'no'
              }
             ),
            ('connector',
             {'name': 'AC_connector',
              'role': 'inter-router',
              'port': cls.inter_router_ports['A'],
              'cost': int(lower_cost / 2),
              'stripAnnotations': 'no',
              'linkCapacity' : 1000
              }
             ),
            ('connector',
             {'name': 'BC_connector',
              'role': 'inter-router',
              'port': cls.inter_router_ports['B'],
              'cost': int(lower_cost / 2),
              'stripAnnotations': 'no',
              'linkCapacity' : 1000
              }
             )
        ]

        # The two connections that this router will make, AD and BD,
        # will be higher cost than the other paths the networks already has
        # available to get from A to B.
        cls.router_D_config = [
            ('listener',
             {'port': client_ports['D'],
              'role': 'normal',
              'stripAnnotations': 'no'
              }
             ),
            ('connector',
             {'name': 'AD_connector',
              'role': 'inter-router',
              'port': cls.inter_router_ports['A'],
              'cost': int(higher_cost / 2),
              'stripAnnotations': 'no',
              'linkCapacity' : 1000
              }
             ),
            ('connector',
             {'name': 'BD_connector',
              'role': 'inter-router',
              'port': cls.inter_router_ports['B'],
              'cost': int(higher_cost / 2),
              'stripAnnotations': 'no',
              'linkCapacity' : 1000
              }
             )
        ]

    # This method allows test code to add new routers during the test,
    # rather than only at startup like A and B above.

    def addRouter(self, name, more_config) :
        config = [('router',  {'mode': 'interior', 'id': name}),
                  ('address', {'prefix': 'closest',   'distribution': 'closest'}),
                  ('address', {'prefix': 'balanced',  'distribution': 'balanced'}),
                  ('address', {'prefix': 'multicast', 'distribution': 'multicast'})
                  ]    \
            + more_config

        config = Qdrouterd.Config(config)

        TopologyAdditionTests.routers.append(TopologyAdditionTests.tester.qdrouterd(name, config, wait=True))

    def test_01_new_route_low_cost(self):
        # During the test, test code will add a new router C,
        # connecting A and B with new low-cost links. At that
        # point the test's messages should switch from using
        # route AB to using route ACB.
        # By passing both of these routes to the test, I tell
        # it to expect both of them to be used.
        # If it terminates with the second path remaining unused
        # it will fail.
        #
        # Since this test alters the path that the messages follow,
        # it is OK for some messages to be released rather than
        # delivered. It doesn't always happen - depends on timing.
        initial_expected_trace = ['0/A', '0/B']
        final_expected_trace   = ['0/A', '0/C', '0/B']
        released_ok = True

        test = AddRouter(self.A_addr,
                         self.B_addr,
                         "closest/01",
                         self,
                         'C',
                         self.router_C_config,
                         [initial_expected_trace, final_expected_trace],
                         released_ok
                         )
        test.run()
        self.assertIsNone(test.error)

    def test_02_new_route_high_cost(self):
        # During the test, test code will add a new router D,
        # connecting A and B with new links. But the links are
        # higher cost than what already exist. The network should
        # ignore them and keep using the lowest cost route that it
        # already has.
        # We communicate this expectation to the test by sending
        # it a single expected trace. The test will fail with
        # error if any other traces are encountered.
        #
        # Since this test does not alter the path that the messages
        # follow, it is *not* OK for any messages to be released
        # rather than delivered.
        only_expected_trace   = ['0/A', '0/C', '0/B']
        released_ok = False

        test = AddRouter(self.A_addr,
                         self.B_addr,
                         "closest/02",
                         self,
                         'D',
                         self.router_D_config,
                         [only_expected_trace],
                         released_ok
                         )
        test.run()
        self.assertIsNone(test.error)


# ================================================================
#     Tests
# ================================================================


# --------------------------------------------------------------
#
# First test
# ------------------
#
# Send some messages through the original A---B router network,
# Then change it to look like this:
#
#                    C
#                   / \
#                  /   \
#                 /     \
#                /       \
#               A -------- B
#
# But the caller controls what costs are assigned to the two
# new links, so only the caller knows whether messages should
# start to flow through the new route ACB or not. It passes
# that knowledge in to us as a list of expected paths.
# This test's job is to make sure that all the expected paths
# get used by messages, and no others get used.
#
#
# Second test
# ------------------
#
# The triangular network from the first test still exists, and
# we will add to it a new router D which also connects A and B.
#
#                    C
#                   / \
#                  /   \
#                 /     \
#                /       \
#               A -------- B
#                \       /
#                 \     /
#                  \   /
#                   \ /
#                    D
# As in the first test, the caller tells us what routes ought
# to be followed, by putting them in the 'expected_traces' arg.
#
# --------------------------------------------------------------

class AddRouter (MessagingHandler):
    def __init__(self,
                 send_addr,
                 recv_addr,
                 destination,
                 parent,
                 new_router_name,
                 new_router_config,
                 expected_traces,
                 released_ok
                 ):
        super(AddRouter, self).__init__(prefetch=100)
        self.send_addr         = send_addr
        self.recv_addr         = recv_addr
        self.dest              = destination
        self.parent            = parent
        self.new_router_name   = new_router_name
        self.new_router_config = new_router_config
        self.released_ok       = released_ok

        self.error         = None
        self.sender        = None
        self.receiver      = None

        self.n_messages    = 30
        self.n_sent        = 0
        self.n_received    = 0
        self.n_released    = 0
        self.n_accepted    = 0

        self.test_timer    = None
        self.send_timer    = None
        self.timeout_count = 0
        self.reactor       = None
        self.container     = None
        self.finishing     = False

        # The parent sends us a list of the traces we
        # ought to see on messages.
        # Make a little data structure that
        # will keep track of how many times each trace was seen.
        self.expected_trace_counts = list()
        for i in range(len(expected_traces)) :
            self.expected_trace_counts.append([expected_traces[i], 0])

    def run(self) :
        Container(self).run()

    # Close everything and allow the test to terminate.
    def bail(self, reason_for_bailing) :
        self.finishing = True
        self.error = reason_for_bailing
        self.receiver.close()
        self.send_conn.close()
        self.recv_conn.close()
        self.test_timer.cancel()
        self.send_timer.cancel()

    # There are two timers. The 'test' timer should only expire if
    # something has gone wrong, in which case it terminates the test.
    # The 'send' timer expires frequently, and every time it goes off
    # we send out a little batch of messages.
    def timeout(self, name):

        if self.finishing :
            return

        self.timeout_count += 1
        if name == "test" :
            self.bail("Timeout Expired: %d messages received, %d expected." % (self.n_received, self.n_messages))
        elif name == "send" :
            self.send()
            self.send_timer = self.reactor.schedule(1, Timeout(self, "send"))

            # At T+5, create the new router with link costs as
            # specified by parent. We do it partway into the test
            # so that some messages will flow through the original
            # network, and some will flow through the network with
            # the new router added.
            if self.timeout_count == 5 :
                self.parent.addRouter(self.new_router_name, self.new_router_config)

    def on_start(self, event):
        self.reactor   = event.reactor
        self.container = event.container

        self.test_timer  = self.reactor.schedule(TIMEOUT, Timeout(self, "test"))
        self.send_timer  = self.reactor.schedule(1, Timeout(self, "send"))

        self.send_conn   = event.container.connect(self.send_addr)
        self.recv_conn   = event.container.connect(self.recv_addr)

        self.sender      = event.container.create_sender(self.send_conn, self.dest)
        self.receiver    = event.container.create_receiver(self.recv_conn, self.dest)
        self.receiver.flow(self.n_messages)

    # ------------------------------------------------------------
    # Sender Side
    # ------------------------------------------------------------

    def send(self):

        if self.n_sent >= self.n_messages :
            return

        # Send little bursts of 3 messages every sender-timeout.
        for _ in range(3) :
            msg = Message(body=self.n_sent)
            self.sender.send(msg)
            self.n_sent += 1
            if self.n_sent == self.n_messages :
                return

    # The caller of this tests decides whether it is OK or
    # not OK to have some messages released during the test.
    def on_released(self, event) :
        if self.released_ok :
            self.n_released += 1
            self.check_count()
        else :
            self.bail("a message was released.")

    def on_accepted(self, event) :
        self.n_accepted += 1
        self.check_count()

    #
    # Do the released plus the accepted messages add up to the number
    # that were sent? If so, bail out with success.
    # Do NOT end the test if the number is still shy of the expected
    # total. The callers of this method just call it every time they
    # get something -- it will be called many times poer test.
    #
    # Pleae Note:
    #   This check is on the 'sender' side of this test, rather than the
    #   'receiver' side, because it is to the sender that we make a
    #   guarantee: namely, that the sender should know the disposition of
    #   all sent messages -- whether they have been accepted by the receiver,
    #   or released by the router network.
    #
    def check_count(self) :
        if self.n_accepted + self.n_released == self.n_messages :
            self.finishing = True
            self.finish_test()

    # ------------------------------------------------------------
    # Receiver Side
    # ------------------------------------------------------------

    def on_message(self, event):
        if self.finishing :
            return
        self.n_received += 1
        trace = event.message.annotations['x-opt-qd.trace']
        # Introduce flaws for debugging.
        # if self.n_received == 13 :
        #     trace = [ '0/B', '0/A', '0/D' ]
        # if self.n_received == 13 :
        #     self.n_received -= 1
        self.record_trace(trace)
        self.check_count()

    # Compare the trace that came from a message to the list of
    # traces the caller told us to expect. If it is one of the
    # expected traces, count it. Otherwise, fail the test.
    def record_trace(self, observed_trace):
        for trace_record in self.expected_trace_counts :
            trace = trace_record[0]
            if observed_trace == trace :
                trace_record[1] += 1
                return
        # If we get here, the trace is one we were not expecting. That's bad.
        self.bail("Unexpected trace: %s" % observed_trace)

    # Shut down everything and make sure that all of the extected traces
    # have been seen.
    def finish_test(self) :
        self.test_timer.cancel()
        self.send_timer.cancel()
        for trace_record in self.expected_trace_counts :
            count = trace_record[1]
            # Deliberate flaw for debugging.
            # count = 0
            if count <= 0 :
                self.bail("Trace %s was not seen." % trace_record[0])
                return

        # success
        self.bail(None)


if __name__ == '__main__':
    unittest.main(main_module())

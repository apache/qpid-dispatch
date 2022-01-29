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

import time

from proton import Message
from proton.handlers import MessagingHandler
from proton.reactor import Container

from system_test import AsyncTestReceiver
from system_test import TestCase, Qdrouterd, main_module
from system_test import TIMEOUT
from system_test import unittest

# ------------------------------------------------
# Helper classes for all tests.
# ------------------------------------------------


class Timeout:
    """
    Named timeout object can handle multiple simultaneous
    timers, by telling the parent which one fired.
    """

    def __init__(self, parent, name):
        self.parent = parent
        self.name   = name

    def on_timer_task(self, event):
        self.parent.timeout(self.name)


class ManagementMessageHelper:
    """
    Format management messages.
    """

    def __init__(self, reply_addr):
        self.reply_addr = reply_addr

    def make_connector_query(self, connector_name):
        props = {'operation': 'READ', 'type': 'org.apache.qpid.dispatch.connector', 'name' : connector_name}
        msg = Message(properties=props, reply_to=self.reply_addr)
        return msg

    def make_connector_delete_command(self, connector_name):
        props = {'operation': 'DELETE', 'type': 'org.apache.qpid.dispatch.connector', 'name' : connector_name}
        msg = Message(properties=props, reply_to=self.reply_addr)
        return msg


# ------------------------------------------------
# END Helper classes for all tests.
# ------------------------------------------------


# ================================================================
#     Setup
# ================================================================

class TopologyTests (TestCase):

    @classmethod
    def setUpClass(cls):
        super(TopologyTests, cls).setUpClass()

        def router(name, more_config):

            config = [('router',  {'mode': 'interior', 'id': name}),
                      ('address', {'prefix': 'closest',   'distribution': 'closest'}),
                      ('address', {'prefix': 'balanced',  'distribution': 'balanced'}),
                      ('address', {'prefix': 'multicast', 'distribution': 'multicast'})
                      ]    \
                + more_config

            config = Qdrouterd.Config(config)

            cls.routers.append(cls.tester.qdrouterd(name, config, wait=True))

        cls.routers = []

        A_client_port = cls.tester.get_port()
        B_client_port = cls.tester.get_port()
        C_client_port = cls.tester.get_port()
        D_client_port = cls.tester.get_port()

        A_inter_router_port = cls.tester.get_port()
        B_inter_router_port = cls.tester.get_port()
        C_inter_router_port = cls.tester.get_port()

        #
        #
        #  Topology of the 4-mesh, with costs of connections marked.
        #  Tail of arrow indicates initiator of connection.
        #
        #                1
        #         D ----------> A
        #         | \         > ^
        #         | 20\   50/   |
        #         |     \ /     |
        #      1  |     / \     | 100
        #         |   /     \   |
        #         v /         > |
        #         C ----------> B
        #                1
        #
        #  Test 1 TopologyFailover Notes
        #
        #       1. Messages are always sent from A, and go to B.
        #       2. First route ahould be ADCB.
        #       3. Then we kill connector CD.
        #       4. Next route should be ADB.
        #       5. Then we kill connector BD.
        #       6. Next route should be ACB.
        #       7. Then we kill connector BC.
        #       8. Final route should be AB.

        cls.A_B_cost =  100
        cls.A_C_cost =   50
        cls.A_D_cost =    1
        cls.B_C_cost =    1
        cls.B_D_cost =   20
        cls.C_D_cost =    1

        router('A',
               [
                   ('listener',
                    {'port': A_client_port,
                     'role': 'normal',
                     'stripAnnotations': 'no'
                     }
                    ),
                   ('listener',
                    {'role': 'inter-router',
                     'port': A_inter_router_port,
                     'stripAnnotations': 'no'
                     }
                    )
               ]
               )

        router('B',
               [
                   ('listener',
                    {'port': B_client_port,
                     'role': 'normal',
                     'stripAnnotations': 'no'
                     }
                    ),
                   ('listener',
                    {'role': 'inter-router',
                     'port': B_inter_router_port,
                     'stripAnnotations': 'no'
                     }
                    ),
                   ('connector',
                    {'name': 'AB_connector',
                     'role': 'inter-router',
                     'port': A_inter_router_port,
                     'cost': cls.A_B_cost,
                     'stripAnnotations': 'no'
                     }
                    )
               ]
               )

        router('C',
               [
                   ('listener',
                    {'port': C_client_port,
                     'role': 'normal',
                     'stripAnnotations': 'no'
                     }
                    ),
                   ('listener',
                    {'role': 'inter-router',
                     'port': C_inter_router_port,
                     'stripAnnotations': 'no'
                     }
                    ),
                   ('connector',
                    {'name': 'AC_connector',
                     'role': 'inter-router',
                     'port': A_inter_router_port,
                     'cost' : cls.A_C_cost,
                     'stripAnnotations': 'no'
                     }
                    ),
                   ('connector',
                    {'name': 'BC_connector',
                     'role': 'inter-router',
                     'port': B_inter_router_port,
                     'cost' : cls.B_C_cost,
                     'stripAnnotations': 'no'
                     }
                    )
               ]
               )

        router('D',
               [
                   ('listener',
                    {'port': D_client_port,
                     'role': 'normal',
                     'stripAnnotations': 'no'
                     }
                    ),
                   ('connector',
                    {'name': 'AD_connector',
                     'role': 'inter-router',
                     'port': A_inter_router_port,
                     'cost' : cls.A_D_cost,
                     'stripAnnotations': 'no'
                     }
                    ),
                   ('connector',
                    {'name': 'BD_connector',
                     'role': 'inter-router',
                     'port': B_inter_router_port,
                     'cost' : cls.B_D_cost,
                     'stripAnnotations': 'no'
                     }
                    ),
                   ('connector',
                    {'name': 'CD_connector',
                     'role': 'inter-router',
                     'port': C_inter_router_port,
                     'cost' : cls.C_D_cost,
                     'stripAnnotations': 'no'
                     }
                    )
               ]
               )

        router_A = cls.routers[0]
        router_B = cls.routers[1]
        router_C = cls.routers[2]
        router_D = cls.routers[3]

        router_A.wait_router_connected('B')
        router_A.wait_router_connected('C')
        router_A.wait_router_connected('D')

        cls.client_addrs = (router_A.addresses[0],
                            router_B.addresses[0],
                            router_C.addresses[0],
                            router_D.addresses[0]
                            )

        # 1 means skip that test.
        cls.skip = {'test_01' : 0
                    }

    def test_01_topology_failover(self):
        name = 'test_01'
        if self.skip[name]:
            self.skipTest("Test skipped during development.")
        test = TopologyFailover(name,
                                self.client_addrs,
                                "closest/01"
                                )
        test.run()
        self.assertIsNone(test.error)


# ================================================================
#     Tests
# ================================================================


# Also see 'Test 1 TopologyFailover Notes', above.

class TopologyFailover (MessagingHandler):
    """
    Test that the lowest-cost route is always chosen in a 4-mesh
    network topology, as one link after another is lost.

    This test also ensures that connections that have been
    deliberately severed do no get restored.
    """

    def __init__(self, test_name, client_addrs, destination):
        super(TopologyFailover, self).__init__(prefetch=0)
        self.client_addrs     = client_addrs
        self.dest       = destination
        self.error      = None
        self.sender     = None
        self.receiver   = None
        self.test_timer = None
        self.send_timer = None
        self.n_sent     = 0
        self.n_received = 0
        self.n_accepted = 0
        self.n_released = 0
        self.reactor    = None
        self.state      = None
        self.send_conn  = None
        self.recv_conn  = None
        self.nap_time   = 2
        self.debug      = False

        # Holds the management sender, receiver, and 'helper'
        # associated with each router.
        self.routers = {
            'A' : dict(),
            'B' : dict(),
            'C' : dict(),
            'D' : dict()
        }

        # These are the expectes routing traces, in the order we
        # expect to receive them.
        self.expected_traces = [
            ['0/A', '0/D', '0/C', '0/B'],
            ['0/A', '0/D', '0/B'],
            ['0/A', '0/C', '0/B'],
            ['0/A', '0/B']
        ]
        self.trace_count    = 0

        # This tells the system in what order to kill the connectors.
        self.kill_list = (
            ('D', 'CD_connector'),
            ('D', 'BD_connector'),
            ('C', 'BC_connector')
        )

        # Use this to keep track of which connectors we have found
        # when the test is first getting started and we are checking
        # the topology.
        self.connectors_map = {'AB_connector' : 0,
                               'AC_connector' : 0,
                               'AD_connector' : 0,
                               'BC_connector' : 0,
                               'BD_connector' : 0,
                               'CD_connector' : 0
                               }

    # The simple state machine transitions when certain events happen,
    # if certain conditions are met.  The conditions are checked for
    # by the callbacks for the events.
    # The normal sequence of states in the state machine is:
    #  1. starting        -- doesn't do anything
    #  2. checking        -- checks initial topology
    #  3. examine_trace   -- look at routing trace of first message
    #  4. kill_connector  -- kills the first connector (CD)
    #  5. examine_trace   -- checks routing trace of next message
    #  5. kill_connector  -- kills the next connector  (BD)
    #  5. examine_trace   -- checks routing trace of next message
    #  5. kill_connector  -- kills the next connector  (BC)
    #  5. examine_trace   -- checks routing trace of final message
    #  5. bailing         -- bails out with success

    def state_transition(self, message, new_state) :
        if self.state == new_state :
            return
        self.state = new_state
        self.debug_print("state transition to : %s -- because %s" % (self.state, message))

    def debug_print(self, text) :
        if self.debug:
            print("%s %s" % (time.time(), text))

    # Shut down everything and exit.
    def bail(self, text):
        self.error = text

        self.send_conn.close()
        self.recv_conn.close()

        self.routers['B']['mgmt_conn'].close()
        self.routers['C']['mgmt_conn'].close()
        self.routers['D']['mgmt_conn'].close()

        self.test_timer.cancel()
        self.send_timer.cancel()

    # ------------------------------------------------------------------------
    # I want some behavior from this test that is a little too complex
    # to be governed by the usual callback functions. The way I do this
    # is by making a simple state machine that checks some conditions
    # during some callback, and then either steps forward or terminates
    # the test.
    # The callbacks that activate the state machine are mostly on_message,
    # or timeout.  But there are two different timers: the one-second
    # timer that mostly runs the test, and the 60-second timer that, if it
    # fires, will terminate the test with a timeout error.
    # ------------------------------------------------------------------------
    def timeout(self, name):
        if name == 'test':
            self.set_state('Timeout Expired', 'bailing')
            self.bail("Timeout Expired: n_sent=%d n_received=%d n_accepted=%d" %
                      (self.n_sent, self.n_received, self.n_accepted))
        elif name == 'sender':
            if self.state == 'examine_trace' :
                self.send()
            self.send_timer = self.reactor.schedule(1, Timeout(self, "sender"))

    def on_start(self, event):
        self.state_transition('on_start', 'starting')
        self.reactor = event.reactor
        self.test_timer = event.reactor.schedule(TIMEOUT, Timeout(self, "test"))
        self.send_timer = event.reactor.schedule(1, Timeout(self, "sender"))
        self.send_conn  = event.container.connect(self.client_addrs[0])  # A
        self.recv_conn  = event.container.connect(self.client_addrs[1])  # B

        self.sender     = event.container.create_sender(self.send_conn, self.dest)
        self.receiver   = event.container.create_receiver(self.recv_conn, self.dest)
        self.receiver.flow(100)

        # I will only send management messages to B, C, and D, because
        # they are the owners of the connections that I will want to delete.
        self.routers['B']['mgmt_conn'] = event.container.connect(self.client_addrs[1])
        self.routers['C']['mgmt_conn'] = event.container.connect(self.client_addrs[2])
        self.routers['D']['mgmt_conn'] = event.container.connect(self.client_addrs[3])

        self.routers['B']['mgmt_receiver'] = event.container.create_receiver(self.routers['B']['mgmt_conn'], dynamic=True)
        self.routers['C']['mgmt_receiver'] = event.container.create_receiver(self.routers['C']['mgmt_conn'], dynamic=True)
        self.routers['D']['mgmt_receiver'] = event.container.create_receiver(self.routers['D']['mgmt_conn'], dynamic=True)

        self.routers['B']['mgmt_sender']   = event.container.create_sender(self.routers['B']['mgmt_conn'], "$management")
        self.routers['C']['mgmt_sender']   = event.container.create_sender(self.routers['C']['mgmt_conn'], "$management")
        self.routers['D']['mgmt_sender']   = event.container.create_sender(self.routers['D']['mgmt_conn'], "$management")

    # -----------------------------------------------------------------
    # At start-time, as the links to the three managed routers
    # open, check each one to make sure that it has all the expected
    # connections.
    # -----------------------------------------------------------------

    def on_link_opened(self, event) :
        self.state_transition('on_link_opened', 'checking')
        # The B mgmt link has opened. Check its connections. --------------------------
        if event.receiver == self.routers['B']['mgmt_receiver'] :
            event.receiver.flow(1000)
            self.routers['B']['mgmt_helper'] = ManagementMessageHelper(event.receiver.remote_source.address)
            for connector in ['AB_connector'] :
                self.connector_check('B', connector)
        # The C mgmt link has opened. Check its connections. --------------------------
        elif event.receiver == self.routers['C']['mgmt_receiver'] :
            event.receiver.flow(1000)
            self.routers['C']['mgmt_helper'] = ManagementMessageHelper(event.receiver.remote_source.address)
            for connector in ['AC_connector', 'BC_connector'] :
                self.connector_check('C', connector)
        # The D mgmt link has opened. Check its connections. --------------------------
        elif event.receiver == self.routers['D']['mgmt_receiver']:
            event.receiver.flow(1000)
            self.routers['D']['mgmt_helper'] = ManagementMessageHelper(event.receiver.remote_source.address)
            for connector in ['AD_connector', 'BD_connector', 'CD_connector'] :
                self.connector_check('D', connector)

    def send(self):
        n_sent_this_time = 0
        if self.sender.credit <= 0:
            self.receiver.flow(100)
            return
        # Send messages one at a time.
        if self.sender.credit > 0 :
            msg = Message(body=self.n_sent)
            self.sender.send(msg)
            n_sent_this_time += 1
            self.n_sent += 1
        self.debug_print("sent: %d" % self.n_sent)

    def on_message(self, event):

        if event.receiver in (
                self.routers['B']['mgmt_receiver'],
                self.routers['C']['mgmt_receiver'],
                self.routers['D']['mgmt_receiver']):

            # ----------------------------------------------------------------
            # This is a management message.
            # ----------------------------------------------------------------
            if self.state == 'checking' :
                connection_name = event.message.body['name']

                if connection_name in self.connectors_map :
                    self.connectors_map[connection_name] = 1
                else :
                    self.state_transition("bad connection name: %s" % connection_name, 'bailing')
                    self.bail("bad connection name: %s" % connection_name)

                n_connections = sum(self.connectors_map.values())
                if n_connections == 6 :
                    self.state_transition("all %d connections found" % n_connections, 'examine_trace')
            elif self.state == 'kill_connector' :
                if event.message.properties["statusDescription"] == 'No Content':
                    # We are in the process of killing a connector, and
                    # have received the response to the kill message.
                    self.state_transition('got kill response', 'examine_trace')
                    # This sleep is here because one early bug that this test found
                    # (and which is now fixed) involved connections that had been
                    # deleted coming back sometimes. It was a race and only happened
                    # very occasionally -- but with a pause here, after getting
                    # confirmation that we have successfully deleted the connector,
                    # the bug would show up 60 to 75% of the time. I think that leaving
                    # this sleep here is the only way to ensure that that particular
                    # bug stays fixed.
                    time.sleep(self.nap_time)
        else:
            # ----------------------------------------------------------------
            # This is a payload message.
            # ----------------------------------------------------------------
            self.n_received += 1
            if self.state == 'examine_trace' :
                trace    = event.message.annotations['x-opt-qd.trace']
                expected = self.expected_traces[self.trace_count]
                if trace == expected :
                    if self.trace_count == len(self.expected_traces) - 1 :
                        self.state_transition('final expected trace %s observed' % expected, 'bailing')
                        self.bail(None)
                        return
                    self.state_transition("expected trace %d observed successfully %s" % (self.trace_count, expected), 'kill_connector')
                    self.kill_a_connector(self.kill_list[self.trace_count])
                    self.trace_count += 1
                else:
                    self.state_transition("expected trace %s but got %s" % (expected, trace), 'bailing')
                    self.bail("expected trace %s but got %s" % (expected, trace))

    def on_accepted(self, event):
        self.n_accepted += 1

    def on_released(self, event) :
        self.n_released += 1

    def connector_check(self, router, connector) :
        self.debug_print("checking connector for router %s" % router)
        mgmt_helper = self.routers[router]['mgmt_helper']
        mgmt_sender = self.routers[router]['mgmt_sender']
        msg = mgmt_helper.make_connector_query(connector)
        mgmt_sender.send(msg)

    def kill_a_connector(self, target) :
        router = target[0]
        connector = target[1]
        self.debug_print("killing connector %s on router %s" % (connector, router))
        mgmt_helper = self.routers[router]['mgmt_helper']
        mgmt_sender = self.routers[router]['mgmt_sender']
        msg = mgmt_helper.make_connector_delete_command(connector)
        mgmt_sender.send(msg)

    def run(self):
        Container(self).run()


class RouterFluxTest(TestCase):
    """
    Verify route table addresses are flushed properly when a remote router is
    rebooted or the link is determined to be stale.
    """

    def _create_router(self, name,
                       ra_interval=None,
                       ra_stale=None,
                       ra_flux=None,
                       extra=None):

        config = [
            ('router', {'id': name,
                        'mode': 'interior',
                        # these are the default values from qdrouter.json
                        'raIntervalSeconds': ra_interval or 30,
                        'raIntervalFluxSeconds': ra_flux or 4,
                        'remoteLsMaxAgeSeconds': ra_stale or 60}),
            ('listener', {'role': 'normal',
                          'port': self.tester.get_port()}),
            ('address', {'prefix': 'closest',
                         'distribution': 'closest'}),
            ('address', {'prefix': 'multicast',
                         'distribution': 'multicast'}),
        ]

        if extra:
            config.extend(extra)
        return self.tester.qdrouterd(name, Qdrouterd.Config(config),
                                     wait=False, expect=None)

    def _deploy_routers(self,
                        ra_interval=None,
                        ra_stale=None,
                        ra_flux=None):
        # configuration:
        # linear 3 interior routers
        #
        #  +-------+    +-------+    +-------+
        #  | INT.A |<==>| INT.B |<==>| INT.C |
        #  +-------+    +-------+    +-------+
        #
        # INT.B has an inter-router listener, INT.A and INT.C connect in

        i_r_port = self.tester.get_port()

        INT_A = self._create_router('INT.A',
                                    ra_interval,
                                    ra_stale,
                                    ra_flux,
                                    extra=[('connector',
                                            {'role': 'inter-router',
                                             'name': 'connectorToB',
                                             'port': i_r_port})])
        INT_A.listener = INT_A.addresses[0]

        INT_B = self._create_router('INT.B',
                                    ra_interval,
                                    ra_stale,
                                    ra_flux,
                                    extra=[('listener',
                                            {'role': 'inter-router',
                                             'port': i_r_port})])
        INT_B.inter_router_port = i_r_port

        INT_C = self._create_router('INT.C',
                                    ra_interval,
                                    ra_stale,
                                    ra_flux,
                                    extra=[('connector',
                                            {'role': 'inter-router',
                                             'name': 'connectorToB',
                                             'port': i_r_port})])
        #
        # wait until router network is formed
        #
        INT_B.wait_router_connected('INT.A')
        INT_B.wait_router_connected('INT.C')

        #
        # create mobile addresses on INT_A
        #
        consumers = [
            AsyncTestReceiver(INT_A.listener,
                              source='closest/on_A'),
            AsyncTestReceiver(INT_A.listener,
                              source='closest/on_A')]
        #
        # wait for addresses to show up on INT.C
        #
        INT_C.wait_address('closest/on_A')

        return (INT_A, INT_B, INT_C, consumers)

    def test_01_reboot_INT_A(self):
        """
        When a router comes online after a reboot its route table sequence will
        be different from the last update it sent.  This should cause the local
        router to flush all mobile addresses it learned from the remote router
        before it rebooted.

        Reboot INT.A and expect its mobile addresses are flushed on INT_C
        """

        # bump the remoteLsMaxAgeSeconds to longer than the test timeout so the
        # test will timeout if the addresses are not removed before the link is
        # considered stale
        stale_timeout = int(TIMEOUT * 2)
        INT_A, INT_B, INT_C, consumers = self._deploy_routers(ra_stale=stale_timeout)

        # at this point all routers are running and the mobile addresses have
        # propagated to INT_C.  Now reboot INT_A
        INT_A.teardown()

        # stop consumers so INT_A's route table will be different when it comes
        # back online so it will require an immediate sync
        for c in consumers:
            c.stop()

        time.sleep(1.0)
        INT_A = self._create_router('INT.A',
                                    ra_stale=stale_timeout,
                                    extra=[('connector',
                                            {'role': 'inter-router',
                                             'name': 'connectorToB',
                                             'port':
                                             INT_B.inter_router_port})])
        INT_A.wait_router_connected('INT.B')

        # expect: INT_A mobile addresses should be removed from INT_C
        # immediately rather than waiting for the remoteLsMaxAgeSeconds timeout

        mgmt = INT_C.management
        a_type = 'org.apache.qpid.dispatch.router.address'
        rsp = mgmt.query(a_type).get_dicts()
        while any('closest/on_A' in a['name'] for a in rsp):
            time.sleep(0.25)
            rsp = mgmt.query(a_type).get_dicts()

    def test_02_shutdown_INT_A(self):
        """
        When a neighboring router is no longer available, the routing algorithm
        does not immediately remove the mobile addresses.  Instead it waits
        remoteLsMaxAgeSeconds to give the router time to come back.  This allows
        the route table to avoid costly updates should the network temporarily
        bounce.

        Delete INT.A and expect its mobile addresses are flushed on INT_C after
        remoteLsMaxAgeSeconds
        """

        # shorten the RA intervals to speed up the test:
        max_age = 6
        INT_A, INT_B, INT_C, consumers = self._deploy_routers(ra_interval=2,
                                                              ra_stale=max_age,
                                                              ra_flux=1)

        # at this point all routers are running and the mobile addresses have
        # propagated to INT_C.  Now remove INT_A
        INT_A.teardown()
        for c in consumers:
            c.stop()

        start = time.time()

        # wait for INT_A mobile addresses to be removed from INT_C, this
        # should happen after ra_stale seconds
        mgmt = INT_C.management
        a_type = 'org.apache.qpid.dispatch.router.address'
        rsp = mgmt.query(a_type).get_dicts()
        while any('closest/on_A' in a['name'] for a in rsp):
            time.sleep(0.25)
            rsp = mgmt.query(a_type).get_dicts()

        # bit of a hack but ensure that the flush did not take an unreasonably
        # long time with respect to the ra_stale value (3x is a guess btw)
        self.assertLessEqual(time.time() - start, 3.0 * max_age)


if __name__ == '__main__':
    unittest.main(main_module())

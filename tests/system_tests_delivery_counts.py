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

from proton import Condition, Message, Delivery,  Timeout
from system_test import TestCase, Qdrouterd, TIMEOUT
from system_test import get_link_info
from system_test import PollTimeout
from proton.handlers import MessagingHandler
from proton.reactor import Container
from qpid_dispatch.management.client import Node


_LINK_STATISTIC_KEYS = set(['unsettledCount',
                            'undeliveredCount',
                            'releasedCount',
                            'presettledCount',
                            'acceptedCount',
                            'droppedPresettledCount',
                            'rejectedCount',
                            'deliveryCount',
                            'modifiedCount'])


def _link_stats_are_zero(statistics, keys):
    """
    Verify that all statistics whose keys are present are zero
    """
    for key in keys:
        if statistics.get(key) != 0:
            return False
    return True;


class OneRouterModifiedTest(TestCase):
    @classmethod
    def setUpClass(cls):
        """Start three routers"""
        super(OneRouterModifiedTest, cls).setUpClass()

        listen_port = cls.tester.get_port()
        config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'A'}),
            ('listener', {'port': listen_port, 'authenticatePeer': False, 'saslMechanisms': 'ANONYMOUS'})])

        cls.router = cls.tester.qdrouterd(name="A", config=config, wait=True)

    def test_one_router_modified_counts(self):
        address = self.router.addresses[0]

        test = ModifiedDeliveriesTest(address)
        test.run()

        local_node = Node.connect(address, timeout=TIMEOUT)
        outs = local_node.query(type='org.apache.qpid.dispatch.router')

        deliveries_modified_index = outs.attribute_names.index('modifiedDeliveries')

        results = outs.results[0]

        self.assertEqual(results[deliveries_modified_index], 10)

        # check link statistics
        self.assertTrue(_link_stats_are_zero(test.sender_stats,
                                             _LINK_STATISTIC_KEYS - set(['deliveryCount',
                                                                         'modifiedCount'])))
        self.assertEqual(test.sender_stats['deliveryCount'], 10)
        self.assertEqual(test.sender_stats['modifiedCount'], 10)

        # receiver just drops the link, so these are not counted as modified
        # but unsettled instead
        self.assertEqual(test.receiver_stats['deliveryCount'], 10)
        self.assertEqual(test.receiver_stats['unsettledCount'], 10)
        self.assertTrue(_link_stats_are_zero(test.receiver_stats,
                                             _LINK_STATISTIC_KEYS - set(['deliveryCount',
                                                                         'unsettledCount'])))


class OneRouterRejectedTest(TestCase):
    @classmethod
    def setUpClass(cls):
        """Start three routers"""
        super(OneRouterRejectedTest, cls).setUpClass()

        listen_port = cls.tester.get_port()
        config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'A'}),
            ('listener', {'port': listen_port, 'authenticatePeer': False, 'saslMechanisms': 'ANONYMOUS'})])

        cls.router = cls.tester.qdrouterd(name="A", config=config, wait=True)

    def test_one_router_rejected_counts(self):
        address = self.router.addresses[0]

        test = RejectedDeliveriesTest(address)
        test.run()

        local_node = Node.connect(address, timeout=TIMEOUT)
        outs = local_node.query(type='org.apache.qpid.dispatch.router')

        deliveries_rejected_index = outs.attribute_names.index('rejectedDeliveries')

        results = outs.results[0]

        self.assertEqual(results[deliveries_rejected_index], 10)

        # check link statistics
        self.assertEqual(test.sender_stats['deliveryCount'], 10)
        self.assertEqual(test.sender_stats['rejectedCount'], 10)
        self.assertTrue(_link_stats_are_zero(test.sender_stats,
                                             _LINK_STATISTIC_KEYS - set(['deliveryCount',
                                                                         'rejectedCount'])))

        self.assertEqual(test.receiver_stats['deliveryCount'], 10)
        self.assertEqual(test.receiver_stats['rejectedCount'], 10)
        self.assertTrue(_link_stats_are_zero(test.receiver_stats,
                                             _LINK_STATISTIC_KEYS - set(['deliveryCount',
                                                                         'rejectedCount'])))


class OneRouterReleasedDroppedPresettledTest(TestCase):
    @classmethod
    def setUpClass(cls):
        """Start three routers"""
        super(OneRouterReleasedDroppedPresettledTest, cls).setUpClass()

        listen_port = cls.tester.get_port()
        config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'A'}),
            ('address', {'prefix': 'multicast', 'distribution': 'multicast'}),
            ('listener', {'port': listen_port, 'authenticatePeer': False, 'saslMechanisms': 'ANONYMOUS'})])

        cls.router = cls.tester.qdrouterd(name="A", config=config, wait=True)

    def test_one_router_released_dropped_counts(self):
        address = self.router.addresses[0]

        test = ReleasedDroppedPresettledCountTest(address)
        test.run()

        local_node = Node.connect(address, timeout=TIMEOUT)
        outs = local_node.query(type='org.apache.qpid.dispatch.router')

        deliveries_dropped_presettled_index = outs.attribute_names.index('droppedPresettledDeliveries')
        deliveries_released_index = outs.attribute_names.index('releasedDeliveries')
        deliveries_presettled_index = outs.attribute_names.index('presettledDeliveries')

        results = outs.results[0]

        self.assertEqual(results[deliveries_dropped_presettled_index], 10)
        self.assertEqual(results[deliveries_released_index], 10)
        self.assertEqual(results[deliveries_presettled_index], 10)

        # check link statistics
        self.assertEqual(test.sender_stats['deliveryCount'], test.n_sent)
        self.assertEqual(test.sender_stats['releasedCount'], 10)
        self.assertEqual(test.sender_stats['presettledCount'], 10)
        self.assertEqual(test.sender_stats['droppedPresettledCount'], 10)
        self.assertTrue(_link_stats_are_zero(test.sender_stats,
                                             _LINK_STATISTIC_KEYS - set(['deliveryCount',
                                                                         'releasedCount',
                                                                         'presettledCount',
                                                                         'droppedPresettledCount'])))


class TwoRouterReleasedDroppedPresettledTest(TestCase):
    @classmethod
    def setUpClass(cls):
        super(TwoRouterReleasedDroppedPresettledTest, cls).setUpClass()

        listen_port_1 = cls.tester.get_port()
        listen_port_2 = cls.tester.get_port()
        listen_port_inter_router = cls.tester.get_port()

        config_1 = Qdrouterd.Config([
            ('router', {'mode': 'interior', 'id': 'A'}),
            ('address', {'prefix': 'multicast', 'distribution': 'multicast'}),
            ('listener', {'port': listen_port_1, 'authenticatePeer': False, 'saslMechanisms': 'ANONYMOUS'}),
            ('listener', {'role': 'inter-router', 'port': listen_port_inter_router, 'authenticatePeer': False, 'saslMechanisms': 'ANONYMOUS'}),
           ])

        config_2 = Qdrouterd.Config([
            ('router', {'mode': 'interior', 'id': 'B'}),
            ('listener', {'port': listen_port_2, 'authenticatePeer': False, 'saslMechanisms': 'ANONYMOUS'}),
            ('connector', {'name': 'connectorToA', 'role': 'inter-router', 'port': listen_port_inter_router,
                           'verifyHostname': 'no'}),
            ])

        cls.routers = []
        cls.routers.append(cls.tester.qdrouterd("A", config_1, wait=True))
        cls.routers.append(cls.tester.qdrouterd("B", config_2, wait=True))
        cls.routers[1].wait_router_connected('A')

    def test_two_router_released_dropped_counts(self):
        address = self.routers[0].addresses[0]

        # Send presettled and settled messages to router 1.
        # Make sure the hello messages (which are presettled dont show up in the counts

        test = ReleasedDroppedPresettledCountTest(address)
        test.run()

        local_node = Node.connect(address, timeout=TIMEOUT)
        outs = local_node.query(type='org.apache.qpid.dispatch.router')

        deliveries_dropped_presettled_index = outs.attribute_names.index('droppedPresettledDeliveries')
        deliveries_released_index = outs.attribute_names.index('releasedDeliveries')
        deliveries_presettled_index = outs.attribute_names.index('presettledDeliveries')

        results = outs.results[0]

        self.assertEqual(results[deliveries_dropped_presettled_index], 10)
        self.assertEqual(results[deliveries_released_index], 10)
        self.assertEqual(results[deliveries_presettled_index], 10)

        # check link statistics
        self.assertEqual(test.sender_stats['deliveryCount'], test.n_sent)
        self.assertEqual(test.sender_stats['releasedCount'], 10)
        self.assertEqual(test.sender_stats['presettledCount'], 10)
        self.assertEqual(test.sender_stats['droppedPresettledCount'], 10)
        self.assertTrue(_link_stats_are_zero(test.sender_stats,
                                             _LINK_STATISTIC_KEYS - set(['deliveryCount',
                                                                         'releasedCount',
                                                                         'presettledCount',
                                                                         'droppedPresettledCount'])))


class LinkRouteIngressEgressTransitTest(TestCase):
    @classmethod
    def setUpClass(cls):
        """Start three routers"""
        super(LinkRouteIngressEgressTransitTest, cls).setUpClass()

        def router(name, connection):

            config = [
                ('router', {'mode': 'interior', 'id': 'QDR.%s'%name}),
            ] + connection

            config = Qdrouterd.Config(config)
            cls.routers.append(cls.tester.qdrouterd(name, config, wait=False))

        cls.routers = []
        a_listener_port = cls.tester.get_port()
        b_listener_port = cls.tester.get_port()
        c_listener_port = cls.tester.get_port()
        test_tag_listener_port = cls.tester.get_port()

        router('A',
               [
                   ('listener', {'role': 'normal', 'host': '0.0.0.0', 'port': a_listener_port, 'saslMechanisms': 'ANONYMOUS'}),
               ])
        router('B',
               [
                   ('listener', {'role': 'normal', 'host': '0.0.0.0', 'port': b_listener_port, 'saslMechanisms': 'ANONYMOUS'}),
                   ('listener', {'name': 'test-tag', 'role': 'route-container', 'host': '0.0.0.0', 'port': test_tag_listener_port, 'saslMechanisms': 'ANONYMOUS'}),

                   ('connector', {'name': 'broker', 'role': 'route-container', 'host': '0.0.0.0', 'port': a_listener_port, 'saslMechanisms': 'ANONYMOUS'}),
                   ('connector', {'name': 'routerC', 'role': 'inter-router', 'host': '0.0.0.0', 'port': c_listener_port}),


                   ('linkRoute', {'prefix': 'pulp.task', 'connection': 'broker', 'direction': 'in'}),
                   ('linkRoute', {'prefix': 'pulp.task', 'connection': 'broker', 'direction': 'out'}),

               ]
               )
        router('C',
               [
                   ('listener', {'host': '0.0.0.0', 'role': 'normal', 'port': cls.tester.get_port(), 'saslMechanisms': 'ANONYMOUS'}),
                   ('listener', {'host': '0.0.0.0', 'role': 'inter-router', 'port': c_listener_port, 'saslMechanisms': 'ANONYMOUS'}),

                   ('linkRoute', {'prefix': 'pulp.task', 'direction': 'in'}),
                   ('linkRoute', {'prefix': 'pulp.task', 'direction': 'out'}),
                ]
               )

        # Wait for the routers to locate each other, and for route propagation
        # to settle
        cls.routers[2].wait_router_connected('QDR.B')
        cls.routers[1].wait_router_connected('QDR.C')
        cls.routers[2].wait_address("pulp.task", remotes=1, delay=3)

        # This is not a classic router network in the sense that QDR.A and D are acting as brokers. We allow a little
        # bit more time for the routers to stabilize.
        sleep(2)

    def test_link_route_ingress_egress_transit_counts(self):
        address1 = self.routers[2].addresses[0]
        address2 = self.routers[2].addresses[0]

        local_node = Node.connect(address1, timeout=TIMEOUT)
        outs = local_node.query(type='org.apache.qpid.dispatch.router')

        deliveries_ingress_index = outs.attribute_names.index('deliveriesIngress')
        deliveries_egress_index = outs.attribute_names.index('deliveriesEgress')
        deliveries_transit_index = outs.attribute_names.index('deliveriesTransit')

        results = outs.results[0]

        pre_ingress_count = results[deliveries_ingress_index]
        pre_egress_count = results[deliveries_egress_index]
        pre_transit_count = results[deliveries_transit_index]

        # Send and receive on the same router, router C
        test = IngressEgressTransitLinkRouteTest(address1, address2)
        test.run()
        local_node = Node.connect(address1, timeout=TIMEOUT)
        outs = local_node.query(type='org.apache.qpid.dispatch.router')

        deliveries_ingress_index = outs.attribute_names.index('deliveriesIngress')
        deliveries_egress_index = outs.attribute_names.index('deliveriesEgress')
        deliveries_transit_index = outs.attribute_names.index('deliveriesTransit')

        results = outs.results[0]

        post_ingress_count = results[deliveries_ingress_index]
        post_egress_count = results[deliveries_egress_index]
        post_transit_count = results[deliveries_transit_index]

        # 10 messages entered the router, and 10 messages were echoed by router A and 3 mgmt requests
        self.assertEqual(post_ingress_count - pre_ingress_count, 23)

        # 10 messages + 3 mgmt request
        self.assertEqual(post_egress_count - pre_egress_count, 13)

        # 10 messages went out this router
        self.assertEqual(post_transit_count - pre_transit_count, 10)

        # Check link statistics
        self.assertEqual(test.sender_stats['deliveryCount'], 10)
        self.assertEqual(test.sender_stats['acceptedCount'], 10)
        self.assertTrue(_link_stats_are_zero(test.sender_stats,
                                             _LINK_STATISTIC_KEYS - set(['deliveryCount',
                                                                         'acceptedCount'])))

        self.assertEqual(test.receiver_stats['deliveryCount'], 10)
        self.assertEqual(test.receiver_stats['acceptedCount'], 10)
        self.assertTrue(_link_stats_are_zero(test.receiver_stats,
                                             _LINK_STATISTIC_KEYS - set(['deliveryCount',
                                                                         'acceptedCount'])))


class TwoRouterIngressEgressTest(TestCase):
    @classmethod
    def setUpClass(cls):
        super(TwoRouterIngressEgressTest, cls).setUpClass()

        listen_port_1 = cls.tester.get_port()
        listen_port_2 = cls.tester.get_port()
        listen_port_inter_router = cls.tester.get_port()

        config_1 = Qdrouterd.Config([
            ('router', {'mode': 'interior', 'id': 'A'}),
            ('listener', {'port': listen_port_1, 'authenticatePeer': False, 'saslMechanisms': 'ANONYMOUS'}),
            ('listener', {'role': 'inter-router', 'port': listen_port_inter_router, 'authenticatePeer': False, 'saslMechanisms': 'ANONYMOUS'}),
           ])

        config_2 = Qdrouterd.Config([
            ('router', {'mode': 'interior', 'id': 'B'}),
            ('listener', {'port': listen_port_2, 'authenticatePeer': False, 'saslMechanisms': 'ANONYMOUS'}),
            ('connector', {'name': 'connectorToA', 'role': 'inter-router', 'port': listen_port_inter_router,
                           'verifyHostname': 'no'}),
            ])

        cls.routers = []
        cls.routers.append(cls.tester.qdrouterd("A", config_1, wait=True))
        cls.routers.append(cls.tester.qdrouterd("B", config_2, wait=True))
        cls.routers[1].wait_router_connected('A')

    def test_two_router_ingress_egress_counts(self):
        in_router_addr = self.routers[0].addresses[0]
        out_router_addr = self.routers[1].addresses[0]

        # Gather the values for deliveries_ingress and deliveries_egress before running the test.

        local_node = Node.connect(in_router_addr, timeout=TIMEOUT)
        outs = local_node.query(type='org.apache.qpid.dispatch.router')
        deliveries_ingress_index = outs.attribute_names.index('deliveriesIngress')
        results = outs.results[0]

        pre_deliveries_ingresss = results[deliveries_ingress_index]

        local_node = Node.connect(out_router_addr, timeout=TIMEOUT)
        outs = local_node.query(type='org.apache.qpid.dispatch.router')
        deliveries_egress_index = outs.attribute_names.index('deliveriesEgress')
        deliveries_accepted_index = outs.attribute_names.index('acceptedDeliveries')
        results = outs.results[0]

        pre_deliveries_egress = results[deliveries_egress_index]
        pre_deliveries_accepted = results[deliveries_accepted_index]

        # Now run the test.  At the end of the test each router will be queried
        # for the per-link stats
        test = IngressEgressTwoRouterTest(in_router_addr, out_router_addr)
        test.run()

        # Gather the values for deliveries_ingress and deliveries_egress after running the test.
        local_node = Node.connect(in_router_addr, timeout=TIMEOUT)
        outs = local_node.query(type='org.apache.qpid.dispatch.router')
        deliveries_ingress_index = outs.attribute_names.index('deliveriesIngress')
        results = outs.results[0]

        post_deliveries_ingresss = results[deliveries_ingress_index]

        local_node = Node.connect(out_router_addr, timeout=TIMEOUT)
        outs = local_node.query(type='org.apache.qpid.dispatch.router')
        deliveries_egress_index = outs.attribute_names.index('deliveriesEgress')
        deliveries_accepted_index = outs.attribute_names.index('acceptedDeliveries')
        results = outs.results[0]

        post_deliveries_egress = results[deliveries_egress_index]
        post_deliveries_accepted = results[deliveries_accepted_index]

        accepted_deliveries_diff = post_deliveries_accepted - pre_deliveries_accepted

        # 12 = 10 msgs + 2 mgmt requests
        self.assertEqual(post_deliveries_ingresss - pre_deliveries_ingresss, 12)
        self.assertEqual(post_deliveries_egress - pre_deliveries_egress, 12)

        # check the link statistics
        self.assertEqual(test.sender_stats['deliveryCount'], 10)
        self.assertEqual(test.sender_stats['acceptedCount'], 10)
        self.assertTrue(_link_stats_are_zero(test.sender_stats,
                                             _LINK_STATISTIC_KEYS - set(['deliveryCount',
                                                                         'acceptedCount'])))


        self.assertEqual(test.receiver_stats['deliveryCount'], 10)
        self.assertEqual(test.receiver_stats['acceptedCount'], 10)
        self.assertTrue(_link_stats_are_zero(test.receiver_stats,
                                             _LINK_STATISTIC_KEYS - set(['deliveryCount',
                                                                         'acceptedCount'])))

        # The management requests are counted in the acceptedDeliveries, so it is difficult to measure the
        # exact number of accepted deliveries at this point in time. But it must at least be 10 since
        # we know for sure from the test that the 10 dispositions related to the 10 sent messages
        # were definitely received
        self.assertTrue(accepted_deliveries_diff >= 10)


class OneRouterIngressEgressTest(TestCase):
    @classmethod
    def setUpClass(cls):
        """Start a router and a messenger"""
        super(OneRouterIngressEgressTest, cls).setUpClass()

        listen_port = cls.tester.get_port()
        config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'A'}),
            ('listener', {'port': listen_port, 'authenticatePeer': False, 'saslMechanisms': 'ANONYMOUS'})])

        cls.router = cls.tester.qdrouterd(name="A", config=config, wait=True)

    def test_one_router_ingress_egress_counts(self):
        address = self.router.addresses[0]

        test = IngressEgressOneRouterTest(address)
        test.run()

        local_node = Node.connect(address, timeout=TIMEOUT)
        outs = local_node.query(type='org.apache.qpid.dispatch.router')

        deliveries_ingress_index = outs.attribute_names.index('deliveriesIngress')
        deliveries_egress_index = outs.attribute_names.index('deliveriesEgress')

        results = outs.results[0]

        # 13 = ten msgs + 3 mgmt requests
        self.assertEqual(results[deliveries_ingress_index], 13)
        # 12 = ten msgs + 2 mgmt requests
        self.assertEqual(results[deliveries_egress_index], 12)

        # check the link statistics
        self.assertEqual(test.sender_stats['deliveryCount'], 10)
        self.assertEqual(test.sender_stats['acceptedCount'], 10)
        self.assertTrue(_link_stats_are_zero(test.sender_stats,
                                             _LINK_STATISTIC_KEYS - set(['deliveryCount',
                                                                         'acceptedCount'])))

        self.assertEqual(test.receiver_stats['deliveryCount'], 10)
        self.assertEqual(test.receiver_stats['acceptedCount'], 10)
        self.assertTrue(_link_stats_are_zero(test.receiver_stats,
                                             _LINK_STATISTIC_KEYS - set(['deliveryCount',
                                                                         'acceptedCount'])))


class RouteContainerEgressCount(TestCase):
    @classmethod
    def setUpClass(cls):
        super(RouteContainerEgressCount, cls).setUpClass()

        config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'QDR'}),

            #
            # Create a general-purpose listener for sending and receiving deliveries
            #
            ('listener', {'port': cls.tester.get_port()}),

            # Create a route-container listener and give it a name myListener.
            # Later on we will create an autoLink which has a connection property of myListener.
            #
            ('listener', {'role': 'route-container', 'name': 'myListener', 'port': cls.tester.get_port()}),
            #
            # Note here that the connection is set to a previously declared 'myListener'
            #
            ('autoLink', {'address': 'myListener.1', 'connection': 'myListener', 'direction': 'in'}),
            ('autoLink', {'address': 'myListener.1', 'connection': 'myListener', 'direction': 'out'}),
        ])

        cls.router = cls.tester.qdrouterd(name="A", config=config, wait=True)

    def test_route_container_egress(self):
        regular_addr = self.router.addresses[0]
        route_container_addr = self.router.addresses[1]
        test = RouteContainerEgressTest(route_container_addr, regular_addr)
        test.run()

        local_node = Node.connect(regular_addr, timeout=TIMEOUT)
        outs = local_node.query(type='org.apache.qpid.dispatch.router')

        deliveries_egress_route_container_index = outs.attribute_names.index('deliveriesEgressRouteContainer')

        results = outs.results[0]
        # 11 = 10 msgs + 1 mgmt msg
        self.assertEqual(results[deliveries_egress_route_container_index], 11)

        # check link statistics
        self.assertEqual(test.sender_stats['deliveryCount'], 10)
        self.assertEqual(test.sender_stats['acceptedCount'], 10)
        self.assertTrue(_link_stats_are_zero(test.sender_stats,
                                             _LINK_STATISTIC_KEYS - set(['deliveryCount',
                                                                         'acceptedCount'])))

        self.assertEqual(test.receiver_stats['deliveryCount'], 10)
        self.assertEqual(test.receiver_stats['acceptedCount'], 10)
        self.assertTrue(_link_stats_are_zero(test.receiver_stats,
                                             _LINK_STATISTIC_KEYS - set(['deliveryCount',
                                                                         'acceptedCount'])))


class OneRouterLinkCountersTest(TestCase):
    """
    A set of tests that validate link-level counters
    """
    CREDIT = 20  # default issued by test receiver client
    COUNT  = 40  # default total msgs the sender client generates

    @classmethod
    def setUpClass(cls):
        # create one router
        super(OneRouterLinkCountersTest, cls).setUpClass()

        listen_port = cls.tester.get_port()
        config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'LinkCounters'}),
            ('listener', {'port': listen_port,
                          'authenticatePeer': False,
                          'saslMechanisms': 'ANONYMOUS',
                          'linkCapacity': cls.CREDIT})])

        cls.router = cls.tester.qdrouterd(name="LinkCounters", config=config, wait=True)


    class LinkCountersTest(MessagingHandler):
        """
        Create 1 sender and 1 receiver to router_addr.  Send count messages.
        The test ends when the receivers deliveryCount reaches rx_limit.
        Explicitly set the receiver credit and whether to sender sends
        presettled or unsettled messages.
        """
        def __init__(self, router_addr, count=None, rx_limit=None,
                     credit=None, presettled=False, outcome=None):
            super(OneRouterLinkCountersTest.LinkCountersTest,
                  self).__init__(auto_accept=False,
                                 auto_settle=False,
                                 prefetch=0)
            self.router_addr = router_addr
            self.presettled = presettled
            self.outcome = outcome
            self.count = OneRouterLinkCountersTest.COUNT \
                if count is None else count
            self.credit = OneRouterLinkCountersTest.COUNT \
                if credit is None else credit
            self.rx_limit = OneRouterLinkCountersTest.COUNT \
                if rx_limit is None else rx_limit

            self.sent = 0
            self.timer = 0
            self.poll_timer = None
            self.conn = None
            self.sender_stats = None
            self.receiver_stats = None

        def timeout(self):
            self._cleanup()

        def _cleanup(self):
            if self.conn:
                self.conn.close()
                self.conn = None
            if self.poll_timer:
                self.poll_timer.cancel()
                self.poll_timer = None
            if self.timer:
                self.timer.cancel()
                self.timer = None

        def poll_timeout(self):
            """
            Periodically check the deliveryCount on the receiver.  Once it
            reaches rx_limit the test is complete: gather link statistics
            before closing the clients
            """
            li = get_link_info("Rx_Test01", self.router_addr)
            if li and li['deliveryCount'] == self.rx_limit:
                self.receiver_stats = li
                self.sender_stats = get_link_info("Tx_Test01", self.router_addr)
                self._cleanup()
            else:
                self.poll_timer = self.reactor.schedule(0.5, PollTimeout(self))

        def on_start(self, event):
            self.reactor = event.reactor
            self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))
            self.poll_timer = event.reactor.schedule(0.5, PollTimeout(self))
            self.conn = event.container.connect(self.router_addr)
            self.receiver = event.container.create_receiver(self.conn,
                                                            source="Test01",
                                                            name='Rx_Test01')
            self.receiver.flow(self.credit)
            self.sender = event.container.create_sender(self.conn,
                                                        target="Test01",
                                                        name="Tx_Test01")
        def on_sendable(self, event):
            if self.sent < self.count:
                dlv = self.sender.send(Message(body="Test01"))
                if self.presettled:
                    dlv.settle()
                self.sent += 1

        def on_message(self, event):
            if self.outcome:
                event.delivery.update(self.outcome)
                event.delivery.settle()
                # otherwise just drop it

        def run(self):
            Container(self).run()


    def test_01_presettled(self):
        """
        Verify the presettled dropped count link counter by exhausting credit
        before sending is complete
        """
        limit = self.CREDIT//2  # 1/2 the capacity given the sender
        test = self.LinkCountersTest(self.router.addresses[0],
                                     presettled=True,
                                     count=self.COUNT,
                                     rx_limit=limit,
                                     credit=limit)
        test.run()

        # since these are presettled the sender should have credit
        # replenished by the router after each message.
        self.assertEqual(test.sender_stats['deliveryCount'], self.COUNT)
        self.assertEqual(test.sender_stats['presettledCount'], self.COUNT)
        self.assertTrue(_link_stats_are_zero(test.sender_stats,
                                             _LINK_STATISTIC_KEYS
                                             - set(['deliveryCount',
                                                    'presettledCount'])))

        # since credit is fixed at limit, exactly that number of msgs can be received
        self.assertEqual(test.receiver_stats['deliveryCount'], limit)

        # verify that some messages were dropped and some are stuck on the
        # undelivered list
        self.assertTrue(test.receiver_stats['undeliveredCount'] > 0)
        self.assertTrue(test.receiver_stats['droppedPresettledCount'] > 0)

        # expect that whatever was not dropped was delivered
        self.assertEqual(test.receiver_stats['deliveryCount'],
                         (test.receiver_stats['presettledCount']
                          - test.receiver_stats['droppedPresettledCount']))

        # expect the sum of dropped+delivered+undelivered accounts for all
        # messages sent
        self.assertEqual(self.COUNT,
                         (test.receiver_stats['deliveryCount']
                          + test.receiver_stats['undeliveredCount']
                          + test.receiver_stats['droppedPresettledCount']))

        # all other counters must be zero
        self.assertTrue(_link_stats_are_zero(test.receiver_stats,
                                             _LINK_STATISTIC_KEYS
                                             - set(['deliveryCount',
                                                    'undeliveredCount',
                                                    'droppedPresettledCount',
                                                    'presettledCount'])))

    def test_02_unsettled(self):
        """
        Verify the link unsettled count by granting less credit than required
        by the sender
        """
        test = self.LinkCountersTest(self.router.addresses[0],
                                     presettled=False,
                                     count=self.COUNT,
                                     rx_limit=self.CREDIT,
                                     credit=self.CREDIT)
        test.run()

        # expect the receiver to get rx_limit worth of unsettled deliveries
        self.assertEqual(test.receiver_stats['deliveryCount'], self.CREDIT)
        self.assertEqual(test.receiver_stats['unsettledCount'], self.CREDIT)
        self.assertTrue(_link_stats_are_zero(test.receiver_stats,
                                             _LINK_STATISTIC_KEYS
                                             - set(['deliveryCount',
                                                    'unsettledCount'])))

        # expect sender only to be able to send as much as credit
        self.assertEqual(test.sender_stats['deliveryCount'], self.CREDIT)
        self.assertEqual(test.sender_stats['unsettledCount'], self.CREDIT)
        self.assertTrue(_link_stats_are_zero(test.sender_stats,
                                             _LINK_STATISTIC_KEYS
                                             - set(['deliveryCount',
                                                    'unsettledCount'])))

    def test_03_released(self):
        """
        Verify the link released count by releasing all received messages
        """
        test = self.LinkCountersTest(self.router.addresses[0],
                                     outcome=Delivery.RELEASED)
        test.run()
        self.assertEqual(test.receiver_stats['deliveryCount'], self.COUNT)
        self.assertEqual(test.receiver_stats['releasedCount'], self.COUNT)
        self.assertTrue(_link_stats_are_zero(test.receiver_stats,
                                             _LINK_STATISTIC_KEYS
                                             - set(['deliveryCount',
                                                    'releasedCount'])))

        self.assertEqual(test.sender_stats['deliveryCount'], self.COUNT)
        self.assertEqual(test.sender_stats['releasedCount'], self.COUNT)
        self.assertTrue(_link_stats_are_zero(test.sender_stats,
                                             _LINK_STATISTIC_KEYS
                                             - set(['deliveryCount',
                                                    'releasedCount'])))

    def test_04_one_credit_accepted(self):
        """
        Verify counters on a credit-blocked link
        """
        test = self.LinkCountersTest(self.router.addresses[0],
                                     outcome=Delivery.ACCEPTED,
                                     rx_limit=1,
                                     credit=1)
        test.run()
        # expect only 1 delivery, an link credit worth of queued up messages
        self.assertEqual(test.receiver_stats['deliveryCount'], 1)
        self.assertEqual(test.receiver_stats['acceptedCount'], 1)
        self.assertEqual(test.receiver_stats['undeliveredCount'], self.CREDIT)
        self.assertTrue(_link_stats_are_zero(test.receiver_stats,
                                             _LINK_STATISTIC_KEYS
                                             - set(['deliveryCount',
                                                    'undeliveredCount',
                                                    'acceptedCount'])))

        # expect that one message will be delivered, then link capacity
        # messages will be enqueued internally
        self.assertEqual(test.sender_stats['unsettledCount'], self.CREDIT)
        self.assertEqual(test.sender_stats['deliveryCount'], self.CREDIT + 1)
        self.assertEqual(test.sender_stats['acceptedCount'], 1)
        self.assertTrue(_link_stats_are_zero(test.sender_stats,
                                             _LINK_STATISTIC_KEYS
                                             - set(['deliveryCount',
                                                    'unsettledCount',
                                                    'acceptedCount'])))


class RouteContainerIngressCount(TestCase):
    @classmethod
    def setUpClass(cls):
        super(RouteContainerIngressCount, cls).setUpClass()

        config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'QDR'}),

            #
            # Create a general-purpose listener for sending and receiving deliveries
            #
            ('listener', {'port': cls.tester.get_port()}),

            # Create a route-container listener and give it a name myListener.
            # Later on we will create an autoLink which has a connection property of myListener.
            #
            ('listener', {'role': 'route-container', 'name': 'myListener', 'port': cls.tester.get_port()}),
            #
            # Note here that the connection is set to a previously declared 'myListener'
            #
            ('autoLink', {'address': 'myListener.1', 'connection': 'myListener', 'direction': 'in'}),
            ('autoLink', {'address': 'myListener.1', 'connection': 'myListener', 'direction': 'out'}),
        ])

        cls.router = cls.tester.qdrouterd(name="A", config=config, wait=True)

    def test_route_container_ingress(self):
        regular_addr = self.router.addresses[0]
        route_container_addr = self.router.addresses[1]
        test = RouteContainerIngressTest(route_container_addr, regular_addr)
        test.run()

        local_node = Node.connect(regular_addr, timeout=TIMEOUT)
        outs = local_node.query(type='org.apache.qpid.dispatch.router')

        deliveries_ingress_route_container_index = outs.attribute_names.index('deliveriesIngressRouteContainer')

        results = outs.results[0]
        # 22 = 20 msgs + 2 mgmt msgs
        self.assertEqual(results[deliveries_ingress_route_container_index], 22)

        # check link statistics
        self.assertEqual(test.sender_stats['deliveryCount'], 10)
        self.assertEqual(test.sender_stats['acceptedCount'], 10)
        self.assertTrue(_link_stats_are_zero(test.sender_stats,
                                             _LINK_STATISTIC_KEYS - set(['deliveryCount',
                                                                         'acceptedCount'])))
        self.assertEqual(test.sender1_stats['deliveryCount'], 10)
        self.assertEqual(test.sender1_stats['acceptedCount'], 10)
        self.assertTrue(_link_stats_are_zero(test.sender1_stats,
                                             _LINK_STATISTIC_KEYS - set(['deliveryCount',
                                                                         'acceptedCount'])))
        self.assertEqual(test.receiver_stats['deliveryCount'], 20)
        self.assertEqual(test.receiver_stats['acceptedCount'], 20)
        self.assertTrue(_link_stats_are_zero(test.receiver_stats,
                                             _LINK_STATISTIC_KEYS - set(['deliveryCount',
                                                                         'acceptedCount'])))


class IngressEgressTwoRouterTest(MessagingHandler):
    def __init__(self, sender_address, receiver_address):
        super(IngressEgressTwoRouterTest, self).__init__()
        self.sender = None
        self.receiver = None
        self.conn_sender = None
        self.conn_recv = None
        self.timer = None
        self.dest = 'examples'
        self.sender_address = sender_address
        self.receiver_address = receiver_address
        self.n_sent = 0
        self.n_received = 0
        self.num_messages = 10
        self.start = False
        self.n_accept = 0
        self.sender_stats = None
        self.receiver_stats = None
        self.done = False

    def timeout(self):
        self.conn_sender.close()
        self.conn_recv.close()

    def check_if_done(self):
        if not self.done and self.num_messages == self.n_received and self.n_accept == self.num_messages:
            self.done = True
            self.sender_stats = get_link_info('Tx_IngressEgressTwoRouterTest',
                                               self.sender_address)
            self.receiver_stats = get_link_info('Rx_IngressEgressTwoRouterTest',
                                                 self.receiver_address)
            self.conn_sender.close()
            self.conn_recv.close()
            self.timer.cancel()

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.conn_recv = event.container.connect(self.receiver_address)
        self.receiver = event.container.create_receiver(self.conn_recv,
                                                        source=self.dest,
                                                        name='Rx_IngressEgressTwoRouterTest')

    def on_sendable(self, event):
        if not self.start:
            return

        if self.n_sent < self.num_messages:
            msg = Message(body={'number': self.n_sent})
            self.sender.send(msg)
            self.n_sent += 1

    def on_link_opened(self, event):
        if event.receiver == self.receiver:
            self.start = True
            self.conn_sender = event.container.connect(self.sender_address)
            self.sender = event.container.create_sender(self.conn_sender,
                                                        target=self.dest,
                                                        name='Tx_IngressEgressTwoRouterTest')

    def on_message(self, event):
        if event.receiver == self.receiver:
            self.n_received += 1

    def on_accepted(self, event):
        if event.sender:
            self.n_accept += 1
        self.check_if_done()

    def run(self):
        Container(self).run()


class IngressEgressOneRouterTest(MessagingHandler):
    def __init__(self, address):
        super(IngressEgressOneRouterTest, self).__init__()
        self.sender = None
        self.receiver = None
        self.conn = None
        self.timer = None
        self.dest = 'examples'
        self.address = address
        self.n_sent = 0
        self.n_received = 0
        self.n_accepted = 0
        self.num_messages = 10
        self.sender_stats = None
        self.receiver_stats = None
        self.done = False

    def timeout(self):
        self.conn.close()

    def check_if_done(self):
        if not self.done and (self.n_sent == self.n_received
                              and self.n_sent == self.n_accepted):
            self.done = True
            self.sender_stats = get_link_info('Tx_IngressEgressOneRouterTest', self.address)
            self.receiver_stats = get_link_info('Rx_IngressEgressOneRouterTest', self.address)
            self.conn.close()
            self.timer.cancel()

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.conn = event.container.connect(self.address)
        self.sender = event.container.create_sender(self.conn,
                                                    target=self.dest,
                                                    name='Tx_IngressEgressOneRouterTest')
        self.receiver = event.container.create_receiver(self.conn,
                                                        source=self.dest,
                                                        name='Rx_IngressEgressOneRouterTest')

    def on_sendable(self, event):
        if self.n_sent < self.num_messages:
            msg = Message(body={'number': self.n_sent})
            self.sender.send(msg)
            self.n_sent += 1

    def on_message(self, event):
        if event.receiver == self.receiver:
            self.n_received += 1

    def on_accepted(self, event):
        self.n_accepted += 1
        self.check_if_done()

    def run(self):
        Container(self).run()


class RouteContainerEgressTest(MessagingHandler):
    def __init__(self, route_container_addr, sender_addr):
        super(RouteContainerEgressTest, self).__init__()
        self.sender_addr = sender_addr
        self.route_container_addr = route_container_addr
        self.timer = None
        self.error = None
        self.receiver = None
        self.receiver_conn = None
        self.dest = "myListener.1"
        self.sender_conn = None
        self.sender = None
        self.start = False
        self.n_sent = 0
        self.n_received = 0
        self.n_accepted = 0
        self.num_messages = 10
        self.sender_stats = None
        self.receiver_stats = None
        self.done = False

    def check_if_done(self):
        if not self.done and (self.n_sent == self.n_received
                              and self.n_sent == self.n_accepted):
            self.done = True
            self.sender_stats = get_link_info('Tx_RouteContainerEgressTest', self.sender_addr)
            self.receiver_stats = get_link_info('Rx_RouteContainerEgressTest', self.route_container_addr)
            self.receiver_conn.close()
            self.sender_conn.close()
            self.timer.cancel()

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.receiver_conn = event.container.connect(self.route_container_addr)
        self.receiver = event.container.create_receiver(self.receiver_conn,
                                                        source=self.dest,
                                                        name='Rx_RouteContainerEgressTest')

    def timeout(self):
        self.error = "Timeout Expired: self.n_sent=%d self.n_received=%d" % (self.n_sent, self.self.n_received)
        self.sender_conn.close()
        self.receiver_conn.close()

    def on_sendable(self, event):
        if not self.start:
            return

        if self.n_sent < self.num_messages:
            msg = Message(body={'number': self.n_sent})
            self.sender.send(msg)
            self.n_sent += 1

    def on_message(self, event):
        if event.receiver == self.receiver:
            self.n_received += 1

    def on_accepted(self, event):
        self.n_accepted += 1
        self.check_if_done()

    def on_link_opened(self, event):
        if event.receiver == self.receiver:
            self.start = True
            self.sender_conn = event.container.connect(self.sender_addr)
            self.sender = event.container.create_sender(self.sender_conn,
                                                        target=self.dest,
                                                        name='Tx_RouteContainerEgressTest')

    def run(self):
        Container(self).run()


class RouteContainerIngressTest(MessagingHandler):
    def __init__(self, route_container_addr, receiver_addr):
        super(RouteContainerIngressTest, self).__init__()
        self.receiver_addr = receiver_addr
        self.route_container_addr = route_container_addr
        self.timer = None
        self.error = None
        self.receiver = None
        self.receiver_conn = None
        self.dest = "myListener.1"
        self.sender_conn = None
        self.sender = None
        self.sender1 = None
        self.start = False
        self.n_sent = 0
        self.n_received = 0
        self.n_accepted = 0
        self.num_messages = 20
        self.sender_stats = None
        self.sender1_stats = None
        self.receiver_stats = None
        self.done = False

    def check_if_done(self):
        if not self.done and (self.n_sent == self.n_received
                              and self.n_sent == self.n_accepted):
            self.done = True
            self.sender_stats = get_link_info('A', self.route_container_addr)
            self.sender1_stats = get_link_info('B', self.route_container_addr)
            self.receiver_stats = get_link_info('Rx_RouteContainerIngressTest', self.receiver_addr)
            self.receiver_conn.close()
            self.sender_conn.close()
            self.timer.cancel()

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.receiver_conn = event.container.connect(self.receiver_addr)
        self.receiver = event.container.create_receiver(self.receiver_conn,
                                                        source=self.dest,
                                                        name='Rx_RouteContainerIngressTest')
        self.sender_conn = event.container.connect(self.route_container_addr)

    def timeout(self):
        self.error = "Timeout Expired: self.n_sent=%d self.n_received=%d" % (self.n_sent, self.self.n_received)
        self.sender_conn.close()
        self.receiver_conn.close()

    def on_sendable(self, event):
        if not self.start:
            return

        if self.n_sent < self.num_messages:
            msg = Message(body={'number': self.n_sent})
            self.sender.send(msg)
            self.n_sent += 1

            self.sender1.send(msg)
            self.n_sent += 1

    def on_message(self, event):
        if event.receiver == self.receiver:
            self.n_received += 1

    def on_accepted(self, event):
        self.n_accepted += 1
        self.check_if_done()

    def on_link_opened(self, event):
        if event.receiver == self.receiver:
            self.start = True
            # Create 2 senders. Each sender will send 10 messages each
            self.sender = event.container.create_sender(self.sender_conn, self.dest, name="A")
            self.sender1 = event.container.create_sender(self.sender_conn, self.dest, name="B")

    def run(self):
        Container(self).run()


class IngressEgressTransitLinkRouteTest(MessagingHandler):
    def __init__(self, sender_addr, receiver_addr):
        super(IngressEgressTransitLinkRouteTest, self).__init__()
        self.timer = None
        self.receiver_conn = None
        self.receiver = None
        self.sender = None
        self.sender_conn = None
        self.dest = "pulp.task"
        self.start = False
        self.n_sent = 0
        self.num_messages = 10
        self.n_received = 0
        self.n_accepted = 0
        self.sender_addr = sender_addr
        self.receiver_addr = receiver_addr
        self.error = None
        self.sender_stats = None
        self.receiver_stats = None
        self.done = False

    def timeout(self):
        self.error = "Timeout Expired: self.n_sent=%d self.n_received=%d" % (self.n_sent, self.self.n_received)
        self.sender_conn.close()
        self.receiver_conn.close()

    def check_if_done(self):
        if not self.done and (self.n_sent == self.n_received
                              and self.n_sent == self.n_accepted):
            self.done = True
            self.sender_stats = get_link_info('Tx_IngressEgressTransitLinkRouteTest',
                                               self.sender_addr)
            self.receiver_stats = get_link_info('Rx_IngressEgressTransitLinkRouteTest',
                                                 self.receiver_addr)
            self.receiver_conn.close()
            self.sender_conn.close()
            self.timer.cancel()

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.receiver_conn = event.container.connect(self.receiver_addr)
        self.receiver = event.container.create_receiver(self.receiver_conn,
                                                        source=self.dest,
                                                        name='Rx_IngressEgressTransitLinkRouteTest')

    def on_link_opened(self, event):
        if event.receiver == self.receiver:
            self.sender_conn = event.container.connect(self.sender_addr)
            self.sender = event.container.create_sender(self.sender_conn,
                                                        target=self.dest,
                                                        name='Tx_IngressEgressTransitLinkRouteTest')
            self.start = True

    def on_sendable(self, event):
        if not self.start:
            return

        if self.n_sent < self.num_messages:
            msg = Message(body={'number': self.n_sent})
            self.sender.send(msg)
            self.n_sent += 1

    def on_message(self, event):
        if event.receiver == self.receiver:
            self.n_received += 1

    def on_accepted(self, event):
        self.n_accepted += 1
        self.check_if_done()

    def run(self):
        Container(self).run()


class ReleasedDroppedPresettledCountTest(MessagingHandler):
    def __init__(self, sender_addr):
        super(ReleasedDroppedPresettledCountTest, self).__init__()
        self.timer = None
        self.sender_conn = None
        self.sender = None
        self.error = None
        self.n_sent = 0
        self.num_messages = 20
        self.sender_addr = sender_addr
        self.sender_stats = None

        # We are sending to a multicast address
        self.dest = "multicast"
        self.n_released = 0
        self.expect_released = 10
        self.done = False

    def check_if_done(self):
        if not self.done and self.expect_released == self.n_released:
            self.done = True
            self.sender_stats = get_link_info('ReleasedDroppedPresettledCountTest',
                                               self.sender_addr)
            self.sender_conn.close()
            self.timer.cancel()

    def timeout(self):
        self.error = "Timeout Expired: self.n_sent=%d, self.self.n_released=%d  " % (self.n_sent, self.n_released)
        self.sender_conn.close()

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.sender_conn = event.container.connect(self.sender_addr)

        # Note that this is an anonymous link which will be granted credit w/o
        # blocking for consumers.  Therefore all messages sent to this address
        # will be dropped
        self.sender = event.container.create_sender(self.sender_conn,
                                                    name='ReleasedDroppedPresettledCountTest')
    def on_sendable(self, event):
        # We are sending a total of 20 deliveries. 10 unsettled and 10 pre-settled to a multicast address
        if self.n_sent < self.num_messages:
            msg = Message(body={'number': self.n_sent})
            msg.address = self.dest
            dlv = self.sender.send(msg)
            if self.n_sent < 10:
                dlv.settle()
            self.n_sent += 1

    def on_released(self, event):
        self.n_released += 1
        self.check_if_done()

    def run(self):
        Container(self).run()


class RejectedDeliveriesTest(MessagingHandler):
    def __init__(self, addr):
        super(RejectedDeliveriesTest, self).__init__(auto_accept=False)
        self.addr = addr
        self.dest = "someaddress"
        self.error = None
        self.n_sent = 0
        self.num_messages = 10
        self.n_rejected = 0
        self.sender_conn = None
        self.receiver_conn = None
        self.timer = None
        self.sender = None
        self.receiver = None
        self.sender_stats = None
        self.receiver_stats = None
        self.done = False

    def check_if_done(self):
        if not self.done and self.n_rejected == self.num_messages:
            self.done = True
            self.sender_stats = get_link_info('Tx_RejectedDeliveriesTest', self.addr)
            self.receiver_stats = get_link_info('Rx_RejectedDeliveriesTest', self.addr)
            self.sender_conn.close()
            self.receiver_conn.close()
            self.timer.cancel()

    def timeout(self):
        self.error = "Timeout Expired: self.n_sent=%d, self.self.n_rejected=%d  " % (self.n_sent, self.n_rejected)
        self.sender_conn.close()
        self.receiver_conn.close()

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.sender_conn = event.container.connect(self.addr)
        self.sender = event.container.create_sender(self.sender_conn,
                                                    target=self.dest,
                                                    name='Tx_RejectedDeliveriesTest')
        self.receiver_conn = event.container.connect(self.addr)
        self.receiver = event.container.create_receiver(self.receiver_conn,
                                                        source=self.dest,
                                                        name='Rx_RejectedDeliveriesTest')

    def on_rejected(self, event):
        self.n_rejected += 1
        self.check_if_done()

    def on_message(self, event):
        # We will reject every delivery we receive.
        self.reject(event.delivery)

    def on_sendable(self, event):
        if self.n_sent < self.num_messages:
            msg = Message( body={'number': self.n_sent})
            self.sender.send(msg)
            self.n_sent += 1

    def run(self):
        Container(self).run()


class ModifiedDeliveriesTest(MessagingHandler):
    def __init__(self, addr):
        super(ModifiedDeliveriesTest, self).__init__(auto_accept=False)
        self.addr = addr
        self.dest = "someaddress"
        self.error = None
        self.n_sent = 0
        self.num_messages = 10
        self.n_modified = 0
        self.sender_conn = None
        self.receiver_conn = None
        self.timer = None
        self.sender = None
        self.receiver = None
        self.n_received = 0
        self.sender_stats = None
        self.receiver_stats = None
        self.done = False

    def check_if_done(self):
        if not self.done and self.n_modified == self.num_messages:
            self.done = True
            self.sender_stats = get_link_info('Tx_ModifiedDeliveriesTest', self.addr)
            self.sender_conn.close()
            self.receiver_conn.close()
            self.timer.cancel()

    def timeout(self):
        self.error = "Timeout Expired: self.n_sent=%d, self.self.n_modified=%d  " % (self.n_sent, self.n_modified)
        self.sender_conn.close()
        self.receiver_conn.close()

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.sender_conn = event.container.connect(self.addr)
        self.sender = event.container.create_sender(self.sender_conn,
                                                    target=self.dest,
                                                    name='Tx_ModifiedDeliveriesTest')
        self.receiver_conn = event.container.connect(self.addr)
        self.receiver = event.container.create_receiver(self.receiver_conn,
                                                        source=self.dest,
                                                        name='Rx_ModifiedDeliveriesTest')

    def on_released(self, event):
        if event.delivery.remote_state == Delivery.MODIFIED:
            self.n_modified += 1
        self.check_if_done()

    def on_message(self, event):
        # The messages have arrived at the receiver but we will not settle the message and instead just closed the
        # connection. Since the router did not receive the acknowledgements, it will send back MODIFIED dispositions
        # to the sender.
        self.n_received += 1
        # After 10 messages are received, simply close the receiver connection without acknowledging the messages
        if self.n_received == self.num_messages:
            self.receiver_stats = get_link_info('Rx_ModifiedDeliveriesTest', self.addr)
            self.receiver_conn.close()

    def on_sendable(self, event):
        if self.n_sent < self.num_messages:
            msg = Message(body={'number': self.n_sent})
            self.sender.send(msg)
            self.n_sent += 1

    def run(self):
        Container(self).run()

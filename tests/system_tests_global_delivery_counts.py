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

from proton import Condition, Message, Delivery, PENDING, ACCEPTED, REJECTED, Url, symbol, Timeout
from system_test import TestCase, Qdrouterd, TIMEOUT
from proton.handlers import MessagingHandler
from proton.reactor import Container
from qpid_dispatch.management.client import Node


class OneRouterModifiedTest(TestCase):
    @classmethod
    def setUpClass(cls):
        """Start three routers"""
        super(OneRouterModifiedTest, cls).setUpClass()

        listen_port = cls.tester.get_port()
        config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'A'}),
            ('listener', {'port': listen_port, 'authenticatePeer': False, 'saslMechanisms': 'ANONYMOUS'})])

        cls.router = Qdrouterd(name="A", config=config, wait=True)

    def test_one_router_modified_counts(self):
        address = self.router.addresses[0]

        test = ModifieddDeliveriesTest(address)
        test.run()

        local_node = Node.connect(address, timeout=TIMEOUT)
        outs = local_node.query(type='org.apache.qpid.dispatch.router')

        deliveries_modified_index = outs.attribute_names.index('modifiedDeliveries')

        results = outs.results[0]

        self.assertEqual(results[deliveries_modified_index], 10)


class OneRouterRejectedTest(TestCase):
    @classmethod
    def setUpClass(cls):
        """Start three routers"""
        super(OneRouterRejectedTest, cls).setUpClass()

        listen_port = cls.tester.get_port()
        config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'A'}),
            ('listener', {'port': listen_port, 'authenticatePeer': False, 'saslMechanisms': 'ANONYMOUS'})])

        cls.router = Qdrouterd(name="A", config=config, wait=True)

    def test_one_router_rejected_counts(self):
        address = self.router.addresses[0]

        test = RejectedDeliveriesTest(address)
        test.run()

        local_node = Node.connect(address, timeout=TIMEOUT)
        outs = local_node.query(type='org.apache.qpid.dispatch.router')

        deliveries_rejected_index = outs.attribute_names.index('rejectedDeliveries')

        results = outs.results[0]

        self.assertEqual(results[deliveries_rejected_index], 10)


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

        cls.router = Qdrouterd(name="A", config=config, wait=True)

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
        cls.routers[2].wait_address("pulp.task", remotes=1, delay=0.5)

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

        # 10 messages entered the router, and 10 messages were echoed by router A and one mgmt request
        self.assertEqual(post_ingress_count - pre_ingress_count, 21)

        # 10 messages + 1 mgmt request
        self.assertEqual(post_egress_count - pre_egress_count, 11)

        # 10 messages went out this router
        self.assertEqual(post_transit_count - pre_transit_count, 10)


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
        address1 = self.routers[0].addresses[0]
        address2 = self.routers[1].addresses[0]

        # Gather the values for deliveries_ingress and deliveries_egress before running the test.

        local_node = Node.connect(address1, timeout=TIMEOUT)
        outs = local_node.query(type='org.apache.qpid.dispatch.router')
        deliveries_ingress_index = outs.attribute_names.index('deliveriesIngress')
        results = outs.results[0]

        pre_deliveries_ingresss = results[deliveries_ingress_index]

        local_node = Node.connect(address2, timeout=TIMEOUT)
        outs = local_node.query(type='org.apache.qpid.dispatch.router')
        deliveries_egress_index = outs.attribute_names.index('deliveriesEgress')
        deliveries_accepted_index = outs.attribute_names.index('acceptedDeliveries')
        results = outs.results[0]

        pre_deliveries_egress = results[deliveries_egress_index]
        pre_deliveries_accepted = results[deliveries_accepted_index]

        # Now run the test.
        test = IngressEgressTwoRouterTest(address1, address2)
        test.run()

        # Gather the values for deliveries_ingress and deliveries_egress after running the test.
        local_node = Node.connect(address1, timeout=TIMEOUT)
        outs = local_node.query(type='org.apache.qpid.dispatch.router')
        deliveries_ingress_index = outs.attribute_names.index('deliveriesIngress')
        results = outs.results[0]

        post_deliveries_ingresss = results[deliveries_ingress_index]

        local_node = Node.connect(address2, timeout=TIMEOUT)
        outs = local_node.query(type='org.apache.qpid.dispatch.router')
        deliveries_egress_index = outs.attribute_names.index('deliveriesEgress')
        deliveries_accepted_index = outs.attribute_names.index('acceptedDeliveries')
        results = outs.results[0]

        post_deliveries_egress = results[deliveries_egress_index]
        post_deliveries_accepted = results[deliveries_accepted_index]

        accepted_deliveries_diff = post_deliveries_accepted - pre_deliveries_accepted

        self.assertEqual(post_deliveries_ingresss - pre_deliveries_ingresss, 11)
        self.assertEqual(post_deliveries_egress - pre_deliveries_egress, 11)

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

        cls.router = Qdrouterd(name="A", config=config, wait=True)

    def test_one_router_ingress_egress_counts(self):
        address = self.router.addresses[0]

        test = IngressEgressOneRouterTest(address)
        test.run()

        local_node = Node.connect(address, timeout=TIMEOUT)
        outs = local_node.query(type='org.apache.qpid.dispatch.router')

        deliveries_ingress_index = outs.attribute_names.index('deliveriesIngress')
        deliveries_egress_index = outs.attribute_names.index('deliveriesEgress')

        results = outs.results[0]

        self.assertEqual(results[deliveries_ingress_index], 11)
        self.assertEqual(results[deliveries_egress_index], 10)


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
            ('autoLink', {'addr': 'myListener.1', 'connection': 'myListener', 'direction': 'in'}),
            ('autoLink', {'addr': 'myListener.1', 'connection': 'myListener', 'direction': 'out'}),
        ])

        cls.router = Qdrouterd(name="A", config=config, wait=True)

    def test_route_container_egress(self):
        regular_addr = self.router.addresses[0]
        route_container_addr = self.router.addresses[1]
        test = RouteContainerEgressTest(route_container_addr, regular_addr)
        test.run()

        local_node = Node.connect(regular_addr, timeout=TIMEOUT)
        outs = local_node.query(type='org.apache.qpid.dispatch.router')

        deliveries_egress_route_container_index = outs.attribute_names.index('deliveriesEgressRouteContainer')

        results = outs.results[0]
        self.assertEqual(results[deliveries_egress_route_container_index], 10)


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
            ('autoLink', {'addr': 'myListener.1', 'connection': 'myListener', 'direction': 'in'}),
            ('autoLink', {'addr': 'myListener.1', 'connection': 'myListener', 'direction': 'out'}),
        ])

        cls.router = Qdrouterd(name="A", config=config, wait=True)

    def test_route_container_ingress(self):
        regular_addr = self.router.addresses[0]
        route_container_addr = self.router.addresses[1]
        test = RouteContainerIngressTest(route_container_addr, regular_addr)
        test.run()

        local_node = Node.connect(regular_addr, timeout=TIMEOUT)
        outs = local_node.query(type='org.apache.qpid.dispatch.router')

        deliveries_ingress_route_container_index = outs.attribute_names.index('deliveriesIngressRouteContainer')

        results = outs.results[0]
        self.assertEqual(results[deliveries_ingress_route_container_index], 20)


class IngressEgressTwoRouterTest(MessagingHandler):
    def __init__(self, address1, address2):
        super(IngressEgressTwoRouterTest, self).__init__()
        self.sender = None
        self.receiver = None
        self.conn_sender = None
        self.conn_recv = None
        self.timer = None
        self.dest = 'examples'
        self.address1 = address1
        self.address2 = address2
        self.n_sent = 0
        self.n_received = 0
        self.num_messages = 10
        self.start = False
        self.n_accept = 0

    def timeout(self):
        self.conn_sender.close()
        self.conn_recv.close()

    def check_if_done(self):
        if self.num_messages == self.n_received and self.n_accept == self.num_messages:
            self.conn_sender.close()
            self.conn_recv.close()
            self.timer.cancel()

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.conn_sender = event.container.connect(self.address1)
        self.conn_recv = event.container.connect(self.address2)
        self.receiver = event.container.create_receiver(self.conn_recv, self.dest)
        self.sender = event.container.create_sender(self.conn_sender, self.dest)

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
        self.num_messages = 10

    def timeout(self):
        self.conn.close()

    def check_if_done(self):
        if self.n_sent == self.n_received:
            self.conn.close()
            self.timer.cancel()

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.conn = event.container.connect(self.address)
        self.sender = event.container.create_sender(self.conn, self.dest)
        self.receiver = event.container.create_receiver(self.conn, self.dest)

    def on_sendable(self, event):
        if self.n_sent < self.num_messages:
            msg = Message(body={'number': self.n_sent})
            self.sender.send(msg)
            self.n_sent += 1

    def on_message(self, event):
        if event.receiver == self.receiver:
            self.n_received += 1

    def on_accepted(self, event):
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
        self.num_messages = 10

    def check_if_done(self):
        if self.n_sent == self.n_received:
            self.receiver_conn.close()
            self.sender_conn.close()
            self.timer.cancel()

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.receiver_conn = event.container.connect(self.route_container_addr)
        self.receiver = event.container.create_receiver(self.receiver_conn, self.dest)
        self.sender_conn = event.container.connect(self.sender_addr)
        self.sender = event.container.create_sender(self.sender_conn, self.dest)

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
        self.check_if_done()

    def on_link_opened(self, event):
        if event.receiver == self.receiver:
            self.start = True

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
        self.num_messages = 20

    def check_if_done(self):
        if self.n_sent == self.n_received:
            self.receiver_conn.close()
            self.sender_conn.close()
            self.timer.cancel()

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.receiver_conn = event.container.connect(self.receiver_addr)
        self.receiver = event.container.create_receiver(self.receiver_conn, self.dest)
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
        self.sender_addr = sender_addr
        self.receiver_addr = receiver_addr
        self.error = None

    def timeout(self):
        self.error = "Timeout Expired: self.n_sent=%d self.n_received=%d" % (self.n_sent, self.self.n_received)
        self.sender_conn.close()
        self.receiver_conn.close()

    def check_if_done(self):
        if self.n_sent == self.n_received:
            self.receiver_conn.close()
            self.sender_conn.close()
            self.timer.cancel()

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.receiver_conn = event.container.connect(self.receiver_addr)
        self.receiver = event.container.create_receiver(self.receiver_conn, self.dest)
        self.sender_conn = event.container.connect(self.sender_addr)
        self.sender = event.container.create_sender(self.sender_conn, self.dest)

    def on_link_opened(self, event):
        if event.receiver == self.receiver:
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

        # We are sending to a multicast address
        self.dest = "multicast"
        self.n_released = 0
        self.expect_released = 10

    def check_if_done(self):
        if self.expect_released == self.n_released:
            self.sender_conn.close()
            self.timer.cancel()

    def timeout(self):
        self.error = "Timeout Expired: self.n_sent=%d, self.self.n_released=%d  " % (self.n_sent, self.n_released)
        self.sender_conn.close()

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.sender_conn = event.container.connect(self.sender_addr)

        # Note that there are no receivers. All messages sent to this address will be dropped
        self.sender = event.container.create_sender(self.sender_conn, self.dest)

    def on_sendable(self, event):
        # We are sending a total of 20 deliveries. 10 unsettled and 10 pre-settled to a multicast address
        if self.n_sent < self.num_messages:
            msg = Message(body={'number': self.n_sent})
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

    def check_if_done(self):
        if self.n_rejected == self.num_messages:
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
        self.sender = event.container.create_sender(self.sender_conn, self.dest)
        self.receiver_conn = event.container.connect(self.addr)
        self.receiver = event.container.create_receiver(self.receiver_conn, self.dest)

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


class ModifieddDeliveriesTest(MessagingHandler):
    def __init__(self, addr):
        super(ModifieddDeliveriesTest, self).__init__(auto_accept=False)
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

    def check_if_done(self):
        if self.n_modified == self.num_messages:
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
        self.sender = event.container.create_sender(self.sender_conn, self.dest)
        self.receiver_conn = event.container.connect(self.addr)
        self.receiver = event.container.create_receiver(self.receiver_conn, self.dest)

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
            self.receiver_conn.close()

    def on_sendable(self, event):
        if self.n_sent < self.num_messages:
            msg = Message(body={'number': self.n_sent})
            self.sender.send(msg)
            self.n_sent += 1

    def run(self):
        Container(self).run()

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

from proton import Message, Delivery
from proton.handlers import MessagingHandler
from proton.reactor import Container

from qpid_dispatch.management.client import Node

from system_test import TestCase, Qdrouterd, TIMEOUT, get_link_info, \
    get_inter_router_links, has_mobile_dest_in_address_table, PollTimeout, TestTimeout

LARGE_PAYLOAD = ("X" * 1024) * 30

_LINK_STATISTIC_KEYS = {'unsettledCount',
                        'undeliveredCount',
                        'releasedCount',
                        'presettledCount',
                        'acceptedCount',
                        'droppedPresettledCount',
                        'rejectedCount',
                        'deliveryCount',
                        'modifiedCount'}


def get_body(n_sent, large_message=False):
    if large_message:
        body = {'number': n_sent, 'msg': LARGE_PAYLOAD}
    else:
        body = {'number': n_sent}


def _link_stats_are_zero(statistics, keys):
    """
    Verify that all statistics whose keys are present are zero
    """
    for key in keys:
        if statistics.get(key) != 0:
            return False
    return True


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

    def router_modified_counts(self, large_message=False):
        address = self.router.addresses[0]

        local_node = Node.connect(address, timeout=TIMEOUT)
        outs = local_node.query(type='org.apache.qpid.dispatch.router')
        deliveries_modified_index = outs.attribute_names.index('modifiedDeliveries')
        results = outs.results[0]
        num_modified_deliveries_pre_test = results[deliveries_modified_index]

        num_messages = 10
        test = ModifiedDeliveriesTest(address, num_messages, large_message)
        test.run()

        outs = local_node.query(type='org.apache.qpid.dispatch.router')
        results = outs.results[0]

        self.assertEqual(results[deliveries_modified_index] - num_modified_deliveries_pre_test, num_messages)

        # check link statistics
        self.assertTrue(_link_stats_are_zero(test.sender_stats,
                                             _LINK_STATISTIC_KEYS - {'deliveryCount',
                                                                     'modifiedCount'}))
        self.assertEqual(test.sender_stats['deliveryCount'], num_messages)
        self.assertEqual(test.sender_stats['modifiedCount'], num_messages)

        # receiver just drops the link, so these are not counted as modified
        # but unsettled instead
        self.assertEqual(test.receiver_stats['deliveryCount'], num_messages)
        self.assertEqual(test.receiver_stats['unsettledCount'], num_messages)
        self.assertTrue(_link_stats_are_zero(test.receiver_stats,
                                             _LINK_STATISTIC_KEYS - {'deliveryCount',
                                                                     'unsettledCount'}))

    def test_one_router_modified_counts(self):
        self.router_modified_counts()

    def test_one_router_large_message_modified_counts(self):
        self.router_modified_counts(True)


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

    def one_router_rejected_counts(self, large_message=False):
        address = self.router.addresses[0]

        local_node = Node.connect(address, timeout=TIMEOUT)
        outs = local_node.query(type='org.apache.qpid.dispatch.router')
        deliveries_rejected_index = outs.attribute_names.index('rejectedDeliveries')
        results = outs.results[0]
        deliveries_rejected_pre_test = results[deliveries_rejected_index]

        num_messages = 10
        test = RejectedDeliveriesTest(address, num_messages, large_message)
        test.run()

        outs = local_node.query(type='org.apache.qpid.dispatch.router')
        results = outs.results[0]

        self.assertEqual(results[deliveries_rejected_index] - deliveries_rejected_pre_test, num_messages)

        # check link statistics
        self.assertEqual(test.sender_stats['deliveryCount'], num_messages)
        self.assertEqual(test.sender_stats['rejectedCount'], num_messages)
        self.assertTrue(_link_stats_are_zero(test.sender_stats,
                                             _LINK_STATISTIC_KEYS - {'deliveryCount',
                                                                     'rejectedCount'}))

        self.assertEqual(test.receiver_stats['deliveryCount'], num_messages)
        self.assertEqual(test.receiver_stats['rejectedCount'], num_messages)
        self.assertTrue(_link_stats_are_zero(test.receiver_stats,
                                             _LINK_STATISTIC_KEYS - {'deliveryCount',
                                                                     'rejectedCount'}))

    def test_one_router_rejected_counts(self):
        self.one_router_rejected_counts()

    def test_one_router_large_message_rejected_counts(self):
        self.one_router_rejected_counts(True)


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

    def one_router_released_dropped_count(self, large_message=False):
        address = self.router.addresses[0]

        local_node = Node.connect(address, timeout=TIMEOUT)
        outs = local_node.query(type='org.apache.qpid.dispatch.router')

        deliveries_dropped_presettled_index = outs.attribute_names.index('droppedPresettledDeliveries')
        deliveries_released_index = outs.attribute_names.index('releasedDeliveries')
        deliveries_presettled_index = outs.attribute_names.index('presettledDeliveries')
        results = outs.results[0]

        deliveries_dropped_presettled_pre_test = results[deliveries_dropped_presettled_index]
        deliveries_released_pre_test = results[deliveries_released_index]
        deliveries_presettled_pre_test = results[deliveries_presettled_index]
        num_messages = 20
        test = ReleasedDroppedPresettledCountTest(address, num_messages, large_message)
        test.run()

        outs = local_node.query(type='org.apache.qpid.dispatch.router')

        results = outs.results[0]

        self.assertEqual(results[deliveries_dropped_presettled_index] - deliveries_dropped_presettled_pre_test, 10)
        self.assertEqual(results[deliveries_released_index] - deliveries_released_pre_test, 10)
        self.assertEqual(results[deliveries_presettled_index] - deliveries_presettled_pre_test, 10)

        # check link statistics
        self.assertEqual(test.sender_stats['deliveryCount'], test.n_sent)
        self.assertEqual(test.sender_stats['releasedCount'], 10)
        self.assertEqual(test.sender_stats['presettledCount'], 10)
        self.assertEqual(test.sender_stats['droppedPresettledCount'], 10)
        self.assertTrue(_link_stats_are_zero(test.sender_stats,
                                             _LINK_STATISTIC_KEYS - {'deliveryCount',
                                                                     'releasedCount',
                                                                     'presettledCount',
                                                                     'droppedPresettledCount'}))

    def test_one_router_released_dropped_counts(self):
        self.one_router_released_dropped_count()

    def test_one_router_large_message_released_dropped_counts(self):
        self.one_router_released_dropped_count(True)


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
            ('connector', {'name': 'connectorToA', 'role': 'inter-router', 'port': listen_port_inter_router}),
        ])

        cls.routers = []
        cls.routers.append(cls.tester.qdrouterd("A", config_1, wait=True))
        cls.routers.append(cls.tester.qdrouterd("B", config_2, wait=True))
        cls.routers[1].wait_router_connected('A')

    def two_router_released_dropped_counts(self, large_message=False):
        address = self.routers[0].addresses[0]

        # Send presettled and settled messages to router 1.
        # Make sure the hello messages (which are presettled dont show up in the counts

        local_node = Node.connect(address, timeout=TIMEOUT)
        outs = local_node.query(type='org.apache.qpid.dispatch.router')
        deliveries_dropped_presettled_index = outs.attribute_names.index('droppedPresettledDeliveries')
        deliveries_released_index = outs.attribute_names.index('releasedDeliveries')
        deliveries_presettled_index = outs.attribute_names.index('presettledDeliveries')
        results = outs.results[0]
        deliveries_dropped_presettled_pre_test = results[deliveries_dropped_presettled_index]
        deliveries_released_pre_test = results[deliveries_released_index]
        deliveries_presettled_pre_test = results[deliveries_presettled_index]
        num_messages = 20
        test = ReleasedDroppedPresettledCountTest(address, num_messages, large_message)
        test.run()

        outs = local_node.query(type='org.apache.qpid.dispatch.router')
        results = outs.results[0]

        self.assertEqual(results[deliveries_dropped_presettled_index] - deliveries_dropped_presettled_pre_test, 10)
        self.assertEqual(results[deliveries_released_index] - deliveries_released_pre_test, 10)
        self.assertEqual(results[deliveries_presettled_index] - deliveries_presettled_pre_test, 10)

        # check link statistics
        self.assertEqual(test.sender_stats['deliveryCount'], test.n_sent)
        self.assertEqual(test.sender_stats['releasedCount'], 10)
        self.assertEqual(test.sender_stats['presettledCount'], 10)
        self.assertEqual(test.sender_stats['droppedPresettledCount'], 10)
        self.assertTrue(_link_stats_are_zero(test.sender_stats,
                                             _LINK_STATISTIC_KEYS - {'deliveryCount',
                                                                     'releasedCount',
                                                                     'presettledCount',
                                                                     'droppedPresettledCount'}))

    def test_two_router_released_dropped_counts(self):
        self.two_router_released_dropped_counts()

    def test_two_router_large_message_released_dropped_counts(self):
        self.two_router_released_dropped_counts(True)


class AddressCheckerTimeout :
    def __init__(self, parent):
        self.parent = parent

    def on_timer_task(self, event):
        self.parent.address_check_timeout()


class CounterCheckerTimeout :
    def __init__(self, parent):
        self.parent = parent

    def on_timer_task(self, event):
        self.parent.count_check_timeout()


class LargePresettledLinkCounterTest(MessagingHandler):
    def __init__(self, sender_addr, receiver_addr):
        super(LargePresettledLinkCounterTest, self).__init__()
        self.timer = None
        self.sender_conn = None
        self.receiver_conn = None
        self.receiver = None
        self.error = None
        self.n_sent = 0
        self.n_received = 0
        self.num_messages = 25
        self.sender_addr = sender_addr
        self.receiver_addr = receiver_addr
        self.dest = "LargePresettledLinkCounterTest"
        self.links = None
        self.success = False
        self.address_check_timer = None
        self.container = None
        self.num_attempts = 0
        self.reactor = None
        self.done = False

    def check_if_done(self):
        if self.done:
            # Step 5: All messages have been received by receiver.
            # Check the presettled count on the inter-router link of
            # Router B (where the receiver is attached).
            self.links = get_inter_router_links(self.receiver_addr)
            for link in self.links:
                # The self.num_messages + 1 is because before this test started the presettledCount was 1
                if link.get("linkDir") == "in" and link.get("presettledCount") == self.num_messages + 1:
                    self.success = True
                    break
            self.sender_conn.close()
            self.receiver_conn.close()
            self.timer.cancel()

    def address_check_timeout(self):
        if has_mobile_dest_in_address_table(self.sender_addr, self.dest):
            # Step 3: The address has propagated to Router A. Now attach a sender
            # to router A.
            self.sender_conn = self.container.connect(self.sender_addr)
            self.sender = self.container.create_sender(self.sender_conn,
                                                       self.dest,
                                                       name='SenderA')
        else:
            if self.num_attempts < 2:
                self.address_check_timer = self.reactor.schedule(2,
                                                                 AddressCheckerTimeout(self))
                self.num_attempts += 1

    def timeout(self):
        self.error = "Timeout Expired: self.n_sent=%d, self.self.n_received=%d  " % (self.n_sent, self.n_received)
        self.sender_conn.close()
        self.receiver_conn.close()

    def on_start(self, event):
        self.container = event.container
        self.timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        # Step 1: Create a receiver with name ReceiverA to address LargePresettledLinkCounterTest
        # This receiver is attached to router B. Later a sender will be
        # created which will be connected to Router A. The sender will send
        # on the same address that the receiver is receiving on.
        self.receiver_conn = event.container.connect(self.receiver_addr)
        self.receiver = event.container.create_receiver(self.receiver_conn,
                                                        self.dest,
                                                        name='ReceiverA')

    def on_link_opened(self, event):
        self.reactor = event.reactor
        if event.receiver:
            # Step 2: The receiver link has been opened.
            # Give 2 seconds for the address to propagate to the other router (Router A)
            self.address_check_timer = event.reactor.schedule(2, AddressCheckerTimeout(self))
            self.num_attempts += 1

    def on_sendable(self, event):
        # Step 4: Send self.num_messages multi-frame large pre-settled messages.
        # These messages will travel over inter-router link to Router B.
        if self.n_sent < self.num_messages:
            msg = Message(body=LARGE_PAYLOAD)
            dlv = self.sender.send(msg)
            # We are sending a pre-settled large multi frame message.
            dlv.settle()
            self.n_sent += 1

    def on_message(self, event):
        if self.receiver == event.receiver:
            self.n_received += 1
            if self.n_received == self.num_messages:
                self.done = True
            self.check_if_done()

    def run(self):
        Container(self).run()


class LargePresettledReleasedLinkCounterTest(MessagingHandler):
    def __init__(self, sender_addr, receiver_addr):
        super(LargePresettledReleasedLinkCounterTest, self).__init__(prefetch=0)
        self.sender_addr = sender_addr
        self.receiver_addr = receiver_addr
        self.dest = "LargePresettledReleasedLinkCounterTest"
        self.receiver_dropoff_count = 50
        self.num_messages = 200
        self.num_attempts = 0
        self.n_sent = 0
        self.done = False
        self.n_received = 0
        self.count_check_timer = None
        self.success = False
        self.links = None
        self.receiver_conn_closed = False

    def check_if_done(self):
        # Step 6:
        # Check the counts on the inter-router link of
        # Router B (where the receiver is attached). There
        # should be no released or modified messages.
        self.links = get_inter_router_links(self.receiver_addr)
        for link in self.links:
            # We don't know how many deliveries got from one side of the
            # inter-router link to the other but there should at least be as
            # many as was sent to the receiver
            if link.get("linkDir") == "in" \
                    and link.get("presettledCount") > self.receiver_dropoff_count \
                    and link.get("deliveryCount") > self.receiver_dropoff_count \
                    and link.get("releasedCount") == 0\
                    and link.get("modifiedCount") == 0:
                self.success = True
                break
        self.sender_conn.close()
        self.timer.cancel()

    def count_check_timeout(self):
        self.check_if_done()

    def address_check_timeout(self):
        if has_mobile_dest_in_address_table(self.sender_addr, self.dest):
            # Step 3: The address has propagated to Router A. Now attach a sender
            # to router A.
            self.sender_conn = self.container.connect(self.sender_addr)
            self.sender = self.container.create_sender(self.sender_conn,
                                                       self.dest,
                                                       name='SenderA')
        else:
            if self.num_attempts < 2:
                self.address_check_timer = self.reactor.schedule(2, AddressCheckerTimeout(self))
                self.num_attempts += 1

    def timeout(self):
        self.error = "Timeout Expired: self.n_sent=%d, self.self.n_received=%d  " % (self.n_sent, self.n_received)
        self.sender_conn.close()
        if not self.receiver_conn_closed:
            self.receiver_conn.close()

    def on_start(self, event):
        self.container = event.container
        self.timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        # Step 1: Create a receiver with name ReceiverA to address LargePresettledReleasedLinkCounterTest
        # This receiver is attached to router B. Later a sender will be
        # created which will be connected to Router A. The sender will send
        # on the same address that the receiver is receiving on.
        self.receiver_conn = event.container.connect(self.receiver_addr)
        self.receiver = event.container.create_receiver(self.receiver_conn,
                                                        self.dest,
                                                        name='ReceiverA')
        self.receiver.flow(self.receiver_dropoff_count)

    def on_link_opened(self, event):
        self.reactor = event.reactor
        if event.receiver:
            # Step 2: The receiver link has been opened.
            # Give 2 seconds for the address to propagate to the other router (Router A)
            self.address_check_timer = event.reactor.schedule(2, AddressCheckerTimeout(self))
            self.num_attempts += 1

    def on_sendable(self, event):
        # Step 4: Send self.num_messages multi-frame large pre-settled messages.
        # These messages will travel over inter-router link to Router B.
        while self.n_sent < self.num_messages:
            msg = Message(body=LARGE_PAYLOAD)
            dlv = self.sender.send(msg)
            # We are sending a pre-settled large multi frame message.
            dlv.settle()
            self.n_sent += 1

    def on_message(self, event):
        if self.receiver == event.receiver and not self.done:
            self.n_received += 1
            # Step 5: The receiver receives only 50 messages out of the 200
            # messages and drops out.
            if self.n_received == self.receiver_dropoff_count:
                self.done = True
                self.receiver_conn.close()
                self.receiver_conn_closed = True
                self.count_check_timer = event.reactor.schedule(3, CounterCheckerTimeout(self))

    def run(self):
        Container(self).run()


class TwoRouterLargeMessagePresettledCountTest(TestCase):
    @classmethod
    def setUpClass(cls):
        super(TwoRouterLargeMessagePresettledCountTest, cls).setUpClass()

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
            ('connector', {'name': 'connectorToA', 'role': 'inter-router', 'port': listen_port_inter_router}),
        ])

        cls.routers = []
        cls.routers.append(cls.tester.qdrouterd("A", config_1, wait=True))
        cls.routers.append(cls.tester.qdrouterd("B", config_2, wait=True))
        cls.routers[1].wait_router_connected('A')

    def test_verify_inter_router_presettled_count_DISPATCH_1540(self):
        sender_address = self.routers[0].addresses[0]
        receiver_address = self.routers[1].addresses[0]
        # Sends presettled large messages across routers and checks
        # the pre-settled count on the inter-router link of the downstream
        # router (i.e. that to which receiver is attached)
        # This test will fail if DISPATCH-1540 is not fixed since the
        # pre-settled count will show zero
        test = LargePresettledLinkCounterTest(sender_address, receiver_address)
        test.run()
        self.assertTrue(test.success)


class TwoRouterLargeMessagePresettledReleasedCountTest(TestCase):
    @classmethod
    def setUpClass(cls):
        super(TwoRouterLargeMessagePresettledReleasedCountTest, cls).setUpClass()

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
            ('connector', {'name': 'connectorToA', 'role': 'inter-router', 'port': listen_port_inter_router}),
        ])

        cls.routers = []
        cls.routers.append(cls.tester.qdrouterd("A", config_1, wait=True))
        cls.routers.append(cls.tester.qdrouterd("B", config_2, wait=True))
        cls.routers[1].wait_router_connected('A')

    def test_verify_inter_router_presettled_released_count_DISPATCH_1541(self):
        # This test sends presettled large messages across routers. A sender is on
        # router A and a receiver on B. The sender sends 200 messages, the receiver
        # receives 50 messages and goes away by closing its connection. There should be no released or
        # modified messages on the incoming inter-router link on Router B
        # This test will fail without the patch to DISPATCH-1541
        sender_address = self.routers[0].addresses[0]
        receiver_address = self.routers[1].addresses[0]
        test = LargePresettledReleasedLinkCounterTest(sender_address, receiver_address)
        test.run()
        self.assertTrue(test.success)


class LinkRouteIngressEgressTransitTest(TestCase):
    @classmethod
    def setUpClass(cls):
        """Start three routers"""
        super(LinkRouteIngressEgressTransitTest, cls).setUpClass()

        def router(name, connection):

            config = [
                ('router', {'mode': 'interior', 'id': 'QDR.%s' % name}),
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
        cls.routers[2].wait_address("pulp.task", remotes=1, delay=3, count=2)

        # This is not a classic router network in the sense that QDR.A and D are acting as brokers. We allow a little
        # bit more time for the routers to stabilize.
        sleep(2)

    def link_route_ingress_egress_transit_counts(self, large_message=False):
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

        num_messages = 10
        # Send and receive on the same router, router C
        test = IngressEgressTransitLinkRouteTest(address1, address2, num_messages, large_message=large_message)
        test.run()
        local_node = Node.connect(address1, timeout=TIMEOUT)
        outs = local_node.query(type='org.apache.qpid.dispatch.router')

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
                                             _LINK_STATISTIC_KEYS - {'deliveryCount',
                                                                     'acceptedCount'}))

        self.assertEqual(test.receiver_stats['deliveryCount'], 10)
        self.assertEqual(test.receiver_stats['acceptedCount'], 10)
        self.assertTrue(_link_stats_are_zero(test.receiver_stats,
                                             _LINK_STATISTIC_KEYS - {'deliveryCount',
                                                                     'acceptedCount'}))

    def test_link_route_ingress_egress_transit_counts(self):
        self.link_route_ingress_egress_transit_counts()

    def test_link_route_large_message_ingress_egress_transit_counts(self):
        self.link_route_ingress_egress_transit_counts(True)


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
            ('connector', {'name': 'connectorToA', 'role': 'inter-router', 'port': listen_port_inter_router}),
        ])

        cls.routers = []
        cls.routers.append(cls.tester.qdrouterd("A", config_1, wait=True))
        cls.routers.append(cls.tester.qdrouterd("B", config_2, wait=True))
        cls.routers[1].wait_router_connected('A')

    def two_router_ingress_egress_counts(self, large_message=False):
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
        num_messages = 10
        test = IngressEgressTwoRouterTest(in_router_addr, out_router_addr, num_messages, large_message=large_message)
        test.run()

        # Gather the values for deliveries_ingress and deliveries_egress after running the test.
        local_node = Node.connect(in_router_addr, timeout=TIMEOUT)
        outs = local_node.query(type='org.apache.qpid.dispatch.router')
        results = outs.results[0]

        post_deliveries_ingresss = results[deliveries_ingress_index]

        local_node = Node.connect(out_router_addr, timeout=TIMEOUT)
        outs = local_node.query(type='org.apache.qpid.dispatch.router')
        results = outs.results[0]

        post_deliveries_egress = results[deliveries_egress_index]
        post_deliveries_accepted = results[deliveries_accepted_index]

        accepted_deliveries_diff = post_deliveries_accepted - pre_deliveries_accepted

        # 12 = 10 msgs + 2 mgmt requests
        self.assertEqual(post_deliveries_ingresss - pre_deliveries_ingresss, 12)
        self.assertEqual(post_deliveries_egress - pre_deliveries_egress, 12)

        # check the link statistics
        self.assertEqual(test.sender_stats['deliveryCount'], num_messages)
        self.assertEqual(test.sender_stats['acceptedCount'], num_messages)
        self.assertTrue(_link_stats_are_zero(test.sender_stats,
                                             _LINK_STATISTIC_KEYS - {'deliveryCount',
                                                                     'acceptedCount'}))

        self.assertEqual(test.receiver_stats['deliveryCount'], num_messages)
        self.assertEqual(test.receiver_stats['acceptedCount'], num_messages)
        self.assertTrue(_link_stats_are_zero(test.receiver_stats,
                                             _LINK_STATISTIC_KEYS - {'deliveryCount',
                                                                     'acceptedCount'}))

        # The management requests are counted in the acceptedDeliveries, so it is difficult to measure the
        # exact number of accepted deliveries at this point in time. But it must at least be 10 since
        # we know for sure from the test that the 10 dispositions related to the 10 sent messages
        # were definitely received
        self.assertTrue(accepted_deliveries_diff >= num_messages)

    def test_two_router_ingress_egress_counts(self):
        self.two_router_ingress_egress_counts()

    def test_two_router_large_message_ingress_egress_counts(self):
        self.two_router_ingress_egress_counts(True)


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

    def one_router_ingress_egress_counts(self, large_message=False):
        address = self.router.addresses[0]

        local_node = Node.connect(address, timeout=TIMEOUT)
        outs = local_node.query(type='org.apache.qpid.dispatch.router')

        deliveries_ingress_index = outs.attribute_names.index('deliveriesIngress')
        deliveries_egress_index = outs.attribute_names.index('deliveriesEgress')
        results = outs.results[0]
        deliveries_ingress_pre_test = results[deliveries_ingress_index]
        deliveries_egress_pre_test = results[deliveries_egress_index]

        num_messages = 10
        test = IngressEgressOneRouterTest(address, num_messages, large_message=large_message)
        test.run()

        outs = local_node.query(type='org.apache.qpid.dispatch.router')

        results = outs.results[0]

        # 13 = ten msgs + 3 mgmt requests
        self.assertEqual(results[deliveries_ingress_index] - deliveries_ingress_pre_test, 13)
        # 12 = ten msgs + 2 mgmt requests
        self.assertEqual(results[deliveries_egress_index] - deliveries_egress_pre_test, 13)

        # check the link statistics
        self.assertEqual(test.sender_stats['deliveryCount'], num_messages)
        self.assertEqual(test.sender_stats['acceptedCount'], num_messages)
        self.assertTrue(_link_stats_are_zero(test.sender_stats,
                                             _LINK_STATISTIC_KEYS - {'deliveryCount',
                                                                     'acceptedCount'}))

        self.assertEqual(test.receiver_stats['deliveryCount'], num_messages)
        self.assertEqual(test.receiver_stats['acceptedCount'], num_messages)
        self.assertTrue(_link_stats_are_zero(test.receiver_stats,
                                             _LINK_STATISTIC_KEYS - {'deliveryCount',
                                                                     'acceptedCount'}))

    def test_one_router_ingress_egress_counts(self):
        self.one_router_ingress_egress_counts()

    def test_one_router_large_message_ingress_egress_counts(self):
        self.one_router_ingress_egress_counts(True)


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

    def route_container_egress(self , large_message=False):
        regular_addr = self.router.addresses[0]
        route_container_addr = self.router.addresses[1]
        num_messages = 10
        local_node = Node.connect(regular_addr, timeout=TIMEOUT)
        outs = local_node.query(type='org.apache.qpid.dispatch.router')
        deliveries_egress_route_container_index = outs.attribute_names.index('deliveriesEgressRouteContainer')
        results = outs.results[0]

        deliveries_egress_pre_test = results[deliveries_egress_route_container_index]

        test = RouteContainerEgressTest(route_container_addr, regular_addr, num_messages, large_message=large_message)
        test.run()

        outs = local_node.query(type='org.apache.qpid.dispatch.router')
        deliveries_egress_route_container_index = outs.attribute_names.index('deliveriesEgressRouteContainer')

        results = outs.results[0]
        # 11 = 10 msgs + 1 mgmt msg
        self.assertEqual(results[deliveries_egress_route_container_index] - deliveries_egress_pre_test, 11)

        # check link statistics
        self.assertEqual(test.sender_stats['deliveryCount'], num_messages)
        self.assertEqual(test.sender_stats['acceptedCount'], num_messages)
        self.assertTrue(_link_stats_are_zero(test.sender_stats,
                                             _LINK_STATISTIC_KEYS - {'deliveryCount',
                                                                     'acceptedCount'}))

        self.assertEqual(test.receiver_stats['deliveryCount'], num_messages)
        self.assertEqual(test.receiver_stats['acceptedCount'], num_messages)
        self.assertTrue(_link_stats_are_zero(test.receiver_stats,
                                             _LINK_STATISTIC_KEYS - {'deliveryCount',
                                                                     'acceptedCount'}))

    def test_route_container_egress_count(self):
        self.route_container_egress()

    def test_route_container_large_message_egress_count(self):
        self.route_container_egress(True)


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
                     credit=None, presettled=False, outcome=None,
                     large_message=False):
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
            self.received = 0
            self.timer = 0
            self.poll_timer = None
            self.conn = None
            self.sender_stats = None
            self.receiver_stats = None
            self.large_message = large_message
            self.reactor = None
            self.sender = None
            self.receiver = None
            self.max_attempts = 10
            self.num_attempts = 0

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

        def _get_sender_receiver_stats(self):
            self.receiver_stats = get_link_info("Rx_Test01", self.router_addr)
            self.sender_stats = get_link_info("Tx_Test01", self.router_addr)

        def poll_timeout(self):
            """
            Periodically check the deliveryCount or the releasedCount on the receiver and sender.
            """
            restart_poll_timer = True
            self._get_sender_receiver_stats()
            if self.receiver_stats and self.outcome == Delivery.RELEASED and \
                    self.receiver_stats['releasedCount'] == self.rx_limit:
                if self.sender_stats and self.sender_stats['releasedCount'] == self.rx_limit:
                    # We do not want to check just the deliveryCount here. The deliveryCount gets
                    # updated much earlier than the releasedCount. We will check the releasedCount instead.
                    # Check the releasedCount on the sender and the receiver. This is because it takes time
                    # to propagate the outcome back to the sender the sender's outcomes would lag behind the receivers
                    restart_poll_timer = False
                    self._cleanup()
            elif self.receiver_stats and self.receiver_stats['deliveryCount'] == self.rx_limit:
                restart_poll_timer = False
                self._cleanup()

            if restart_poll_timer:
                self.num_attempts += 1
                if self.num_attempts == self.max_attempts:
                    # There is something wrong, fail the test.
                    self.timeout()
                else:
                    self.poll_timer = self.reactor.schedule(0.5, PollTimeout(self))

        def on_start(self, event):
            self.reactor = event.reactor
            self.timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))
            self.conn = event.container.connect(self.router_addr)
            self.receiver = event.container.create_receiver(self.conn,
                                                            source="Test01",
                                                            name='Rx_Test01')
            self.receiver.flow(self.credit)
            self.sender = event.container.create_sender(self.conn,
                                                        target="Test01",
                                                        name="Tx_Test01")

        def on_sendable(self, event):
            while self.sent < self.count:
                if self.large_message:
                    dlv = self.sender.send(Message(body=LARGE_PAYLOAD))
                else:
                    dlv = self.sender.send(Message(body="Test01"))
                if self.presettled:
                    dlv.settle()
                self.sent += 1

        def on_message(self, event):
            self.received += 1
            if self.outcome:
                event.delivery.update(self.outcome)
                event.delivery.settle()

            # Start up a poll timer once all the deliveries have been sent/received.
            if self.received == self.rx_limit and self.sent ==  self.count:
                self.poll_timer = event.reactor.schedule(0.5, PollTimeout(self))

        def run(self):
            Container(self).run()

    def verify_released(self, large_message=False):
        """
        Verify the link released count by releasing all received messages
        """
        test = self.LinkCountersTest(self.router.addresses[0],
                                     outcome=Delivery.RELEASED,
                                     large_message=large_message)
        test.run()
        self.assertEqual(test.receiver_stats['deliveryCount'], self.COUNT)
        self.assertEqual(test.receiver_stats['releasedCount'], self.COUNT)
        self.assertTrue(_link_stats_are_zero(test.receiver_stats,
                                             _LINK_STATISTIC_KEYS
                                             - {'deliveryCount',
                                                'releasedCount'}))

        self.assertEqual(test.sender_stats['deliveryCount'], self.COUNT)
        self.assertEqual(test.sender_stats['releasedCount'], self.COUNT)
        self.assertTrue(_link_stats_are_zero(test.sender_stats,
                                             _LINK_STATISTIC_KEYS
                                             - {'deliveryCount',
                                                'releasedCount'}))

    def verify_unsettled_count(self, large_message=False):
        """
        Verify the link unsettled count by granting less credit than required
        by the sender
        """
        test = self.LinkCountersTest(self.router.addresses[0],
                                     presettled=False,
                                     count=self.COUNT,
                                     rx_limit=self.CREDIT,
                                     credit=self.CREDIT,
                                     large_message=large_message)
        test.run()

        # expect the receiver to get rx_limit worth of unsettled deliveries
        self.assertEqual(test.receiver_stats['deliveryCount'], self.CREDIT)
        self.assertEqual(test.receiver_stats['unsettledCount'], self.CREDIT)
        self.assertTrue(_link_stats_are_zero(test.receiver_stats,
                                             _LINK_STATISTIC_KEYS
                                             - {'deliveryCount',
                                                'unsettledCount'}))

        # expect sender only to be able to send as much as credit
        self.assertEqual(test.sender_stats['deliveryCount'], self.CREDIT)
        self.assertEqual(test.sender_stats['unsettledCount'], self.CREDIT)
        self.assertTrue(_link_stats_are_zero(test.sender_stats,
                                             _LINK_STATISTIC_KEYS
                                             - {'deliveryCount',
                                                'unsettledCount'}))

    def verify_presettled_count(self, large_message=False):
        """
        Verify the presettled dropped count link counter by exhausting credit
        before sending is complete
        """
        limit = self.CREDIT // 2  # 1/2 the capacity given the sender
        test = self.LinkCountersTest(self.router.addresses[0],
                                     presettled=True,
                                     count=self.COUNT,
                                     rx_limit=limit,
                                     credit=limit,
                                     large_message=large_message)
        test.run()

        # since these are presettled the sender should have credit
        # replenished by the router after each message.
        self.assertEqual(test.sender_stats['deliveryCount'], self.COUNT)
        self.assertEqual(test.sender_stats['presettledCount'], self.COUNT)
        self.assertTrue(_link_stats_are_zero(test.sender_stats,
                                             _LINK_STATISTIC_KEYS
                                             - {'deliveryCount',
                                                'presettledCount'}))

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
                                             - {'deliveryCount',
                                                'undeliveredCount',
                                                'droppedPresettledCount',
                                                'presettledCount'}))

    def verify_one_credit_accepted(self, large_message=False):
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
                                             - {'deliveryCount',
                                                'undeliveredCount',
                                                'acceptedCount'}))

        # expect that one message will be delivered, then link capacity
        # messages will be enqueued internally
        self.assertEqual(test.sender_stats['unsettledCount'], self.CREDIT)
        self.assertEqual(test.sender_stats['deliveryCount'], self.CREDIT + 1)
        self.assertEqual(test.sender_stats['acceptedCount'], 1)
        self.assertTrue(_link_stats_are_zero(test.sender_stats,
                                             _LINK_STATISTIC_KEYS
                                             - {'deliveryCount',
                                                'unsettledCount',
                                                'acceptedCount'}))

    def test_01_presettled(self):
        self.verify_presettled_count()

    def test_02_large_mesage_presettled(self):
        self.verify_presettled_count(True)

    def test_03_unsettled(self):
        self.verify_presettled_count()

    def test_04_large_message_unsettled(self):
        self.verify_presettled_count(True)

    def test_05_released(self):
        self.verify_released()

    def test_06_large_message_released(self):
        self.verify_released(True)

    def test_07_one_credit_accepted(self):
        self.verify_one_credit_accepted()

    def test_08_large_message_one_credit_accepted(self):
        self.verify_one_credit_accepted(True)


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
                                             _LINK_STATISTIC_KEYS - {'deliveryCount',
                                                                     'acceptedCount'}))
        self.assertEqual(test.sender1_stats['deliveryCount'], 10)
        self.assertEqual(test.sender1_stats['acceptedCount'], 10)
        self.assertTrue(_link_stats_are_zero(test.sender1_stats,
                                             _LINK_STATISTIC_KEYS - {'deliveryCount',
                                                                     'acceptedCount'}))
        self.assertEqual(test.receiver_stats['deliveryCount'], 20)
        self.assertEqual(test.receiver_stats['acceptedCount'], 20)
        self.assertTrue(_link_stats_are_zero(test.receiver_stats,
                                             _LINK_STATISTIC_KEYS - {'deliveryCount',
                                                                     'acceptedCount'}))


class IngressEgressTwoRouterTest(MessagingHandler):
    def __init__(self, sender_address, receiver_address, num_messages, large_message=False):
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
        self.num_messages = num_messages
        self.start = False
        self.n_accept = 0
        self.sender_stats = None
        self.receiver_stats = None
        self.done = False
        self.large_message = large_message

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
        self.timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.conn_recv = event.container.connect(self.receiver_address)
        self.receiver = event.container.create_receiver(self.conn_recv,
                                                        source=self.dest,
                                                        name='Rx_IngressEgressTwoRouterTest')

    def on_sendable(self, event):
        if not self.start:
            return

        if self.n_sent < self.num_messages:
            msg = Message(body=get_body(self.n_sent, self.large_message))
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
    def __init__(self, address, num_messages, large_message=False):
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
        self.num_messages = num_messages
        self.sender_stats = None
        self.receiver_stats = None
        self.done = False
        self.large_message = large_message

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
        self.timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.conn = event.container.connect(self.address)
        self.sender = event.container.create_sender(self.conn,
                                                    target=self.dest,
                                                    name='Tx_IngressEgressOneRouterTest')
        self.receiver = event.container.create_receiver(self.conn,
                                                        source=self.dest,
                                                        name='Rx_IngressEgressOneRouterTest')

    def on_sendable(self, event):
        if self.n_sent < self.num_messages:
            msg = Message(body=get_body(self.n_sent, self.large_message))
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
    def __init__(self, route_container_addr, sender_addr, num_messages, large_message=False):
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
        self.num_messages = num_messages
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
        self.timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))
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
        self.timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))
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
    def __init__(self, sender_addr, receiver_addr, num_messages, large_message=False):
        super(IngressEgressTransitLinkRouteTest, self).__init__()
        self.timer = None
        self.receiver_conn = None
        self.receiver = None
        self.sender = None
        self.sender_conn = None
        self.dest = "pulp.task"
        self.start = False
        self.n_sent = 0
        self.num_messages = num_messages
        self.n_received = 0
        self.n_accepted = 0
        self.sender_addr = sender_addr
        self.receiver_addr = receiver_addr
        self.error = None
        self.sender_stats = None
        self.receiver_stats = None
        self.done = False
        self.large_message = large_message

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
        self.timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))
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
            msg = Message(body=get_body(self.n_sent, self.large_message))
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
    def __init__(self, sender_addr, num_messages, large_message=False):
        super(ReleasedDroppedPresettledCountTest, self).__init__()
        self.timer = None
        self.sender_conn = None
        self.sender = None
        self.error = None
        self.n_sent = 0
        self.num_messages = num_messages
        self.sender_addr = sender_addr
        self.sender_stats = None

        # We are sending to a multicast address
        self.dest = "multicast"
        self.n_released = 0
        self.expect_released = 10
        self.done = False
        self.large_message = large_message

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
        self.timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.sender_conn = event.container.connect(self.sender_addr)

        # Note that this is an anonymous link which will be granted credit w/o
        # blocking for consumers.  Therefore all messages sent to this address
        # will be dropped
        self.sender = event.container.create_sender(self.sender_conn,
                                                    name='ReleasedDroppedPresettledCountTest')

    def on_sendable(self, event):
        # We are sending a total of 20 deliveries. 10 unsettled and 10 pre-settled to a multicast address
        if self.n_sent < self.num_messages:
            msg = Message(body=get_body(self.n_sent, self.large_message))
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
    def __init__(self, addr, num_messages, large_message=False):
        super(RejectedDeliveriesTest, self).__init__(auto_accept=False)
        self.addr = addr
        self.dest = "someaddress"
        self.error = None
        self.n_sent = 0
        self.num_messages = num_messages
        self.n_rejected = 0
        self.sender_conn = None
        self.receiver_conn = None
        self.timer = None
        self.sender = None
        self.receiver = None
        self.sender_stats = None
        self.receiver_stats = None
        self.done = False
        self.large_message = large_message

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
        self.timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))
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
            msg = Message(body=get_body(self.n_sent, self.large_message))
            self.sender.send(msg)
            self.n_sent += 1

    def run(self):
        Container(self).run()


class ModifiedDeliveriesTest(MessagingHandler):
    def __init__(self, addr, num_messages, large_message=False):
        super(ModifiedDeliveriesTest, self).__init__(auto_accept=False)
        self.addr = addr
        self.dest = "someaddress"
        self.error = None
        self.n_sent = 0
        self.num_messages = num_messages
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
        self.large_message = large_message

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
        self.timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))
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
            msg = Message(body=get_body(self.n_sent, self.large_message))
            self.sender.send(msg)
            self.n_sent += 1

    def run(self):
        Container(self).run()

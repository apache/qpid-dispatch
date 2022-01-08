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

#
# Test the multicast forwarder
#

import abc
import sys
from time import sleep

from proton.handlers import MessagingHandler
from proton.reactor import Container
from proton.reactor import LinkOption
from proton import Connection
from proton import Link
from proton import Message
from proton import Delivery
from system_test import AsyncTestSender, AsyncTestReceiver, TestCase, Qdrouterd, main_module, TIMEOUT, TestTimeout, unittest


MAX_FRAME = 1023
LINK_CAPACITY = 250
W_THREADS = 2
LARGE_PAYLOAD = ("X" * MAX_FRAME) * 19

# check for leaks of the following entities
ALLOC_STATS = ["qd_message_t",
               "qd_buffer_t",
               "qdr_delivery_t"]


class MulticastLinearTest(TestCase):
    """
    Verify the multicast forwarding logic across a multihop linear router
    configuration
    """
    @classmethod
    def setUpClass(cls):
        """Start a router"""
        super(MulticastLinearTest, cls).setUpClass()

        def router(name, mode, extra):
            config = [
                ('router', {'mode': mode,
                            'id': name,
                            'allowUnsettledMulticast': 'yes',
                            'workerThreads': W_THREADS}),
                ('listener', {'role': 'normal',
                              'port': cls.tester.get_port(),
                              'maxFrameSize': MAX_FRAME,
                              'linkCapacity': LINK_CAPACITY}),
                ('address', {'prefix': 'multicast', 'distribution': 'multicast'}),
            ]

            if extra:
                config.extend(extra)
            config = Qdrouterd.Config(config)
            cls.routers.append(cls.tester.qdrouterd(name, config, wait=True))
            return cls.routers[-1]

        # configuration:
        # two edge routers connected via 2 interior routers.
        #
        #  +-------+    +---------+    +---------+    +-------+
        #  |  EA1  |<==>|  INT.A  |<==>|  INT.B  |<==>|  EB1  |
        #  +-------+    +---------+    +---------+    +-------+
        #
        # Each router has 2 multicast consumers
        # EA1 and INT.A each have a multicast sender

        cls.routers = []

        interrouter_port = cls.tester.get_port()
        cls.INTA_edge_port   = cls.tester.get_port()
        cls.INTB_edge_port   = cls.tester.get_port()

        router('INT.A', 'interior',
               [('listener', {'role': 'inter-router',
                              'port': interrouter_port}),
                ('listener', {'role': 'edge', 'port': cls.INTA_edge_port})])
        cls.INT_A = cls.routers[0]
        cls.INT_A.listener = cls.INT_A.addresses[0]

        router('INT.B', 'interior',
               [('connector', {'name': 'connectorToA',
                               'role': 'inter-router',
                               'port': interrouter_port}),
                ('listener', {'role': 'edge',
                              'port': cls.INTB_edge_port})])

        cls.INT_B = cls.routers[1]
        cls.INT_B.listener = cls.INT_B.addresses[0]

        router('EA1', 'edge',
               [('listener', {'name': 'rc', 'role': 'route-container',
                              'port': cls.tester.get_port()}),
                ('connector', {'name': 'uplink', 'role': 'edge',
                               'port': cls.INTA_edge_port}),
                ('linkRoute', {'prefix': 'CfgLinkRoute1', 'containerId': 'FakeBroker', 'direction': 'in'}),
                ('linkRoute', {'prefix': 'CfgLinkRoute1', 'containerId': 'FakeBroker', 'direction': 'out'})])
        cls.EA1 = cls.routers[2]
        cls.EA1.listener = cls.EA1.addresses[0]
        cls.EA1.route_container = cls.EA1.addresses[1]

        router('EB1', 'edge',
               [('connector', {'name': 'uplink',
                               'role': 'edge',
                               'port': cls.INTB_edge_port,
                               'maxFrameSize': 1024}),
                ('listener', {'name': 'rc', 'role': 'route-container',
                              'port': cls.tester.get_port()}),
                ('linkRoute', {'pattern': '*.cfg.pattern.#', 'containerId': 'FakeBroker', 'direction': 'in'}),
                ('linkRoute', {'pattern': '*.cfg.pattern.#', 'containerId': 'FakeBroker', 'direction': 'out'})])
        cls.EB1 = cls.routers[3]
        cls.EB1.listener = cls.EB1.addresses[0]
        cls.EB1.route_container = cls.EB1.addresses[1]

        cls.INT_A.wait_router_connected('INT.B')
        cls.INT_B.wait_router_connected('INT.A')
        cls.EA1.wait_connectors()
        cls.EB1.wait_connectors()

        # Client topology:
        # all routes have 2 receivers
        # Edge router EA1 and interior INT_A have a sender each
        #
        cls.config = [
            # edge router EA1:
            {'router':      cls.EA1,
             'senders':     ['S-EA1-1'],
             'receivers':   ['R-EA1-1', 'R-EA1-2'],
             'subscribers': 2,
             'remotes':     0
             },
            # Interior router INT_A:
            {'router':      cls.INT_A,
             'senders':     ['S-INT_A-1'],
             'receivers':   ['R-INT_A-1', 'R-INT_A-2'],
             'subscribers': 3,
             'remotes':     1,
             },
            # Interior router INT_B:
            {'router':      cls.INT_B,
             'senders':     [],
             'receivers':   ['R-INT_B-1', 'R-INT_B-2'],
             'subscribers': 3,
             'remotes':     1,
             },
            # edge router EB1
            {'router':      cls.EB1,
             'senders':     [],
             'receivers':   ['R-EB1-1', 'R-EB1-2'],
             'subscribers': 2,
             'remotes':     0,
             }
        ]

    def _get_alloc_stats(self, router, stats):
        # return a map of the current allocator counters for each entity type
        # name in stats

        #
        # 57: END = [{u'heldByThreads': int32(384), u'typeSize': int32(536),
        # u'transferBatchSize': int32(64), u'globalFreeListMax': int32(0),
        # u'batchesRebalancedToGlobal': int32(774), u'typeName':
        # u'qd_buffer_t', u'batchesRebalancedToThreads': int32(736),
        # u'totalFreeToHeap': int32(0), u'totalAllocFromHeap': int32(2816),
        # u'localFreeListMax': int32(128), u'type':
        # u'org.apache.qpid.dispatch.allocator', u'identity':
        # u'allocator/qd_buffer_t', u'name': u'allocator/qd_buffer_t'}]

        d = dict()
        mgmt = router.management
        atype = 'org.apache.qpid.dispatch.allocator'
        q = mgmt.query(type=atype).get_dicts()
        for name in stats:
            d[name] = next(a for a in q if a['typeName'] == name)
        return d

    def _check_for_leaks(self):
        for r in self.routers:
            stats = self._get_alloc_stats(r, ALLOC_STATS)
            for name in ALLOC_STATS:
                # ensure threads haven't leaked
                max_allowed  = ((W_THREADS + 1)
                                * stats[name]['localFreeListMax'])
                held = stats[name]['heldByThreads']
                if held >= (2 * max_allowed):
                    print("OOPS!!! %s: (%s) - held=%d max=%d\n   %s\n"
                          % (r.config.router_id,
                             name, held, max_allowed, stats))
                    sys.stdout.flush()
                    self.assertFalse(held >= (2 * max_allowed))

    #
    # run all the negative tests first so that if we screw up the internal
    # state of the brokers the positive tests will likely fail
    #

    def _presettled_large_msg_rx_detach(self, config, count, drop_clients):
        # detach receivers during receive
        body = " MCAST PRESETTLED LARGE RX DETACH " + LARGE_PAYLOAD
        test = MulticastPresettledRxFail(config, count,
                                         drop_clients,
                                         detach=True,
                                         body=body)
        test.run()
        self.assertIsNone(test.error)

    def test_01_presettled_large_msg_rx_detach(self):
        self._presettled_large_msg_rx_detach(self.config, 10, ['R-EA1-1', 'R-EB1-2'])
        self._presettled_large_msg_rx_detach(self.config, 10, ['R-INT_A-2', 'R-INT_B-1'])

    def _presettled_large_msg_rx_close(self, config, count, drop_clients):
        # close receiver connections during receive
        body = " MCAST PRESETTLED LARGE RX CLOSE " + LARGE_PAYLOAD
        test = MulticastPresettledRxFail(config, count,
                                         drop_clients,
                                         detach=False,
                                         body=body)
        test.run()
        self.assertIsNone(test.error)

    def test_02_presettled_large_msg_rx_close(self):
        self._presettled_large_msg_rx_close(self.config, 10, ['R-EA1-2', 'R-EB1-1'])
        self._presettled_large_msg_rx_close(self.config, 10, ['R-INT_A-1', 'R-INT_B-2'])

    def _unsettled_large_msg_rx_detach(self, config, count, drop_clients):
        # detach receivers during the test
        body = " MCAST UNSETTLED LARGE RX DETACH " + LARGE_PAYLOAD
        test = MulticastUnsettledRxFail(self.config, count, drop_clients, detach=True, body=body)
        test.run()
        self.assertIsNone(test.error)

    def test_10_unsettled_large_msg_rx_detach(self):
        self._unsettled_large_msg_rx_detach(self.config, 10, ['R-EA1-1', 'R-EB1-2'])
        self._unsettled_large_msg_rx_detach(self.config, 10, ['R-INT_A-2', 'R-INT_B-1'])

    def _unsettled_large_msg_rx_close(self, config, count, drop_clients):
        # close receiver connections during test
        body = " MCAST UNSETTLED LARGE RX CLOSE " + LARGE_PAYLOAD
        test = MulticastUnsettledRxFail(self.config, count, drop_clients, detach=False, body=body)
        test.run()
        self.assertIsNone(test.error)

    def test_11_unsettled_large_msg_rx_close(self):
        self._unsettled_large_msg_rx_close(self.config, 10, ['R-EA1-2', 'R-EB1-1', ])
        self._unsettled_large_msg_rx_close(self.config, 10, ['R-INT_A-1', 'R-INT_B-2'])

    #
    # now the positive tests
    #

    def test_50_presettled(self):
        # Simply send a bunch of pre-settled multicast messages
        body = " MCAST PRESETTLED "
        test = MulticastPresettled(self.config, 10, body, SendPresettled())
        test.run()

    def test_51_presettled_mixed_large_msg(self):
        # Same as above, but large message bodies (mixed sender settle mode)
        body = " MCAST MAYBE PRESETTLED LARGE " + LARGE_PAYLOAD
        test = MulticastPresettled(self.config, 11, body, SendMixed())
        test.run()
        self.assertIsNone(test.error)

    def test_52_presettled_large_msg(self):
        # Same as above, (pre-settled sender settle mode)
        body = " MCAST PRESETTLED LARGE " + LARGE_PAYLOAD
        test = MulticastPresettled(self.config, 13, body, SendPresettled())
        test.run()
        self.assertIsNone(test.error)

    def test_60_unsettled_3ack(self):
        # Sender sends unsettled, waits for Outcome from Receiver then settles
        # Expect all messages to be accepted
        body = " MCAST UNSETTLED "
        test = MulticastUnsettled3Ack(self.config, 10, body)
        test.run()
        self.assertIsNone(test.error)
        self.assertEqual(test.n_outcomes[Delivery.ACCEPTED], test.n_sent)

    def test_61_unsettled_3ack_large_msg(self):
        # Same as above but with multiframe streaming
        body = " MCAST UNSETTLED LARGE " + LARGE_PAYLOAD
        test = MulticastUnsettled3Ack(self.config, 11, body=body)
        test.run()
        self.assertIsNone(test.error)
        self.assertEqual(test.n_outcomes[Delivery.ACCEPTED], test.n_sent)

    def _unsettled_3ack_outcomes(self,
                                 config,
                                 count,
                                 outcomes,
                                 expected):
        body = " MCAST UNSETTLED 3ACK OUTCOMES " + LARGE_PAYLOAD
        test = MulticastUnsettled3Ack(self.config,
                                      count,
                                      body,
                                      outcomes=outcomes)
        test.run()
        self.assertIsNone(test.error)
        self.assertEqual(test.n_outcomes[expected], test.n_sent)

    def test_63_unsettled_3ack_outcomes(self):
        # Verify the expected outcome is returned to the sender when the
        # receivers return different outcome values.  If no outcome is
        # specified for a receiver it will default to ACCEPTED

        # expect REJECTED if any reject:
        self._unsettled_3ack_outcomes(self.config, 3,
                                      {'R-EB1-1': Delivery.REJECTED,
                                       'R-EB1-2': Delivery.MODIFIED,
                                       'R-INT_B-2': Delivery.RELEASED},
                                      Delivery.REJECTED)
        self._unsettled_3ack_outcomes(self.config, 3,
                                      {'R-EB1-1': Delivery.REJECTED,
                                       'R-INT_B-2': Delivery.RELEASED},
                                      Delivery.REJECTED)
        # expect ACCEPT if no rejects
        self._unsettled_3ack_outcomes(self.config, 3,
                                      {'R-EB1-2': Delivery.MODIFIED,
                                       'R-INT_B-2': Delivery.RELEASED},
                                      Delivery.ACCEPTED)
        # expect MODIFIED over RELEASED
        self._unsettled_3ack_outcomes(self.config, 3,
                                      {'R-EA1-1': Delivery.RELEASED,
                                       'R-EA1-2': Delivery.RELEASED,
                                       'R-INT_A-1': Delivery.RELEASED,
                                       'R-INT_A-2': Delivery.RELEASED,
                                       'R-INT_B-1': Delivery.RELEASED,
                                       'R-INT_B-2': Delivery.RELEASED,
                                       'R-EB1-1': Delivery.RELEASED,
                                       'R-EB1-2': Delivery.MODIFIED},
                                      Delivery.MODIFIED)

        # and released only if all released
        self._unsettled_3ack_outcomes(self.config, 3,
                                      {'R-EA1-1': Delivery.RELEASED,
                                       'R-EA1-2': Delivery.RELEASED,
                                       'R-INT_A-1': Delivery.RELEASED,
                                       'R-INT_A-2': Delivery.RELEASED,
                                       'R-INT_B-1': Delivery.RELEASED,
                                       'R-INT_B-2': Delivery.RELEASED,
                                       'R-EB1-1': Delivery.RELEASED,
                                       'R-EB1-2': Delivery.RELEASED},
                                      Delivery.RELEASED)

    def test_70_unsettled_1ack(self):
        # Sender sends unsettled, expects both outcome and settlement from
        # receiver before sender settles locally
        body = " MCAST UNSETTLED 1ACK "
        test = MulticastUnsettled1Ack(self.config, 10, body)
        test.run()
        self.assertIsNone(test.error)

    def test_71_unsettled_1ack_large_msg(self):
        # Same as above but with multiframe streaming
        body = " MCAST UNSETTLED 1ACK LARGE " + LARGE_PAYLOAD
        test = MulticastUnsettled1Ack(self.config, 10, body)
        test.run()
        self.assertIsNone(test.error)

    def test_80_unsettled_3ack_message_annotations(self):
        body = " MCAST UNSETTLED 3ACK LARGE MESSAGE ANNOTATIONS " + LARGE_PAYLOAD
        test = MulticastUnsettled3AckMA(self.config, 10, body)
        test.run()
        self.assertIsNone(test.error)

    def test_90_credit_no_subscribers(self):
        """Verify that multicast senders are blocked until a consumer is present."""
        test = MulticastCreditBlocked(address=self.EA1.listener,
                                      target='multicast/no/subscriber1')

        test.run()
        self.assertIsNone(test.error)

        test = MulticastCreditBlocked(address=self.INT_A.listener,
                                      target='multicast/no/subscriber2')
        test.run()
        self.assertIsNone(test.error)

    def test_91_anonymous_sender(self):
        """
        Verify that senders over anonymous links do not block waiting for
        consumers.
        """

        # no receiver - should not block, return RELEASED
        msg = Message(body="test_100_anonymous_sender")
        msg.address = "multicast/test_100_anonymous_sender"
        tx = AsyncTestSender(address=self.INT_B.listener,
                             count=5,
                             target=None,
                             message=msg,
                             container_id="test_100_anonymous_sender")
        tx.wait()
        self.assertEqual(5, tx.released)

        # now add a receiver:
        rx = AsyncTestReceiver(address=self.INT_A.listener,
                               source=msg.address)
        self.INT_B.wait_address(msg.address)
        tx = AsyncTestSender(address=self.INT_B.listener,
                             count=5,
                             target=None,
                             message=msg,
                             container_id="test_100_anonymous_sender")
        tx.wait()
        self.assertEqual(5, tx.accepted)
        rx.stop()

    def test_999_check_for_leaks(self):
        self._check_for_leaks()


#
# Settlement options for Link attach
#

class SendPresettled(LinkOption):
    """All messages are sent presettled"""

    def apply(self, link):
        link.snd_settle_mode = Link.SND_SETTLED
        link.rcv_settle_mode = Link.RCV_FIRST


class SendMixed(LinkOption):
    """Messages may be sent unsettled or settled"""

    def apply(self, link):
        link.snd_settle_mode = Link.SND_MIXED
        link.rcv_settle_mode = Link.RCV_FIRST


class Link1Ack(LinkOption):
    """Messages will be sent unsettled"""

    def apply(self, link):
        link.snd_settle_mode = Link.SND_UNSETTLED
        link.rcv_settle_mode = Link.RCV_FIRST


class Link3Ack(LinkOption):
    """
    Messages will be sent unsettled and the receiver will wait for sender to
    settle first.
    """

    def apply(self, link):
        link.snd_settle_mode = Link.SND_UNSETTLED
        link.rcv_settle_mode = Link.RCV_SECOND


class MulticastBase(MessagingHandler, metaclass=abc.ABCMeta):
    """Common multicast boilerplate code"""

    def __init__(self, config, count, body, topic=None, **handler_kwargs):
        super(MulticastBase, self).__init__(**handler_kwargs)
        self.msg_count = count
        self.config = config
        self.topic = topic or "multicast/test"
        self.body = body

        # totals
        self.n_senders = 0
        self.n_receivers = 0
        self.n_sent = 0
        self.n_received = 0
        self.n_settled = 0
        self.n_accepted = 0
        self.n_released = 0
        self.n_rejected = 0
        self.n_modified = 0
        self.n_partial = 0

        # all maps indexed by client name:
        self.receivers = {}
        self.senders = {}
        self.r_conns = {}
        self.s_conns = {}

        # per receiver
        self.c_received = {}

        # count per outcome
        self.n_outcomes = {}

        self.error = None
        self.timers = []
        self.reactor = None

    def done(self):
        # stop the reactor and clean up the test
        for t in self.timers:
            t.cancel()
        for c_dict in [self.r_conns, self.s_conns]:
            for conn in c_dict.values():
                conn.close()
        self.r_conns = {}
        self.s_conns = {}

    def timeout(self):
        self.error = "Timeout Expired"
        self.done()

    @abc.abstractmethod
    def create_receiver(self, container, conn, source, name):
        """must override in subclass"""

    @abc.abstractmethod
    def create_sender(self, container, conn, target, name):
        """must override in subclass"""

    def on_start(self, event):
        self.reactor = event.reactor
        self.timers.append(self.reactor.schedule(TIMEOUT, TestTimeout(self)))
        # first create all the receivers first
        for cfg in self.config:
            for name in cfg['receivers']:
                conn = event.container.connect(cfg['router'].listener)
                assert name not in self.r_conns
                self.r_conns[name] = conn
                self.create_receiver(event.container, conn, self.topic, name)
                self.n_receivers += 1
                self.c_received[name] = 0

    def on_link_opened(self, event):
        if event.receiver:
            r_name = event.receiver.name
            self.receivers[r_name] = event.receiver
            # create senders after all receivers are opened
            # makes it easy to check when the clients are ready
            if len(self.receivers) == self.n_receivers:
                for cfg in self.config:
                    for name in cfg['senders']:
                        conn = event.container.connect(cfg['router'].listener)
                        assert name not in self.s_conns
                        self.s_conns[name] = conn
                        self.create_sender(event.container, conn, self.topic, name)
                        self.n_senders += 1

    def on_sendable(self, event):
        s_name = event.sender.name
        if s_name not in self.senders:
            self.senders[s_name] = event.sender
            if len(self.senders) == self.n_senders:
                # all senders ready to send, now wait until the routes settle
                for cfg in self.config:
                    cfg['router'].wait_address(self.topic,
                                               subscribers=cfg['subscribers'],
                                               remotes=cfg['remotes'])
                for sender in self.senders.values():
                    self.do_send(sender)

    def on_message(self, event):
        if event.delivery.partial:
            self.n_partial += 1
        else:
            dlv = event.delivery
            self.n_received += 1
            name = event.link.name
            self.c_received[name] = 1 + self.c_received.get(name, 0)

    def on_accepted(self, event):
        self.n_accepted += 1
        name = event.link.name
        self.n_outcomes[Delivery.ACCEPTED] = 1 + self.n_outcomes.get(Delivery.ACCEPTED, 0)

    def on_released(self, event):
        # for some reason Proton 'helpfully' calls on_released even though the
        # delivery state is actually MODIFIED
        if event.delivery.remote_state == Delivery.MODIFIED:
            return self.on_modified(event)
        self.n_released += 1
        name = event.link.name
        self.n_outcomes[Delivery.RELEASED] = 1 + self.n_outcomes.get(Delivery.RELEASED, 0)

    def on_modified(self, event):
        self.n_modified += 1
        name = event.link.name
        self.n_outcomes[Delivery.MODIFIED] = 1 + self.n_outcomes.get(Delivery.MODIFIED, 0)

    def on_rejected(self, event):
        self.n_rejected += 1
        name = event.link.name
        self.n_outcomes[Delivery.REJECTED] = 1 + self.n_outcomes.get(Delivery.REJECTED, 0)

    def on_settled(self, event):
        self.n_settled += 1

    def run(self):
        Container(self).run()

        # wait until all routers have cleaned up the route tables
        clean = False
        while not clean:
            clean = True
            for cfg in self.config:
                mgmt = cfg['router'].management
                atype = 'org.apache.qpid.dispatch.router.address'
                addrs = mgmt.query(type=atype).get_dicts()
                if any(self.topic in a['name'] for a in addrs):
                    clean = False
                    break
            if not clean:
                sleep(0.1)


class MulticastPresettled(MulticastBase):
    """
    Test multicast forwarding for presettled transfers.
    Verifies that all messages are settled by the sender
    """

    def __init__(self, config, count, body, settlement_mode):
        # use a large prefetch to prevent drops
        super(MulticastPresettled, self).__init__(config,
                                                  count,
                                                  body,
                                                  prefetch=(count * 1024),
                                                  auto_accept=False,
                                                  auto_settle=False)
        self.settlement_mode = settlement_mode
        self.unexpected_unsettled = 0
        self.expected_settled = 0
        self.sender_settled = 0
        self.done_count = 0
        self.unsettled_deliveries = dict()

    def create_receiver(self, container, conn, source, name):
        return container.create_receiver(conn, source=source, name=name,
                                         options=self.settlement_mode)

    def create_sender(self, container, conn, target, name):
        return container.create_sender(conn, target=target, name=name,
                                       options=self.settlement_mode)

    def do_send(self, sender):
        for i in range(self.msg_count):
            msg = Message(body=" %s -> %s:%s" % (sender.name, i, self.body))
            dlv = sender.send(msg)
            # settled before sending out the message
            dlv.settle()
            self.n_sent += 1

    def check_if_done(self):
        # wait for all present receivers to receive all messages
        # and for all received messages to be settled by the
        # sender
        to_rcv = self.n_senders * self.msg_count * self.n_receivers
        if to_rcv == self.n_received and not self.unsettled_deliveries:
            self.done()

    def on_message(self, event):
        super(MulticastPresettled, self).on_message(event)
        if event.receiver:
            if not event.delivery.settled:
                # it may be that settle will come after on_message
                # so track that here
                event.delivery.update(Delivery.ACCEPTED)
                self.unexpected_unsettled += 1
                tag = str(event.delivery.tag)
                if tag not in self.unsettled_deliveries:
                    self.unsettled_deliveries[tag] = 1
                else:
                    self.unsettled_deliveries[tag] += 1
            else:
                self.expected_settled += 1
            event.receiver.flow(100)
        self.check_if_done()

    def on_settled(self, event):
        super(MulticastPresettled, self).on_settled(event)
        if event.receiver:
            self.sender_settled += 1
            tag = str(event.delivery.tag)
            try:
                # got a delayed settle
                self.unsettled_deliveries[tag] -= 1
                if self.unsettled_deliveries[tag] == 0:
                    del self.unsettled_deliveries[tag]
            except KeyError:
                pass
            self.check_if_done()


class MulticastPresettledRxFail(MulticastPresettled):
    """Spontaneously close a receiver or connection on message received"""

    def __init__(self, config, count, drop_clients, detach, body):
        super(MulticastPresettledRxFail, self).__init__(config, count, body, SendPresettled())
        self.drop_clients = drop_clients
        self.detach = detach

    def check_if_done(self):
        # Verify each receiver got the expected number of messages.
        # Avoid waiting for dropped receivers.
        done = True
        to_rcv = self.n_senders * self.msg_count
        for name, count in self.c_received.items():
            if name not in self.drop_clients:
                if count != to_rcv:
                    done = False
        if done:
            self.done()

    def on_message(self, event):
        # close the receiver on arrival of the first message
        r_name = event.receiver.name
        if r_name in self.drop_clients:
            if self.detach:
                if event.receiver.state & Link.LOCAL_ACTIVE:
                    event.receiver.close()
            elif event.connection.state & Connection.LOCAL_ACTIVE:
                event.connection.close()
        super(MulticastPresettledRxFail, self).on_message(event)


class MulticastUnsettled3Ack(MulticastBase):
    """
    Send count messages per sender, senders wait for terminal outcome from
    receivers before settling
    """

    def __init__(self, config, count, body, outcomes=None):
        pfetch = int((count + 1) / 2)
        super(MulticastUnsettled3Ack, self).__init__(config,
                                                     count,
                                                     body,
                                                     prefetch=pfetch,
                                                     auto_accept=False,
                                                     auto_settle=False)
        self.outcomes = outcomes or {}

    def create_receiver(self, container, conn, source, name):
        return container.create_receiver(conn, source=source, name=name,
                                         options=Link3Ack())

    def create_sender(self, container, conn, target, name):
        return container.create_sender(conn, target=target, name=name,
                                       options=Link3Ack())

    def do_send(self, sender):
        for i in range(self.msg_count):
            msg = Message(body=" %s -> %s:%s" % (sender.name, i, self.body))
            dlv = sender.send(msg)
            self.n_sent += 1

    def on_message(self, event):
        # receiver: send outcome do not settle
        super(MulticastUnsettled3Ack, self).on_message(event)
        if event.delivery.settled:
            self.error = "Unexpected pre-settled message received!"
            self.done()
            return
        r_name = event.receiver.name
        outcome = self.outcomes.get(r_name, Delivery.ACCEPTED)
        event.delivery.update(outcome)
        if event.receiver.credit == 0:
            event.receiver.flow(1)

    def on_settled(self, event):
        super(MulticastUnsettled3Ack, self).on_settled(event)
        event.delivery.settle()
        self.check_if_done()

    def on_accepted(self, event):
        super(MulticastUnsettled3Ack, self).on_accepted(event)
        event.delivery.settle()
        self.check_if_done()

    def on_released(self, event):
        super(MulticastUnsettled3Ack, self).on_released(event)
        event.delivery.settle()
        self.check_if_done()

    def on_modified(self, event):
        super(MulticastUnsettled3Ack, self).on_modified(event)
        event.delivery.settle()
        self.check_if_done()

    def on_rejected(self, event):
        super(MulticastUnsettled3Ack, self).on_rejected(event)
        event.delivery.settle()
        self.check_if_done()

    def check_if_done(self):
        to_send = self.msg_count * self.n_senders
        to_rcv = to_send * self.n_receivers

        n_outcomes = (self.n_accepted + self.n_rejected
                      + self.n_modified + self.n_released)

        # expect senders to see settlement
        if (self.n_sent == to_send
                and self.n_received == to_rcv
                and n_outcomes == to_send
                and self.n_settled == to_rcv):
            self.done()


class MulticastUnsettled1Ack(MulticastUnsettled3Ack):
    """Sender sends unsettled, the receiver sets outcome and immediately settles"""

    def __init__(self, config, count, body, outcomes=None):
        super(MulticastUnsettled1Ack, self).__init__(config,
                                                     count,
                                                     outcomes)

    def create_receiver(self, container, conn, source, name):
        return container.create_receiver(conn, source=source, name=name,
                                         options=Link1Ack())

    def create_sender(self, container, conn, target, name):
        return container.create_sender(conn, target=target, name=name,
                                       options=Link1Ack())

    def on_message(self, event):
        # receiver: send outcome and settle
        super(MulticastUnsettled1Ack, self).on_message(event)
        event.delivery.settle()

    def check_if_done(self):
        to_send = self.msg_count * self.n_senders
        to_rcv = to_send * self.n_receivers

        n_outcomes = (self.n_accepted + self.n_rejected
                      + self.n_modified + self.n_released)

        # expect sender to see settlement
        if (self.n_received == to_rcv
                and n_outcomes == to_send
                and self.n_settled == to_send):
            self.done()


class MulticastUnsettledRxFail(MulticastUnsettled3Ack):
    """Spontaneously close a receiver or connection on message received"""

    def __init__(self, config, count, drop_clients, detach, body):
        super(MulticastUnsettledRxFail, self).__init__(config, count, body)
        self.drop_clients = drop_clients
        self.detach = detach

    def check_if_done(self):
        # Verify each receiver got the expected number of messages.
        # Avoid waiting for dropped receivers.
        done = True
        to_rcv = self.n_senders * self.msg_count
        for name, count in self.c_received.items():
            if name not in self.drop_clients:
                if count != to_rcv:
                    done = False
        if done:
            self.done()

    def on_message(self, event):
        # close the receiver on arrival of the first message
        r_name = event.receiver.name
        if r_name in self.drop_clients:
            if self.detach:
                if event.receiver.state & Link.LOCAL_ACTIVE:
                    event.receiver.close()
            elif event.connection.state & Connection.LOCAL_ACTIVE:
                event.connection.close()
        super(MulticastUnsettledRxFail, self).on_message(event)


class MulticastUnsettled3AckMA(MulticastUnsettled3Ack):
    """Try 3 Ack, but with a bunch of user Message Annotations (why not?)"""

    def __init__(self, config, count, body, outcomes=None):
        super(MulticastUnsettled3AckMA, self).__init__(config,
                                                       count,
                                                       body,
                                                       outcomes=None)
        self._huge_ma = {
            "my-key": "my-data",
            "my-other-key": "my-other-data",
            "my-map": {"my-map-key1": "X",
                       "my-map-key2": 0x12,
                       "my-map-key3": "+0123456789" * 101,
                       "my-map-list": list(range(97))
                       },
            "my-last-key": "so long, folks!"
        }

    def do_send(self, sender):
        for i in range(self.msg_count):
            msg = Message(body=" %s -> %s:%s" % (sender.name, i, self.body))
            msg.annotations = self._huge_ma
            dlv = sender.send(msg)
            self.n_sent += 1

    def on_message(self, event):
        msg = event.message
        if event.message.annotations != self._huge_ma:
            self.error = "forwarded message annotations mismatch original"
            self.done()
            return
        super(MulticastUnsettled3AckMA, self).on_message(event)


class MulticastCreditBlocked(MessagingHandler):
    """
    Ensure that credit is not provided when there are no consumers present.
    This client connects to 'address' and creates a sender to 'target'.  Once
    the sending link has opened a short timer is started.  It is expected that
    on_sendable() is NOT invoked before the timer expires.
    """

    def __init__(self, address, target=None, timeout=2, **handler_kwargs):
        super(MulticastCreditBlocked, self).__init__(**handler_kwargs)
        self.target = target
        self.address = address
        self.time_out = timeout

        self.conn = None
        self.sender = None
        self.timer = None
        self.error = "Timeout NOT triggered as expected!"

    def done(self):
        # stop the reactor and clean up the test
        if self.timer:
            self.timer.cancel()
        if self.conn:
            self.conn.close()

    def timeout(self):
        self.error = None
        self.done()

    def on_start(self, event):
        self.conn = event.container.connect(self.address)
        self.sender = event.container.create_sender(self.conn,
                                                    target=self.target,
                                                    name="McastBlocked")

    def on_link_opened(self, event):
        self.timer = event.reactor.schedule(self.time_out, TestTimeout(self))

    def on_sendable(self, event):
        self.error = "Unexpected call to on_sendable()!"
        self.done()

    def run(self):
        Container(self).run()


if __name__ == '__main__':
    unittest.main(main_module())

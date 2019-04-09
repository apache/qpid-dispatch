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

from __future__ import unicode_literals
from __future__ import division
from __future__ import absolute_import
from __future__ import print_function

from time import sleep
import unittest2 as unittest

from proton.handlers import MessagingHandler
from proton.reactor import Container
from proton.reactor import LinkOption
from proton import Connection
from proton import Link
from proton import Message
from proton import Delivery
from qpid_dispatch.management.client import Node
from system_test import TestCase
from system_test import Qdrouterd
from system_test import main_module
from system_test import TIMEOUT


class TestTimeout(object):
    def __init__(self, parent):
        self.parent = parent

    def on_timer_task(self, event):
        self.parent.timeout()


MAX_FRAME=1025
W_THREADS=2

# check for leaks of the following entities
ALLOC_STATS=["qd_message_t",
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
                              'maxFrameSize': MAX_FRAME}),
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
             'receivers':   [],
             'subscribers': 1,
             'remotes':     0
            },
            # Interior router INT_A:
            {'router':      cls.INT_A,
             'senders':     [],
             # 'receivers':   ['R-INT_A-1'],
             'receivers':   [],
             'subscribers': 0,
             'remotes':     1,
            },
            # Interior router INT_B:
            {'router':      cls.INT_B,
             'senders':     [],
             'receivers':   [],
             'subscribers': 1,
             'remotes':     0,
            },
            # edge router EB1
            {'router':      cls.EB1,
             'senders':     [],
             'receivers':   ['R-EB1-1'],
             'subscribers': 1,
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
            d[name] = list(filter(lambda a: a['typeName'] == name, q))[0]
        return d

    def test_51_maybe_presettled_large_msg(self):
        body = " MCAST MAYBE PRESETTLED LARGE "
        body += "X" * (MAX_FRAME * 19)
        for repeat in range(5):
            test = MulticastPresettled(self.config, 100, body, SendMaybePresettled())
            test.run()
            self.assertEqual(None, test.error)

    def test_51_presettled_large_msg(self):
        body = " MCAST PRESETTLED LARGE "
        body += "X" * (MAX_FRAME * 23)
        for repeat in range(5):
            test = MulticastPresettled(self.config, 100, body, SendMustBePresettled())
            test.run()
            self.assertEqual(None, test.error)

    def _check_for_leaks(self):
        for r in self.routers:
            stats = self._get_alloc_stats(r, ALLOC_STATS)
            for name in ALLOC_STATS:
                # ensure threads haven't leaked
                max_allowed  = ((W_THREADS + 1)
                                * stats[name]['localFreeListMax'])
                held = stats[name]['heldByThreads']
                import sys; sys.stdout.flush()
                if held >= (2 * max_allowed):
                    print("OOPS!!! %s: (%s) - held=%d max=%d\n   %s\n"
                          % (r.config.router_id,
                             name, held, max_allowed, stats))
                    import sys; sys.stdout.flush()
                    self.assertFalse(held >= (2 * max_allowed))

    def test_999_check_for_leaks(self):
        self._check_for_leaks()


class SendMaybePresettled(LinkOption):
    """
    Set the default send settlement modes on link negotiation to mixed
    """
    def apply(self, link):
        link.snd_settle_mode = Link.SND_MIXED
        link.rcv_settle_mode = Link.RCV_FIRST


class SendMustBePresettled(LinkOption):
    """
    Set the default send settlement modes on a link to presettled
    """
    def apply(self, link):
        link.snd_settle_mode = Link.SND_SETTLED
        link.rcv_settle_mode = Link.RCV_FIRST


class MulticastBase(MessagingHandler):
    def __init__(self, config, count, body, topic=None, **handler_kwargs):
        super(MulticastBase, self).__init__(**handler_kwargs)
        self.msg_count = count
        self.config = config
        self.topic = topic or "whatevahcast/test"
        self.body = body

        # totals
        self.n_senders = 0;
        self.n_receivers = 0;
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

        # self.c_accepted = {}
        # self.c_released = {}
        # self.c_rejected = {}
        # self.c_modified = {}
        # self.c_settled  = {}

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

    def create_receiver(self, container, conn, source, name):
        # must override in subclass
        assert(False)

    def create_sender(self, container, conn, target, name):
        # must override in subclass
        assert(False)

    def on_start(self, event):
        self.reactor = event.reactor
        self.timers.append(self.reactor.schedule(TIMEOUT, TestTimeout(self)))
        # first create all the receivers first
        for cfg in self.config:
            for name in cfg['receivers']:
                conn = event.container.connect(cfg['router'].listener)
                assert(name not in self.r_conns)
                self.r_conns[name] = conn
                self.create_receiver(event.container, conn, self.topic, name)
                self.n_receivers += 1

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
                        assert(name not in self.s_conns)
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
        name = event.link.name

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
                if list(filter(lambda a: a['name'].find(self.topic) != -1, addrs)):
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
        # and for all received messagest to be settled by the
        # sender
        to_rcv = self.n_senders * self.msg_count
        if to_rcv == self.n_received and not self.unsettled_deliveries:
            self.done()

    def on_message(self, event):
        super(MulticastPresettled, self).on_message(event)
        if event.receiver:
            if not event.delivery.settled:
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
                self.unsettled_deliveries[tag] -= 1
                if self.unsettled_deliveries[tag] == 0:
                    del self.unsettled_deliveries[tag]
            except KeyError:
                pass
            self.check_if_done()


if __name__ == '__main__':
    unittest.main(main_module())

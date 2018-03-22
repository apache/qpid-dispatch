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

import unittest2 as unittest
import logging
from proton import Message, PENDING, ACCEPTED, REJECTED, Timeout
from system_test import TestCase, Qdrouterd, main_module, TIMEOUT
from proton.handlers import MessagingHandler
from proton.reactor import Container
from qpid_dispatch.management.client import Node

# PROTON-828:
try:
    from proton import MODIFIED
except ImportError:
    from proton import PN_STATUS_MODIFIED as MODIFIED


class TwoRouterTest(TestCase):

    inter_router_port = None

    @classmethod
    def setUpClass(cls):
        """Start a router and a messenger"""
        super(TwoRouterTest, cls).setUpClass()

        def router(name, client_server, connection):

            config = [
                ('router', {'mode': 'interior', 'id': 'QDR.%s'%name, 'allowUnsettledMulticast': 'yes'}),

                ('listener', {'port': cls.tester.get_port(), 'stripAnnotations': 'no'}),

                # The following listeners were exclusively added to test the stripAnnotations attribute in qdrouterd.conf file
                # Different listeners will be used to test all allowed values of stripAnnotations ('no', 'both', 'out', 'in')
                ('listener', {'port': cls.tester.get_port(), 'stripAnnotations': 'no'}),
                ('listener', {'port': cls.tester.get_port(), 'stripAnnotations': 'both'}),
                ('listener', {'port': cls.tester.get_port(), 'stripAnnotations': 'out'}),
                ('listener', {'port': cls.tester.get_port(), 'stripAnnotations': 'in'}),

                ('address', {'prefix': 'closest', 'distribution': 'closest'}),
                ('address', {'prefix': 'spread', 'distribution': 'balanced'}),
                ('address', {'prefix': 'multicast', 'distribution': 'multicast'}),

                # for testing pattern matching
                ('address', {'pattern': 'a.b.c.d',
                             'distribution': 'closest'}),
                ('address', {'pattern': '#.b.c.d',
                             'distribution': 'multicast'}),
                ('address', {'pattern': 'a/*/#/d',
                             'distribution': 'closest'}),
                ('address', {'pattern': '*/b/c/d',
                             'distribution': 'multicast'}),
                ('address', {'pattern': 'a.x.d',
                             'distribution': 'closest'}),
                ('address', {'pattern': 'a.*.d',
                             'distribution': 'multicast'}),
                connection
            ]

            config = Qdrouterd.Config(config)

            cls.routers.append(cls.tester.qdrouterd(name, config, wait=True))

        cls.routers = []

        inter_router_port = cls.tester.get_port()

        router('A', 'server',
               ('listener', {'role': 'inter-router', 'port': inter_router_port}))

        router('B', 'client',
               ('connector', {'name': 'connectorToA', 'role': 'inter-router', 'port': inter_router_port,
                              'verifyHostName': 'no'}))

        cls.routers[0].wait_router_connected('QDR.B')
        cls.routers[1].wait_router_connected('QDR.A')

    def test_01_pre_settled(self):
        test = DeliveriesInTransit(self.routers[0].addresses[0], self.routers[1].addresses[0])
        test.run()
        self.assertEqual(None, test.error)

        local_node = Node.connect(self.routers[0].addresses[0], timeout=TIMEOUT)
        outs = local_node.query(type='org.apache.qpid.dispatch.routerStats')

        # deliveriesTransit must most surely be greater than num_msgs
        pos = outs.attribute_names.index("deliveriesTransit")
        results = outs.results[0]
        self.assertTrue(results[pos] > 104)

    def test_02a_multicast_unsettled(self):
        test = MulticastUnsettled(self.routers[0].addresses[0])
        test.run()
        self.assertEqual(None, test.error)

    def test_02c_sender_settles_first(self):
        test = SenderSettlesFirst(self.routers[0].addresses[0], self.routers[1].addresses[0])
        test.run()
        self.assertEqual(None, test.error)

    def test_03_message_annotations(self):
        test = MessageAnnotationsTest(self.routers[0].addresses[0], self.routers[1].addresses[0])
        test.run()
        self.assertEqual(None, test.error)

    def test_03a_test_strip_message_annotations_no(self):
        test = MessageAnnotationsStripTest(self.routers[0].addresses[1], self.routers[1].addresses[1])
        test.run()
        self.assertEqual(None, test.error)

    def test_03a_test_strip_message_annotations_no_add_trace(self):
        test = MessageAnnotationsStripAddTraceTest(self.routers[0].addresses[1], self.routers[1].addresses[1])
        test.run()
        self.assertEqual(None, test.error)

    def test_03a_test_strip_message_annotations_both_add_ingress_trace(self):
        test = MessageAnnotationsStripBothAddIngressTrace(self.routers[0].addresses[2], self.routers[1].addresses[2])
        test.run()
        self.assertEqual(None, test.error)

    def test_03a_test_strip_message_annotations_out(self):
        test = MessageAnnotationsStripMessageAnnotationsOut(self.routers[0].addresses[3], self.routers[1].addresses[3])
        test.run()
        self.assertEqual(None, test.error)

    def test_03a_test_strip_message_annotations_in(self):
        test = MessageAnnotationSstripMessageAnnotationsInn(self.routers[0].addresses[4], self.routers[1].addresses[4])
        test.run()
        self.assertEqual(None, test.error)

    def test_04_management(self):
        M = self.messenger()
        M.start()
        M.route("amqp:/*", self.routers[0].addresses[0]+"/$1")
        sub = M.subscribe("amqp:/#")
        reply = sub.address

        request  = Message()
        response = Message()

        request.address    = "amqp:/_local/$management"
        request.reply_to   = reply
        request.properties = {u'type':u'org.amqp.management', u'name':u'self', u'operation':u'GET-MGMT-NODES'}

        M.put(request)
        M.send()
        M.recv()
        M.get(response)

        assert response.properties['statusCode'] == 200, response.properties['statusDescription']
        self.assertIn('amqp:/_topo/0/QDR.B/$management', response.body)

        request.address    = "amqp:/_topo/0/QDR.B/$management"
        request.reply_to   = reply
        request.properties = {u'type':u'org.amqp.management', u'name':u'self', u'operation':u'GET-MGMT-NODES'}

        M.put(request)
        M.send()
        M.recv()
        M.get(response)

        self.assertEqual(response.properties['statusCode'], 200)
        self.assertTrue('amqp:/_topo/0/QDR.A/$management' in response.body)

        M.stop()

    def test_05_semantics_multicast(self):
        addr = "amqp:/multicast.1"
        M1 = self.messenger()
        M2 = self.messenger()
        M3 = self.messenger()
        M4 = self.messenger()

        M1.route("amqp:/*", self.routers[0].addresses[0]+"/$1")
        M2.route("amqp:/*", self.routers[1].addresses[0]+"/$1")
        M3.route("amqp:/*", self.routers[0].addresses[0]+"/$1")
        M4.route("amqp:/*", self.routers[1].addresses[0]+"/$1")

        M1.start()
        M2.start()
        M3.start()
        M4.start()

        M2.subscribe(addr)
        M3.subscribe(addr)
        M4.subscribe(addr)
        self.routers[0].wait_address("multicast.1", 1, 1)

        tm = Message()
        rm = Message()

        tm.address = addr
        for i in range(100):
            tm.body = {'number': i}
            M1.put(tm)
        M1.send()

        for i in range(100):
            try:
                M2.recv(1)
                M2.get(rm)
                self.assertEqual(i, rm.body['number'])
            except:
                print "M2 at", i

            try:
                M3.recv(1)
                M3.get(rm)
                self.assertEqual(i, rm.body['number'])
            except:
                print "M3 at", i

            try:
                M4.recv(1)
                M4.get(rm)
                self.assertEqual(i, rm.body['number'])
            except:
                print "M4 at", i

        M1.stop()
        M2.stop()
        M3.stop()
        M4.stop()

    def test_06_semantics_closest_is_local(self):
        addr = "amqp:/closest.1"
        M1 = self.messenger()
        M2 = self.messenger()
        M3 = self.messenger()
        M4 = self.messenger()

        M2.timeout = 0.1
        M3.timeout = 0.1
        M4.timeout = 0.1

        M1.route("amqp:/*", self.routers[0].addresses[0]+"/$1")
        M2.route("amqp:/*", self.routers[1].addresses[0]+"/$1")
        M3.route("amqp:/*", self.routers[0].addresses[0]+"/$1")
        M4.route("amqp:/*", self.routers[1].addresses[0]+"/$1")

        M1.start()
        M2.start()
        M3.start()
        M4.start()

        M2.subscribe(addr)
        self.routers[0].wait_address("closest.1", 0, 1)
        M3.subscribe(addr)
        M4.subscribe(addr)
        self.routers[0].wait_address("closest.1", 1, 1)

        tm = Message()
        rm = Message()

        tm.address = addr
        for i in range(30):
            tm.body = {'number': i}
            M1.put(tm)
        M1.send()

        i = 0
        rx_set = []
        for i in range(30):
            M3.recv(1)
            M3.get(rm)
            rx_set.append(rm.body['number'])

        try:
            M2.recv(1)
            self.assertEqual(0, "Unexpected messages arrived on M2")
        except AssertionError:
            raise
        except Exception:
            pass

        try:
            M4.recv(1)
            self.assertEqual(0, "Unexpected messages arrived on M4")
        except AssertionError:
            raise
        except Exception:
            pass

        self.assertEqual(30, len(rx_set))
        rx_set.sort()
        for i in range(30):
            self.assertEqual(i, rx_set[i])

        M1.stop()
        M2.stop()
        M3.stop()
        M4.stop()

    def test_07_semantics_closest_is_remote(self):
        addr = "amqp:/closest.2"
        M1 = self.messenger()
        M2 = self.messenger()
        M3 = self.messenger()
        M4 = self.messenger()

        M1.route("amqp:/*", self.routers[0].addresses[0]+"/$1")
        M2.route("amqp:/*", self.routers[1].addresses[0]+"/$1")
        M3.route("amqp:/*", self.routers[0].addresses[0]+"/$1")
        M4.route("amqp:/*", self.routers[1].addresses[0]+"/$1")

        M1.start()
        M2.start()
        M3.start()
        M4.start()

        M2.subscribe(addr)
        M4.subscribe(addr)
        self.routers[0].wait_address("closest.2", 0, 1)

        tm = Message()
        rm = Message()

        tm.address = addr
        for i in range(30):
            tm.body = {'number': i}
            M1.put(tm)
        M1.send()

        i = 0
        rx_set = []
        for i in range(15):
            M2.recv(1)
            M2.get(rm)
            rx_set.append(rm.body['number'])

            M4.recv(1)
            M4.get(rm)
            rx_set.append(rm.body['number'])

        self.assertEqual(30, len(rx_set))
        rx_set.sort()
        for i in range(30):
            self.assertEqual(i, rx_set[i])

        M1.stop()
        M2.stop()
        M3.stop()
        M4.stop()

    def test_08_semantics_spread(self):
        addr = "amqp:/spread.1"
        M1 = self.messenger()
        M2 = self.messenger()
        M3 = self.messenger()
        M4 = self.messenger()

        M2.timeout = 0.1
        M3.timeout = 0.1
        M4.timeout = 0.1

        M1.route("amqp:/*", self.routers[0].addresses[0]+"/$1")
        M2.route("amqp:/*", self.routers[1].addresses[0]+"/$1")
        M3.route("amqp:/*", self.routers[0].addresses[0]+"/$1")
        M4.route("amqp:/*", self.routers[1].addresses[0]+"/$1")

        M1.start()
        M2.start()
        M3.start()
        M4.start()
        M2.subscribe(addr)
        M3.subscribe(addr)
        M4.subscribe(addr)
        self.routers[0].wait_address("spread.1", 1, 1)

        tm = Message()
        rm = Message()

        tm.address = addr
        for i in range(50):
            tm.body = {'number': i}
            M1.put(tm)
        M1.send()

        i = 0
        rx_set = []
        ca = 0
        cb = 0
        cc = 0

        while len(rx_set) < 50:
            try:
                M2.recv(1)
                M2.get(rm)
                rx_set.append(rm.body['number'])
                ca += 1
            except:
                pass

            try:
                M3.recv(1)
                M3.get(rm)
                rx_set.append(rm.body['number'])
                cb += 1
            except:
                pass

            try:
                M4.recv(1)
                M4.get(rm)
                rx_set.append(rm.body['number'])
                cc += 1
            except:
                pass

        self.assertEqual(50, len(rx_set))
        rx_set.sort()
        for i in range(50):
            self.assertEqual(i, rx_set[i])
        self.assertTrue(ca > 0)
        self.assertTrue(cb > 0)
        self.assertTrue(cc > 0)

        M1.stop()
        M2.stop()
        M3.stop()
        M4.stop()

    def test_9_to_override(self):
        addr = "amqp:/toov/1"
        M1 = self.messenger()
        M2 = self.messenger()

        M1.route("amqp:/*", self.routers[0].addresses[0]+"/$1")
        M2.route("amqp:/*", self.routers[1].addresses[0]+"/$1")

        M1.start()
        M2.start()
        M2.subscribe(addr)
        self.routers[0].wait_address("toov/1", 0, 1)

        tm = Message()
        rm = Message()

        tm.address = addr

        ##
        ## Pre-existing TO
        ##
        tm.annotations = {'x-opt-qd.to': 'toov/1'}
        for i in range(10):
            tm.body = {'number': i}
            M1.put(tm)
        M1.send()

        for i in range(10):
            M2.recv(1)
            M2.get(rm)
            self.assertEqual(i, rm.body['number'])
            ma = rm.annotations
            self.assertEqual(ma.__class__, dict)
            self.assertEqual(ma['x-opt-qd.to'], 'toov/1')

        M1.stop()
        M2.stop()

    def test_10_propagated_disposition(self):
        addr = "amqp:/unsettled/2"
        M1 = self.messenger()
        M2 = self.messenger()

        M1.route("amqp:/*", self.routers[0].addresses[0]+"/$1")
        M2.route("amqp:/*", self.routers[1].addresses[0]+"/$1")

        M1.outgoing_window = 5
        M2.incoming_window = 5

        M1.start()
        M2.start()
        M2.subscribe(addr)
        self.routers[0].wait_address("unsettled/2", 0, 1)

        tm = Message()
        rm = Message()

        tm.address = addr
        tm.body = {'number': 0}

        ##
        ## Test ACCEPT
        ##
        tx_tracker = M1.put(tm)
        M1.send(0)
        M2.recv(1)
        rx_tracker = M2.get(rm)
        self.assertEqual(0, rm.body['number'])
        self.assertEqual(PENDING, M1.status(tx_tracker))

        M2.accept(rx_tracker)
        M2.settle(rx_tracker)

        M2.flush()
        M1.flush()

        self.assertEqual(ACCEPTED, M1.status(tx_tracker))

        ##
        ## Test REJECT
        ##
        tx_tracker = M1.put(tm)
        M1.send(0)
        M2.recv(1)
        rx_tracker = M2.get(rm)
        self.assertEqual(0, rm.body['number'])
        self.assertEqual(PENDING, M1.status(tx_tracker))

        M2.reject(rx_tracker)
        M2.settle(rx_tracker)

        M2.flush()
        M1.flush()

        self.assertEqual(REJECTED, M1.status(tx_tracker))

        M1.stop()
        M2.stop()

    def test_11_three_ack(self):
        addr = "amqp:/three_ack/1"
        M1 = self.messenger()
        M2 = self.messenger()

        M1.route("amqp:/*", self.routers[0].addresses[0]+"/$1")
        M2.route("amqp:/*", self.routers[1].addresses[0]+"/$1")

        M1.outgoing_window = 5
        M2.incoming_window = 5

        M1.start()
        M2.start()
        M2.subscribe(addr)
        self.routers[0].wait_address("three_ack/1", 0, 1)

        tm = Message()
        rm = Message()

        tm.address = addr
        tm.body = {'number': 200}

        tx_tracker = M1.put(tm)
        M1.send(0)
        M2.recv(1)
        rx_tracker = M2.get(rm)
        self.assertEqual(200, rm.body['number'])
        self.assertEqual(PENDING, M1.status(tx_tracker))

        M2.accept(rx_tracker)

        M2.flush()
        M1.flush()

        self.assertEqual(ACCEPTED, M1.status(tx_tracker))

        M1.settle(tx_tracker)

        M1.flush()
        M2.flush()

        ##
        ## We need a way to verify on M2 (receiver) that the tracker has been
        ## settled on the M1 (sender).  [ See PROTON-395 ]
        ##

        M2.settle(rx_tracker)

        M2.flush()
        M1.flush()

        M1.stop()
        M2.stop()

    def test_12_excess_deliveries_released(self):
        """
        Message-route a series of deliveries where the receiver provides credit for a subset and
        once received, closes the link.  The remaining deliveries should be released back to the sender.
        """
        test = ExcessDeliveriesReleasedTest(self.routers[0].addresses[0], self.routers[1].addresses[0])
        test.run()
        self.assertEqual(None, test.error)

    def test_15_attach_on_inter_router(self):
        test = AttachOnInterRouterTest(self.routers[0].addresses[5])
        test.run()
        self.assertEqual(None, test.error)

    def test_16_delivery_annotations(self):
        addr = "amqp:/delivery_annotations.1"
        M1 = self.messenger()
        M2 = self.messenger()

        M1.route("amqp:/*", self.routers[0].addresses[0]+"/$1")
        M2.route("amqp:/*", self.routers[1].addresses[0]+"/$1")
        M1.start()
        M2.start()
        M2.subscribe(addr)

        tm = Message()
        rm = Message()

        self.routers[0].wait_address("delivery_annotations.1", 0, 1)

        tm.annotations = {'a1': 'a1', 'b1': 'b2'}
        tm.address = addr
        tm.instructions = {'work': 'hard', 'stay': 'humble'}
        tm.body = {'number': 38}
        M1.put(tm)
        M1.send()

        M2.recv(1)
        M2.get(rm)
        self.assertEqual(38, rm.body['number'])

        M1.stop()
        M2.stop()

    def test_17_address_wildcard(self):
        # verify proper distribution is selected by wildcard
        addresses = [
            # (address, count of messages expected to be received)
            ('a.b.c.d',   1), # closest 'a.b.c.d'
            ('b.c.d',     2), # multi   '#.b.c.d'
            ('f.a.b.c.d', 2), # multi   '#.b.c.d
            ('a.c.d',     2), # multi   'a.*.d'
            ('a/c/c/d',   1), # closest 'a/*/#.d
            ('a/x/z/z/d', 1), # closest 'a/*/#.d
            ('a/x/d',     1), # closest 'a.x.d'
            ('a.x.e',     1), # balanced  ----
            ('m.b.c.d',   2)  # multi   '*/b/c/d'
        ]

        # two receivers per address - one for each router
        receivers = []
        for a in addresses:
            for x in range(2):
                M = self.messenger(timeout=0.1)
                M.route("amqp:/*", self.routers[x].addresses[0]+"/$1")
                M.start()
                M.subscribe('amqp:/' + a[0])
                receivers.append(M)
            self.routers[0].wait_address(a[0], 1, 1)
            self.routers[1].wait_address(a[0], 1, 1)

        # single sender sends one message to each address
        M1 = self.messenger()
        M1.route("amqp:/*", self.routers[0].addresses[0]+"/$1")
        M1.start()
        for a in addresses:
            tm = Message()
            tm.address = 'amqp:/' + a[0]
            tm.body = {'address': a[0]}
            M1.put(tm)
            M1.send()

        # gather all received messages
        msgs_recvd = {}
        rm = Message()
        for M in receivers:
            try:
                while True:
                    M.recv(1)
                    M.get(rm)
                    index = rm.body.get('address', "ERROR")
                    if index not in msgs_recvd:
                        msgs_recvd[index] = 0
                    msgs_recvd[index] += 1
            except Exception as exc:
                self.assertTrue("None" in str(exc))

        # verify expected count == actual count
        self.assertTrue("ERROR" not in msgs_recvd)
        for a in addresses:
            self.assertTrue(a[0] in msgs_recvd)
            self.assertEqual(a[1], msgs_recvd[a[0]])

        M1.stop()
        for M in receivers:
            M.stop()

    def test_17_large_streaming_test(self):
        test = LargeMessageStreamTest(self.routers[0].addresses[0], self.routers[1].addresses[0])
        test.run()
        self.assertEqual(None, test.error)


class Timeout(object):
    def __init__(self, parent):
        self.parent = parent

    def on_timer_task(self, event):
        self.parent.timeout()

class LargeMessageStreamTest(MessagingHandler):
    def __init__(self, address1, address2):
        super(LargeMessageStreamTest, self).__init__()
        self.address1 = address1
        self.address2 = address2
        self.dest = "LargeMessageStreamTest"
        self.error = None
        self.conn1 = None
        self.conn2 = None
        self.count = 10
        self.n_sent = 0
        self.timer = None
        self.sender = None
        self.receiver = None
        self.n_received = 0
        self.body = ""
        for i in range(10000):
            self.body += "0123456789101112131415"

    def check_if_done(self):
        if self.n_received == self.count:
            self.timer.cancel()
            self.conn1.close()
            self.conn2.close()

    def timeout(self):
        self.error = "Timeout Expired: sent=%d, received=%d" % (self.n_sent, self.n_received)
        self.conn1.close()
        self.conn2.close()

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.conn1 = event.container.connect(self.address1)
        self.conn2 = event.container.connect(self.address2)
        self.sender = event.container.create_sender(self.conn1, self.dest)
        self.receiver = event.container.create_receiver(self.conn2, self.dest)

    def on_sendable(self, event):
        if self.n_sent < self.count:
            msg = Message(body=self.body)
            # send(msg) calls the stream function which streams data from sender to the router
            event.sender.send(msg)
            self.n_sent += 1

    def on_message(self, event):
        self.n_received += 1
        self.check_if_done()

    def run(self):
        Container(self).run()


class ExcessDeliveriesReleasedTest(MessagingHandler):
    def __init__(self, address1, address2):
        super(ExcessDeliveriesReleasedTest, self).__init__(prefetch=0)
        self.address1 = address1
        self.address2 = address2
        self.dest = "closest.EDRtest"
        self.error = None
        self.sender = None
        self.receiver = None
        self.n_sent  = 0
        self.n_received = 0
        self.n_accepted = 0
        self.n_released = 0
        self.timer = None
        self.conn1 = None
        self.conn2 = None

    def timeout(self):
        self.error = "Timeout Expired"
        self.conn1.close()
        self.conn2.close()

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.conn1 = event.container.connect(self.address1)
        self.conn2 = event.container.connect(self.address2)
        self.sender   = event.container.create_sender(self.conn1, self.dest)
        self.receiver = event.container.create_receiver(self.conn2, self.dest)
        self.receiver.flow(6)

    def on_sendable(self, event):
        for i in range(10 - self.n_sent):
            msg = Message(body=i)
            event.sender.send(msg)
            self.n_sent += 1

    def on_accepted(self, event):
        self.n_accepted += 1

    def on_released(self, event):
        self.n_released += 1
        if self.n_released == 4:
            if self.n_accepted != 6:
                self.error = "Expected 6 accepted, got %d" % self.n_accepted
            if self.n_received != 6:
                self.error = "Expected 6 received, got %d" % self.n_received
            self.conn1.close()
            self.conn2.close()
            self.timer.cancel()

    def on_message(self, event):
        self.n_received += 1
        if self.n_received == 6:
            self.receiver.close()

    def run(self):
        Container(self).run()


class AttachOnInterRouterTest(MessagingHandler):
    """Expect an error when attaching a link to an inter-router listener"""
    def __init__(self, address):
        super(AttachOnInterRouterTest, self).__init__(prefetch=0)
        self.address = address
        self.dest = "AOIRtest"
        self.error = None
        self.sender = None

    def timeout(self):
        self.error = "Timeout Expired"
        self.conn.close()

    def on_start(self, event):
        self.timer  = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.conn   = event.container.connect(self.address)
        self.sender = event.container.create_sender(self.conn, self.dest)

    def on_link_remote_close(self, event):
        self.conn.close()
        self.timer.cancel()

    def run(self):
        logging.disable(logging.ERROR) # Hide expected log errors
        try:
            Container(self).run()
        finally:
            logging.disable(logging.NOTSET) # Restore to normal


class DeliveriesInTransit(MessagingHandler):
    def __init__(self, address1, address2):
        super(DeliveriesInTransit, self).__init__()
        self.address1 = address1
        self.address2 = address2
        self.dest = "pre_settled.1"
        self.error = "All messages not received"
        self.n_sent = 0
        self.timer = None
        self.conn1 = None
        self.conn2 = None
        self.sender = None
        self.num_msgs = 104
        self.sent_count = 0
        self.received_count = 0
        self.receiver = None

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.conn1 = event.container.connect(self.address1)
        self.sender = event.container.create_sender(self.conn1, self.dest)
        self.conn2 = event.container.connect(self.address2)
        self.receiver = event.container.create_receiver(self.conn2, self.dest)

    def on_sendable(self, event):
        if self.n_sent <= self.num_msgs-1:
            msg = Message(body="Hello World")
            self.sender.send(msg)
            self.n_sent += 1

    def check_if_done(self):
        if self.n_sent == self.received_count:
            self.error = None
            self.timer.cancel()
            self.conn1.close()
            self.conn2.close()

    def on_message(self, event):
        self.received_count+=1
        self.check_if_done()

    def run(self):
        Container(self).run()


class MessageAnnotationsTest(MessagingHandler):
    def __init__(self, address1, address2):
        super(MessageAnnotationsTest, self).__init__()
        self.address1 = address1
        self.address2 = address2
        self.dest = "ma/1"
        self.error = "Message annotations not found"
        self.timer = None
        self.conn1 = None
        self.conn2 = None
        self.sender = None
        self.receiver = None
        self.sent_count = 0
        self.msg_not_sent = True

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.conn1 = event.container.connect(self.address1)
        self.sender = event.container.create_sender(self.conn1, self.dest)
        self.conn2 = event.container.connect(self.address2)
        self.receiver = event.container.create_receiver(self.conn2, self.dest)

    def on_sendable(self, event):
        if self.msg_not_sent:
            msg = Message(body={'number': 0})
            event.sender.send(msg)
            self.msg_not_sent = False

    def on_message(self, event):
        if 0 == event.message.body['number']:
            ma = event.message.annotations
            if ma['x-opt-qd.ingress'] == '0/QDR.A' and ma['x-opt-qd.trace'] == ['0/QDR.A', '0/QDR.B']:
                self.error = None
        self.accept(event.delivery)
        self.timer.cancel()
        self.conn1.close()
        self.conn2.close()

    def run(self):
        Container(self).run()


class MessageAnnotationsStripTest(MessagingHandler):
    def __init__(self, address1, address2):
        super(MessageAnnotationsStripTest, self).__init__()
        self.address1 = address1
        self.address2 = address2
        self.dest = "message_annotations_strip_no/1"
        self.error = "Message annotations not found"
        self.timer = None
        self.conn1 = None
        self.conn2 = None
        self.sender = None
        self.receiver = None
        self.sent_count = 0
        self.msg_not_sent = True

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.conn1 = event.container.connect(self.address1)
        self.sender = event.container.create_sender(self.conn1, self.dest)
        self.conn2 = event.container.connect(self.address2)
        self.receiver = event.container.create_receiver(self.conn2, self.dest)

    def on_sendable(self, event):
        if self.msg_not_sent:
            msg = Message(body={'number': 0})
            ingress_message_annotations = {'work': 'hard', 'stay': 'humble'}
            msg.annotations = ingress_message_annotations
            event.sender.send(msg)
            self.msg_not_sent = False

    def on_message(self, event):
        if 0 == event.message.body['number']:
            ma = event.message.annotations
            if ma['x-opt-qd.ingress'] == '0/QDR.A' and ma['x-opt-qd.trace'] == ['0/QDR.A', '0/QDR.B'] \
                    and ma['work'] == 'hard' and ma['stay'] == 'humble':
                self.error = None
        self.accept(event.delivery)
        self.timer.cancel()
        self.conn1.close()
        self.conn2.close()

    def run(self):
        Container(self).run()


class MessageAnnotationSstripMessageAnnotationsInn(MessagingHandler):
    def __init__(self, address1, address2):
        super(MessageAnnotationSstripMessageAnnotationsInn, self).__init__()
        self.address1 = address1
        self.address2 = address2
        self.dest = "strip_message_annotations_in/1"
        self.error = "Message annotations not found"
        self.timer = None
        self.conn1 = None
        self.conn2 = None
        self.sender = None
        self.receiver = None
        self.sent_count = 0
        self.msg_not_sent = True

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.conn1 = event.container.connect(self.address1)
        self.sender = event.container.create_sender(self.conn1, self.dest)
        self.conn2 = event.container.connect(self.address2)
        self.receiver = event.container.create_receiver(self.conn2, self.dest)

    def on_sendable(self, event):
        if self.msg_not_sent:
            msg = Message(body={'number': 0})
            #
            # Pre-existing ingress and trace
            #
            ingress_message_annotations = {'x-opt-qd.ingress': 'ingress-router', 'x-opt-qd.trace': ['X/QDR']}
            msg.annotations = ingress_message_annotations
            event.sender.send(msg)
            self.msg_not_sent = False

    def on_message(self, event):
        if 0 == event.message.body['number']:
            if event.message.annotations['x-opt-qd.ingress'] == '0/QDR.A' \
                    and event.message.annotations['x-opt-qd.trace'] == ['0/QDR.A', '0/QDR.B']:
                self.error = None
        self.timer.cancel()
        self.conn1.close()
        self.conn2.close()

    def run(self):
        Container(self).run()


class MessageAnnotationsStripMessageAnnotationsOut(MessagingHandler):
    def __init__(self, address1, address2):
        super(MessageAnnotationsStripMessageAnnotationsOut, self).__init__()
        self.address1 = address1
        self.address2 = address2
        self.dest = "strip_message_annotations_out/1"
        self.error = "Message annotations not found"
        self.timer = None
        self.conn1 = None
        self.conn2 = None
        self.sender = None
        self.receiver = None
        self.sent_count = 0
        self.msg_not_sent = True

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.conn1 = event.container.connect(self.address1)
        self.sender = event.container.create_sender(self.conn1, self.dest)
        self.conn2 = event.container.connect(self.address2)
        self.receiver = event.container.create_receiver(self.conn2, self.dest)

    def on_sendable(self, event):
        if self.msg_not_sent:
            msg = Message(body={'number': 0})
            event.sender.send(msg)
            self.msg_not_sent = False

    def on_message(self, event):
        if 0 == event.message.body['number']:
            if event.message.annotations is None:
                self.error = None
        self.timer.cancel()
        self.conn1.close()
        self.conn2.close()

    def run(self):
        Container(self).run()


class MessageAnnotationsStripBothAddIngressTrace(MessagingHandler):
    def __init__(self, address1, address2):
        super(MessageAnnotationsStripBothAddIngressTrace, self).__init__()
        self.address1 = address1
        self.address2 = address2
        self.dest = "strip_message_annotations_both_add_ingress_trace/1"
        self.error = "Message annotations not found"
        self.timer = None
        self.conn1 = None
        self.conn2 = None
        self.sender = None
        self.receiver = None
        self.sent_count = 0
        self.msg_not_sent = True

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.conn1 = event.container.connect(self.address1)
        self.sender = event.container.create_sender(self.conn1, self.dest)
        self.conn2 = event.container.connect(self.address2)
        self.receiver = event.container.create_receiver(self.conn2, self.dest)

    def on_sendable(self, event):
        if self.msg_not_sent:
            msg = Message(body={'number': 0})
            ingress_message_annotations = {'work': 'hard',
                                           'x-opt-qd': 'humble',
                                           'x-opt-qd.ingress': 'ingress-router',
                                           'x-opt-qd.trace': ['0/QDR.A']}
            msg.annotations = ingress_message_annotations
            event.sender.send(msg)
            self.msg_not_sent = False

    def on_message(self, event):
        if 0 == event.message.body['number']:
            if event.message.annotations == {'work': 'hard', 'x-opt-qd': 'humble'}:
                self.error = None
        self.timer.cancel()
        self.conn1.close()
        self.conn2.close()

    def run(self):
        Container(self).run()


class MessageAnnotationsStripAddTraceTest(MessagingHandler):
    def __init__(self, address1, address2):
        super(MessageAnnotationsStripAddTraceTest, self).__init__()
        self.address1 = address1
        self.address2 = address2
        self.dest = "message_annotations_strip_no/1"
        self.error = "Message annotations not found"
        self.timer = None
        self.conn1 = None
        self.conn2 = None
        self.sender = None
        self.receiver = None
        self.sent_count = 0
        self.msg_not_sent = True

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.conn1 = event.container.connect(self.address1)
        self.sender = event.container.create_sender(self.conn1, self.dest)
        self.conn2 = event.container.connect(self.address2)
        self.receiver = event.container.create_receiver(self.conn2, self.dest)

    def on_sendable(self, event):
        if self.msg_not_sent:
            msg = Message(body={'number': 0})
            ingress_message_annotations = {'x-opt-qd.trace': ['0/QDR.1']}
            msg.annotations = ingress_message_annotations
            event.sender.send(msg)
            self.msg_not_sent = False

    def on_message(self, event):
        if 0 == event.message.body['number']:
            ma = event.message.annotations
            if ma['x-opt-qd.ingress'] == '0/QDR.A' and ma['x-opt-qd.trace'] == ['0/QDR.1', '0/QDR.A', '0/QDR.B']:
                self.error = None
        self.accept(event.delivery)
        self.timer.cancel()
        self.conn1.close()
        self.conn2.close()

    def run(self):
        Container(self).run()



class SenderSettlesFirst(MessagingHandler):
    def __init__(self, address1, address2):
        super(SenderSettlesFirst, self).__init__(auto_accept=False)
        self.address1 = address1
        self.address2 = address2
        self.dest = "closest.senderfirst.1"
        self.error = "Message body received differs from the one sent"
        self.n_sent = 0
        self.timer = None
        self.conn1 = None
        self.conn2 = None
        self.sender = None
        self.sent_count = 0
        self.received_count = 0
        self.receiver = None
        self.msg_not_sent = True

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.conn1 = event.container.connect(self.address1)
        self.sender = event.container.create_sender(self.conn1, self.dest)
        self.conn2 = event.container.connect(self.address2)
        self.receiver = event.container.create_receiver(self.conn2, self.dest)

    def on_sendable(self, event):
        if self.msg_not_sent:
            msg = Message(body={'number': 0})
            dlv = event.sender.send(msg)
            dlv.settle()
            self.msg_not_sent = False

    def on_message(self, event):
        if 0 == event.message.body['number']:
            self.error = None
        self.accept(event.delivery)
        self.timer.cancel()
        self.conn1.close()
        self.conn2.close()

    def run(self):
        Container(self).run()


class MulticastUnsettled(MessagingHandler):
    def __init__(self, address):
        super(MulticastUnsettled, self).__init__()
        self.address = address
        self.dest = "multicast.2"
        self.error = None
        self.n_sent = 0
        self.count = 3
        self.n_received_a = 0
        self.n_received_b = 0
        self.n_received_c = 0
        self.timer = None
        self.conn = None
        self.sender = None
        self.receiver_a = None
        self.receiver_b = None
        self.receiver_c = None

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.conn = event.container.connect(self.address)
        self.sender = event.container.create_sender(self.conn, self.dest)
        self.receiver_a = event.container.create_receiver(self.conn, self.dest, name="A")
        self.receiver_b = event.container.create_receiver(self.conn, self.dest, name="B")
        self.receiver_c = event.container.create_receiver(self.conn, self.dest, name="C")

    def timeout(self):
        self.error = "Timeout Expired: sent=%d rcvd=%d/%d/%d" % \
                     (self.n_sent, self.n_received_a, self.n_received_b, self.n_received_c)
        self.conn.close()

    def check_if_done(self):
        if self.n_received_a + self.n_received_b + self.n_received_c == self.count:
            self.timer.cancel()
            self.conn.close()

    def on_sendable(self, event):
        if self.n_sent == 0:
            msg = Message(body="Appearance-Test")
            self.sender.send(msg)
            self.n_sent += 1

    def on_message(self, event):
        if event.receiver == self.receiver_a:
            self.n_received_a += 1
        if event.receiver == self.receiver_b:
            self.n_received_b += 1
        if event.receiver == self.receiver_c:
            self.n_received_c += 1

    def on_accepted(self, event):
        self.check_if_done()

    def run(self):
        Container(self).run()


if __name__ == '__main__':
    unittest.main(main_module())

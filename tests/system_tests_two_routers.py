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

import unittest, os, json, logging
from subprocess import PIPE, STDOUT
from proton import Message, PENDING, ACCEPTED, REJECTED, RELEASED, Timeout
from system_test import TestCase, Qdrouterd, main_module, DIR, TIMEOUT, Process
from proton.handlers import MessagingHandler
from proton.reactor import Container, AtMostOnce, AtLeastOnce

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
        addr = "amqp:/pre_settled.1"
        M1 = self.messenger()
        M2 = self.messenger()

        M1.route("amqp:/*", self.routers[0].addresses[0]+"/$1")
        M2.route("amqp:/*", self.routers[1].addresses[0]+"/$1")

        M2.subscribe(addr)

        tm = Message()
        rm = Message()

        self.routers[0].wait_address("pre_settled.1", 0, 1)

        tm.address = addr
        for i in range(100):
            tm.body = {'number': i}
            M1.put(tm)
        M1.send()

        for i in range(100):
            M2.recv(1)
            M2.get(rm)
            self.assertEqual(i, rm.body['number'])

        M1.stop()
        M2.stop()

    def test_02a_multicast_unsettled(self):
        addr = "amqp:/multicast.2"
        M1 = self.messenger()
        M2 = self.messenger()
        M3 = self.messenger()
        M4 = self.messenger()

        M1.route("amqp:/*", self.routers[0].addresses[0]+"/$1")
        M2.route("amqp:/*", self.routers[1].addresses[0]+"/$1")
        M3.route("amqp:/*", self.routers[0].addresses[0]+"/$1")
        M4.route("amqp:/*", self.routers[1].addresses[0]+"/$1")

        M1.outgoing_window = 5
        M2.incoming_window = 5
        M3.incoming_window = 5
        M4.incoming_window = 5

        M1.start()
        M2.start()
        M3.start()
        M4.start()

        M2.subscribe(addr)
        M3.subscribe(addr)
        M4.subscribe(addr)
        self.routers[0].wait_address("multicast.2", 1, 1)

        tm = Message()
        rm = Message()

        tm.address = addr
        for i in range(2):
            tm.body = {'number': i}
            M1.put(tm)
        M1.send(0)

        for i in range(2):
            M2.recv(1)
            trk = M2.get(rm)
            M2.accept(trk)
            M2.settle(trk)
            self.assertEqual(i, rm.body['number'])

            M3.recv(1)
            trk = M3.get(rm)
            M3.accept(trk)
            M3.settle(trk)
            self.assertEqual(i, rm.body['number'])

            M4.recv(1)
            trk = M4.get(rm)
            M4.accept(trk)
            M4.settle(trk)
            self.assertEqual(i, rm.body['number'])

        M1.stop()
        M2.stop()
        M3.stop()
        M4.stop()


    def test_02c_sender_settles_first(self):
        addr = "amqp:/closest.senderfirst.1"
        M1 = self.messenger()
        M2 = self.messenger()

        M1.route("amqp:/*", self.routers[0].addresses[0]+"/$1")
        M2.route("amqp:/*", self.routers[1].addresses[0]+"/$1")

        M1.outgoing_window = 5
        M2.incoming_window = 5

        M1.start()
        M2.start()
        M2.subscribe(addr)
        self.routers[0].wait_address("closest.senderfirst.1", 0, 1)

        tm = Message()
        rm = Message()

        tm.address = addr
        tm.body = {'number': 0}
        ttrk = M1.put(tm)
        M1.send(0)

        M1.settle(ttrk)
        M1.flush()
        M2.flush()

        M2.recv(1)
        rtrk = M2.get(rm)
        M2.accept(rtrk)
        M2.settle(rtrk)
        self.assertEqual(0, rm.body['number'])

        M1.flush()
        M2.flush()

        M1.stop()
        M2.stop()


    def test_03_propagated_disposition(self):
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


    def test_04_unsettled_undeliverable(self):
        addr = self.routers[0].addresses[0]+"/unsettled_undeliverable/1"
        M1 = self.messenger()

        M1.outgoing_window = 5

        M1.start()
        M1.timeout = 1
        tm = Message()
        tm.address = addr
        tm.body = {'number': 200}

        exception = False
        try:
            M1.put(tm)
            M1.send(0)
            M1.flush()
        except Exception:
            exception = True

        M1.stop()


    def test_05_three_ack(self):
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


    def notest_06_link_route_sender(self):
        pass

    def notest_07_link_route_receiver(self):
        pass


    def test_08_message_annotations(self):
        addr = "amqp:/ma/1"
        M1 = self.messenger()
        M2 = self.messenger()

        M1.route("amqp:/*", self.routers[0].addresses[0]+"/$1")
        M2.route("amqp:/*", self.routers[1].addresses[0]+"/$1")

        M1.start()
        M2.start()
        M2.subscribe(addr)
        self.routers[0].wait_address("ma/1", 0, 1)

        tm = Message()
        rm = Message()

        tm.address = addr

        ##
        ## No inbound message annotations
        ##
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
            self.assertEqual(ma['x-opt-qd.ingress'], '0/QDR.A')
            self.assertEqual(ma['x-opt-qd.trace'], ['0/QDR.A', '0/QDR.B'])

        M1.stop()
        M2.stop()


    #The stripAnnotations property is set to 'no'
    def test_08a_test_strip_message_annotations_no(self):
        addr = "amqp:/message_annotations_strip_no/1"

        M1 = self.messenger()
        M2 = self.messenger()

        M1.route("amqp:/*", self.routers[0].addresses[1]+"/$1")
        M2.route("amqp:/*", self.routers[1].addresses[1]+"/$1")

        M1.start()
        M2.start()
        M2.subscribe(addr)
        self.routers[0].wait_address("message_annotations_strip_no/1", 0, 1)

        ingress_message = Message()
        ingress_message.address = addr
        ingress_message.body = {'message': 'Hello World!'}
        ingress_message_annotations = {'work': 'hard', 'stay': 'humble'}

        ingress_message.annotations = ingress_message_annotations

        M1.put(ingress_message)
        M1.send()

        # Receive the message
        M2.recv(1)
        egress_message = Message()
        M2.get(egress_message)

        #Make sure 'Hello World!' is in the message body dict
        self.assertEqual('Hello World!', egress_message.body['message'])


        egress_message_annotations = egress_message.annotations

        self.assertEqual(egress_message_annotations.__class__, dict)
        self.assertEqual(egress_message_annotations['x-opt-qd.ingress'], '0/QDR.A')
        self.assertEqual(egress_message_annotations['work'], 'hard')
        self.assertEqual(egress_message_annotations['stay'], 'humble')
        self.assertEqual(egress_message_annotations['x-opt-qd.trace'], ['0/QDR.A', '0/QDR.B'])

        M1.stop()
        M2.stop()

    # This unit test is currently skipped because dispatch router do not pass thru custom message annotations.
    # Once the feature is added the @unittest.skip decorator can be removed.
    # The stripAnnotations property is set to 'no'
    def test_08a_strip_message_annotations_custom(self):
        addr = "amqp:/message_annotations_strip_no_custom/1"

        M1 = self.messenger()
        M2 = self.messenger()

        M1.route("amqp:/*", self.routers[0].addresses[1]+"/$1")
        M2.route("amqp:/*", self.routers[1].addresses[1]+"/$1")

        M1.start()
        M2.start()
        M2.subscribe(addr)
        self.routers[0].wait_address("message_annotations_strip_no_custom/1", 0, 1)

        ingress_message = Message()
        ingress_message.address = addr
        ingress_message.body = {'message': 'Hello World!'}
        ingress_message_annotations = {}
        ingress_message_annotations['custom-annotation'] = '1/Custom_Annotation'

        ingress_message.annotations = ingress_message_annotations

        M1.put(ingress_message)
        M1.send()

        # Receive the message
        M2.recv(1)
        egress_message = Message()
        M2.get(egress_message)

        # Make sure 'Hello World!' is in the message body dict
        self.assertEqual('Hello World!', egress_message.body['message'])

        egress_message_annotations = egress_message.annotations

        self.assertEqual(egress_message_annotations.__class__, dict)
        self.assertEqual(egress_message_annotations['custom-annotation'], '1/Custom_Annotation')
        self.assertEqual(egress_message_annotations['x-opt-qd.ingress'], '0/QDR.A')
        self.assertEqual(egress_message_annotations['x-opt-qd.trace'], ['0/QDR.A', '0/QDR.B'])

        M1.stop()
        M2.stop()

    #The stripAnnotations property is set to 'no'
    def test_08a_test_strip_message_annotations_no_add_trace(self):
        addr = "amqp:/strip_message_annotations_no_add_trace/1"

        M1 = self.messenger()
        M2 = self.messenger()

        M1.route("amqp:/*", self.routers[0].addresses[1]+"/$1")
        M2.route("amqp:/*", self.routers[1].addresses[1]+"/$1")

        M1.start()
        M2.start()
        M2.subscribe(addr)
        self.routers[0].wait_address("strip_message_annotations_no_add_trace/1", 0, 1)

        ingress_message = Message()
        ingress_message.address = addr
        ingress_message.body = {'message': 'Hello World!'}

        ##
        ## Pre-existing ingress and trace
        ##
        #ingress_message_annotations = {'x-opt-qd.ingress': 'ingress-router', 'x-opt-qd.trace': ['0/QDR.1']}
        ingress_message_annotations = {'x-opt-qd.trace': ['0/QDR.1']}
        ingress_message.annotations = ingress_message_annotations

        ingress_message.annotations = ingress_message_annotations

        M1.put(ingress_message)
        M1.send()

        # Receive the message
        M2.recv(1)
        egress_message = Message()
        M2.get(egress_message)

        #Make sure 'Hello World!' is in the message body dict
        self.assertEqual('Hello World!', egress_message.body['message'])


        egress_message_annotations = egress_message.annotations

        self.assertEqual(egress_message_annotations.__class__, dict)
        self.assertEqual(egress_message_annotations['x-opt-qd.ingress'], '0/QDR.A')
        self.assertEqual(egress_message_annotations['x-opt-qd.trace'], ['0/QDR.1', '0/QDR.A', '0/QDR.B'])

        M1.stop()
        M2.stop()

    # Test to see if the dispatch router specific annotations were stripped.
    # The stripAnnotations property is set to 'both'
    # Send a message to the router with pre-existing ingress and trace annotations and make sure that nothing comes out.
    def test_08a_test_strip_message_annotations_both_add_ingress_trace(self):
        addr = "amqp:/strip_message_annotations_both_add_ingress_trace/1"

        M1 = self.messenger()
        M2 = self.messenger()

        M1.route("amqp:/*", self.routers[0].addresses[2]+"/$1")
        M2.route("amqp:/*", self.routers[1].addresses[2]+"/$1")

        M1.start()
        M2.start()
        M2.subscribe(addr)
        self.routers[0].wait_address("strip_message_annotations_both_add_ingress_trace/1", 0, 1)

        ingress_message = Message()
        ingress_message.address = addr
        ingress_message.body = {'message': 'Hello World!'}

        ##
        # Pre-existing ingress and trace. Intentionally populate the trace with the 0/QDR.A which is the trace
        # of the first router. If the inbound annotations were not stripped, the router would drop this message
        # since it would consider this message as being looped.
        #
        ingress_message_annotations = {'work': 'hard',
                                       'x-opt-qd': 'humble',
                                       'x-opt-qd.ingress': 'ingress-router',
                                       'x-opt-qd.trace': ['0/QDR.A']}
        ingress_message.annotations = ingress_message_annotations

        #Put and send the message
        M1.put(ingress_message)
        M1.send()

        # Receive the message
        M2.recv(1)
        egress_message = Message()
        M2.get(egress_message)

        # Router specific annotations (annotations with prefix "x-opt-qd.") will be stripped. User defined annotations will not be stripped.
        self.assertEqual(egress_message.annotations, {'work': 'hard', 'x-opt-qd': 'humble'})

        M1.stop()
        M2.stop()

    # Send in pre-existing trace and ingress and annotations and make sure that there are no outgoing annotations.
    # stripAnnotations property is set to "in"
    def test_08a_test_strip_message_annotations_out(self):
        addr = "amqp:/strip_message_annotations_out/1"

        M1 = self.messenger()
        M2 = self.messenger()

        M1.route("amqp:/*", self.routers[0].addresses[3]+"/$1")
        M2.route("amqp:/*", self.routers[1].addresses[3]+"/$1")

        M1.start()
        M2.start()
        M2.subscribe(addr)
        self.routers[0].wait_address("strip_message_annotations_out/1", 0, 1)

        ingress_message = Message()
        ingress_message.address = addr
        ingress_message.body = {'message': 'Hello World!'}

        #Put and send the message
        M1.put(ingress_message)
        M1.send()

        # Receive the message
        egress_message = Message()
        M2.recv(1)
        M2.get(egress_message)

         #Make sure 'Hello World!' is in the message body dict
        self.assertEqual('Hello World!', egress_message.body['message'])

        egress_message_annotations = egress_message.annotations

        self.assertEqual(egress_message.annotations, None)

        M1.stop()
        M2.stop()

    # Send in pre-existing trace and ingress and annotations and make sure that there are no outgoing annotations.
    # stripAnnotations property is set to "in"
    def test_08a_test_strip_message_annotations_out_custom(self):
        # This test puts three items into message_annotations.
        # Current router code depends on the order of the items in the annotation map and
        # this code can't coerce python and the underlying messenger to send the items in
        # any particular order. That is, the order of the items in the code is not equal
        # to the order of the map items on the wire. Thus the test fails.
        pass
        #addr = "amqp:/strip_message_annotations_out/08a"

        #M1 = self.messenger()
        #M2 = self.messenger()

        #M1.route("amqp:/*", self.routers[0].addresses[3]+"/$1")
        #M2.route("amqp:/*", self.routers[1].addresses[3]+"/$1")

        #M1.start()
        #M2.start()
        #M2.subscribe(addr)
        #self.routers[0].wait_address("strip_message_annotations_out/08a", 0, 1)

        #ingress_message = Message()
        #ingress_message.address = addr
        #ingress_message.body = {'message': 'Hello World!'}

        ## Annotations with prefix "x-opt-qd." will be skipped
        #ingress_message_annotations = {'work': 'zarg', "x-opt-qd": "custom", "x-opt-qd.": "custom"}
        #ingress_message.annotations = ingress_message_annotations

        ## Put and send the message
        #M1.put(ingress_message)
        #M1.send()

        ## Receive the message
        #egress_message = Message()
        #M2.recv(1)
        #M2.get(egress_message)

        ## Make sure 'Hello World!' is in the message body dict
        #self.assertEqual('Hello World!', egress_message.body['message'])

        #self.assertEqual(egress_message.annotations, {'work': 'hard', "x-opt-qd": "custom"})

        #M1.stop()
        #M2.stop()

    #Send in pre-existing trace and ingress and annotations and make sure that they are not in the outgoing annotations.
    #stripAnnotations property is set to "in"
    def test_08a_test_strip_message_annotations_in(self):
        addr = "amqp:/strip_message_annotations_in/1"

        M1 = self.messenger()
        M2 = self.messenger()
        M1.route("amqp:/*", self.routers[0].addresses[4]+"/$1")
        M2.route("amqp:/*", self.routers[1].addresses[4]+"/$1")

        M1.start()
        M2.start()
        M2.subscribe(addr)
        self.routers[0].wait_address("strip_message_annotations_in/1", 0, 1)

        ingress_message = Message()
        ingress_message.address = addr
        ingress_message.body = {'message': 'Hello World!'}

        ##
        ## Pre-existing ingress and trace
        ##
        ingress_message_annotations = {'x-opt-qd.ingress': 'ingress-router', 'x-opt-qd.trace': ['X/QDR']}
        ingress_message.annotations = ingress_message_annotations

        #Put and send the message
        M1.put(ingress_message)
        M1.send()

        # Receive the message
        egress_message = Message()
        M2.recv(1)
        M2.get(egress_message)

         #Make sure 'Hello World!' is in the message body dict
        self.assertEqual('Hello World!', egress_message.body['message'])

        egress_message_annotations = egress_message.annotations

        self.assertEqual(egress_message_annotations.__class__, dict)
        self.assertEqual(egress_message_annotations['x-opt-qd.ingress'], '0/QDR.A')
        self.assertEqual(egress_message_annotations['x-opt-qd.trace'], ['0/QDR.A', '0/QDR.B'])

        M1.stop()
        M2.stop()


    def test_09_management(self):
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

    def test_10_semantics_multicast(self):
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

    def test_11a_semantics_closest_is_local(self):
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

    def test_11b_semantics_closest_is_remote(self):
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

    def test_12_semantics_spread(self):
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

    def test_13_to_override(self):
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

    def test_14_excess_deliveries_released(self):
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
        self.n_sent     = 0
        self.n_received = 0
        self.n_accepted = 0
        self.n_released = 0

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

if __name__ == '__main__':
    unittest.main(main_module())

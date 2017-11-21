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
from proton import Condition, Message, Delivery, PENDING, ACCEPTED, REJECTED, Url, symbol
from system_test import TestCase, Qdrouterd, main_module, TIMEOUT
from proton.handlers import MessagingHandler, TransactionHandler
from proton.reactor import Container, AtMostOnce, AtLeastOnce
from proton.utils import BlockingConnection, SyncRequestResponse
from qpid_dispatch.management.client import Node

CONNECTION_PROPERTIES_UNICODE_STRING = {u'connection': u'properties', u'int_property': 6451}
CONNECTION_PROPERTIES_SYMBOL = dict()
CONNECTION_PROPERTIES_SYMBOL[symbol("connection")] = symbol("properties")
CONNECTION_PROPERTIES_BINARY = {'client_identifier': 'policy_server'}


class OneRouterTest(TestCase):
    """System tests involving a single router"""
    @classmethod
    def setUpClass(cls):
        """Start a router and a messenger"""
        super(OneRouterTest, cls).setUpClass()
        name = "test-router"
        OneRouterTest.listen_port = cls.tester.get_port()
        config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'QDR', 'allowUnsettledMulticast': 'yes'}),

            # Setting the stripAnnotations to 'no' so that the existing tests will work.
            # Setting stripAnnotations to no will not strip the annotations and any tests that were already in this file
            # that were expecting the annotations to not be stripped will continue working.
            ('listener', {'port': OneRouterTest.listen_port, 'maxFrameSize': '2048', 'stripAnnotations': 'no'}),

            # The following listeners were exclusively added to test the stripAnnotations attribute in qdrouterd.conf file
            # Different listeners will be used to test all allowed values of stripAnnotations ('no', 'both', 'out', 'in')
            ('listener', {'port': cls.tester.get_port(), 'maxFrameSize': '2048', 'stripAnnotations': 'no'}),
            ('listener', {'port': cls.tester.get_port(), 'maxFrameSize': '2048', 'stripAnnotations': 'both'}),
            ('listener', {'port': cls.tester.get_port(), 'maxFrameSize': '2048', 'stripAnnotations': 'out'}),
            ('listener', {'port': cls.tester.get_port(), 'maxFrameSize': '2048', 'stripAnnotations': 'in'}),

            ('address', {'prefix': 'closest', 'distribution': 'closest'}),
            ('address', {'prefix': 'spread', 'distribution': 'balanced'}),
            ('address', {'prefix': 'multicast', 'distribution': 'multicast'}),
            ('address', {'prefix': 'unavailable', 'distribution': 'unavailable'})
        ])
        cls.router = cls.tester.qdrouterd(name, config)
        cls.router.wait_ready()
        cls.address = cls.router.addresses[0]

    def test_listen_error(self):
        """Make sure a router exits if a initial listener fails, doesn't hang"""
        config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'bad'}),
            ('listener', {'port': OneRouterTest.listen_port})])
        r = Qdrouterd(name="expect_fail", config=config, wait=False)
        self.assertEqual(1, r.wait())

    def test_01_pre_settled(self):
        addr = self.address+"/pre_settled/1"
        M1 = self.messenger()
        M2 = self.messenger()

        M1.start()
        M2.start()
        M2.subscribe(addr)

        tm = Message()
        rm = Message()

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
        addr = self.address+"/multicast.unsettled.1"
        M1 = self.messenger()
        M2 = self.messenger()
        M3 = self.messenger()
        M4 = self.messenger()


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


    def test_02b_disp_to_closed_connection(self):
        addr = self.address+"/pre_settled/2"
        M1 = self.messenger()
        M2 = self.messenger()


        M1.outgoing_window = 5
        M2.incoming_window = 5

        M1.start()
        M2.start()
        M2.subscribe(addr)

        tm = Message()
        rm = Message()

        tm.address = addr
        for i in range(2):
            tm.body = {'number': i}
            M1.put(tm)
        M1.send(0)
        M1.stop()

        for i in range(2):
            M2.recv(1)
            trk = M2.get(rm)
            M2.accept(trk)
            M2.settle(trk)
            self.assertEqual(i, rm.body['number'])

        M2.stop()


    def test_02c_sender_settles_first(self):
        addr = self.address+"/settled/senderfirst/1"
        M1 = self.messenger()
        M2 = self.messenger()


        M1.outgoing_window = 5
        M2.incoming_window = 5

        M1.start()
        M2.start()
        M2.subscribe(addr)

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
        addr = self.address+"/unsettled/1"
        M1 = self.messenger()
        M2 = self.messenger()

        M1.outgoing_window = 5
        M2.incoming_window = 5

        M1.start()
        M2.start()
        M2.subscribe(addr)

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
        addr = self.address+"/unsettled_undeliverable/1"
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

        self.assertEqual(exception, True)

        M1.stop()


    def test_05_three_ack(self):
        addr = self.address+"/three_ack/1"
        M1 = self.messenger()
        M2 = self.messenger()

        M1.outgoing_window = 5
        M2.incoming_window = 5

        M1.start()
        M2.start()
        M2.subscribe(addr)

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


#    def test_06_link_route_sender(self):
#        pass

#    def test_07_link_route_receiver(self):
#        pass


    def test_08_message_annotations(self):
        addr = self.address+"/ma/1"
        M1 = self.messenger()
        M2 = self.messenger()

        M1.start()
        M2.start()
        M2.subscribe(addr)

        tm = Message()
        rm = Message()

        tm.address = addr

        #
        # No inbound delivery annotations
        #
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
            self.assertEqual(ma['x-opt-qd.ingress'], '0/QDR')
            self.assertEqual(ma['x-opt-qd.trace'], ['0/QDR'])

        #
        # Pre-existing ingress
        #
        tm.annotations = {'x-opt-qd.ingress': 'ingress-router'}
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
            self.assertEqual(ma['x-opt-qd.ingress'], 'ingress-router')
            self.assertEqual(ma['x-opt-qd.trace'], ['0/QDR'])

        #
        # Invalid trace type
        #
        tm.annotations = {'x-opt-qd.trace' : 45}
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
            self.assertEqual(ma['x-opt-qd.ingress'], '0/QDR')
            self.assertEqual(ma['x-opt-qd.trace'], ['0/QDR'])

        #
        # Empty trace
        #
        tm.annotations = {'x-opt-qd.trace' : []}
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
            self.assertEqual(ma['x-opt-qd.ingress'], '0/QDR')
            self.assertEqual(ma['x-opt-qd.trace'], ['0/QDR'])

        #
        # Non-empty trace
        #
        tm.annotations = {'x-opt-qd.trace' : ['0/first.hop']}
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
            self.assertEqual(ma['x-opt-qd.ingress'], '0/QDR')
            self.assertEqual(ma['x-opt-qd.trace'], ['0/first.hop', '0/QDR'])

        M1.stop()
        M2.stop()

    # Tests stripping of ingress and egress annotations.
    # There is a property in qdrouter.json called stripAnnotations with possible values of ["in", "out", "both", "no"]
    # The default for stripAnnotations is "both" (which means strip annotations on both ingress and egress)
    # This test will test the stripAnnotations = no option - meaning no annotations must be stripped.
    # We will send in a custom annotation and make that we get back 3 annotations on the received message
    def test_08a_strip_message_annotations_custom(self):
        addr = self.router.addresses[1]+"/strip_message_annotations_no_custom/1"

        M1 = self.messenger()
        M2 = self.messenger()

        M1.start()
        M2.start()
        M2.subscribe(addr)

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
        self.assertEqual(egress_message_annotations['x-opt-qd.ingress'], '0/QDR')
        self.assertEqual(egress_message_annotations['x-opt-qd.trace'], ['0/QDR'])

        M1.stop()
        M2.stop()

    # stripAnnotations property is set to "no"
    def test_08a_test_strip_message_annotations_no(self):
        addr = self.router.addresses[1]+"/strip_message_annotations_no/1"

        M1 = self.messenger()
        M2 = self.messenger()

        M1.start()
        M2.start()
        M2.subscribe(addr)

        ingress_message = Message()
        ingress_message.address = addr
        ingress_message.body = {'message': 'Hello World!'}
        ingress_message_annotations = {}

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
        self.assertEqual(egress_message_annotations['x-opt-qd.ingress'], '0/QDR')
        self.assertEqual(egress_message_annotations['x-opt-qd.trace'], ['0/QDR'])

        M1.stop()
        M2.stop()

    # stripAnnotations property is set to "no"
    def test_08a_test_strip_message_annotations_no_add_trace(self):
        addr = self.router.addresses[1]+"/strip_message_annotations_no_add_trace/1"

        M1 = self.messenger()
        M2 = self.messenger()

        M1.start()
        M2.start()
        M2.subscribe(addr)

        ingress_message = Message()
        ingress_message.address = addr
        ingress_message.body = {'message': 'Hello World!'}

        #
        # Pre-existing ingress and trace
        #
        ingress_message_annotations = {'x-opt-qd.ingress': 'ingress-router',
                                       'x-opt-qd.trace': ['0/QDR.1'],
                                       'work': 'hard'}
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
        self.assertEqual(egress_message_annotations['x-opt-qd.ingress'], 'ingress-router')
        # Make sure the user defined annotation also makes it out.
        self.assertEqual(egress_message_annotations['work'], 'hard')
        self.assertEqual(egress_message_annotations['x-opt-qd.trace'], ['0/QDR.1', '0/QDR'])

        M1.stop()
        M2.stop()

    # Dont send any pre-existing ingress or trace annotations. Make sure that there are no outgoing message annotations
    # stripAnnotations property is set to "both"
    def test_08a_test_strip_message_annotations_both(self):
        addr = self.router.addresses[2]+"/strip_message_annotations_both/1"

        M1 = self.messenger()
        M2 = self.messenger()

        M1.start()
        M2.start()
        M2.subscribe(addr)

        ingress_message = Message()
        ingress_message.address = addr
        ingress_message.body = {'message': 'Hello World!'}

        #Put and send the message
        M1.put(ingress_message)
        M1.send()

        # Receive the message
        M2.recv(1)
        egress_message = Message()
        M2.get(egress_message)

        self.assertEqual(egress_message.annotations, None)

        M1.stop()
        M2.stop()

    # Dont send any pre-existing ingress or trace annotations. Send in a custom annotation.
    # Make sure that the custom annotation comes out and nothing else.
    # stripAnnotations property is set to "both"
    def test_08a_test_strip_message_annotations_both_custom(self):
        addr = self.router.addresses[2]+"/strip_message_annotations_both/1"

        M1 = self.messenger()
        M2 = self.messenger()

        M1.start()
        M2.start()
        M2.subscribe(addr)

        ingress_message = Message()
        ingress_message.address = addr
        ingress_message.body = {'message': 'Hello World!'}

        # Only annotations with prefix "x-opt-qd." will be stripped
        ingress_message_annotations = {'stay': 'humble', 'x-opt-qd': 'work'}
        ingress_message.annotations = ingress_message_annotations

        #Put and send the message
        M1.put(ingress_message)
        M1.send()

        # Receive the message
        M2.recv(1)
        egress_message = Message()
        M2.get(egress_message)

        self.assertEqual(egress_message.annotations, ingress_message_annotations)

        M1.stop()
        M2.stop()

    #Dont send any pre-existing ingress or trace annotations. Make sure that there are no outgoing message annotations
    #stripAnnotations property is set to "out"
    def test_08a_test_strip_message_annotations_out(self):
        addr = self.router.addresses[3]+"/strip_message_annotations_out/1"

        M1 = self.messenger()
        M2 = self.messenger()

        M1.start()
        M2.start()
        M2.subscribe(addr)

        ingress_message = Message()
        ingress_message.address = addr
        ingress_message.body = {'message': 'Hello World!'}

        #Put and send the message
        M1.put(ingress_message)
        M1.send()

        # Receive the message
        M2.recv(1)
        egress_message = Message()
        M2.get(egress_message)

        self.assertEqual(egress_message.annotations, None)

        M1.stop()
        M2.stop()

    #Send in pre-existing trace and ingress and annotations and make sure that they are not in the outgoing annotations.
    #stripAnnotations property is set to "in"
    def test_08a_test_strip_message_annotations_in(self):
        addr = self.router.addresses[4]+"/strip_message_annotations_in/1"

        M1 = self.messenger()
        M2 = self.messenger()

        M1.start()
        M2.start()
        M2.subscribe(addr)

        ingress_message = Message()
        ingress_message.address = addr
        ingress_message.body = {'message': 'Hello World!'}

        ##
        ## Pre-existing ingress and trace
        ##
        ingress_message_annotations = {'x-opt-qd.ingress': 'ingress-router', 'x-opt-qd.trace': ['0/QDR.1']}
        ingress_message.annotations = ingress_message_annotations

        #Put and send the message
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
        self.assertEqual(egress_message_annotations['x-opt-qd.ingress'], '0/QDR')
        self.assertEqual(egress_message_annotations['x-opt-qd.trace'], ['0/QDR'])

        M1.stop()
        M2.stop()


    def test_09_management(self):
        addr  = "amqp:/$management"

        M = self.messenger()
        M.start()
        M.route("amqp:/*", self.address+"/$1")
        sub = M.subscribe("amqp:/#")
        reply = sub.address

        request  = Message()
        response = Message()

        request.address        = addr
        request.reply_to       = reply
        request.correlation_id = "C1"
        request.properties     = {u'type':u'org.amqp.management', u'name':u'self', u'operation':u'GET-MGMT-NODES'}

        M.put(request)
        M.send()
        M.recv()
        M.get(response)

        assert response.properties['statusCode'] == 200, response.properties['statusCode']
        self.assertEqual(response.correlation_id, "C1")
        self.assertEqual(response.body, [])

        request.address        = addr
        request.reply_to       = reply
        request.correlation_id = 135
        request.properties     = {u'type':u'org.amqp.management', u'name':u'self', u'operation':u'GET-MGMT-NODES'}

        M.put(request)
        M.send()
        M.recv()
        M.get(response)

        self.assertEqual(response.properties['statusCode'], 200)
        self.assertEqual(response.correlation_id, 135)
        self.assertEqual(response.body, [])

        request.address        = addr
        request.reply_to       = reply
        request.properties     = {u'type':u'org.amqp.management', u'name':u'self', u'operation':u'GET-MGMT-NODES'}

        M.put(request)
        M.send()
        M.recv()
        M.get(response)

        self.assertEqual(response.properties['statusCode'], 200)
        self.assertEqual(response.body, [])

        M.stop()


    def test_09a_management_no_reply(self):
        addr  = "amqp:/$management"

        M = self.messenger()
        M.start()
        M.route("amqp:/*", self.address+"/$1")

        request  = Message()

        request.address        = addr
        request.correlation_id = "C1"
        request.properties     = {u'type':u'org.amqp.management', u'name':u'self', u'operation':u'GET-MGMT-NODES'}

        M.put(request)
        M.send()

        M.put(request)
        M.send()

        M.stop()


    def test_09c_management_get_operations(self):
        addr  = "amqp:/_local/$management"

        M = self.messenger()
        M.start()
        M.route("amqp:/*", self.address+"/$1")
        sub = M.subscribe("amqp:/#")
        reply = sub.address

        request  = Message()
        response = Message()

        ##
        ## Unrestricted request
        ##
        request.address    = addr
        request.reply_to   = reply
        request.properties = {u'type':u'org.amqp.management', u'name':u'self', u'operation':u'GET-OPERATIONS'}

        M.put(request)
        M.send()
        M.recv()
        M.get(response)

        self.assertEqual(response.properties['statusCode'], 200)
        self.assertEqual(response.body.__class__, dict)
        self.assertTrue('org.apache.qpid.dispatch.router' in response.body.keys())
        self.assertTrue(len(response.body.keys()) > 2)
        self.assertTrue(response.body['org.apache.qpid.dispatch.router'].__class__, list)

        M.stop()


    def test_09d_management_not_implemented(self):
        addr  = "amqp:/$management"

        M = self.messenger()
        M.start()
        M.route("amqp:/*", self.address+"/$1")
        sub = M.subscribe("amqp:/#")
        reply = sub.address

        request  = Message()
        response = Message()

        ##
        ## Request with an invalid operation
        ##
        request.address    = addr
        request.reply_to   = reply
        request.properties = {u'type':u'org.amqp.management', u'name':u'self', u'operation':u'NOT-IMPL'}

        M.put(request)
        M.send()
        M.recv()
        M.get(response)

        self.assertEqual(response.properties['statusCode'], 501)

        M.stop()


    def test_10_semantics_multicast(self):
        addr = self.address+"/multicast.10"
        M1 = self.messenger()
        M2 = self.messenger()
        M3 = self.messenger()
        M4 = self.messenger()


        M1.start()
        M2.start()
        M3.start()
        M4.start()

        M2.subscribe(addr)
        M3.subscribe(addr)
        M4.subscribe(addr)

        tm = Message()
        rm = Message()

        tm.address = addr
        for i in range(100):
            tm.body = {'number': i}
            M1.put(tm)
        M1.send()

        for i in range(100):
            M2.recv(1)
            M2.get(rm)
            self.assertEqual(i, rm.body['number'])

            M3.recv(1)
            M3.get(rm)
            self.assertEqual(i, rm.body['number'])

            M4.recv(1)
            M4.get(rm)
            self.assertEqual(i, rm.body['number'])

        M1.stop()
        M2.stop()
        M3.stop()
        M4.stop()

    def test_11_semantics_closest(self):
        addr = self.address+"/closest.1"
        M1 = self.messenger()
        M2 = self.messenger()
        M3 = self.messenger()
        M4 = self.messenger()


        M1.start()
        M2.start()
        M3.start()
        M4.start()

        M2.subscribe(addr)
        M3.subscribe(addr)
        M4.subscribe(addr)

        tm = Message()
        rm = Message()

        tm.address = addr
        for i in range(30):
            tm.body = {'number': i}
            M1.put(tm)
        M1.send()

        i = 0
        rx_set = []
        for i in range(10):
            M2.recv(1)
            M2.get(rm)
            rx_set.append(rm.body['number'])

            M3.recv(1)
            M3.get(rm)
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
        addr = self.address+"/spread.1"
        M1 = self.messenger()
        M2 = self.messenger()
        M3 = self.messenger()
        M4 = self.messenger()

        M2.timeout = 0.1
        M3.timeout = 0.1
        M4.timeout = 0.1

        M1.start()
        M2.start()
        M3.start()
        M4.start()

        M2.subscribe(addr)
        M3.subscribe(addr)
        M4.subscribe(addr)

        tm = Message()
        rm = Message()

        tm.address = addr
        for i in range(30):
            tm.body = {'number': i}
            M1.put(tm)
        M1.send()

        i = 0
        rx_set = []
        ca = 0
        cb = 0
        cc = 0

        while len(rx_set) < 30:
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

        self.assertEqual(30, len(rx_set))
        self.assertTrue(ca > 0)
        self.assertTrue(cb > 0)
        self.assertTrue(cc > 0)

        rx_set.sort()
        for i in range(30):
            self.assertEqual(i, rx_set[i])

        M1.stop()
        M2.stop()
        M3.stop()
        M4.stop()


    def test_13_to_override(self):
        addr = self.address+"/toov/1"
        M1 = self.messenger()
        M2 = self.messenger()

        M1.start()
        M2.start()
        M2.subscribe(addr)

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

    def test_14_send_settle_mode_settled(self):
        """
        The receiver sets a snd-settle-mode of settle thus indicating that it wants to receive settled messages from
        the sender. This tests make sure that the delivery that comes to the receiver comes as already settled.
        """
        send_settle_mode_test = SndSettleModeTest(self.address)
        send_settle_mode_test.run()
        self.assertTrue(send_settle_mode_test.message_received)
        self.assertTrue(send_settle_mode_test.delivery_already_settled)

    def test_15_excess_deliveries_released(self):
        """
        Message-route a series of deliveries where the receiver provides credit for a subset and
        once received, closes the link.  The remaining deliveries should be released back to the sender.
        """
        test = ExcessDeliveriesReleasedTest(self.address)
        test.run()
        self.assertEqual(None, test.error)

    def test_16_multicast_unsettled(self):
        test = MulticastUnsettledTest(self.address)
        test.run()
        self.assertEqual(None, test.error)

    # Will uncomment this test once https://issues.apache.org/jira/browse/PROTON-1514 is fixed
    #def test_17_multiframe_presettled(self):
    #    test = MultiframePresettledTest(self.address)
    #    test.run()
    #    self.assertEqual(None, test.error)

    def test_16a_multicast_no_receivcer(self):
        test = MulticastUnsettledNoReceiverTest(self.address)
        test.run()
        self.assertEqual(None, test.error)

    def test_18_released_vs_modified(self):
        test = ReleasedVsModifiedTest(self.address)
        test.run()
        self.assertEqual(None, test.error)

    def test_19_appearance_of_balance(self):
        test = AppearanceOfBalanceTest(self.address)
        test.run()
        self.assertEqual(None, test.error)

    def test_20_batched_settlement(self):
        test = BatchedSettlementTest(self.address)
        test.run()
        self.assertEqual(None, test.error)

    def test_21_presettled_overflow(self):
        test = PresettledOverflowTest(self.address)
        test.run()
        self.assertEqual(None, test.error)

    def test_27_create_unavailable_sender(self):
        test = UnavailableSender(self.address)
        test.run()
        self.assertTrue(test.passed)

    def test_28_create_unavailable_receiver(self):
        test = UnavailableReceiver(self.address)
        test.run()
        self.assertTrue(test.passed)

    def test_22_large_streaming_test(self):
        test = LargeMessageStreamTest(self.address)
        test.run()
        self.assertEqual(None, test.error)

    def test_25_reject_coordinator(self):
        test = RejectCoordinatorTest(self.address)
        test.run()
        self.assertTrue(test.passed)

    def test_reject_disposition(self):
        test = RejectDispositionTest(self.address)
        test.run()
        self.assertTrue(test.received_error)

    def test_connection_properties_unicode_string(self):
        """
        Tests connection property that is a map of unicode strings and integers
        """
        connection = BlockingConnection(self.router.addresses[0],
                                        timeout=60,
                                        properties=CONNECTION_PROPERTIES_UNICODE_STRING)
        client = SyncRequestResponse(connection)

        node = Node.connect(self.router.addresses[0])

        results = node.query(type='org.apache.qpid.dispatch.connection', attribute_names=[u'properties']).results

        found = False
        for result in results:
            if u'connection' in result[0] and u'int_property' in result[0]:
                found = True
                self.assertEqual(result[0][u'connection'], u'properties')
                self.assertEqual(result[0][u'int_property'], 6451)

        self.assertTrue(found)
        client.connection.close()

    def test_connection_properties_symbols(self):
        """
        Tests connection property that is a map of symbols
        """
        connection = BlockingConnection(self.router.addresses[0],
                                        timeout=60,
                                        properties=CONNECTION_PROPERTIES_SYMBOL)
        client = SyncRequestResponse(connection)

        node = Node.connect(self.router.addresses[0])

        results = node.query(type='org.apache.qpid.dispatch.connection', attribute_names=[u'properties']).results

        found = False
        for result in results:
            if u'connection' in result[0]:
                if result[0][u'connection'] == u'properties':
                    found = True
                    break

        self.assertTrue(found)

        client.connection.close()

    def test_connection_properties_binary(self):
        """
        Tests connection property that is a binary map. The router ignores AMQP binary data type.
        Router should not return anything for connection properties
        """
        connection = BlockingConnection(self.router.addresses[0],
                                        timeout=60,
                                        properties=CONNECTION_PROPERTIES_BINARY)
        client = SyncRequestResponse(connection)

        node = Node.connect(self.router.addresses[0])

        results = node.query(type='org.apache.qpid.dispatch.connection', attribute_names=[u'properties']).results

        results_found = True

        for result in results:
            if not result[0]:
                results_found = False
            else:
                results_found = True
                break

        self.assertFalse(results_found)

        client.connection.close()


class Timeout(object):
    def __init__(self, parent):
        self.parent = parent

    def on_timer_task(self, event):
        self.parent.timeout()


HELLO_WORLD = "Hello World!"

class SndSettleModeTest(MessagingHandler):
    def __init__(self, address):
        super(SndSettleModeTest, self).__init__()
        self.address = address
        self.sender = None
        self.receiver = None
        self.message_received = False
        self.delivery_already_settled = False

    def on_start(self, event):
        conn = event.container.connect(self.address)
        # The receiver sets link.snd_settle_mode = Link.SND_SETTLED. It wants to receive settled messages
        self.receiver = event.container.create_receiver(conn, "org/apache/dev", options=AtMostOnce())

        # With AtLeastOnce, the sender will not settle.
        self.sender = event.container.create_sender(conn, "org/apache/dev", options=AtLeastOnce())

    def on_sendable(self, event):
        msg = Message(body=HELLO_WORLD)
        event.sender.send(msg)
        event.sender.close()

    def on_message(self, event):
        self.delivery_already_settled = event.delivery.settled
        if HELLO_WORLD == event.message.body:
            self.message_received = True
        else:
            self.message_received = False
        event.connection.close()

    def run(self):
        Container(self).run()


class ExcessDeliveriesReleasedTest(MessagingHandler):
    def __init__(self, address):
        super(ExcessDeliveriesReleasedTest, self).__init__(prefetch=0)
        self.address = address
        self.dest = "closest.EDRtest"
        self.error = None
        self.sender = None
        self.receiver = None
        self.n_sent     = 0
        self.n_received = 0
        self.n_accepted = 0
        self.n_released = 0

    def on_start(self, event):
        conn = event.container.connect(self.address)
        self.sender   = event.container.create_sender(conn, self.dest)
        self.receiver = event.container.create_receiver(conn, self.dest)
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
            event.connection.close()

    def on_message(self, event):
        self.n_received += 1
        if self.n_received == 6:
            self.receiver.close()

    def run(self):
        Container(self).run()

class UnavailableBase(MessagingHandler):
    def __init__(self, address):
        super(UnavailableBase, self).__init__()
        self.address = address
        self.dest = "unavailable"
        self.conn = None
        self.sender = None
        self.receiver = None
        self.link_error = False
        self.link_closed = False
        self.passed = False
        self.timer = None
        self.link_name = "test_link"

    def check_if_done(self):
        if self.link_error and self.link_closed:
            self.passed = True
            self.conn.close()
            self.timer.cancel()

    def on_link_error(self, event):
        link = event.link
        if event.link.name == self.link_name and link.remote_condition.description \
                == "Node not found":
            self.link_error = True
        self.check_if_done()

    def on_link_remote_close(self, event):
        if event.link.name == self.link_name:
            self.link_closed = True
            self.check_if_done()

    def run(self):
        Container(self).run()

class UnavailableSender(UnavailableBase):
    def __init__(self, address):
        super(UnavailableSender, self).__init__(address)

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.conn = event.container.connect(self.address)
        # Creating a sender to an address with unavailable distribution
        # The router will not allow this link to be established. It will close the link with an error of
        # "Node not found"
        self.sender = event.container.create_sender(self.conn, self.dest, name=self.link_name)

class UnavailableReceiver(UnavailableBase):
    def __init__(self, address):
        super(UnavailableReceiver, self).__init__(address)

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.conn = event.container.connect(self.address)
        # Creating a receiver to an address with unavailable distribution
        # The router will not allow this link to be established. It will close the link with an error of
        # "Node not found"
        self.receiver = event.container.create_receiver(self.conn, self.dest, name=self.link_name)

class MulticastUnsettledTest(MessagingHandler):
    def __init__(self, address):
        super(MulticastUnsettledTest, self).__init__(prefetch=0)
        self.address = address
        self.dest = "multicast.MUtest"
        self.error = None
        self.count      = 10
        self.n_sent     = 0
        self.n_received = 0
        self.n_accepted = 0

    def check_if_done(self):
        if self.n_received == self.count * 2 and self.n_accepted == self.count:
            self.timer.cancel()
            self.conn.close()

    def timeout(self):
        self.error = "Timeout Expired: sent=%d, received=%d, accepted=%d" % (self.n_sent, self.n_received, self.n_accepted)
        self.conn.close()

    def on_start(self, event):
        self.timer     = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.conn      = event.container.connect(self.address)
        self.sender    = event.container.create_sender(self.conn, self.dest)
        self.receiver1 = event.container.create_receiver(self.conn, self.dest, name="A")
        self.receiver2 = event.container.create_receiver(self.conn, self.dest, name="B")
        self.receiver1.flow(self.count)
        self.receiver2.flow(self.count)

    def on_sendable(self, event):
        for i in range(self.count - self.n_sent):
            msg = Message(body=i)
            event.sender.send(msg)
            self.n_sent += 1

    def on_accepted(self, event):
        self.n_accepted += 1
        self.check_if_done()

    def on_message(self, event):
        if not event.delivery.settled:
            self.error = "Received unsettled delivery"
        self.n_received += 1
        self.check_if_done()

    def run(self):
        Container(self).run()

class LargeMessageStreamTest(MessagingHandler):
    def __init__(self, address):
        super(LargeMessageStreamTest, self).__init__()
        self.address = address
        self.dest = "LargeMessageStreamTest"
        self.error = None
        self.count = 10
        self.n_sent = 0
        self.timer = None
        self.conn = None
        self.sender = None
        self.receiver = None
        self.n_received = 0
        self.body = ""
        for i in range(10000):
            self.body += "0123456789101112131415"

    def check_if_done(self):
        if self.n_received == self.count:
            self.timer.cancel()
            self.conn.close()

    def timeout(self):
        self.error = "Timeout Expired: sent=%d, received=%d" % (self.n_sent, self.n_received)
        self.conn.close()

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.conn = event.container.connect(self.address)
        self.sender = event.container.create_sender(self.conn, self.dest)
        self.receiver = event.container.create_receiver(self.conn, self.dest, name="A")
        self.receiver.flow(self.count)

    def on_sendable(self, event):
        for i in range(self.count):
            msg = Message(body=self.body)
            # send(msg) calls the stream function which streams data from sender to the router
            event.sender.send(msg)
            self.n_sent += 1

    def on_message(self, event):
        self.n_received += 1
        self.check_if_done()

    def run(self):
        Container(self).run()


class MultiframePresettledTest(MessagingHandler):
    def __init__(self, address):
        super(MultiframePresettledTest, self).__init__(prefetch=0)
        self.address = address
        self.dest = "closest.MFPtest"
        self.error = None
        self.count      = 10
        self.n_sent     = 0
        self.n_received = 0

        self.body = ""
        for i in range(10000):
            self.body += "0123456789"

    def check_if_done(self):
        if self.n_received == self.count:
            self.timer.cancel()
            self.conn.close()

    def timeout(self):
        self.error = "Timeout Expired: sent=%d, received=%d" % (self.n_sent, self.n_received)
        self.conn.close()

    def on_start(self, event):
        self.timer     = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.conn      = event.container.connect(self.address)
        self.sender    = event.container.create_sender(self.conn, self.dest)
        self.receiver  = event.container.create_receiver(self.conn, self.dest, name="A")
        self.receiver.flow(self.count)

    def on_sendable(self, event):
        for i in range(self.count - self.n_sent):
            msg = Message(body=self.body)
            dlv = event.sender.send(msg)
            dlv.settle()
            self.n_sent += 1

    def on_message(self, event):
        if not event.delivery.settled:
            self.error = "Received unsettled delivery"
        self.n_received += 1
        self.check_if_done()

    def run(self):
        Container(self).run()

class MulticastUnsettledNoReceiverTest(MessagingHandler):
    """
    Creates a sender to a multicast address. Router provides a credit of 'linkCapacity' to this sender even
    if there are no receivers (The sender should be able to send messages to multicast addresses even when no receiver
    is connected). The router will send a disposition of released back to the sender and will end up dropping
    these messages since there is no receiver.
    """
    def __init__(self, address):
        super(MulticastUnsettledNoReceiverTest, self).__init__(prefetch=0)
        self.address = address
        self.dest = "multicast.MulticastNoReceiverTest"
        self.error = "Some error"
        self.n_sent = 0
        self.max_send = 250
        self.n_released = 0
        self.n_accepted = 0
        self.timer = None
        self.conn = None
        self.sender = None

    def check_if_done(self):
        if self.n_accepted > 0:
            self.error = "Messages should not be accepted as there are no receivers"
            self.timer.cancel()
            self.conn.close()
        elif self.n_sent == self.n_released:
            self.error = None
            self.timer.cancel()
            self.conn.close()

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.conn = event.container.connect(self.address)
        self.sender = event.container.create_sender(self.conn, self.dest)

    def on_sendable(self, event):
        if self.n_sent >= self.max_send:
            return
        self.n_sent += 1
        msg = Message(body=self.n_sent)
        event.sender.send(msg)

    def on_accepted(self, event):
        self.n_accepted += 1
        self.check_if_done()

    def on_released(self, event):
        self.n_released += 1
        self.check_if_done()

    def run(self):
        Container(self).run()


class ReleasedVsModifiedTest(MessagingHandler):
    def __init__(self, address):
        super(ReleasedVsModifiedTest, self).__init__(prefetch=0, auto_accept=False)
        self.address = address
        self.dest = "closest.RVMtest"
        self.error = None
        self.count      = 10
        self.accept     = 6
        self.n_sent     = 0
        self.n_received = 0
        self.n_released = 0
        self.n_modified = 0

    def check_if_done(self):
        if self.n_received == self.accept and self.n_released == self.count - self.accept and self.n_modified == self.accept:
            self.timer.cancel()
            self.conn.close()

    def timeout(self):
        self.error = "Timeout Expired: sent=%d, received=%d, released=%d, modified=%d" % \
                     (self.n_sent, self.n_received, self.n_released, self.n_modified)
        self.conn.close()

    def on_start(self, event):
        self.timer     = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.conn      = event.container.connect(self.address)
        self.sender    = event.container.create_sender(self.conn, self.dest)
        self.receiver  = event.container.create_receiver(self.conn, self.dest, name="A")
        self.receiver.flow(self.accept)

    def on_sendable(self, event):
        for i in range(self.count - self.n_sent):
            msg = Message(body="RvM-Test")
            event.sender.send(msg)
            self.n_sent += 1

    def on_message(self, event):
        self.n_received += 1
        if self.n_received == self.accept:
            self.receiver.close()

    def on_released(self, event):
        if event.delivery.remote_state == Delivery.MODIFIED:
            self.n_modified += 1
        else:
            self.n_released += 1
        self.check_if_done()

    def run(self):
        Container(self).run()


class AppearanceOfBalanceTest(MessagingHandler):
    def __init__(self, address):
        super(AppearanceOfBalanceTest, self).__init__()
        self.address = address
        self.dest = "balanced.AppearanceTest"
        self.error = None
        self.count        = 9
        self.n_sent       = 0
        self.n_received_a = 0
        self.n_received_b = 0
        self.n_received_c = 0

    def check_if_done(self):
        if self.n_received_a + self.n_received_b + self.n_received_c == self.count:
            if self.n_received_a != 3 or self.n_received_b != 3 or self.n_received_c != 3:
                self.error = "Incorrect Distribution: %d/%d/%d" % (self.n_received_a, self.n_received_b, self.n_received_c)
            self.timer.cancel()
            self.conn.close()

    def timeout(self):
        self.error = "Timeout Expired: sent=%d rcvd=%d/%d/%d" % \
                     (self.n_sent, self.n_received_a, self.n_received_b, self.n_received_c)
        self.conn.close()

    def on_start(self, event):
        self.timer      = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.conn       = event.container.connect(self.address)
        self.sender     = event.container.create_sender(self.conn, self.dest)
        self.receiver_a = event.container.create_receiver(self.conn, self.dest, name="A")
        self.receiver_b = event.container.create_receiver(self.conn, self.dest, name="B")
        self.receiver_c = event.container.create_receiver(self.conn, self.dest, name="C")

    def send(self):
        if self.n_sent < self.count:
            msg = Message(body="Appearance-Test")
            self.sender.send(msg)
            self.n_sent += 1

    def on_sendable(self, event):
        if self.n_sent == 0:
            self.send()

    def on_message(self, event):
        if event.receiver == self.receiver_a:
            self.n_received_a += 1
        if event.receiver == self.receiver_b:
            self.n_received_b += 1
        if event.receiver == self.receiver_c:
            self.n_received_c += 1

    def on_accepted(self, event):
        self.send()
        self.check_if_done()

    def run(self):
        Container(self).run()


class BatchedSettlementTest(MessagingHandler):
    def __init__(self, address):
        super(BatchedSettlementTest, self).__init__(auto_accept=False)
        self.address = address
        self.dest = "balanced.BatchedSettlement"
        self.error = None
        self.count       = 200
        self.batch_count = 20
        self.n_sent      = 0
        self.n_received  = 0
        self.n_settled   = 0
        self.batch       = []

    def check_if_done(self):
        if self.n_settled == self.count:
            self.timer.cancel()
            self.conn.close()

    def timeout(self):
        self.error = "Timeout Expired: sent=%d rcvd=%d settled=%d" % \
                     (self.n_sent, self.n_received, self.n_settled)
        self.conn.close()

    def on_start(self, event):
        self.timer    = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.conn     = event.container.connect(self.address)
        self.sender   = event.container.create_sender(self.conn, self.dest)
        self.receiver = event.container.create_receiver(self.conn, self.dest)

    def send(self):
        while self.n_sent < self.count and self.sender.credit > 0:
            msg = Message(body="Batch-Test")
            self.sender.send(msg)
            self.n_sent += 1

    def on_sendable(self, event):
        self.send()

    def on_message(self, event):
        self.n_received += 1
        self.batch.insert(0, event.delivery)
        if len(self.batch) == self.batch_count:
            while len(self.batch) > 0:
                self.accept(self.batch.pop())

    def on_accepted(self, event):
        self.n_settled += 1
        self.check_if_done()

    def run(self):
        Container(self).run()


class RejectCoordinatorTest(MessagingHandler, TransactionHandler):
    def __init__(self, url):
        super(RejectCoordinatorTest, self).__init__(prefetch=0)
        self.url = Url(url)
        self.error = "The router can't coordinate transactions by itself, a linkRoute to a coordinator must be " \
                     "configured to use transactions."
        self.container = None
        self.conn = None
        self.sender = None
        self.timer = None
        self.passed = False
        self.link_error = False
        self.link_remote_close = False

    def timeout(self):
        self.conn.close()

    def check_if_done(self):
        if self.link_remote_close and self.link_error:
            self.passed = True
            self.conn.close()
            self.timer.cancel()

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.container = event.container
        self.conn = self.container.connect(self.url)
        self.sender = self.container.create_sender(self.conn, self.url.path)
        # declare_transaction tries to create a link with name "txn-ctrl" to the
        # transaction coordinator which has its own target, it has no address
        # The router cannot coordinate transactions itself and so there will be a link error when this
        # link is attempted to be created
        self.container.declare_transaction(self.conn, handler=self)

    def on_link_error(self, event):
        link = event.link
        # If the link name is 'txn-ctrl' and there is a link error and it matches self.error, then we know
        # that the router has rejected the link because it cannot coordinate transactions itself
        if link.name == "txn-ctrl" and link.remote_condition.description == self.error and \
                        link.remote_condition.name == 'amqp:precondition-failed':
            self.link_error = True
            self.check_if_done()

    def on_link_remote_close(self, event):
        link = event.link
        if link.name == "txn-ctrl":
            self.link_remote_close = True
            self.check_if_done()

    def run(self):
        Container(self).run()


class PresettledOverflowTest(MessagingHandler):
    def __init__(self, address):
        super(PresettledOverflowTest, self).__init__(prefetch=0)
        self.address = address
        self.dest = "balanced.PresettledOverflow"
        self.error = None
        self.count       = 500
        self.n_sent      = 0
        self.n_received  = 0
        self.last_seq    = -1

    def timeout(self):
        self.error = "Timeout Expired: sent=%d rcvd=%d last_seq=%d" % (self.n_sent, self.n_received, self.last_seq)
        self.conn.close()

    def on_start(self, event):
        self.timer    = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.conn     = event.container.connect(self.address)
        self.sender   = event.container.create_sender(self.conn, self.dest)
        self.receiver = event.container.create_receiver(self.conn, self.dest)
        self.receiver.flow(10)

    def send(self):
        while self.n_sent < self.count and self.sender.credit > 0:
            msg = Message(body={"seq": self.n_sent})
            dlv = self.sender.send(msg)
            dlv.settle()
            self.n_sent += 1
        if self.n_sent == self.count:
            self.receiver.flow(self.count)

    def on_sendable(self, event):
        if self.n_sent < self.count:
            self.send()

    def on_message(self, event):
        self.n_received += 1
        self.last_seq = event.message.body["seq"]
        if self.last_seq == self.count - 1:
            if self.n_received == self.count:
                self.error = "No deliveries were dropped"
            self.conn.close()
            self.timer.cancel()

    def run(self):
        Container(self).run()


class RejectDispositionTest(MessagingHandler):
    def __init__(self, address):
        super(RejectDispositionTest, self).__init__(auto_accept=False)
        self.address = address
        self.sent = False
        self.received_error = False
        self.dest = "rejectDispositionTest"
        self.error_description = 'you were out of luck this time!'
        self.error_name = u'amqp:internal-error'


    def on_start(self, event):
        conn = event.container.connect(self.address)
        event.container.create_sender(conn, self.dest)
        event.container.create_receiver(conn, self.dest)

    def on_sendable(self, event):
        if not self.sent:
            event.sender.send(Message(body=u"Hello World!"))
            self.sent = True

    def on_rejected(self, event):
        if event.delivery.remote.condition.description == self.error_description \
                and event.delivery.remote.condition.name == self.error_name:
            self.received_error = True
        event.connection.close()

    def on_message(self, event):
        event.delivery.local.condition = Condition(self.error_name, self.error_description)
        self.reject(event.delivery)

    def run(self):
        Container(self).run()

if __name__ == '__main__':
    unittest.main(main_module())

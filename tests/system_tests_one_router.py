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
from proton import Message, PENDING, ACCEPTED, REJECTED, RELEASED
from system_test import TestCase, Messenger, Qdrouterd, main_module

class RouterTest(TestCase):
    """System tests involving a single router"""

    @classmethod
    def setUpClass(cls):
        """Start a router and a messenger"""
        super(RouterTest, cls).setUpClass()
        name = "test-router"
        config = Qdrouterd.Config([
            ('container', {'workerThreads': 4, 'containerName': 'Qpid.Dispatch.Router.A'}),
            ('router', {'mode': 'standalone', 'routerId': 'QDR'}),
            ('listener', {'port': cls.tester.get_port()}),
            ('fixedAddress', {'prefix': '/closest/', 'fanout': 'single', 'bias': 'closest'}),
            ('fixedAddress', {'prefix': '/spread/', 'fanout': 'single', 'bias': 'spread'}),
            ('fixedAddress', {'prefix': '/multicast/', 'fanout': 'multiple'}),
            ('fixedAddress', {'prefix': '/', 'fanout': 'multiple'})
        ])
        cls.router = cls.tester.qdrouterd(name, config)
        cls.router.wait_ready()
        cls.address = cls.router.addresses[0]


    def test_00_discard(self):
        addr = self.address+"/discard/1"
        M1 = self.messenger()
        M1.start()
        tm = Message()
        tm.address = addr
        for i in range(100):
            tm.body = {'number': i}
            M1.put(tm)
        M1.send()
        M1.stop()


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


    def test_02_multicast(self):
        addr = self.address+"/pre_settled/multicast/1"
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


    def test_02a_multicast_unsettled(self):
        addr = self.address+"/pre_settled/multicast/1"
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
        addr = self.address+"/pre_settled/multicast/1"
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
        tm = Message()
        tm.address = addr
        tm.body = {'number': 200}

        tx_tracker = M1.put(tm)
        M1.send(0)
        M1.flush()
        self.assertEqual(RELEASED, M1.status(tx_tracker))

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


        ##
        ## No inbound delivery annotations
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
            self.assertEqual(ma['x-opt-qd.ingress'], '0/QDR')
            self.assertEqual(ma['x-opt-qd.trace'], ['0/QDR'])

        ##
        ## Pre-existing ingress
        ##
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

        ##
        ## Invalid trace type
        ##
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

        ##
        ## Empty trace
        ##
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

        ##
        ## Non-empty trace
        ##
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
        addr = self.address+"/multicast/1"
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
        addr = self.address+"/closest/1"
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
        addr = self.address+"/spread/1"
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


if __name__ == '__main__':
    unittest.main(main_module())

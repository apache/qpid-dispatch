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

import unittest, os
from proton import Message, PENDING, ACCEPTED, REJECTED, RELEASED, SSLDomain, SSLUnavailable
from system_test import TestCase, Qdrouterd, main_module


class RouterTest(TestCase):
    @classmethod
    def setUpClass(cls):
        """Start a router and a messenger"""
        super(RouterTest, cls).setUpClass()

        def ssl_config(client_server, connection): return [] # Over-ridden by RouterTestSsl

        def router(name, client_server, connection):
            config = Qdrouterd.Config(ssl_config(client_server, connection) + [
                ('container', {'workerThreads': 4, 'containerName': 'Qpid.Dispatch.Router.%s'%name}),
                ('router', {'mode': 'interior', 'routerId': 'QDR.%s'%name}),
                ('listener', {'port': cls.tester.get_port()}),
                ('fixedAddress', {'prefix': '/closest/', 'fanout': 'single', 'bias': 'closest'}),
                ('fixedAddress', {'prefix': '/spread/', 'fanout': 'single', 'bias': 'spread'}),
                ('fixedAddress', {'prefix': '/multicast/', 'fanout': 'multiple'}),
                ('fixedAddress', {'prefix': '/', 'fanout': 'multiple'}),
                connection
            ])
            cls.routers.append(cls.tester.qdrouterd(name, config, wait=True))

        cls.routers = []
        router('A', 'server',
               ('listener', {'role': 'inter-router', 'port': cls.tester.get_port()}))
        router('B', 'client',
               ('connector', {'role': 'inter-router', 'port': cls.routers[0].ports[1]}))

        cls.routers[0].wait_router_connected('QDR.B')
        cls.routers[1].wait_router_connected('QDR.A')


    def test_00_discard(self):
        addr = self.routers[0].addresses[0]+"/discard/1"
        M1 = self.messenger()
        tm = Message()
        tm.address = addr
        for i in range(100):
            tm.body = {'number': i}
            M1.put(tm)
        M1.send()

    def test_01_pre_settled(self):
        addr = "amqp:/pre_settled/1"
        M1 = self.messenger()
        M2 = self.messenger()

        M1.route("amqp:/*", self.routers[0].addresses[0]+"/$1")
        M2.route("amqp:/*", self.routers[1].addresses[0]+"/$1")

        M2.subscribe(addr)

        tm = Message()
        rm = Message()

        self.routers[0].wait_address("pre_settled/1", 0, 1)

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
        addr = "amqp:/pre_settled/multicast/1"
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
        self.routers[0].wait_address("pre_settled/multicast/1", 1, 1)

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
        addr = "amqp:/pre_settled/multicast/2"
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
        self.routers[0].wait_address("pre_settled/multicast/2", 1, 1)

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
        addr = "amqp:/settled/senderfirst/1"
        M1 = self.messenger()
        M2 = self.messenger()

        M1.route("amqp:/*", self.routers[0].addresses[0]+"/$1")
        M2.route("amqp:/*", self.routers[1].addresses[0]+"/$1")

        M1.outgoing_window = 5
        M2.incoming_window = 5

        M1.start()
        M2.start()
        M2.subscribe(addr)
        self.routers[0].wait_address("settled/senderfirst/1", 0, 1)

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
        tm = Message()
        tm.address = addr
        tm.body = {'number': 200}

        tx_tracker = M1.put(tm)
        M1.send(0)
        M1.flush()
        self.assertEqual(RELEASED, M1.status(tx_tracker))

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


    def test_08_delivery_annotations(self):
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
            self.assertEqual(ma['x-opt-qd.ingress'], '0/QDR.A')
            self.assertEqual(ma['x-opt-qd.trace'], ['0/QDR.A', '0/QDR.B'])

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
        addr = "amqp:/multicast/1"
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
        self.routers[0].wait_address("multicast/1", 1, 1)

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
        addr = "amqp:/closest/1"
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
        self.routers[0].wait_address("closest/1", 1, 1)

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

        self.assertEqual(30, len(rx_set))
        rx_set.sort()
        for i in range(30):
            self.assertEqual(i, rx_set[i])

        M1.stop()
        M2.stop()
        M3.stop()
        M4.stop()

    def test_11b_semantics_closest_is_remote(self):
        addr = "amqp:/closest/2"
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
        self.routers[0].wait_address("closest/2", 0, 1)

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
        addr = "amqp:/spread/1"
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
        self.routers[0].wait_address("spread/1", 1, 1)

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



try:
    SSLDomain(SSLDomain.MODE_CLIENT)
    class RouterTestSsl(RouterTest):
        def ssl_config(self, client_server, connection):
            connection[1]['ssl-profile'] = 'ssl-profile-name'
            def ssl_file(name):
                return os.path.join(os.path.dirname(__file__), 'config-2', name)
            return [
                ('ssl-profile', {
                    'name': 'ssl-profile-name',
                    'cert-db': ssl_file('ca-certificate.pem'),
                    'cert-file': ssl_file(client_server+'-certificate.pem'),
                    'key-file': ssl_file(client_server+'-private-key.pem'),
                    'password': client_server+'-password'})]

except SSLUnavailable:
    class RouterTestSsl(TestCase):
        def test_skip(self):
            self.skipTest("Proton SSL support unavailable.")

if __name__ == '__main__':
    unittest.main(main_module())

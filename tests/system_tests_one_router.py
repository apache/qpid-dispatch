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

import sys
import os
import time
import unittest
import subprocess
from proton import Messenger, Message, PENDING, ACCEPTED, REJECTED, RELEASED

def startRouter(obj):
    default_home = os.path.normpath('/usr/lib/qpid-dispatch')
    home = os.environ.get('QPID_DISPATCH_HOME', default_home)
    config_file = '%s/tests/config-1/A.conf' % home

    obj.router = subprocess.Popen(['qdrouterd', '-c', config_file],
                                  stderr=subprocess.PIPE,
                                  stdout=subprocess.PIPE)
    time.sleep(1)

def stopRouter(obj):
    obj.router.terminate()
    obj.router.communicate()


class RouterTest(unittest.TestCase):

    if (sys.version_info[0] == 2) and (sys.version_info[1] < 7):
        def setUp(self):
            startRouter(self)

        def tearDown(self):
            stopRouter(self)
    else:
        @classmethod
        def setUpClass(cls):
            startRouter(cls)

        @classmethod
        def tearDownClass(cls):
            stopRouter(cls)

    def flush(self, messenger):
        while messenger.work(0.1):
            pass

    def subscribe(self, messenger, address):
        sub = messenger.subscribe(address)
        self.flush(messenger)
        return sub


    def test_00_discard(self):
        addr = "amqp://0.0.0.0:20000/discard/1"
        M1 = Messenger()
        M1.timeout = 1.0
        M1.start()
        tm = Message()
        tm.address = addr
        for i in range(100):
            tm.body = {'number': i}
            M1.put(tm)
        M1.send()
        M1.stop()


    def test_01_pre_settled(self):
        addr = "amqp://0.0.0.0:20000/pre_settled/1"
        M1 = Messenger()
        M2 = Messenger()

        M1.timeout = 1.0
        M2.timeout = 1.0

        M1.start()
        M2.start()
        self.subscribe(M2, addr)

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
        addr = "amqp://0.0.0.0:20000/pre_settled/multicast/1"
        M1 = Messenger()
        M2 = Messenger()
        M3 = Messenger()
        M4 = Messenger()

        M1.timeout = 1.0
        M2.timeout = 1.0
        M3.timeout = 1.0
        M4.timeout = 1.0

        M1.start()
        M2.start()
        M3.start()
        M4.start()

        self.subscribe(M2, addr)
        self.subscribe(M3, addr)
        self.subscribe(M4, addr)

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
        addr = "amqp://0.0.0.0:20000/pre_settled/multicast/1"
        M1 = Messenger()
        M2 = Messenger()
        M3 = Messenger()
        M4 = Messenger()

        M1.timeout = 1.0
        M2.timeout = 1.0
        M3.timeout = 1.0
        M4.timeout = 1.0

        M1.outgoing_window = 5
        M2.incoming_window = 5
        M3.incoming_window = 5
        M4.incoming_window = 5

        M1.start()
        M2.start()
        M3.start()
        M4.start()

        self.subscribe(M2, addr)
        self.subscribe(M3, addr)
        self.subscribe(M4, addr)

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
        addr = "amqp://0.0.0.0:20000/pre_settled/multicast/1"
        M1 = Messenger()
        M2 = Messenger()

        M1.timeout = 1.0
        M2.timeout = 1.0

        M1.outgoing_window = 5
        M2.incoming_window = 5

        M1.start()
        M2.start()
        self.subscribe(M2, addr)

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
        addr = "amqp://0.0.0.0:20000/settled/senderfirst/1"
        M1 = Messenger()
        M2 = Messenger()

        M1.timeout = 1.0
        M2.timeout = 1.0

        M1.outgoing_window = 5
        M2.incoming_window = 5

        M1.start()
        M2.start()
        self.subscribe(M2, addr)

        tm = Message()
        rm = Message()

        tm.address = addr
        tm.body = {'number': 0}
        ttrk = M1.put(tm)
        M1.send(0)

        M1.settle(ttrk)
        self.flush(M1)
        self.flush(M2)

        M2.recv(1)
        rtrk = M2.get(rm)
        M2.accept(rtrk)
        M2.settle(rtrk)
        self.assertEqual(0, rm.body['number'])

        self.flush(M1)
        self.flush(M2)

        M1.stop()
        M2.stop()


    def test_03_propagated_disposition(self):
        addr = "amqp://0.0.0.0:20000/unsettled/1"
        M1 = Messenger()
        M2 = Messenger()

        M1.timeout = 1.0
        M2.timeout = 1.0
        M1.outgoing_window = 5
        M2.incoming_window = 5

        M1.start()
        M2.start()
        self.subscribe(M2, addr)

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

        self.flush(M2)
        self.flush(M1)

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

        self.flush(M2)
        self.flush(M1)

        self.assertEqual(REJECTED, M1.status(tx_tracker))

        M1.stop()
        M2.stop()


    def test_04_unsettled_undeliverable(self):
        addr = "amqp://0.0.0.0:20000/unsettled_undeliverable/1"
        M1 = Messenger()

        M1.timeout = 1.0
        M1.outgoing_window = 5

        M1.start()
        tm = Message()
        tm.address = addr
        tm.body = {'number': 200}

        tx_tracker = M1.put(tm)
        M1.send(0)
        self.flush(M1)
        self.assertEqual(RELEASED, M1.status(tx_tracker))

        M1.stop()


    def test_05_three_ack(self):
        addr = "amqp://0.0.0.0:20000/three_ack/1"
        M1 = Messenger()
        M2 = Messenger()

        M1.timeout = 1.0
        M2.timeout = 1.0
        M1.outgoing_window = 5
        M2.incoming_window = 5

        M1.start()
        M2.start()
        self.subscribe(M2, addr)

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

        self.flush(M2)
        self.flush(M1)

        self.assertEqual(ACCEPTED, M1.status(tx_tracker))

        M1.settle(tx_tracker)

        self.flush(M1)
        self.flush(M2)

        ##
        ## We need a way to verify on M2 (receiver) that the tracker has been
        ## settled on the M1 (sender).  [ See PROTON-395 ]
        ##

        M2.settle(rx_tracker)

        self.flush(M2)
        self.flush(M1)

        M1.stop()
        M2.stop()


#    def test_06_link_route_sender(self):
#        pass 

#    def test_07_link_route_receiver(self):
#        pass 


    def test_08_delivery_annotations(self):
        addr = "amqp://0.0.0.0:20000/da/1"
        M1 = Messenger()
        M2 = Messenger()

        M1.timeout = 1.0
        M2.timeout = 1.0

        M1.start()
        M2.start()
        self.subscribe(M2, addr)

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
            da = rm.instructions
            self.assertEqual(da.__class__, dict)
            self.assertEqual(da['x-opt-qd.ingress'], '0/QDR')
            self.assertEqual(da['x-opt-qd.trace'], ['0/QDR'])

        ##
        ## Pre-existing ingress
        ##
        tm.instructions = {'x-opt-qd.ingress': 'ingress-router'}
        for i in range(10):
            tm.body = {'number': i}
            M1.put(tm)
        M1.send()

        for i in range(10):
            M2.recv(1)
            M2.get(rm)
            self.assertEqual(i, rm.body['number'])
            da = rm.instructions
            self.assertEqual(da.__class__, dict)
            self.assertEqual(da['x-opt-qd.ingress'], 'ingress-router')
            self.assertEqual(da['x-opt-qd.trace'], ['0/QDR'])

        ##
        ## Invalid trace type
        ##
        tm.instructions = {'x-opt-qd.trace' : 45}
        for i in range(10):
            tm.body = {'number': i}
            M1.put(tm)
        M1.send()

        for i in range(10):
            M2.recv(1)
            M2.get(rm)
            self.assertEqual(i, rm.body['number'])
            da = rm.instructions
            self.assertEqual(da.__class__, dict)
            self.assertEqual(da['x-opt-qd.ingress'], '0/QDR')
            self.assertEqual(da['x-opt-qd.trace'], ['0/QDR'])

        ##
        ## Empty trace
        ##
        tm.instructions = {'x-opt-qd.trace' : []}
        for i in range(10):
            tm.body = {'number': i}
            M1.put(tm)
        M1.send()

        for i in range(10):
            M2.recv(1)
            M2.get(rm)
            self.assertEqual(i, rm.body['number'])
            da = rm.instructions
            self.assertEqual(da.__class__, dict)
            self.assertEqual(da['x-opt-qd.ingress'], '0/QDR')
            self.assertEqual(da['x-opt-qd.trace'], ['0/QDR'])

        ##
        ## Non-empty trace
        ##
        tm.instructions = {'x-opt-qd.trace' : ['0/first.hop']}
        for i in range(10):
            tm.body = {'number': i}
            M1.put(tm)
        M1.send()

        for i in range(10):
            M2.recv(1)
            M2.get(rm)
            self.assertEqual(i, rm.body['number'])
            da = rm.instructions
            self.assertEqual(da.__class__, dict)
            self.assertEqual(da['x-opt-qd.ingress'], '0/QDR')
            self.assertEqual(da['x-opt-qd.trace'], ['0/first.hop', '0/QDR'])

        M1.stop()
        M2.stop()


    def test_09_management(self):
        addr  = "amqp:/$management"

        M = Messenger()
        M.timeout = 2.0
        M.start()
        M.route("amqp:/*", "amqp://0.0.0.0:20000/$1")
        sub = self.subscribe(M, "amqp:/#")
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

        self.assertEqual(response.properties['statusCode'], 200)
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

    def test_09a_management_get_types(self):
        addr  = "amqp:/_local/$management"

        M = Messenger()
        M.timeout = 2.0
        M.start()
        M.route("amqp:/*", "amqp://0.0.0.0:20000/$1")
        sub = self.subscribe(M, "amqp:/#")
        reply = sub.address

        request  = Message()
        response = Message()

        ##
        ## Unrestricted request
        ##
        request.address    = addr
        request.reply_to   = reply
        request.properties = {u'type':u'org.amqp.management', u'name':u'self', u'operation':u'GET-TYPES'}

        M.put(request)
        M.send()
        M.recv()
        M.get(response)

        self.assertEqual(response.properties['statusCode'], 200)
        self.assertEqual(response.body.__class__, dict)
        self.assertTrue('org.apache.qpid.dispatch.router' in response.body.keys())
        self.assertTrue(len(response.body.keys()) > 2)

        ##
        ## Restricted Request one match
        ##
        request.address    = addr
        request.reply_to   = reply
        request.properties = {u'type':u'org.amqp.management', u'name':u'self', u'operation':u'GET-TYPES',
                              u'entityType':'org.apache.qpid.dispatch.connection'}

        M.put(request)
        M.send()
        M.recv()
        M.get(response)

        self.assertEqual(response.properties['statusCode'], 200)
        self.assertEqual(response.body.__class__, dict)
        self.assertTrue('org.apache.qpid.dispatch.connection' in response.body.keys())
        self.assertEqual(len(response.body.keys()), 1)

        ##
        ## Restricted Request with no matches
        ##
        request.address    = addr
        request.reply_to   = reply
        request.properties = {u'type':u'org.amqp.management', u'name':u'self', u'operation':u'GET-TYPES',
                              u'entityType':'com.profitron.item'}

        M.put(request)
        M.send()
        M.recv()
        M.get(response)

        self.assertEqual(response.properties['statusCode'], 200)
        self.assertEqual(response.body, {})

        ##
        ## Error Request
        ##
        request.address    = addr
        request.reply_to   = reply
        request.properties = {u'type':u'org.amqp.management', u'name':u'self', u'operation':u'GET-TYPES',
                              u'entityType':['one', 'two']}

        M.put(request)
        M.send()
        M.recv()
        M.get(response)

        self.assertEqual(response.properties['statusCode'], 400)

        M.stop()


    def test_09b_management_get_attributes(self):
        addr  = "amqp:/_local/$management"

        M = Messenger()
        M.timeout = 2.0
        M.start()
        M.route("amqp:/*", "amqp://0.0.0.0:20000/$1")
        sub = self.subscribe(M, "amqp:/#")
        reply = sub.address

        request  = Message()
        response = Message()

        ##
        ## Unrestricted request
        ##
        request.address    = addr
        request.reply_to   = reply
        request.properties = {u'type':u'org.amqp.management', u'name':u'self', u'operation':u'GET-ATTRIBUTES'}

        M.put(request)
        M.send()
        M.recv()
        M.get(response)

        self.assertEqual(response.properties['statusCode'], 200)
        self.assertEqual(response.body.__class__, dict)
        self.assertTrue('org.apache.qpid.dispatch.router' in response.body.keys())
        self.assertTrue(len(response.body.keys()) > 2)
        self.assertTrue(response.body['org.apache.qpid.dispatch.router'].__class__, list)

        ##
        ## Restricted Request with a match
        ##
        request.address    = addr
        request.reply_to   = reply
        request.properties = {u'type':u'org.amqp.management', u'name':u'self', u'operation':u'GET-ATTRIBUTES',
                              u'entityType':'org.apache.qpid.dispatch.router'}

        M.put(request)
        M.send()
        M.recv()
        M.get(response)

        self.assertEqual(response.properties['statusCode'], 200)
        self.assertEqual(response.body.__class__, dict)
        self.assertTrue('org.apache.qpid.dispatch.router' in response.body.keys())
        self.assertEqual(len(response.body.keys()), 1)
        self.assertTrue('mode' in response.body['org.apache.qpid.dispatch.router'])

        ##
        ## Restricted Request with no matches
        ##
        request.address    = addr
        request.reply_to   = reply
        request.properties = {u'type':u'org.amqp.management', u'name':u'self', u'operation':u'GET-ATTRIBUTES',
                              u'entityType':'com.profitron.item'}

        M.put(request)
        M.send()
        M.recv()
        M.get(response)

        self.assertEqual(response.properties['statusCode'], 200)
        self.assertEqual(response.body, {})

        M.stop()


    def test_09c_management_get_operations(self):
        addr  = "amqp:/_local/$management"

        M = Messenger()
        M.timeout = 2.0
        M.start()
        M.route("amqp:/*", "amqp://0.0.0.0:20000/$1")
        sub = self.subscribe(M, "amqp:/#")
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

        M = Messenger()
        M.timeout = 2.0
        M.start()
        M.route("amqp:/*", "amqp://0.0.0.0:20000/$1")
        sub = self.subscribe(M, "amqp:/#")
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
        addr = "amqp://0.0.0.0:20000/multicast/1"
        M1 = Messenger()
        M2 = Messenger()
        M3 = Messenger()
        M4 = Messenger()

        M1.timeout = 1.0
        M2.timeout = 1.0
        M3.timeout = 1.0
        M4.timeout = 1.0

        M1.start()
        M2.start()
        M3.start()
        M4.start()

        self.subscribe(M2, addr)
        self.subscribe(M3, addr)
        self.subscribe(M4, addr)

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
        addr = "amqp://0.0.0.0:20000/closest/1"
        M1 = Messenger()
        M2 = Messenger()
        M3 = Messenger()
        M4 = Messenger()

        M1.timeout = 1.0
        M2.timeout = 1.0
        M3.timeout = 1.0
        M4.timeout = 1.0

        M1.start()
        M2.start()
        M3.start()
        M4.start()

        self.subscribe(M2, addr)
        self.subscribe(M3, addr)
        self.subscribe(M4, addr)

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
        addr = "amqp://0.0.0.0:20000/spread/1"
        M1 = Messenger()
        M2 = Messenger()
        M3 = Messenger()
        M4 = Messenger()

        M1.timeout = 1.0
        M2.timeout = 1.0
        M3.timeout = 1.0
        M4.timeout = 1.0

        M1.start()
        M2.start()
        M3.start()
        M4.start()

        self.subscribe(M2, addr)
        self.subscribe(M3, addr)
        self.subscribe(M4, addr)

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


if __name__ == '__main__':
    unittest.main()

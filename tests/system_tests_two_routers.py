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

def wait_for_addr(messenger, addr, local_count, remote_count):
    msub  = messenger.subscribe("amqp:/#")
    reply = msub.address
    req   = Message()
    rsp   = Message()

    done = False
    while not done:
        req.address    = "amqp:/_local/$management"
        req.reply_to   = reply
        req.properties = {u'operation':u'QUERY', u'entityType':u'org.apache.qpid.dispatch.router.address'}
        req.body       = {u'attributeNames': [u'name', u'subscriberCount', u'remoteCount']}
        messenger.put(req)
        messenger.send()
        messenger.recv()
        messenger.get(rsp)
        for item in rsp.body[u'results']:
            if item[0][2:] == addr and \
               local_count == item[1] and \
               remote_count == item[2]:
                done = True
        time.sleep(0.2)

def wait_for_routethrough(messenger, addr):
    msub  = messenger.subscribe("amqp:/#")
    reply = msub.address
    req   = Message()
    rsp   = Message()

    done = False
    while not done:
        req.address    = "amqp:/_topo/0/%s/$management" % addr
        req.reply_to   = reply
        req.properties = {u'operation':u'GET-OPERATIONS', u'type':u'org.amqp.management', u'name':u'self'}
        messenger.put(req)
        messenger.send()
        try:
            messenger.recv()
            done = True
        except Exception:
            pass
        time.sleep(0.2)

def startRouter(obj):
    default_home = os.path.normpath('/usr/lib/qpid-dispatch')
    home = os.environ.get('QPID_DISPATCH_HOME', default_home)
    if obj.ssl_option == "ssl":
        configA_file = '%s/tests/config-2/A-ssl.conf' % home
        configB_file = '%s/tests/config-2/B-ssl.conf' % home 
    else:
        configA_file = '%s/tests/config-2/A.conf' % home
        configB_file = '%s/tests/config-2/B.conf' % home

    obj.routerA = subprocess.Popen(['qdrouterd', '-c', configA_file],
                                   stderr=subprocess.PIPE,
                                   stdout=subprocess.PIPE)
    obj.routerB = subprocess.Popen(['qdrouterd', '-c', configB_file],
                                   stderr=subprocess.PIPE,
                                   stdout=subprocess.PIPE)
    time.sleep(1)
    print "Waiting for router topology to stabilize..."

    M1 = Messenger()
    M2 = Messenger()

    M1.route("amqp:/*", "amqp://0.0.0.0:20100/$1")
    M2.route("amqp:/*", "amqp://0.0.0.0:20101/$1")

    M1.start()
    M2.start()

    M1.timeout = 0.5
    M2.timeout = 0.5

    wait_for_routethrough(M1, "QDR.B")
    wait_for_routethrough(M2, "QDR.A")

    M1.stop()
    M2.stop()


def stopRouter(obj):
    obj.routerA.terminate()
    obj.routerB.terminate()
    obj.routerA.wait()
    obj.routerB.wait()


class RouterTest(unittest.TestCase):
    ssl_option = None

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
        addr = "amqp://0.0.0.0:20100/discard/1"
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
        addr = "amqp:/pre_settled/1"
        M1 = Messenger()
        M2 = Messenger()

        M1.route("amqp:/*", "amqp://0.0.0.0:20100/$1")
        M2.route("amqp:/*", "amqp://0.0.0.0:20101/$1")

        M1.timeout = 1.0
        M2.timeout = 1.0

        M1.start()
        M2.start()
        self.subscribe(M2, addr)

        tm = Message()
        rm = Message()

        wait_for_addr(M1, "pre_settled/1", 0, 1)

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
        M1 = Messenger()
        M2 = Messenger()
        M3 = Messenger()
        M4 = Messenger()

        M1.route("amqp:/*", "amqp://0.0.0.0:20100/$1")
        M2.route("amqp:/*", "amqp://0.0.0.0:20101/$1")
        M3.route("amqp:/*", "amqp://0.0.0.0:20100/$1")
        M4.route("amqp:/*", "amqp://0.0.0.0:20101/$1")

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
        wait_for_addr(M1, "pre_settled/multicast/1", 1, 1)

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
        M1 = Messenger()
        M2 = Messenger()
        M3 = Messenger()
        M4 = Messenger()

        M1.route("amqp:/*", "amqp://0.0.0.0:20100/$1")
        M2.route("amqp:/*", "amqp://0.0.0.0:20101/$1")
        M3.route("amqp:/*", "amqp://0.0.0.0:20100/$1")
        M4.route("amqp:/*", "amqp://0.0.0.0:20101/$1")

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
        wait_for_addr(M1, "pre_settled/multicast/2", 1, 1)

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
        M1 = Messenger()
        M2 = Messenger()

        M1.route("amqp:/*", "amqp://0.0.0.0:20100/$1")
        M2.route("amqp:/*", "amqp://0.0.0.0:20101/$1")

        M1.timeout = 1.0
        M2.timeout = 1.0

        M1.outgoing_window = 5
        M2.incoming_window = 5

        M1.start()
        M2.start()
        self.subscribe(M2, addr)
        wait_for_addr(M1, "settled/senderfirst/1", 0, 1)

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
        addr = "amqp:/unsettled/2"
        M1 = Messenger()
        M2 = Messenger()

        M1.route("amqp:/*", "amqp://0.0.0.0:20100/$1")
        M2.route("amqp:/*", "amqp://0.0.0.0:20101/$1")

        M1.timeout = 1.0
        M2.timeout = 1.0

        M1.outgoing_window = 5
        M2.incoming_window = 5

        M1.start()
        M2.start()
        self.subscribe(M2, addr)
        wait_for_addr(M1, "unsettled/2", 0, 1)

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
        addr = "amqp://0.0.0.0:20100/unsettled_undeliverable/1"
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
        addr = "amqp:/three_ack/1"
        M1 = Messenger()
        M2 = Messenger()

        M1.route("amqp:/*", "amqp://0.0.0.0:20100/$1")
        M2.route("amqp:/*", "amqp://0.0.0.0:20101/$1")

        M1.timeout = 1.0
        M2.timeout = 1.0

        M1.outgoing_window = 5
        M2.incoming_window = 5

        M1.start()
        M2.start()
        self.subscribe(M2, addr)
        wait_for_addr(M1, "three_ack/1", 0, 1)

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


    def notest_06_link_route_sender(self):
        pass 

    def notest_07_link_route_receiver(self):
        pass 


    def test_08_delivery_annotations(self):
        addr = "amqp:/ma/1"
        M1 = Messenger()
        M2 = Messenger()

        M1.route("amqp:/*", "amqp://0.0.0.0:20100/$1")
        M2.route("amqp:/*", "amqp://0.0.0.0:20101/$1")

        M1.timeout = 1.0
        M2.timeout = 1.0

        M1.start()
        M2.start()
        self.subscribe(M2, addr)
        wait_for_addr(M1, "ma/1", 0, 1)

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
        M = Messenger()
        M.timeout = 2.0
        M.start()
        M.route("amqp:/*", "amqp://0.0.0.0:20100/$1")
        sub = self.subscribe(M, "amqp:/#")
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

        self.assertEqual(response.properties['statusCode'], 200)
        self.assertTrue('amqp:/_topo/0/QDR.B/$management' in response.body)

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
        M1 = Messenger()
        M2 = Messenger()
        M3 = Messenger()
        M4 = Messenger()

        M1.route("amqp:/*", "amqp://0.0.0.0:20100/$1")
        M2.route("amqp:/*", "amqp://0.0.0.0:20101/$1")
        M3.route("amqp:/*", "amqp://0.0.0.0:20100/$1")
        M4.route("amqp:/*", "amqp://0.0.0.0:20101/$1")

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
        wait_for_addr(M1, "multicast/1", 1, 1)

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
        M1 = Messenger()
        M2 = Messenger()
        M3 = Messenger()
        M4 = Messenger()

        M1.route("amqp:/*", "amqp://0.0.0.0:20100/$1")
        M2.route("amqp:/*", "amqp://0.0.0.0:20101/$1")
        M3.route("amqp:/*", "amqp://0.0.0.0:20100/$1")
        M4.route("amqp:/*", "amqp://0.0.0.0:20101/$1")

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
        wait_for_addr(M1, "closest/1", 1, 1)

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
        M1 = Messenger()
        M2 = Messenger()
        M3 = Messenger()
        M4 = Messenger()

        M1.route("amqp:/*", "amqp://0.0.0.0:20100/$1")
        M2.route("amqp:/*", "amqp://0.0.0.0:20101/$1")
        M3.route("amqp:/*", "amqp://0.0.0.0:20100/$1")
        M4.route("amqp:/*", "amqp://0.0.0.0:20101/$1")

        M1.timeout = 1.0
        M2.timeout = 1.0
        M3.timeout = 1.0
        M4.timeout = 1.0

        M1.start()
        M2.start()
        M3.start()
        M4.start()

        self.subscribe(M2, addr)
        self.subscribe(M4, addr)
        wait_for_addr(M1, "closest/2", 0, 1)

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
        M1 = Messenger()
        M2 = Messenger()
        M3 = Messenger()
        M4 = Messenger()

        M1.route("amqp:/*", "amqp://0.0.0.0:20100/$1")
        M2.route("amqp:/*", "amqp://0.0.0.0:20101/$1")
        M3.route("amqp:/*", "amqp://0.0.0.0:20100/$1")
        M4.route("amqp:/*", "amqp://0.0.0.0:20101/$1")

        M1.timeout = 1.0
        M2.timeout = 0.2
        M3.timeout = 0.2
        M4.timeout = 0.2

        M1.start()
        M2.start()
        M3.start()
        M4.start()
        self.subscribe(M2, addr)
        self.subscribe(M3, addr)
        self.subscribe(M4, addr)
        wait_for_addr(M1, "spread/1", 1, 1)

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
        M1 = Messenger()
        M2 = Messenger()

        M1.route("amqp:/*", "amqp://0.0.0.0:20100/$1")
        M2.route("amqp:/*", "amqp://0.0.0.0:20101/$1")

        M1.timeout = 1.0
        M2.timeout = 1.0

        M1.start()
        M2.start()
        self.subscribe(M2, addr)
        wait_for_addr(M1, "toov/1", 0, 1)

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
    if len(sys.argv) > 1:
        if '--ssl' in sys.argv:
            sys.argv.remove('--ssl')
            RouterTest.ssl_option = "ssl"
            print "...Using SSL configuration"
        else:
            print "...Using non-SSL configuration"
    unittest.main()

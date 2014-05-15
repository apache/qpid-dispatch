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

"""
System tests involving one or more brokers and dispatch routers integrated
with waypoints.
"""
import unittest, system_test
from system_test import wait_port, wait_ports, Qdrouterd, retry, message, MISSING_REQUIREMENTS

class BrokerSystemTest(system_test.TestCase): # pylint: disable=too-many-public-methods
    """System tests involving routers and qpidd brokers"""

    # Hack for python 2.6 which does not support setupClass.
    # We set setup_ok = true in setupClass, and skip all tests if it's not true.
    setup_ok = False

    @classmethod
    def setUpClass(cls):
        """Start 3 qpidd brokers, wait for them to be ready."""
        super(BrokerSystemTest, cls).setUpClass()
        cls.qpidd = [cls.tester.qpidd('qpidd%s'%i, port=cls.get_port())
                    for i in xrange(3)]
        for q in cls.qpidd:
            wait_port(q.port)
        cls.setup_ok = True

    @classmethod
    def tearDownClass(cls):
        if cls.setup_ok:
            cls.setup_ok = False
            super(BrokerSystemTest, cls).tearDownClass()

    def test_distrbuted_queue(self):
        """Static distributed queue, one router, three brokers"""
        if not self.setup_ok:
            return self.skipTest("setUpClass failed")
        testq = self.id()       # The distributed queue name
        for q in self.qpidd:
            q.agent.addQueue(testq)

        # Start a qdrouterd
        # We have a waypoint for each broker, on the same testq address.
        # Sending to testq should spread messages to the qpidd queues.
        # Subscribing to testq should gather messages from the qpidd queues.
        router_conf = Qdrouterd.Config([
            ('log', {'module':'DEFAULT', 'level':'NOTICE'}),
            ('log', {'module':'ROUTER', 'level':'TRACE'}),
            ('log', {'module':'MESSAGE', 'level':'TRACE'}),
            ('container', {'container-name':self.id()}),
            ('container', {'container-name':self.id()}),
            ('router', {'mode': 'standalone', 'router-id': self.id()}),
            ('listener', {'addr':'0.0.0.0', 'port':self.get_port()}),
            ('fixed-address', {'prefix':testq, 'phase':0, 'fanout':'single', 'bias':'spread'}),
            ('fixed-address', {'prefix':testq, 'phase':1, 'fanout':'single', 'bias':'spread'})
        ])
        # Add connector and waypoint for each broker.
        for q in self.qpidd:
            router_conf += [
                ('connector', {'name':q.name, 'addr':'0.0.0.0', 'port':q.port}),
                ('waypoint', {'name':testq, 'out-phase':1, 'in-phase':0, 'connector':q.name})]

        router = self.qdrouterd('router0', router_conf)
        wait_ports(router.ports)
        for q in self.qpidd:
            retry(lambda: router.is_connected(q.port))

        msgr = self.messenger()

        address = router.addresses[0]+"/"+testq
        msgr.subscribe(address, flush=True)
        n = 20                  # Messages per broker
        r = range(n*len(self.qpidd))
        for i in r:
            msgr.put(message(address=address, body=i))
        messages = sorted(msgr.fetch().body for i in r)
        msgr.flush()
        self.assertEqual(messages, r)
        # Verify we got back exactly what we sent.
        qs = [q.agent.getQueue(testq) for q in self.qpidd]
        enq = sum(q.msgTotalEnqueues for q in qs)
        deq = sum(q.msgTotalDequeues for q in qs)
        self.assertEquals((enq, deq), (len(r), len(r)))
        # Verify the messages were spread equally over the brokers.
        self.assertEquals(
            [(q.msgTotalEnqueues, q.msgTotalDequeues) for q in qs],
            [(n, n) for q in qs]
        )

if __name__ == '__main__':
    if MISSING_REQUIREMENTS:
        print MISSING_REQUIREMENTS
    else:
        unittest.main()

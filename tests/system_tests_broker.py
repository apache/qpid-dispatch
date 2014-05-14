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

"""System tests involving one or more brokers and dispatch routers

FIXME aconway 2014-04-29:

These tests is a work in progress, they do not pass
and they are not run by the qdtest script.

They are provided as an example of how to use the system_test module.

To run the tests from a dispatch checkout:
 . config.sh; python tests/system_tests_broker.py
Note the tests wil 
"""

from system_test import *

class BrokerSystemTest(TestCase):

    def test_broker(self):
        testq = 'testq'

        # Start two qpidd brokers called qpidd0 and qpidd1
        qpidd = [
            self.qpidd('qpidd%s'%i,
                       Qpidd.Config({'port':self.get_port(), 'trace':1}))
                  for i in xrange(2) ]

        # FIXME aconway 2014-05-13: router waypoint connection seems fragile
        # unless everything is set up beforehand.
        wait_ports([q.port for q in qpidd])
        qpidd[0].agent.addQueue(testq)

        # Start a qdrouterd
        router_conf = Qdrouterd.Config([
            ('log', { 'module':'DEFAULT', 'level':'NOTICE' }),
            ('log', { 'module':'ROUTER', 'level':'TRACE' }),
            ('log', { 'module':'MESSAGE', 'level':'TRACE' }),
            ('container', {'container-name':self.id()}),
            ('container', {'container-name':self.id()}),
            ('router', { 'mode': 'standalone', 'router-id': self.id() }),
            ('listener', {'addr':'0.0.0.0', 'port':self.get_port()}),
            ('connector', {'name':'qpidd0', 'addr':'0.0.0.0', 'port':qpidd[0].port}),
            ('connector', {'name':'qpidd1', 'addr':'0.0.0.0', 'port':qpidd[1].port}),
            ('fixed-address', {'prefix':'testq', 'phase':0, 'fanout':'single', 'bias':'closest'}),
            ('fixed-address', {'prefix':'testq', 'phase':1, 'fanout':'single', 'bias':'closest'}),
            ('waypoint', {'name':'testq', 'out-phase':1, 'in-phase':0, 'connector':'qpidd0'})
        ])
        router = self.qdrouterd('router0', router_conf)
        wait_ports(router.ports)
        retry(lambda: router.is_connected(qpidd[0].port))

        # Test for waypoint routing via queue
        m=self.message(address=router.addresses[0]+"/"+testq, body="FOO")
        msgr = self.messenger()
        msgr.subscribe(m.address)
        msgr.put(m)
        msgr.send()
        msg = Message()
        msgr.recv(1)
        msgr.get(msg)
        msgr.accept()
        msgr.flush()
        self.assertEqual(msg.body, m.body)
        aq = qpidd[0].agent.getQueue(testq)
        self.assertEquals((aq.msgTotalEnqueues, aq.msgTotalDequeues), (1,1))

if __name__ == '__main__': unittest.main()

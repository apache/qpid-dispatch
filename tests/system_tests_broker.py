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

        # Start a qdrouterd
        router_conf = Qdrouterd.Config([
            ('log', { 'module':'DEFAULT', 'level':'TRACE' }),
            ('container', {'container-name':self.id()}),
            ('container', {'container-name':self.id()}),
            ('router', { 'mode': 'standalone', 'router-id': self.id() }),
            ('listener', {'addr':'0.0.0.0', 'port':self.get_port()}),
            ('connector', {'name':'qpidd0', 'addr':'localhost', 'port':qpidd[0].port}),
            ('connector', {'name':'qpidd1', 'addr':'localhost', 'port':qpidd[1].port}),
            ('fixed-address', {'prefix':'/testme/', 'fanout':'multiple'}),
            ('waypoint', {'name':testq, 'out-phase':1, 'connector':'qpidd0'})
        ])
        router = self.qdrouterd('router0', router_conf)

        # Wait for broker & router to be ready
        wait_ports([q.port for q in qpidd] + router.ports)

        # Smoke test for qpidd
        qc = self.cleanup(qm.Connection.establish(qpidd[0].address))
        qc.session().sender(testq+";{create:always}").send("a")
        qr = qc.session().receiver(testq)
        self.assertEqual(qr.fetch(1).content, "a")

        # Smoke test for dispatch.
        addr = router.addresses[0]+"/xxx/1"
        m1, m2 = self.messenger(), self.messenger()
        m2.subscribe(addr)
        m1.put(self.message(address=addr, body="b"))
        m1.send()
        msg = Message()
        m2.recv(1)
        m2.get(msg)
        self.assertEqual(msg.body, "b")

        # FIXME aconway 2014-05-05: test for waypoint routing via queue

if __name__ == '__main__': unittest.main()

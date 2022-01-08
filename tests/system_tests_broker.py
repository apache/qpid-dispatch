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

from itertools import cycle

import system_test
from system_test import Qdrouterd, message, MISSING_REQUIREMENTS
from system_test import unittest


class DistributedQueueTest(system_test.TestCase):  # pylint: disable=too-many-public-methods
    """System tests involving routers and qpidd brokers"""

    if MISSING_REQUIREMENTS:
        def test_skip(self):
            self.skipTest(MISSING_REQUIREMENTS)
    else:
        @classmethod
        def setUpClass(cls):
            """Start 3 qpidd brokers, wait for them to be ready."""
            super(DistributedQueueTest, cls).setUpClass()
            cls.qpidds = [cls.tester.qpidd('qpidd%s' % i, port=cls.get_port(), wait=False)
                          for i in range(3)]
            for q in cls.qpidds:
                q.wait_ready()

        @classmethod
        def tearDownClass(cls):
            super(DistributedQueueTest, cls).tearDownClass()

        def setUp(self):
            super(DistributedQueueTest, self).setUp()
            self.testq = 'testq.' + self.id().split('.')[-1]  # The distributed queue name

        def common_router_conf(self, name, mode='standalone'):
            """Common router configuration for the tests"""
            return Qdrouterd.Config([
                ('router', {'mode': mode, 'id': name})
            ])

        def verify_equal_spread(self, send_addresses, receive_addresses):
            """Verify we send/receive to the queue the load was spread over the brokers.
            Send to each of the send_addresses in turn, subscribe to all of the receive_addresses.
            """
            msgr = self.messenger()
            for a in receive_addresses:
                msgr.subscribe(a)
            msgr.flush()
            n = 20                  # Messages per broker
            r = ["x-%02d" % i for i in range(n * len(self.qpidds))]
            for b, a in zip(r, cycle(send_addresses)):
                msgr.put(message(address=a, body=b))
            msgr.flush()
            messages = sorted(msgr.fetch().body for i in r)
            msgr.flush()
            self.assertEqual(r, messages)

            qs = [q.management.getQueue(self.testq) for q in self.qpidds]
            enq = sum(q.msgTotalEnqueues for q in qs)
            deq = sum(q.msgTotalDequeues for q in qs)
            self.assertEqual((enq, deq), (len(r), len(r)))
            # Verify each broker handled a reasonable share of the messages.
            self.assert_fair([q.msgTotalEnqueues for q in qs])

        def test_distrbuted_queue(self):
            """Create a distributed queue with N routers and N brokers.
            Each router is connected to all the brokers."""
            for q in self.qpidds:
                q.management.addQueue(self.testq)

            def router(i):
                """Create router<i> with waypoints to each broker."""
                name = "router%s" % i
                rconf = self.common_router_conf(name, mode='interior')
                rconf += [
                    ('listener', {'port': self.get_port(), 'role': 'normal'}),
                ]
                for q in self.qpidds:
                    rconf += [
                        ('connector', {'name': q.name, 'port': q.port})]
                return self.qdrouterd(name, rconf, wait=False)
            routers = [router(i) for i in range(len(self.qpidds))]
            for r in routers:
                r.wait_ready()
            addrs = [r.addresses[0] + "/" + self.testq for r in routers]
            self.verify_equal_spread(addrs, addrs)


if __name__ == '__main__':
    unittest.main(system_test.main_module())

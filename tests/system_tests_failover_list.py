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

from system_test import TestCase, Qdrouterd, main_module, TIMEOUT, TestTimeout
from system_test import unittest
from proton.handlers import MessagingHandler
from proton.reactor import Container


class RouterTest(TestCase):

    @classmethod
    def setUpClass(cls):
        """Start a router"""
        super(RouterTest, cls).setUpClass()

        def router(name):

            config = [
                ('router', {'mode': 'standalone', 'id': name}),
                ('listener', {'port': cls.tester.get_port()}),
                # failoverList has been deprecated. We are using it here to test backward compatibility.
                ('listener', {'port': cls.tester.get_port(), 'failoverList': 'other-host:25000'}),
                ('listener', {'port': cls.tester.get_port(), 'failoverUrls': 'second-host:25000, amqps://third-host:5671'})
            ]

            config = Qdrouterd.Config(config)

            cls.routers.append(cls.tester.qdrouterd(name, config, wait=True))

        cls.routers = []

        router('A')
        cls.routers[0].wait_ready()

    def test_01_no_failover_list(self):
        test = FailoverTest(self.routers[0].addresses[0], 0)
        test.run()
        self.assertIsNone(test.error)

    def test_02_single_failover_host(self):
        test = FailoverTest(self.routers[0].addresses[1], 1, [{'network-host': 'other-host', 'port': '25000'}])
        test.run()
        self.assertIsNone(test.error)

    def test_03_double_failover_host(self):
        test = FailoverTest(self.routers[0].addresses[2], 2,
                            [{'network-host': 'second-host', 'port': '25000'}, {'scheme': 'amqps', 'network-host': 'third-host', 'port': '5671'}])
        test.run()
        self.assertIsNone(test.error)


class FailoverTest(MessagingHandler):

    def __init__(self, host, count, elements=None):
        super(FailoverTest, self).__init__()
        self.host     = host
        self.count    = count
        self.elements = elements or []
        self.conn     = None
        self.error    = None

    def timeout(self):
        self.error = "Timeout Expired"
        self.conn.close()

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.conn  = event.container.connect(self.host)

    def on_connection_opened(self, event):
        properties = event.connection.remote_properties
        fol = None
        try:
            fol = properties['failover-server-list']
        except:
            fol = None

        if self.count == 0:
            if fol is not None and fol != []:
                self.error = "Expected no failover-list, got: %r" % fol

        elif fol.__class__ != list:
            self.error = "Expected list, got: %r" % fol.__class__

        elif self.count != len(fol):
            self.error = "Expected list of size %d, got size %d" % (self.count, len(fol))

        for i in range(self.count):
            got  = fol[i]
            want = self.elements[i]
            if got != want:
                self.error = "Expected %r, got %r" % (want, got)

        self.timer.cancel()
        self.conn.close()

    def run(self):
        Container(self).run()


if __name__ == '__main__':
    unittest.main(main_module())

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

from system_test import TestCase, Qdrouterd, main_module, TIMEOUT, unittest, TestTimeout
from proton.handlers import MessagingHandler
from proton.reactor import Container


class RouterTest(TestCase):

    inter_router_port = None

    @classmethod
    def setUpClass(cls):
        """Start a router"""
        super(RouterTest, cls).setUpClass()

        def router(name, connection):

            config = [
                ('router', {'mode': 'interior', 'id': name}),
                ('listener', {'port': cls.tester.get_port()}),
                connection
            ]

            config = Qdrouterd.Config(config)

            cls.routers.append(cls.tester.qdrouterd(name, config, wait=True, cl_args=["-T"]))

        cls.routers = []

        inter_router_port = cls.tester.get_port()

        router('A', ('listener', {'role': 'inter-router', 'port': inter_router_port}))
        router('B', ('connector', {'name': 'connectorToA', 'role': 'inter-router', 'port': inter_router_port}))

        cls.routers[0].wait_router_connected('B')
        cls.routers[1].wait_router_connected('A')

    def test_address_watch(self):
        test = AddressWatchTest(self.routers[0], self.routers[1])
        test.run()
        self.assertIsNone(test.error)


class AddressWatchTest(MessagingHandler):
    def __init__(self, host_a, host_b):
        super(AddressWatchTest, self).__init__()
        self.host_a = host_a
        self.host_b = host_b
        self.addr     = 'addr_watch/test_address/1'
        self.conn_a   = None
        self.conn_b   = None
        self.error    = None
        self.sender   = None
        self.receiver = None
        self.n_closed = 0

    def timeout(self):
        self.error = "Timeout Expired"
        if self.conn_a:
            self.conn_a.close()
        if self.conn_b:
            self.conn_b.close()

    def on_start(self, event):
        self.timer    = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.conn_a   = event.container.connect(self.host_a.addresses[0])
        self.conn_b   = event.container.connect(self.host_b.addresses[0])
        self.receiver = event.container.create_receiver(self.conn_a, self.addr)
        self.sender   = event.container.create_sender(self.conn_b, self.addr)

    def on_sendable(self, event):
        if event.sender == self.sender:
            self.conn_a.close()
            self.conn_b.close()

    def on_connection_closed(self, event):
        self.n_closed += 1
        if self.n_closed == 2:
            with open(self.host_a.logfile_path, 'r') as router_log:
                log_lines = router_log.read().split("\n")
                search_lines = [s for s in log_lines if "ADDRESS_WATCH" in s and "on_watch" in s]
                matches = [s for s in search_lines if "loc: 1 rem: 0 prod: 0" in s]
                if len(matches) == 0:
                    self.error = "Didn't see local consumer on router A"
            with open(self.host_b.logfile_path, 'r') as router_log:
                log_lines = router_log.read().split("\n")
                search_lines = [s for s in log_lines if "ADDRESS_WATCH" in s and "on_watch" in s]
                matches = [s for s in search_lines if "loc: 0 rem: 1 prod: 1" in s]
                if len(matches) == 0:
                    self.error = "Didn't see remote consumer and local producer on router B"
            self.timer.cancel()

    def run(self):
        Container(self).run()


if __name__ == '__main__':
    unittest.main(main_module())

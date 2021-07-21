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

from proton import Message, symbol, int32
from system_test import TestCase, Qdrouterd, main_module, TIMEOUT
from system_test import unittest, TestTimeout
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
                ('listener', {'port': cls.tester.get_port(), 'stripAnnotations': 'no'}),
                ('listener', {'port': cls.tester.get_port(), 'stripAnnotations': 'no', 'multiTenant': 'yes'}),
                ('listener', {'port': cls.tester.get_port(), 'stripAnnotations': 'no', 'role': 'route-container'}),
                connection
            ]

            config = Qdrouterd.Config(config)

            cls.routers.append(cls.tester.qdrouterd(name, config, wait=True))

        cls.routers = []

        inter_router_port = cls.tester.get_port()

        router('A', ('listener', {'role': 'inter-router', 'port': inter_router_port}))
        router('B', ('connector', {'name': 'connectorToA', 'role': 'inter-router', 'port': inter_router_port}))

        cls.routers[0].wait_router_connected('B')
        cls.routers[1].wait_router_connected('A')

    def test_01_reject_higher_version_hello(self):
        test = RejectHigherVersionHelloTest(self.routers[0].addresses[3])
        test.run()
        self.assertIsNone(test.error)

    def test_02_reject_higher_version_mar(self):
        test = RejectHigherVersionMARTest(self.routers[0].addresses[3], self.routers[0].addresses[0])
        test.run()
        self.assertIsNone(test.error)


class RejectHigherVersionHelloTest(MessagingHandler):
    def __init__(self, host):
        super(RejectHigherVersionHelloTest, self).__init__()
        self.host     = host
        self.conn     = None
        self.error    = None
        self.sender   = None
        self.receiver = None
        self.hello_count = 0

    def timeout(self):
        self.error = "Timeout Expired - hello_count: %d" % self.hello_count
        self.conn.close()

    def fail(self, error):
        self.error = error
        self.conn.close()
        self.timer.cancel()

    def on_start(self, event):
        self.timer    = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.conn     = event.container.connect(self.host)
        self.receiver = event.container.create_receiver(self.conn)
        self.sender   = event.container.create_sender(self.conn)

        self.receiver.source.capabilities.put_object(symbol("qd.router"))
        self.receiver.target.capabilities.put_object(symbol("qd.router"))
        self.sender.source.capabilities.put_object(symbol("qd.router"))
        self.sender.target.capabilities.put_object(symbol("qd.router"))

    def on_message(self, event):
        opcode = event.message.properties['opcode']
        body   = event.message.body

        if opcode == 'HELLO':
            rid = body['id']
            self.hello_count += 1
            if self.hello_count > 2:
                self.fail(None)
                return

            if 'TEST' in body['seen']:
                self.fail('Version 2 HELLO message improperly accepted by router')
                return

            hello = Message(address='_local/qdhello',
                            properties={'opcode': 'HELLO'},
                            body={'id': 'TEST', 'pv': int32(2), 'area': '0', 'instance': 100, 'seen': [rid]})
            dlv = self.sender.send(hello)
            dlv.settle()

    def run(self):
        Container(self).run()


class RejectHigherVersionMARTest(MessagingHandler):
    def __init__(self, host, normal_host):
        super(RejectHigherVersionMARTest, self).__init__()
        self.host        = host
        self.normal_host = normal_host
        self.conn        = None
        self.normal_conn = None
        self.error       = None
        self.sender      = None
        self.receiver    = None
        self.hello_count = 0
        self.mar_count   = 0
        self.finished    = False

    def timeout(self):
        self.error = "Timeout Expired - hello_count: %d, mar_count: %d" % (self.hello_count, self.mar_count)
        self.conn.close()
        self.normal_conn.close()

    def fail(self, error):
        self.error = error
        self.conn.close()
        self.normal_conn.close()
        self.timer.cancel()

    def on_start(self, event):
        self.timer       = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.conn        = event.container.connect(self.host)
        self.normal_conn = event.container.connect(self.normal_host)

        ##
        # Attach a mobile address to cause mobile_Seq to be bumped
        ##
        self.mobile_receiver = event.container.create_receiver(self.normal_conn, "mobile_address")

    def on_link_opened(self, event):
        if event.receiver == self.mobile_receiver:
            self.receiver = event.container.create_receiver(self.conn)
            self.sender   = event.container.create_sender(self.conn)

            self.receiver.source.capabilities.put_object(symbol("qd.router"))
            self.receiver.target.capabilities.put_object(symbol("qd.router"))
            self.sender.source.capabilities.put_object(symbol("qd.router"))
            self.sender.target.capabilities.put_object(symbol("qd.router"))

    def on_message(self, event):
        if self.finished:
            return
        opcode = event.message.properties['opcode']
        body   = event.message.body
        rid    = body['id']

        if opcode == 'HELLO':
            self.hello_count += 1

            hello = Message(address='_local/qdhello',
                            properties={'opcode': 'HELLO'},
                            body={'id': 'TEST', 'pv': int32(1), 'area': '0', 'instance': 100, 'seen': [rid]})
            dlv = self.sender.send(hello)
            dlv.settle()

        elif opcode == 'RA':
            if self.mar_count > 2:
                self.finished = True
                self.fail(None)
                return

            mar = Message(address='_topo/0/%s/qdrouter.ma' % rid,
                          properties={'opcode': 'MAR'},
                          body={'id': 'TEST', 'pv': int32(2), 'area': '0', 'have_seq': int32(0)})
            dlv = self.sender.send(mar)
            dlv.settle()
            self.mar_count += 1

        elif opcode == 'MAU':
            self.fail('Received unexpected MAU from router')

    def run(self):
        Container(self).run()


if __name__ == '__main__':
    unittest.main(main_module())

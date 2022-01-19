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
from proton.handlers import MessagingHandler
from proton.reactor import Container

from system_test import TestCase, Qdrouterd, main_module, TIMEOUT
from system_test import unittest, TestTimeout


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


class HugeRouterIdTest(TestCase):
    """
    Ensure long router identifiers are advertized properly.

    Deploy a mesh of four routers with ids > 64 octets in length.
    """
    MAX_ID_LEN = 127

    @classmethod
    def setUpClass(cls):
        super(HugeRouterIdTest, cls).setUpClass()

        def router(name, extra_config):

            config = [
                ('router', {'mode': 'interior', 'id': name}),
                ('listener', {'port': cls.tester.get_port()})
            ] + extra_config

            config = Qdrouterd.Config(config)

            r = cls.tester.qdrouterd(name[:32], config, wait=False)
            cls.routers.append(r)
            return r

        cls.routers = []

        ir_port_A = cls.tester.get_port()
        ir_port_B = cls.tester.get_port()
        ir_port_C = cls.tester.get_port()
        ir_port_D = cls.tester.get_port()

        name_suffix = "X" * (cls.MAX_ID_LEN - 2)

        cls.RA_name = 'A.' + name_suffix
        cls.RA = router(cls.RA_name,
                        [('listener', {'role': 'inter-router', 'port': ir_port_A}),
                         ('connector', {'role': 'inter-router', 'port': ir_port_B}),
                         ('connector', {'role': 'inter-router', 'port': ir_port_C}),
                         ('connector', {'role': 'inter-router', 'port': ir_port_D})])

        cls.RB_name = 'B.' + name_suffix
        cls.RB = router(cls.RB_name,
                        [('listener', {'role': 'inter-router', 'port': ir_port_B}),
                         ('connector', {'role': 'inter-router', 'port': ir_port_C}),
                         ('connector', {'role': 'inter-router', 'port': ir_port_D})])

        cls.RC_name = 'C.' + name_suffix
        cls.RC = router(cls.RC_name,
                        [('listener', {'role': 'inter-router', 'port': ir_port_C}),
                         ('connector', {'role': 'inter-router', 'port': ir_port_D})])

        cls.RD_name = 'D.' + name_suffix
        cls.RD = router(cls.RD_name,
                        [('listener', {'role': 'inter-router', 'port': ir_port_D})])

        cls.RA.wait_ports()
        cls.RB.wait_ports()
        cls.RC.wait_ports()
        cls.RD.wait_ports()

    def test_01_wait_for_routers(self):
        """
        Wait until all the router in the mesh can see each other
        """
        self.RA.wait_router_connected(self.RB_name)
        self.RA.wait_router_connected(self.RC_name)
        self.RA.wait_router_connected(self.RD_name)

        self.RB.wait_router_connected(self.RA_name)
        self.RB.wait_router_connected(self.RC_name)
        self.RB.wait_router_connected(self.RD_name)

        self.RC.wait_router_connected(self.RA_name)
        self.RC.wait_router_connected(self.RB_name)
        self.RC.wait_router_connected(self.RD_name)

        self.RD.wait_router_connected(self.RA_name)
        self.RD.wait_router_connected(self.RB_name)
        self.RD.wait_router_connected(self.RC_name)


if __name__ == '__main__':
    unittest.main(main_module())

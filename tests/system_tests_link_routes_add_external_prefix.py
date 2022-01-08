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

import json
from time import sleep
from subprocess import PIPE, STDOUT

from proton import Message
from proton.handlers import MessagingHandler
from proton.reactor import Container

from system_test import TestCase, Qdrouterd, main_module, TIMEOUT, Process, TestTimeout
from system_test import unittest


def parse_record(fields, line):
    return [line[f[0]:f[1]].strip() for f in fields]


def parse_fields(header, items):
    pos = [header.find(name) for name in header.split()] + [len(header)]
    fields = list(zip(pos, pos[1:]))
    return [parse_record(fields, item) for item in items]


class LinkRouteTest(TestCase):
    """
    Tests the addExternalPrefix property of the linkRoute entity on the dispatch router.

    Sets up 3 routers (one of which are acting as brokers (QDR.A)). The other two routers have linkRoutes
    configured such that matching traffic will be directed to/from the 'fake' broker.

        QDR.A acting broker #1
             +---------+         +---------+         +---------+
             |         | <------ |         | <-----  |         |
             |  QDR.A  |         |  QDR.B  |         |  QDR.C  |
             |         | ------> |         | ------> |         |
             +---------+         +---------+         +---------+

    """
    @classmethod
    def get_router(cls, index):
        return cls.routers[index]

    @classmethod
    def setUpClass(cls):
        """Start three routers"""
        super(LinkRouteTest, cls).setUpClass()

        def router(name, connection):

            config = [
                ('router', {'mode': 'interior', 'id': 'QDR.%s' % name}),
            ] + connection

            config = Qdrouterd.Config(config)
            cls.routers.append(cls.tester.qdrouterd(name, config, wait=False))

        cls.routers = []
        a_listener_port = cls.tester.get_port()
        b_listener_port = cls.tester.get_port()
        c_listener_port = cls.tester.get_port()

        router('A',
               [
                   ('listener', {'role': 'normal', 'host': '0.0.0.0', 'port': a_listener_port, 'saslMechanisms': 'ANONYMOUS'}),
               ])
        router('B',
               [
                   ('listener', {'role': 'normal', 'host': '0.0.0.0', 'port': b_listener_port, 'saslMechanisms': 'ANONYMOUS'}),
                   ('connector', {'name': 'broker', 'role': 'route-container', 'host': '0.0.0.0', 'port': a_listener_port, 'saslMechanisms': 'ANONYMOUS'}),
                   ('connector', {'name': 'routerC', 'role': 'inter-router', 'host': '0.0.0.0', 'port': c_listener_port}),

                   ('linkRoute', {'prefix': 'foo', 'containerId': 'QDR.A', 'direction': 'in', 'addExternalPrefix': 'bar.'}),
                   ('linkRoute', {'prefix': 'foo', 'containerId': 'QDR.A', 'direction': 'out', 'addExternalPrefix': 'bar.'}),

                   ('linkRoute', {'prefix': 'qdr-a', 'containerId': 'QDR.A', 'direction': 'in', 'delExternalPrefix': 'qdr-a.'}),
                   ('linkRoute', {'prefix': 'qdr-a', 'containerId': 'QDR.A', 'direction': 'out', 'delExternalPrefix': 'qdr-a.'})

               ]
               )
        router('C',
               [
                   ('listener', {'host': '0.0.0.0', 'role': 'normal', 'port': cls.tester.get_port(), 'saslMechanisms': 'ANONYMOUS'}),
                   ('listener', {'host': '0.0.0.0', 'role': 'inter-router', 'port': c_listener_port, 'saslMechanisms': 'ANONYMOUS'}),

                   ('linkRoute', {'prefix': 'foo', 'direction': 'in', 'addExternalPrefix': 'bar.'}),
                   ('linkRoute', {'prefix': 'foo', 'direction': 'out', 'addExternalPrefix': 'bar.'}),

                   ('linkRoute', {'prefix': 'qdr-a', 'direction': 'in', 'delExternalPrefix': 'qdr-a.'}),
                   ('linkRoute', {'prefix': 'qdr-a', 'direction': 'out', 'delExternalPrefix': 'qdr-a.'})
               ]
               )

        # Wait for the routers to locate each other, and for route propagation
        # to settle
        cls.routers[1].wait_router_connected('QDR.C')
        cls.routers[2].wait_router_connected('QDR.B')
        cls.routers[2].wait_address("foo", remotes=1, delay=0.5, count=2)

        # This is not a classic router network in the sense that QDR.A is acting as a broker. We allow a little
        # bit more time for the routers to stabilize.
        sleep(2)

    def run_qdstat_linkRoute(self, address, args=None):
        cmd = ['qdstat', '--bus', str(address), '--timeout', str(TIMEOUT)] + ['--linkroute']
        if args:
            cmd = cmd + args
        p = self.popen(
            cmd,
            name='qdstat-' + self.id(), stdout=PIPE, expect=None,
            universal_newlines=True)

        out = p.communicate()[0]
        assert p.returncode == 0, "qdstat exit status %s, output:\n%s" % (p.returncode, out)
        return out

    def run_qdmanage(self, cmd, input=None, expect=Process.EXIT_OK, address=None):
        p = self.popen(
            ['qdmanage'] + cmd.split(' ') + ['--bus', address or self.address(), '--indent=-1', '--timeout', str(TIMEOUT)],
            stdin=PIPE, stdout=PIPE, stderr=STDOUT, expect=expect,
            universal_newlines=True)
        out = p.communicate(input)[0]
        try:
            p.teardown()
        except Exception as e:
            raise Exception("%s\n%s" % (e, out))
        return out

    def test_qdstat_link_routes_on_B(self):
        output = self.run_qdstat_linkRoute(self.routers[1].addresses[0])
        lines = output.split("\n")
        self.assertEqual(len(lines), 11)  # 4 links, 3 lines of header and an empty line at the end
        header = lines[4]
        columns = header.split()
        self.assertEqual(len(columns), 6)
        self.assertEqual(columns[4], "add-ext-prefix")
        self.assertEqual(columns[5], "del-ext-prefix")
        linkroutes = parse_fields(header, lines[6:10])
        self.assertEqual(linkroutes[0][0], "foo")
        self.assertEqual(linkroutes[0][1], "in")
        self.assertEqual(linkroutes[0][4], "bar.")
        self.assertEqual(linkroutes[1][0], "foo")
        self.assertEqual(linkroutes[1][1], "out")
        self.assertEqual(linkroutes[1][4], "bar.")
        self.assertEqual(linkroutes[2][0], "qdr-a")
        self.assertEqual(linkroutes[2][1], "in")
        self.assertEqual(linkroutes[2][5], "qdr-a.")
        self.assertEqual(linkroutes[3][0], "qdr-a")
        self.assertEqual(linkroutes[3][1], "out")
        self.assertEqual(linkroutes[3][5], "qdr-a.")

    def test_qdstat_link_routes_on_C(self):
        output = self.run_qdmanage('QUERY --type=org.apache.qpid.dispatch.router.config.linkRoute', address=self.routers[2].addresses[0])
        objects = json.loads(output)
        self.assertEqual(len(objects), 4)
        index = {}
        for o in objects:
            index["%s-%s" % (o["prefix"], o["direction"])] = o
        self.assertEqual(index["foo-in"]["addExternalPrefix"], "bar.")
        self.assertEqual(index["foo-out"]["addExternalPrefix"], "bar.")
        self.assertEqual(index["qdr-a-in"]["delExternalPrefix"], "qdr-a.")
        self.assertEqual(index["qdr-a-out"]["delExternalPrefix"], "qdr-a.")

    def test_route_sender_add_prefix_on_B(self):
        test = SendReceive("%s/foo" % self.routers[1].addresses[0], "%s/bar.foo" % self.routers[0].addresses[0])
        test.run()
        self.assertIsNone(test.error)

    def test_route_receiver_add_prefix_on_B(self):
        test = SendReceive("%s/bar.foo" % self.routers[0].addresses[0], "%s/foo" % self.routers[1].addresses[0])
        test.run()
        self.assertIsNone(test.error)

    def test_route_sender_add_prefix_on_C(self):
        test = SendReceive("%s/foo" % self.routers[2].addresses[0], "%s/bar.foo" % self.routers[0].addresses[0])
        test.run()
        self.assertIsNone(test.error)

    def test_route_receiver_add_prefix_on_C(self):
        test = SendReceive("%s/bar.foo" % self.routers[0].addresses[0], "%s/foo" % self.routers[2].addresses[0])
        test.run()
        self.assertIsNone(test.error)

    def test_route_sender_del_prefix_on_B(self):
        test = SendReceive("%s/qdr-a.baz" % self.routers[1].addresses[0], "%s/baz" % self.routers[0].addresses[0])
        test.run()
        self.assertIsNone(test.error)

    def test_route_receiver_del_prefix_on_B(self):
        test = SendReceive("%s/baz" % self.routers[0].addresses[0], "%s/qdr-a.baz" % self.routers[1].addresses[0])
        test.run()
        self.assertIsNone(test.error)

    def test_route_sender_del_prefix_on_C(self):
        test = SendReceive("%s/qdr-a.baz" % self.routers[2].addresses[0], "%s/baz" % self.routers[0].addresses[0])
        test.run()
        self.assertIsNone(test.error)

    def test_route_receiver_del_prefix_on_C(self):
        test = SendReceive("%s/baz" % self.routers[0].addresses[0], "%s/qdr-a.baz" % self.routers[2].addresses[0])
        test.run()
        self.assertIsNone(test.error)


class SendReceive(MessagingHandler):

    def __init__(self, send_url, recv_url, message=None):
        super(SendReceive, self).__init__()
        self.send_url = send_url
        self.recv_url = recv_url
        self.message = message or Message(body="SendReceiveTest")
        self.sent = False
        self.error = None

    def close(self):
        self.sender.close()
        self.receiver.close()
        self.sender.connection.close()
        self.receiver.connection.close()

    def timeout(self):
        self.error = "Timeout Expired - Check for cores"
        self.close()

    def stop(self):
        self.close()
        self.timer.cancel()

    def on_start(self, event):
        self.timer      = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        event.container.container_id = "SendReceiveTestClient"
        self.sender = event.container.create_sender(self.send_url)
        self.receiver = event.container.create_receiver(self.recv_url)

    def on_sendable(self, event):
        if not self.sent:
            event.sender.send(self.message)
            self.sent = True

    def on_message(self, event):
        if self.message.body != event.message.body:
            self.error = "Incorrect message. Got %s, expected %s" % (event.message.body, self.message.body)

    def on_accepted(self, event):
        self.stop()

    def run(self):
        Container(self).run()


if __name__ == '__main__':
    unittest.main(main_module())

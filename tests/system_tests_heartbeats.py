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

from proton import Message
from proton.handlers import MessagingHandler
from proton.reactor import Container

from system_test import TestCase, Qdrouterd, main_module, TIMEOUT, unittest, TestTimeout


class HeartbeatTimer:
    def __init__(self, parent):
        self.parent = parent

    def on_timer_task(self, event):
        self.parent.heartbeat()


class RouterTest(TestCase):

    inter_router_port = None

    @classmethod
    def setUpClass(cls):
        """Start a router"""
        super(RouterTest, cls).setUpClass()

        def router(name, mode, extra=None):
            config = [
                ('router', {'mode': mode, 'id': name, "helloMaxAgeSeconds": '10'}),
                ('listener', {'port': cls.tester.get_port(), 'stripAnnotations': 'no'}),
                ('address', {'prefix': 'queue', 'waypoint': 'yes'}),
                ('address', {'prefix': 'multi', 'ingressPhase': '0', 'egressPhase': '9'})
            ]

            if extra:
                config.append(extra)
            config = Qdrouterd.Config(config)
            cls.routers.append(cls.tester.qdrouterd(name, config, wait=True))

        cls.routers = []
        router('INT.A', 'interior')

    def test_01_verify_rx_reject(self):
        test = ReceiverRejectTest(self.routers[0].addresses[0])
        test.run()
        self.assertIsNone(test.error)

    def test_02_close_with_no_heartbeats(self):
        test = ConnectionCloseTest(self.routers[0].addresses[0], 0)
        test.run()
        self.assertIsNone(test.error)

    def test_03_close_with_two_heartbeats(self):
        test = ConnectionCloseTest(self.routers[0].addresses[0], 2)
        test.run()
        self.assertIsNone(test.error)


class ReceiverRejectTest(MessagingHandler):
    def __init__(self, host):
        super(ReceiverRejectTest, self).__init__()
        self.host     = host
        self.conn     = None
        self.receiver = None
        self.addr     = '_$qd.edge_heartbeat'
        self.error    = None

    def timeout(self):
        self.error = "Timeout Expired"
        self.conn.close()

    def fail(self, error):
        self.error = error
        self.conn.close()
        self.timer.cancel()

    def on_start(self, event):
        self.timer    = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.conn     = event.container.connect(self.host)
        self.receiver = event.container.create_receiver(self.conn, self.addr)

    def on_link_closing(self, event):
        if event.receiver == self.receiver:
            self.fail(None)

    def on_link_opened(self, event):
        if event.receiver == self.receiver:
            if event.receiver.remote_source.address is not None:
                self.fail('Heartbeat receiver was unexpectedly opened')

    def run(self):
        Container(self).run()


class ConnectionCloseTest(MessagingHandler):
    def __init__(self, host, count):
        super(ConnectionCloseTest, self).__init__()
        self.host    = host
        self.count   = count
        self.conn    = None
        self.sender  = None
        self.addr    = '_$qd.edge_heartbeat'
        self.error   = None
        self.pending = False
        self.n_tx    = 0

    def timeout(self):
        self.error = "Timeout Expired - n_tx=%d" % self.n_tx
        self.conn.close()
        if self.hb_timer:
            self.hb_timer.cancel()

    def fail(self, error):
        self.error = error
        self.conn.close()
        self.timer.cancel()
        if self.hb_timer:
            self.hb_timer.cancel()

    def on_start(self, event):
        self.timer    = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.reactor  = event.reactor
        self.hb_timer = None
        self.conn     = event.container.connect(self.host)
        self.sender   = event.container.create_sender(self.conn, self.addr)
        if self.count > 0:
            self.heartbeat()

    def send_heartbeat(self):
        if self.sender.credit > 0 and self.pending:
            msg = Message(body=self.n_tx)
            self.sender.send(msg)
            self.n_tx += 1
            self.pending = False
            self.hb_timer = self.reactor.schedule(1.0, HeartbeatTimer(self))

    def heartbeat(self):
        self.hb_timer = None
        if self.n_tx < self.count:
            self.pending = True
            self.send_heartbeat()

    def on_sendable(self, event):
        if event.sender == self.sender:
            self.send_heartbeat()

    def on_disconnected(self, event):
        self.fail(None)

    def run(self):
        Container(self).run()


if __name__ == '__main__':
    unittest.main(main_module())

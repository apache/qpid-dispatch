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

import unittest, os, json
from subprocess import PIPE, STDOUT
from proton import Message, PENDING, ACCEPTED, REJECTED, RELEASED, SSLDomain, SSLUnavailable, Timeout
from system_test import TestCase, Qdrouterd, main_module, DIR, TIMEOUT, Process
from proton.handlers import MessagingHandler
from proton.reactor import Container, DynamicNodeProperties

# PROTON-828:
try:
    from proton import MODIFIED
except ImportError:
    from proton import PN_STATUS_MODIFIED as MODIFIED


class RouterTest(TestCase):

    inter_router_port = None

    @classmethod
    def setUpClass(cls):
        """Start a router"""
        super(RouterTest, cls).setUpClass()

        def router(name):

            config = [
                ('router', {'mode': 'standalone', 'id': name}),
                ('listener', {'port': cls.tester.get_port()}),
                ('address', {'prefix': 'multicast', 'distribution' : 'multicast'}),
            ]

            config = Qdrouterd.Config(config)

            cls.routers.append(cls.tester.qdrouterd(name, config, wait=True))

        cls.routers = []

        inter_router_port = cls.tester.get_port()

        router('A')
        cls.routers[0].wait_ready()


    def test_01_default_multicast_test(self):
        test = DeniedUnsettledMulticastTest(self.routers[0].addresses[0])
        test.run()
        self.assertEqual(None, test.error)


class Timeout(object):
    def __init__(self, parent):
        self.parent = parent

    def on_timer_task(self, event):
        self.parent.timeout()


class DeniedUnsettledMulticastTest(MessagingHandler):
    def __init__(self, host):
        super(DeniedUnsettledMulticastTest, self).__init__()
        self.host       = host
        self.count      = 10
        self.error      = None
        self.addr       = "multicast/test"
        self.sent_uns   = 0
        self.sent_pres  = 0
        self.n_received = 0
        self.n_rejected = 0

    def timeout(self):
        self.error = "Timeout Expired - n_received=%d n_rejected=%d" % (self.n_received, self.n_rejected)
        self.conn.close()

    def check_done(self):
        if self.n_received == self.count and self.n_rejected == self.count:
            self.conn.close()
            self.timer.cancel()

    def send(self):
        while self.sent_uns < self.count:
            m = Message(body="Unsettled %d" % self.sent_uns)
            self.sender.send(m)
            self.sent_uns += 1
        while self.sent_pres < self.count:
            m = Message(body="Presettled %d" % self.sent_pres)
            dlv = self.sender.send(m)
            dlv.settle()
            self.sent_pres += 1

    def on_start(self, event):
        self.timer    = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.conn     = event.container.connect(self.host)
        self.receiver = event.container.create_receiver(self.conn, self.addr)
        self.sender   = event.container.create_sender(self.conn, self.addr)

    def on_sendable(self, event):
        self.send()

    def on_message(self, event):
        self.n_received += 1
        self.check_done()

    def on_rejected(self, event):
        self.n_rejected += 1
        self.check_done()

    def run(self):
        Container(self).run()


if __name__ == '__main__':
    unittest.main(main_module())

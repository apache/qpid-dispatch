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

import re
from subprocess import PIPE
from system_test import TestCase, Qdrouterd, TIMEOUT, main_module, unittest, TestTimeout
from proton.handlers import MessagingHandler
from proton.reactor import Container
from proton import Message


class DefaultDistributionTest(TestCase):
    """System tests for testing the defaultDistribution attribute of the router entity"""
    @classmethod
    def setUpClass(cls):
        super(DefaultDistributionTest, cls).setUpClass()
        name = "test-router"
        config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'QDR', "defaultDistribution": 'unavailable'}),

            ('listener', {'port': cls.tester.get_port()}),

            ('address', {'prefix': 'closest', 'distribution': 'closest'}),
            ('address', {'prefix': 'spread', 'distribution': 'balanced'}),
            ('address', {'prefix': 'multicast', 'distribution': 'multicast'})
        ])
        cls.router = cls.tester.qdrouterd(name, config)
        cls.router.wait_ready()
        cls.address = cls.router.addresses[0]

    def run_qdstat(self, args, regexp=None, address=None):
        p = self.popen(
            ['qdstat', '--bus', str(address or self.address), '--timeout', str(TIMEOUT)] + args,
            name='qdstat-' + self.id(), stdout=PIPE, expect=None,
            universal_newlines=True)

        out = p.communicate()[0]
        assert p.returncode == 0, \
            "qdstat exit status %s, output:\n%s" % (p.returncode, out)
        if regexp:
            assert re.search(regexp, out, re.I), "Can't find '%s' in '%s'" % (regexp, out)
        return out

    def test_create_unavailable_sender(self):
        test = UnavailableSender(self.address)
        test.run()
        self.assertTrue(test.passed)

    def test_create_unavailable_receiver(self):
        test = UnavailableReceiver(self.address)
        test.run()
        self.assertTrue(test.passed)

    def test_anonymous_sender(self):
        test = UnavailableAnonymousSender(self.address)
        test.run()
        self.assertTrue(test.received_error)

    def test_general(self):
        out = self.run_qdstat(['--general'], r'(?s)Router Statistics.*Mode\s*Standalone')
        self.assertIn("Connections                      1", out)
        self.assertIn("Nodes                            0", out)
        self.assertIn("Auto Links                       0", out)
        self.assertIn("Link Routes                      0", out)
        self.assertIn("Router Id                        QDR", out)
        self.assertIn("Mode                             standalone", out)


class UnavailableBase(MessagingHandler):

    def __init__(self, address):
        super(UnavailableBase, self).__init__()
        self.address = address
        self.dest = "UnavailableBase"
        self.conn = None
        self.sender = None
        self.receiver = None
        self.link_error = False
        self.link_closed = False
        self.passed = False
        self.timer = None
        self.link_name = "base_link"

    def check_if_done(self):
        if self.link_error and self.link_closed:
            self.passed = True
            self.conn.close()
            self.timer.cancel()

    def on_link_error(self, event):
        link = event.link
        if event.link.name == self.link_name and link.remote_condition.description \
                == "Node not found":
            self.link_error = True
        self.check_if_done()

    def on_link_remote_close(self, event):
        if event.link.name == self.link_name:
            self.link_closed = True
            self.check_if_done()

    def run(self):
        Container(self).run()


class UnavailableSender(UnavailableBase):

    def __init__(self, address):
        super(UnavailableSender, self).__init__(address)

    def timeout(self):
        self.error = "Timeout Expired while while attempting to send to unavailable address"
        self.conn.close()

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.conn = event.container.connect(self.address)
        # Creating a sender to an address with unavailable distribution
        # The router will not allow this link to be established. It will close the link with an error of
        # "Node not found"
        self.sender = event.container.create_sender(self.conn, self.dest, name=self.link_name)


class UnavailableReceiver(UnavailableBase):

    def __init__(self, address):
        super(UnavailableReceiver, self).__init__(address)

    def timeout(self):
        self.error = "Timeout Expired while while attempting to receive from unavailable address"
        self.conn.close()

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.conn = event.container.connect(self.address)
        # Creating a receiver to an address with unavailable distribution
        # The router will not allow this link to be established. It will close the link with an error of
        # "Node not found"
        self.receiver = event.container.create_receiver(self.conn, self.dest, name=self.link_name)


class UnavailableAnonymousSender(MessagingHandler):

    def __init__(self, address):
        super(UnavailableAnonymousSender, self).__init__()
        self.address = address
        self.dest = "UnavailableBase"
        self.conn = None
        self.sender = None
        self.receiver = None
        self.received_error = False
        self.timer = None
        self.link_name = "anon_link"
        self.error_description = "Deliveries cannot be sent to an unavailable address"
        self.error_name = 'amqp:not-found'
        self.num_sent = 0

    def timeout(self):
        self.error = "Timeout Expired while attempting to send to unavailable address"
        self.conn.close()

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.conn = event.container.connect(self.address)
        # Creating an anonymous sender
        self.sender = event.container.create_sender(self.conn, name=self.link_name)

    def on_sendable(self, event):
        if self.num_sent < 1:
            msg = Message(id=1, body='Hello World')
            # this is a unavailable address
            msg.address = "SomeUnavailableAddress"
            event.sender.send(msg)
            self.num_sent += 1

    def on_rejected(self, event):
        if event.link.name == self.link_name and event.delivery.remote.condition.name == self.error_name \
                and self.error_description == event.delivery.remote.condition.description:
            self.received_error = True
            self.conn.close()
            self.timer.cancel()

    def run(self):
        Container(self).run()


if __name__ == '__main__':
    unittest.main(main_module())

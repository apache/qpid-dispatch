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

from __future__ import unicode_literals
from __future__ import division
from __future__ import absolute_import
from __future__ import print_function

import os
from time import sleep
from threading import Event
from threading import Timer

from proton import Message, symbol
from system_test import TestCase, Qdrouterd, main_module, TIMEOUT, DIR, MgmtMsgProxy, unittest, TestTimeout
from proton.handlers import MessagingHandler
from proton.reactor import Container

class AddrTimer(object):
    def __init__(self, parent):
        self.parent = parent

    def on_timer_task(self, event):
        self.parent.check_address()


class RouterTest(TestCase):

    @classmethod
    def setUpClass(cls):
        """Start a router"""
        super(RouterTest, cls).setUpClass()

        policy_config_path = os.path.join(DIR, 'policy-tempfile')

        def router(name, mode, connection, extra=None):
            config = [
                ('router', {'mode': mode, 'id': name}),
                connection
            ]

            if extra:
                config.append(extra)
            config = Qdrouterd.Config(config)
            cls.routers.append(cls.tester.qdrouterd(name, config, wait=True))

        cls.routers = []

        router('OPEN', 'standalone', ('listener', {'port': cls.tester.get_port()}))
        router('BLOCKED', 'standalone', ('listener', {'port': cls.tester.get_port(), 'policyVhost':'blocked'}),
               ('policy', {'enableVhostPolicy':True, 'policyDir':policy_config_path}))
        router('ALLOWED', 'standalone', ('listener', {'port': cls.tester.get_port(), 'policyVhost':'allowed'}),
               ('policy', {'enableVhostPolicy':True, 'policyDir':policy_config_path}))
        router('COUNT_TEST', 'standalone', ('listener', {'port': cls.tester.get_port(), 'policyVhost':'allowed'}),
               ('policy', {'enableVhostPolicy':True, 'policyDir':policy_config_path}))


    def test_01_temp_file_normal(self):
        test = TempFileTest(self.routers[0].addresses[0], 'test.01', 100, 1, True)
        test.run()
        self.assertEqual(None, test.error)

    def test_02_no_subject(self):
        test = TempFileTest(self.routers[0].addresses[0], None, 100, 1, False, 'amqp:invalid-field')
        test.run()
        self.assertEqual(None, test.error)

    def test_03_illegal_path_traversal(self):
        test = TempFileTest(self.routers[0].addresses[0], '../test.03', 100, 1, False, 'amqp:invalid-field')
        test.run()
        self.assertEqual(None, test.error)

    def test_04_blocked_by_policy(self):
        test = TempFileTest(self.routers[1].addresses[0], 'test.04', 100, 1, False, 'amqp:unauthorized-access')
        test.run()
        self.assertEqual(None, test.error)

    def test_05_allowed_by_policy(self):
        test = TempFileTest(self.routers[2].addresses[0], 'test.05', 100, 1, True)
        test.run()
        self.assertEqual(None, test.error)

    def test_06_file_too_large(self):
        test = TempFileTest(self.routers[2].addresses[0], 'test.06', 1000, 1, False, 'amqp:unauthorized-access')
        test.run()
        self.assertEqual(None, test.error)

    def test_07_too_many_files(self):
        test = TempFileTest(self.routers[3].addresses[0], 'test.07', 100, 20, False, 'amqp:unauthorized-access')
        test.run()
        self.assertEqual(None, test.error)


class TempFileTest(MessagingHandler):
    def __init__(self, host, filename, content_size, file_count, expect_pass, expect_error=None):
        super(TempFileTest, self).__init__()
        self.host         = host
        self.filename     = filename
        self.content_size = content_size
        self.file_count   = file_count;
        self.expect_pass  = expect_pass
        self.expect_error = expect_error

        self.error      = None
        self.n_sent     = 0
        self.n_settled  = 0
        self.n_rejected = 0
        self.condition  = None
        self.conn       = None
        self.sender     = None

    def timeout(self):
        self.error = "Timeout Expired - n_sent=%d n_settled=%d n_rejected=%d" % (self.n_sent, self.n_settled, self.n_rejected)
        self.conn.close()

    def fail(self, error):
        self.error = error
        self.conn.close()
        self.timer.cancel()

    def complete(self):
        if self.expect_pass:
            if self.n_rejected == 0:
                self.fail(None)
            else:
                self.fail("Expected successful transfer - error: %r" % self.condition)
        else:
            if self.n_rejected == 0:
                self.fail("Expected failed transfer - no rejections")
            else:
                if self.condition.name == self.expect_error:
                    self.fail(None)
                else:
                    self.fail("Expected condition: %s Got %s" % (self.expect_error, self.condition.name))

    def on_start(self, event):
        self.timer   = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.conn    = event.container.connect(self.host)
        self.sender  = event.container.create_sender(self.conn, '_$qd.store_file')

    def on_sendable(self, event):
        if event.sender == self.sender:
            while self.sender.credit > 0 and self.n_sent < self.file_count:
                chars   = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                content = "".join(chars[c % len(chars)] for c in os.urandom(self.content_size))
                subject = None
                if self.filename:
                    subject = "%s.%d" % (self.filename, self.n_sent)
                msg = Message(subject=subject, body=content)
                self.sender.send(msg)
                self.n_sent += 1

    def on_accepted(self, event):
        self.n_settled += 1
        if self.n_settled == self.n_sent:
            self.complete()

    def on_rejected(self, event):
        self.n_settled  += 1
        self.n_rejected += 1
        self.condition = event.delivery.remote.condition
        if self.n_settled == self.n_sent:
            self.complete()

    def run(self):
        Container(self).run()


if __name__== '__main__':
    unittest.main(main_module())

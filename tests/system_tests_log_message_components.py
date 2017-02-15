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
from proton import Message, symbol
from system_test import TestCase, Qdrouterd, Process, TIMEOUT
from subprocess import PIPE, STDOUT
from proton.handlers import MessagingHandler
from proton.reactor import Container



class RouterMessageLogTestBase(TestCase):
    def run_qdmanage(self, cmd, input=None, expect=Process.EXIT_OK, address=None):
        p = self.popen(
            ['qdmanage'] +
            cmd.split(' ') +
            ['--bus',
             address or self.address(),
             '--indent=-1', '--timeout', str(TIMEOUT)],
            stdin=PIPE, stdout=PIPE, stderr=STDOUT, expect=expect)
        out = p.communicate(input)[0]
        try:
            p.teardown()
        except Exception, e:
            raise Exception("%s\n%s" % (e, out))
        return out

class RouterMessageLogTestAll(RouterMessageLogTestBase):
    """System tests to check log messages emitted by router"""
    @classmethod
    def setUpClass(cls):
        """Start a router and a messenger"""
        super(RouterMessageLogTestAll, cls).setUpClass()
        name = "test-router"
        config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'QDR'}),

            ('listener', {'port': cls.tester.get_port(), 'logMessage': 'all'}),

            ('address', {'prefix': 'closest', 'distribution': 'closest'}),
            ('address', {'prefix': 'spread', 'distribution': 'balanced'}),
            ('address', {'prefix': 'multicast', 'distribution': 'multicast'}),
        ])
        cls.router = cls.tester.qdrouterd(name, config)
        cls.router.wait_ready()

    def address(self):
        return self.router.addresses[0]

    def test_log_message_all(self):
        test = LogMessageTest(self.address())
        test.run()
        self.assertTrue(test.message_received)

        everything_ok = False

        logs = json.loads(self.run_qdmanage("get-log"))
        for log in logs:
            if log[0] == u'MESSAGE':
                if "message-id='123455'" in log[2]:
                    self.assertTrue("user-id='testuser'" in log[2])
                    self.assertTrue("subject='test-subject'" in log[2])
                    self.assertTrue("reply-to='hello_world'" in log[2])
                    self.assertTrue("correlation-id='89'" in log[2])
                    self.assertTrue("content-type='text/html; charset=utf-8'" in log[2])
                    self.assertTrue("content-encoding='gzip, deflate'" in log[2])
                    self.assertTrue("group-id='group1', group-sequence='0', reply-to-group-id='group0'" in log[2])
                    self.assertTrue("application properties={app-property=[10, 20, 30], some-other=O_one}" in log[2])
                    self.assertTrue("creation-time='2017-02-22" in log[2])
                    self.assertTrue("10:23.883" in log[2])

                    everything_ok = True

        self.assertTrue(everything_ok)

class RouterMessageLogTestNone(RouterMessageLogTestBase):
    """System tests to check log messages emitted by router"""
    @classmethod
    def setUpClass(cls):
        """Start a router and a messenger"""
        super(RouterMessageLogTestNone, cls).setUpClass()
        name = "test-router"
        config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'QDR'}),

            ('listener', {'port': cls.tester.get_port()}),

            ('address', {'prefix': 'closest', 'distribution': 'closest'}),
            ('address', {'prefix': 'spread', 'distribution': 'balanced'}),
            ('address', {'prefix': 'multicast', 'distribution': 'multicast'}),
        ])
        cls.router = cls.tester.qdrouterd(name, config)
        cls.router.wait_ready()

    def address(self):
        return self.router.addresses[0]

    def test_log_message_none(self):
        test = LogMessageTest(self.address())
        test.run()
        self.assertTrue(test.message_received)

        everything_ok = True
        logs = json.loads(self.run_qdmanage("get-log"))
        for log in logs:
            if log[0] == u'MESSAGE':
                everything_ok = False

        self.assertTrue(everything_ok)

class RouterMessageLogTestSome(RouterMessageLogTestBase):
    """System tests to check log messages emitted by router"""
    @classmethod
    def setUpClass(cls):
        """Start a router and a messenger"""
        super(RouterMessageLogTestSome, cls).setUpClass()
        name = "test-router"
        config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'QDR'}),

            ('listener', {'port': cls.tester.get_port(), 'logMessage': 'message-id,user-id,subject,reply-to'}),

            ('address', {'prefix': 'closest', 'distribution': 'closest'}),
            ('address', {'prefix': 'spread', 'distribution': 'balanced'}),
            ('address', {'prefix': 'multicast', 'distribution': 'multicast'}),
        ])
        cls.router = cls.tester.qdrouterd(name, config)
        cls.router.wait_ready()

    def address(self):
        return self.router.addresses[0]

    def test_log_message_some(self):
        test = LogMessageTest(self.address())
        test.run()
        self.assertTrue(test.message_received)

        everything_ok = False
        logs = json.loads(self.run_qdmanage("get-log"))
        for log in logs:
            if log[0] == u'MESSAGE':
                if u"received Message{message-id='123455', user-id='testuser', subject='test-subject', reply-to='hello_world'}" in log[2]:
                    everything_ok = True

        self.assertTrue(everything_ok)

class LogMessageTest(MessagingHandler):
    def __init__(self, address):
        super(LogMessageTest, self).__init__(auto_accept=False)
        self.address = address
        self.sent = False
        self.message_received = False
        self.dest = "logMessageTest"

    def on_start(self, event):
        conn = event.container.connect(self.address)
        event.container.create_sender(conn, self.dest)
        event.container.create_receiver(conn, self.dest)

    def on_sendable(self, event):
        if not self.sent:
            msg = Message()
            msg.address = self.address
            msg.id = '123455'
            msg.user_id = 'testuser'
            msg.subject = 'test-subject'
            msg.content_type = 'text/html; charset=utf-8'
            msg.correlation_id = 89
            msg.creation_time = 1487772623.883
            msg.group_id = "group1"
            msg.reply_to = 'hello_world'
            msg.content_encoding = 'gzip, deflate'
            msg.reply_to_group_id = "group0"
            application_properties = dict()
            application_properties['app-property'] = [10, 20, 30]
            application_properties['some-other'] = symbol("O_one")
            msg.properties = application_properties
            msg.body = u"Hello World!"
            event.sender.send(msg)
            self.sent = True

    def on_message(self, event):
        if "Hello World!" == event.message.body:
            self.message_received = True
        event.connection.close()

    def run(self):
        Container(self).run()

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
from subprocess import PIPE, STDOUT

from proton import Message, symbol
from proton.handlers import MessagingHandler
from proton.reactor import Container

from qpid_dispatch_internal.compat import BINARY

from system_test import TestCase, Qdrouterd, Process, TIMEOUT
from system_test import unittest

# force streaming in order to check that
# freeing sent buffers does not lose fields
# needed by logging
MAX_FRAME = 1024
BIG_BODY = 'X' * 1000000


class RouterMessageLogTestBase(TestCase):
    def run_qdmanage(self, cmd, input=None, expect=Process.EXIT_OK, address=None):
        p = self.popen(
            ['qdmanage'] +
            cmd.split(' ') +
            ['--bus',
             address or self.address(),
             '--indent=-1', '--timeout', str(TIMEOUT)],
            stdin=PIPE, stdout=PIPE, stderr=STDOUT, expect=expect,
            universal_newlines=True)
        out = p.communicate(input)[0]
        try:
            p.teardown()
        except Exception as e:
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

            ('listener', {'port': cls.tester.get_port(),
                          'maxFrameSize': MAX_FRAME,
                          'messageLoggingComponents': 'all'}),

            ('log', {'module': 'MESSAGE',
                     'enable': 'trace+',
                     'outputFile': 'QDR-message.log'}),
            ('log', {'module': 'DEFAULT',
                     'enable': 'info+',
                     'includeSource': 'true',
                     'outputFile': 'QDR.log'}),

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
        message_logs = [log for log in logs if log[0] == 'MESSAGE']
        self.assertTrue(message_logs)
        test_message = [log for log in message_logs if "message-id=\"123455\"" in log[2]]
        self.assertTrue(2 == len(test_message), message_logs)  # Sent and Received
        self.assertIn('Received', test_message[0][2])
        self.assertIn('Sent', test_message[1][2])
        for log in test_message:
            self.assertIn('user-id=b"testuser"', log[2])
            self.assertIn('subject="test-subject"', log[2])
            self.assertIn('reply-to="hello_world"', log[2])
            self.assertIn('correlation-id=89', log[2])
            self.assertIn('content-type=:"text/html; charset=utf-8"', log[2])
            self.assertIn('content-encoding=:"gzip, deflate"', log[2])
            self.assertIn('group-id="group1", group-sequence=0, reply-to-group-id="group0"', log[2])
            self.assertIn('app-properties={"app-property"=[10, 20, 30], "some-other"=:"O_one"}', log[2])
            self.assertIn('creation-time="2017-02-22', log[2])
            self.assertIn('10:23.883', log[2])


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
            if log[0] == 'MESSAGE':
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
            # logMessage has been deprecated. We are using it here so we can make sure that it is still
            # backward compatible.
            ('listener', {'port': cls.tester.get_port(), 'logMessage': 'user-id,subject,reply-to'}),

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
        message_logs = [log for log in logs if log[0] == 'MESSAGE']
        self.assertTrue(message_logs)
        test_message = [log for log in message_logs if
                        'Received Message{user-id=b"testuser", subject="test-subject", reply-to="hello_world"}' in log[2]]
        self.assertTrue(test_message, message_logs)
        test_message = [log for log in message_logs if
                        'Sent Message{user-id=b"testuser", subject="test-subject", reply-to="hello_world"}' in log[2]]
        self.assertTrue(test_message, message_logs)


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
            msg.user_id = BINARY('testuser')
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
            msg.body = ["Hello World!", BIG_BODY]
            event.sender.send(msg)
            self.sent = True

    def on_message(self, event):
        if "Hello World!" == event.message.body[0]:
            self.message_received = True
        event.connection.close()

    def run(self):
        Container(self).run()


if __name__ == '__main__':
    unittest.main()

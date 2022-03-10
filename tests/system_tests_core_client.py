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

from system_test import TestCase, Qdrouterd, QdManager, TIMEOUT

from proton import Message
from proton import Endpoint
from proton.handlers import MessagingHandler
from proton.reactor import Container

# test the request/response core client messaging API
#
# These tests rely on enabling the router test hooks, which instantiates a test
# client (see modules/test_hooks/core_test_hooks) see core_test_hooks.c

CONTAINER_ID = "org.apache.qpid.dispatch.test_core_client"
TARGET_ADDR = "test_core_client_address"


class CoreClientAPITest(TestCase):
    @classmethod
    def setUpClass(cls):
        super(CoreClientAPITest, cls).setUpClass()

        config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'QDR.A'}),
            ('listener', {'port': cls.tester.get_port()}),
        ])

        cls.router = cls.tester.qdrouterd("A", config, cl_args=["-T"])

    def test_send_receive(self):
        ts = TestService(self.router.addresses[0], credit=250)
        ts.run()
        self.assertTrue(ts.error is None)
        self.assertEqual(250, ts.in_count)
        self.assertEqual(250, ts.out_count)

    def test_credit_starve(self):
        ts = TestCreditStarve(self.router.addresses[0])
        ts.run()
        self.assertTrue(ts.error is None)
        self.assertTrue(ts.starved)
        self.assertEqual(10, ts.in_count)

    def test_unexpected_conn_close(self):
        ts = TestEarlyClose(self.router.addresses[0])
        ts.run()
        self.assertTrue(ts.error is None)
        self.assertTrue(ts.in_count >= 1)

    def test_bad_format(self):
        ts = TestNoCorrelationId(self.router.addresses[0])
        ts.run()
        self.assertTrue(ts.error is None)
        self.assertTrue(ts.rejected)

    def test_old_cid(self):
        ts = TestOldCorrelationId(self.router.addresses[0])
        ts.run()
        self.assertTrue(ts.error is None)
        self.assertTrue(ts.accepted)

    def test_call_timeout(self):
        qm = QdManager(self.router.addresses[0])
        ts = TestCallTimeout(self.router.addresses[0], qm)
        ts.run()
        self.assertEqual("TIMED OUT!", ts.error)


class TestService(MessagingHandler):
    """a service that the core client can communicate with"""
    __test__ = False

    class Timeout:
        def __init__(self, service):
            self.service = service

        def on_timer_task(self, event):
            self.service.timeout()

    def __init__(self, address, container_id=CONTAINER_ID, credit=1):
        super(TestService, self).__init__(prefetch=0)
        self._container = Container(self)
        self._container.container_id = CONTAINER_ID
        self._conn = None
        self.address = address
        self.timer = None
        self.error = None
        self.reply_link = None
        self.incoming_link = None
        self.credit = credit
        self.in_count = 0
        self.out_count = 0

    def fail(self, error):
        self.error = error
        if self._conn:
            self._conn.close()

    def timeout(self):
        self.fail("Timeout expired")

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, self.Timeout(self))
        self._conn = event.container.connect(self.address)

    def on_link_opening(self, event):
        link = event.link
        if link.state & Endpoint.LOCAL_UNINIT:
            link.source.copy(link.remote_source)
            link.target.copy(link.remote_target)
        if event.sender:
            if not link.remote_source.dynamic:
                self.fail("expected dynamic source terminus")
                return
            link.source.dynamic = False
            link.source.address = "a/reply/address"
            self.reply_link = link
        else:
            link.flow(self.credit)
            self.incoming_link = link

    def create_reply(self, message):
        return Message(body=message.body,
                       correlation_id=message.correlation_id)

    # echo back to sender
    def on_message(self, event):
        self.in_count += 1
        cid = event.message.correlation_id
        self.reply_link.send(self.create_reply(event.message))

    # stop when all sent messages have settled
    def on_settled(self, event):
        self.out_count += 1
        self.credit -= 1
        if self.credit == 0:
            self._conn.close()

    def on_connection_closed(self, event):
        self._conn = None

    def run(self):
        self._container.timeout = 1.0
        self._container.start()
        while self._container.process():
            if self._conn is None and self._container.quiesced:
                break
        self._container.stop()
        self._container.process()


class TestCreditStarve(TestService):
    """wait until all credit is exhausted, then re-flow more credit"""
    __test__ = False

    def __init__(self, address):
        super(TestCreditStarve, self).__init__(address, credit=5)
        self.starved = False

    def on_settled(self, event):
        self.credit -= 1
        if self.credit == 0:
            if not self.starved:
                self.starved = True
                self.credit = 5
                self.incoming_link.drain(self.credit)
            else:
                self._conn.close()


class TestEarlyClose(TestService):
    """grant 10, but don't respond and close early"""
    __test__ = False

    def __init__(self, address):
        super(TestEarlyClose, self).__init__(address, credit=10)

    def on_message(self, event):
        self.in_count += 1
        if self.in_count == 1:
            self._conn.close()


class TestNoCorrelationId(TestService):
    __test__ = False

    def __init__(self, address):
        super(TestNoCorrelationId, self).__init__(address, credit=1)
        self.rejected = False

    def create_reply(self, message):
        return Message(body=dict())

    def on_rejected(self, event):
        self.rejected = True


class TestOldCorrelationId(TestService):
    __test__ = False

    def __init__(self, address):
        super(TestOldCorrelationId, self).__init__(address, credit=1)
        self.accepted = False

    def create_reply(self, message):
        return Message(body=dict(),
                       correlation_id="not going to match")

    def on_accepted(self, event):
        self.accepted = True


class TestCallTimeout(TestService):
    """test that the timeout is handled properly"""
    __test__ = False

    class PeriodicLogScrape:
        # periodically scan the log for the timeout error
        def __init__(self, service):
            self.service = service

        def on_timer_task(self, event):
            log = self.service.qm.get_log()
            for e in log:
                if (e[0] == 'ROUTER_CORE' and e[1] == 'error'
                        and e[2] == 'client test request done '
                                    'error=Timed out'):
                    # yes this is the line you're looking for:
                    self.service.error = "TIMED OUT!"
                    if self.service._conn:
                        self.service._conn.close()
                    return
            event.reactor.schedule(1, TestCallTimeout.PeriodicLogScrape(self.service))

    def __init__(self, address, qm):
        super(TestCallTimeout, self).__init__(address, credit=1)
        self.qm = qm

    def on_message(self, event):
        # drop it
        event.reactor.schedule(1, self.PeriodicLogScrape(self))

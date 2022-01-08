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

from system_test import TestCase, Qdrouterd, main_module, TIMEOUT, \
    MgmtMsgProxy, TestTimeout, PollTimeout
from system_test import unittest
from proton.handlers import MessagingHandler
from proton.reactor import Container


class AddrTimer:
    def __init__(self, parent):
        self.parent = parent

    def on_timer_task(self, event):
        self.parent.check_address()


class RouterTest(TestCase):

    inter_router_port = None

    @classmethod
    def setUpClass(cls):
        """Start a router"""
        super(RouterTest, cls).setUpClass()

        def router(name, mode, connection=None, extra=None):
            config = [
                ('router', {'mode': mode, 'id': name}),
                ('listener', {'port': cls.tester.get_port(), 'stripAnnotations': 'no'}),
                ('address', {'prefix': 'closest', 'distribution': 'closest'}),
                ('address', {'prefix': 'spread', 'distribution': 'balanced'}),
                ('address', {'prefix': 'multicast', 'distribution': 'multicast'})
            ]

            if connection:
                config.append(connection)

            if extra:
                config.append(extra)

            config = Qdrouterd.Config(config)
            cls.routers.append(cls.tester.qdrouterd(name, config, wait=True))

        cls.routers = []

        inter_router_port = cls.tester.get_port()
        edge_port_A       = cls.tester.get_port()
        edge_port_B       = cls.tester.get_port()

        router('INT.A', 'interior')
        router('INT.B', 'interior',
               ('listener', {'role': 'inter-router', 'port': inter_router_port}))

    def test_interior_sync_up(self):
        test = InteriorSyncUpTest(self.routers[0].addresses[0], self.routers[1].addresses[0], self.routers[1].ports[1])
        test.run()
        self.assertIsNone(test.error)


class DelayTimeout:
    def __init__(self, parent):
        self.parent = parent

    def on_timer_task(self, event):
        self.parent.delay_timeout()


class InteriorSyncUpTest(MessagingHandler):
    def __init__(self, host_a, host_b, inter_router_port):
        """
        This test verifies that a router can join an existing network and be synced up using
        an absolute MAU (as opposed to an incremental MAU).  This requires that the exiting
        network have sequenced through mobile address changes at least 10 times to avoid the
        cached incremental update optimization that the routers use.
        """
        super(InteriorSyncUpTest, self).__init__()
        self.host_a            = host_a
        self.host_b            = host_b
        self.timer             = None
        self.poll_timer        = None
        self.delay_timer       = None
        self.count             = 2000
        self.delay_count       = 12   # This should be larger than MAX_KEPT_DELTAS in mobile.py
        self.inter_router_port = inter_router_port

        self.receivers      = []
        self.n_receivers    = 0
        self.n_setup_delays = 0
        self.error          = None
        self.last_action    = "test initialization"
        self.expect         = ""

    def fail(self, text):
        self.error = text
        self.conn_a.close()
        self.conn_b.close()
        self.timer.cancel()
        if self.poll_timer:
            self.poll_timer.cancel()
        if self.delay_timer:
            self.delay_timer.cancel()

    def timeout(self):
        self.error = "Timeout Expired - last_action: %s, n_receivers: %d, n_setup_delays: %d" % \
                     (self.last_action, self.n_receivers, self.n_setup_delays)
        self.conn_a.close()
        self.conn_b.close()
        if self.poll_timer:
            self.poll_timer.cancel()
        if self.delay_timer:
            self.delay_timer.cancel()

    def poll_timeout(self):
        self.probe()

    def delay_timeout(self):
        self.n_setup_delays += 1
        self.add_receivers()

    def add_receivers(self):
        if len(self.receivers) < self.count:
            self.receivers.append(self.container.create_receiver(self.conn_b, "address.%d" % len(self.receivers)))
        if self.n_setup_delays < self.delay_count:
            self.delay_timer = self.reactor.schedule(2.0, DelayTimeout(self))
        else:
            while len(self.receivers) < self.count:
                self.receivers.append(self.container.create_receiver(self.conn_b, "address.%d" % len(self.receivers)))

    def on_start(self, event):
        self.container      = event.container
        self.reactor        = event.reactor
        self.timer          = self.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.conn_a         = self.container.connect(self.host_a)
        self.conn_b         = self.container.connect(self.host_b)
        self.probe_receiver = self.container.create_receiver(self.conn_a, dynamic=True)
        self.last_action = "on_start - opened connections"

    def probe(self):
        self.probe_sender.send(self.proxy.query_addresses())

    def on_link_opened(self, event):
        if event.receiver == self.probe_receiver:
            self.probe_reply  = self.probe_receiver.remote_source.address
            self.proxy        = MgmtMsgProxy(self.probe_reply)
            self.probe_sender = self.container.create_sender(self.conn_a, '$management')
        elif event.sender == self.probe_sender:
            ##
            # Create listeners for an address per count
            ##
            self.add_receivers()
            self.last_action = "started slow creation of receivers"
        elif event.receiver in self.receivers:
            self.n_receivers += 1
            if self.n_receivers == self.count:
                self.expect = "not-found"
                self.probe()
                self.last_action = "started probe expecting addresses not found"

    def on_message(self, event):
        if event.receiver == self.probe_receiver:
            response = self.proxy.response(event.message)

            if response.status_code < 200 or response.status_code > 299:
                self.fail("Unexpected operation failure: (%d) %s" % (response.status_code, response.status_description))

            if self.expect == "not-found":
                response = self.proxy.response(event.message)
                for addr in response.results:
                    if "address." in addr.name:
                        self.fail("Found address on host-a when we didn't expect it - %s" % addr.name)

                ##
                # Hook up the two routers to start the sync-up
                ##
                self.probe_sender.send(self.proxy.create_connector("IR", port=self.inter_router_port, role="inter-router"))
                self.expect      = "create-success"
                self.last_action = "created inter-router connector"

            elif self.expect == "create-success":
                ##
                # Start polling for the addresses on host_a
                ##
                response  = self.proxy.response(event.message)
                self.probe_sender.send(self.proxy.query_addresses())
                self.expect      = "query-success"
                self.last_action = "started probing host_a for addresses"

            elif self.expect == "query-success":
                response  = self.proxy.response(event.message)
                got_count = 0
                for addr in response.results:
                    if "address." in addr.name:
                        got_count += 1

                self.last_action = "Got a query response with %d of the expected addresses" % (got_count)

                if got_count == self.count:
                    self.fail(None)
                else:
                    self.poll_timer = self.reactor.schedule(0.5, PollTimeout(self))

    def run(self):
        container = Container(self)
        container.run()


if __name__ == '__main__':
    unittest.main(main_module())

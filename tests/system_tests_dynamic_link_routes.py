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
from proton import Message, PENDING, ACCEPTED, REJECTED, RELEASED, SSLDomain, SSLUnavailable, Timeout, symbol
from system_test import TestCase, Qdrouterd, main_module, DIR, Process
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

        def router(name, connection):

            config = [
                ('router', {'mode': 'interior', 'id': name}),
                ('listener', {'port': cls.tester.get_port(), 'stripAnnotations': 'no'}),
                ('listener', {'port': cls.tester.get_port(), 'stripAnnotations': 'no', 'multiTenant': 'yes'}),
                ('listener', {'port': cls.tester.get_port(), 'stripAnnotations': 'no', 'role': 'route-container'}),
                ('address', {'prefix': 'closest', 'distribution': 'closest'}),
                ('address', {'prefix': 'spread', 'distribution': 'balanced'}),
                ('address', {'prefix': 'multicast', 'distribution': 'multicast'}),
                connection
            ]

            config = Qdrouterd.Config(config)

            cls.routers.append(cls.tester.qdrouterd(name, config, wait=True))

        cls.routers = []

        inter_router_port = cls.tester.get_port()

        router('A', ('listener', {'role': 'inter-router', 'port': inter_router_port}))
        router('B', ('connector', {'name': 'connectorToA', 'role': 'inter-router', 'port': inter_router_port, 'verifyHostName': 'no'}))

        cls.routers[0].wait_router_connected('B')
        cls.routers[1].wait_router_connected('A')


    def test_01_single_incoming_prefix(self):
        test = DynamicLinkRouteTest(self.routers[0].addresses[0],
                                    self.routers[0].addresses[0],
                                    self.routers[1].addresses[0],
                                    {symbol('qd.link-route-patterns'):[u'linkRoute.prefix.#', symbol('in')]},
                                    [u'ClinkRoute.prefix'])
        test.run()
        self.assertEqual(None, test.error)


    def test_02_single_outgoing_prefix(self):
        test = DynamicLinkRouteTest(self.routers[0].addresses[0],
                                    self.routers[0].addresses[0],
                                    self.routers[1].addresses[0],
                                    {symbol('qd.link-route-patterns'):[u'linkRoute.prefix.#', symbol('out')]},
                                    [u'DlinkRoute.prefix'])
        test.run()
        self.assertEqual(None, test.error)


    def test_03_single_inout_prefix(self):
        test = DynamicLinkRouteTest(self.routers[0].addresses[0],
                                    self.routers[0].addresses[0],
                                    self.routers[1].addresses[0],
                                    {symbol('qd.link-route-patterns'):[u'linkRoute.prefix.#', symbol('inout')]},
                                    [u'ClinkRoute.prefix', u'DlinkRoute.prefix'])
        test.run()
        self.assertEqual(None, test.error)


    def test_04_single_incoming_pattern(self):
        test = DynamicLinkRouteTest(self.routers[0].addresses[0],
                                    self.routers[0].addresses[0],
                                    self.routers[1].addresses[0],
                                    {symbol('qd.link-route-patterns'):[u'linkRoute.*.pattern.#', symbol('in')]},
                                    [u'ElinkRoute.*.pattern.#'])
        test.run()
        self.assertEqual(None, test.error)


    def test_05_single_outgoing_pattern(self):
        test = DynamicLinkRouteTest(self.routers[0].addresses[0],
                                    self.routers[0].addresses[0],
                                    self.routers[1].addresses[0],
                                    {symbol('qd.link-route-patterns'):[u'linkRoute.*.pattern.#', symbol('out')]},
                                    [u'FlinkRoute.*.pattern.#'])
        test.run()
        self.assertEqual(None, test.error)


    def test_06_single_inout_pattern(self):
        test = DynamicLinkRouteTest(self.routers[0].addresses[0],
                                    self.routers[0].addresses[0],
                                    self.routers[1].addresses[0],
                                    {symbol('qd.link-route-patterns'):[u'linkRoute.*.pattern.#', symbol('inout')]},
                                    [u'ElinkRoute.*.pattern.#', u'FlinkRoute.*.pattern.#'])
        test.run()
        self.assertEqual(None, test.error)


    def test_07_pattern_in_flat_noise(self):
        test = DynamicLinkRouteTest(self.routers[0].addresses[0],
                                    self.routers[0].addresses[0],
                                    self.routers[1].addresses[0],
                                    {symbol('earlier-property'):u'value',
                                     symbol('qd.link-route-patterns'):[u'linkRoute.*.pattern.#', symbol('inout')],
                                     symbol('later-property'):u'value'},
                                    [u'ElinkRoute.*.pattern.#', u'FlinkRoute.*.pattern.#'])
        test.run()
        self.assertEqual(None, test.error)


    def test_08_pattern_in_deep_noise(self):
        test = DynamicLinkRouteTest(self.routers[0].addresses[0],
                                    self.routers[0].addresses[0],
                                    self.routers[1].addresses[0],
                                    {symbol('earlier-property'):{symbol('first-sub'):u'value',
                                                                 symbol('second-sub'):45},
                                     symbol('qd.link-route-patterns'):[u'linkRoute.*.pattern.#', symbol('inout')],
                                     symbol('later-property'):[u'one', u'two', u'three']},
                                    [u'ElinkRoute.*.pattern.#', u'FlinkRoute.*.pattern.#'])
        test.run()
        self.assertEqual(None, test.error)


    def test_09_prefix_on_route_container(self):
        test = DynamicLinkRouteTest(self.routers[0].addresses[2],
                                    self.routers[0].addresses[0],
                                    self.routers[1].addresses[0],
                                    {symbol('qd.link-route-patterns'):[u'linkRoute.prefix.#', symbol('in')]},
                                    [u'ClinkRoute.prefix'])
        test.run()
        self.assertEqual(None, test.error)


    def test_10_multiple_patterns(self):
        test = DynamicLinkRouteTest(self.routers[0].addresses[0],
                                    self.routers[0].addresses[0],
                                    self.routers[1].addresses[0],
                                    {symbol('qd.link-route-patterns'):[u'linkRoute.prefix.#', symbol('in'),
                                                                       u'some.*.pattern.#.end', symbol('out'),
                                                                       u'another.pattern.*.x', symbol('inout')]},
                                    [u'ClinkRoute.prefix', u'Fsome.*.pattern.#.end',
                                     u'Eanother.pattern.*.x', u'Fanother.pattern.*.x'])
        test.run()
        self.assertEqual(None, test.error)


    def test_11_prefix_with_multi_tenancy(self):
        test = DynamicLinkRouteTest(self.routers[0].addresses[1],
                                    self.routers[0].addresses[0],
                                    self.routers[1].addresses[0],
                                    {symbol('qd.link-route-patterns'):[u'linkRoute.prefix.#', symbol('inout')]},
                                    [u'C0.0.0.0/linkRoute.prefix', u'D0.0.0.0/linkRoute.prefix'])
        test.run()
        self.assertEqual(None, test.error)


    def test_12_pattern_with_multi_tenancy(self):
        test = DynamicLinkRouteTest(self.routers[0].addresses[1],
                                    self.routers[0].addresses[0],
                                    self.routers[1].addresses[0],
                                    {symbol('qd.link-route-patterns'):[u'linkRoute.#.prefix.#', symbol('inout')]},
                                    [u'E0.0.0.0/linkRoute.#.prefix.#', u'F0.0.0.0/linkRoute.#.prefix.#'])
        test.run()
        self.assertEqual(None, test.error)


class Entity(object):
    def __init__(self, status_code, status_description, attrs):
        self.status_code        = status_code
        self.status_description = status_description
        self.attrs              = attrs

    def __getattr__(self, key):
        return self.attrs[key]


class RouterProxy(object):
    def __init__(self, reply_addr):
        self.reply_addr = reply_addr

    def response(self, msg):
        ap = msg.properties
        return Entity(ap['statusCode'], ap['statusDescription'], msg.body)

    def read_address(self, name):
        ap = {'operation': 'READ', 'type': 'org.apache.qpid.dispatch.router.address', 'name': name}
        return Message(properties=ap, reply_to=self.reply_addr)

    def query_addresses(self):
        ap = {'operation': 'QUERY', 'type': 'org.apache.qpid.dispatch.router.address'}
        return Message(properties=ap, reply_to=self.reply_addr)


class Timeout(object):
    def __init__(self, parent):
        self.parent = parent

    def on_timer_task(self, event):
        self.parent.timeout()


class PollTimeout(object):
    def __init__(self, parent):
        self.parent = parent

    def on_timer_task(self, event):
        self.parent.poll_timeout()


class DynamicLinkRouteTest(MessagingHandler):
    def __init__(self, local_host, local_mgmt_host, remote_mgmt_host, properties, expected_addresses):
        super(DynamicLinkRouteTest, self).__init__()
        self.local_host       = local_host
        self.local_mgmt_host  = local_mgmt_host
        self.remote_mgmt_host = remote_mgmt_host
        self.properties       = properties
        self.expected_local   = expected_addresses
        self.expected_remote  = expected_addresses[:]  # Make a copy
        self.removed_local    = expected_addresses[:]  # Make a copy
        self.removed_remote   = expected_addresses[:]  # Make a copy

        self.local_conn       = None
        self.local_mgmt_conn  = None
        self.remote_mgmt_conn = None
        self.error            = None
        self.proxy            = None
        self.timer            = None
        self.poll_timer       = None

    def timeout(self):
        self.error = "Timeout Expired - local: %r  remote: %r  rlocal: %r  rremote: %r" %\
                     (self.expected_local, self.expected_remote, self.removed_local, self.removed_remote)
        if len(self.expected_local) + len(self.expected_remote) > 0:
            self.local_conn.close()
        self.local_mgmt_conn.close()
        self.remote_mgmt_conn.close()
        if self.poll_timer:
            self.poll_timer.cancel()

    def poll_timeout(self):
        self.poll_timer = None
        self.send_management_request()

    def send_management_request(self):
        if len(self.expected_local) > 0:
            message = self.proxy.read_address(self.expected_local[0])
            self.local_sender.send(message)
            return
        
        if len(self.expected_remote) > 0:
            message = self.proxy.read_address(self.expected_remote[0])
            self.remote_sender.send(message)
            return

        if len(self.removed_local) > 0:
            message = self.proxy.read_address(self.removed_local[0])
            self.local_sender.send(message)
            return
        
        if len(self.removed_remote) > 0:
            message = self.proxy.read_address(self.removed_remote[0])
            self.remote_sender.send(message)
            return

        self.local_mgmt_conn.close()
        self.remote_mgmt_conn.close()
        self.timer.cancel()

    def on_start(self, event):
        self.timer            = event.reactor.schedule(15.0, Timeout(self))
        self.local_conn       = event.container.connect(self.local_host, properties=self.properties)
        self.local_mgmt_conn  = event.container.connect(self.local_mgmt_host)
        self.remote_mgmt_conn = event.container.connect(self.remote_mgmt_host)
        self.reply_receiver   = event.container.create_receiver(self.local_mgmt_conn, dynamic=True)

    def on_link_opened(self, event):
        if event.receiver == self.reply_receiver:
            self.proxy         = RouterProxy(self.reply_receiver.remote_source.address)
            self.local_sender  = event.container.create_sender(self.local_mgmt_conn,  "$management")
            self.remote_sender = event.container.create_sender(self.remote_mgmt_conn, "$management")
            self.send_management_request()

    def on_message(self, event):
        if event.receiver == self.reply_receiver:
            response = self.proxy.response(event.message)
            if len(self.expected_local) > 0:
                if response.status_code == 200:
                    if response.name == self.expected_local[0] and response.containerCount == 1 and response.remoteCount == 0:
                        self.expected_local.pop(0)
                        self.send_management_request()
                        return
            elif len(self.expected_remote) > 0:
                if response.status_code == 200:
                    if response.name == self.expected_remote[0] and response.containerCount == 0 and response.remoteCount == 1:
                        self.expected_remote.pop(0)
                        self.send_management_request()
                        if len(self.expected_remote) == 0:
                            self.local_conn.close()
                        return
            elif len(self.removed_local) > 0:
                if response.status_code == 404:
                    self.removed_local.pop(0)
                    self.send_management_request()
                    return
            elif len(self.removed_remote) > 0:
                if response.status_code == 404:
                    self.removed_remote.pop(0)
                    self.send_management_request()
                    return
            self.poll_timer = event.reactor.schedule(0.1, PollTimeout(self))

    def run(self):
        Container(self).run()


if __name__ == '__main__':
    unittest.main(main_module())

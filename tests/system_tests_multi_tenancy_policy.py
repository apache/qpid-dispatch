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
from proton.reactor import Container, DynamicNodeProperties

from qpid_dispatch_internal.compat import UNICODE

from system_test import TestCase, Qdrouterd, main_module, TIMEOUT, unittest, TestTimeout, PollTimeout, Logger


class RouterMultitenantPolicyTest(TestCase):

    inter_router_port = None

    @classmethod
    def setUpClass(cls):
        """Start a router"""
        super(RouterMultitenantPolicyTest, cls).setUpClass()

        def router(name, connection):

            config = [
                ('router', {'mode': 'interior', 'id': name}),
                ('listener', {'port': cls.tester.get_port(), 'stripAnnotations': 'no'}),
                ('listener', {'port': cls.tester.get_port(), 'stripAnnotations': 'no', 'multiTenant': 'yes'}),
                ('listener', {'port': cls.tester.get_port(), 'stripAnnotations': 'no', 'role': 'route-container'}),
                ('linkRoute', {'prefix': 'hosted-group-1/link', 'direction': 'in', 'containerId': 'LRC'}),
                ('linkRoute', {'prefix': 'hosted-group-1/link', 'direction': 'out', 'containerId': 'LRC'}),
                ('autoLink', {'address': 'hosted-group-1/queue.waypoint', 'containerId': 'ALC', 'direction': 'in'}),
                ('autoLink', {'address': 'hosted-group-1/queue.waypoint', 'containerId': 'ALC', 'direction': 'out'}),
                ('autoLink', {'address': 'hosted-group-1/queue.ext', 'containerId': 'ALCE', 'direction': 'in', 'externalAddress': 'EXT'}),
                ('autoLink', {'address': 'hosted-group-1/queue.ext', 'containerId': 'ALCE', 'direction': 'out', 'externalAddress': 'EXT'}),
                ('address', {'prefix': 'closest', 'distribution': 'closest'}),
                ('address', {'prefix': 'spread', 'distribution': 'balanced'}),
                ('address', {'prefix': 'multicast', 'distribution': 'multicast'}),
                ('address', {'prefix': 'hosted-group-1/queue', 'waypoint': 'yes'}),
                ('policy', {'enableVhostPolicy': 'true'}),
                ('vhost', {'hostname': 'hosted-group-1',
                           'allowUnknownUser': 'true',
                           'aliases': '0.0.0.0',
                           'groups': {
                               '$default': {
                                   'users': '*',
                                   'maxConnections': 100,
                                   'remoteHosts': '*',
                                   'sources': '*',
                                   'targets': '*',
                                   'allowAnonymousSender': 'true',
                                   'allowWaypointLinks': 'true',
                                   'allowDynamicSource': 'true'
                               }
                           }
                           }),
                connection
            ]

            config = Qdrouterd.Config(config)

            cls.routers.append(cls.tester.qdrouterd(name, config, wait=True))

        cls.routers = []

        inter_router_port = cls.tester.get_port()

        router('A', ('listener', {'role': 'inter-router', 'port': inter_router_port}))
        router('B', ('connector', {'name': 'connectorToA', 'role': 'inter-router', 'port': inter_router_port}))

        cls.routers[0].wait_router_connected('B')
        cls.routers[1].wait_router_connected('A')

    def test_01_one_router_targeted_sender_no_tenant(self):
        test = MessageTransferTest(self.routers[0].addresses[0],
                                   self.routers[0].addresses[0],
                                   "anything/addr_01",
                                   "anything/addr_01",
                                   self.routers[0].addresses[0],
                                   "M0anything/addr_01")
        test.run()
        self.assertIsNone(test.error)

    def test_02_one_router_targeted_sender_tenant_on_sender(self):
        test = MessageTransferTest(self.routers[0].addresses[1],
                                   self.routers[0].addresses[0],
                                   "addr_02",
                                   "hosted-group-1/addr_02",
                                   self.routers[0].addresses[0],
                                   "M0hosted-group-1/addr_02")
        test.run()
        self.assertIsNone(test.error)

    def test_03_one_router_targeted_sender_tenant_on_receiver(self):
        test = MessageTransferTest(self.routers[0].addresses[0],
                                   self.routers[0].addresses[1],
                                   "hosted-group-1/addr_03",
                                   "addr_03",
                                   self.routers[0].addresses[0],
                                   "M0hosted-group-1/addr_03")
        test.run()
        self.assertIsNone(test.error)

    def test_04_one_router_targeted_sender_tenant_on_both(self):
        test = MessageTransferTest(self.routers[0].addresses[1],
                                   self.routers[0].addresses[1],
                                   "addr_04",
                                   "addr_04",
                                   self.routers[0].addresses[0],
                                   "M0hosted-group-1/addr_04")
        test.run()
        self.assertIsNone(test.error)

    def test_05_two_router_targeted_sender_no_tenant(self):
        test = MessageTransferTest(self.routers[0].addresses[0],
                                   self.routers[1].addresses[0],
                                   "hosted-group-1/addr_05",
                                   "hosted-group-1/addr_05",
                                   self.routers[0].addresses[0],
                                   "M0hosted-group-1/addr_05")
        test.run()
        self.assertIsNone(test.error)

    def test_06_two_router_targeted_sender_tenant_on_sender(self):
        test = MessageTransferTest(self.routers[0].addresses[1],
                                   self.routers[1].addresses[0],
                                   "addr_06",
                                   "hosted-group-1/addr_06",
                                   self.routers[0].addresses[0],
                                   "M0hosted-group-1/addr_06")
        test.run()
        self.assertIsNone(test.error)

    def test_07_two_router_targeted_sender_tenant_on_receiver(self):
        test = MessageTransferTest(self.routers[0].addresses[0],
                                   self.routers[1].addresses[1],
                                   "hosted-group-1/addr_07",
                                   "addr_07",
                                   self.routers[0].addresses[0],
                                   "M0hosted-group-1/addr_07")
        test.run()
        self.assertIsNone(test.error)

    def test_08_two_router_targeted_sender_tenant_on_both(self):
        test = MessageTransferTest(self.routers[0].addresses[1],
                                   self.routers[1].addresses[1],
                                   "addr_08",
                                   "addr_08",
                                   self.routers[0].addresses[0],
                                   "M0hosted-group-1/addr_08")
        test.run()
        self.assertIsNone(test.error)

    def test_09_one_router_anonymous_sender_no_tenant(self):
        test = MessageTransferAnonTest(self.routers[0].addresses[0],
                                       self.routers[0].addresses[0],
                                       "anything/addr_09",
                                       "anything/addr_09",
                                       self.routers[0].addresses[0],
                                       "M0anything/addr_09")
        test.run()
        self.assertIsNone(test.error)

    def test_10_one_router_anonymous_sender_tenant_on_sender(self):
        test = MessageTransferAnonTest(self.routers[0].addresses[1],
                                       self.routers[0].addresses[0],
                                       "addr_10",
                                       "hosted-group-1/addr_10",
                                       self.routers[0].addresses[0],
                                       "M0hosted-group-1/addr_10")
        test.run()
        self.assertIsNone(test.error)

    def test_11_one_router_anonymous_sender_tenant_on_receiver(self):
        test = MessageTransferAnonTest(self.routers[0].addresses[0],
                                       self.routers[0].addresses[1],
                                       "hosted-group-1/addr_11",
                                       "addr_11",
                                       self.routers[0].addresses[0],
                                       "M0hosted-group-1/addr_11")
        test.run()
        self.assertIsNone(test.error)

    def test_12_one_router_anonymous_sender_tenant_on_both(self):
        test = MessageTransferAnonTest(self.routers[0].addresses[1],
                                       self.routers[0].addresses[1],
                                       "addr_12",
                                       "addr_12",
                                       self.routers[0].addresses[0],
                                       "M0hosted-group-1/addr_12")
        test.run()
        self.assertIsNone(test.error)

    def test_13_two_router_anonymous_sender_no_tenant(self):
        test = MessageTransferAnonTest(self.routers[0].addresses[0],
                                       self.routers[1].addresses[0],
                                       "anything/addr_13",
                                       "anything/addr_13",
                                       self.routers[0].addresses[0],
                                       "M0anything/addr_13")
        test.run()
        self.assertIsNone(test.error)

    def test_14_two_router_anonymous_sender_tenant_on_sender(self):
        test = MessageTransferAnonTest(self.routers[0].addresses[1],
                                       self.routers[1].addresses[0],
                                       "addr_14",
                                       "hosted-group-1/addr_14",
                                       self.routers[0].addresses[0],
                                       "M0hosted-group-1/addr_14")
        test.run()
        self.assertIsNone(test.error)

    def test_15_two_router_anonymous_sender_tenant_on_receiver(self):
        test = MessageTransferAnonTest(self.routers[0].addresses[0],
                                       self.routers[1].addresses[1],
                                       "hosted-group-1/addr_15",
                                       "addr_15",
                                       self.routers[0].addresses[0],
                                       "M0hosted-group-1/addr_15")
        test.run()
        self.assertIsNone(test.error)

    def test_16_two_router_anonymous_sender_tenant_on_both(self):
        test = MessageTransferAnonTest(self.routers[0].addresses[1],
                                       self.routers[1].addresses[1],
                                       "addr_16",
                                       "addr_16",
                                       self.routers[0].addresses[0],
                                       "M0hosted-group-1/addr_16")
        test.run()
        self.assertIsNone(test.error)

    def test_17_one_router_link_route_targeted(self):
        test = LinkRouteTest(self.routers[0].addresses[1],
                             self.routers[0].addresses[2],
                             "link.addr_17",
                             "hosted-group-1/link.addr_17",
                             False,
                             self.routers[0].addresses[0])
        test.run()
        self.assertIsNone(test.error)

    def test_18_one_router_link_route_targeted_no_tenant(self):
        test = LinkRouteTest(self.routers[0].addresses[0],
                             self.routers[0].addresses[2],
                             "hosted-group-1/link.addr_18",
                             "hosted-group-1/link.addr_18",
                             False,
                             self.routers[0].addresses[0])
        test.run()
        self.assertIsNone(test.error)

    def test_19_one_router_link_route_dynamic(self):
        test = LinkRouteTest(self.routers[0].addresses[1],
                             self.routers[0].addresses[2],
                             "link.addr_19",
                             "hosted-group-1/link.addr_19",
                             True,
                             self.routers[0].addresses[0])
        test.run()
        self.assertIsNone(test.error)

    def test_20_one_router_link_route_dynamic_no_tenant(self):
        test = LinkRouteTest(self.routers[0].addresses[0],
                             self.routers[0].addresses[2],
                             "hosted-group-1/link.addr_20",
                             "hosted-group-1/link.addr_20",
                             True,
                             self.routers[0].addresses[0])
        test.run()
        self.assertIsNone(test.error)

    def test_21_two_router_link_route_targeted(self):
        test = LinkRouteTest(self.routers[0].addresses[1],
                             self.routers[1].addresses[2],
                             "link.addr_21",
                             "hosted-group-1/link.addr_21",
                             False,
                             self.routers[0].addresses[0])
        test.run()
        self.assertIsNone(test.error)

    def test_22_two_router_link_route_targeted_no_tenant(self):
        test = LinkRouteTest(self.routers[0].addresses[0],
                             self.routers[1].addresses[2],
                             "hosted-group-1/link.addr_22",
                             "hosted-group-1/link.addr_22",
                             False,
                             self.routers[0].addresses[0])
        test.run()
        self.assertIsNone(test.error)

    def test_23_two_router_link_route_dynamic(self):
        test = LinkRouteTest(self.routers[0].addresses[1],
                             self.routers[1].addresses[2],
                             "link.addr_23",
                             "hosted-group-1/link.addr_23",
                             True,
                             self.routers[0].addresses[0])
        test.run()
        self.assertIsNone(test.error)

    def test_24_two_router_link_route_dynamic_no_tenant(self):
        test = LinkRouteTest(self.routers[0].addresses[0],
                             self.routers[1].addresses[2],
                             "hosted-group-1/link.addr_24",
                             "hosted-group-1/link.addr_24",
                             True,
                             self.routers[0].addresses[0])
        test.run()
        self.assertIsNone(test.error)

    def test_25_one_router_anonymous_sender_non_mobile(self):
        test = MessageTransferAnonTest(self.routers[0].addresses[1],
                                       self.routers[0].addresses[0],
                                       "_local/addr_25",
                                       "_local/addr_25",
                                       self.routers[0].addresses[0],
                                       "Laddr_25")
        test.run()
        self.assertIsNone(test.error)

    def test_26_one_router_targeted_sender_non_mobile(self):
        test = MessageTransferTest(self.routers[0].addresses[1],
                                   self.routers[0].addresses[0],
                                   "_local/addr_26",
                                   "_local/addr_26",
                                   self.routers[0].addresses[0],
                                   "Laddr_26")
        test.run()
        self.assertIsNone(test.error)

    def test_27_two_router_anonymous_sender_non_mobile(self):
        test = MessageTransferAnonTest(self.routers[0].addresses[1],
                                       self.routers[1].addresses[0],
                                       "_topo/0/B/addr_27",
                                       "_local/addr_27",
                                       self.routers[1].addresses[0],
                                       "Laddr_27")
        test.run()
        self.assertIsNone(test.error)

    def test_28_two_router_targeted_sender_non_mobile(self):
        test = MessageTransferTest(self.routers[0].addresses[1],
                                   self.routers[1].addresses[0],
                                   "_topo/0/B/addr_28",
                                   "_local/addr_28",
                                   self.routers[1].addresses[0],
                                   "Laddr_28")
        test.run()
        self.assertIsNone(test.error)

    def test_29_one_router_waypoint_no_tenant(self):
        test = WaypointTest(self.routers[0].addresses[0],
                            self.routers[0].addresses[2],
                            "hosted-group-1/queue.waypoint",
                            "hosted-group-1/queue.waypoint")
        test.run()
        # Dump the logger output only if there is a test error, otherwise dont bother
        if test.error:
            test.logger.dump()
        self.assertIsNone(test.error)

    def test_30_one_router_waypoint(self):
        test = WaypointTest(self.routers[0].addresses[1],
                            self.routers[0].addresses[2],
                            "queue.waypoint",
                            "hosted-group-1/queue.waypoint")
        test.run()
        # Dump the logger output only if there is a test error, otherwise dont bother
        if test.error:
            test.logger.dump()
        self.assertIsNone(test.error)

    def test_31_two_router_waypoint_no_tenant(self):
        test = WaypointTest(self.routers[0].addresses[0],
                            self.routers[1].addresses[2],
                            "hosted-group-1/queue.waypoint",
                            "hosted-group-1/queue.waypoint")
        test.run()
        # Dump the logger output only if there is a test error, otherwise dont bother
        if test.error:
            test.logger.dump()
        self.assertIsNone(test.error)

    def test_32_two_router_waypoint(self):
        test = WaypointTest(self.routers[0].addresses[1],
                            self.routers[1].addresses[2],
                            "queue.waypoint",
                            "hosted-group-1/queue.waypoint")
        test.run()
        # Dump the logger output only if there is a test error, otherwise dont bother
        if test.error:
            test.logger.dump()
        self.assertIsNone(test.error)

    def test_33_one_router_waypoint_no_tenant_external_addr(self):
        test = WaypointTest(self.routers[0].addresses[0],
                            self.routers[0].addresses[2],
                            "hosted-group-1/queue.ext",
                            "EXT",
                            "ALCE")
        test.run()
        # Dump the logger output only if there is a test error, otherwise dont bother
        if test.error:
            test.logger.dump()
        self.assertIsNone(test.error)

    def test_34_one_router_waypoint_external_addr(self):
        test = WaypointTest(self.routers[0].addresses[1],
                            self.routers[0].addresses[2],
                            "queue.ext",
                            "EXT",
                            "ALCE")
        test.run()
        # Dump the logger output only if there is a test error, otherwise dont bother
        if test.error:
            test.logger.dump()
        self.assertIsNone(test.error)

    def test_35_two_router_waypoint_no_tenant_external_addr(self):
        test = WaypointTest(self.routers[0].addresses[0],
                            self.routers[1].addresses[2],
                            "hosted-group-1/queue.ext",
                            "EXT",
                            "ALCE")
        test.run()
        # Dump the logger output only if there is a test error, otherwise dont bother
        if test.error:
            test.logger.dump()
        self.assertIsNone(test.error)

    def test_36_two_router_waypoint_external_addr(self):
        test = WaypointTest(self.routers[0].addresses[1],
                            self.routers[1].addresses[2],
                            "queue.ext",
                            "EXT",
                            "ALCE")
        test.run()
        # Dump the logger output only if there is a test error, otherwise dont bother
        if test.error:
            test.logger.dump()
        self.assertIsNone(test.error)


class Entity:
    def __init__(self, status_code, status_description, attrs):
        self.status_code        = status_code
        self.status_description = status_description
        self.attrs              = attrs

    def __getattr__(self, key):
        return self.attrs[key]


class RouterProxy:
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


class MessageTransferTest(MessagingHandler):
    def __init__(self, sender_host, receiver_host, sender_address, receiver_address, lookup_host, lookup_address):
        super(MessageTransferTest, self).__init__()
        self.sender_host      = sender_host
        self.receiver_host    = receiver_host
        self.sender_address   = sender_address
        self.receiver_address = receiver_address
        self.lookup_host      = lookup_host
        self.lookup_address   = lookup_address

        self.sender_conn   = None
        self.receiver_conn = None
        self.lookup_conn   = None
        self.error         = None
        self.sender        = None
        self.receiver      = None
        self.proxy         = None

        self.count      = 10
        self.n_sent     = 0
        self.n_rcvd     = 0
        self.n_accepted = 0

        self.n_receiver_opened = 0
        self.n_sender_opened   = 0

    def timeout(self):
        self.error = "Timeout Expired: n_sent=%d n_rcvd=%d n_accepted=%d n_receiver_opened=%d n_sender_opened=%d" %\
            (self.n_sent, self.n_rcvd, self.n_accepted, self.n_receiver_opened, self.n_sender_opened)
        self.sender_conn.close()
        self.receiver_conn.close()
        self.lookup_conn.close()

    def on_start(self, event):
        self.timer          = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.sender_conn    = event.container.connect(self.sender_host)
        self.receiver_conn  = event.container.connect(self.receiver_host)
        self.lookup_conn    = event.container.connect(self.lookup_host)
        self.reply_receiver = event.container.create_receiver(self.lookup_conn, dynamic=True)
        self.agent_sender   = event.container.create_sender(self.lookup_conn, "$management")

    def send(self):
        while self.sender.credit > 0 and self.n_sent < self.count:
            self.n_sent += 1
            m = Message(body="Message %d of %d" % (self.n_sent, self.count))
            self.sender.send(m)

    def on_link_opened(self, event):
        if event.receiver:
            self.n_receiver_opened += 1
        else:
            self.n_sender_opened += 1

        if event.receiver == self.reply_receiver:
            self.proxy    = RouterProxy(self.reply_receiver.remote_source.address)
            self.sender   = event.container.create_sender(self.sender_conn, self.sender_address)
            self.receiver = event.container.create_receiver(self.receiver_conn, self.receiver_address)

    def on_sendable(self, event):
        if event.sender == self.sender:
            self.send()

    def on_message(self, event):
        if event.receiver == self.receiver:
            self.n_rcvd += 1
        if event.receiver == self.reply_receiver:
            response = self.proxy.response(event.message)
            if response.status_code != 200:
                self.error = "Unexpected error code from agent: %d - %s" % (response.status_code, response.status_description)
            if self.n_sent != self.count or self.n_rcvd != self.count:
                self.error = "Unexpected counts: n_sent=%d n_rcvd=%d n_accepted=%d" % (self.n_sent, self.n_rcvd, self.n_accepted)
            self.sender_conn.close()
            self.receiver_conn.close()
            self.lookup_conn.close()
            self.timer.cancel()

    def on_accepted(self, event):
        if event.sender == self.sender:
            self.n_accepted += 1
            if self.n_accepted == self.count:
                request = self.proxy.read_address(self.lookup_address)
                self.agent_sender.send(request)

    def run(self):
        Container(self).run()


class MessageTransferAnonTest(MessagingHandler):
    def __init__(self, sender_host, receiver_host, sender_address, receiver_address, lookup_host, lookup_address):
        super(MessageTransferAnonTest, self).__init__()
        self.sender_host      = sender_host
        self.receiver_host    = receiver_host
        self.sender_address   = sender_address
        self.receiver_address = receiver_address
        self.lookup_host      = lookup_host
        self.lookup_address   = lookup_address

        self.sender_conn   = None
        self.receiver_conn = None
        self.lookup_conn   = None
        self.error         = None
        self.sender        = None
        self.receiver      = None
        self.proxy         = None

        self.count      = 10
        self.n_sent     = 0
        self.n_rcvd     = 0
        self.n_accepted = 0

        self.n_agent_reads     = 0
        self.n_receiver_opened = 0
        self.n_sender_opened   = 0

    def timeout(self):
        self.error = "Timeout Expired: n_sent=%d n_rcvd=%d n_accepted=%d n_agent_reads=%d n_receiver_opened=%d n_sender_opened=%d" %\
            (self.n_sent, self.n_rcvd, self.n_accepted, self.n_agent_reads, self.n_receiver_opened, self.n_sender_opened)
        self.sender_conn.close()
        self.receiver_conn.close()
        self.lookup_conn.close()
        if self.poll_timer:
            self.poll_timer.cancel()

    def poll_timeout(self):
        self.poll()

    def on_start(self, event):
        self.timer          = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.poll_timer     = None
        self.sender_conn    = event.container.connect(self.sender_host)
        self.receiver_conn  = event.container.connect(self.receiver_host)
        self.lookup_conn    = event.container.connect(self.lookup_host)
        self.reply_receiver = event.container.create_receiver(self.lookup_conn, dynamic=True)
        self.agent_sender   = event.container.create_sender(self.lookup_conn, "$management")
        self.receiver       = event.container.create_receiver(self.receiver_conn, self.receiver_address)

    def send(self):
        while self.sender.credit > 0 and self.n_sent < self.count:
            self.n_sent += 1
            m = Message(body="Message %d of %d" % (self.n_sent, self.count))
            m.address = self.sender_address
            self.sender.send(m)

    def poll(self):
        request = self.proxy.read_address(self.lookup_address)
        self.agent_sender.send(request)
        self.n_agent_reads += 1

    def on_link_opened(self, event):
        if event.receiver:
            self.n_receiver_opened += 1
        else:
            self.n_sender_opened += 1

        if event.receiver == self.reply_receiver:
            self.proxy = RouterProxy(self.reply_receiver.remote_source.address)
            self.poll()

    def on_sendable(self, event):
        if event.sender == self.sender:
            self.send()

    def on_message(self, event):
        if event.receiver == self.receiver:
            self.n_rcvd += 1

        if event.receiver == self.reply_receiver:
            response = self.proxy.response(event.message)
            if response.status_code == 200 and (response.remoteCount + response.subscriberCount) > 0:
                self.sender = event.container.create_sender(self.sender_conn, None)
                if self.poll_timer:
                    self.poll_timer.cancel()
                    self.poll_timer = None
            else:
                self.poll_timer = event.reactor.schedule(0.25, PollTimeout(self))

    def on_accepted(self, event):
        if event.sender == self.sender:
            self.n_accepted += 1
            if self.n_accepted == self.count:
                self.sender_conn.close()
                self.receiver_conn.close()
                self.lookup_conn.close()
                self.timer.cancel()

    def run(self):
        Container(self).run()


class LinkRouteTest(MessagingHandler):
    def __init__(self, first_host, second_host, first_address, second_address, dynamic, lookup_host):
        super(LinkRouteTest, self).__init__(prefetch=0)
        self.first_host     = first_host
        self.second_host    = second_host
        self.first_address  = first_address
        self.second_address = second_address
        self.dynamic        = dynamic
        self.lookup_host    = lookup_host

        self.first_conn      = None
        self.second_conn     = None
        self.error           = None
        self.first_sender    = None
        self.first_receiver  = None
        self.second_sender   = None
        self.second_receiver = None
        self.poll_timer      = None

        self.count     = 10
        self.n_sent    = 0
        self.n_rcvd    = 0
        self.n_settled = 0

    def timeout(self):
        self.error = "Timeout Expired: n_sent=%d n_rcvd=%d n_settled=%d" % (self.n_sent, self.n_rcvd, self.n_settled)
        self.first_conn.close()
        self.second_conn.close()
        self.lookup_conn.close()
        if self.poll_timer:
            self.poll_timer.cancel()

    def poll_timeout(self):
        self.poll()

    def fail(self, text):
        self.error = text
        self.second_conn.close()
        self.first_conn.close()
        self.timer.cancel()
        self.lookup_conn.close()
        if self.poll_timer:
            self.poll_timer.cancel()

    def send(self):
        while self.first_sender.credit > 0 and self.n_sent < self.count:
            self.n_sent += 1
            m = Message(body="Message %d of %d" % (self.n_sent, self.count))
            self.first_sender.send(m)

    def poll(self):
        request = self.proxy.read_address("Dhosted-group-1/link")
        self.agent_sender.send(request)

    def setup_first_links(self, event):
        self.first_sender = event.container.create_sender(self.first_conn, self.first_address)
        if self.dynamic:
            self.first_receiver = event.container.create_receiver(self.first_conn,
                                                                  dynamic=True,
                                                                  options=DynamicNodeProperties({"x-opt-qd.address":
                                                                                                 UNICODE(self.first_address)}))
        else:
            self.first_receiver = event.container.create_receiver(self.first_conn, self.first_address)

    def on_start(self, event):
        self.timer          = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.first_conn     = event.container.connect(self.first_host)
        self.second_conn    = event.container.connect(self.second_host)
        self.lookup_conn    = event.container.connect(self.lookup_host)
        self.reply_receiver = event.container.create_receiver(self.lookup_conn, dynamic=True)
        self.agent_sender   = event.container.create_sender(self.lookup_conn, "$management")

    def on_link_opening(self, event):
        if event.sender:
            self.second_sender = event.sender
            if self.dynamic:
                if event.sender.remote_source.dynamic:
                    event.sender.source.address = self.second_address
                    event.sender.open()
                else:
                    self.fail("Expected dynamic source on sender")
            else:
                if event.sender.remote_source.address == self.second_address:
                    event.sender.source.address = self.second_address
                    event.sender.open()
                else:
                    self.fail("Incorrect address on incoming sender: got %s, expected %s" %
                              (event.sender.remote_source.address, self.second_address))

        elif event.receiver:
            self.second_receiver = event.receiver
            if event.receiver.remote_target.address == self.second_address:
                event.receiver.target.address = self.second_address
                event.receiver.open()
            else:
                self.fail("Incorrect address on incoming receiver: got %s, expected %s" %
                          (event.receiver.remote_target.address, self.second_address))

    def on_link_opened(self, event):
        if event.receiver:
            event.receiver.flow(self.count)

        if event.receiver == self.reply_receiver:
            self.proxy = RouterProxy(self.reply_receiver.remote_source.address)
            self.poll()

    def on_sendable(self, event):
        if event.sender == self.first_sender:
            self.send()

    def on_message(self, event):
        if event.receiver == self.first_receiver:
            self.n_rcvd += 1

        if event.receiver == self.reply_receiver:
            response = self.proxy.response(event.message)
            if response.status_code == 200 and (response.remoteCount + response.containerCount) > 0:
                if self.poll_timer:
                    self.poll_timer.cancel()
                    self.poll_timer = None
                self.setup_first_links(event)
            else:
                self.poll_timer = event.reactor.schedule(0.25, PollTimeout(self))

    def on_settled(self, event):
        if event.sender == self.first_sender:
            self.n_settled += 1
            if self.n_settled == self.count:
                self.fail(None)

    def run(self):
        container = Container(self)
        container.container_id = 'LRC'
        container.run()


class WaypointTest(MessagingHandler):
    def __init__(self, first_host, second_host, first_address, second_address, container_id="ALC"):
        super(WaypointTest, self).__init__()
        self.first_host     = first_host
        self.second_host    = second_host
        self.first_address  = first_address
        self.second_address = second_address
        self.container_id   = container_id
        self.logger = Logger(title="WaypointTest")

        self.first_conn        = None
        self.second_conn       = None
        self.error             = None
        self.first_sender      = None
        self.first_sender_created = False
        self.first_sender_link_opened = False
        self.first_receiver    = None
        self.first_receiver_created    = False
        self.waypoint_sender   = None
        self.waypoint_receiver = None
        self.waypoint_queue    = []
        self.waypoint_sender_opened = False
        self.waypoint_receiver_opened = False
        self.firsts_created = False

        self.count  = 10
        self.n_sent = 0
        self.n_rcvd = 0
        self.n_waypoint_rcvd = 0
        self.n_thru = 0
        self.outs = None

    def timeout(self):
        self.error = "Timeout Expired: n_sent=%d n_rcvd=%d n_thru=%d n_waypoint_rcvd=%d" % (self.n_sent, self.n_rcvd, self.n_thru, self.n_waypoint_rcvd)
        self.first_conn.close()
        self.second_conn.close()
        self.logger.dump()

    def fail(self, text):
        self.error = text
        self.second_conn.close()
        self.first_conn.close()
        self.timer.cancel()
        self.outs = "n_sent=%d n_rcvd=%d n_thru=%d n_waypoint_rcvd=%d" % (self.n_sent, self.n_rcvd, self.n_thru, self.n_waypoint_rcvd)
        print(self.outs)

    def send_client(self):
        while self.first_sender.credit > 0 and self.n_sent < self.count:
            self.n_sent += 1
            m = Message(body="Message %d of %d" % (self.n_sent, self.count))
            self.first_sender.send(m)

    def send_waypoint(self):
        self.logger.log("send_waypoint called")
        while self.waypoint_sender.credit > 0 and len(self.waypoint_queue) > 0:
            self.n_thru += 1
            m = self.waypoint_queue.pop()
            self.waypoint_sender.send(m)
            self.logger.log("waypoint_sender message sent")
        else:
            self.logger.log("waypoint_sender did not sent - credit = %s, len(self.waypoint_queue) = %s" % (str(self.waypoint_sender.credit), str(len(self.waypoint_queue))))

    def on_start(self, event):
        self.timer       = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.first_conn  = event.container.connect(self.first_host)
        self.second_conn = event.container.connect(self.second_host)

    def on_link_flow(self, event):
        if event.sender == self.waypoint_sender and self.first_sender_link_opened and not self.first_sender_created:
            self.first_sender_created = True
            self.first_sender = event.container.create_sender(self.first_conn, self.first_address)

    def on_link_opened(self, event):
        if event.receiver == self.waypoint_receiver and not self.first_sender_link_opened:
            self.first_sender_link_opened = True

    def on_link_opening(self, event):
        if event.sender and not self.waypoint_sender:
            self.waypoint_sender = event.sender
            if event.sender.remote_source.address == self.second_address:
                event.sender.source.address = self.second_address
                event.sender.open()
                self.waypoint_sender_opened = True
            else:
                self.fail("Incorrect address on incoming sender: got %s, expected %s" %
                          (event.sender.remote_source.address, self.second_address))

        elif event.receiver and not self.waypoint_receiver:
            self.waypoint_receiver = event.receiver
            if event.receiver.remote_target.address == self.second_address:
                event.receiver.target.address = self.second_address
                event.receiver.open()
                self.waypoint_receiver_opened = True
            else:
                self.fail("Incorrect address on incoming receiver: got %s, expected %s" %
                          (event.receiver.remote_target.address, self.second_address))

        if self.waypoint_sender_opened and self.waypoint_receiver_opened and not self.first_receiver_created:
            self.first_receiver_created = True
            self.first_receiver = event.container.create_receiver(self.first_conn, self.first_address)

    def on_sendable(self, event):
        if event.sender == self.first_sender:
            self.send_client()

    def on_message(self, event):
        if event.receiver == self.first_receiver:
            self.n_rcvd += 1
            if self.n_rcvd == self.count and self.n_thru == self.count:
                self.fail(None)
        elif event.receiver == self.waypoint_receiver:
            self.n_waypoint_rcvd += 1
            m = Message(body=event.message.body)
            self.waypoint_queue.append(m)
            self.send_waypoint()

    def run(self):
        container = Container(self)
        container.container_id = self.container_id
        container.run()


if __name__ == '__main__':
    unittest.main(main_module())

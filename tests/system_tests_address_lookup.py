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

from system_test import TestCase, Qdrouterd, TIMEOUT
from system_test import AsyncTestReceiver
from test_broker import FakeBroker

from proton import Disposition
from proton import Message
from proton.utils import BlockingConnection
from proton.utils import SyncRequestResponse
from proton.utils import SendException
from proton.utils import LinkDetached


class LinkRouteLookupTest(TestCase):
    """Tests link route address lookup"""
    # hardcoded values from the router's C code
    QD_TERMINUS_ADDRESS_LOOKUP = '_$qd.addr_lookup'
    PROTOCOL_VERSION = 1
    OPCODE_LINK_ROUTE_LOOKUP = 1
    QCM_ADDR_LOOKUP_OK = 0
    QCM_ADDR_LOOKUP_NOT_FOUND = 3

    def _check_response(self, message):
        is_instance_dict = isinstance(message.properties, dict)
        self.assertTrue(is_instance_dict)
        self.assertEqual(self.PROTOCOL_VERSION, message.properties.get('version'))
        self.assertTrue(message.properties.get('opcode') is not None)
        is_instance_list = isinstance(message.body, list)
        self.assertTrue(is_instance_list)
        self.assertEqual(2, len(message.body))
        return (message.properties.get('status'),
                message.body[0],  # is link_route?
                message.body[1])  # has destinations?

    @classmethod
    def setUpClass(cls):
        """Start a router"""
        super(LinkRouteLookupTest, cls).setUpClass()

        def router(name, mode, extra=None):
            config = [
                ('router', {'mode': mode, 'id': name}),
                ('listener', {'role': 'normal', 'host': '0.0.0.0', 'port': cls.tester.get_port(), 'saslMechanisms': 'ANONYMOUS'})
            ]

            if extra:
                config.extend(extra)
            config = Qdrouterd.Config(config)
            cls.routers.append(cls.tester.qdrouterd(name, config, wait=False))

        cls.routers = []

        inter_router_port = cls.tester.get_port()
        edge_port_A       = cls.tester.get_port()
        broker_port_A     = cls.tester.get_port()
        broker_port_B     = cls.tester.get_port()

        router('INT.A', 'interior',
               [
                   ('listener', {'role': 'edge',
                                 'port': cls.tester.get_port()}),
                   ('listener', {'role': 'inter-router',
                                 'port': inter_router_port}),
                   ('connector', {'role': 'route-container',
                                  'port': cls.tester.get_port()}),

                   ('linkRoute', {'pattern': 'org.apache.A.#',
                                  'containerId': 'FakeBrokerA',
                                  'direction': 'in'}),
                   ('linkRoute', {'pattern': 'org.apache.A.#',
                                  'containerId': 'FakeBrokerA',
                                  'direction': 'out'})
               ])
        cls.INT_A = cls.routers[-1]
        cls.INT_A.listener = cls.INT_A.addresses[0]
        cls.INT_A.edge_listener = cls.INT_A.addresses[1]
        cls.INT_A.inter_router_listener = cls.INT_A.addresses[2]
        cls.INT_A.broker_connector = cls.INT_A.connector_addresses[0]

        router('INT.B', 'interior',
               [
                   ('listener', {'role': 'edge',
                                 'port': cls.tester.get_port()}),
                   ('connector', {'role': 'inter-router',
                                  'name': 'connectorToA',
                                  'port': inter_router_port}),
                   ('connector', {'role': 'route-container',
                                  'port': cls.tester.get_port()}),

                   ('linkRoute', {'pattern': 'org.apache.B.#',
                                  'containerId': 'FakeBrokerB',
                                  'direction': 'in'}),
                   ('linkRoute', {'pattern': 'org.apache.B.#',
                                  'containerId': 'FakeBrokerB',
                                  'direction': 'out'})
               ])
        cls.INT_B = cls.routers[-1]
        cls.INT_B.edge_listener = cls.INT_B.addresses[1]
        cls.INT_B.broker_connector = cls.INT_B.connector_addresses[1]

        cls.INT_A.wait_router_connected('INT.B')
        cls.INT_B.wait_router_connected('INT.A')

    def _lookup_request(self, lr_address, direction):
        """Construct a link route lookup request message"""
        return Message(body=[lr_address,
                             direction],
                       properties={"version": self.PROTOCOL_VERSION,
                                   "opcode": self.OPCODE_LINK_ROUTE_LOOKUP})

    def test_link_route_lookup_ok(self):
        """
        verify a link route address can be looked up successfully for both
        locally attached and remotely attached containers
        """

        # fire up a fake broker attached to the router local to the edge router
        # wait until both in and out addresses are ready
        fb = FakeBroker(self.INT_A.broker_connector, container_id='FakeBrokerA')
        self.INT_A.wait_address("org.apache.A", containers=1, count=2)

        # create a fake edge and lookup the target address
        bc = BlockingConnection(self.INT_A.edge_listener, timeout=TIMEOUT)
        srr = SyncRequestResponse(bc, self.QD_TERMINUS_ADDRESS_LOOKUP)

        for direction in [True, False]:
            # True = direction inbound (receiver) False = direction outbound (sender)
            rsp = self._check_response(srr.call(self._lookup_request("org.apache.A.foo", direction)))
            self.assertEqual(self.QCM_ADDR_LOOKUP_OK, rsp[0])
            self.assertTrue(rsp[1])
            self.assertTrue(rsp[2])

        # shutdown fake router
        fb.join()

        # now fire up a fake broker attached to the remote router
        fb = FakeBroker(self.INT_B.broker_connector, container_id='FakeBrokerB')
        self.INT_A.wait_address("org.apache.B", remotes=1, count=2)

        for direction in [True, False]:
            rsp = self._check_response(srr.call(self._lookup_request("org.apache.B.foo", direction)))
            self.assertEqual(self.QCM_ADDR_LOOKUP_OK, rsp[0])
            self.assertTrue(rsp[1])
            self.assertTrue(rsp[2])

        fb.join()
        bc.close()

    def test_link_route_lookup_not_found(self):
        """verify correct handling of lookup misses"""
        bc = BlockingConnection(self.INT_A.edge_listener, timeout=TIMEOUT)
        srr = SyncRequestResponse(bc, self.QD_TERMINUS_ADDRESS_LOOKUP)

        rsp = self._check_response(srr.call(self._lookup_request("not.found.address", True)))
        self.assertEqual(self.QCM_ADDR_LOOKUP_NOT_FOUND, rsp[0])

    def test_link_route_lookup_not_link_route(self):
        """verify correct handling of matches to mobile addresses"""
        addr = "not.a.linkroute"
        client = AsyncTestReceiver(self.INT_A.listener, addr)
        self.INT_A.wait_address(addr)

        bc = BlockingConnection(self.INT_A.edge_listener, timeout=TIMEOUT)
        srr = SyncRequestResponse(bc, self.QD_TERMINUS_ADDRESS_LOOKUP)

        rsp = self._check_response(srr.call(self._lookup_request(addr, True)))
        # self.assertEqual(self.QCM_ADDR_LOOKUP_OK, rsp[0])
        self.assertEqual(False, rsp[1])
        bc.close()
        client.stop()

    def test_link_route_lookup_no_dest(self):
        """verify correct handling of matches to mobile addresses"""
        bc = BlockingConnection(self.INT_A.edge_listener, timeout=TIMEOUT)
        srr = SyncRequestResponse(bc, self.QD_TERMINUS_ADDRESS_LOOKUP)
        rsp = self._check_response(srr.call(self._lookup_request("org.apache.A.nope", True)))
        self.assertEqual(self.QCM_ADDR_LOOKUP_OK, rsp[0])
        self.assertEqual(True, rsp[1])
        self.assertEqual(False, rsp[2])
        bc.close()

    def _invalid_request_test(self, msg, disposition=Disposition.REJECTED):
        bc = BlockingConnection(self.INT_A.edge_listener, timeout=TIMEOUT)
        srr = SyncRequestResponse(bc, self.QD_TERMINUS_ADDRESS_LOOKUP)
        # @TODO(kgiusti) - self.assertRaises does not work here:
        try:
            srr.call(msg)
            self.assertTrue(False)
        except SendException as exc:
            self.assertEqual(disposition, exc.state)
        bc.close()

    def test_link_route_invalid_request(self):
        """Test various invalid message content"""

        # empty message
        self._invalid_request_test(Message())

        # missing properties
        msg = self._lookup_request("ignore", False)
        msg.properties = None
        self._invalid_request_test(msg)

        # missing version
        msg = self._lookup_request("ignore", False)
        del msg.properties['version']
        self._invalid_request_test(msg)

        # invalid version
        msg = self._lookup_request("ignore", False)
        msg.properties['version'] = "blech"
        self._invalid_request_test(msg)

        # unsupported version
        msg = self._lookup_request("ignore", False)
        msg.properties['version'] = 97387187
        self._invalid_request_test(msg)

        # missing opcode
        msg = self._lookup_request("ignore", False)
        del msg.properties['opcode']
        self._invalid_request_test(msg)

        # bad opcode
        msg = self._lookup_request("ignore", False)
        msg.properties['opcode'] = "snarf"
        self._invalid_request_test(msg)

        # bad body
        msg = self._lookup_request("ignore", False)
        msg.body = [71]
        self._invalid_request_test(msg)

    def test_lookup_bad_connection(self):
        """Verify that clients connected via non-edge connections fail"""
        bc = BlockingConnection(self.INT_A.listener, timeout=TIMEOUT)
        self.assertRaises(LinkDetached, SyncRequestResponse, bc, self.QD_TERMINUS_ADDRESS_LOOKUP)
        bc.close()

        bc = BlockingConnection(self.INT_A.inter_router_listener, timeout=TIMEOUT)
        self.assertRaises(LinkDetached, SyncRequestResponse, bc, self.QD_TERMINUS_ADDRESS_LOOKUP)
        bc.close()

        # consuming from the lookup address is forbidden:
        bc = BlockingConnection(self.INT_A.edge_listener, timeout=TIMEOUT)
        self.assertRaises(LinkDetached, bc.create_receiver, self.QD_TERMINUS_ADDRESS_LOOKUP)
        bc.close()

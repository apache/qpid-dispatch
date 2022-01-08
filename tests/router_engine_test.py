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

import os
import sys

import mock  # noqa F401: imported for side-effects (installs mock definitions for tests)  # pylint: disable=unused-import

sys.path.append(os.path.join(os.environ["SOURCE_DIR"], "python"))

from system_test import unittest
from system_test import main_module
from qpid_dispatch.management.entity import EntityBase
from qpid_dispatch_internal.router.data import LinkState, MessageHELLO
from qpid_dispatch_internal.router.engine import HelloProtocol, PathEngine


class Adapter:
    def __init__(self, domain):
        self._domain = domain

    def log(self, level, text):
        print("Adapter.log(%d): domain=%s, text=%s" % (level, self._domain, text))

    def send(self, dest, opcode, body):
        print("Adapter.send: domain=%s, dest=%s, opcode=%s, body=%s" % (self._domain, dest, opcode, body))

    def remote_bind(self, subject, peer):
        print("Adapter.remote_bind: subject=%s, peer=%s" % (subject, peer))

    def remote_unbind(self, subject, peer):
        print("Adapter.remote_unbind: subject=%s, peer=%s" % (subject, peer))

    def node_updated(self, address, reachable, neighbor, link_bit, router_bit):
        print("Adapter.node_updated: address=%s, reachable=%r, neighbor=%r, link_bit=%d, router_bit=%d" %
              (address, reachable, neighbor, link_bit, router_bit))


class DataTest(unittest.TestCase):
    def test_link_state(self):
        ls = LinkState(None, 'R1', 1, {'R2': 1, 'R3': 1})
        self.assertEqual(ls.id, 'R1')
        self.assertEqual(ls.ls_seq, 1)
        self.assertEqual(ls.peers, {'R2': 1, 'R3': 1})
        ls.bump_sequence()
        self.assertEqual(ls.id, 'R1')
        self.assertEqual(ls.ls_seq, 2)
        self.assertEqual(ls.peers, {'R2': 1, 'R3': 1})

        result = ls.add_peer('R4', 5)
        self.assertTrue(result)
        self.assertEqual(ls.peers, {'R2': 1, 'R3': 1, 'R4': 5})
        result = ls.add_peer('R2', 1)
        self.assertFalse(result)
        self.assertEqual(ls.peers, {'R2': 1, 'R3': 1, 'R4': 5})

        result = ls.del_peer('R3')
        self.assertTrue(result)
        self.assertEqual(ls.peers, {'R2': 1, 'R4': 5})
        result = ls.del_peer('R5')
        self.assertFalse(result)
        self.assertEqual(ls.peers, {'R2': 1, 'R4': 5})

        encoded = ls.to_dict()
        new_ls = LinkState(encoded)
        self.assertEqual(new_ls.id, 'R1')
        self.assertEqual(new_ls.ls_seq, 2)
        self.assertEqual(new_ls.peers, {'R2': 1, 'R4': 5})

    def test_hello_message(self):
        msg1 = MessageHELLO(None, 'R1', ['R2', 'R3', 'R4'])
        self.assertEqual(msg1.get_opcode(), "HELLO")
        self.assertEqual(msg1.id, 'R1')
        self.assertEqual(msg1.seen_peers, ['R2', 'R3', 'R4'])
        encoded = msg1.to_dict()
        msg2 = MessageHELLO(encoded)
        self.assertEqual(msg2.get_opcode(), "HELLO")
        self.assertEqual(msg2.id, 'R1')
        self.assertEqual(msg2.seen_peers, ['R2', 'R3', 'R4'])
        self.assertTrue(msg2.is_seen('R3'))
        self.assertFalse(msg2.is_seen('R9'))


class NodeTrackerTest(unittest.TestCase):
    def log(self, level, text):
        pass

    def add_neighbor_router(self, address, router_bit, link_bit):
        self.address    = address
        self.router_bit = router_bit
        self.link_bit   = link_bit
        self.calls     += 1

    def del_neighbor_router(self, router_id, router_bit):
        self.address    = None
        self.router_bit = router_bit
        self.link_bit   = None
        self.calls     += 1

    def add_remote_router(self, address, router_bit):
        self.address    = address
        self.router_bit = router_bit
        self.link_bit   = None
        self.calls     += 1

    def del_remote_router(self, router_id, router_bit):
        self.address    = None
        self.router_bit = router_bit
        self.link_bit   = None
        self.calls     += 1

    def reset(self):
        self.address    = None
        self.router_bit = None
        self.link_bit   = None
        self.calls      = 0


class NeighborTest(unittest.TestCase):
    def log(self, level, text):
        pass

    def log_hello(self, level, text):
        pass

    def send(self, dest, msg):
        self.sent.append((dest, msg))

    def neighbor_refresh(self, node_id, ProtocolVersion, instance, link_id, cost, now):
        self.neighbors[node_id] = (instance, link_id, cost, now)

    def setUp(self):
        self.sent = []
        self.neighbors = {}
        self.id = "R1"
        self.instance = 0
        # Fake configuration
        self.config = EntityBase({
            'helloIntervalSeconds'    :  1.0,
            'helloMaxAgeSeconds'      :  3.0,
            'raIntervalSeconds'       : 30.0,
            'remoteLsMaxAgeSeconds'   : 60.0})
        self.neighbors = {}

    def test_hello_sent(self):
        self.sent = []
        self.local_link_state = None
        self.engine = HelloProtocol(self, self)
        self.engine.tick(1.0)
        self.assertEqual(len(self.sent), 1)
        dest, msg = self.sent.pop(0)
        self.assertEqual(dest, "amqp:/_local/qdhello")
        self.assertEqual(msg.get_opcode(), "HELLO")
        self.assertEqual(msg.id, self.id)
        self.assertEqual(msg.seen_peers, [])
        self.assertEqual(self.local_link_state, None)

    def test_sees_peer(self):
        self.sent = []
        self.neighbors = {}
        self.engine = HelloProtocol(self, self)
        self.engine.handle_hello(MessageHELLO(None, 'R2', []), 2.0, 0, 1)
        self.engine.tick(5.0)
        self.assertEqual(len(self.sent), 1)
        dest, msg = self.sent.pop(0)
        self.assertEqual(msg.seen_peers, ['R2'])

    def test_establish_peer(self):
        self.sent = []
        self.neighbors = {}
        self.engine = HelloProtocol(self, self)
        self.engine.handle_hello(MessageHELLO(None, 'R2', ['R1']), 0.5, 0, 1)
        self.engine.tick(1.0)
        self.engine.tick(2.0)
        self.engine.tick(3.0)
        self.assertEqual(len(self.neighbors), 1)
        self.assertEqual(list(self.neighbors.keys()), ['R2'])

    def test_establish_multiple_peers(self):
        self.sent = []
        self.neighbors = {}
        self.engine = HelloProtocol(self, self)
        self.engine.handle_hello(MessageHELLO(None, 'R2', ['R1']), 0.5, 0, 1)
        self.engine.tick(1.0)
        self.engine.handle_hello(MessageHELLO(None, 'R3', ['R1', 'R2']), 1.5, 0, 1)
        self.engine.tick(2.0)
        self.engine.handle_hello(MessageHELLO(None, 'R4', ['R1']), 2.5, 0, 1)
        self.engine.handle_hello(MessageHELLO(None, 'R5', ['R2']), 2.5, 0, 1)
        self.engine.handle_hello(MessageHELLO(None, 'R6', ['R1']), 2.5, 0, 1)
        self.engine.tick(3.0)
        keys = sorted(self.neighbors.keys())
        self.assertEqual(keys, ['R2', 'R3', 'R4', 'R6'])


class PathTest(unittest.TestCase):
    def setUp(self):
        self.id = 'R1'
        self.engine = PathEngine(self)

    def log(self, level, text):
        pass

    def test_topology1(self):
        """

        +====+      +----+      +----+
        | R1 |------| R2 |------| R3 |
        +====+      +----+      +----+

        """
        collection = {'R1': LinkState(None, 'R1', 1, {'R2': 1}),
                      'R2': LinkState(None, 'R2', 1, {'R1': 1, 'R3': 1}),
                      'R3': LinkState(None, 'R3', 1, {'R2': 1})}
        next_hops, costs, valid_origins, radius = self.engine.calculate_routes(collection)
        self.assertEqual(len(next_hops), 2)
        self.assertEqual(next_hops['R2'], 'R2')
        self.assertEqual(next_hops['R3'], 'R2')

        valid_origins['R2'].sort()
        valid_origins['R3'].sort()
        self.assertEqual(valid_origins['R2'], [])
        self.assertEqual(valid_origins['R3'], [])

        self.assertEqual(radius, 2)

    def test_topology2(self):
        """

        +====+      +----+      +----+
        | R1 |------| R2 |------| R4 |
        +====+      +----+      +----+
                       |           |
                    +----+      +----+      +----+
                    | R3 |------| R5 |------| R6 |
                    +----+      +----+      +----+

        """
        collection = {'R1': LinkState(None, 'R1', 1, {'R2': 1}),
                      'R2': LinkState(None, 'R2', 1, {'R1': 1, 'R3': 1, 'R4': 1}),
                      'R3': LinkState(None, 'R3', 1, {'R2': 1, 'R5': 1}),
                      'R4': LinkState(None, 'R4', 1, {'R2': 1, 'R5': 1}),
                      'R5': LinkState(None, 'R5', 1, {'R3': 1, 'R4': 1, 'R6': 1}),
                      'R6': LinkState(None, 'R6', 1, {'R5': 1})}
        next_hops, costs, valid_origins, radius = self.engine.calculate_routes(collection)
        self.assertEqual(len(next_hops), 5)
        self.assertEqual(next_hops['R2'], 'R2')
        self.assertEqual(next_hops['R3'], 'R2')
        self.assertEqual(next_hops['R4'], 'R2')
        self.assertEqual(next_hops['R5'], 'R2')
        self.assertEqual(next_hops['R6'], 'R2')

        valid_origins['R2'].sort()
        valid_origins['R3'].sort()
        valid_origins['R4'].sort()
        valid_origins['R5'].sort()
        valid_origins['R6'].sort()
        self.assertEqual(valid_origins['R2'], [])
        self.assertEqual(valid_origins['R3'], [])
        self.assertEqual(valid_origins['R4'], [])
        self.assertEqual(valid_origins['R5'], [])
        self.assertEqual(valid_origins['R6'], [])

        self.assertEqual(radius, 4)

    def test_topology3(self):
        """

        +----+      +----+      +----+
        | R2 |------| R3 |------| R4 |
        +----+      +----+      +----+
                       |           |
                    +====+      +----+      +----+
                    | R1 |------| R5 |------| R6 |
                    +====+      +----+      +----+

        """
        collection = {'R2': LinkState(None, 'R2', 1, {'R3': 1}),
                      'R3': LinkState(None, 'R3', 1, {'R1': 1, 'R2': 1, 'R4': 1}),
                      'R4': LinkState(None, 'R4', 1, {'R3': 1, 'R5': 1}),
                      'R1': LinkState(None, 'R1', 1, {'R3': 1, 'R5': 1}),
                      'R5': LinkState(None, 'R5', 1, {'R1': 1, 'R4': 1, 'R6': 1}),
                      'R6': LinkState(None, 'R6', 1, {'R5': 1})}
        next_hops, costs, valid_origins, radius = self.engine.calculate_routes(collection)
        self.assertEqual(len(next_hops), 5)
        self.assertEqual(next_hops['R2'], 'R3')
        self.assertEqual(next_hops['R3'], 'R3')
        self.assertEqual(next_hops['R4'], 'R3')
        self.assertEqual(next_hops['R5'], 'R5')
        self.assertEqual(next_hops['R6'], 'R5')

        valid_origins['R2'].sort()
        valid_origins['R3'].sort()
        valid_origins['R4'].sort()
        valid_origins['R5'].sort()
        valid_origins['R6'].sort()
        self.assertEqual(valid_origins['R2'], ['R5', 'R6'])
        self.assertEqual(valid_origins['R3'], ['R5', 'R6'])
        self.assertEqual(valid_origins['R4'], [])
        self.assertEqual(valid_origins['R5'], ['R2', 'R3'])
        self.assertEqual(valid_origins['R6'], ['R2', 'R3'])

        self.assertEqual(radius, 2)

    def test_topology4(self):
        """

        +----+      +----+      +----+
        | R2 |------| R3 |------| R4 |
        +----+      +----+      +----+
                       |           |
                    +====+      +----+      +----+
                    | R1 |------| R5 |------| R6 |------ R7 (no ls from R7)
                    +====+      +----+      +----+

        """
        collection = {'R2': LinkState(None, 'R2', 1, {'R3': 1}),
                      'R3': LinkState(None, 'R3', 1, {'R1': 1, 'R2': 1, 'R4': 1}),
                      'R4': LinkState(None, 'R4', 1, {'R3': 1, 'R5': 1}),
                      'R1': LinkState(None, 'R1', 1, {'R3': 1, 'R5': 1}),
                      'R5': LinkState(None, 'R5', 1, {'R1': 1, 'R4': 1, 'R6': 1}),
                      'R6': LinkState(None, 'R6', 1, {'R5': 1, 'R7': 1})}
        next_hops, costs, valid_origins, radius = self.engine.calculate_routes(collection)
        self.assertEqual(len(next_hops), 6)
        self.assertEqual(next_hops['R2'], 'R3')
        self.assertEqual(next_hops['R3'], 'R3')
        self.assertEqual(next_hops['R4'], 'R3')
        self.assertEqual(next_hops['R5'], 'R5')
        self.assertEqual(next_hops['R6'], 'R5')
        self.assertEqual(next_hops['R7'], 'R5')

        valid_origins['R2'].sort()
        valid_origins['R3'].sort()
        valid_origins['R4'].sort()
        valid_origins['R5'].sort()
        valid_origins['R6'].sort()
        valid_origins['R7'].sort()
        self.assertEqual(valid_origins['R2'], ['R5', 'R6', 'R7'])
        self.assertEqual(valid_origins['R3'], ['R5', 'R6', 'R7'])
        self.assertEqual(valid_origins['R4'], [])
        self.assertEqual(valid_origins['R5'], ['R2', 'R3'])
        self.assertEqual(valid_origins['R6'], ['R2', 'R3'])
        self.assertEqual(valid_origins['R7'], ['R2', 'R3'])

        self.assertEqual(radius, 3)

    def test_topology5(self):
        """

        +----+      +----+      +----+
        | R2 |------| R3 |------| R4 |
        +----+      +----+      +----+
           |           |           |
           |        +====+      +----+      +----+
           +--------| R1 |------| R5 |------| R6 |------ R7 (no ls from R7)
                    +====+      +----+      +----+

        """
        collection = {'R2': LinkState(None, 'R2', 1, {'R3': 1, 'R1': 1}),
                      'R3': LinkState(None, 'R3', 1, {'R1': 1, 'R2': 1, 'R4': 1}),
                      'R4': LinkState(None, 'R4', 1, {'R3': 1, 'R5': 1}),
                      'R1': LinkState(None, 'R1', 1, {'R3': 1, 'R5': 1, 'R2': 1}),
                      'R5': LinkState(None, 'R5', 1, {'R1': 1, 'R4': 1, 'R6': 1}),
                      'R6': LinkState(None, 'R6', 1, {'R5': 1, 'R7': 1})}
        next_hops, costs, valid_origins, radius = self.engine.calculate_routes(collection)
        self.assertEqual(len(next_hops), 6)
        self.assertEqual(next_hops['R2'], 'R2')
        self.assertEqual(next_hops['R3'], 'R3')
        self.assertEqual(next_hops['R4'], 'R3')
        self.assertEqual(next_hops['R5'], 'R5')
        self.assertEqual(next_hops['R6'], 'R5')
        self.assertEqual(next_hops['R7'], 'R5')

        self.assertEqual(costs['R2'], 1)
        self.assertEqual(costs['R3'], 1)
        self.assertEqual(costs['R4'], 2)
        self.assertEqual(costs['R5'], 1)
        self.assertEqual(costs['R6'], 2)
        self.assertEqual(costs['R7'], 3)

        valid_origins['R2'].sort()
        valid_origins['R3'].sort()
        valid_origins['R4'].sort()
        valid_origins['R5'].sort()
        valid_origins['R6'].sort()
        valid_origins['R7'].sort()
        self.assertEqual(valid_origins['R2'], ['R5', 'R6', 'R7'])
        self.assertEqual(valid_origins['R3'], ['R5', 'R6', 'R7'])
        self.assertEqual(valid_origins['R4'], [])
        self.assertEqual(valid_origins['R5'], ['R2', 'R3'])
        self.assertEqual(valid_origins['R6'], ['R2', 'R3'])
        self.assertEqual(valid_origins['R7'], ['R2', 'R3'])

        self.assertEqual(radius, 3)

    def test_topology5_with_asymmetry1(self):
        """

        +----+      +----+      +----+
        | R2 |------| R3 |------| R4 |
        +----+      +----+      +----+
           ^           |           |
           ^        +====+      +----+      +----+
           +-<-<-<--| R1 |------| R5 |------| R6 |------ R7 (no ls from R7)
                    +====+      +----+      +----+

        """
        collection = {'R2': LinkState(None, 'R2', 1, {'R3': 1}),
                      'R3': LinkState(None, 'R3', 1, {'R1': 1, 'R2': 1, 'R4': 1}),
                      'R4': LinkState(None, 'R4', 1, {'R3': 1, 'R5': 1}),
                      'R1': LinkState(None, 'R1', 1, {'R3': 1, 'R5': 1, 'R2': 1}),
                      'R5': LinkState(None, 'R5', 1, {'R1': 1, 'R4': 1, 'R6': 1}),
                      'R6': LinkState(None, 'R6', 1, {'R5': 1, 'R7': 1})}
        next_hops, costs, valid_origins, radius = self.engine.calculate_routes(collection)
        self.assertEqual(len(next_hops), 6)
        self.assertEqual(next_hops['R2'], 'R2')
        self.assertEqual(next_hops['R3'], 'R3')
        self.assertEqual(next_hops['R4'], 'R3')
        self.assertEqual(next_hops['R5'], 'R5')
        self.assertEqual(next_hops['R6'], 'R5')
        self.assertEqual(next_hops['R7'], 'R5')

        valid_origins['R2'].sort()
        valid_origins['R3'].sort()
        valid_origins['R4'].sort()
        valid_origins['R5'].sort()
        valid_origins['R6'].sort()
        valid_origins['R7'].sort()
        self.assertEqual(valid_origins['R2'], ['R5', 'R6', 'R7'])
        self.assertEqual(valid_origins['R3'], ['R5', 'R6', 'R7'])
        self.assertEqual(valid_origins['R4'], [])
        self.assertEqual(valid_origins['R5'], ['R2', 'R3'])
        self.assertEqual(valid_origins['R6'], ['R2', 'R3'])
        self.assertEqual(valid_origins['R7'], ['R2', 'R3'])

        self.assertEqual(radius, 3)

    def test_topology5_with_asymmetry2(self):
        """

        +----+      +----+      +----+
        | R2 |------| R3 |------| R4 |
        +----+      +----+      +----+
           v           |           |
           v        +====+      +----+      +----+
           +->->->->| R1 |------| R5 |------| R6 |------ R7 (no ls from R7)
                    +====+      +----+      +----+

        """
        collection = {'R2': LinkState(None, 'R2', 1, {'R3': 1, 'R1': 1}),
                      'R3': LinkState(None, 'R3', 1, {'R1': 1, 'R2': 1, 'R4': 1}),
                      'R4': LinkState(None, 'R4', 1, {'R3': 1, 'R5': 1}),
                      'R1': LinkState(None, 'R1', 1, {'R3': 1, 'R5': 1}),
                      'R5': LinkState(None, 'R5', 1, {'R1': 1, 'R4': 1, 'R6': 1}),
                      'R6': LinkState(None, 'R6', 1, {'R5': 1, 'R7': 1})}
        next_hops, costs, valid_origins, radius = self.engine.calculate_routes(collection)
        self.assertEqual(len(next_hops), 6)
        self.assertEqual(next_hops['R2'], 'R3')
        self.assertEqual(next_hops['R3'], 'R3')
        self.assertEqual(next_hops['R4'], 'R3')
        self.assertEqual(next_hops['R5'], 'R5')
        self.assertEqual(next_hops['R6'], 'R5')
        self.assertEqual(next_hops['R7'], 'R5')

        valid_origins['R2'].sort()
        valid_origins['R3'].sort()
        valid_origins['R4'].sort()
        valid_origins['R5'].sort()
        valid_origins['R6'].sort()
        valid_origins['R7'].sort()
        self.assertEqual(valid_origins['R2'], ['R5', 'R6', 'R7'])
        self.assertEqual(valid_origins['R3'], ['R5', 'R6', 'R7'])
        self.assertEqual(valid_origins['R4'], [])
        self.assertEqual(valid_origins['R5'], ['R2', 'R3'])
        self.assertEqual(valid_origins['R6'], ['R2', 'R3'])
        self.assertEqual(valid_origins['R7'], ['R2', 'R3'])

        self.assertEqual(radius, 3)

    def test_topology5_with_asymmetry3(self):
        """

        +----+      +----+      +----+
        | R2 |------| R3 |------| R4 |
        +----+      +----+      +----+
           v           |           |
           v        +====+      +----+      +----+
           +->->->->| R1 |------| R5 |<-<-<-| R6 |------ R7 (no ls from R7)
                    +====+      +----+      +----+

        """
        collection = {'R2': LinkState(None, 'R2', 1, {'R3': 1, 'R1': 1}),
                      'R3': LinkState(None, 'R3', 1, {'R1': 1, 'R2': 1, 'R4': 1}),
                      'R4': LinkState(None, 'R4', 1, {'R3': 1, 'R5': 1}),
                      'R1': LinkState(None, 'R1', 1, {'R3': 1, 'R5': 1}),
                      'R5': LinkState(None, 'R5', 1, {'R1': 1, 'R4': 1}),
                      'R6': LinkState(None, 'R6', 1, {'R5': 1, 'R7': 1})}
        next_hops, costs, valid_origins, radius = self.engine.calculate_routes(collection)
        self.assertEqual(len(next_hops), 4)
        self.assertEqual(next_hops['R2'], 'R3')
        self.assertEqual(next_hops['R3'], 'R3')
        self.assertEqual(next_hops['R4'], 'R3')
        self.assertEqual(next_hops['R5'], 'R5')

        valid_origins['R2'].sort()
        valid_origins['R3'].sort()
        valid_origins['R4'].sort()
        valid_origins['R5'].sort()
        self.assertEqual(valid_origins['R2'], ['R5'])
        self.assertEqual(valid_origins['R3'], ['R5'])
        self.assertEqual(valid_origins['R4'], [])
        self.assertEqual(valid_origins['R5'], ['R2', 'R3'])

        self.assertEqual(radius, 2)

    def test_topology5_with_costs1(self):
        """

        +----+      +----+      +----+
        | R2 |--4---| R3 |---4--| R4 |
        +----+      +----+      +----+
           |           |           |
           |           3           5
           |           |           |
           |        +====+      +----+      +----+
           +--20----| R1 |--10--| R5 |--2---| R6 |------ R7 (no ls from R7)
                    +====+      +----+      +----+

        """
        collection = {'R2': LinkState(None, 'R2', 1, {'R3': 4,  'R1': 20}),
                      'R3': LinkState(None, 'R3', 1, {'R1': 3,  'R2': 4,  'R4': 4}),
                      'R4': LinkState(None, 'R4', 1, {'R3': 4,  'R5': 5}),
                      'R1': LinkState(None, 'R1', 1, {'R3': 3,  'R5': 10, 'R2': 20}),
                      'R5': LinkState(None, 'R5', 1, {'R1': 10, 'R4': 5,  'R6': 2}),
                      'R6': LinkState(None, 'R6', 1, {'R5': 2,  'R7': 1})}
        next_hops, costs, valid_origins, radius = self.engine.calculate_routes(collection)
        self.assertEqual(len(next_hops), 6)
        self.assertEqual(next_hops['R2'], 'R3')
        self.assertEqual(next_hops['R3'], 'R3')
        self.assertEqual(next_hops['R4'], 'R3')
        self.assertEqual(next_hops['R5'], 'R5')
        self.assertEqual(next_hops['R6'], 'R5')
        self.assertEqual(next_hops['R7'], 'R5')

        self.assertEqual(costs['R2'], 7)
        self.assertEqual(costs['R3'], 3)
        self.assertEqual(costs['R4'], 7)
        self.assertEqual(costs['R5'], 10)
        self.assertEqual(costs['R6'], 12)
        self.assertEqual(costs['R7'], 13)

        valid_origins['R2'].sort()
        valid_origins['R3'].sort()
        valid_origins['R4'].sort()
        valid_origins['R5'].sort()
        valid_origins['R6'].sort()
        valid_origins['R7'].sort()
        self.assertEqual(valid_origins['R2'], [])
        self.assertEqual(valid_origins['R3'], [])
        self.assertEqual(valid_origins['R4'], [])
        self.assertEqual(valid_origins['R5'], [])
        self.assertEqual(valid_origins['R6'], [])
        self.assertEqual(valid_origins['R7'], [])

        self.assertEqual(radius, 3)

    def test_topology5_with_costs2(self):
        """

        +----+      +----+      +----+
        | R2 |--4---| R3 |---4--| R4 |
        +----+      +----+      +----+
           |           |           |
           |          100         100
           |           |           |
           |        +====+      +----+      +----+
           +---5----| R1 |--10--| R5 |--2---| R6 |------ R7 (no ls from R7)
                    +====+      +----+      +----+

        """
        collection = {'R2': LinkState(None, 'R2', 1, {'R3': 4,   'R1': 5}),
                      'R3': LinkState(None, 'R3', 1, {'R1': 100, 'R2': 4,   'R4': 4}),
                      'R4': LinkState(None, 'R4', 1, {'R3': 4,   'R5': 100}),
                      'R1': LinkState(None, 'R1', 1, {'R3': 100, 'R5': 10,  'R2': 5}),
                      'R5': LinkState(None, 'R5', 1, {'R1': 10,  'R4': 100, 'R6': 2}),
                      'R6': LinkState(None, 'R6', 1, {'R5': 2,   'R7': 1})}
        next_hops, costs, valid_origins, radius = self.engine.calculate_routes(collection)
        self.assertEqual(len(next_hops), 6)
        self.assertEqual(next_hops['R2'], 'R2')
        self.assertEqual(next_hops['R3'], 'R2')
        self.assertEqual(next_hops['R4'], 'R2')
        self.assertEqual(next_hops['R5'], 'R5')
        self.assertEqual(next_hops['R6'], 'R5')
        self.assertEqual(next_hops['R7'], 'R5')

        self.assertEqual(costs['R2'], 5)
        self.assertEqual(costs['R3'], 9)
        self.assertEqual(costs['R4'], 13)
        self.assertEqual(costs['R5'], 10)
        self.assertEqual(costs['R6'], 12)
        self.assertEqual(costs['R7'], 13)

        valid_origins['R2'].sort()
        valid_origins['R3'].sort()
        valid_origins['R4'].sort()
        valid_origins['R5'].sort()
        valid_origins['R6'].sort()
        valid_origins['R7'].sort()
        self.assertEqual(valid_origins['R2'], ['R5', 'R6', 'R7'])
        self.assertEqual(valid_origins['R3'], ['R5', 'R6', 'R7'])
        self.assertEqual(valid_origins['R4'], ['R5', 'R6', 'R7'])
        self.assertEqual(valid_origins['R5'], ['R2', 'R3', 'R4'])
        self.assertEqual(valid_origins['R6'], ['R2', 'R3', 'R4'])
        self.assertEqual(valid_origins['R7'], ['R2', 'R3', 'R4'])

        self.assertEqual(radius, 3)

    def test_topology6_path_vs_valid_origin(self):
        """

        +====+      +====+      +----+
        | R1 |--10--| R3 |--10--| R5 |
        +====+      +====+      +----+
           |           |           |
           1          10           1
           |           |           |
        +----+      +----+      +----+
        | R2 |--10--| R4 |--10--| R6 |
        +----+      +----+      +----+

        """
        collection = {'R1': LinkState(None, 'R1', 1, {'R2': 1,  'R3': 10}),
                      'R2': LinkState(None, 'R2', 1, {'R1': 1,  'R4': 10}),
                      'R3': LinkState(None, 'R3', 1, {'R1': 10, 'R4': 10, 'R5': 10}),
                      'R4': LinkState(None, 'R4', 1, {'R2': 10, 'R3': 10, 'R6': 10}),
                      'R5': LinkState(None, 'R5', 1, {'R3': 10, 'R6': 1}),
                      'R6': LinkState(None, 'R6', 1, {'R4': 10, 'R5': 1})}

        self.id = 'R3'
        self.engine = PathEngine(self)
        r3_next_hops, r3_costs, r3_valid_origins, r3_radius = self.engine.calculate_routes(collection)

        self.id = 'R1'
        self.engine = PathEngine(self)
        r1_next_hops, r1_costs, r1_valid_origins, r1_radius = self.engine.calculate_routes(collection)

        self.assertEqual(r1_next_hops['R6'], 'R2')
        self.assertEqual(r3_valid_origins['R6'], [])

        self.assertEqual(r1_radius, 3)
        self.assertEqual(r3_radius, 2)


if __name__ == '__main__':
    unittest.main(main_module())

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

import ast
from time import sleep
from subprocess import PIPE, STDOUT

from system_test import TestCase, Qdrouterd, main_module, TIMEOUT, Process
from system_test import AsyncTestReceiver
from system_test import unittest
from proton import Message, Timeout
from proton.reactor import AtMostOnce, AtLeastOnce
from proton.utils import BlockingConnection, SendException

_EXCHANGE_TYPE = "org.apache.qpid.dispatch.router.config.exchange"
_BINDING_TYPE  = "org.apache.qpid.dispatch.router.config.binding"


class ExchangeBindingsTest(TestCase):
    """
    Tests the exchange/bindings of the dispatch router.
    """
    def _create_router(self, name, config):

        config = [
            ('router',   {'mode': 'standalone', 'id': 'QDR.%s'%name}),
            ('listener', {'role': 'normal', 'host': '0.0.0.0',
                          'port': self.tester.get_port(),
                          'saslMechanisms':'ANONYMOUS'})
            ] + config
        return self.tester.qdrouterd(name, Qdrouterd.Config(config))

    def run_qdmanage(self, router, cmd, input=None, expect=Process.EXIT_OK):
        p = self.popen(
            ['qdmanage'] + cmd.split(' ')
            + ['--bus', router.addresses[0], '--indent=-1', '--timeout', str(TIMEOUT)],
            stdin=PIPE, stdout=PIPE, stderr=STDOUT, expect=expect,
            universal_newlines=True)
        out = p.communicate(input)[0]
        try:
            p.teardown()
        except Exception as e:
            raise Exception("%s\n%s" % (e, out))
        return out

    def _validate_entity(self, name, kind, entities, expected):
        for entity in entities:
            if "name" in entity and entity["name"] == name:
                for k,v in expected.items():
                    self.assertIn(k, entity)
                    self.assertEqual(v, entity[k])
                return
        raise Exception("Could not find %s named %s" % (kind, name))

    def _validate_exchange(self, router, name, **kwargs):
        _ = self.run_qdmanage(router, "query --type %s" % _EXCHANGE_TYPE)
        self._validate_entity(name, "exchange", ast.literal_eval(_), kwargs)

    def _validate_binding(self, router, name, **kwargs):
        _ = self.run_qdmanage(router, "query --type %s" % _BINDING_TYPE)
        self._validate_entity(name, "binding", ast.literal_eval(_), kwargs)

    def test_qdmanage(self):
        """
        Tests the management API via qdmanage
        """
        router = self._create_router("A", [])

        # create exchanges
        ex_config = [
            ["Exchange1", {"address": "Address1"}],
            ["Exchange2", {"address": "Address2",
                           "phase": 2,
                           "alternateAddress": "Alternate2",
                           "alternatePhase": 1,
                           "matchMethod": "mqtt"}]
        ]

        for cfg in ex_config:
            args = ""
            for k, v in cfg[1].items():
                args += "%s=%s " % (k, v)
            self.run_qdmanage(router,
                              "create --type %s --name %s %s" %
                              (_EXCHANGE_TYPE, cfg[0], args))

        # validate
        _ = self.run_qdmanage(router, "query --type %s" % _EXCHANGE_TYPE)
        query = ast.literal_eval(_)
        self.assertEqual(len(ex_config), len(query))
        for cfg in ex_config:
            self._validate_entity(name=cfg[0],
                                  kind="exchange",
                                  entities=query,
                                  expected=cfg[1])
        for ex in query:
            self.assertEqual(0, ex['bindingCount'])

        # create bindings
        binding_config = [
            ["b11", {"exchangeName":    "Exchange1",
                     "bindingKey":      "a.b.*.#",
                     "nextHopAddress":  "nextHop1",
                     "nextHopPhase":    3}],
            ["b12", {"exchangeName":    "Exchange1",
                     "bindingKey":      "a.*.c.#",
                     "nextHopAddress":  "nextHop1",
                     "nextHopPhase":    3}],
            ["b13", {"exchangeName":    "Exchange1",
                     "bindingKey":      "a.b.*.#",
                     "nextHopAddress":  "nextHop2",
                     "nextHopPhase":    0}],
            ["b14", {"exchangeName":    "Exchange1",
                     "bindingKey":      "a.*.c.#",
                     "nextHopAddress":  "nextHop2",
                     "nextHopPhase":    0}],

            ["b21", {"exchangeName":    "Exchange2",
                     "bindingKey":      "a/b/?/#",
                     "nextHopAddress":  "nextHop3"}],
            ["b22", {"exchangeName":    "Exchange2",
                     "bindingKey":      "a",
                     "nextHopAddress":  "nextHop4"}],
            ["b23", {"exchangeName":    "Exchange2",
                     "bindingKey":      "a/b",
                     "nextHopAddress":  "nextHop4"}],
            ["b24", {"exchangeName":    "Exchange2",
                     "bindingKey":      "b",
                     "nextHopAddress":  "nextHop3"}]
        ]

        for cfg in binding_config:
            args = ""
            for k, v in cfg[1].items():
                args += "%s=%s " % (k, v)
            self.run_qdmanage(router,
                              "create --type %s --name %s %s" %
                              (_BINDING_TYPE, cfg[0], args))

        # validate
        _ = self.run_qdmanage(router, "query --type %s" % _BINDING_TYPE)
        bindings = ast.literal_eval(_)
        self.assertEqual(len(binding_config), len(bindings))
        for cfg in binding_config:
            self._validate_entity(name=cfg[0],
                                  kind="binding",
                                  entities=bindings,
                                  expected=cfg[1])

        _ = self.run_qdmanage(router, "query --type %s" % _EXCHANGE_TYPE)
        exchanges = ast.literal_eval(_)
        self.assertEqual(len(ex_config), len(exchanges))
        for ex in exchanges:
            self.assertEqual(4, ex["bindingCount"])

        # verify reads
        _ = self.run_qdmanage(router, "read --type %s --name Exchange2" % _EXCHANGE_TYPE)
        self.assertEqual("Exchange2", ast.literal_eval(_)["name"])
        _ = self.run_qdmanage(router, "read --type %s --name b24" % _BINDING_TYPE)
        self.assertEqual("b24", ast.literal_eval(_)["name"])

        # binding deletion by id:
        bid = bindings[0]["identity"]
        self.run_qdmanage(router, "delete --type " + _BINDING_TYPE +
                              " --identity %s" % bid)
        _ = self.run_qdmanage(router, "query --type %s" % _BINDING_TYPE)
        bindings = ast.literal_eval(_)
        self.assertEqual(len(binding_config) - 1, len(bindings))
        for binding in bindings:
            self.assertFalse(binding["identity"] == bid)

        # binding deletion by name:
        self.run_qdmanage(router, "delete --type " + _BINDING_TYPE +
                              " --name b14")
        _ = self.run_qdmanage(router, "query --type %s" % _BINDING_TYPE)
        bindings = ast.literal_eval(_)
        self.assertEqual(len(binding_config) - 2, len(bindings))
        for binding in bindings:
            self.assertFalse(binding["name"] == "b14")

        # exchange deletion by name:
        self.run_qdmanage(router, "delete --type " + _EXCHANGE_TYPE +
                              " --name Exchange1")
        _ = self.run_qdmanage(router, "query --type %s" % _EXCHANGE_TYPE)
        exchanges = ast.literal_eval(_)
        self.assertEqual(len(ex_config) - 1, len(exchanges))
        self.assertEqual("Exchange2", exchanges[0]["name"])

        # negative testing

        # exchange name is required
        self.assertRaises(Exception, self.run_qdmanage, router,
                          "create --type " + _EXCHANGE_TYPE +
                          " address=Nope")
        # exchange address is required
        self.assertRaises(Exception, self.run_qdmanage, router,
                          "create --type " + _EXCHANGE_TYPE +
                          " --name Nope")
        # duplicate exchange names
        self.assertRaises(Exception, self.run_qdmanage, router,
                          "create --type " + _EXCHANGE_TYPE +
                          " --name Exchange2 address=foo")
        # invalid match method
        self.assertRaises(Exception, self.run_qdmanage, router,
                          "create --type " + _EXCHANGE_TYPE +
                          " --name Exchange3 address=foo"
                          " matchMethod=blinky")
        # duplicate exchange addresses
        self.assertRaises(Exception, self.run_qdmanage, router,
                          "create --type " + _EXCHANGE_TYPE +
                          " --name Nope address=Address2")
        # binding with no exchange name
        self.assertRaises(Exception, self.run_qdmanage, router,
                          "create --type " + _BINDING_TYPE +
                          " --name Nope")
        # binding with bad exchange name
        self.assertRaises(Exception, self.run_qdmanage, router,
                          "create --type " + _BINDING_TYPE +
                          " exchangeName=Nope")
        # binding with duplicate name
        self.assertRaises(Exception, self.run_qdmanage, router,
                          "create --type " + _BINDING_TYPE +
                          " --name b22 exchangeName=Exchange2"
                          " bindingKey=b nextHopAddress=nextHop3")
        # binding with duplicate pattern & next hop
        self.assertRaises(Exception, self.run_qdmanage, router,
                          "create --type " + _BINDING_TYPE +
                          " --name Nuhuh exchangeName=Exchange2"
                          " key=b nextHop=nextHop3")
        # binding with no next hop
        self.assertRaises(Exception, self.run_qdmanage, router,
                          "create --type " + _BINDING_TYPE +
                          " --name Nuhuh exchangeName=Exchange2"
                          " bindingKey=x/y/z")

        # invalid mqtt key
        self.assertRaises(Exception, self.run_qdmanage, router,
                          "create --type " + _BINDING_TYPE +
                          " exchangeName=Exchange2"
                          " bindingKey=x/#/z"
                          " nextHopAddress=Nope")

        # delete exchange by identity:
        self.run_qdmanage(router, "delete --type " + _EXCHANGE_TYPE +
                              " --identity %s" % exchanges[0]["identity"])

    def test_forwarding(self):
        """
        Simple forwarding over a single 0-10 exchange
        """
        config = [
            ('exchange', {'address': 'Address1',
                          'name': 'Exchange1',
                          'matchMethod': 'amqp'}),
            # two different patterns, same next hop:
            ('binding', {'name':           'binding1',
                         'exchangeName':   'Exchange1',
                         'bindingKey':     'a.*',
                         'nextHopAddress': 'nextHop1'}),
            ('binding', {'name':           'binding2',
                         'exchangeName':   'Exchange1',
                         'bindingKey':     'a.b',
                         'nextHopAddress': 'nextHop1'}),
            # duplicate patterns, different next hops:
            ('binding', {'name':           'binding3',
                         'exchangeName':   'Exchange1',
                         'bindingKey':     'a.c.#',
                         'nextHopAddress': 'nextHop1'}),
            ('binding', {'name': 'binding4',
                         'exchangeName':   'Exchange1',
                         'bindingKey':     'a.c.#',
                         'nextHopAddress': 'nextHop2'}),
            # match for nextHop2 only
            ('binding', {'name':           'binding5',
                         'exchangeName':   'Exchange1',
                         'bindingKey':     'a.b.c',
                         'nextHopAddress': 'nextHop2'})
        ]
        router = self._create_router('A', config)

        # create clients for message transfer
        conn = BlockingConnection(router.addresses[0])
        sender = conn.create_sender(address="Address1", options=AtMostOnce())
        nhop1 = conn.create_receiver(address="nextHop1", credit=100)
        nhop2 = conn.create_receiver(address="nextHop2", credit=100)

        # verify initial metrics
        self._validate_exchange(router, name='Exchange1',
                                bindingCount=5,
                                receivedCount=0,
                                droppedCount=0,
                                forwardedCount=0,
                                divertedCount=0)

        for b in range(5):
            self._validate_binding(router,
                                   name='binding%s' % (b + 1),
                                   matchedCount=0)

        # send message with subject "a.b"
        # matches (binding1, binding2)
        # forwarded to NextHop1 only
        sender.send(Message(subject='a.b', body='A'))
        self.assertEqual('A', nhop1.receive(timeout=TIMEOUT).body)

        # send message with subject "a.c"
        # matches (bindings 1,3,4)
        # ->  NextHop1, NextHop2
        sender.send(Message(subject='a.c', body='B'))
        self.assertEqual('B', nhop1.receive(timeout=TIMEOUT).body)
        self.assertEqual('B', nhop2.receive(timeout=TIMEOUT).body)

        # send message with subject "a.c.d"
        # matches bindings 3,4
        # -> NextHop1, NextHop2
        sender.send(Message(subject='a.c.d', body='C'))
        self.assertEqual('C', nhop1.receive(timeout=TIMEOUT).body)
        self.assertEqual('C', nhop2.receive(timeout=TIMEOUT).body)

        # send message with subject "x.y.z"
        # no binding match - expected to drop
        # not forwarded
        sender.send(Message(subject='x.y.z', body=["I am Noone"]))

        # send message with subject "a.b.c"
        # matches binding5
        # -> NextHop2
        sender.send(Message(subject='a.b.c', body='D'))
        self.assertEqual('D', nhop2.receive(timeout=TIMEOUT).body)

        # ensure there are no more messages on either hop:

        self.assertRaises(Timeout, nhop1.receive, timeout=0.25)
        self.assertRaises(Timeout, nhop2.receive, timeout=0.25)

        # validate counters
        self._validate_binding(router, name='binding1',
                               matchedCount=2)
        self._validate_binding(router, name='binding2',
                               matchedCount=1)
        self._validate_binding(router, name='binding3',
                               matchedCount=2)
        self._validate_binding(router, name='binding4',
                               matchedCount=2)
        self._validate_binding(router, name='binding5',
                               matchedCount=1)
        self._validate_exchange(router, name="Exchange1",
                                receivedCount=5,
                                forwardedCount=4,
                                divertedCount=0,
                                droppedCount=1)
        conn.close()

    def test_forwarding_mqtt(self):
        """
        Simple forwarding over a single mqtt exchange
        """
        config = [
            ('exchange', {'address':          'Address2',
                          'name':             'Exchange1',
                          'matchMethod':      'mqtt',
                          'alternateAddress': 'altNextHop'}),

            ('binding', {'name':           'binding1',
                         'exchangeName':   'Exchange1',
                         'bindingKey':     'a/b',
                         'nextHopAddress': 'nextHop1'}),
            ('binding', {'name':           'binding2',
                         'exchangeName':   'Exchange1',
                         'bindingKey':     'a/+',
                         'nextHopAddress': 'nextHop2'}),
            ('binding', {'name':           'binding3',
                         'exchangeName':   'Exchange1',
                         'bindingKey':     'c/#',
                         'nextHopAddress': 'nextHop1'}),
            ('binding', {'name':           'binding4',
                         'exchangeName':   'Exchange1',
                         'bindingKey':     'c/b',
                         'nextHopAddress': 'nextHop2'}),
        ]
        router = self._create_router('B', config)

        # create clients for message transfer
        conn = BlockingConnection(router.addresses[0])
        sender = conn.create_sender(address="Address2", options=AtMostOnce())
        nhop1 = conn.create_receiver(address="nextHop1", credit=100)
        nhop2 = conn.create_receiver(address="nextHop2", credit=100)
        alt = conn.create_receiver(address="altNextHop", credit=100)

        # send message with subject "a.b"
        # matches (binding1, binding2)
        # forwarded to NextHop1, NextHop2
        sender.send(Message(subject='a/b', body='A'))
        self.assertEqual('A', nhop1.receive(timeout=TIMEOUT).body)
        self.assertEqual('A', nhop2.receive(timeout=TIMEOUT).body)

        # send message with subject "a/c"
        # matches binding2
        # ->  NextHop2
        sender.send(Message(subject='a/c', body='B'))
        self.assertEqual('B', nhop2.receive(timeout=TIMEOUT).body)

        # send message with subject "c/b"
        # matches bindings 3,4
        # -> NextHop1, NextHop2
        sender.send(Message(subject='c/b', body='C'))
        self.assertEqual('C', nhop1.receive(timeout=TIMEOUT).body)
        self.assertEqual('C', nhop2.receive(timeout=TIMEOUT).body)

        # send message with subject "c/b/dee/eee"
        # matches binding3
        # -> NextHop1
        sender.send(Message(subject='c/b/dee/eee', body='D'))
        self.assertEqual('D', nhop1.receive(timeout=TIMEOUT).body)

        # send message with subject "x.y.z"
        # no binding match
        # -> alternate
        sender.send(Message(subject='x.y.z', body="?"))
        self.assertEqual('?', alt.receive(timeout=TIMEOUT).body)

        # ensure there are no more messages on either hop:

        self.assertRaises(Timeout, nhop1.receive, timeout=0.25)
        self.assertRaises(Timeout, nhop2.receive, timeout=0.25)
        self.assertRaises(Timeout, alt.receive, timeout=0.25)

        # validate counters
        self._validate_binding(router, name='binding1',
                               matchedCount=1)
        self._validate_binding(router, name='binding2',
                               matchedCount=2)
        self._validate_binding(router, name='binding3',
                               matchedCount=2)
        self._validate_binding(router, name='binding4',
                               matchedCount=1)
        self._validate_exchange(router, name="Exchange1",
                                receivedCount=5,
                                forwardedCount=5,
                                divertedCount=1,
                                droppedCount=0)
        conn.close()

    def test_forwarding_sync(self):
        """
        Forward unsettled messages to multiple subscribers
        """
        config = [
            ('router',   {'mode': 'standalone', 'id': 'QDR.mcast'}),
            ('listener', {'role': 'normal', 'host': '0.0.0.0',
                          'port': self.tester.get_port(),
                          'saslMechanisms':'ANONYMOUS'}),
            ('address', {'pattern': 'nextHop2/#', 'distribution': 'multicast'}),
            ('exchange', {'address':          'Address3',
                          'name':             'Exchange1',
                          'alternateAddress': 'altNextHop'}),
            ('binding', {'name':           'binding1',
                         'exchangeName':   'Exchange1',
                         'bindingKey':     'a.b',
                         'nextHopAddress': 'nextHop1'}),
            ('binding', {'name':           'binding2',
                         'exchangeName':   'Exchange1',
                         'bindingKey':     '*.b',
                         'nextHopAddress': 'nextHop2'})
        ]
        router = self.tester.qdrouterd('QDR.mcast', Qdrouterd.Config(config))

        # create clients for message transfer
        conn = BlockingConnection(router.addresses[0])
        sender = conn.create_sender(address="Address3", options=AtLeastOnce())
        nhop1 = AsyncTestReceiver(address=router.addresses[0], source="nextHop1")
        nhop2A = AsyncTestReceiver(address=router.addresses[0], source="nextHop2")
        nhop2B = AsyncTestReceiver(address=router.addresses[0], source="nextHop2")
        alt = AsyncTestReceiver(address=router.addresses[0], source="altNextHop")

        sender.send(Message(subject='a.b', body='A'))
        sender.send(Message(subject='x.y', body='B'))

        self.assertEqual('A', nhop1.queue.get(timeout=TIMEOUT).body)
        self.assertEqual('A', nhop2A.queue.get(timeout=TIMEOUT).body)
        self.assertEqual('A', nhop2B.queue.get(timeout=TIMEOUT).body)
        self.assertEqual('B', alt.queue.get(timeout=TIMEOUT).body)
        nhop1.stop()
        nhop2A.stop()
        nhop2B.stop()
        alt.stop()
        conn.close()

        self.assertTrue(nhop1.queue.empty())
        self.assertTrue(nhop2A.queue.empty())
        self.assertTrue(nhop2B.queue.empty())
        self.assertTrue(alt.queue.empty())

    def test_remote_exchange(self):
        """
        Verify that the exchange and bindings are visible to other routers in
        the network
        """
        def router(self, name, extra_config):

            config = [
                ('router', {'mode': 'interior', 'id': 'QDR.%s'%name, 'allowUnsettledMulticast': 'yes'}),
                ('listener', {'port': self.tester.get_port(), 'stripAnnotations': 'no'})
            ] + extra_config

            config = Qdrouterd.Config(config)

            self.routers.append(self.tester.qdrouterd(name, config, wait=True))

        self.inter_router_port = self.tester.get_port()
        self.routers = []

        router(self, 'A',
               [('listener',
                 {'role': 'inter-router', 'port': self.inter_router_port}),

                ('address', {'pattern': 'nextHop1/#',
                             'distribution': 'multicast'}),
                ('address', {'pattern': 'nextHop2/#',
                             'distribution': 'balanced'}),
                ('address', {'pattern': 'nextHop3/#',
                             'distribution': 'closest'}),

                ('exchange', {'address': 'AddressA',
                              'name': 'ExchangeA',
                              'matchMethod': 'mqtt'}),

                ('binding', {'name':           'bindingA1',
                             'exchangeName':   'ExchangeA',
                             'bindingKey':     'a/b',
                             'nextHopAddress': 'nextHop1'}),
                ('binding', {'name':           'bindingA2',
                             'exchangeName':   'ExchangeA',
                             'bindingKey':     'a/+',
                             'nextHopAddress': 'nextHop2'}),
                ('binding', {'name':           'bindingA3',
                             'exchangeName':   'ExchangeA',
                             'bindingKey':     '+/b',
                             'nextHopAddress': 'nextHop3'}),
                ('binding', {'name':           'bindingA4',
                             'exchangeName':   'ExchangeA',
                             'bindingKey':     'a/#',
                             'nextHopAddress': 'NotSubscribed'})
               ])

        router(self, 'B',
               [('connector', {'name': 'connectorToA',
                               'role': 'inter-router',
                               'port': self.inter_router_port}),
                ('address', {'pattern': 'nextHop1/#',
                             'distribution': 'multicast'}),
                ('address', {'pattern': 'nextHop2/#',
                             'distribution': 'balanced'}),
                ('address', {'pattern': 'nextHop3/#',
                             'distribution': 'closest'})
               ])

        self.routers[0].wait_router_connected('QDR.B')
        self.routers[1].wait_router_connected('QDR.A')
        self.routers[1].wait_address('AddressA')

        # connect clients to router B (no exchange)
        nhop1A = AsyncTestReceiver(self.routers[1].addresses[0], 'nextHop1')
        nhop1B = AsyncTestReceiver(self.routers[1].addresses[0], 'nextHop1')
        nhop2  = AsyncTestReceiver(self.routers[1].addresses[0], 'nextHop2')
        nhop3  = AsyncTestReceiver(self.routers[1].addresses[0], 'nextHop3')

        self.routers[0].wait_address('nextHop1', remotes=1)
        self.routers[0].wait_address('nextHop2', remotes=1)
        self.routers[0].wait_address('nextHop3', remotes=1)

        conn = BlockingConnection(self.routers[1].addresses[0])
        sender = conn.create_sender(address="AddressA", options=AtLeastOnce())
        sender.send(Message(subject='a/b', body='Hi!'))

        # multicast
        self.assertEqual('Hi!', nhop1A.queue.get(timeout=TIMEOUT).body)
        self.assertEqual('Hi!', nhop1B.queue.get(timeout=TIMEOUT).body)

        # balanced and closest
        self.assertEqual('Hi!', nhop2.queue.get(timeout=TIMEOUT).body)
        self.assertEqual('Hi!', nhop3.queue.get(timeout=TIMEOUT).body)

        nhop1A.stop()
        nhop1B.stop()
        nhop2.stop()
        nhop3.stop()
        conn.close()

    def test_large_messages(self):
        """
        Verify that multi-frame messages are forwarded properly
        """
        MAX_FRAME=1024
        config = [
            ('router', {'mode': 'interior', 'id': 'QDR.X',
                        'allowUnsettledMulticast': 'yes'}),
            ('listener', {'port': self.tester.get_port(),
                          'stripAnnotations': 'no',
                          'maxFrameSize': MAX_FRAME}),

            ('address', {'pattern': 'nextHop1/#',
                         'distribution': 'multicast'}),

            ('exchange', {'address': 'AddressA',
                          'name': 'ExchangeA'}),

            ('binding', {'name':           'bindingA1',
                         'exchangeName':   'ExchangeA',
                         'bindingKey':     'a/b',
                         'nextHopAddress': 'nextHop1'})
        ]

        router = self.tester.qdrouterd('QDR.X',
                                       Qdrouterd.Config(config),
                                       wait=True)

        # connect clients to router B (no exchange)
        nhop1A = AsyncTestReceiver(router.addresses[0], 'nextHop1',
                                   conn_args={'max_frame_size': MAX_FRAME})
        nhop1B = AsyncTestReceiver(router.addresses[0], 'nextHop1',
                                   conn_args={'max_frame_size': MAX_FRAME})

        conn = BlockingConnection(router.addresses[0],
                                  max_frame_size=MAX_FRAME)
        sender = conn.create_sender(address="AddressA")
        jumbo = (10 * MAX_FRAME) * 'X'
        sender.send(Message(subject='a/b', body=jumbo))

        # multicast
        self.assertEqual(jumbo, nhop1A.queue.get(timeout=TIMEOUT).body)
        self.assertEqual(jumbo, nhop1B.queue.get(timeout=TIMEOUT).body)

        nhop1A.stop()
        nhop1B.stop()
        conn.close()

    def test_forwarding_fanout(self):
        """
        Verify bindings that do not have a key receive all messages
        """
        config = [
            ('exchange', {'address': 'AddressF',
                          'name': 'ExchangeF'}),
            ('binding', {'name':           'binding1',
                         'exchangeName':   'ExchangeF',
                         'bindingKey':     'pattern',
                         'nextHopAddress': 'nextHop1'}),
            # two bindings w/o key
            ('binding', {'name':           'binding2',
                         'exchangeName':   'ExchangeF',
                         'nextHopAddress': 'nextHop2'}),
            ('binding', {'name':           'binding3',
                         'exchangeName':   'ExchangeF',
                         'nextHopAddress': 'nextHop3'})
        ]

        for meth in ['amqp', 'mqtt']:
            config[0][1]['matchMethod'] = meth
            router = self._create_router('A', config)

            # create clients for message transfer
            conn = BlockingConnection(router.addresses[0])
            sender = conn.create_sender(address="AddressF", options=AtMostOnce())
            nhop1 = conn.create_receiver(address="nextHop1", credit=100)
            nhop2 = conn.create_receiver(address="nextHop2", credit=100)
            nhop3 = conn.create_receiver(address="nextHop3", credit=100)

            # send message with subject "nope"
            # should arrive at nextHop2 & 3 only
            sender.send(Message(subject='nope', body='A'))
            self.assertEqual('A', nhop2.receive(timeout=TIMEOUT).body)
            self.assertEqual('A', nhop3.receive(timeout=TIMEOUT).body)

            # send message with subject "pattern"
            # forwarded to all bindings:
            sender.send(Message(subject='pattern', body='B'))
            self.assertEqual('B', nhop1.receive(timeout=TIMEOUT).body)
            self.assertEqual('B', nhop2.receive(timeout=TIMEOUT).body)
            self.assertEqual('B', nhop3.receive(timeout=TIMEOUT).body)

            conn.close()
            router.teardown()


if __name__ == '__main__':
    unittest.main(main_module())


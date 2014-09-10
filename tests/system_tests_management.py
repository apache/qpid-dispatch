##
## Licensed to the Apache Software Foundation (ASF) under one
## or more contributor license agreements.  See the NOTICE file
## distributed with this work for additional information
## regarding copyright ownership.  The ASF licenses this file
## to you under the Apache License, Version 2.0 (the
## "License"); you may not use this file except in compliance
## with the License.  You may obtain a copy of the License at
##
##   http://www.apache.org/licenses/LICENSE-2.0
##
## Unless required by applicable law or agreed to in writing,
## software distributed under the License is distributed on an
## "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
## KIND, either express or implied.  See the License for the
## specific language governing permissions and limitations
## under the License
##

"""System tests for management of qdrouter"""

import unittest, system_test, re, os
from qpid_dispatch.management import Node, ManagementError, Url, BadRequest, NotImplemented, NotFound, Forbidden
from system_test import Qdrouterd, message, retry

LISTENER = 'org.apache.qpid.dispatch.listener'
CONNECTOR = 'org.apache.qpid.dispatch.connector'
FIXED_ADDRESS = 'org.apache.qpid.dispatch.fixed-address'
WAYPOINT = 'org.apache.qpid.dispatch.waypoint'
DUMMY = 'org.apache.qpid.dispatch.dummy'

ADDRESS = 'org.apache.qpid.dispatch.router.address'

class ManagementTest(system_test.TestCase): # pylint: disable=too-many-public-methods

    @classmethod
    def setUpClass(cls):
        super(ManagementTest, cls).setUpClass()
        name = cls.__name__
        cls.log_file = name+".log"
        conf = Qdrouterd.Config([
            ('log', {'module':'DEFAULT', 'level':'trace', 'output':cls.log_file}),
            ('router', { 'mode': 'standalone', 'router-id': name}),
            ('listener', {'name': 'l0', 'port':cls.get_port(), 'role':'normal'}),
            # Extra listeners to exercise managment query
            ('listener', {'name': 'l1', 'port':cls.get_port(), 'role':'normal'}),
            ('listener', {'name': 'l2', 'port':cls.get_port(), 'role':'normal'})
        ])
        cls.router = cls.tester.qdrouterd('%s'%name, conf)
        cls.router.wait_ready()

    def setUp(self):
        super(ManagementTest, self).setUp()
        self.node = self.cleanup(Node(self.router.addresses[0]))
        # Temporary access to separate new management address.
        self.node2 = self.cleanup(Node(Url(self.router.addresses[0], path='$management2')))
        self.maxDiff = None
        self.longMessage = True
        unittest.util._MAX_LENGTH = 256 # Monkey patch unittest truncation

    def test_bad_query(self):
        """Test that various badly formed queries get the proper response"""
        # No operation attribute
        self.assertRaises(BadRequest, self.node.call, self.node.request())
        # Unknown operation
        self.assertRaises(NotImplemented, self.node.call, self.node.request(operation="nosuch"))
        # No entityType or attributeList
        self.assertRaises(BadRequest, self.node.query)

    def test_query_type(self):
        """Query with type only"""
        response = self.node2.query(type=LISTENER)
        for attr in ['type', 'name', 'identity', 'addr', 'port']:
            self.assertTrue(attr in response.attribute_names)
        for r in response.entities: # Check types
            self.assertEqual(len(response.attribute_names), len(r.attributes))
            self.assertEqual(r['type'], LISTENER)
        self.assertTrue(
            set(['l0', 'l1', 'l2']) <= set(r['name'] for r in response.entities))

    def test_query_type_attributes(self):
        """Query with type and attribute names"""
        attribute_names=['type', 'name', 'port']
        response = self.node2.query(type=LISTENER, attribute_names=attribute_names)
        self.assertEqual(attribute_names, response.attribute_names)
        expect = [[LISTENER, 'l%s' % i, str(self.router.ports[i])] for i in xrange(3)]
        for r in expect: # We might have extras in results due to create tests
            self.assertTrue(r in response.results)
            self.assertTrue(dict(zip(attribute_names, r)) in [e.attributes for e in response.entities])

    def test_query_attributes(self):
        """Query with attributes only"""
        attribute_names=['type', 'name', 'port']
        response = self.node2.query(attribute_names=attribute_names)
        self.assertEqual(attribute_names, response.attribute_names)
        expect = [[LISTENER, 'l%s' % i, str(self.router.ports[i])] for i in xrange(3)]
        for r in expect: # We might have extras in results due to create tests
            self.assertTrue(r in response.results)
        for name in ['router0', 'log0']:
            self.assertTrue([r for r in response.entities if r['name'] == name],
                            msg="Can't find result with name '%s'" % name)

    def assertMapSubset(self, small, big):
        """Assert that mapping small is a subset of mapping big"""
        missing = [(k, v) for k, v in small.items() if (k, v) not in big.items()]
        assert not missing, "Not a subset, missing %s, sub=%s, super=%s"%(missing, small, big)

    def assert_create_ok(self, type, name, attributes):
        entity = self.node2.create(type, name, attributes)
        self.assertMapSubset(attributes, entity.attributes)
        return entity

    def test_create_listener(self):
        """Create a new listener on a running router"""

        port = self.get_port()
        # Note qdrouter schema defines port as string not int, since it can be a service name.
        attributes = {'name':'foo', 'port':str(port), 'role':'normal', 'sasl-mechanisms': 'ANONYMOUS'}
        entity = self.assert_create_ok(LISTENER, 'foo', attributes)
        self.assertEqual(entity['identity'], attributes['name'])
        self.assertEqual(entity['addr'], '0.0.0.0')

        # Connect via the new listener
        node3 = self.cleanup(Node(Url(port=port, path='$management')))
        router = node3.query(type='org.apache.qpid.dispatch.router').entities
        self.assertEqual(self.__class__.router.name, router[0]['name'])

    def test_create_log(self):
        """Create a log entity"""
        # FIXME aconway 2014-07-04: rework log entity.
        # - allow auto-assigned name/identity? Use module as name/identity?
        # - 1 entity with full log state, allow updates.
        log = os.path.abspath("test_create_log.log")
        # FIXME aconway 2014-09-08: PYAGENT->AGENT
        self.assert_create_ok('log', 'log.1', dict(module='PYAGENT', level="error", output=log))
        # Cause an error and verify it shows up in the log file.
        self.assertRaises(ManagementError, self.node2.create, type='nosuch', name='nosuch')
        f = self.cleanup(open(log))
        logstr = f.read()
        self.assertTrue(re.search(r'ValidationError.*nosuch', logstr),
                        msg="Can't find expected ValidationError.*nosuch in '%r'" % logstr)

    def test_create_fixed_address(self):
        self.assert_create_ok(FIXED_ADDRESS, 'fixed1', dict(prefix='fixed1'))
        msgr = self.messenger(flush=True)
        address = self.router.addresses[0]+'/fixed1'
        msgr.subscribe(address)
        msgr.put(message(address=address, body='hello'))
        self.assertEqual('hello', msgr.fetch().body)

    def test_create_connector_waypoint(self):
        """Test creating waypoint, connector and fixed-address
        Create a waypoint that leads out and back from a second router.
        """
        conf = Qdrouterd.Config([
            ('log', {'module':'DEFAULT', 'level':'trace', 'output':'wp-router.log'}),
            ('router', {'mode': 'standalone', 'router-id': 'wp-router'}),
            ('listener', {'port':self.get_port(), 'role':'normal'}),
            ('fixed-address', {'prefix':'foo'})
        ])
        wp_router = self.qdrouterd('wp-router', conf)
        wp_router.wait_ready()

        # Configure the router
        for c in [
                (FIXED_ADDRESS, 'a1', {'prefix':'foo', 'phase':0, 'fanout':'single', 'bias':'spread'}),
                (FIXED_ADDRESS, 'a2', {'prefix':'foo', 'phase':1, 'fanout':'single', 'bias':'spread'}),
                (CONNECTOR, 'wp_connector', {'port':str(wp_router.ports[0]), 'sasl-mechanisms': 'ANONYMOUS', 'role': 'on-demand'}),
                (WAYPOINT, 'wp', {'address': 'foo', 'in-phase': 0, 'out-phase': 1, 'connector': 'wp_connector'})
        ]:
            self.assert_create_ok(*c)
        assert retry(lambda: self.router.is_connected, wp_router.ports[0])

        # Send a message through self.router, verify it goes via wp_router
        address=self.router.addresses[0]+"/foo"
        mr = self.messenger(flush=True)
        mr.subscribe(address)
        messages = ['a', 'b', 'c']
        for m in messages:
            mr.put(message(address=address, body=m)); mr.send()

        # Check messages arrived
        self.assertEqual(messages, [mr.fetch().body for i in messages])

        # Check log files to verify that the messages went via wp_router
        # TODO aconway 2014-07-07: should be able to check this via management
        # stats instead.
        try:
            f = open('wp-router.log')
            self.assertEqual(6, len(re.findall(r'MESSAGE.*to=.*/foo', f.read())))
        finally:
            f.close()

    def test_entity(self):
        entity = self.node2.read(type=LISTENER, name='l0')
        self.assertEqual('l0', entity.name)
        self.assertEqual('l0', entity.identity)
        self.assertEqual(str(self.router.ports[0]), entity.port)

        entity = self.node2.read(type=LISTENER, identity='l1')
        self.assertEqual('l1', entity.name)
        self.assertEqual('l1', entity.identity)
        self.assertEqual(str(self.router.ports[1]), entity.port)

        # Can't specify both name and identity
        self.assertRaises(BadRequest, self.node2.read, type=LISTENER, name='l0', identity='l1')

        # Bad type
        self.assertRaises(NotFound, self.node2.read, type=CONNECTOR, name='l0')

        # Unknown entity
        self.assertRaises(NotFound, self.node2.read, type=LISTENER, name='nosuch')

        # Update and delete are not allowed by the schema
        self.assertRaises(Forbidden, entity.update)
        self.assertRaises(Forbidden, entity.delete)

        # Non-standard request is not allowed by schema.
        self.assertRaises(Forbidden, entity.call, 'nosuchop', foo="bar")

        # Dummy entity supports all CRUD operations
        dummy = self.node2.create(type=DUMMY, name='MyDummy', attributes={'arg1': 'START'})
        self.assertEqual(dummy.type, DUMMY)
        self.assertEqual(dummy.name, 'MyDummy')
        self.assertEqual(dummy.arg1, 'START')
        identity = dummy.identity
        self.assertEqual(
            dict(type=DUMMY, identity=identity, name='MyDummy', arg1='START'),
            dummy.attributes)

        dummy.attributes['num1'] = 42
        dummy.arg1 = 'one'
        self.assertEqual(
            dict(type=DUMMY, identity=identity, name='MyDummy', arg1='one', num1=42),
            dummy.attributes)
        dummy.update()

        dummy.attributes.update(dict(arg1='x', num1=0))
        dummy.read()
        self.assertEqual(
            dict(type=DUMMY, name='MyDummy', identity=identity, arg1='one', num1=42),
            dummy.attributes)

        dummy2 = self.node2.read(type=DUMMY, name='MyDummy')
        self.assertEqual(dummy.attributes, dummy2.attributes)

        self.assertEqual({'operation': 'callme', 'foo': 'bar', 'type': DUMMY, 'identity': identity},
                         dummy.call('callme', foo='bar'))

        dummy.badattribute = 'Bad'
        self.assertRaises(BadRequest, dummy.update)

        dummy.delete()
        self.assertRaises(NotFound, self.node2.read, type=DUMMY, name='MyDummy')

    def test_get_types(self):
        self.assertRaises(NotImplemented, self.node2.get_types)

    def test_get_attributes(self):
        self.assertRaises(NotImplemented, self.node2.get_attributes)

    def test_get_operations(self):
        self.assertRaises(NotImplemented, self.node2.get_operations)

    def test_get_other_nodes(self):
        self.assertRaises(NotImplemented, self.node2.get_other_nodes)

if __name__ == '__main__':
    unittest.main()

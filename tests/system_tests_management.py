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
from qpid_dispatch_internal.management import Node, ManagementError, Url, BAD_REQUEST, NOT_IMPLEMENTED, STATUS_TEXT, NOT_FOUND
from system_test import Qdrouterd, message, retry

LISTENER = 'org.apache.qpid.dispatch.listener'
CONNECTOR = 'org.apache.qpid.dispatch.connector'
FIXED_ADDRESS = 'org.apache.qpid.dispatch.fixed-address'
WAYPOINT = 'org.apache.qpid.dispatch.waypoint'

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

    def assertRaisesManagement(self, status, call, *args, **kwargs):
        """Assert that call(*args, **kwargs) raises a ManagementError with status"""
        try:
            call(*args, **kwargs)
            self.fail("Expected ManagementError(%r)"%(STATUS_TEXT[status]))
        except ManagementError, e:
            self.assertEqual(status, e.status,
                             msg="Expected status %r, got '%s'"%(STATUS_TEXT[status], e))

    def test_bad_query(self):
        """Test that various badly formed queries get the proper response"""
        # No operation attribute
        self.assertRaisesManagement(BAD_REQUEST, self.node.call, self.node.request())
        # Unknown operation
        self.assertRaisesManagement(NOT_IMPLEMENTED, self.node.call,
                                    self.node.request(operation="nosuch"))
        # No entityType or attributeList
        self.assertRaisesManagement(BAD_REQUEST, self.node.query)

    def test_query_type(self):
        address = 'org.apache.qpid.dispatch.router.address'
        response = self.node.query(type=address)
        self.assertEqual(response.attribute_names[0:3], ['type', 'name', 'identity'])
        for r in response.result_maps:  # Check types
            self.assertEqual(r['type'], address)
        names = [r['name'] for r in response.result_maps]
        self.assertTrue('L$management' in names)
        self.assertTrue('M0$management' in names)

    def test_query_type(self):
        """Query with type only"""
        response = self.node2.query(type=LISTENER)
        for attr in ['type', 'name', 'identity', 'addr', 'port']:
            self.assertIn(attr, response.attribute_names)
        for r in response.result_maps:           # Check types
            self.assertEqual(len(response.attribute_names), len(r))
            self.assertEqual(r['type'], LISTENER)
        self.assertTrue(
            set(['l0', 'l1', 'l2']) <= set(r['name'] for r in response.result_maps))

    def test_query_type_attributes(self):
        """Query with type and attribute names"""
        attribute_names=['type', 'name', 'port']
        response = self.node2.query(type=LISTENER, attribute_names=attribute_names)
        self.assertEqual(attribute_names, response.attribute_names)
        expect = [[LISTENER, 'l%s' % i, str(self.router.ports[i])] for i in xrange(3)]
        for r in expect: # We might have extras in results due to create tests
            self.assertIn(r, response.results)
            self.assertIn(dict(zip(attribute_names, r)), response.result_maps)

    def test_query_attributes(self):
        """Query with attributes only"""
        attribute_names=['type', 'name', 'port']
        response = self.node2.query(attribute_names=attribute_names)
        self.assertEqual(attribute_names, response.attribute_names)
        expect = [[LISTENER, 'l%s' % i, str(self.router.ports[i])] for i in xrange(3)]
        for r in expect: # We might have extras in results due to create tests
            self.assertIn(r, response.results)
        for name in ['router0', 'log0']:
            self.assertTrue([r for r in response.result_maps if r['name'] == name],
                            msg="Can't find result with name '%s'" % name)

    def assertMapSubset(self, sub, super):
        """Assert that mapping sub is a subset of mapping super"""
        missing = [(k, v) for k, v in sub.items() if (k, v) not in super.items()]
        assert not missing, "Not a subset, missing %s, sub=%s, super=%s"%(missing, sub, super)

    def assert_create_ok(self, type, name, attributes):
        result = self.node2.create(type, name, attributes)
        self.assertMapSubset(attributes, result)
        return result

    def test_create_listener(self):
        """Create a new listener on a running router"""

        port = self.get_port()
        # Note qdrouter schema defines port as string not int, since it can be a service name.
        attributes = {'name':'foo', 'port':str(port), 'role':'normal', 'sasl-mechanisms': 'ANONYMOUS'}
        result = self.assert_create_ok(LISTENER, 'foo', attributes)
        self.assertEqual(result['identity'], attributes['name'])
        self.assertEqual(result['addr'], '0.0.0.0')

        # Connect via the new listener
        node3 = self.cleanup(Node(Url(port=port, path='$management')))
        router = node3.query(type='org.apache.qpid.dispatch.router')
        self.assertEqual(self.__class__.router.name, router.result_maps[0]['name'])

    def test_create_log(self):
        """Create a log entity"""
        # FIXME aconway 2014-07-04: rework log entity.
        # - allow auto-assigned name/identity? Use module as name/identity?
        # - 1 entity with full log state, allow updates.
        log = os.path.abspath("test_create_log.log")
        self.assert_create_ok('log', 'log.1', dict(module='AGENT', level="error", output=log))
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

    def test_read(self):
        attributes = self.node2.read(type=LISTENER, name='l0')
        self.assertEqual('l0', attributes['name'])
        self.assertEqual('l0', attributes['identity'])
        self.assertEqual(str(self.router.ports[0]), attributes['port'])

        attributes = self.node2.read(type=LISTENER, identity='l1')
        self.assertEqual('l1', attributes['name'])
        self.assertEqual('l1', attributes['identity'])
        self.assertEqual(str(self.router.ports[1]), attributes['port'])

        # Can't specify both name and identity
        self.assertRaisesManagement(
            BAD_REQUEST, self.node2.read, type=LISTENER, name='l0', identity='l1')

        # Bad type
        self.assertRaisesManagement(
            NOT_FOUND, self.node2.read, type=CONNECTOR, name='l0')

        # Unknown entity
        self.assertRaisesManagement(
            NOT_FOUND, self.node2.read, type=LISTENER, name='nosuch')

if __name__ == '__main__':
    unittest.main()

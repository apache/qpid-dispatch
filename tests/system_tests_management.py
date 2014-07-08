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
from qpid_dispatch_internal.management import Node, ManagementError, Url
from system_test import Qdrouterd, message, retry
from httplib import BAD_REQUEST, NOT_IMPLEMENTED

class ManagementTest(system_test.TestCase): # pylint: disable=too-many-public-methods

    @classmethod
    def setUpClass(cls):
        super(ManagementTest, cls).setUpClass()
        name = cls.__name__
        cls.log_file = name+".log"
        conf = Qdrouterd.Config([
            ('log', {'module':'DEFAULT', 'level':'trace', 'output':cls.log_file}),
            ('router', {'mode': 'standalone', 'router-id': name}),
            ('listener', {'port':cls.get_port(), 'role':'normal'})
        ])
        cls.router = cls.tester.qdrouterd('%s'%name, conf)
        cls.router.wait_ready()

    def setUp(self):
        super(ManagementTest, self).setUp()
        self.node = self.cleanup(Node(self.router.addresses[0]))
        # Temporary access to separate new management address.
        self.node2 = self.cleanup(Node(Url(self.router.addresses[0], path='$management2')))

    def assertRaisesManagement(self, status, pattern, call, *args, **kwargs):
        """Assert that call(*args, **kwargs) raises a ManagementError
        with status and matching pattern in description """
        try:
            call(*args, **kwargs)
            self.fail("Expected ManagementError with %s, %s"%(status, pattern))
        except ManagementError, e:
            self.assertEqual(e.status, status)
            assert re.search("(?i)"+pattern, e.description), "No match for %s in %s"%(pattern, e.description)

    def test_bad_query(self):
        """Test that various badly formed queries get the proper response"""
        self.assertRaisesManagement(
            BAD_REQUEST, "No operation", self.node.call, self.node.request())
        self.assertRaisesManagement(
            NOT_IMPLEMENTED, "Not Implemented: nosuch",
            self.node.call, self.node.request(operation="nosuch"))
        self.assertRaisesManagement(
            BAD_REQUEST, r'(entityType|attributeNames).*must be provided',
            self.node.query)

    def test_query_type(self):
        address = 'org.apache.qpid.dispatch.router.address'
        response = self.node.query(type=address)
        self.assertEqual(response.attribute_names[0:3], ['type', 'name', 'identity'])
        for r in response:  # Check types
            self.assertEqual(r.type, address)
        names = [r.name for r in response]
        self.assertTrue('L$management' in names)
        self.assertTrue('M0$management' in names)

        # TODO aconway 2014-06-05: negative test: offset, count not implemented on router
        try:
            # Try offset, count
            self.assertGreater(len(names), 2)
            response0 = self.node.query(type=address, count=1)
            self.assertEqual(names[0:1], [r[1] for r in response0])
            response1_2 = self.node.query(type=address, count=2, offset=1)
            self.assertEqual(names[1:3], [r[1] for r in response1_2])
            self.fail("Negative test passed!")
        except: pass

    def test_query_attribute_names(self):
        """TODO aconway 2014-06-05: negative test: attribute_names query doesn't work."""
        response = self.node.query(attribute_names=["type", "name", "identity"])
        try:
            self.assertNotEqual([], response)
            self.fail("Negative test passed!")
        except: pass

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
        result = self.assert_create_ok('org.apache.qpid.dispatch.listener', 'foo', attributes)
        self.assertEqual(result['identity'], attributes['name'])
        self.assertEqual(result['addr'], '0.0.0.0')

        # Connect via the new listener
        node3 = self.cleanup(Node(Url(port=port, path='$management')))
        router = node3.query(type='org.apache.qpid.dispatch.router')
        self.assertEqual(self.__class__.router.name, router[0]['name'])

    def test_create_log(self):
        """Create a log entity"""
        # FIXME aconway 2014-07-04:
        # - allow auto-assigned name/identity? Use module as name/identity?
        # - rework log entity: 1 entity with full log state, allow updates.
        log = os.path.abspath("test_create_log.log")
        self.assert_create_ok('log', 'log.1', dict(module='AGENT', level="error", output=log))
        # Cause an error and verify it shows up in the log file.
        self.assertRaises(ManagementError, self.node2.create, type='nosuch', name='nosuch')
        f = self.cleanup(open(log))
        logstr = f.read()
        self.assertTrue(re.search(r'ValidationError.*nosuch', logstr),
                        msg="Can't find expected ValidationError.*nosuch in '%r'" % logstr)

    def test_create_fixed_address(self):
        self.assert_create_ok('fixed-address', 'fixed1', dict(prefix='fixed1'))
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
                ('fixed-address', 'a1', {'prefix':'foo', 'phase':0, 'fanout':'single', 'bias':'spread'}),
                ('fixed-address', 'a2', {'prefix':'foo', 'phase':1, 'fanout':'single', 'bias':'spread'}),
                ('connector', 'wp_connector', {'port':str(wp_router.ports[0]), 'sasl-mechanisms': 'ANONYMOUS', 'role': 'on-demand'}),
                ('waypoint', 'wp', {'address': 'foo', 'in-phase': 0, 'out-phase': 1, 'connector': 'wp_connector'})
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

if __name__ == '__main__':
    unittest.main()

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

import unittest, system_test, re
from qpid_dispatch_internal.management import amqp
from system_test import Qdrouterd, MISSING_REQUIREMENTS
from httplib import BAD_REQUEST, NOT_IMPLEMENTED

class ManagementTest(system_test.TestCase): # pylint: disable=too-many-public-methods

    @classmethod
    def setUpClass(cls):
        super(ManagementTest, cls).setUpClass()
        name = cls.__name__
        conf = Qdrouterd.Config([
            ('log', {'module':'DEFAULT', 'level':'trace', 'output':name+".log"}),
            ('router', {'mode': 'standalone', 'router-id': name}),
            ('listener', {'port':cls.get_port(), 'role':'normal'})
        ])
        cls.router = cls.tester.qdrouterd('%s'%name, conf)
        cls.router.wait_ready()

    def setUp(self):
        super(ManagementTest, self).setUp()
        self.node = self.cleanup(amqp.Node(self.router.addresses[0]))

    def assertRaisesManagement(self, status, pattern, call, *args, **kwargs):
        """Assert that call(*args, **kwargs) raises a ManagementError
        with status and matching pattern in description """
        try:
            call(*args, **kwargs)
            self.fail("Expected ManagementError with %s, %s"%(status, pattern))
        except amqp.ManagementError, e:
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

    def test_query_entity_type(self):
        # FIXME aconway 2014-06-03: prefix support in Node, get from schema.
        address = 'org.apache.qpid.dispatch.router.address'
        response = self.node.query(entity_type=address)
        self.assertEqual(response.attribute_names[0:3], ['type', 'name', 'identity'])
        for r in response.results:  # Check types
            self.assertEqual(r[0], address)
        names = [r[1] for r in response.results]
        self.assertTrue('L$management' in names)
        self.assertTrue('M0$management' in names)

        # FIXME aconway 2014-06-05: negative test: offset, count not implemented on router
        try:
            # Try offset, count
            self.assertGreater(len(names), 2)
            response0 = self.node.query(entity_type=address, count=1)
            self.assertEqual(names[0:1], [r[1] for r in response0.results])
            response1_2 = self.node.query(entity_type=address, count=2, offset=1)
            self.assertEqual(names[1:3], [r[1] for r in response1_2.results])
            self.fail("Negative test passed!")
        except: pass

    def test_query_attribute_names(self):
        response = self.node.query(attribute_names=["type", "name", "identity"])
        # FIXME aconway 2014-06-05: negative test: attribute_names query doesn't work.
        # Need a better test.
        try:
            self.assertNotEqual([], response.results)
            self.fail("Negative test passed!")
        except: pass

if __name__ == '__main__':
    if MISSING_REQUIREMENTS:
        print MISSING_REQUIREMENTS
    else:
        unittest.main()

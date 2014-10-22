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
from qpid_dispatch.management import Node, ManagementError, Url, BadRequestStatus, NotImplementedStatus, NotFoundStatus, ForbiddenStatus
from system_test import Qdrouterd, message, retry

DISPATCH = 'org.apache.qpid.dispatch'
LISTENER = DISPATCH + '.listener'
CONNECTOR = DISPATCH + '.connector'
FIXED_ADDRESS = DISPATCH + '.fixedAddress'
WAYPOINT = DISPATCH + '.waypoint'
DUMMY = DISPATCH + '.dummy'
ROUTER = DISPATCH + '.router'
LINK = ROUTER + '.link'
ADDRESS = ROUTER + '.address'
NODE = ROUTER + '.node'


class ManagementTest(system_test.TestCase): # pylint: disable=too-many-public-methods

    @classmethod
    def setUpClass(cls):
        super(ManagementTest, cls).setUpClass()
        # Stand-alone router
        name = cls.__name__
        conf = Qdrouterd.Config([
            ('router', { 'mode': 'standalone', 'routerId': name}),
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
        self.maxDiff = None
        self.longMessage = True

    def test_bad_query(self):
        """Test that various badly formed queries get the proper response"""
        # No operation attribute
        self.assertRaises(BadRequestStatus, self.node.call, self.node.request())
        self.assertRaises(NotImplementedStatus, self.node.call,
                          self.node.request(operation="nosuch", type="org.amqp.management"))

    def test_query_type(self):
        """Query with type only"""
        response = self.node.query(type=LISTENER)
        for attr in ['type', 'name', 'identity', 'addr', 'port']:
            self.assertTrue(attr in response.attribute_names)
        for r in response.get_dicts():
            self.assertEqual(len(response.attribute_names), len(r))
            self.assertEqual(r['type'], LISTENER)
        self.assertTrue(
            set(['l0', 'l1', 'l2']) <= set(r['name'] for r in response.get_entities()))

    def test_query_type_attributes(self):
        """Query with type and attribute names"""
        attribute_names=['type', 'name', 'port']
        response = self.node.query(type=LISTENER, attribute_names=attribute_names)
        self.assertEqual(attribute_names, response.attribute_names)
        expect = [[LISTENER, 'l%s' % i, str(self.router.ports[i])] for i in xrange(3)]
        for r in expect: # We might have extras in results due to create tests
            self.assertTrue(r in response.results)
            self.assertTrue(dict(zip(attribute_names, r)) in response.get_dicts())

    def test_query_attributes(self):
        """Query with attributes only"""
        attribute_names=['type', 'name', 'port']
        response = self.node.query(attribute_names=attribute_names)
        self.assertEqual(attribute_names, response.attribute_names)
        expect = [[LISTENER, 'l%s' % i, str(self.router.ports[i])] for i in xrange(3)]
        for r in expect: # We might have extras in results due to create tests
            self.assertTrue(r in response.results)
        for name in ['router:' + self.router.name, 'log:0']:
            self.assertTrue([r for r in response.get_dicts() if r['name'] == name],
                            msg="Can't find result with name '%s'" % name)

    def assertMapSubset(self, small, big):
        """Assert that mapping small is a subset of mapping big"""
        missing = [(k, v) for k, v in small.items() if (k, v) not in big.items()]
        assert not missing, "Not a subset, missing %s, sub=%s, super=%s"%(missing, small, big)

    def assert_create_ok(self, type, name, attributes):
        entity = self.node.create(attributes, type, name)
        self.assertMapSubset(attributes, entity.attributes)
        return entity

    def test_create_listener(self):
        """Create a new listener on a running router"""

        port = self.get_port()
        # Note qdrouter schema defines port as string not int, since it can be a service name.
        attributes = {'name':'foo', 'port':str(port), 'role':'normal', 'saslMechanisms': 'ANONYMOUS'}
        entity = self.assert_create_ok(LISTENER, 'foo', attributes)
        self.assertEqual(entity['identity'], attributes['name'])
        self.assertEqual(entity['addr'], '0.0.0.0')

        # Connect via the new listener
        node3 = self.cleanup(Node(Url(port=port)))
        router = node3.query(type=ROUTER).get_entities()
        self.assertEqual(self.__class__.router.name, router[0]['routerId'])

    def test_create_log(self):
        """Create a log entity"""
        log = os.path.abspath("test_create_log.log")
        self.assert_create_ok('log', 'log.1', dict(module='AGENT', level="error", output=log))
        # Cause an error and verify it shows up in the log file.
        self.assertRaises(ManagementError, self.node.create, type='nosuch', name='nosuch')
        f = self.cleanup(open(log))
        logstr = f.read()
        self.assertTrue(re.search(r'ValidationError.*nosuch', logstr),
                        msg="Can't find expected ValidationError.*nosuch in '%r'" % logstr)

    def test_create_fixed_address(self):
        self.assert_create_ok(FIXED_ADDRESS, 'fixed1', dict(prefix='fixed1'))
        msgr = self.messenger()
        address = self.router.addresses[0]+'/fixed1'
        msgr.subscribe(address)
        msgr.put(message(address=address, body='hello'))
        self.assertEqual('hello', msgr.fetch().body)

    def test_create_connector_waypoint(self):
        """Test creating waypoint, connector and fixedAddress
        Create a waypoint that leads out and back from a second router.
        """
        conf = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'routerId': 'wp-router'}),
            ('listener', {'port':self.get_port(), 'role':'normal'}),
            ('fixedAddress', {'prefix':'foo'})
        ])
        wp_router = self.qdrouterd('wp-router', conf)
        wp_router.wait_ready()

        # Configure the router
        for c in [
                (FIXED_ADDRESS, 'a1', {'prefix':'foo', 'phase':0, 'fanout':'single', 'bias':'spread'}),
                (FIXED_ADDRESS, 'a2', {'prefix':'foo', 'phase':1, 'fanout':'single', 'bias':'spread'}),
                (CONNECTOR, 'wp_connector', {'port':str(wp_router.ports[0]), 'saslMechanisms': 'ANONYMOUS', 'role': 'on-demand'}),
                (WAYPOINT, 'wp', {'address': 'foo', 'inPhase': 0, 'outPhase': 1, 'connector': 'wp_connector'})
        ]:
            self.assert_create_ok(*c)
        assert retry(lambda: self.router.is_connected, wp_router.ports[0])

        # Send a message through self.router, verify it goes via wp_router
        address=self.router.addresses[0]+"/foo"
        mr = self.messenger()
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
        entity = self.node.read(type=LISTENER, name='l0')
        self.assertEqual('l0', entity.name)
        self.assertEqual('l0', entity.identity)
        self.assertEqual(str(self.router.ports[0]), entity.port)

        entity = self.node.read(type=LISTENER, identity='l1')
        self.assertEqual('l1', entity.name)
        self.assertEqual('l1', entity.identity)
        self.assertEqual(str(self.router.ports[1]), entity.port)

        # Bad type
        self.assertRaises(NotFoundStatus, self.node.read, type=CONNECTOR, name='l0')

        # Unknown entity
        self.assertRaises(NotFoundStatus, self.node.read, type=LISTENER, name='nosuch')

        # Update and delete are not allowed by the schema
        self.assertRaises(ForbiddenStatus, entity.update)
        self.assertRaises(ForbiddenStatus, entity.delete)

        # Non-standard request is not allowed by schema.
        self.assertRaises(ForbiddenStatus, entity.call, 'nosuchop', foo="bar")

        # Dummy entity supports all CRUD operations
        dummy = self.node.create({'arg1': 'START'}, type=DUMMY, name='MyDummy', )
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

        dummy2 = self.node.read(type=DUMMY, name='MyDummy')
        self.assertEqual(dummy.attributes, dummy2.attributes)

        self.assertEqual({'operation': 'callme', 'foo': 'bar', 'type': DUMMY, 'identity': identity},
                         dummy.call('callme', foo='bar'))

        dummy.badattribute = 'Bad'
        self.assertRaises(BadRequestStatus, dummy.update)

        dummy.delete()
        self.assertRaises(NotFoundStatus, self.node.read, type=DUMMY, name='MyDummy')

    def test_link(self):
        """Verify we can find our own reply-to address in links"""
        response = self.node.query(type=LINK)
        path = self.node.reply_to.split('/')[-1]
        mylink = [l for l in response.get_dicts()
                  if l['owningAddr'] and l['owningAddr'].endswith(path)]
        self.assertTrue(mylink)

    def test_connection(self):
        """Verify there is at least one connection"""
        response = self.node.query(type='connection')
        self.assertTrue(response.get_dicts())

    def test_router(self):
        """Verify router counts match entity counts"""
        entities = self.node.query().get_entities()
        routers = [e for e in entities if e.type == ROUTER]
        self.assertEqual(1, len(routers))
        router = routers[0]
        self.assertEqual(router.linkCount, len([e for e in entities if e.type == LINK]))
        self.assertEqual(router.addrCount, len([e for e in entities if e.type == ADDRESS]))

    def test_router_node(self):
        """Test node entity in a pair of linked routers"""
        # Pair of linked interior routers
        conf1 = Qdrouterd.Config([
            ('router', { 'mode': 'interior', 'routerId': 'router1'}),
            ('listener', {'port':self.get_port(), 'role':'normal'}),
            ('listener', {'port':self.get_port(), 'role':'inter-router'})
        ])
        conf2 = Qdrouterd.Config([
            ('router', { 'mode': 'interior', 'routerId': 'router2'}),
            ('listener', {'port':self.get_port(), 'role':'normal'}),
            ('connector', {'port':conf1.sections('listener')[1]['port'], 'role':'inter-router'})
        ])
        routers = [self.qdrouterd('router1', conf1, wait=False),
                   self.qdrouterd('router2', conf2, wait=False)]
        for r in routers: r.wait_ready()
        routers[0].wait_connected('router2')
        routers[1].wait_connected('router1')

        nodes = [self.cleanup(Node(Url(r.addresses[0]))) for r in routers]

        class RNodes(list):
            def __call__(self):
                self[:] = sum([n.query(type=NODE).get_entities() for n in nodes], [])
                return self
        rnodes = RNodes()

        assert retry(lambda: len(rnodes()) >= 2)
        self.assertEqual(['Rrouter2', 'Rrouter1'], [r.addr for r in rnodes])
        # FIXME aconway 2014-10-15: verify nextHop and validOrigins updated correctly
        self.assertEqual([u'amqp:/_topo/0/router2/$management', u'amqp:/_topo/0/router1/$management'],
                         sum([n.get_mgmt_nodes() for n in nodes], []))

    def test_get_types(self):
        types = self.node.get_types()
        self.assertIn('org.apache.qpid.dispatch.listener', types)
        self.assertIn('org.apache.qpid.dispatch.waypoint', types)
        self.assertIn('org.apache.qpid.dispatch.router.link', types)

    def test_get_operations(self):
        result = self.node.get_operations(type=DUMMY)
        self.assertEqual({DUMMY: ["CREATE", "READ", "UPDATE", "DELETE", "CALLME"]}, result)
        result = self.node.get_operations()
        for type in LISTENER, WAYPOINT, LINK: self.assertIn(type, result)
        self.assertEqual(["READ"], result[LINK])
        self.assertEqual(["CREATE", "READ"], result[WAYPOINT])

    def test_get_attributes(self):
        result = self.node.get_attributes(type=DUMMY)
        self.assertEqual(set([u'arg1', u'arg2', u'num1', u'num2', u'name', u'identity', u'type']),
                         set(result[DUMMY]))
        result = self.node.get_attributes()
        for type in LISTENER, WAYPOINT, LINK: self.assertIn(type, result)
        for a in ['linkType', 'linkDir', 'owningAddr']: self.assertIn(a, result[LINK])

if __name__ == '__main__':
    unittest.main(system_test.main_module())

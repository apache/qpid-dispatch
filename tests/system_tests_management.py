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

import unittest, system_test, re, os, json, sys
from qpid_dispatch.management.client import Node, ManagementError, Url, BadRequestStatus, NotImplementedStatus, NotFoundStatus, ForbiddenStatus
from qpid_dispatch_internal.management.qdrouter import QdSchema
from qpid_dispatch_internal.compat import OrderedDict, dictify
from system_test import Qdrouterd, message, retry, retry_exception, wait_ports, Process
from proton import ConnectionException
from itertools import chain

PREFIX = u'org.apache.qpid.dispatch.'
MANAGEMENT = PREFIX + 'management'
CONFIGURATION = PREFIX + 'configurationEntity'
OPERATIONAL = PREFIX + 'operationalEntity'
LISTENER = PREFIX + 'listener'
CONNECTOR = PREFIX + 'connector'
FIXED_ADDRESS = PREFIX + 'fixedAddress'
WAYPOINT = PREFIX + 'waypoint'
DUMMY = PREFIX + 'dummy'
ROUTER = PREFIX + 'router'
LINK = ROUTER + '.link'
ADDRESS = ROUTER + '.address'
NODE = ROUTER + '.node'

def short_name(name):
    if name.startswith(PREFIX):
        return name[len(PREFIX):]
    return name


class ManagementTest(system_test.TestCase):

    @classmethod
    def setUpClass(cls):
        super(ManagementTest, cls).setUpClass()
        # Stand-alone router
        conf0=Qdrouterd.Config([
            ('router', { 'mode': 'standalone', 'routerId': 'solo'}),
            ('listener', {'name': 'l0', 'port':cls.get_port(), 'role':'normal'}),
            # Extra listeners to exercise managment query
            ('listener', {'name': 'l1', 'port':cls.get_port(), 'role':'normal'}),
            ('listener', {'name': 'l2', 'port':cls.get_port(), 'role':'normal'})
        ])
        cls._router = cls.tester.qdrouterd(config=conf0, wait=False)

        # Trio of interior routers linked in a line so we can see some next-hop values.
        conf0 = Qdrouterd.Config([
            ('router', { 'mode': 'interior', 'routerId': 'router0'}),
            ('listener', {'port':cls.get_port(), 'role':'normal'}),
            ('listener', {'port':cls.get_port(), 'role':'inter-router'})
        ])
        conf1 = Qdrouterd.Config([
            ('router', { 'mode': 'interior', 'routerId': 'router1'}),
            ('listener', {'port':cls.get_port(), 'role':'normal'}),
            ('connector', {'port':conf0.sections('listener')[1]['port'], 'role':'inter-router'}),
            ('listener', {'port':cls.get_port(), 'role':'inter-router'})
        ])
        conf2 = Qdrouterd.Config([
            ('router', { 'mode': 'interior', 'routerId': 'router2'}),
            ('listener', {'port':cls.get_port(), 'role':'normal'}),
            ('connector', {'port':conf1.sections('listener')[1]['port'], 'role':'inter-router'})
        ])
        cls._routers = [cls.tester.qdrouterd(config=c, wait=False) for c in [conf0, conf1, conf2]]

        # Stand-alone router for logging tests (avoid interfering with logging for other tests.)
        conflog=Qdrouterd.Config([
            ('router', { 'mode': 'standalone', 'routerId': 'logrouter'}),
            ('listener', {'port':cls.get_port(), 'role':'normal'}),
        ])
        cls._logrouter = cls.tester.qdrouterd(config=conflog, wait=False)


    @property
    def router(self): return self.__class__._router.wait_ready()

    @property
    def logrouter(self): return self.__class__._logrouter.wait_ready()

    @property
    def routers(self):
        """Wait on demand and return the linked interior routers"""
        if not self._routers:
            self._routers = self.__class__._routers
            self._routers[0].wait_router_connected('router1')
            self._routers[1].wait_router_connected('router2')
            self._routers[2].wait_router_connected('router0')
        return self._routers

    def setUp(self):
        super(ManagementTest, self).setUp()
        self._routers = None # Wait on demand
        self.maxDiff = None
        self.longMessage = True
        self.node = self.cleanup(Node.connect(self.router.addresses[0]))

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
        for name in ['router/' + self.router.name, 'log/DEFAULT']:
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
        self.assertEqual(entity['name'], 'foo')
        self.assertEqual(entity['addr'], '0.0.0.0')

        # Connect via the new listener
        node3 = self.cleanup(Node.connect(Url(port=port)))
        router = node3.query(type=ROUTER).get_entities()
        self.assertEqual(self.router.name, router[0]['routerId'])

    def test_log(self):
        """Create, update and query log entities"""

        node = self.cleanup(Node.connect(self.logrouter.addresses[0]))
        default = node.read(identity='log/DEFAULT')
        self.assertEqual(default.attributes,
                         {u'identity': u'log/DEFAULT',
                          u'enable': u'trace+',
                          u'module': u'DEFAULT',
                          u'name': u'log/DEFAULT',
                          u'output': u'logrouter.log',
                          u'source': True,
                          u'timestamp': True,
                          u'type': u'org.apache.qpid.dispatch.log'})


        def check_log(log, error=True, debug=False):
            """Cause an error and check for expected error and debug logs"""
            bad_type = "nosuch"
            self.assertRaises(ManagementError, node.create, type=bad_type, name=bad_type)
            f = self.cleanup(open(log))
            logstr = f.read()
            def assert_expected(expect, regex, logstr):
                match = re.search(regex, logstr)
                assert bool(expect) == bool(match), "%s %s:\n%s" % (
                    ((match and "Found") or "Not found"), regex, logstr)
            assert_expected(error, r'AGENT \(error\).*%s' % bad_type, logstr)
            assert_expected(debug, r'AGENT \(debug\)', logstr)

        log_count = [0]         # In list to work-around daft python scoping rules.

        def update_check_log(attributes, error=True, debug=False):
            log_count[0] += 1
            log = os.path.abspath("test_log.log%s" % log_count[0])
            attributes["output"] = log
            attributes["identity"] = "log/AGENT"
            node.update(attributes)
            check_log(log, error, debug)

        # Expect error but no debug
        update_check_log(dict(enable="warning+"))
        update_check_log(dict(enable="error"))
        update_check_log(dict(enable="TRACE , Error info")) # Case and space insensitive

        # Expect no error if not enabled.
        update_check_log(dict(enable="info,critical"), error=False)
        update_check_log(dict(enable="none"), error=False)
        update_check_log(dict(enable=""), error=False)

        # Expect debug
        update_check_log(dict(enable="Debug"), error=False, debug=True)
        update_check_log(dict(enable="trace+"), debug=True)

        # Check defaults are picked up
        update_check_log(dict(enable="default"), error=True, debug=True)
        node.update(dict(identity="log/DEFAULT", enable="debug"))
        update_check_log(dict(enable="DEFAULT"), error=False, debug=True)
        node.update(dict(identity="log/DEFAULT", enable="error"))
        update_check_log(dict(enable="default"), error=True, debug=False)

        # Invalid values
        self.assertRaises(ManagementError, node.update, dict(identity="log/AGENT", enable="foo"))

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

        # Verify the entities
        id = 'connector/0.0.0.0:%s' % wp_router.ports[0]
        connector = self.node.read(identity=id)
        self.assertEqual(
            [connector.name, connector.addr, connector.port, connector.role],
            ['wp_connector', '0.0.0.0', str(wp_router.ports[0]), 'on-demand'])

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

    def test_dummy(self):
        """Test all operations on the dummy test entity"""
        entity = self.node.read(type=LISTENER, name='l0')
        self.assertEqual('l0', entity.name)
        self.assertEqual(str(self.router.ports[0]), entity.port)

        entity = self.node.read(
            type=LISTENER, identity='listener/0.0.0.0:%s' % self.router.ports[1])
        self.assertEqual('l1', entity.name)
        self.assertEqual(str(self.router.ports[1]), entity.port)

        # Bad type
        self.assertRaises(BadRequestStatus, self.node.read, type=CONNECTOR, name='l0')

        # Unknown entity
        self.assertRaises(NotFoundStatus, self.node.read, type=LISTENER, name='nosuch')

        # Update and delete are not allowed by the schema
        self.assertRaises(NotImplementedStatus, entity.update)
        self.assertRaises(NotImplementedStatus, entity.delete)

        # Non-standard request is not allowed by schema.
        self.assertRaises(NotImplementedStatus, entity.call, 'nosuchop', foo="bar")

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

        integers = [0, 1, 42, (2**63)-1, -1, -42, -(2**63)]
        test_data = ["bytes", u"string"] + integers
        for data in test_data:
            try:
                self.assertEqual(
                    {u'operation': u'callme', u'type': DUMMY, u'identity': identity, u'data': data},
                    dummy.call('callme', data=data))
            except TypeError, e:
                extype, value, trace = sys.exc_info()
                raise extype, "data=%r: %s" % (data, value), trace

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
        self.assertTrue(response.results)

    def test_router(self):
        """Verify router counts match entity counts"""
        entities = self.node.query().get_entities()
        routers = [e for e in entities if e.type == ROUTER]
        self.assertEqual(1, len(routers))
        router = routers[0]
        self.assertEqual(router.linkCount, len([e for e in entities if e.type == LINK]))
        self.assertEqual(router.addrCount, len([e for e in entities if e.type == ADDRESS]))

    def test_router_node(self):
        """Test node entity in a trio of linked routers"""
        nodes = [self.cleanup(Node.connect(Url(r.addresses[0]))) for r in self.routers]
        rnode_lists = [n.query(type=NODE).get_dicts() for n in nodes]

        def check(attrs):
            name = attrs['routerId']
            self.assertEqual(attrs['identity'], 'router.node/%s' % name)
            self.assertEqual(attrs['name'], 'router.node/%s' % name)
            self.assertEqual(attrs['type'], 'org.apache.qpid.dispatch.router.node')
            self.assertEqual(attrs['address'], 'amqp:/_topo/0/%s' % name)
            return name

        self.assertEqual(set(["router1", "router2"]), set([check(n) for n in rnode_lists[0]]))
        self.assertEqual(set(["router0", "router2"]), set([check(n) for n in rnode_lists[1]]))
        self.assertEqual(set(["router1", "router0"]), set([check(n) for n in rnode_lists[2]]))

    def test_entity_names(self):
        nodes = [self.cleanup(Node.connect(Url(r.addresses[0]))) for r in self.routers]
        # Test that all entities have a consitent identity format: type/name
        entities = list(chain(
            *[n.query(attribute_names=['type', 'identity', 'name']).iter_entities() for n in nodes]))
        for e in entities:
            if e.type == MANAGEMENT:
                self.assertEqual(e.identity, "self")
            else:
                self.assertRegexpMatches(e.identity, "^%s/" % short_name(e.type), e)

    def test_remote_node(self):
        """Test that we can access management info of remote nodes using get_mgmt_nodes addresses"""
        nodes = [self.cleanup(Node.connect(Url(r.addresses[0]))) for r in self.routers]
        remotes = sum([n.get_mgmt_nodes() for n in nodes], [])
        self.assertEqual([u'amqp:/_topo/0/router2/$management', u'amqp:/_topo/0/router1/$management'], remotes)
        # Query router2 indirectly via router1
        remote_url = Url(self.routers[0].addresses[0], path=Url(remotes[0]).path)
        remote = self.cleanup(Node.connect(remote_url))
        self.assertEqual(["router2"], [r.routerId for r in remote.query(type=ROUTER).get_entities()])

    def test_remote_node(self):
        """Test that we can access management info of remote nodes using get_mgmt_nodes addresses"""
        nodes = [self.cleanup(Node.connect(Url(r.addresses[0]))) for r in self.routers]
        remotes = sum([n.get_mgmt_nodes() for n in nodes], [])
        self.assertEqual(set([u'amqp:/_topo/0/router%s/$management' % i for i in [0, 1, 2]]),
                         set(remotes))
        self.assertEqual(6, len(remotes))
        # Query router2 indirectly via router1
        remote_url = Url(self.routers[0].addresses[0], path=Url(remotes[0]).path)
        remote = self.cleanup(Node.connect(remote_url))
        router_id = remotes[0].split("/")[3]
        assert router_id in ['router1', 'router2']
        self.assertEqual([router_id], [r.routerId for r in remote.query(type=ROUTER).get_entities()])

    def test_get_types(self):
        types = self.node.get_types()
        self.assertIn(CONFIGURATION, types[LISTENER])
        self.assertIn(WAYPOINT, types)
        self.assertIn(OPERATIONAL, types[LINK])

    def test_get_attributes(self):
        types = self.node.get_attributes()
        self.assertIn(SSL_PROFILE, types[CONNECTOR])
        self.assertEqual([], types[WAYPOINT])

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

    def test_standalone_no_inter_router(self):
        """Verify that we do not allow inter-router connectors or listeners in standalone mode"""

        attrs = dict(role="inter-router", saslMechanisms="ANONYMOUS")
        self.assertRaises(
            BadRequestStatus,
            self.node.create, dict(attrs, type=LISTENER, name="bad1", port=str(self.get_port())))

        self.assertRaises(
            BadRequestStatus,
            self.node.create, dict(attrs, type=CONNECTOR, name="bad2", port=str(self.get_port())))

        conf = Qdrouterd.Config([
            ('router', { 'mode': 'standalone', 'routerId': 'all_by_myself1'}),
            ('listener', {'port':self.get_port(), 'role':'inter-router'})
        ])
        r = self.qdrouterd('routerX', conf, wait=False)
        r.expect = Process.EXIT_FAIL
        self.assertTrue(r.wait() != 0)

        conf = Qdrouterd.Config([
            ('router', { 'mode': 'standalone', 'routerId': 'all_by_myself2'}),
            ('listener', {'port':self.get_port(), 'role':'normal'}),
            ('connector', {'port':self.get_port(), 'role':'inter-router'})
        ])
        r = self.qdrouterd('routerY', conf, wait=False)
        r.expect = Process.EXIT_FAIL
        self.assertTrue(r.wait() != 0)

    def test_get_schema(self):
        schema = dictify(QdSchema().dump())
        got = self.node.call(self.node.request(operation="GET-JSON-SCHEMA", identity="self")).body
        self.assertEquals(schema, dictify(json.loads(got)))
        got = self.node.call(self.node.request(operation="GET-SCHEMA", identity="self")).body
        self.assertEquals(schema, got)

if __name__ == '__main__':
    unittest.main(system_test.main_module())

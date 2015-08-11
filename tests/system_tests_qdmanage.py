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
# under the License
#

import re, json, unittest, os
from system_test import TestCase, Process, Qdrouterd, main_module, TIMEOUT, DIR
from subprocess import PIPE, STDOUT
from qpid_dispatch_internal.compat import OrderedDict, dictify
from qpid_dispatch_internal.management.qdrouter import QdSchema
from proton import Url

DUMMY = "org.apache.qpid.dispatch.dummy"

class QdmanageTest(TestCase):
    """Test qdmanage tool output"""

    @staticmethod
    def ssl_file(name):
        return os.path.join(DIR, 'ssl_certs', name)

    @classmethod
    def setUpClass(cls):
        super(QdmanageTest, cls).setUpClass()
        config = Qdrouterd.Config([
            ('ssl-profile', {'name': 'server-ssl',
                             'cert-db': cls.ssl_file('ca-certificate.pem'),
                             'cert-file': cls.ssl_file('server-certificate.pem'),
                             'key-file': cls.ssl_file('server-private-key.pem'),
                             'password': 'server-password'}),
            ('listener', {'port': cls.tester.get_port()}),
            ('listener', {'port': cls.tester.get_port(), 'ssl-profile': 'server-ssl'})
        ])
        cls.router = cls.tester.qdrouterd('test-router', config, wait=True)

    def address(self): return self.router.addresses[0]

    def run_qdmanage(self, cmd, input=None, expect=Process.EXIT_OK, address=None):
        p = self.popen(
            ['qdmanage'] + cmd.split(' ') + ['--bus', address or self.address(), '--indent=-1', '--timeout', str(TIMEOUT)],
            stdin=PIPE, stdout=PIPE, stderr=STDOUT, expect=expect)
        out = p.communicate(input)[0]
        try:
            p.teardown()
        except Exception, e:
            raise Exception("%s\n%s" % (e, out))
        return out

    def assert_entity_equal(self, expect, actual, copy=None):
        """Copy keys in copy from actual to idenity, then assert maps equal."""
        if copy:
            for k in copy: expect[k] = actual[k]
        self.assertEqual(expect, actual)

    def assert_entities_equal(self, expect, actual, copy=None):
        """Do assert_entities_equal on a list of maps."""
        for e, a in zip(expect, actual): self.assert_entity_equal(e, a, copy)

    def test_crud(self):

        def check(cmd, expect, copy=None, **kwargs):
            actual = json.loads(self.run_qdmanage(cmd))
            self.assert_entity_equal(expect, actual, copy=copy)

        expect = {'arg1': 'foo', 'type': DUMMY, 'name': 'mydummy2'}
        # create with type, name in attributes
        check('create arg1=foo type=dummy name=mydummy2', expect, copy=['identity'], attributes=json.dumps(expect))
        # create with type, name as arguments
        expect['name'] = 'mydummy'
        check('create name=mydummy type=dummy arg1=foo', expect, copy=['identity'])
        check('read --name mydummy', expect)
        check('read --identity %s' % expect['identity'], expect)
        expect.update([], arg1='bar', num1=555)
        check('update name=mydummy arg1=bar num1=555', expect)
        check('read --name=mydummy', expect)
        expect.update([], arg1='xxx', num1=888)
        # name outside attributes
        check('update name=mydummy arg1=xxx num1=888', expect)
        check('read --name=mydummy', expect)
        self.run_qdmanage('delete --name mydummy')
        self.run_qdmanage('read --name=mydummy', expect=Process.EXIT_FAIL)


    def test_stdin(self):
        """Test piping from stdin"""
        def check(cmd, expect, input, copy=None):
            actual = json.loads(self.run_qdmanage(cmd + " --stdin", input=input))
            self.assert_entity_equal(expect, actual, copy=copy)

        def check_list(cmd, expect_list, input, copy=None):
            actual = json.loads(self.run_qdmanage(cmd + " --stdin", input=input))
            self.assert_entities_equal(expect_list, actual, copy=copy)

        expect = {'type': DUMMY, 'name': 'mydummyx', 'arg1': 'foo'}
        check('create', expect, json.dumps(expect), copy=['identity'])

        expect_list = [{'type': DUMMY, 'name': 'mydummyx%s' % i} for i in xrange(3)]
        check_list('create', expect_list, json.dumps(expect_list), copy=['identity'])

        expect['arg1'] = 'bar'
        expect['num1'] = 42
        check('update', expect, json.dumps(expect))

        for i in xrange(3):
            expect_list[i]['arg1'] = 'bar'
            expect_list[i]['num1'] = i
        check_list('update', expect_list, json.dumps(expect_list))

    def test_query(self):
        def long_type(name): return u'org.apache.qpid.dispatch.'+name
        types = ['listener', 'log', 'container', 'router', 'router.link']
        long_types = [long_type(name) for name in types]

        qall = json.loads(self.run_qdmanage('query'))
        qall_types = set([e['type'] for e in qall])
        for t in long_types: self.assertIn(t, qall_types)

        qlistener = json.loads(self.run_qdmanage('query --type=listener'))
        self.assertEqual([long_type('listener')]*2, [e['type'] for e in qlistener])
        self.assertEqual(self.router.ports[0], int(qlistener[0]['port']))

        qattr = json.loads(
            self.run_qdmanage('query type name'))

        for e in qattr: self.assertEqual(2, len(e))

        def name_type(entities):
            ignore_types = [long_type(t) for t in ['router.link', 'connection', 'router.address']]
            return set((e['name'], e['type']) for e in entities
                       if e['type'] not in ignore_types)
        self.assertEqual(name_type(qall), name_type(qattr))

    def test_get_schema(self):
        schema = dictify(QdSchema().dump())
        actual = self.run_qdmanage("get-json-schema")
        self.assertEquals(schema, dictify(json.loads(actual)))
        actual = self.run_qdmanage("get-schema")
        self.assertEquals(schema, dictify(json.loads(actual)))

    def test_get_log(self):
        log = json.loads(self.run_qdmanage("get-log limit=1"))[0]
        self.assertEquals(['AGENT', 'trace'], log[0:2])
        self.assertRegexpMatches(log[2], 'get-log')

    def test_ssl(self):
        """Simple test for SSL connection. Note system_tests_qdstat has a more complete SSL test"""
        url = Url(self.router.addresses[1], scheme="amqps")
        schema = dictify(QdSchema().dump())
        actual = self.run_qdmanage("GET-JSON-SCHEMA")
        self.assertEquals(schema, dictify(json.loads(actual)))

if __name__ == '__main__':
    unittest.main(main_module())

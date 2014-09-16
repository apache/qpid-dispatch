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

import re, json, unittest
from system_test import TestCase, Process, Qdrouterd
from subprocess import PIPE, STDOUT
from copy import copy

DUMMY = "org.apache.qpid.dispatch.dummy"

class QdmanageTest(TestCase):
    """Test qdmanage tool output"""

    @classmethod
    def setUpClass(cls):
        super(QdmanageTest, cls).setUpClass()
        config = Qdrouterd.Config([
            ('listener', {'port': cls.tester.get_port()})
        ])
        cls.router = cls.tester.qdrouterd('test-router', config)

    def run_qdmanage(self, cmd, input=None, expect=Process.EXIT_OK, **kwargs):
        args = filter(None, sum([["--%s" % k.replace('_','-'), v]
                                 for k, v in kwargs.iteritems()], []))
        p = self.popen(
            ['qdmanage', cmd, '--bus', self.router.hostports[0]+"/$management2",
             '--indent=-1']+args,
            stdin=PIPE, stdout=PIPE, stderr=STDOUT, expect=expect)
        out = p.communicate(input)[0]
        if expect == Process.EXIT_OK:
            assert p.returncode == 0, "%s exit %s, output:\n%s" % (p.args, p.returncode, out)
        else:
            assert p.returncode != 0, "%s expected to fail but exit 0" %(p.args)
        return out

    def test_help(self):
        self.run_qdmanage('help', r'Usage: qdmanage', expect=Process.EXIT_FAIL)
        for cmd in ['create', 'read', 'update', 'delete', 'query']:
            out = self.run_qdmanage(cmd, help=None)
            assert re.search('Usage: %s \[options\]' % cmd, out, re.I), \
                "Can't find '%s' in '%s'" % (regexp, out)

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
            actual = json.loads(self.run_qdmanage(cmd, **kwargs))
            self.assert_entity_equal(expect, actual, copy=copy)

        expect = {'arg1': 'foo', 'type': DUMMY, 'name': 'mydummy2' }
        # create with type, name in attributes
        check('create', expect, copy=['identity'], attributes=json.dumps(expect))
        # create with type, name as arguments
        expect['name'] = 'mydummy'
        check('create', expect, copy=['identity'],
              type='dummy', name='mydummy', attributes='{"arg1" : "foo"}')

        check('read', expect, name="mydummy")
        check('read', expect, identity=expect['identity'])
        expect.update([], arg1='bar', num1=555)
        check('update', expect, attributes='{"name":"mydummy", "arg1" : "bar", "num1":555}')
        check('read', expect, name="mydummy")
        expect.update([], arg1='xxx', num1=888)
        # name outside attributes
        check('update', expect, name='mydummy', attributes='{"arg1": "xxx", "num1": 888}')
        check('read', expect, name="mydummy")
        self.run_qdmanage('delete', name="mydummy")
        self.run_qdmanage('read', name="mydummy", expect=Process.EXIT_FAIL)


    def test_stdin(self):
        def check(cmd, expect, input, copy=None):
            actual = json.loads(self.run_qdmanage(cmd, input=input, stdin=None))
            self.assert_entity_equal(expect, actual, copy=copy)

        def check_list(cmd, expect_list, input, copy=None):
            actual = json.loads(self.run_qdmanage(cmd, input=input, stdin=None))
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
        TYPES=['listener', 'log', 'container', 'router']
        LONG_TYPES=[long_type(name) for name in TYPES]

        qall = json.loads(self.run_qdmanage('query'))
        self.assertTrue(set(LONG_TYPES) <= set([e['type'] for e in qall]))

        qlistener = json.loads(self.run_qdmanage('query', type='listener'))
        self.assertEqual([long_type('listener')], [e['type'] for e in qlistener])
        self.assertEqual(self.router.ports[0], int(qlistener[0]['port']))

        qattr = json.loads(
            self.run_qdmanage('query', attribute_names='["type", "name"]'))
        for e in qattr: self.assertEqual(2, len(e))
        self.assertEqual(
            set([(e['name'], e['type']) for e in qall]),
            set([(e['name'], e['type']) for e in qattr]))


if __name__ == '__main__':
    unittest.main()

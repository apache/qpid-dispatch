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

#pylint: disable=wildcard-import,missing-docstring,too-many-public-methods

import unittest
from qpid_dispatch_internal.management.schema import Schema
from qpid_dispatch_internal.management.entity import Entity, EntityList
from schema import SCHEMA_1

class EntityTest(unittest.TestCase):

    def test_entity(self):
        s = Schema(**SCHEMA_1)
        e = Entity('container', name='x', schema=s)
        self.assertEqual(e.name, 'x')
        self.assertEqual(e.attributes, {'name':'x'})
        e.validate()
        attrs = {'name':'x', 'worker-threads':1}
        self.assertEqual(e.attributes, attrs)
        self.assertEqual(e.dump(as_map=True), {'entity_type':'container', 'attributes':attrs})
        self.assertEqual(e.dump(), ('container', attrs))


    def test_entity_list(self):
        s = Schema(**SCHEMA_1)
        contents = [('container', {'name':'x'}),
             ('listener', {'name':'y', 'addr':'1'}),
             ('listener', {'name':'z', 'addr':'2'}),
             ('listener', {'name':'q', 'addr':'2'}),
             ('connector', {'name':'c1', 'addr':'1'})]
        l = EntityList(s, contents)

        self.assertEqual(l.dump(), contents)
        self.assertEqual([e.name for e in l.get(entity_type='listener')], ['y', 'z', 'q'])
        self.assertEqual([e.name for e in l.get(entity_type='listener', addr='1')], ['y'])
        self.assertEqual([e.name for e in l.get(addr='1')], ['y', 'c1'])
        self.assertEqual(l.get(name='x', single=True).name, 'x')

        self.assertRaises(ValueError, l.get, entity_type='listener', single=True)
        self.assertRaises(ValueError, l.get, name='nosuch', single=True)

if __name__ == '__main__':
    unittest.main()

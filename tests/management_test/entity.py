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

import unittest
from qpid_dispatch.management.entity import EntityBase, camelcase


class EntityTest(unittest.TestCase):

    def test_camelcase(self):
        self.assertEqual('', camelcase(''))
        self.assertEqual('foo', camelcase('foo'))
        self.assertEqual('Foo', camelcase('foo', capital=True))
        self.assertEqual('fooBar', camelcase('foo bar'))
        self.assertEqual('fooBar', camelcase('foo.bar'))
        self.assertEqual('fooBar', camelcase('foo-bar'))
        self.assertEqual('fooBar', camelcase('foo_bar'))
        self.assertEqual('fooBarBaz', camelcase('foo_bar.baz'))
        self.assertEqual('FooBarBaz', camelcase('foo_bar.baz', capital=True))
        self.assertEqual('fooBar', camelcase('fooBar'))
        self.assertEqual('FooBar', camelcase('fooBar', capital=True))

    def test_entity(self):
        e = EntityBase({'fooBar': 'baz'}, type='container', name='x')
        self.assertEqual(e.attributes, {'type': 'container', 'name': 'x', 'fooBar': 'baz'})
        self.assertEqual(e.name, 'x')
        self.assertEqual(e['name'], 'x')

        e.name = 'y'
        self.assertEqual(e.name, 'y')
        self.assertEqual(e['name'], 'y')
        self.assertEqual(e.attributes['name'], 'y')

        e.xx = 'xx'
        self.assertEqual(e.xx, 'xx')
        self.assertEqual(e['xx'], 'xx')
        self.assertEqual(e.attributes['xx'], 'xx')


if __name__ == '__main__':
    unittest.main()

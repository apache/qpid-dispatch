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
from qpid_dispatch_internal.management import Entity

class EntityTest(unittest.TestCase):

    def test_entity(self):
        e = Entity({'foo-bar': 'baz'}, type='container', name='x')
        self.assertEqual(e.name, 'x')
        self.assertEqual(e.attributes, {'type': 'container', 'name':'x', 'foo-bar': 'baz'})
        self.assertEqual(e.foo_bar, 'baz')
        self.assertEqual(e.attributes['foo-bar'], 'baz')

        e.foo_bar = 'x'
        self.assertEqual(e.foo_bar, 'x')
        self.assertEqual(e.attributes['foo-bar'], 'x')

        e.xx = 'xx'
        self.assertEqual(e.xx, 'xx')
        self.assertEqual(e.attributes['xx'], 'xx')

        e.attributes['y-y'] = 'yy'
        self.assertEqual(e.y_y, 'yy')
        self.assertEqual(e.attributes['y-y'], 'yy')
        def assign(): e.attributes['foo/bar'] = 1
        self.assertRaises(KeyError, assign)

if __name__ == '__main__':
    unittest.main()

#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# 'License'); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# 'AS IS' BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#


#pylint: disable=wildcard-import,missing-docstring,too-many-public-methods

import unittest, json
from qpid_dispatch_internal.management.schema import Schema, BooleanType, EnumType, AttributeType, ValidationError, EnumValue, EntityType
import collections

def replace_od(thing):
    if isinstance(thing, collections.Mapping):
        return dict((k, replace_od(v)) for k,v in thing.iteritems())
    if isinstance(thing, list):
        return [replace_od(t) for t in thing]
    return thing

SCHEMA_1 = {
    "prefix":"org.example",
    "annotations": {
        "entityId": {
            "attributes": {
                "name": {"type":"String", "required": True, "unique":True},
                "type": {"type":"String", "required": True}
            }
        }
    },
    "entityTypes": {
        "container": {
            "singleton": True,
            "annotations" : ["entityId"],
            "attributes": {
                "workerThreads" : {"type":"Integer", "default": 1}
            }
        },
        "listener": {
            "annotations" : ["entityId"],
            "attributes": {
                "addr" : {"type":"String"}
            }
        },
        "connector": {
            "annotations" : ["entityId"],
            "attributes": {
                "addr" : {"type":"String"}
            }
        }
    }
}

class SchemaTest(unittest.TestCase):

    def test_bool(self):
        b = BooleanType()
        self.assertTrue(b.validate('on'))
        self.assertTrue(b.validate(True))
        self.assertFalse(b.validate(False))
        self.assertFalse(b.validate('no'))
        self.assertRaises(ValidationError, b.validate, 'x')

    def test_enum(self):
        e = EnumType(['a', 'b', 'c'])
        self.assertEqual(e.validate('a'), 'a')
        self.assertEqual(e.validate(1), 'b')
        self.assertEqual(e.validate('c'), 2)
        self.assertEqual(e.validate(2), 2)
        self.assertRaises(ValidationError, e.validate, 'foo')
        self.assertRaises(ValidationError, e.validate, 3)

        self.assertEqual('["x"]', json.dumps([EnumValue('x',3)]))

    def test_attribute_def(self):
        a = AttributeType('foo', 'String', default='FOO')
        self.assertEqual('FOO', a.missing_value())
        self.assertEqual(a.validate('x'), 'x')

        a = AttributeType('foo', 'String', default='FOO', required=True)
        self.assertEqual('FOO', a.missing_value())

        a = AttributeType('foo', 'String', required=True)
        self.assertRaises(ValidationError, a.missing_value) # Missing required value.

        a = AttributeType('foo', 'String', value='FOO') # Fixed value
        self.assertEqual('FOO', a.missing_value())
        self.assertEqual(a.validate('FOO'), 'FOO')
        self.assertRaises(ValidationError, a.validate, 'XXX') # Bad fixed value

        self.assertRaises(ValidationError, AttributeType, 'foo', 'String', value='FOO', default='BAR') # Illegal

        a = AttributeType('foo', 'Integer')
        self.assertEqual(3, a.validate(3))
        self.assertEqual(3, a.validate('3'))
        self.assertEqual(3, a.validate(3.0))
        self.assertRaises(ValidationError, a.validate, None)
        self.assertRaises(ValidationError, a.validate, "xxx")


    def test_entity_type(self):
        s = Schema(annotations={
            'i1':{'attributes': { 'foo1': {'type':'String', 'default':'FOO1'}}},
            'i2':{'attributes': { 'foo2': {'type':'String', 'default':'FOO2'}}}})

        e = EntityType('MyEntity', s, attributes={
            'foo': {'type':'String', 'default':'FOO'},
            'req': {'type':'Integer', 'required':True},
            'e': {'type':['x', 'y']}})
        self.assertRaises(ValidationError, e.validate, {}) # Missing required 'req'
        self.assertEqual(e.validate({'req':42}), {'foo': 'FOO', 'req': 42})
        # Try with an annotation
        e = EntityType('e2', s, attributes={'x':{'type':'Integer'}}, annotations=['i1', 'i2'])
        self.assertEqual(e.validate({'x':1}), {'x':1, 'foo1': 'FOO1', 'foo2': 'FOO2'})

    def test_entity_refs(self):
        e = EntityType('MyEntity', Schema(), attributes={
            'type': {'type': 'String', 'required': True, 'value': '$$entityType'},
            'name': {'type':'String', 'default':'$identity'},
            'identity': {'type':'String', 'default':'$name', "required": True}})

        self.assertEqual({'type': 'MyEntity', 'identity': 'x', 'name': 'x'},
                         e.validate({'identity':'x'}))
        self.assertEqual({'type': 'MyEntity', 'identity': 'x', 'name': 'x'},
                         e.validate({'name':'x'}))
        self.assertEqual({'type': 'MyEntity', 'identity': 'x', 'name': 'y'},
                         e.validate({'identity': 'x', 'name':'y'}))
        self.assertRaises(ValidationError, e.validate, {}) # Circular reference.

    def test_entity_annotation_refs(self):
        s = Schema(annotations={
            'i1': {'attributes': {
                'name': {'type':'String', 'default':'$identity'},
                'identity': {'type':'String', 'default':'$name', "required": True}}}})

        e = EntityType('MyEntity', s, attributes={}, annotations=['i1'])
        self.assertEqual({'identity': 'x', 'name': 'x'}, e.validate({'identity':'x'}))
        self.assertEqual({'identity': 'x', 'name': 'x'}, e.validate({'name':'x'}))
        self.assertEqual({'identity': 'x', 'name': 'y'}, e.validate({'identity': 'x', 'name':'y'}))
        self.assertRaises(ValidationError, e.validate, {})

    def test_schema_dump(self):
        s = Schema(**SCHEMA_1)
        self.maxDiff = None     # pylint: disable=invalid-name
        self.longMessage = True     # pylint: disable=invalid-name
        expect = {
            "prefix":"org.example",

            "annotations": {
                "entityId": {
                    "attributes": {
                        "name": {"required": True,
                                 "unique":True,
                                 "type": "String"}
                    }
                }
            },

            "entityTypes": {
                "container": {
                    "singleton": True,
                    "attributes": {
                        "name": {"type":"String", "unique":True, "required": True},
                        "workerThreads": {"type":"Integer", "default": 1}
                    }
                    },
                    "listener": {
                        "attributes": {
                            "name": {"type":"String", "unique":True, "required": True},
                            "addr" : {"type":"String"}
                        }
                    },
                "connector": {
                    "attributes": {
                        "name": {"type":"String", "unique":True, "required": True},
                        "addr" : {"type":"String"}
                    }
                }
            }
        }
        def jsontof(j,fname):
            with open(fname,'w') as f:
                json.dump(j, f, indent=4)
        self.assertDictEqual(expect, replace_od(s.dump()))

        s2 = Schema(**s.dump())
        self.assertEqual(s.dump(), s2.dump())

    def test_schema_validate(self):
        s = Schema(**SCHEMA_1)
        # Duplicate unique attribute 'name'
        m = [{'type': 'listener', 'name':'x'},
             {'type': 'listener', 'name':'x'}]
        self.assertRaises(ValidationError, s.validate_all, m)
        # Duplicate singleton entity 'container'
        m = [{'type': 'container', 'name':'x'},
             {'type': 'container', 'name':'y'}]
        self.assertRaises(ValidationError, s.validate_all, m)
        # Valid model
        m = [{'type': 'container', 'name':'x'},
             {'type': 'listener', 'name':'y'}]
        s.validate_all(m)

    def test_schema_entity(self):
        s = Schema(**SCHEMA_1)
        self.assertRaises(ValidationError, s.entity, {'type': 'nosuch'})
        self.assertRaises(ValidationError, s.entity, {'type': 'listener', 'nosuch': 'x'})
        e = s.entity({'type': 'listener', 'name':'x', 'addr':'foo'})
        self.assertEqual(e.attributes, {'type': 'org.example.listener', 'name':'x', 'addr':'foo'})
        self.assertEqual(e['addr'], 'foo')
        self.assertRaises(ValidationError, e.__setitem__, 'nosuch', 'x')
        try:
            e.nosuch = 'x'
            self.fail("Expected exception")
        except: pass

if __name__ == '__main__':
    unittest.main()

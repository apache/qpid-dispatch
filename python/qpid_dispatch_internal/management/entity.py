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

"""
Representation of AMQP management entities.

An entity has a set of named attributes and an L{EntityType} defined by a L{Schema}.
"""

from schema import EntityType
from copy import copy

class Entity(dict):
    """
    A management entity: a set of attributes with an associated entity-type.

    @ivar entity_type: The type of the entity.
    @type entitytype: L{EntityType}
    @ivar attributes: The attribute values.
    @type attribute: dict.
    @ivar I{attribute-name}: Access an entity attribute as a python attribute.
    """

    def __init__(self, entity_type, attributes=None, schema=None, **kw_attributes):
        """
        @param entity_type: An L{EntityType} or the name of an entity type in the schema.
        @param schema: The L{Schema} defining entity_type.
        @param attributes: An attribute mapping.
        @param kw_attributes: Attributes as keyword arguments.
        """
        super(Entity, self).__init__()
        if schema and entity_type in schema.entity_types:
            self.entity_type = schema.entity_types[entity_type]
        else:
            assert isinstance(entity_type, EntityType), "'%s' is not an entity type"%entity_type
            self.entity_type = entity_type
        self.attributes = attributes or {}
        self.attributes.update(kw_attributes)

    def validate(self, **kwargs):
        """
        Calls self.entity_type.validate(self). See L{Schema.validate}
        """
        self.entity_type.validate(self.attributes, **kwargs)

    def __getattr__(self, name):
        if not name in self.attributes:
            raise AttributeError("'%s' object has no attribute '%s'"%(self.__class__.__name__, name))
        return self.attributes[name]

    def dump(self, as_map=False):
        """
        Dump as a json-friendly tuple or map.
        @keyword as_map:
            If true dump as a map: { "entity_type":"<type>", "attributes":{"<name>":"<value>", ...}}
            Otherwise dump as a tuple: ("<type>", {"<name>":"<value", ...})
        """
        if as_map:
            return {'entity_type':self.entity_type.name, 'attributes':self.attributes}
        else:
            return (self.entity_type.name, self.attributes)

class EntityList(list):
    """
    A list of entities with some convenience methods for finding entities
    by type or attribute value.

    @ivar schema: The ${schema.Schema}
    @ivar <singleton-entity>: Python attribute shortcut to
        self.get(entity_type=<singleton-entity>, single=True)
    """

    def __init__(self, schema, contents=None):
        """
        @param schema: The L{Schema} for this entity list.
        @param contents: A list of L{Entity} or tuple (entity-type-name, { attributes...})
        """
        self.schema = schema
        super(EntityList, self).__init__()
        self.replace(contents or [])

    def entity(self, entity):
        """
        Make an L{Entity}. If entity is already an L{Entity} return it unchanged.
        Otherwise entity should be a tuple (entity-type-name, { attributes...})
        @param entity: An L{Entity} or a tuple
        """
        if isinstance(entity, Entity):
            return entity
        else:
            return Entity(entity[0], entity[1], self.schema)

    def validate(self):
        """
        Calls self.schema.validate(self). See L{Schema.validate}.
        """
        self.schema.validate(self)

    def get(self, single=False, entity_type=None, **kwargs):
        """
        Get a list of entities matching the criteria defined by keyword arguments.
        @keyword single: If True return a single value. Raise an exception if the result is not a single value.
        @keyword entity_type: An entity type name, return instances of that type.
        @param kwargs: Set of attribute-name:value keywords. Return instances of entities where all the attributes match.
        @return: a list of entities or a single entity if single=True.
        """
        result = self
        def match(e):
            """True if e matches the criteria"""
            if entity_type and e.entity_type.name != entity_type:
                return False
            for name, value in kwargs.iteritems():
                if name not in e.attributes or e.attributes[name] != value:
                    return False
            return True
        result = [e for e in self if match(e)]
        if single:
            if len(result) != 1:
                criteria = copy(kwargs)
                if entity_type: criteria['entity_type'] = entity_type
                raise ValueError("Expecting single value for %s, got %s"%(criteria, result))
            return result[0]
        return result

    def __getattr__(self, name):
        if name in self.schema.entity_types:
            return self.get(entity_type=name, single=self.schema.entity_types[name].singleton)
        raise AttributeError("'%s' object has no attribute '%s'"%(self.__class__.__name__, name))

    def dump(self, as_map=False):
        """
        Dump as a json-friendly list of entities. See L{Entity.dump}
        @keyword as_map: If true dump entities as maps, else as tuples.
        """
        return [e.dump(as_map) for e in self]

    def replace(self, contents):
        """
        Replace the contents of the list.
        @param contents: A list of L{Entity} or tuple (entity-type-name, { attributes...})
        """
        self[:] = [self.entity(c) for c in contents]

##*
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
Schema for AMQP management entity types.

Schema validation will validate and transform values, add default values and
check for uniqueness of enties/attributes that are specified to be unique.

A Schema can be loaded/dumped to a json file.
"""

import os

class SchemaError(Exception):
    """Class for schema errors"""
    pass

def schema_file(name):
    """Return a file name relative to the directory from which this module was loaded."""
    return os.path.join(os.path.dirname(__file__), name)

class Type(object):
    """Base class for schema types.

    @ivar name: The type name.
    @ivar pytype: The python type for this schema type.
    """
    def __init__(self, name, pytype):
        """
        @param name: The type name.
        @param pytype: The python type for this schema type.
        """
        self.name, self.pytype = name, pytype

    def validate(self, value, **kwargs): # pylint: disable=unused-argument
        """
        Convert value to the correct python type.

        @param kwargs: See L{Schema.validate}
        """
        return self.pytype(value)

    def dump(self):
        """
        @return: Representation of the type to dump to json. Normally the type name,
            EnumType.dump is the exception.
        """
        return self.name

    def __repr__(self):
        return str(self.dump())

class BooleanType(Type):
    """A boolean schema type"""

    def __init__(self):
        super(BooleanType, self).__init__("Boolean", bool)

    VALUES = {"yes":1, "true":1, "on":1, "no":0, "false":0, "off":0}

    def validate(self, value, **kwargs):
        """
        @param value: A string such as "yes", "false" etc. is converted appropriately.
            Any other type is converted using python's bool()
        @param kwargs: See L{Schema.validate}
        @return A python bool.
        """
        try:
            if isinstance(value, basestring):
                return self.VALUES[value.lower()]
            return bool(value)
        except:
            raise ValueError("Invalid Boolean value '%r'"%value)

class EnumType(Type):
    """An enumerated type"""

    def __init__(self, tags):
        """
        @param tags: A list of string values for the enumerated type.
        """
        assert isinstance(tags, list)
        super(EnumType, self).__init__("enum%s"%([str(t) for t in tags]), int)
        self.tags = tags

    def validate(self, value, enum_as_int=False, **kwargs):
        """
        @param value: May be a string from the set of enum tag strings or anything
            that can convert to an int - in which case it must be in the enum range.
        @keyword enum_as_int: If true the return value will be an int.
        @param kwargs: See L{Schema.validate}
        @return: If enum_as_int is True the int value of the enum, othewise the enum tag string.
        """
        if value in self.tags:
            if enum_as_int:
                return self.tags.index(value)
            else:
                return value
        else:
            try:
                i = int(value)
                if 0 <= i and i < len(self.tags):
                    if enum_as_int:
                        return i
                    else:
                        return self.tags[i]
            except (ValueError, IndexError):
                pass
        raise ValueError("Invalid value for %s: '%r'"%(self.name, value))

    def dump(self):
        """
        @return: A list of the enum tags.
        """
        return self.tags

BUILTIN_TYPES = dict((t.name, t) for t in [Type("String", str), Type("Integer", int), BooleanType()])

def get_type(rep):
    """
    Get a schema type.
    @param rep: json representation of the type.
    """
    if isinstance(rep, list):
        return EnumType(rep)
    if rep in BUILTIN_TYPES:
        return BUILTIN_TYPES[rep]
    raise SchemaError("No such schema type: %s"%rep)

def _dump_dict(items):
    """
    Remove all items with None value from a mapping.
    @return: Map of non-None items.
    """
    return dict((k, v) for k, v in items if v)

def _is_unique(found, category, value):
    """
    Check if value has already been found in category.

    @param found: Map of values found in each category { category:set(value,...), ...}
        Modified: value is added to the category.
        If found is None then _is_unique simply returns True.
    @param category: category to check for value.
    @param value: value to check.
    @return: True if value is unique so far (i.e. was not already found)
    """
    if found is None:
        return True
    if not category in found:
        found[category] = set()
    if value in found[category]:
        return False
    else:
        found[category].add(value)
        return True


class AttributeDef(object):
    """
    Definition of an attribute.

    @ivar name: Attribute name.
    @ivar atype: Attribute L{Type}
    @ivar required: True if the attribute is reqiured.
    @ivar default: Default value for the attribute or None if no default.
    @ivar unique: True if the attribute value is unique.
    """

    def __init__(self, name, type=None, default=None, required=False, unique=False): # pylint: disable=redefined-builtin
        """
        See L{AttributeDef} instance variables.
        """
        self.name = name
        self.atype = get_type(type)
        self.required = required
        self.default = default
        self.unique = unique
        if default is not None:
            self.default = self.atype.validate(default)

    def validate(self, value, check_required=True, add_default=True, check_unique=None, **kwargs):
        """
        Validate value for this attribute definition.
        @keyword check_required: Raise an exception if required attributes are misssing.
        @keyword add_default:  Add a default value for missing attributes.
        @keyword check_unique: A dict to collect values to check for uniqueness.
            None means don't check for uniqueness.
        @param kwargs: See L{Schema.validate}
        @return: value converted to the correct python type. Rais exception if any check fails.
        """
        if value is None and add_default:
            value = self.default
        if value is None:
            if self.required and check_required:
                raise SchemaError("Missing value for attribute '%s'"%self.name)
            else:
                return None
        else:
            if self.unique and not _is_unique(check_unique, self.name, value):
                raise SchemaError("Multiple instances of unique attribute '%s'"%self.name)
            return self.atype.validate(value, **kwargs)

    def dump(self):
        """
        @return: Json-friendly representation of an attribute type
        """
        return _dump_dict([
            ('type', self.atype.dump()), ('default', self.default), ('required', self.required)])

    def __str__(self):
        return "AttributeDef%s"%(self.__dict__)

class EntityType(object):
    """
    An entity type defines a set of attributes for an entity.

    @ivar name: Entity type name.
    @ivar attributes: Map of L{AttributeDef} for entity.
    @ivar singleton: If true only one entity of this type is allowed.
    """
    def __init__(self, name, schema, singleton=False, include=None, attributes=None):
        """
        @param name: name of the entity type.
        @param schema: schema for this type.
        @param singleton: True if entity type is a singleton.
        @param include: List of names of include types for this entity.
        @param attributes: Map of attributes {name: {type:, default:, required:, unique:}}
        """
        self.name = name
        self.schema = schema
        self.singleton = singleton
        self.attributes = {}
        if attributes:
            self.add_attributes(attributes)
        if include and self.schema.includes:
            for i in include:
                self.add_attributes(schema.includes[i])

    def add_attributes(self, attributes):
        """
        Add attributes.
        @param attributes: Map of attributes {name: {type:, default:, required:, unique:}}
        """
        for k, v in attributes.iteritems():
            if k in self.attributes:
                raise SchemaError("Attribute '%s' duplicated in '%s'"%(k, self.name))
            self.attributes[k] = AttributeDef(k, **v)

    def dump(self):
        """Json friendly representation"""
        return _dump_dict([
            ('singleton', self.singleton),
            ('attributes', dict((k, v.dump()) for k, v in self.attributes.iteritems()))])

    def validate(self, attributes, check_singleton=None, **kwargs):
        """
        Validate attributes.
        @param attributes: Map of attribute values {name:value}
            Modifies attributes: adds defaults, converts values.
        @param check_singleton: dict to enable singleton checking or None to disable.
        @param kwargs: See L{Schema.validate}
        """
        if self.singleton and not _is_unique(check_singleton, self.name, True):
            raise SchemaError("Found multiple instances of singleton entity type '%s'"%self.name)
        # Validate
        for name, value in attributes.iteritems():
            attributes[name] = self.attributes[name].validate(value, **kwargs)
        # Set defaults, check for missing required values
        for attr in self.attributes.itervalues():
            if attr.name not in attributes:
                value = attr.validate(None, **kwargs)
                if not value is None:
                    attributes[attr.name] = value
        # Drop null items
        for name in attributes.keys():
            if attributes[name] is None:
                del attributes[name]
        return attributes


class Schema(object):
    """
    Schema defining entity types.

    @ivar prefix: Prefix to prepend to short entity names.
    @ivar entity_types: Map of L{EntityType} by name.
    """
    def __init__(self, prefix="", includes=None, entity_types=None):
        """
        @param prefix: Prefix for entity names.
        @param includes: Map of  { include-name: {attribute-name:value, ... }}
        @param entity_types: Map of  { entity-type-name: { singleton:, include:[...], attributes:{...}}}
        """
        self.prefix = self.prefixdot = prefix
        if not prefix.endswith('.'):
            self.prefixdot += '.'
        self.includes = includes or {}
        self.entity_types = {}
        if entity_types:
            for k, v in entity_types.iteritems():
                self.add_entity_type(k, **v)

    def add_entity_type(self, name, singleton=False, include=None, attributes=None):
        """
        Add an entity type to the schema.
        @param name: Entity type name.
        @param singleton: True if this is a singleton.
        @param include: List of names of include sections for this entity.
        @param attributes: Map of attributes {name: {type:, default:, required:, unique:}}
        """
        self.entity_types[name] = EntityType(name, self, singleton, include, attributes)

    def short_name(self, name):
        """Remove prefix from name if present"""
        if name.startswith(self.prefixdot):
            return name[len(self.prefixdot):]
        else: return name

    def long_name(self, name):
        """Add prefix to name if absent"""
        if not name.startswith(self.prefixdot):
            return self.prefixdot + name
        else: return name

    def dump(self):
        """Return json-friendly representation"""
        return {'prefix':self.prefix,
                'includes':self.includes,
                'entity_types':dict((k, v.dump()) for k, v in self.entity_types.iteritems())}

    def validate(self, entities, enum_as_int=False, check_required=True, add_default=True, check_unique=True, check_singleton=True):
        """
        Validate entities, verify singleton entities and unique attributes are unique.

        @param entities: List of L{Entity}
        @keyword enum_as_int: Represent enums as int rather than string.
        @keyword check_required: Raise exception if required attributes are missing.
        @keyword add_default: Add defaults for missing attributes.
        @keyword check_unique: Raise exception if unique attributes are duplicated.
        @keyword check_singleton: Raise exception if singleton entities are duplicated.

        """
        if check_singleton: check_singleton = {}
        if check_unique: check_unique = {}
        for e in entities:
            assert e.entity_type.schema is self, "Entity '%s' from wrong schema"%e
            e.validate(
                enum_as_int=enum_as_int,
                check_required=check_required,
                add_default=add_default,
                check_unique=check_unique,
                check_singleton=check_singleton)
        return entities

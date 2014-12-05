#
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

import sys
from qpid_dispatch.management import entity
from qpid_dispatch.management.error import ForbiddenStatus
from ..compat import OrderedDict

class ValidationError(Exception):
    """Error raised if schema validation fails"""
    pass


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

        @param kwargs: See L{Schema.validate_all}
        """
        return self.pytype(value)

    def dump(self):
        """
        @return: Representation of the type to dump to json. Normally the type name,
            EnumType.dump is the exception.
        """
        return self.name

    def __str__(self):
        """String name of type."""
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
        @param kwargs: See L{Schema.validate_all}
        @return A python bool.
        """
        try:
            if isinstance(value, basestring):
                return self.VALUES[value.lower()]
            return bool(value)
        except:
            raise ValidationError("Invalid Boolean value '%r'"%value)


class EnumValue(str):
    """A string that convets to an integer value via int()"""

    def __new__(cls, name, value):
        s = super(EnumValue, cls).__new__(cls, name)
        setattr(s, 'value', value)
        return s

    def __int__(self): return self.value
    def __eq__(self, x): return str(self) == x or int(self) == x
    def __ne__(self, x): return not self == x
    def __repr__(self): return "EnumValue('%s', %s)"%(str(self), int(self))


class EnumType(Type):
    """An enumerated type"""

    def __init__(self, tags):
        """
        @param tags: A list of string values for the enumerated type.
        """
        assert isinstance(tags, list)
        super(EnumType, self).__init__("enum%s"%([str(t) for t in tags]), int)
        self.tags = tags

    def validate(self, value, **kwargs):
        """
        @param value: May be a string from the set of enum tag strings or anything
            that can convert to an int - in which case it must be in the enum range.
        @param kwargs: See L{Schema.validate_all}
        @return: An EnumValue.
        """
        if value in self.tags:
            return EnumValue(value, self.tags.index(value))
        else:
            try:
                i = int(value)
                return EnumValue(self.tags[i], i)
            except (ValueError, IndexError):
                pass
        raise ValidationError("Invalid value for %s: '%r'"%(self.name, value))

    def dump(self):
        """
        @return: A list of the enum tags.
        """
        return self.tags

    def __str__(self):
        """String description of enum type."""
        return "One of [%s]"%(', '.join(self.tags))

BUILTIN_TYPES = dict((t.name, t) for t in
                     [Type("String", str), Type("Integer", int), Type("List", list), BooleanType()])

def get_type(rep):
    """
    Get a schema type.
    @param rep: json representation of the type.
    """
    if isinstance(rep, list):
        return EnumType(rep)
    if rep in BUILTIN_TYPES:
        return BUILTIN_TYPES[rep]
    raise ValidationError("No such schema type: %s"%rep)

def _dump_dict(items):
    """
    Remove all items with None value from a mapping.
    @return: Map of non-None items.
    """
    return OrderedDict((k, v) for k, v in items if v)

def _is_unique(found, item):
    """
    Return true if found is None or item is not in found (adds item to found.)
    Return false if item is in found.
    """
    if found is None or found is False:
        return True
    if item not in found:
        found.add(item)
        return True
    return False

class AttributeType(object):
    """
    Definition of an attribute.

    @ivar name: Attribute name.
    @ivar atype: Attribute L{Type}
    @ivar required: True if the attribute is reqiured.
    @ivar default: Default value for the attribute or None if no default. Can be a reference.
    @ivar value: Fixed value for the attribute. Can be a reference.
    @ivar unique: True if the attribute value is unique.
    @ivar description: Description of the attribute type.
    @ivar annotation: Annotation section or None
    """

    def __init__(self, name, type=None, default=None, required=False, unique=False,
                 value=None, annotation=None, description=""):
        """
        See L{AttributeType} instance variables.
        """
        self.name = name
        self.atype = get_type(type)
        self.required = required
        self.default = default
        self.value = value
        self.unique = unique
        self.description = description
        self.annotation = annotation
        if self.value is not None and self.default is not None:
            raise ValidationError("Attribute '%s' has default value and fixed value"%self.name)

    def missing_value(self, check_required=True, add_default=True, **kwargs):
        """
        Fill in missing default and fixed values but don't resolve references.
        @keyword check_required: Raise an exception if required attributes are misssing.
        @keyword add_default:  Add a default value for missing attributes.
        @param kwargs: See L{Schema.validate_all}
        """
        if self.value is not None: # Fixed value attribute
            return self.value
        if add_default and self.default is not None:
            return self.default
        if check_required and self.required:
            raise ValidationError("Missing required attribute '%s'"%(self.name))


    def validate(self, value, resolve=lambda x: x, check_unique=None, **kwargs):
        """
        Validate value for this attribute definition.
        @param value: The value to validate.
        @param resolve: function to resolve value references.
        @keyword check_unique: set of (name, value) to check for attribute uniqueness.
            None means don't check for uniqueness.
        @param kwargs: See L{Schema.validate_all}
        @return: value converted to the correct python type. Rais exception if any check fails.
        """
        value = resolve(value)
        if self.unique and not _is_unique(check_unique, (self.name, value)):
            raise ValidationError("Duplicate value '%s' for unique attribute '%s'"%(value, self.name))
        if self.value and value != resolve(self.value):
            raise ValidationError("Attribute '%s' has fixed value '%s' but given '%s'"%(self.name, resolve(self.value), value))
        try:
            return self.atype.validate(value, **kwargs)
        except (TypeError, ValueError), e:
            raise ValidationError, str(e), sys.exc_info()[2]

    def dump(self):
        """
        @return: Json-friendly representation of an attribute type
        """
        return _dump_dict([
            ('type', self.atype.dump()),
            ('default', self.default),
            ('required', self.required),
            ('unique', self.unique),
            ('description', self.description)
        ])

    def __str__(self):
        return "%s(%s)"%(self.__class__.__name__, self.name)


class RedefinedError(ValueError): pass


class AttrsAndOps(object):
    """Base class for Annotation and EntityType - a named holder of attribute types and operations"""

    def __init__(self, name, schema, attributes=None, operations=None, description=""):
        self.name, self.schema, self.description = schema.long_name(name), schema, description
        self.short_name = schema.short_name(name)
        self.attributes = OrderedDict()
        if attributes: self.add_attributes(attributes)
        self.operations = operations or []

    def add_attributes(self, attributes):
        """
        Add attributes.
        @param attributes: Map of attributes {name: {type:, default:, required:, unique:}}
        """
        for k, v in attributes.iteritems():
            if k in self.attributes:
                raise ValidationError("Duplicate attribute in '%s': '%s'"%(self.name, k))
            self.attributes[k] = AttributeType(k, **v)

    def dump(self):
        """Json friendly representation"""
        return _dump_dict([
            ('attributes', OrderedDict(
                (k, v.dump()) for k, v in self.attributes.iteritems()
                if k != 'type')), # Don't dump 'type' attribute, dumped separately.
            ('operations', self.operations),
            ('description', self.description or None)
        ])

    def __repr__(self):
        return "%s(%s)"%(self.__class__.__name__, self.name)

    def __str__(self):
        return self.name


class Annotation(AttrsAndOps):
    """An annotation type defines a set of attributes that can be re-used by multiple EntityTypes"""
    def __init__(self, name, schema, attributes=None, operations=None, description=""):
        super(Annotation, self).__init__(name, schema, attributes, operations, description)
        attributes = attributes or {}
        for a in self.attributes.itervalues():
            a.annotation = self


class EntityType(AttrsAndOps):
    """
    An entity type defines a set of attributes for an entity.

    @ivar name: Fully qualified entity type name.
    @ivar short_name: Un-prefixed short name.
    @ivar attributes: Map of L{AttributeType} for entity.
    @ivar singleton: If true only one entity of this type is allowed.
    #ivar annotation: List of names of sections annotationd by this entity.
    """
    def __init__(self, name, schema, singleton=False, annotations=None, attributes=None,
                 extends=None, description="", operations=None):
        """
        @param name: name of the entity type.
        @param schema: schema for this type.
        @param singleton: True if entity type is a singleton.
        @param annotation: List of names of annotation types for this entity.
        @param attributes: Map of attributes {name: {type:, default:, required:, unique:}}
        @param description: Human readable description.
        @param operations: Allowed operations, list of operation names.
        """
        super(EntityType, self).__init__(name, schema, attributes, operations, description)
        self.base = None
        self.all_bases = []
        if extends:
            self.base = schema.entity_type(extends)
            self.all_bases = [self.base] + self.base.all_bases
            self._extend(self.base, 'extend')

        self.annotations = [ schema.annotation(a) for a in annotations or []]
        for a in self.annotations:
            self._extend(a, 'be annotated')

        # This map defines values that can be referred to using $$ in the schema.
        self.refs = {'entityType': self.name}
        self.singleton = singleton

    def _extend(self, other, how):
        """Add attributes and operations from other"""
        def check(a, b, what):
            overlap = set(a) & set(b)
            if overlap:
                raise RedefinedError("'%s' cannot %s '%s', re-defines %s: %s"
                                     % (name, how, other.short_name, what, list(overlap).join(', ')))
        check(self.operations, other.operations, "operations")
        self.operations = self.operations + other.operations
        check(self.attributes.iterkeys(), other.attributes.itervalues(), "attributes")
        self.attributes.update(other.attributes)

    def extends(self, base): return base in self.all_bases

    def is_a(self, type): return type == self or self.extends(type)

    def dump(self):
        """Json friendly representation"""
        d = super(EntityType, self).dump()
        if self.singleton: d['singleton'] = True
        return d

    def resolve(self, value, attributes):
        """
        Resolve a $ or $$ reference.
        $attr refers to another attribute.
        $$name refers to a value in EntityType.refs
        """
        values = [value]
        while True:
            if isinstance(value, basestring) and value.startswith('$$'):
                if value[2:] not in self.refs:
                    raise ValidationError("Invalid entity type reference '%s'"%value)
                value = self.refs[value[2:]]
            elif isinstance(value, basestring) and value.startswith('$'):
                if value[1:] not in self.attributes:
                    raise ValidationError("Invalid attribute reference '%s'"%value)
                value = attributes.get(value[1:])
            else:
                return value # Not a reference, don't need to resolve
            if value == values[0]: # Circular reference
                raise ValidationError("Unresolved circular reference '%s'"%values)
            values.append(value)


    def validate(self, attributes, check_singleton=None, **kwargs):
        """
        Validate attributes for entity type.
        @param attributes: Map attributes name:value or Entity with attributes property.
            Modifies attributes: adds defaults, converts values.
        @param check_singleton: set of entity-type name to enable singleton checking.
            None to disable.
        @param kwargs: See L{Schema.validate_all}
        """

        if isinstance(attributes, Entity): attributes = attributes.attributes

        if self.singleton and not _is_unique(check_singleton, self.name):
            raise ValidationError("Multiple instances of singleton '%s'"%self.name)
        try:
            # Add missing values
            for attr in self.attributes.itervalues():
                if attr.name not in attributes:
                    value = attr.missing_value(**kwargs)
                    if value is not None: attributes[attr.name] = value

            # Validate attributes.
            for name, value in attributes.iteritems():
                if name not in self.attributes:
                    raise ValidationError("%s has unknown attribute '%s'"%(self, name))
                if name == 'type':
                    value = self.schema.long_name(value)
                attributes[name] = self.attributes[name].validate(
                    value, lambda v: self.resolve(v, attributes), **kwargs)
        except ValidationError, e:
            raise  ValidationError, "%s: %s"%(self, e), sys.exc_info()[2]

        return attributes

    def allowed(self, op):
        """Raise excepiton if op is not a valid operation on entity."""
        op = op.upper()
        if op not in self.operations:
            raise ForbiddenStatus("Operation '%s' not allowed for '%s'" % (op, self.name))

    def __repr__(self): return "%s(%s)" % (type(self).__name__, self.name)

    def __str__(self): return self.name

    def name_is(self, name):
        return self.name == self.schema.long_name(name)

class Schema(object):
    """
    Schema defining entity types.
    Note: keyword arguments come from schema so use camelCase

    @ivar prefix: Prefix to prepend to short entity type names.
    @ivar entityTypes: Map of L{EntityType} by name.
    @ivar annotations: Map of L{Annotation} by name.
    @ivar description: Text description of schema.
    """
    def __init__(self, prefix="", annotations=None, entityTypes=None, description=""):
        """
        @param prefix: Prefix for entity names.
        @param annotations: Map of  { annotation-name: {attribute-name:value, ... }}
        @param entity_types: Map of  { entity-type-name: { singleton:, annotation:[...], attributes:{...}}}
        @param description: Human readable description.
        """
        if prefix:
            self.prefix = prefix.strip('.')
            self.prefixdot = self.prefix + '.'
        else:
            self.prefix = self.prefixdot = ""

        self.description = description
        self.annotations = OrderedDict()
        self.entity_types = OrderedDict()
        def add_defs(thing, mymap, defs):
            for k, v in defs.iteritems():
                t = thing(k, self, **v)
                mymap[t.name] = t
        add_defs(Annotation, self.annotations, annotations or {})
        add_defs(EntityType, self.entity_types, entityTypes or {})

    def short_name(self, name):
        """Remove prefix from name if present"""
        if name.startswith(self.prefixdot):
            name = name[len(self.prefixdot):]
        return name

    def long_name(self, name):
        """Add prefix to unqualified name"""
        if not name: return name
        if not name.startswith(self.prefixdot):
            name = self.prefixdot + name
        return name

    def dump(self):
        """Return json-friendly representation"""
        return OrderedDict([
            ('prefix', self.prefix),
            ('annotations',
             OrderedDict((a.short_name, a.dump()) for a in self.annotations.itervalues())),
            ('entityTypes',
             OrderedDict((e.short_name, e.dump()) for e in self.entity_types.itervalues()))
        ])

    def _lookup(self, map, name, message, error):
        try:
            return map[self.long_name(name)]
        except KeyError:
            if error: raise ValidationError(message % name)
            else: return None

    def entity_type(self, name, error=True):
        return self._lookup(self.entity_types, name, "No such entity type '%s'", error)

    def annotation(self, name, error=True):
        return self._lookup(self.annotations, name, "No such annotation '%s'", error)

    def validate_entity(self, attributes, check_required=True, add_default=True,
                        check_unique=None, check_singleton=None):
        """
        Validate a single entity.

        @param attributes: Map of attribute name: value
        @keyword check_required: Raise exception if required attributes are missing.
        @keyword add_default: Add defaults for missing attributes.
        @keyword check_unique: Used by L{validate_all}
        @keyword check_singleton: Used by L{validate_all}
        """
        attributes['type'] = self.long_name(attributes['type'])
        if not attributes['type'] in self.entity_types:
            raise ValidationError("No such entity type: '%s'" % attributes['type'])
        entity_type = self.entity_types[attributes['type']]
        entity_type.validate(
            attributes,
            check_required=check_required,
            add_default=add_default,
            check_unique=check_unique,
            check_singleton=check_singleton)

    def validate_all(self, attribute_maps, check_required=True, add_default=True,
                     check_unique=True, check_singleton=True):
        """
        Validate a list of attribute maps representing entity attributes.
        Verify singleton entities and unique attributes are unique.
        Modifies attribute_maps, adds default values, converts values.

        @param attribute_maps: List of attribute name:value maps.
        @keyword check_required: Raise exception if required attributes are missing.
        @keyword add_default: Add defaults for missing attributes.
        @keyword check_unique: Raise exception if unique attributes are duplicated.
        @keyword check_singleton: Raise exception if singleton entities are duplicated
        """
        if check_singleton: check_singleton = set()
        if check_unique: check_unique = set()

        for e in attribute_maps:
            self.validate_entity(e,
                                 check_required=check_required,
                                 add_default=add_default,
                                 check_unique=check_unique,
                                 check_singleton=check_singleton)

    def entity(self, attributes):
        """Convert an attribute map into an L{Entity}"""
        attributes = dict((k, v) for k, v in attributes.iteritems() if v is not None)
        return Entity(self.entity_type(attributes['type']), attributes)

    def entities(self, attribute_maps):
        """Convert a list of attribute maps into a list of L{Entity}"""
        return [self.entity(m) for m in attribute_maps]

    def by_type(self, type):
        """Return an iterator over entity types that extend or are type.
        If type is None return all entities."""
        return (t for t in self.entity_types.itervalues() if not type or t.is_a(type))


class Entity(entity.Entity):
    """A map of attributes associated with an L{EntityType}"""
    def __init__(self, entity_type, attributes=None, validate=True, **kwattrs):
        super(Entity, self).__init__(attributes, **kwattrs)
        self.__dict__['entity_type'] = entity_type
        self.attributes.setdefault('type', entity_type.name)
        if validate: self.validate()

    def __setitem__(self, name, value):
        super(Entity, self).__setitem__(name, value)
        self.validate()

    def validate(self):
        self.entity_type.validate(self.attributes)

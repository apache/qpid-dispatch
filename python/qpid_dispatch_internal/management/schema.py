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

"""
Schema for AMQP management entity types.

Schema validation will validate and transform values, add default values and
check for uniqueness of enties/attributes that are specified to be unique.

A Schema can be loaded/dumped to a json file.
"""

import sys
import traceback
from collections import OrderedDict
from qpid_dispatch.management.entity import EntityBase
from qpid_dispatch.management.error import NotImplementedStatus

try:
    from ..dispatch import LogAdapter, LOG_WARNING
    logger_available = True
except ImportError:
    # We need to do this because at compile time the schema is pulled using this code and at that time the
    # LogAdapter is not loaded. When running the router, the LogAdapter is available.
    logger_available = False


class ValidationError(Exception):
    """Error raised if schema validation fails"""
    pass


class Type:
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

    def validate(self, value):
        """Convert value to the correct python type."""
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
        super(BooleanType, self).__init__("boolean", bool)

    VALUES = {"yes": 1, "true": 1, "on": 1, "no": 0, "false": 0, "off": 0}

    def validate(self, value):
        """
        @param value: A string such as "yes", "false" etc. is converted appropriately.
            Any other type is converted using python's bool()
        @return A python bool.
        """
        try:
            if isinstance(value, str):
                return self.VALUES[value.lower()]
            return bool(value)
        except:
            raise ValidationError("Invalid Boolean value '%r'" % value)


class EnumValue(str):
    """A string that converts to an integer value via int()"""

    def __new__(cls, name, value):
        s = super(EnumValue, cls).__new__(cls, name)
        setattr(s, 'value', value)
        return s

    def __hash__(self): return super(EnumValue, self).__hash__()
    def __int__(self): return self.value
    def __long__(self): return self.value
    def __eq__(self, x): return str(self) == x or int(self) == x
    def __ne__(self, x): return not self == x
    def __repr__(self): return "EnumValue('%s', %s)" % (str(self), int(self))


class EnumType(Type):
    """An enumerated type"""

    def __init__(self, tags):
        """@param tags: A list of string values for the enumerated type."""
        assert isinstance(tags, list)
        super(EnumType, self).__init__("enum%s" % ([str(t) for t in tags]), int)
        self.tags = tags

    def validate(self, value):
        """
        @param value: May be a string from the set of enum tag strings or anything
            that can convert to an int - in which case it must be in the enum range.
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
        raise ValidationError("Invalid value for %s: %r" % (self.name, value))

    def dump(self):
        """@return: A list of the enum tags."""
        return self.tags

    def __str__(self):
        """String description of enum type."""
        return "One of [%s]" % ', '.join([("'%s'" % tag) for tag in self.tags])


class PropertiesType(Type):
    """
    A PropertiesType is a restricted map: keys must be AMQP 1.0 Symbol types.
    See the "fields" type in:
    http://docs.oasis-open.org/amqp/core/v1.0/os/amqp-core-transport-v1.0-os.html#type-fields
    """

    def __init__(self):
        super(PropertiesType, self).__init__("properties", dict)

    def validate(self, value):
        if not isinstance(value, dict):
            raise ValidationError("Properties must be a map")

        for key in value.keys():
            if (not isinstance(key, str)
                    or any(ord(x) > 127 for x in key)):
                raise ValidationError("Property keys must be ASCII encoded")
        return value


BUILTIN_TYPES = OrderedDict(
    (t.name, t) for t in [Type("string", str),
                          Type("path", str),
                          Type("entityId", str),
                          Type("integer", int),
                          Type("list", list),
                          Type("map", dict),
                          Type("dict", dict),
                          PropertiesType(),
                          BooleanType()])


def get_type(rep):
    """
    Get a schema type.
    @param rep: json representation of the type.
    """
    if isinstance(rep, list):
        return EnumType(rep)
    if rep in BUILTIN_TYPES:
        return BUILTIN_TYPES[rep]
    raise ValidationError("No such schema type: %s" % rep)


def _dump_dict(items):
    """
    Remove all items with None value from a mapping.
    @return: Map of non-None items.
    """
    return OrderedDict((k, v) for k, v in items if v)


class AttributeType:
    """
    Definition of an attribute.

    @ivar name: Attribute name.
    @ivar atype: Attribute L{Type}
    @ivar required: True if the attribute is required.
    @ivar default: Default value for the attribute or None if no default. Can be a reference.
    @ivar value: Fixed value for the attribute. Can be a reference.
    @ivar unique: True if the attribute value is unique.
    @ivar description: Description of the attribute type.
    @ivar defined_in: EntityType in which this attribute is defined.
    @ivar create: If true the attribute can be set by CREATE.
    @ivar update: If true the attribute can be modified by UPDATE.
    @ivar graph: If true the attribute could be graphed by a console.
    """

    def __init__(self, name, type=None, defined_in=None, default=None,
                 required=False, unique=False, hidden=False, deprecated=False, deprecationName=None,
                 value=None, description="", create=False, update=False, graph=False):
        """See L{AttributeType} instance variables."""
        try:
            self.name = name
            self.type = type
            self.defined_in = defined_in
            self.atype = get_type(self.type)
            self.required = required
            self.hidden = hidden
            self.deprecated = deprecated
            self.default = default
            self.deprecation_name = deprecationName
            self.value = value
            self.unique = unique
            self.description = description
            if self.value is not None and self.default is not None:
                raise ValidationError("Attribute '%s' has default value and fixed value" %
                                      self.name)
            self.create = create
            self.update = update
            self.graph = graph
        except Exception:
            raise ValidationError("Attribute '%s': %s\n%s"
                                  % (name,
                                     sys.exc_info()[1],
                                     sys.exc_info()[2]))

    def missing_value(self):
        """Fill in missing default and fixed values."""
        if self.value is not None:  # Fixed value attribute
            return self.value
        if self.default is not None:
            return self.default
        if self.required:
            raise ValidationError("Missing required attribute '%s'" % (self.name))

    def validate(self, value):
        """
        Validate value for this attribute definition.
        @param value: The value to validate.
        @return: value converted to the correct python type. Rais exception if any check fails.
        """
        if self.value and value != self.value:
            raise ValidationError("Attribute '%s' has fixed value '%s' but given '%s'" % (
                self.name, self.value, value))
        try:
            return self.atype.validate(value)
        except (TypeError, ValueError) as e:
            raise ValidationError("%s:%s" % (str(e), sys.exc_info()[2]))

    def dump(self):
        """@return: Json-friendly representation of an attribute type"""
        return _dump_dict([
            ('type', self.atype.dump()),
            ('default', self.default),
            ('required', self.required),
            ('unique', self.unique),
            ('deprecated', self.deprecated),
            ('description', self.description),
            ('graph', self.graph)
        ])

    def __str__(self):
        return self.name


class MessageDef:
    """A request or response message"""

    def __init__(self, body=None, properties=None):
        self.body = None
        if body:
            self.body = AttributeType("body", **body)
        self.properties = dict((name, AttributeType(name, **value))
                               for name, value in (properties or {}).items())


class OperationDef:
    """An operation definition"""

    def __init__(self, name, description=None, request=None, response=None):
        try:
            self.name = name
            self.description = description
            self.request = self.response = None
            if request:
                self.request = MessageDef(**request)
            if response:
                self.response = MessageDef(**response)
        except Exception as exc:
            raise ValidationError("Operation '%s': %s\n%s"
                                  % (name, str(exc), sys.exc_info()[2]))


class EntityType:
    """
    An entity type defines a set of attributes for an entity.

    @ivar name: Fully qualified entity type name.
    @ivar short_name: Un-prefixed short name.
    @ivar attributes: Map of L{AttributeType} for entity.
    @ivar singleton: If true only one entity of this type is allowed.
    @ivar referential: True if an entity can be referred to by name from another entity.
    """

    def __init__(self, name, schema, attributes=None, operations=None, operationDefs=None,
                 description="", fullName=True, singleton=False, deprecated=False,
                 extends=None, referential=False):
        """
        @param name: name of the entity type.
        @param schema: schema for this type.
        @param singleton: True if entity type is a singleton.
        @param attributes: Map of attributes {name: {type:, default:, required:, unique:}}
        @param description: Human readable description.
        @param operations: Allowed operations, list of operation names.
        """
        try:
            self.schema = schema
            self.description = description
            if fullName:
                self.name = schema.long_name(name)
                self.short_name = schema.short_name(name)

                if self.short_name.startswith("router.config."):
                    self.short_name = self.short_name.replace("router.config.", "")
            else:
                self.name = self.short_name = name
            self.attributes = OrderedDict((k, AttributeType(k, defined_in=self, **v))
                                          for k, v in (attributes or {}).items())

            self.deprecated_attributes = OrderedDict()
            for key, value in self.attributes.items():
                if value.deprecation_name or value.deprecated:
                    attr_type = AttributeType(value.deprecation_name or key,
                                              type=value.type,
                                              defined_in=self,
                                              default=value.default,
                                              required=value.required,
                                              unique=value.unique,
                                              hidden=value.hidden,
                                              deprecated=True,
                                              deprecationName=None,
                                              value=value.value,
                                              description="(DEPRECATED) " + value.description,
                                              create=value.create,
                                              update=value.update,
                                              graph=value.graph)
                    if value.deprecation_name:
                        self.deprecated_attributes[value.deprecation_name] = attr_type
                    else:
                        self.deprecated_attributes[key] = attr_type

            self.operations = operations or []

            # Bases are resolved in self.init()
            self.base = extends
            self.all_bases = []

            self.references = []
            self.singleton = singleton
            self.deprecated = deprecated
            self.referential = referential
            self._init = False      # Have not yet initialized from base and attributes.
            # Operation definitions
            self.operation_defs = dict((name, OperationDef(name, **op))
                                       for name, op in (operationDefs or {}).items())
        except Exception as exc:
            raise ValidationError("%s '%s': %s\n%s" % (type(self).__name__,
                                                       name,
                                                       exc,
                                                       sys.exc_info()[2]))

    def init(self):
        """Find bases after all types are loaded."""
        if self._init:
            return
        self._init = True
        if self.base:
            self.base = self.schema.entity_type(self.base)
            self.base.init()
            self.all_bases = [self.base] + self.base.all_bases
            self._extend(self.base, 'extend')

    def _extend(self, other, how):
        """Add attributes and operations from other"""
        def check(a, b, what):
            overlap = set(a) & set(b)
            if overlap:
                raise ValidationError("'%s' cannot %s '%s', re-defines %s: %s"
                                      % (self.name, how, other.short_name, what, ",".join(overlap)))
        check(self.operations, other.operations, "operations")
        self.operations += other.operations
        check(list(self.attributes.keys()), other.attributes.values(), "attributes")
        self.attributes.update(other.attributes)
        if other.name == 'entity':
            # Fill in entity "type" attribute automatically.
            self.attributes["type"]["value"] = self.name

    def extends(self, base): return base in self.all_bases

    def is_a(self, type): return type == self or self.extends(type)

    def attribute(self, name):
        """Get the AttributeType for name"""
        if name not in self.attributes and name not in self.deprecated_attributes.keys():
            raise ValidationError("Unknown attribute '%s' for '%s'" % (name, self))
        if self.attributes.get(name):
            return self.attributes[name]
        if self.deprecated_attributes.get(name):
            return self.deprecated_attributes[name]
        return None

    def log(self, level, text):
        self.schema.log(level, text)

    @property
    def my_attributes(self):
        """Return only attribute types defined in this entity type"""
        return [a for a in self.attributes.values() if a.defined_in == self]

    def validate(self, attributes):
        """
        Validate attributes for entity type.
        @param attributes: Map attributes name:value or Entity with attributes property.
            Modifies attributes: adds defaults, converts values.
        """
        if isinstance(attributes, SchemaEntity):
            attributes = attributes.attributes

        try:
            # Add missing values
            for attr in self.attributes.values():
                if attributes.get(attr.name) is None:
                    value = None
                    deprecation_name = attr.deprecation_name
                    if deprecation_name:
                        value = attributes.get(deprecation_name)
                        if value is not None:
                            if logger_available:
                                self.log(LOG_WARNING, "Attribute '%s' of entity '%s' has been deprecated."
                                                      " Use '%s' instead" % (deprecation_name, self.short_name, attr.name))
                            del attributes[deprecation_name]

                    if value is None:
                        value = attr.missing_value()
                    if value is not None:
                        attributes[attr.name] = value
                    if value is None and attr.name in attributes:
                        del attributes[attr.name]
                else:
                    deprecation_name = attr.deprecation_name
                    if deprecation_name:
                        value = attributes.get(deprecation_name)
                        if value is not None:
                            # Both name and deprecation name have values
                            # For example, both dir and direction of linkRoute have been specified, This is
                            # illegal. Just fail.
                            raise ValidationError("Both '%s' and '%s' cannot be specified for entity '%s'" %
                                                  (deprecation_name, attr.name, self.short_name))

            # Validate attributes.
            for name, value in attributes.items():
                if name == 'type':
                    value = self.schema.long_name(value)
                attributes[name] = self.attribute(name).validate(value)
        except ValidationError as e:
            raise ValidationError("%s: %s" % (self, e))

        return attributes

    def allowed(self, op, body):
        """Raise exception if op is not a valid operation on entity."""
        op = op.upper()
        if op not in self.operations:
            raise NotImplementedStatus("Operation '%s' not implemented for '%s' %s" % (
                op, self.name, self.operations))

    def create_check(self, attributes):
        for a in attributes:
            if not self.attribute(a).create:
                raise ValidationError("Cannot set attribute '%s' in CREATE" % a)

    def update_check(self, new_attributes, old_attributes):
        for a, v in new_attributes.items():
            # Its not an error to include an attribute in UPDATE if the value is not changed.
            if not self.attribute(a).update and \
               not (a in old_attributes and old_attributes[a] == v):
                raise ValidationError("Cannot update attribute '%s' in UPDATE" % a)

    def dump(self):
        """Json friendly representation"""
        return _dump_dict([
            ('attributes', OrderedDict(
                (k, v.dump()) for k, v in self.attributes.items()
                if k != 'type')),  # Don't dump 'type' attribute, dumped separately.
            ('operations', self.operations),
            ('description', self.description or None),
            ('fullyQualifiedType', self.name or None),
            ('references', self.references),
            ('deprecated', self.deprecated),
            ('singleton', self.singleton)
        ])

    def __repr__(self): return "%s(%s)" % (type(self).__name__, self.name)

    def __str__(self): return self.name

    def name_is(self, name):
        return self.name == self.schema.long_name(name)


class Schema:
    """
    Schema defining entity types.
    Note: keyword arguments come from schema so use camelCase

    @ivar prefix: Prefix to prepend to short entity type names.
    @ivar entityTypes: Map of L{EntityType} by name.
    @ivar description: Text description of schema.
    """

    def __init__(self, prefix="", entityTypes=None, description=""):
        """
        @param prefix: Prefix for entity names.
        @param entity_types: Map of  { entityTypeName: { singleton:, attributes:{...}}}
        @param description: Human readable description.
        """
        if logger_available:
            self.log_adapter = LogAdapter("AGENT")
        else:
            self.log_adapter = None

        if prefix:
            self.prefix = prefix.strip('.')
            self.prefixdot = self.prefix + '.'
        else:
            self.prefix = self.prefixdot = ""
        self.description = description

        def parsedefs(cls, defs):
            return OrderedDict((self.long_name(k), cls(k, self, **v))
                               for k, v in (defs or {}).items())

        self.entity_types = parsedefs(EntityType, entityTypes)

        self.all_attributes = set()

        for e in self.entity_types.values():
            e.init()
            self.all_attributes.update(e.attributes.keys())

    def log(self, level, text):
        if not self.log_adapter:
            return
        info = traceback.extract_stack(limit=2)[0]  # Caller frame info
        self.log_adapter.log(level, text, info[0], info[1])

    def short_name(self, name):
        """Remove prefix from name if present"""
        if not name:
            return name
        if name.startswith(self.prefixdot):
            name = name[len(self.prefixdot):]
        return name

    def long_name(self, name):
        """Add prefix to unqualified name"""
        if not name:
            return name
        if not name.startswith(self.prefixdot):
            name = self.prefixdot + name
        return name

    def dump(self):
        """Return json-friendly representation"""
        return OrderedDict([
            ('prefix', self.prefix),
            ('entityTypes',
             OrderedDict((e.short_name, e.dump()) for e in self.entity_types.values()))
        ])

    def _lookup(self, map, name, message, error):
        found = map.get(name) or map.get(self.long_name(name))
        if not found and error:
            raise ValidationError(message % name)
        return found

    def entity_type(self, name, error=True):
        return self._lookup(self.entity_types, name, "No such entity type '%s'", error)

    def validate_entity(self, attributes):
        """
        Validate a single entity.

        @param attributes: Map of attribute name: value
        """
        attributes['type'] = self.long_name(attributes['type'])
        entity_type = self.entity_type(attributes['type'])
        entity_type.validate(attributes)

    def validate_all(self, attribute_maps):
        """Validate all the entities from entity_iter, return a list of valid entities."""
        entities = []
        for a in attribute_maps:
            self.validate_add(a, entities)
            entities.append(a)

    def validate_add(self, attributes, entities):
        """
        Validate that attributes would be valid when added to entities.
        Assumes entities are already valid
        @raise ValidationError if adding e violates a global constraint like uniqueness.
        """
        self.validate_entity(attributes)
        entity_type = self.entity_type(attributes['type'])
        # Find all the unique attribute types present in attributes
        unique = [a for a in entity_type.attributes.values() if a.unique and a.name in attributes]
        if not unique and not entity_type.singleton:
            return              # Nothing to do
        for e in entities:
            if entity_type.singleton and attributes['type'] == e['type']:
                raise ValidationError("Adding %s singleton %s when %s already exists" %
                                      (attributes['type'], attributes, e))
            for a in unique:
                try:
                    if entity_type.attributes[a.name] == a and attributes[a.name] == e[a.name]:
                        raise ValidationError(
                            "adding %s duplicates unique attribute '%s' from existing %s" %
                            (attributes, a.name, e))
                except KeyError:
                    continue    # Missing attribute or definition means no clash

    def entity(self, attributes):
        """Convert an attribute map into an L{SchemaEntity}"""
        attributes = dict((k, v) for k, v in attributes.items() if v is not None)
        return SchemaEntity(self.entity_type(attributes['type']), attributes)

    def entities(self, attribute_maps):
        """Convert a list of attribute maps into a list of L{SchemaEntity}"""
        return [self.entity(m) for m in attribute_maps]

    def filter(self, predicate):
        """Return an iterator over entity types that satisfy predicate."""
        if predicate is None:
            return self.entity_types.values()
        return (t for t in self.entity_types.values() if predicate(t))

    def by_type(self, type):
        """Return an iterator over entity types that extend or are type.
        If type is None return all entities."""
        if not type:
            return self.entity_types.values()
        else:
            return self.filter(lambda t: t.is_a(type))


class SchemaEntity(EntityBase):
    """A map of attributes associated with an L{EntityType}"""

    def __init__(self, entity_type, attributes=None, validate=True, **kwattrs):
        super(SchemaEntity, self).__init__(attributes, **kwattrs)
        self.__dict__['entity_type'] = entity_type
        self.attributes.setdefault('type', entity_type.name)
        if validate:
            self.validate()

    def _set(self, name, value):
        super(SchemaEntity, self)._set(name, value)
        self.validate()

    def validate(self):
        self.entity_type.validate(self.attributes)

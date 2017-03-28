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
AMQP Managment Entity
"""

import itertools, re

def clean_dict(items, **kwargs):
    """
    @param items: A mapping or iterable of pairs.
    @return: dict containing items + kwargs without any None values. All keys are unicode.
    """
    if hasattr(items, 'iteritems'): items = items.iteritems()
    return dict((unicode(k), v) for k, v in itertools.chain(items, kwargs.iteritems())
                if v is not None)

class EntityBase(object):
    """
    A collection of named attributes.

    Attribute access:
    - via index operator: entity['foo']
    - as python attributes: entity.foo (only if attribute name is a legal python identitfier
      after replacing '-' with '_')

    @ivar attributes: Map of attribute values for this entity.

    NOTE: EntityBase does not itself implement the python map protocol because map
    methods (in particular 'update') can clash with AMQP methods and attributes.
    """

    def __init__(self, attributes=None, **kwargs):
        self.__dict__['attributes'] = {}
        if attributes:
            for k, v in attributes.iteritems():
                self.attributes[k] = v
                self.__dict__[self._pyname(k)] = v
        for k, v in kwargs.iteritems():
            self._set(k, v)

    def __getitem__(self, name):
        return self.attributes[name]

    def __getattr__(self, name):
        return self.attributes[name]

    def __contains__(self, name):
        return name in self.attributes

    @staticmethod
    def _pyname(name): return name.replace('-', '_')

    def _set(self, name, value):
        """Subclasses can override _set to do validation on each change"""
        self.attributes[name] = value
        self.__dict__[self._pyname(name)] = value

    # Access using []
    def __setitem__(self, name, value): self._set(name, value)

    def __delitem__(self, name):
        del self.attributes[name]
        del self.__dict__[self._pyname(name)]

    # Access as python attribute.
    def __setattr__(self, name, value): self._set(name, value)

    def __delattr__(self, name):
        self.__delitem__(name)

    def __repr__(self): return "EntityBase(%r)" % self.attributes

    SPECIAL = [u"name", u"identity", u"type"]
    N_SPECIAL = len(SPECIAL)
    PRIORITY = dict([(SPECIAL[i], i) for i in xrange(N_SPECIAL)])

    def __str__(self):
        # Print the name and identity first.
        keys = sorted(self.attributes.keys(), key=lambda k: self.PRIORITY.get(k, self.N_SPECIAL))
        return "Entity(%s)" % ", ".join("%s=%s" % (k, self.attributes[k]) for k in keys)


def update(entity, values):
    """Update entity from values
    @param entity: an Entity
    @param values: a map of values
    """
    for k, v in values.iteritems(): entity[k] = v

SEPARATOR_RE = re.compile(r' |_|-|\.')

def camelcase(str, capital=False):
    """Convert string str with ' ', '_', '.' or '-' separators to camelCase."""
    if not str: return ''
    words = SEPARATOR_RE.split(str)
    first = words[0]
    if capital: first = first[0].upper() + first[1:]
    return first + ''.join([w.capitalize() for w in words[1:]])

CAPS_RE = re.compile('[A-Z]')

def uncamelcase(str, separator='_'):
    """Convert camelCase string str to string with separator, e.g. camel_case"""
    if len(str) == 0: return str
    return str[0] + CAPS_RE.sub(lambda m: separator+m.group(0).lower(), str[1:])


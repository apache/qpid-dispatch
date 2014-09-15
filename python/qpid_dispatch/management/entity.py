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

import itertools

def clean_dict(items, **kwargs):
    """
    @param items: A mapping or iterable of pairs.
    @return: dict containing items + kwargs without any None values. All keys are unicode.
    """
    if hasattr(items, 'iteritems'): items = items.iteritems()
    return dict((unicode(k), v) for k, v in itertools.chain(items, kwargs.iteritems())
                if v is not None)

class Entity(object):
    """
    A collection of named attributes.

    Attribute access:
    - via index operator: entity['foo']
    - as python attributes: entity.foo (only if attribute name is a legal python identitfier)

    @ivar attributes: Map of attribute values for this entity.

    NOTE: Entity does not itself implement the python map protocol because map
    methods (in particular 'update') can clash with AMQP methods and attributes.
    """

    def __init__(self, attributes=None, **kwargs):
        self.__dict__['attributes'] = dict(attributes or [], **kwargs)

    def __getitem__(self, name): return self.attributes[name]

    def __setitem__(self, name, value): self.attributes[name] = value

    def __getattr__(self, name):
        try:
            return self.attributes[name]
        except KeyError:
            raise AttributeError("'%s' object has no attribute '%s'" % (type(self).__name__, name))

    def __setattr__(self, name, value):
        if name in self.__dict__:
            super(Entity, self).__setattr__(name, value)
        else:
            self.attributes[name] = value

    def __delattr__(self, name):
        try:
            del self.attributes[name]
        except KeyError:
            super(Entity, self).__delattr__(name)

    def __repr__(self): return "Entity(%r)" % self.attributes

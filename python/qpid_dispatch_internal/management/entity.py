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
Representation of management entities. An entity is a set of named attributes.
"""

from qpid_dispatch_internal.compat import OrderedDict


class Entity(OrderedDict):
    """
    A dict mapping attribute names to attribute value.
    Given e = Entity(), attribute 'a' can be accessed as e['a'] or e.a
    """

    def __getattr__(self, name):
        if not name in self:
            raise AttributeError("'%s' object has no attribute '%s'"%(self.__class__.__name__, name))
        return self[name]


class EntityList(list):
    """
    A list of entities with some convenience methods for finding entities
    by type or attribute value.
    """

    def get(self, **kwargs):
        """
        Get a list of entities matching the criteria defined by keyword arguments.
        @param kwargs: Set of attribute-name:value keywords.
            Return instances of entities where all the attributes match.
        @return: a list of entities.
        """
        def match(e):
            """True if e matches the criteria"""
            for name, value in kwargs.iteritems():
                if name not in e or e[name] != value:
                    return False
            return True
        return [e for e in self if match(e)]

    def validate(self, schema):
        """Validate against a schema"""
        schema.validate(self)

    def __getattr__(self, name):
        return self.get(type=name)

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
An Entity object also provides access to the attributes via python properties.
"""

import re
from collections import MutableMapping
from qpid_dispatch_internal.compat import OrderedDict

ILLEGAL = re.compile(r'\W')

def identifier(name):
    """
    Convert name to a python identifier by replacing all illegal characters with '_'
    """
    return ILLEGAL.sub('_', name)

class AttributePropertyMap(OrderedDict):
    """
    An OrderedDict of attribute name:value that also provides
    a mapping of python property names to attribute names.

    The property name is the attribute name with any illegal characters
    (e.g. '-') are converted to '_'.

    Inserting an attribute that maps to the same property name as
    a different attribute already in the map will raise KeyError.
    """

    class _NameMap(dict):
        """A map of property to attribute names"""
        def add(self, attrname):
            propname = identifier(attrname)
            exists = self.get(propname)
            if exists and exists != attrname:
                raise KeyError("Ambiguous property name '%s'" % propname)
            self[propname] = attrname

        def drop(self, attrname):
            for k in self.keys():
                if self[k] == attrname:
                    del self[k]
                    return

    def __init__(self, *args, **kwargs):
        """
        Same __init__ arguments as OrderedDict or dict.
        """
        super(AttributePropertyMap, self).__init__()
        self._names = self._NameMap()
        self.update(*args, **kwargs) # Also updates self._names

    def _attribute_name(self, propname):
        return self._names.get(propname)

    def __setitem__(self, attrname, value):
        self._names.add(attrname)
        super(AttributePropertyMap, self).__setitem__(attrname, value);

    def __delitem__(self, attrname):
        super(AttributePropertyMap, self).__delitem__(attrname);
        self._names.drop(attrname)

class Entity(object):
    """
    An Entity provides a ordered map of attributes.

    Given Entity e, attribute 'foo' can be accessed as follows:

    self.foo
    self['foo']
    self.attributes['foo']

    The property name is the attribute name with any illegal characters
    (e.g. '-') are converted to '_'.

    @ivar attributes: An OrderedDict of attribute name:value.
    """

    def __init__(self, attributes, **kwargs):
        """
        @param attributes: map of atrribute name:value or list of [(name, value)]
        @param kwargs: additional attributes name=value
        """
        # Access __dict__ directly to avoid __getattr__ recursion.
        self.__dict__['attributes'] = AttributePropertyMap(attributes, **kwargs)

    def _attribute_name(self, propname):
        attrname = self.attributes._attribute_name(propname)
        if not attrname:
            raise AttributeError("'%s' has no attribute '%s'"%(type(self).__name__, propname))
        return attrname

    def __getattr__(self, propname):
        return self.attributes[self._attribute_name(propname)]

    def __setattr__(self, propname, value):
        attrname = self.attributes._attribute_name(propname) or propname
        self.attributes[attrname] = value

    def __delattr__(self, propname):
        del self.attributes[self._attribute_name(propname)]

    def __setitem__(self, attrname, value):
        self.attributes[attrname] = value

    def __getitem__(self, attrname):
        return self.attributes[attrname]

    def __repr__(self):
        return "%s(%s)" % (type(self).__name__, super(Entity, self).__repr__())

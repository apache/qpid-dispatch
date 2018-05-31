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

"""Compatibility hacks for older versions of python"""

from __future__ import unicode_literals
from __future__ import division
from __future__ import absolute_import
from __future__ import print_function

__all__ = [
    "OrderedDict",
    "JSON_LOAD_KWARGS",
    "dictify",
    "IS_PY2",
    "PY_STRING_TYPE",
    "PY_TEXT_TYPE",
    "PY_BINARY_TYPE",
    "PY_INTEGER_TYPES",
    "dict_iterkeys",
    "dict_itervalues",
    "dict_iteritems",
    "UNICODE",
    "BINARY"
]

import sys

try:
    from collections import OrderedDict
except Exception:
    from ordereddict import OrderedDict

if sys.version_info >= (2, 7):
    JSON_LOAD_KWARGS = {'object_pairs_hook':OrderedDict}
else:
    JSON_LOAD_KWARGS = {}

def dictify(od):
    """Recursively replace OrderedDict with dict"""
    if isinstance(od, OrderedDict):
        return dict((k, dictify(v)) for k, v in dict_iteritems(od))
    else:
        return od

IS_PY2 = sys.version_info[0] == 2

if IS_PY2:
    PY_STRING_TYPE = basestring  # noqa: F821
    PY_TEXT_TYPE = unicode  # noqa: F821
    PY_BINARY_TYPE = str
    PY_INTEGER_TYPES = (int, long)  # noqa: F821
    PY_LONG_TYPE = long  # noqa: F821
    def dict_iterkeys(d):
        return d.iterkeys()
    def dict_itervalues(d):
        return d.itervalues()
    def dict_iteritems(d):
        return d.iteritems()
    def dict_keys(d):
        return d.keys()
    def dict_values(d):
        return d.values()
    def dict_items(d):
        return d.items()
    def BINARY(s):
        ts = type(s)
        if ts is str:
            return s
        elif ts is unicode:  # noqa: F821
            return s.encode("utf-8")
        else:
            raise TypeError("%s cannot be converted to binary" % ts)
    def UNICODE(s):
        if type(s) is str:
            return s.decode("utf-8")
        elif type(s) is unicode:  # noqa: F821
            return s
        else:
            return unicode(str(s), "utf-8")  # noqa: F821
    def LONG(i):
        return long(i)  # noqa: F821
else:
    PY_STRING_TYPE = str
    PY_TEXT_TYPE = str
    PY_BINARY_TYPE = bytes
    PY_INTEGER_TYPES = (int,)
    PY_LONG_TYPE = int
    def dict_iterkeys(d):
        return iter(d.keys())
    def dict_itervalues(d):
        return iter(d.values())
    def dict_iteritems(d):
        return iter(d.items())
    # .keys(), .items(), .values()
    # now return a dict_view not a list. Should be ok unless dict is modified
    # when iterating over it. Use these if that's the case:
    def dict_keys(d):
        return list(d.keys())
    def dict_values(d):
        return list(d.values())
    def dict_items(d):
        return list(d.items())
    def BINARY(s):
        st = type(s)
        if st is str:
            return s.encode("utf-8")
        elif st is bytes:
            return s
        else:
            raise TypeError("%s cannot be converted to binary" % st)
    def UNICODE(s):
        if type(s) is bytes:
            return s.decode("utf-8")
        elif type(s) is str:
            return s
        else:
            return str(s)
    def LONG(i):
        return int(i)

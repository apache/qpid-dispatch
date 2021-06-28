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

__all__ = [
    "JSON_LOAD_KWARGS",
    "dictify",
    "UNICODE",
    "BINARY"
]

from collections import OrderedDict

JSON_LOAD_KWARGS = {'object_pairs_hook': OrderedDict}


def dictify(od):
    """Recursively replace OrderedDict with dict"""
    if isinstance(od, OrderedDict):
        return dict((k, dictify(v)) for k, v in od.items())
    else:
        return od


def BINARY(s):
    st = type(s)
    if st is str:
        return s.encode("utf-8")
    elif st is bytes:
        return s
    else:
        raise TypeError("%s cannot be converted to binary" % st)


def UNICODE(s):
    if isinstance(s, bytes):
        return s.decode("utf-8")
    elif isinstance(s, str):
        return s
    else:
        return str(s)

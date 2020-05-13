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
# under the License.
#

##
## Define the current protocol version.  Any messages that do not contain version
## information shall be considered to be coming from routers using version 0.
##

from __future__ import unicode_literals
from __future__ import division
from __future__ import absolute_import
from __future__ import print_function

from ..compat import PY_LONG_TYPE
from ..compat import LONG
from ..compat import PY_TEXT_TYPE

ProtocolVersion = LONG(2)
LegacyVersion   = LONG(1)

def getMandatory(data, key, cls=None):
    """
    Get the value mapped to the requested key.    If it's not present, raise an exception.
    """
    if key in data:
        value = data[key]
        if cls and value.__class__ != cls:
            raise Exception("Protocol field has wrong data type: '%s' type=%r expected=%r" % (key, value.__class__, cls))
        return value
    raise Exception("Mandatory protocol field missing: '%s'" % key)


def getOptional(data, key, default=None, cls=None):
    """
    Get the value mapped to the requested key.  If it's not present, return the default value.
    """
    if key in data:
        value = data[key]
        if cls and value.__class__ != cls:
            raise Exception("Protocol field has wrong data type: '%s' type=%r expected=%r" % (key, value.__class__, cls))
        return value
    return default


def isCompatibleVersion(body):
    """
    Return True iff the version of the message body is compatible with this protocol implementation.
    """
    version = 0
    try:
        version = getOptional(body, 'pv', 0, PY_LONG_TYPE)
    except Exception:
        pass

    return version == ProtocolVersion or version == LegacyVersion


def getIdAndVersion(body):
    """
    Return a tuple of id and version from a message body
    """
    result = ('<unknown>', 0)
    try:
        result = (getOptional(body, 'id', '<unknown>', PY_TEXT_TYPE), getOptional(body, 'pv', 0, PY_LONG_TYPE))
    except Exception:
        pass
    return result


class LinkState(object):
    """
    The link-state of a single router.  The link state consists of a list of neighbor routers reachable from
    the reporting router.  The link-state-sequence number is incremented each time the link state changes.
    """
    def __init__(self, body, _id=None, _ls_seq=None, _peers=None):
        self.last_seen = 0
        if body:
            self.id = getMandatory(body, 'id', PY_TEXT_TYPE)
            self.area = '0'
            self.ls_seq = getMandatory(body, 'ls_seq', PY_LONG_TYPE)
            self.peers = getMandatory(body, 'peers', dict)
        else:
            self.id = _id
            self.area = '0'
            self.ls_seq = LONG(_ls_seq)
            self.peers = _peers

    def __repr__(self):
        return "LS(id=%s area=%s ls_seq=%d peers=%r)" % (self.id, self.area, self.ls_seq, self.peers)

    def to_dict(self):
        return {'id'     : self.id,
                'area'   : self.area,
                'ls_seq' : self.ls_seq,
                'peers'  : self.peers}

    def add_peer(self, _id, _cost):
        if _id not in self.peers:
            self.peers[_id] = _cost
            return True
        return False

    def del_peer(self, _id):
        if _id in self.peers:
            self.peers.pop(_id)
            return True
        return False

    def del_all_peers(self):
        self.peers = {}
        self.ls_seq = 0

    def has_peers(self):
        return len(self.peers) > 0

    def is_peer(self, _id):
        return _id in self.peers

    def bump_sequence(self):
        self.ls_seq += 1


class MessageHELLO(object):
    """
    HELLO Message
    scope: neighbors only - HELLO messages travel at most one hop
    This message is used by directly connected routers to determine with whom they have
    bidirectional connectivity.
    """
    def __init__(self, body, _id=None, _seen_peers=None, _instance=LONG(0), legacy=False):
        if body:
            self.id = getMandatory(body, 'id', PY_TEXT_TYPE)
            self.area = '0'
            self.seen_peers = getMandatory(body, 'seen', list)
            self.instance = getOptional(body, 'instance', 0, PY_LONG_TYPE)
            self.version  = getOptional(body, 'pv', 0, PY_LONG_TYPE)
        else:
            self.id   = _id
            self.area = '0'
            self.seen_peers = _seen_peers
            self.instance = _instance
            self.version  = ProtocolVersion if not legacy else LegacyVersion

    def __repr__(self):
        return "HELLO(id=%s pv=%d area=%s inst=%d seen=%r)" % (self.id, self.version, self.area, self.instance, self.seen_peers)

    def get_opcode(self):
        return 'HELLO'

    def to_dict(self):
        return {'id'       : self.id,
                'pv'       : self.version,
                'area'     : self.area,
                'instance' : self.instance,
                'seen'     : self.seen_peers}

    def is_seen(self, _id):
        return self.seen_peers.count(_id) > 0


class MessageRA(object):
    """
    Router Advertisement (RA) Message
    scope: all routers in the area and all designated routers
    This message is sent periodically to indicate the originating router's sequence numbers
    for link-state and mobile-address-state.
    """
    def __init__(self, body, _id=None, _ls_seq=None, _mobile_seq=None, _instance=LONG(0), legacy=False):
        if body:
            self.id = getMandatory(body, 'id', PY_TEXT_TYPE)
            self.area = '0'
            self.ls_seq = getMandatory(body, 'ls_seq', PY_LONG_TYPE)
            self.mobile_seq = getMandatory(body, 'mobile_seq', PY_LONG_TYPE)
            self.instance = getOptional(body, 'instance', 0, PY_LONG_TYPE)
            self.version  = getOptional(body, 'pv', 0, PY_LONG_TYPE)
        else:
            self.id = _id
            self.area = '0'
            self.ls_seq = LONG(_ls_seq)
            self.mobile_seq = LONG(_mobile_seq)
            self.instance = _instance
            self.version  = ProtocolVersion if not legacy else LegacyVersion

    def get_opcode(self):
        return 'RA'

    def __repr__(self):
        return "RA(id=%s pv=%d area=%s inst=%d ls_seq=%d mobile_seq=%d)" % \
                (self.id, self.version, self.area, self.instance, self.ls_seq, self.mobile_seq)

    def to_dict(self):
        return {'id'         : self.id,
                'pv'         : self.version,
                'area'       : self.area,
                'instance'   : self.instance,
                'ls_seq'     : self.ls_seq,
                'mobile_seq' : self.mobile_seq}


class MessageLSU(object):
    """
    """
    def __init__(self, body, _id=None, _ls_seq=None, _ls=None, _legacy=[], _instance=LONG(0), legacy=False):
        if body:
            self.id       = getMandatory(body, 'id', PY_TEXT_TYPE)
            self.area     = '0'
            self.ls_seq   = getMandatory(body, 'ls_seq', PY_LONG_TYPE)
            self.ls       = LinkState(getMandatory(body, 'ls', dict))
            self.instance = getOptional(body, 'instance', 0, PY_LONG_TYPE)
            self.version  = getOptional(body, 'pv', 0, PY_LONG_TYPE)
            self.legacy   = getOptional(body, 'legacy', [], list)
        else:
            self.id       = _id
            self.area     = '0'
            self.ls_seq   = LONG(_ls_seq)
            self.ls       = _ls
            self.instance = _instance
            self.version  = ProtocolVersion if not legacy else LegacyVersion
            self.legacy   = _legacy if not legacy else []

    def get_opcode(self):
        return 'LSU'

    def __repr__(self):
        result = "LSU(id=%s pv=%d area=%s inst=%d ls_seq=%d ls=%r" % \
            (self.id, self.version, self.area, self.instance, self.ls_seq, self.ls)
        if self.version == ProtocolVersion:
            result += " legacy=%r" % self.legacy
        result += ")"
        return result

    def to_dict(self):
        d = {'id'       : self.id,
             'pv'       : self.version,
             'area'     : self.area,
             'instance' : self.instance,
             'ls_seq'   : self.ls_seq,
             'ls'       : self.ls.to_dict()}
        if len(self.legacy) > 0:
            d['legacy'] = self.legacy
        return d


class MessageLSR(object):
    """
    """
    def __init__(self, body, _id=None, legacy=False):
        if body:
            self.id = getMandatory(body, 'id', PY_TEXT_TYPE)
            self.version = getOptional(body, 'pv', 0, PY_LONG_TYPE)
            self.area = '0'
        else:
            self.id = _id
            self.version = ProtocolVersion if not legacy else LegacyVersion
            self.area = '0'

    def get_opcode(self):
        return 'LSR'

    def __repr__(self):
        return "LSR(id=%s pv=%d area=%s)" % (self.id, self.version, self.area)

    def to_dict(self):
        return {'id'      : self.id,
                'pv'      : self.version,
                'area'    : self.area}



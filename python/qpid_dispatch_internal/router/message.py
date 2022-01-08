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

"""Python class to hold message data"""

import json


class Message:
    """
    Holder for message attributes used by python IoAdapter send/receive.

    Interface is like proton.Message, but we don't use proton.Message here because
    creating a proton.Message has side-effects on the proton engine.

    @ivar body: The body of the message, normally a map.
    @ivar to: The to-address for the message.
    @ivar reply_to: The reply-to address for the message.
    @ivar correlation_id: Correlation ID for replying to the message.
    @ivar properties: Application properties.
    """

    _fields = ['address', 'properties', 'body', 'reply_to', 'correlation_id', 'content_type']

    def __init__(self, **kwds):
        """All instance variables can be set as keywords. See L{Message}"""
        for f in self._fields:
            setattr(self, f, kwds.get(f, None))
        for k in kwds:
            getattr(self, k)    # Check for bad attributes

    def __repr__(self):
        return "%s(%s)" % (type(self).__name__,
                           ", ".join("%s=%r" % (f, getattr(self, f)) for f in self._fields))


def simplify(msg):
    m = {}
    for k, v in msg.properties.items():
        m[k] = v
    if msg.body:
        m["body"] = msg.body.decode()
    if msg.content_type:
        m["content_type"] = msg.content_type
    return m


def messages_to_json(msgs):
    return json.dumps([simplify(m) for m in msgs], indent=4)

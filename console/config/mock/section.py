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

import json
import re
from schema import Schema
import pdb

class ConfigSection(object):
    def __init__(self, type, defaults, ignore, forced, opts):
        self.type = type
        self.entries = {}
        for key in defaults:
            if not key in opts:
                opts[key] = defaults[key]

        for key in list(opts.keys()):
            if not key in Schema.attrs(self.type):
                del opts[key]
            elif key.endswith('Count') or key in ignore or opts[key] is None:
                del opts[key]
            elif opts[key] == Schema.default(self.type, key) and key not in forced:
                del opts[key]

        self.setEntries(opts)

    def setType(self, type):
        self.type = type

    def setEntry(self, name, val):
        self.entries[name] = val

    def setEntries(self, d):
        self.entries.update(d)

    def __repr__(self):
        # ensure all entries have values
        self.entries = {k: v for k, v in self.entries.iteritems() if self.entries.get(k)}
        raw = self.type + " " + json.dumps(self.entries, indent=4, separators=('', ': '))
        return re.sub('["]', '', raw)

class RouterSection(ConfigSection):
    defaults = {"mode": "interior"}
    ignore = ["type", "routerId", "identity", "name"]
    def __init__(self, id, **kwargs):
        super(RouterSection, self).__init__("router", RouterSection.defaults, RouterSection.ignore, [], kwargs)
        self.setEntry("id", id)

    def __repr__(self):
        s = super(RouterSection, self).__repr__()
        return s.replace('deploy_host', '#deploy_host', 1)

class ListenerSection(ConfigSection):
    defaults = {"role": "normal",
                 "host": "0.0.0.0",
                 "saslMechanisms": 'ANONYMOUS'
                 }
    def __init__(self, port, **kwargs):
        super(ListenerSection, self).__init__("listener", ListenerSection.defaults, [], [], kwargs)
        self.setEntry("port", port)

class ConnectorSection(ConfigSection):
    defaults = {"role": "normal",
                "host": "0.0.0.0",
                "saslMechanisms": 'ANONYMOUS'
                }
    def __init__(self, port, **kwargs):
        super(ConnectorSection, self).__init__("connector", ConnectorSection.defaults, [], [], kwargs)
        self.setEntry("port", port)

class SslProfileSection(ConfigSection):
    def __init__(self, **kwargs):
        super(SslProfileSection, self).__init__("sslProfile", {}, [], [], kwargs)

class LogSection(ConfigSection):
    def __init__(self, **kwargs):
        super(LogSection, self).__init__("log", {}, [], [], kwargs)

class AddressSection(ConfigSection):
    def __init__(self, **kwargs):
        super(AddressSection, self).__init__("address", {}, [], ['distribution'], kwargs)

if __name__ == '__main__':
    Schema.init('../')
    r = RouterSection("QDR.A")
    l = ListenerSection(20000)
    c = ConnectorSection(20001)
    s = SslProfileSection()
    g = LogSection(module="ROUTER", enable="trace+")
    print r
    print l
    print c
    print s
    print g


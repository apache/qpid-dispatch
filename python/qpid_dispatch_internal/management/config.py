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
Configuration file parsing
"""

import json, re, sys
import os
from copy import copy
from qpid_dispatch.management.entity import camelcase

from ..dispatch import QdDll
from .qdrouter import QdSchema

class Config(object):
    """Load config entities from qdrouterd.conf and validated against L{QdSchema}."""

    def __init__(self, filename=None, schema=QdSchema(), raw_json=False):
        self.schema = schema
        self.config_types = [et for et in schema.entity_types.itervalues()
                             if schema.is_configuration(et)]
        if filename:
            try:
                self.load(filename, raw_json)
            except Exception, e:
                raise Exception, "Cannot load configuration file %s: %s" % (filename, e), sys.exc_info()[2]
        else:
            self.entities = []

    @staticmethod
    def _parse(lines):
        """Parse config file format into a section list"""
        begin = re.compile(r'([\w-]+)[ \t]*{') # WORD {
        end = re.compile(r'}')                 # }
        attr = re.compile(r'([\w-]+)[ \t]*:[ \t]*(.+)') # WORD1: VALUE

        def sub(line):
            """Do substitutions to make line json-friendly"""
            line = line.split('#')[0].strip() # Strip comments
            line = re.sub(begin, r'["\1", {', line)
            line = re.sub(end, r'}],', line)
            line = re.sub(attr, r'"\1": "\2",', line)
            return line

        js_text = "[%s]"%("\n".join([sub(l) for l in lines]))
        spare_comma = re.compile(r',\s*([]}])') # Strip spare commas
        js_text = re.sub(spare_comma, r'\1', js_text)
        # Convert dictionary keys to camelCase
        sections = json.loads(js_text)
        for s in sections:
            s[0] = camelcase(s[0])
            s[1] = dict((camelcase(k), v) for k, v in s[1].iteritems())
            if s[0] == "address":   s[0] = "router.config.address"
            if s[0] == "linkRoute": s[0] = "router.config.linkRoute"
            if s[0] == "autoLink":  s[0] = "router.config.autoLink"
        return sections

    @staticmethod
    def _parserawjson(lines):
        """Parse raw json config file format into a section list"""
        def sub(line):
            """Do substitutions to make line json-friendly"""
            line = line.split('#')[0].strip() # Strip comments
            return line
        js_text = "%s"%("\n".join([sub(l) for l in lines]))
        sections = json.loads(js_text)
        return sections

    def get_config_types(self):
        return self.config_types

    def load(self, source, raw_json=False):
        """
        Load a configuration file.
        @param source: A file name, open file object or iterable list of lines
        @param raw_json: Source is pure json not needing conf-style substitutions
        """
        if isinstance(source, basestring):
            raw_json |= source.endswith(".json")
            with open(source) as f:
                self.load(f, raw_json)
        else:
            sections = self._parserawjson(source) if raw_json else self._parse(source)
            # Add missing singleton sections
            for et in self.get_config_types():
                if et.singleton and not [s for s in sections if s[0] == et.short_name]:
                    sections.append((et.short_name, {}))
            entities = [dict(type=self.schema.long_name(s[0]), **s[1]) for s in sections]
            self.schema.validate_all(entities)
            self.entities = entities

    def by_type(self, entity_type):
        """Return entities of given type"""
        entity_type = self.schema.long_name(entity_type)
        return [e for e in self.entities if e['type'] == entity_type]

    def remove(self, entity):
        self.entities.remove(entity)

class PolicyConfig(Config):
    def __init__(self, filename=None, schema=QdSchema(), raw_json=False):
        super(PolicyConfig, self).__init__(filename, schema, raw_json)

    def get_config_types(self):
        return [s for s in self.config_types if 'policy' in s.name]

def configure_dispatch(dispatch, lib_handle, filename):
    """Called by C router code to load configuration file and do configuration"""
    qd = QdDll(lib_handle)
    dispatch = qd.qd_dispatch_p(dispatch)
    config = Config(filename)

    # NOTE: Can't import agent till dispatch C extension module is initialized.
    from .agent import Agent
    agent = Agent(dispatch, qd)
    qd.qd_dispatch_set_agent(dispatch, agent)

    def configure(attributes):
        """Configure an entity and remove it from config"""
        agent.configure(attributes)
        config.remove(attributes)

    modules = set(agent.schema.entity_type("log").attributes["module"].atype.tags)
    for l in config.by_type('log'):
        configure(l)
        modules.remove(l["module"])

    # Add default entities for any log modules not configured.
    for m in modules:
        agent.configure(attributes=dict(type="log", module=m))

    # Configure and prepare container and router before we can activate the agent.
    configure(config.by_type('container')[0])
    configure(config.by_type('router')[0])
    qd.qd_dispatch_prepare(dispatch)
    qd.qd_router_setup_late(dispatch) # Actions requiring active management agent.
    agent.activate("$_management_internal")

    from qpid_dispatch_internal.display_name.display_name import DisplayNameService
    displayname_service = DisplayNameService("$displayname")
    qd.qd_dispatch_register_display_name_service(dispatch, displayname_service)
    policyDir = config.by_type('policy')[0]['policyDir']
    policyDefaultVhost = config.by_type('policy')[0]['defaultVhost']
    # Remaining configuration
    for t in "sslProfile", "fixedAddress", "listener", "connector", "waypoint", "linkRoutePattern", \
             "router.config.address", "router.config.linkRoute", "router.config.autoLink", \
             "policy", "vhost":
        for a in config.by_type(t):
            configure(a)
            if t == "sslProfile":
                display_file_name = a.get('displayNameFile')
                if display_file_name:
                    ssl_profile_name = a.get('name')
                    displayname_service.add(ssl_profile_name, display_file_name)

    for e in config.entities:
        configure(e)

    # Load the vhosts from the .json files in policyDir
    # Only vhosts are loaded. Other entities are silently discarded.
    if not policyDir == '':
        apath = os.path.abspath(policyDir)
        for i in os.listdir(policyDir):
            if i.endswith(".json"):
                pconfig = PolicyConfig(os.path.join(apath, i))
                for a in pconfig.by_type("vhost"):
                    agent.configure(a)

    # Set policy default application after all rulesets loaded
    agent.policy.set_default_vhost(policyDefaultVhost)

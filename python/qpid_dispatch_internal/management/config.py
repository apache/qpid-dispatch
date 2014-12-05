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
from copy import copy
from qpid_dispatch.management.entity import camelcase
from .schema import ValidationError
from .. import dispatch_c
from .qdrouter import QdSchema

class Config(object):
    """Load config entities from qdrouterd.conf and validated against L{QdSchema}."""

    def __init__(self, filename=None, schema=QdSchema()):
        self.schema = schema
        self.config_types = [e for e in schema.entity_types.itervalues()
                             if schema.is_configuration(e)]
        if filename:
            self.load(filename)
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

        js_text = "[%s]"%("".join([sub(l) for l in lines]))
        spare_comma = re.compile(r',\s*([]}])') # Strip spare commas
        js_text = re.sub(spare_comma, r'\1', js_text)
        # Convert dictionary keys to camelCase
        sections = json.loads(js_text)
        for s in sections:
            s[0] = camelcase(s[0])
            s[1] = dict((camelcase(k), v) for k, v in s[1].iteritems())
        return sections


    def _expand(self, content):
        """
        Find annotation sections (defined by schema) in the content,
        expand references and remove the annotation sections.
        @param content: ((section-name:{name:value...}))
        """
        def _expand_section(section, annotations):
            """Expand one section"""
            attrs = section[1]
            for k in attrs.keys(): # Iterate over keys() because we will modify attr
                inc = [i[1] for i in annotations if i[0] == k and i[1]['name'] == attrs[k]]
                if inc:
                    assert len(inc) == 1
                    inc = copy(inc[0])
                    del inc['name'] # Not a real attribute, just an annotation id.
                    attrs.update(inc)
                    del attrs[k] # Delete the annotation attribute.
            return section
        annotations = [s for s in content if self.schema.annotation(s[0], error=False)]
        return [_expand_section(s, annotations) for s in content
                if self.schema.is_configuration(self.schema.entity_type(s[0], False))]

    def _default_ids(self, content):
        """
        Set default name and identity where missing.
        - If entity has no name/identity, set both to "<entity-type>:<i>"
        - If entity has one of name/identity set the other to be the same.
        - If entity has both, do nothing
        """
        counts = dict((et.short_name, 0) for et in self.config_types)
        for section in content:
            section_name, attrs = section
            count = counts[section_name]
            counts[section_name] += 1
            if 'name' in attrs and 'identity' in attrs:
                continue
            elif 'name' in attrs:
                attrs['identity'] = attrs['name']
            elif 'identity' in attrs:
                attrs['name'] = attrs['identity']
            else:
                identity = "%s:%d"%(section_name, count)
                attrs['name'] = attrs['identity'] = identity
        return content

    def load(self, source):
        """
        Load a configuration file.
        @param source: A file name, open file object or iterable list of lines
        """
        if isinstance(source, basestring):
            with open(source) as f:
                try:
                    self.load(f)
                except:
                    ex_type, ex_value, ex_trace = sys.exc_info()
                    raise ValidationError, "Loading '%s', %s: %s"%(source, ex_type.__name__, ex_value), ex_trace
        else:
            sections = self._parse(source)
            # Add missing singleton sections
            for et in self.config_types:
                if et.singleton and not [s for s in sections if s[0] == et.short_name]:
                    sections.append((et.short_name, {}))
            sections = self._expand(sections)
            sections = self._default_ids(sections)
            entities = [dict(type=self.schema.long_name(s[0]), **s[1]) for s in sections]
            self.schema.validate_all(entities)
            self.entities = entities

    def by_type(self, entity_type):
        """Iterator over entities of given type"""
        entity_type = self.schema.long_name(entity_type)
        for e in self.entities:
            if e['type'] == entity_type: yield e

def configure_dispatch(dispatch, filename):
    """Called by C router code to load configuration file and do configuration"""
    qd = dispatch_c.instance()
    dispatch = qd.qd_dispatch_p(dispatch)
    config = Config(filename)

    # Configure any DEFAULT log entities first so we can report errors in non-
    # default log configurations to the correct place.
    for l in config.by_type('log'):
        if l['module'].upper() == 'DEFAULT': qd.qd_log_entity(l)
    for l in config.by_type('log'):
        if l['module'].upper() != 'DEFAULT': qd.qd_log_entity(l)

    # NOTE: Can't import agent till till dispatch C extension module is initialized.
    from .agent import Agent
    agent = Agent(dispatch, config.entities)
    qd.qd_dispatch_set_agent(dispatch, agent)

    # Configure and prepare container and router before activating agent
    qd.qd_dispatch_configure_container(dispatch, config.by_type('container').next())
    qd.qd_dispatch_configure_router(dispatch, config.by_type('router').next())
    qd.qd_dispatch_prepare(dispatch)

    agent.activate("$management")

    qd.qd_router_setup_late(dispatch) # Actions requiring management agent.

    # Note must configure addresses, waypoints, listeners and connectors after qd_dispatch_prepare
    for a in config.by_type('fixedAddress'): qd.qd_dispatch_configure_address(dispatch, a)
    for w in config.by_type('waypoint'): qd.qd_dispatch_configure_waypoint(dispatch, w)
    for l in config.by_type('listener'): qd.qd_dispatch_configure_listener(dispatch, l)
    for c in config.by_type('connector'): qd.qd_dispatch_configure_connector(dispatch, c)
    qd.qd_connection_manager_start(dispatch)
    qd.qd_waypoint_activate_all(dispatch)


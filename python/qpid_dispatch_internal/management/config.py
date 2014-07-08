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
import schema
from copy import copy
from qpid_dispatch_internal import dispatch_c
from .entity import EntityList, Entity
from .qdrouter import QdSchema
from ..compat import json_load_kwargs

class Config(EntityList):
    """An L{EntityList} loaded from a qdrouterd.conf and validated against L{QdSchema}."""

    def __init__(self, filename=None, schema=QdSchema()):
        self.schema = schema
        if filename: self.load(filename)

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
        return json.loads(js_text, **json_load_kwargs)

    def _expand(self, content):
        """
        Find include sections (defined by schema) in the content,
        expand references and remove the include sections.
        @param content: ((section-name:{name:value...}))
        """
        def _expand_section(section, includes):
            """Expand one section"""
            attrs = section[1]
            for k in attrs.keys(): # Iterate over keys() because we will modify attr
                inc = [i[1] for i in includes if i[0] == k and i[1]['name'] == attrs[k]]
                if inc:
                    assert len(inc) == 1
                    inc = copy(inc[0])
                    del inc['name'] # Not a real attribute, just an include id.
                    attrs.update(inc)
                    del attrs[k] # Delete the include attribute.
            return section
        includes = [s for s in content if s[0] in self.schema.includes]
        return [_expand_section(s, includes) for s in content if s[0] not in self.schema.includes]

    def _default_ids(self, content):
        """
        Set default name and identity where missing.
        - If entity has no name/identity, set both to "<entity-type>-<i>"
        - If entity has one of name/identity set the other to be the same.
        - If entity has both, do nothing
        """
        counts = dict((e, 0) for e in self.schema.entity_types)
        for section in content:
            entity_type, attrs = section
            count = counts[entity_type]
            counts[entity_type] += 1
            if 'name' in attrs and 'identity' in attrs:
                continue
            elif 'name' in attrs:
                attrs['identity'] = attrs['name']
            elif 'identity' in attrs:
                attrs['name'] = attrs['identity']
            else:
                identity = "%s%d"%(entity_type, count)
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
                    raise schema.ValidationError, "Loading '%s', %s: %s"%(source, ex_type.__name__, ex_value), ex_trace
        else:
            sections = self._parse(source);
            # Add missing singleton sections
            for et in self.schema.entity_types.itervalues():
                if et.singleton and not [s for s in sections if s[0] == et.name]:
                    sections.append((et.name, {}))
            sections = self._expand(sections)
            sections = self._default_ids(sections)
            self[:] = [Entity(type=s[0], **s[1]) for s in sections]
            self.validate(self.schema)

    def section_count(self, section):
        return len(self.get(type=section))

    def entity(self, section, index):
        return self.get(type=section)[index]

    def value(self, section, index, key, convert=lambda x: x):
        """
        @return: Value at section, index, key or None if absent.
        @param as_type: A callable to convert the result to some type.
        """
        entities = self.get(type=section)
        if len(entities) <= index or key not in entities[index]:
            return None
        return convert(entities[index][key])


def configure_dispatch(dispatch, filename):
    """Called by C router code to load configuration file and do configuration"""
    qd = dispatch_c.instance()
    dispatch = qd.qd_dispatch_p(dispatch)
    config = Config(filename)
    # Configure any DEFAULT log entities first so we can report errors in non-
    # default log configurations to the correct place.
    for l in config.log:
        if l.module.upper() == 'DEFAULT': qd.qd_log_entity(l)
    for l in config.log:
        if l.module.upper() != 'DEFAULT': qd.qd_log_entity(l)
    qd.qd_dispatch_configure_container(dispatch, config.container[0])
    qd.qd_dispatch_configure_router(dispatch, config.router[0])
    qd.qd_dispatch_prepare(dispatch)
    # Note must configure addresses, waypoints, listeners and connectors after qd_dispatch_prepare
    for a in config.get(type='fixed-address'): qd.qd_dispatch_configure_address(dispatch, a)
    for w in config.waypoint: qd.qd_dispatch_configure_waypoint(dispatch, w)
    for l in config.listener: qd.qd_dispatch_configure_listener(dispatch, l)
    for c in config.connector: qd.qd_dispatch_configure_connector(dispatch, c)
    qd.qd_connection_manager_start(dispatch);
    qd.qd_waypoint_activate_all(dispatch);

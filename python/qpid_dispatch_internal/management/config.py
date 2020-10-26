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
from __future__ import unicode_literals
from __future__ import division
from __future__ import absolute_import
from __future__ import print_function


import json, re, sys
import os
import traceback
from copy import copy
from qpid_dispatch.management.entity import camelcase

from ..dispatch import QdDll
from .qdrouter import QdSchema
from qpid_dispatch_internal.compat import dict_itervalues
from qpid_dispatch_internal.compat import dict_iteritems
from qpid_dispatch_internal.compat import PY_STRING_TYPE
from qpid_dispatch_internal.compat import PY_TEXT_TYPE

try:
    from ..dispatch import LogAdapter, LOG_WARNING, LOG_ERROR
    _log_imported = True
except ImportError:
    # unit test cannot import since LogAdapter not set up
    _log_imported = False


class Config(object):
    """Load config entities from qdrouterd.conf and validated against L{QdSchema}."""

    def __init__(self, filename=None, schema=QdSchema(), raw_json=False):
        self.schema = schema
        self.config_types = [et for et in dict_itervalues(schema.entity_types)
                             if schema.is_configuration(et)]
        self._log_adapter = LogAdapter("AGENT") if _log_imported else None

        if filename:
            try:
                self.load(filename, raw_json)
            except Exception as e:
                raise Exception("Cannot load configuration file %s: %s"
                                % (filename, e))
        else:
            self.entities = []

    def _log(self, level, text):
        if self._log_adapter is not None:
            info = traceback.extract_stack(limit=2)[0] # Caller frame info
            self._log_adapter.log(level, text, info[0], info[1])

    @staticmethod
    def transform_sections(sections):
        for s in sections:
            s[0] = camelcase(s[0])
            s[1] = dict((camelcase(k), v) for k, v in dict_iteritems(s[1]))
            if s[0] == "address":
                s[0] = "router.config.address"
            if s[0] == "linkRoute":
                s[0] = "router.config.linkRoute"
            if s[0] == "autoLink":
                s[0] = "router.config.autoLink"
            if s[0] == "exchange":
                s[0] = "router.config.exchange"
            if s[0] == "binding":
                s[0] = "router.config.binding"

    def _parse(self, lines):
        """
        Parse config file format into a section list

        The config file format is a text file in JSON-ish syntax.  It allows
        the user to define a set of Entities which contain Attributes.
        Attributes may be either a single item or a map of nested attributes.

        Entities and map Attributes start with a single open brace on a line by
        itself (no non-comment text after the opening brace!)

        Entities and map Attributes are terminated by a single closing brace
        that appears on a line by itself (no trailing comma and no non-comment
        trailing text!)

        Entity names and Attribute names and items are NOT enclosed in quotes
        nor are they terminated with commas, however some select Attributes
        have values which are expected to be valid JSON (double quoted
        strings, etc)

        Unlike JSON the config file also allows comments.  A comment begins
        with the '#' character and is terminated at the end of line.
        """

        # note: these regexes expect that trailing comment and leading and
        # trailing whitespace has been removed
        #
        entity = re.compile(r'([\w-]+)[ \t]*{[ \t]*$')                    # WORD {
        attr_map = re.compile(r'([\$]*[\w-]+)[ \t]*:[ \t]*{[ \t]*$')      # WORD: {
        json_map = re.compile(r'("[\$]*[\w-]+)"[ \t]*:[ \t]*{[ \t]*$')    # "WORD": {
        attr_item = re.compile(r'([\w-]+)[ \t]*:[ \t]*([^ \t{]+.*)$')     # WORD1: VALUE
        end = re.compile(r'^}$')                                          # } (only)
        json_end = re.compile(r'}$')                                      # } (at eol)

        # The 'pattern:' and 'bindingKey:' attributes in the schema are special
        # snowflakes. They allow '#' characters in their value, so they cannot
        # be treated as comment delimiters
        special_snowflakes = ['pattern', 'bindingKey', 'hostname']
        hash_ok = re.compile(r'([\w-]+)[ \t]*:[ \t]*([\S]+).*')

        # the 'openProperties' and 'groups' attributes are also special
        # snowflakes in that their value is expected to be valid JSON.  These
        # values do allow single line comments which are stripped out, but the
        # remaining content is expected to be valid JSON.
        json_snowflakes = ['openProperties', 'groups']

        self._line_num = 1
        self._child_level = 0
        self._in_json = False

        def sub(line):
            """Do substitutions to make line json-friendly"""
            line = line.strip()

            # ignore empty and comment lines
            if not line or line.startswith("#"):
                self._line_num += 1
                return ""

            # watch JSON for embedded maps and map terminations
            # always pass JSON as-is except appending a comma at the end
            if self._in_json:
                if json_map.search(line):
                    self._child_level += 1
                if json_end.search(line):
                    self._child_level -= 1
                    if self._child_level == 0:
                        self._in_json = False
                        line = re.sub(json_end, r'},', line)
                self._line_num += 1
                return line

            # filter off pattern items before stripping comments
            if attr_item.search(line):
                if re.sub(attr_item, r'\1', line) in special_snowflakes:
                    self._line_num += 1
                    return re.sub(hash_ok, r'"\1": "\2",', line)

            # now trim trailing comment
            line = line.split('#')[0].strip()

            if entity.search(line):
                # WORD {  --> ["WORD", {
                line = re.sub(entity, r'["\1", {', line)
            elif attr_map.search(line):
                # WORD: {  --> ["WORD": {
                key = re.sub(attr_map, r'\1', line)
                line = re.sub(attr_map, r'"\1": {', line)
                self._child_level += 1
                if key in json_snowflakes:
                    self._in_json = True
            elif attr_item.search(line):
                # WORD: VALUE --> "WORD": "VALUE"
                line = re.sub(attr_item, r'"\1": "\2",', line)
            elif end.search(line):
                # }  --> "}," or "}]," depending on nesting level
                if self._child_level > 0:
                    line = re.sub(end, r'},', line)
                    self._child_level -= 1
                else:
                    # end top level entity list item
                    line = re.sub(end, r'}],', line)
            else:
                # unexpected syntax, let json parser figure it out
                self._log(LOG_WARNING,
                          "Invalid config file syntax (line %d):\n"
                          ">>> %s"
                          % (self._line_num, line))
            self._line_num += 1
            return line

        js_text = "[%s]"%("\n".join([sub(l) for l in lines]))
        if self._in_json or self._child_level != 0:
            self._log(LOG_WARNING,
                      "Configuration file: invalid entity nesting detected.")
        spare_comma = re.compile(r',\s*([]}])') # Strip spare commas
        js_text = re.sub(spare_comma, r'\1', js_text)
        # Convert dictionary keys to camelCase
        try:
            sections = json.loads(js_text)
        except Exception as e:
            self.dump_json("Contents of failed config file", js_text)
            raise
        Config.transform_sections(sections)
        return sections

    def _parserawjson(self, lines):
        """Parse raw json config file format into a section list"""
        def sub(line):
            # ignore comment lines that start with "[whitespace] #"
            line = "" if line.strip().startswith('#') else line
            return line
        js_text = "%s"%("\n".join([sub(l) for l in lines]))
        try:
            sections = json.loads(js_text)
        except Exception as e:
            self.dump_json("Contents of failed json-format config file", js_text)
            raise
        Config.transform_sections(sections)
        return sections

    def get_config_types(self):
        return self.config_types

    def load(self, source, raw_json=False):
        """
        Load a configuration file.
        @param source: A file name, open file object or iterable list of lines
        @param raw_json: Source is pure json not needing conf-style substitutions
        """
        if isinstance(source, (PY_STRING_TYPE, PY_TEXT_TYPE)):
            raw_json |= source.endswith(".json")
            with open(source) as f:
                self.load(f, raw_json)
        else:
            sections = self._parserawjson(source) if raw_json else self._parse(source)
            # Add missing singleton sections
            for et in self.get_config_types():
                if et.singleton and not et.deprecated and not [s for s in sections if s[0] == et.short_name]:
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

    def dump_json(self, title, js_text):
        # Function for config file parse failure logging.
        # js_text is the pre-processed config-format json string or the
        # raw json-format string that was presented to the json interpreter.
        # The logs generated here correlate exactly to the line, column,
        # and character numbers reported by json error exceptions.
        # For each line 'Column 1' immediately follows the vertical bar.
        self._log(LOG_ERROR, title)
        lines = js_text.split("\n")
        for idx in range(len(lines)):
            self._log(LOG_ERROR, "Line %d |%s" % (idx + 1, lines[idx]))

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

    # NOTE: Can't import agent until dispatch C extension module is initialized.
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

    # Configure and prepare the router before we can activate the agent.
    configure(config.by_type('router')[0])
    qd.qd_dispatch_prepare(dispatch)
    qd.qd_router_setup_late(dispatch) # Actions requiring active management agent.
    agent.activate("$_management_internal")

    from qpid_dispatch_internal.display_name.display_name import DisplayNameService
    displayname_service = DisplayNameService()
    qd.qd_dispatch_register_display_name_service(dispatch, displayname_service)

    # Configure policy and policy manager before vhosts
    policyDir           = config.by_type('policy')[0]['policyDir']
    policyDefaultVhost  = config.by_type('policy')[0]['defaultVhost']
    useHostnamePatterns = config.by_type('policy')[0]['enableVhostNamePatterns']
    maxMessageSize      = config.by_type('policy')[0]['maxMessageSize']
    for a in config.by_type("policy"):
        configure(a)
    agent.policy.set_default_vhost(policyDefaultVhost)
    agent.policy.set_use_hostname_patterns(useHostnamePatterns)
    agent.policy.set_max_message_size(maxMessageSize)

    # Configure a block of types
    for t in "sslProfile", "authServicePlugin", \
             "router.config.address", "router.config.linkRoute", "router.config.autoLink", \
             "router.config.exchange", "router.config.binding", \
             "vhost":
        for a in config.by_type(t):
            configure(a)
            if t == "sslProfile":
                display_file_name = a.get('uidNameMappingFile')
                if display_file_name:
                    ssl_profile_name = a.get('name')
                    displayname_service.add(ssl_profile_name, display_file_name)

    # Configure remaining types except for connector and listener
    for e in config.entities:
        if not e['type'] in ['org.apache.qpid.dispatch.connector', 'org.apache.qpid.dispatch.listener']:
            configure(e)

    # Load the vhosts from the .json files in policyDir
    # Only vhosts are loaded. Other entities in these files are silently discarded.
    if not policyDir == '':
        apath = os.path.abspath(policyDir)
        for i in os.listdir(policyDir):
            if i.endswith(".json"):
                pconfig = PolicyConfig(os.path.join(apath, i))
                for a in pconfig.by_type("vhost"):
                    agent.configure(a)

    # Static configuration is loaded except for connectors and listeners.
    # Configuring connectors and listeners last starts inter-router and user messages
    # when the router is in a known and repeatable initial configuration state.
    for t in "connector", "listener":
        for a in config.by_type(t):
            configure(a)

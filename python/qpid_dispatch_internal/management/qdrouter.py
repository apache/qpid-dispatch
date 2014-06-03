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
Qpid Dispatch Router management schema and config file parsing.
"""

import schema, json, re
from entity import EntityList
from copy import copy

class Schema(schema.Schema):
    """
    Qpid Dispatch Router management schema.
    """
    SCHEMA_FILE = schema.schema_file("qdrouter.json")

    def __init__(self):
        """Load schema."""
        with open(self.SCHEMA_FILE) as f:
            schema.Schema.__init__(self, **json.load(f))

    def validate(self, entities, **kwargs):
        """
        In addition to L{schema.Schema.validate}, check the following:

        If the operating mode of the router is not 'interior', then the only
        permitted roles for listeners and connectors is 'normal'.

        @param entities: An L{EntityList}
        @param kwargs: See L{schema.Schema.validate}
        """
        schema.Schema.validate(self, entities, **kwargs)

        if entities.router.mode != 'interior':
            for connect in entities.get(entity_type='listeners') + entities.get(entity_type='connector'):
                if connect['role'] != 'normal':
                    raise schema.SchemaError("Role '%s' for entity '%s' only permitted with 'interior' mode % (entity['role'], connect.name)")

class Configuration(EntityList):
    """An L{EntityList} loaded from a qdrouterd.conf and validated against L{Schema}."""

    def __init__(self, schema=Schema()):
        super(Configuration, self).__init__(schema)

    @staticmethod
    def _parse(lines):
        """Parse config file format into a section list"""
        begin = re.compile(r'([\w-]+)[ \t]*{') # WORD {
        end = re.compile(r'}')                 # }
        attr = re.compile(r'([\w-]+)[ \t]*:[ \t]*([\w-]+)') # WORD1: WORD2

        def sub(line):
            """Do substitutions to make line json-friendly"""
            line = line.split('#')[0].strip() # Strip comments
            line = re.sub(begin, r'["\1", {', line)
            line = re.sub(end, r'}],', line)
            line = re.sub(attr, r'"\1": "\2",', line)
            return line

        js_text = "[%s]"%("".join([sub(l) for l in lines]))
        spare_comma = re.compile(r',\s*([]}])') # Strip spare commas
        return json.loads(re.sub(spare_comma, r'\1', js_text))

    def _expand(self, content):
        """
        Find include sections (defined by schema) in the content,
        expand references and remove the include sections.
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

    def load(self, lines):
        """
        Load a configuration file.
        @param lines: A list of lines, or an open file object.
        """
        self.replace(self._default_ids(self._expand(self._parse(lines))))
        self.validate()

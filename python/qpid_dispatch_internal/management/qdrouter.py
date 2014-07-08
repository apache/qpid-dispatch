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

import json, re, sys
import schema
from entity import EntityList, Entity, OrderedDict
from copy import copy


class QdSchema(schema.Schema):
    """
    Qpid Dispatch Router management schema.
    """
    SCHEMA_FILE = schema.schema_file("qdrouter.json")

    def __init__(self):
        """Load schema."""
        with open(self.SCHEMA_FILE) as f:
            super(QdSchema, self).__init__(**json.load(f, object_pairs_hook=OrderedDict))

    def validate(self, entities, **kwargs):
        """
        In addition to L{schema.Schema.validate}, check the following:

        If the operating mode of the router is not 'interior', then the only
        permitted roles for listeners and connectors is 'normal'.

        @param entities: An L{EntityList}
        @param kwargs: See L{schema.Schema.validate}
        """
        super(QdSchema, self).validate(entities, **kwargs)

        if entities.router[0].mode != 'interior':
            for connect in entities.get(entity_type='listeners') + entities.get(entity_type='connector'):
                if connect['role'] != 'normal':
                    raise schema.ValidationError("Role '%s' for entity '%s' only permitted with 'interior' mode % (entity['role'], connect.name)")


class QdConfig(EntityList):
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
        return json.loads(js_text, object_pairs_hook=OrderedDict)

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
                    raise ex_type, "Loading '%s': %s"%(source, ex_value), ex_trace
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

    def value(self, section, index, key, convert=lambda x: x):
        """
        @return: Value at section, index, key or None if absent.
        @param as_type: A callable to convert the result to some type.
        """
        entities = self.get(type=section)
        if len(entities) <= index or key not in entities[index]:
            return None
        return convert(entities[index][key])

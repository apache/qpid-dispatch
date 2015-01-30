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

"""Library for generating markdown documentation from a L{schema.Schema}"""

from collections import namedtuple
import sys, re
from .schema import quotestr


class SchemaWriter(object):
    """Write the schema as a markdown document"""

    def __init__(self, output, schema, quiet=True):
        self.output, self.schema, self.quiet = output, schema, quiet
        # Options affecting how output is written

    def write(self, what):
        self.output.write(what)

    def warn(self, message):
        if not self.quiet: print >>sys.stderr, message

    def attribute_type(self, attr, holder, show_create=True, show_update=True):
        default = attr.default
        if isinstance(default, basestring) and default.startswith('$'):
            default = None  # Don't show defaults that are references, confusing.

        self.write('\n*%s* '%(attr.name))
        self.write('(%s)\n'%(', '.join(
            filter(None, [str(attr.atype),
                          default and "default=%s" % quotestr(default),
                          attr.required and "required",
                          attr.unique and "unique",
                          show_create and attr.create and "`CREATE`",
                          show_update and attr.update and "`UPDATE`"
                      ]))))
        if attr.description:
            self.write(":   %s\n" % attr.description)
        else:
            self.warn("Warning: No description for %s in %s" % (attr, attr.defined_in.short_name))

    def holder_preface(self, holder):
        self.write('\n### %s\n' % holder.short_name)
        if holder.description:
            self.write('\n%s\n' % holder.description)
        else:
            self.warn("Warning no description for %s" % entity_type)

    def attribute_types(self, holder):
        for attr in holder.my_attributes:
            self.attribute_type(attr, holder)

    def entity_type(self, entity_type):
        self.holder_preface(entity_type)
        if entity_type.operations:
            self.write("\nOperations allowed: `%s`\n\n" % "`, `".join(entity_type.operations))
        for a in entity_type.annotations: self.attribute_types(a)
        self.attribute_types(entity_type)

    def entity_types_extending(self, base_name):
        base = self.schema.entity_type(base_name)
        for entity_type in self.schema.filter(lambda t: t.extends(base)):
            self.entity_type(entity_type)


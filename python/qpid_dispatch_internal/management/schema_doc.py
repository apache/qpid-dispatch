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

"""Library for generating asciidoc documentation from a L{schema.Schema}"""

from __future__ import unicode_literals
from __future__ import division
from __future__ import absolute_import
from __future__ import print_function

from collections import namedtuple
import sys
from .schema import AttributeType
from qpid_dispatch_internal.compat import PY_STRING_TYPE, dict_itervalues

class SchemaWriter(object):
    """Write the schema as an asciidoc document"""

    def __init__(self, output, schema, quiet=True):
        self.output, self.schema, self.quiet = output, schema, quiet
        self._heading = 0
        # Options affecting how output is written

    def warn(self, message):
        if not self.quiet:
            print(message, file=sys.stderr)

    def write(self, text): self.output.write(text)

    def writeln(self, text=""): self.output.write(text+"\n")

    def para(self, text): self.write(text+"\n\n")

    def heading(self, text=None, sub=0):
        self._heading += sub
        if text:
            self.para("\n=%s %s" % ("="*self._heading, text))

    class Section(namedtuple("Section", ["writer", "heading"])):
        def __enter__(self): self.writer.heading(self.heading, sub=+1)
        def __exit__(self, ex, value, trace): self.writer.heading(sub=-1)

    def section(self, heading): return self.Section(self, heading)

    def attribute_qualifiers(self, attr, show_create=True, show_update=True):
        default = attr.default
        if isinstance(default, PY_STRING_TYPE) and default.startswith('$'):
            default = None  # Don't show defaults that are references, confusing.
        return ' (%s)' % (', '.join(
            filter(None, [str(attr.atype),
                          default and "default='%s'" % default,
                          attr.required and "required",
                          attr.unique and "unique",
                          show_create and attr.create and "`CREATE`",
                          show_update and attr.update and "`UPDATE`"
                      ])))

    def attribute_type(self, attr, holder=None, show_create=True, show_update=True):
        self.writeln("'%s'%s::" % (
            attr.name, self.attribute_qualifiers(attr, show_create, show_update)))
        if attr.description:
            self.writeln("  %s" % attr.description)
        else:
            self.warn("Warning: No description for %s in %s" % (attr, attr.defined_in.short_name))
        self.writeln()

    def attribute_types(self, holder):
        holder_attributes = holder.my_attributes
        for attr in holder_attributes:
            if attr.deprecation_name:
                deprecated_attr = AttributeType(attr.deprecation_name, type=attr.type, defined_in=attr.defined_in,
                                                default=attr.default, required=attr.required, unique=attr.unique,
                                                hidden=attr.hidden, deprecated=True, value=attr.value,
                                                description="(DEPRECATED) " + attr.description
                                                            + " This attribute has been deprecated. Use " +
                                                            attr.name + " instead.",
                                                create=attr.create, update=attr.update,
                                                graph=attr.graph)
                holder_attributes.append(deprecated_attr)

        for attr in holder_attributes:
            self.attribute_type(attr, holder)

    def operation_def(self, op, holder):

        def request_response(what):
            message = getattr(op, what)
            if message:
                if message.body:
                    self.para(".%s body%s\n\n%s" % (
                        what.capitalize(), self.attribute_qualifiers(message.body),
                        message.body.description))
                if message.properties:
                    self.para(".%s properties" % (what.capitalize()))
                    for prop in dict_itervalues(message.properties):
                        self.attribute_type(prop)

        with self.section("Operation %s" % op.name):
            if op.description:
                self.para(op.description)
            request_response("request")
            request_response("response")

    def operation_defs(self, entity_type):
        for op in dict_itervalues(entity_type.operation_defs):
            self.operation_def(op, entity_type)

    def entity_type(self, entity_type, operation_defs=True):
        with self.section(entity_type.short_name):
            if entity_type.description:
                self.para('%s' % entity_type.description)
            else:
                self.warn("Warning no description for %s" % entity_type)
            if entity_type.operations:
                self.para("Operations allowed: `%s`\n\n" % "`, `".join(entity_type.operations))

            self.attribute_types(entity_type)
            if entity_type.operation_defs:
                self.operation_defs(entity_type)

    def entity_types_extending(self, base_name):
        base = self.schema.entity_type(base_name)
        for entity_type in self.schema.filter(lambda t: t.extends(base)):
            self.entity_type(entity_type)


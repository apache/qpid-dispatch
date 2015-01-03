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
Generate the schema.md makrdown documentation for management schema.
"""

import sys, re
from pkgutil import get_data
from qpid_dispatch_internal.management.qdrouter import QdSchema

class SchemaWriter(object):
    """Write the schema as a markdown document"""

    def __init__(self, out):
        self.out = out
        self.schema = QdSchema()


    def write(self, value):
        self.out.write(value)

    def warn(self, message):
        print >>sys.stderr, message

    def attribute(self, attr, thing):
        default = attr.default
        if isinstance(default, basestring) and default.startswith('$'):
            default = None  # Don't show defaults that are references, confusing.
        self.write('\n`%s` '%(attr.name))
        self.write('(%s)\n'%(', '.join(
            filter(None, [str(attr.atype),
                          attr.required and "required",
                          attr.unique and "unique",
                          default and "default=%s"%default]))))
        if attr.description:
            self.write(":   %s\n"%attr.description)
        else:
            self.warn("Warning: No description for %s in %s" % (attr, thing.short_name))

    def preface(self, thing):
        self.write('\n### `%s`\n' % thing.short_name)
        if thing.description:
            self.write('\n%s\n' % thing.description)
        else:
            self.warn("Warning no description for %s" % thing)

    def attributes(self, thing):
        for attr in thing.my_attributes:
            self.attribute(attr, thing)

    def annotation(self, annotation):
        self.preface(annotation)
        used_by = ["`%s`" % e.short_name
                   for e in self.schema.filter(lambda e: annotation in e.annotations)]
        if used_by:
            self.write('\nUsed by %s.\n'%(', '.join(used_by)))
        self.attributes(annotation)

    def summary(self, thing):
        return "`%s` (%s)" % (thing.short_name, ", ".join("`%s`" % a for a in thing.attributes))

    def entity_type(self, entity_type):
        self.preface(entity_type)
        if entity_type.base:
            self.write('\nExtends: %s.\n' % self.summary(entity_type.base))
        if entity_type.annotations:
            self.write('\nAnnotations: %s.\n' % (
                ', '.join([self.summary(a) for a in entity_type.annotations])))
        self.attributes(entity_type)

    def entity_types(self, base_name):
        base = self.schema.entity_type(base_name)
        self.entity_type(base)
        for entity_type in self.schema.filter(lambda t: t.extends(base)):
            self.entity_type(entity_type)

    def run(self):
        self.write(get_data('qpid_dispatch.management', 'qdrouter.json.readme.txt')) # Preface
        self.write("The rest of this section provides the schema documentation in readable format.\n")

        self.write("\n## Annotations\n")
        for annotation in self.schema.annotations.itervalues():
            self.annotation(annotation)

        self.write("\n## Base Entity Type\n")
        self.entity_type(self.schema.entity_type("entity"))

        self.write("\n## Configuration Entities\n")
        self.entity_types("configurationEntity")

        self.write("\n## Operational Entities\n")
        self.entity_types("operationalEntity")

def main():
    """Generate schema markdown documentation from L{QdSchema}"""
    SchemaWriter(sys.stdout).run()

if __name__ == '__main__':
    main()

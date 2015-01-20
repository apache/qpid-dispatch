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
Generate the schema.md chapter for the dispatch book from the qdrouter.json schema.
"""

import sys, re
from pkgutil import get_data
from qpid_dispatch_internal.management.qdrouter import QdSchema
from qpid_dispatch_internal.management.schema import quotestr

class SchemaWriter(object):
    """Write the schema as a markdown document"""

    def __init__(self, out, quiet=True):
        self.out = out
        self.schema = QdSchema()
        self.quiet = quiet


    def write(self, value):
        self.out.write(value)

    def warn(self, message):
        if not self.quiet: print >>sys.stderr, message

    def attribute(self, attr, thing):
        default = attr.default
        if isinstance(default, basestring) and default.startswith('$'):
            default = None  # Don't show defaults that are references, confusing.
        self.write('\n*%s* '%(attr.name))
        self.write('(%s)\n'%(', '.join(
            filter(None, [str(attr.atype),
                          attr.required and "required",
                          attr.unique and "unique",
                          default and "default=%s" % quotestr(default)]))))
        if attr.description:
            self.write(":   %s\n"%attr.description)
        else:
            self.warn("Warning: No description for %s in %s" % (attr, thing.short_name))

    def attributes(self, thing):
        for attr in thing.my_attributes:
            self.attribute(attr, thing)

    def preface(self, thing):
        self.write('\n### %s\n' % thing.short_name)
        if thing.description:
            self.write('\n%s\n' % thing.description)
        else:
            self.warn("Warning no description for %s" % entity_type)

    def entity_type(self, entity_type):
        self.preface(entity_type)
        for a in entity_type.annotations: self.attributes(a)
        self.attributes(entity_type)
        ops = entity_type.operations
        if entity_type.singleton: ops.remove('CREATE')
        if ops:
            self.write("\nOperations allowed: %s\n\n" % ", ".join(entity_type.operations))

    def entity_types(self, base_name):
        base = self.schema.entity_type(base_name)
        for entity_type in self.schema.filter(lambda t: t.extends(base)):
            self.entity_type(entity_type)

    def run(self):
        self.write("""
# Management Schema

This chapter documents the set of *management entity types* that define configuration and
management of a Dispatch Router.

All management entity types have the following attributes:

- *type*: The fully qualified type of the entity,
  e.g. `org.apache.qpid.dispatch.router`. In this documentation and when using
  dispatch tools you can use the short name of the type, e.g. `router`

- *identity*: A system-generated identity of the entity. It includes
  the short type name and some identifying information. E.g. `log/AGENT` or
  `listener/localhost:amqp`

There are two kinds of management entity type.

- *Configuration* Entities: Parameters that can be set in the configuration file
(see `qdrouterd.conf(5)` man page) or set at run-time with the `qdmanage(8)`
tool.

- *Operational* Entities: Run-time status values that can be queried using `qdstat(8)` or
`qdmanage(8)` tools.


""")

        self.write("""## Configuration Entities

Configuration entities define the attributes allowed in the configuration file
(see `qdrouterd.conf(5)`) but you can also create entities once the router is
running using the `qdrouterd(8)` tool's `create` operation. Some entities can also
be modified using the `update` operation, see the entity descriptions below.

""")
        self.entity_types("configurationEntity")

        self.write("""\n## Operational Entities

Operational entities provide statistics and other run-time attributes of the router.
The `qdstat(8)` tool provides a convenient way to query run-time statistics.
You can also use the general-purpose management tool `qdmanage(8)` to query 
operational attributes.
""")
        self.entity_types("operationalEntity")

def main():
    """Generate schema markdown documentation from L{QdSchema}"""
    SchemaWriter(sys.stdout).run()

if __name__ == '__main__':
    main()

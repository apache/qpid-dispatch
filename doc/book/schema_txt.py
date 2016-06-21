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
from qpid_dispatch_internal.management.qdrouter import QdSchema
from qpid_dispatch_internal.management.schema_doc import SchemaWriter

class BookSchemaWriter(SchemaWriter):

    def run(self):
        self.heading("Management Schema")
        self.writeln("""
This chapter documents the set of *management entity types* that define
configuration and management of a Dispatch Router. A management entity type has
a set of *attributes* that can be read, some attributes can also be
updated. Some entity types also support *operations* that can be called.

All management entity types have the following standard attributes:

type::
  The fully qualified type of the entity,
  e.g. `org.apache.qpid.dispatch.router`. This document uses the short name
  without the `org.apache.qpid.dispatch` prefix e.g. `router`. The dispatch
  tools will accept the short or long name.

name::
  A user-generated identity for the entity.  This can be used in other entities
  that need to refer to the named entity.

identity::
  A system-generated identity of the entity. It includes
  the short type name and some identifying information. E.g. `log/AGENT` or
  `listener/localhost:amqp`

There are two main categories of management entity type.

Configuration Entities::
  Parameters that can be set in the configuration file
  (see `qdrouterd.conf(5)` man page) or set at run-time with the `qdmanage(8)`
  tool.

Operational Entities::
   Run-time status values that can be queried using `qdstat(8)` or `qdmanage(8)` tools.
""")

        with self.section("Configuration Entities"):
            self.writeln("""
Configuration entities define the attributes allowed in the configuration file
(see `qdrouterd.conf(5)`) but you can also create entities once the router is
running using the `qdrouterd(8)` tool's `create` operation. Some entities can also
be modified using the `update` operation, see the entity descriptions below.
""")
            self.entity_types_extending("configurationEntity")

        with self.section("Operational Entities"):

            self.writeln("""
Operational entities provide statistics and other run-time attributes of the router.
The `qdstat(8)` tool provides a convenient way to query run-time statistics.
You can also use the general-purpose management tool `qdmanage(8)` to query
operational attributes.
""")
            self.entity_types_extending("operationalEntity")

        with self.section("Management Operations"):
            self.writeln("""
The 'qdstat(8)' and 'qdmanage(8)' tools allow you to view or modify management entity
attributes. They work by invoking *management operations*. You can invoke these operations
from any AMQP client by sending a message with the appropriate properties and body to the
'$management' address. The message should have a 'reply-to' address indicating where the
response should be sent.
""")
            def operation_section(title, entity_type):
                with self.section(title):
                    self.operation_defs(entity_type)
            operation_section("Operations for all entity types", self.schema.entity_type("entity"))
            for e in self.schema.filter(lambda et: et.operation_defs and not et.name_is("entity")):
                operation_section("Operations for '%s' entity type" % e.short_name, e)

def main():
    """Generate schema markdown documentation from L{QdSchema}"""
    BookSchemaWriter(sys.stdout, QdSchema()).run()

if __name__ == '__main__':
    main()

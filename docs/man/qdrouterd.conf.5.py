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

"""Generate the qdrouterd.conf. man page from the qdrouterd management schema."""

from __future__ import unicode_literals
from __future__ import division
from __future__ import absolute_import
from __future__ import print_function

import sys
from qpid_dispatch_internal.management.qdrouter import QdSchema
from qpid_dispatch_internal.management.schema_doc import SchemaWriter

CONNECTOR = 'org.apache.qpid.dispatch.connector'
LISTENER = 'org.apache.qpid.dispatch.listener'

# avoid writing these config entity types to the man file, they are not allowed
# in the configuration file and are only supported at run time via management
#
TYPE_FILTER = ['router.connection.linkRoute']


class ManPageWriter(SchemaWriter):

    def __init__(self):
        super(ManPageWriter, self).__init__(sys.stdout, QdSchema())

    def attribute_type(self, attr, holder):
        # Don't show read-only attributes
        if not attr.create and not attr.update:
            # It is ok to show the console and log attributes
            if not holder.short_name == "console" and not holder.short_name == "log":
                return
        super(ManPageWriter, self).attribute_type(attr, holder, show_create=False, show_update=False)

    def man_page(self):
        self.writeln(r"""
qdrouterd.conf(5)
=================
:doctype: manpage

NAME
----
qdrouterd.conf - configuration file for the dispatch router.

SYNOPSIS
--------
Provides the initial configuration when 'qdrouterd(8)' starts. The configuration
of a running router can be modified using 'qdmanage(8)'.


DESCRIPTION
-----------

The configuration file is made up of sections with this syntax:

----
sectionName {
    attributeName: attributeValue
    attributeName: attributeValue
    ...
}
----

For example you can define a router using the 'router' section

----
router {
    mode: standalone
    id: Router.A
    ...
}
----

or define a listener using the 'listener' section

----
listener {
    host: 0.0.0.0
    port: 20102
    saslMechanisms: ANONYMOUS
    ...
}
----

or define a connector using the 'connector' section

----
connector {
    role: inter-router
    host: 0.0.0.0
    port: 20003
    saslMechanisms: ANONYMOUS
    ...
}
----

An 'sslProfile' section with SSL credentials can be included in multiple 'listener' or 'connector' entities. Here's an example, note
how the 'sslProfile' attribute of 'listener' sections references the 'name'
attribute of 'sslProfile' sections.

----
sslProfile {
    name: my-ssl
    caCertFile: ca-certificate-1.pem
    certFile: server-certificate-1.pem
    privateKeyFile: server-private-key.pem
}

listener {
    sslProfile: my-ssl
    host: 0.0.0.0
    port: 20102
    saslMechanisms: ANONYMOUS
}
----
""")

        with self.section("Configuration Sections"):

            config = self.schema.entity_type("configurationEntity")
            for entity_type in self.schema.entity_types.values():
                if entity_type.short_name in TYPE_FILTER:
                    continue
                if config in entity_type.all_bases:
                    with self.section(entity_type.short_name):
                        if entity_type.description:
                            self.para(entity_type.description)
                        self.attribute_types(entity_type)

        self.writeln("""
SEE ALSO
--------

*qdrouterd(8)*, *qdmanage(8)*

http://qpid.apache.org/components/dispatch-router
""")


if __name__ == '__main__':
    ManPageWriter().man_page()

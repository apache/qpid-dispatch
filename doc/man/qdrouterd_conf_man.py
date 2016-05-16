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
Generate the qdrouterd.conf.md man page from the qdrouterd management schema.
"""

import sys
from qpid_dispatch_internal.management.qdrouter import QdSchema
from qpid_dispatch_internal.management.schema_doc import SchemaWriter

class ManPageWriter(SchemaWriter):

    def __init__(self):
        super(ManPageWriter, self).__init__(sys.stdout, QdSchema())

    def attribute_type(self, attr, holder):
        # Don't show read-only attributes
        if not attr.create and not attr.update: return
        super(ManPageWriter, self).attribute_type(attr, holder, show_create=False, show_update=False)

    def man_page(self):
        self.writeln(r"""
:orphan:

qdrouterd.conf manual page
==========================

Synopsis
--------

qdroutered.conf is the configuration file for the dispatch router.

Description
-----------

The configuration file is made up of sections with this syntax:

::

    SECTION-NAME {
        ATTRIBUTE-NAME: ATTRIBUTE-VALUE
        ATTRIBUTE-NAME: ATTRIBUTE-VALUE
        ...
    }

There are two types of sections:

*Configuration sections* correspond to configuration entities. They can be queried and
configured via management tools as well as via the configuration file.

*Annotation sections* define a group of attribute values that can be included in
one or more entity sections.

For example you can define an "ssl-profile" annotation section with SSL credentials
that can be included in multiple "listener" entities. Here's an example, note
how the 'ssl-profile' attribute of 'listener' sections references the 'name'
attribute of 'ssl-profile' sections.

::

    ssl-profile {
        name: ssl-profile-one
        cert-db: ca-certificate-1.pem
        cert-file: server-certificate-1.pem
        key-file: server-private-key.pem
    }

    listener {
        ssl-profile: ssl-profile-one
        host: 0.0.0.0
        port: 20102
        sasl-mechanisms: ANONYMOUS
    }
""")

        with self.section("Annotation Sections"):
            for annotation in self.schema.annotations.itervalues():
                used_by = [e.short_name for e in self.schema.entity_types.itervalues()
                           if annotation in e.annotations]
                with self.section(annotation.short_name):
                    if annotation.description: self.para(annotation.description)
                    if used_by: self.para('Used by: **%s**.'%('**, **'.join(used_by)))
                    self.attribute_types(annotation)

        with self.section("Configuration Sections"):
            config = self.schema.entity_type("configurationEntity")
            for entity_type in self.schema.entity_types.itervalues():
                if config in entity_type.all_bases:
                    with self.section(entity_type.short_name):
                        if entity_type.description: self.para(entity_type.description)
                        if entity_type.annotations:
                            self.para('Annotations: **%s**.'%('**, **'.join(
                                [a.short_name for a in entity_type.annotations])))
                            for a in entity_type.annotations: self.attribute_types(a)
                        self.attribute_types(entity_type)

        self.writeln("""
See also
--------

*qdrouterd(8)*, *qdmanage(8)*

http://qpid.apache.org/components/dispatch-router
        """)

if __name__ == '__main__':
    ManPageWriter().man_page()

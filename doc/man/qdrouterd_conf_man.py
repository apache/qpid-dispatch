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
from qpid_dispatch_internal.management.markdown import SchemaWriter

class ManPageWriter(SchemaWriter):

    def __init__(self, filename):
        super(ManPageWriter, self).__init__(open(filename, 'w'), QdSchema())

    def attribute_type(self, attr, holder):
        # Don't repeat annotationd attributes or show non-create attributes.
        if (attr.annotation and attr.annotation != holder) or not attr.create:
            return
        super(ManPageWriter, self).attribute_type(attr, holder, show_create=False, show_update=False)

    def man_page(self):
        self.write(r"""
# Name

qdrouterd.conf - Configuration file for the Qpid Dispatch router

# Description

The configuration file is made up of sections with this syntax:

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

    ssl-profile {
        name: ssl-profile-one
        cert-db: ca-certificate-1.pem
        cert-file: server-certificate-1.pem
        key-file: server-private-key.pem
    }

    listener {
        ssl-profile: ssl-profile-one
        addr: 0.0.0.0
        port: 20102
        sasl-mechanisms: ANONYMOUS
    }
""")

        self.write("\n\n# Annotation Sections\n\n")
        for annotation in self.schema.annotations.itervalues():
            used_by = [e.short_name for e in self.schema.entity_types.itervalues()
                       if annotation in e.annotations]
            self.write('\n\n## %s\n'%annotation.short_name)
            if used_by: self.write('Used by: **%s**.\n'%('**, **'.join(used_by)))
            self.attribute_types(annotation)

        self.write("\n\n# Configuration Sections\n\n")
        config = self.schema.entity_type("configurationEntity")
        for entity_type in self.schema.entity_types.itervalues():
            if config in entity_type.all_bases:
                self.write('\n## %s\n'% entity_type.short_name)
                if entity_type.annotations:
                    self.write('Annotations: **%s**.\n'%('**, **'.join(
                        [a.short_name for a in entity_type.annotations])))
                self.attribute_types(entity_type)

if __name__ == '__main__':
    ManPageWriter(sys.argv[1]).man_page()

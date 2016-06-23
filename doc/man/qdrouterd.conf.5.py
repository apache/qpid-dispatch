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

"""
Generate the qdrouterd.conf. man page from the qdrouterd management schema.
"""

import sys
from qpid_dispatch_internal.management.qdrouter import QdSchema
from qpid_dispatch_internal.management.schema_doc import SchemaWriter
from qpid_dispatch_internal.management.schema import AttributeType

from qpid_dispatch_internal.compat import OrderedDict

CONNECTOR = 'org.apache.qpid.dispatch.connector'
LISTENER = 'org.apache.qpid.dispatch.listener'

class ManPageWriter(SchemaWriter):

    def __init__(self):
        super(ManPageWriter, self).__init__(sys.stdout, QdSchema())
        self.sslProfileAttributes = []
        self.connectionRoleAddrPortAttrs = []
        self.connectionRoleAddrPortAnnotations = {}

    def attribute_type(self, attr, holder):
        # Don't show read-only attributes
        if not attr.create and not attr.update:
            # It is ok to show the console attributes
            if not holder.short_name == "console":
                return
        super(ManPageWriter, self).attribute_type(attr, holder, show_create=False, show_update=False)

    def is_entity_connector_or_listener(self, entity_type):
        if CONNECTOR == entity_type.name or LISTENER == entity_type.name:
            return True
        return False

    def add_connector_listener_attributes(self, entity_type):

        attr_type = AttributeType('name', type="string", defined_in=entity_type, create=True, update=True,
                                  description="Unique name optionally assigned by user. Can be changed.")
        entity_type.attributes['name'] = attr_type
        self.connectionRoleAddrPortAttrs.append('name')

        # We will have to add the connectionRole and addrPort attributes to listener and connector entities
        # so that they show up in the man doc page.
        for attr in self.connectionRoleAddrPortAnnotations.keys():
            annotation = self.connectionRoleAddrPortAnnotations.get(attr)

            for key in annotation.attributes.keys():
                self.connectionRoleAddrPortAttrs.append(key)
                annotation_attr = annotation.attributes.get(key)
                if not annotation_attr.deprecated:
                    attr_type = AttributeType(key, type=annotation_attr.type,
                                              defined_in=entity_type,
                                              create=True, update=True, description=annotation_attr.description)
                    entity_type.attributes[key] = attr_type

        # Artificially add an sslProfile
        ssl_profile_attr = AttributeType("sslProfile", type="string", defined_in=entity_type,
                                         create=True, update=True, description="name of the sslProfile ")

        entity_type.attributes[u'sslProfile'] = ssl_profile_attr

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
    certDb: ca-certificate-1.pem
    certFile: server-certificate-1.pem
    keyFile: server-private-key.pem
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
            for annotation in self.schema.annotations.itervalues():
                # We are skipping connectionRole and addrPort annotations from the doc because it is
                # confusing to the user
                if "addrPort" in annotation.name or "connectionRole" in annotation.name:
                    self.connectionRoleAddrPortAnnotations[annotation.short_name] = annotation
                    continue
                used_by = [e.short_name for e in self.schema.entity_types.itervalues()
                           if annotation in e.annotations]
                with self.section(annotation.short_name):
                    if annotation.description:
                        self.para(annotation.description)
                    if used_by:
                        self.para('Used by: **%s**.'%('**, **'.join(used_by)))

                    if "sslProfile" in annotation.name:
                        # sslProfileName is an internal attribute and should not show in the doc
                        del annotation.attributes[u'sslProfileName']

                        for attribute in annotation.attributes.keys():
                            self.sslProfileAttributes.append(attribute)

                        attrs = annotation.attributes

                        annotation.attributes = OrderedDict()
                        # The name has to appear first in the doc, the other attributes appear after name
                        name_attr = AttributeType("name", type="string", defined_in=annotation,
                                                  create=True, update=True, description="name of the sslProfile ")
                        annotation.attributes[u'name'] = name_attr
                        annotation.attributes.update(attrs)

                    self.attribute_types(annotation)

            config = self.schema.entity_type("configurationEntity")
            for entity_type in self.schema.entity_types.itervalues():
                if self.is_entity_connector_or_listener(entity_type):
                    for sslProfileAttribute in self.sslProfileAttributes:
                        del entity_type.attributes[sslProfileAttribute]
                    current_attrs = entity_type.attributes

                    entity_type.attributes = OrderedDict()
                    # The attributes from the annotations must appear first in the doc
                    self.add_connector_listener_attributes(entity_type)

                    for attr in self.connectionRoleAddrPortAttrs:
                        if current_attrs.get(attr):
                            del current_attrs[attr]
                    entity_type.attributes.update(current_attrs)

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

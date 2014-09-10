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
Generate the qdrouterd.conf man page from the qdrouterd management schema."""

import sys
from qpid_dispatch_internal.management.qdrouter import QdSchema

def make_man_page(filename):
    """Generate a man page for the configuration file from L{QdSchema} descriptions"""
    with open(filename, 'w') as f:

        f.write(
r""".\" -*- nroff -*-
.\"
.\" Licensed to the Apache Software Foundation (ASF) under one
.\" or more contributor license agreements.  See the NOTICE file
.\" distributed with this work for additional information
.\" regarding copyright ownership.  The ASF licenses this file
.\" to you under the Apache License, Version 2.0 (the
.\" "License"); you may not use this file except in compliance
.\" with the License.  You may obtain a copy of the License at
.\"
.\"   http://www.apache.org/licenses/LICENSE-2.0
.\"
.\" Unless required by applicable law or agreed to in writing,
.\" software distributed under the License is distributed on an
.\" "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
.\" KIND, either express or implied.  See the License for the
.\" specific language governing permissions and limitations
.\" under the License
.\"
.TH QDROUTERD.CONF 5
.SH NAME
qdrouterd.conf \- Configuration file for the Qpid Dispatch router
.SH DESCRIPTION

The configuration file is made up of sections with this syntax:

.nf
SECTION-NAME {
    ATTRIBUTE-NAME: ATTRIBUTE-VALUE
    ATTRIBUTE-NAME: ATTRIBUTE-VALUE
    ...
}
.fi

There are two types of sections:

Entity sections correspond to management entities. They can be queried and
configured via management tools. By default each section is assigned a
management name and identity of <section-name>-<n>. For example
"listener-2". You can provide explicit name and identity attributes in any
section.

Include sections define a group of attribute values that can be included in
one or more entity sections.

For example you can define an "ssl-profile" include section with SSL credentials
that can be included in multiple "listener" entities. Here's an example, note
how the 'ssl-profile' attribute of 'listener' sections references the 'name'
attribute of 'ssl-profile' sections.

.nf
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
.fi
""")
        schema = QdSchema()

        def write_attribute(attr, attrs):
            if attr.include and attr.include != attrs:
                return          # Don't repeat included attributes
            if attr.value is not None:
                return          # Don't show fixed-value attributes, they can't be set in conf file.

            default = attr.default
            if isinstance(default, basestring) and default.startswith('$'):
                default = None  # Don't show defaults that are references, confusing.

            f.write('.IP %s\n'%(attr.name))
            f.write('(%s)\n\n'%(', '.join(
                filter(None, [str(attr.atype),
                              attr.required and "required",
                              attr.unique and "unique",
                              default and "default=%s"%default]))))
            if attr.description:
                f.write("%s\n"%attr.description)
            else:
                print "Warning no description for", attr, "in", attrs

        def write_attributes(attrs):
            if attrs.description:
                f.write('\n%s\n'%attrs.description)
            else:
                print "Warning no description for ", attrs
            for attr in attrs.attributes.itervalues():
                write_attribute(attr, attrs)
            f.write('\n\n')

        f.write(".SH INCLUDE SECTIONS\n\n")
        for include in schema.includes.itervalues():
            used_by = [e.name for e in schema.entity_types.itervalues() if include.name in e.include]
            f.write('.SS "%s"\n'%include.name)
            write_attributes(include)
            f.write('.IP "Included by %s."\n'%(', '.join(used_by)))

        f.write(".SH ENTITY SECTIONS\n\n")
        for name, entity_type in schema.entity_types.iteritems():
            f.write('.SS "%s"\n'% name)
            write_attributes(entity_type)
            f.write('.IP "Includes %s."\n'%(', '.join(entity_type.include)))


if __name__ == '__main__':
    make_man_page(sys.argv[1])

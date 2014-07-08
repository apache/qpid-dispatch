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
from qpid_dispatch_internal.management import QdSchema

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

The dispatch router is configured in terms of configuration "entities". Each
type of entity has a set of associated attributes. For example there is a single
"router" entity that has attributes to set configuration associated with the
router as a whole. There may be multiple "listener" and "connector" entities
that specify how to make and receive external connections.

Some entities have attributes in common, for example "listener" and "connector"
entities both have attributes to specify an IP address. All entities have "name"
and "identity" attributes. Commonly used attribute groups are specified as an
"include group" and referenced from entity types that want to include them.


.SH SYNTAX

This file is divided into sections. "Include" sections define groups of
attributes that are included by multiple entity types. "Entity" sections define
the attributes associated with each type of configuration entity.

.nf
<section-name> {
    <attribute-name>: <attribute-value>
    <attribute-name>: <attribute-value>
    ...
}

<section-name> { ...
.fi

.SH SECTIONS
""")
        def write_attribute(attr, attrs):
            if attr.include and attr.include != attrs:
                return          # Don't repeat included attributes
            f.write('.IP %s\n'%(attr.name))
            f.write('(%s)\n\n'%(', '.join(
                filter(None, [str(attr.atype),
                              attr.required and "required",
                              attr.unique and "unique",
                              attr.default and "default=%s"%attr.default]))))
            if attr.description: f.write("%s\n"%attr.description)

        def write_attributes(attrs):
            if attrs.description:
                f.write('\n%s\n'%attrs.description)
            for attr in attrs.attributes.itervalues():
                write_attribute(attr, attrs)

        schema = QdSchema()
        for include in schema.includes.itervalues():
            f.write('.SS "\'%s\' include group"\n'% include.name)
            write_attributes(include)

        for name, entity_type in schema.entity_types.iteritems():
            f.write('.SS "\'%s\' entity"\n'% name)
            f.write('Includes: %s\n\n'%(', '.join(entity_type.include)))
            write_attributes(entity_type)


if __name__ == '__main__':
    make_man_page(sys.argv[1])

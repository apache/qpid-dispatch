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

PREFACE = r"""
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
"""

def make_man_page(filename):
    """Generate a man page for the configuration file from L{QdSchema} descriptions"""

    with open(filename, 'w') as f:
        f.write(PREFACE)

        schema = QdSchema()

        def write_attribute(attr, attrs):
            if attr.annotation and attr.annotation != attrs:
                return          # Don't repeat annotationd attributes
            if attr.value is not None:
                return          # Don't show fixed-value attributes, they can't be set in conf file.

            default = attr.default
            if isinstance(default, basestring) and default.startswith('$'):
                default = None  # Don't show defaults that are references, confusing.

            f.write('\n%s '%(attr.name))
            f.write('(%s)\n'%(', '.join(
                filter(None, [str(attr.atype),
                              attr.required and "required",
                              attr.unique and "unique",
                              default and "default=%s"%default]))))
            if attr.description:
                f.write(":   %s\n"%attr.description)
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

        f.write("\n\n# Annotation Sections\n\n")
        for annotation in schema.annotations.itervalues():
            used_by = [e.short_name for e in schema.entity_types.itervalues()
                       if annotation in e.annotations]
            f.write('\n\n## %s\n'%annotation.short_name)
            write_attributes(annotation)
            if used_by: f.write('Used by %s.\n'%(', '.join(used_by)))

        f.write("\n\n# Configuration Sections\n\n")
        config = schema.entity_type("configurationEntity")
        for entity_type in schema.entity_types.itervalues():
            if config in entity_type.all_bases:
                f.write('\n## %s\n'% entity_type.short_name)
                write_attributes(entity_type)
                if entity_type.annotations:
                    f.write('Annotations %s.\n'%(', '.join(
                        [a.short_name for a in entity_type.annotations])))


if __name__ == '__main__':
    make_man_page(sys.argv[1])

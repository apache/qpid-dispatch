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
Generate C code from the router schema.
"""

import re
from qpid_dispatch_internal.management.schema import EnumType
from qpid_dispatch_internal.management.qdrouter import QdSchema

copyright="""/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */


"""


class Generator(object):

    def __init__(self):
        self.schema = QdSchema()
        self.prefix = ['qd_schema']

        self.generate_enums()

    def header(self, name, text):
        with open(name+'.h', 'w') as f:
            f.write("#ifndef __%s_h__\n#define __%s_h__\n"%(name, name) + copyright + text + "\n#endif\n")

    def source(self, name, text):
        with open(name+'.c', 'w') as f:
            f.write(copyright + text)

    def identifier(self, name):
        return re.sub(r'\W','_', name)

    def underscore(self, names):
        return '_'.join([self.identifier(name) for name in names])

    def prefix_name(self, names):
        return self.underscore(self.prefix + names)

    def type_name(self, names):
        return self.prefix_name(names + ['t'])

    class BaseEnumGenerator(object):
        def decl(self):
            tags = self.tags + ['ENUM_COUNT']
            return "typedef enum {\n" + \
                ",\n".join(["    " + self.name(tag) for tag in tags]) + \
                "\n} %s;\n\n" % self.type_name + \
                "extern const char *%s[%s];\n\n" %  (self.array, self.count)

        def defn(self):
            return "const char *%s[%s] = {\n" % (self.array, self.count) + \
                ",\n".join('    "%s"'%(self.name(tag)) for tag in self.tags) + \
                "\n};\n\n"

    class EntityTypeEnumGenerator(object):
        """
        Generates enums
        """
        def __init__(self, generator, entity, name, tags):
            self.generator, self.entity, self.tags = generator, entity, tags
            self.enum_name = name
            self.type_name = generator.type_name([entity.short_name, self.enum_name])
            self.array = self.generator.prefix_name([entity.short_name, self.enum_name, 'names'])
            self.count = self.name('ENUM_COUNT')

        def name(self, tag):
            return self.generator.prefix_name([self.entity.short_name, self.enum_name, tag]).upper()

    class OperationDefEnumGenerator(BaseEnumGenerator):
        """
        Generates enums for the base entity operationDefs which include CREATE, READ, UPDATE, DELETE
        """
        def __init__(self, generator, entity, name, tags):
            self.generator, self.entity, self.tags = generator, entity, tags
            self.enum_name = name
            self.type_name = generator.type_name([entity.short_name, self.enum_name])
            self.array = self.generator.prefix_name([entity.short_name, self.enum_name, 'names'])
            self.count = self.name('ENUM_COUNT')

        def name(self, tag):
            return self.generator.prefix_name([self.entity.short_name, self.enum_name, tag]).upper()

    class EnumTypeEnumGenerator(BaseEnumGenerator):
        """
        Generates enums for all EnumTypes
        """
        def __init__(self, generator, entity, attribute, tags):
            self.generator, self.entity, self.attribute = generator, entity, attribute
            self.tags = tags
            self.type_name = generator.type_name([entity.short_name, attribute.name])
            self.array = self.generator.prefix_name([entity.short_name, attribute.name, 'names'])
            self.count = self.name('ENUM_COUNT')

        def name(self, tag):
            return self.generator.prefix_name([self.entity.short_name, self.attribute.name, tag]).upper()

    def generate_enums(self):
        enums = [self.EnumTypeEnumGenerator(self, entity, attribute, attribute.atype.tags)
                 for entity in self.schema.entity_types.itervalues()
                 for attribute in entity.attributes.itervalues()
                 if isinstance(attribute.atype, EnumType)]

        # Create an enum for the operations CREATE, READ, UPDATE, DELETE, QUERY
        base_entity = self.schema.entity_types.get(self.schema.prefixdot + 'entity')
        enums.append(self.OperationDefEnumGenerator(self, base_entity, "operation",
                                                    base_entity.operation_defs.keys() + ['QUERY']))

        # Create enum for entity types
        entity_types = self.schema.entity_types.keys()
        entity_types = [w.replace('org.apache.qpid.dispatch.', '') for w in entity_types]
        enums.append(self.OperationDefEnumGenerator(self, base_entity, "type", entity_types))

        # Create enums for attributes
        entity_types = self.schema.entity_types
        for entity_type in entity_types.values():
            attribute_names = []
            for attrib in entity_type.attributes.values():
                attribute_names.append(attrib.name)
            enums.append(self.OperationDefEnumGenerator(self, entity_type, "attributes", attribute_names))

        self.header('schema_enum', '\n'.join(e.decl() for e in enums))
        self.source('schema_enum', '#include "schema_enum.h"\n\n' + '\n'.join(e.defn() for e in enums))

if __name__ == '__main__':
    Generator()

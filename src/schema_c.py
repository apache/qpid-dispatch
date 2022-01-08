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

copyright = """/*
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


class Generator:

    def __init__(self):
        self.schema = QdSchema()
        self.prefix = ['qd_schema']

        self.generate_enums()

    def header(self, name, text):
        with open(name + '.h', 'w') as f:
            f.write("#ifndef __%s_h__\n#define __%s_h__\n" % (name, name) + copyright + text + "\n#endif\n")

    def source(self, name, text):
        with open(name + '.c', 'w') as f:
            f.write(copyright + text)

    def identifier(self, name): return re.sub(r'\W', '_', name)

    def underscore(self, names): return '_'.join([self.identifier(name) for name in names])

    def prefix_name(self, names): return self.underscore(self.prefix + names)

    def type_name(self, names): return self.prefix_name(names + ['t'])

    class EnumGenerator:
        def __init__(self, generator, entity, attribute):
            self.generator, self.entity, self.attribute = generator, entity, attribute
            self.tags = attribute.atype.tags
            self.type_name = generator.type_name([entity.short_name, attribute.name])
            self.array = self.generator.prefix_name([entity.short_name, attribute.name, 'names'])
            self.count = self.name('ENUM_COUNT')

        def name(self, tag):
            return self.generator.prefix_name([self.entity.short_name, self.attribute.name, tag]).upper()

        def decl(self):
            tags = self.tags + ['ENUM_COUNT']
            return "typedef enum {\n" + \
                ",\n".join(["    " + self.name(tag) for tag in tags]) + \
                "\n} %s;\n\n" % self.type_name + \
                "extern const char *%s[%s];\n\n" %  (self.array, self.count)

        def defn(self):
            return "const char *%s[%s] = {\n" % (self.array, self.count) + \
                ",\n".join('    "%s"' % (self.name(tag)) for tag in self.tags) + \
                "\n};\n\n"

    def generate_enums(self):
        enums = [self.EnumGenerator(self, entity, attribute)
                 for entity in self.schema.entity_types.values()
                 for attribute in entity.attributes.values()
                 if isinstance(attribute.atype, EnumType)]
        self.header('schema_enum', '\n'.join(e.decl() for e in enums))
        self.source('schema_enum', '#include "schema_enum.h"\n\n' + '\n'.join(e.defn() for e in enums))


if __name__ == '__main__':
    Generator()

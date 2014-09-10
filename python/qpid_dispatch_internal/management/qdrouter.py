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
Qpid Dispatch Router management schema and config file parsing.
"""

import json
from . import schema
from ..compat import json_load_kwargs

class QdSchema(schema.Schema):
    """
    Qpid Dispatch Router management schema.
    """
    SCHEMA_FILE = schema.schema_file("qdrouter.json")

    def __init__(self):
        """Load schema."""
        with open(self.SCHEMA_FILE) as f:
            super(QdSchema, self).__init__(**json.load(f, **json_load_kwargs))

    def validate(self, entities, full=True, **kwargs):
        """
        In addition to L{schema.Schema.validate}, check the following:

        If the operating mode of the router is not 'interior', then the only
        permitted roles for listeners and connectors is 'normal'.

        @param entities: List of attribute name:value maps.
        @param full: Perform validation for full configuration.
        @param kwargs: See L{schema.Schema.validate}
        """
        super(QdSchema, self).validate_all(entities, **kwargs)

        if full:
            if entities.router[0].mode != 'interior':
                for connect in entities.get(entity_type='listeners') + entities.get(entity_type='connector'):
                    if connect['role'] != 'normal':
                        raise schema.ValidationError("Role '%s' for connection '%s' only permitted with 'interior' mode" % (connect['role'], connect.name))

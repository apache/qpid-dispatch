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
from pkgutil import get_data
from . import schema
from ..compat import JSON_LOAD_KWARGS

class QdSchema(schema.Schema):
    """
    Qpid Dispatch Router management schema.
    """

    CONFIGURATION_ENTITY = u"configurationEntity"
    OPERATIONAL_ENTITY = u"operationalEntity"

    def __init__(self):
        """Load schema."""
        qd_schema = get_data('qpid_dispatch.management', 'qdrouter.json')
        try:
            super(QdSchema, self).__init__(**json.loads(qd_schema, **JSON_LOAD_KWARGS))
        except Exception,e:
            raise ValueError("Invalid schema qdrouter.json: %s" % e)
        self.configuration_entity = self.entity_type(self.CONFIGURATION_ENTITY)
        self.operational_entity = self.entity_type(self.OPERATIONAL_ENTITY)

    def validate_full(self, entities, **kwargs):
        """
        In addition to L{schema.Schema.validate}, check the following:

        listeners and connectors can only have role=inter-router if the
        router has mode=interior.


        @param entities: List of attribute name:value maps.
        @param kwargs: See L{schema.Schema.validate}
        """
        entities = list(entities) # Need to traverse twice
        super(QdSchema, self).validate_all(entities, **kwargs)
        inter_router = not_interior = None
        for e in entities:
            if self.short_name(e.type) == "router" and e.mode != "interior":
                not_interior = e.mode
            if self.short_name(e.type) in ["listener", "connector"] and e.role == "inter-router":
                inter_router = e
            if not_interior and inter_router:
                raise schema.ValidationError(
                    "role='inter-router' only allowed with router mode='interior' for %s." % inter_router)

    def is_configuration(self, entity_type):
        return entity_type and self.configuration_entity in entity_type.all_bases

    def is_operational(self, entity_type):
        return entity_type and self.operational_entity in entity_type.all_bases

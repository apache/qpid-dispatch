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

"""Qpid Dispatch Router management schema and config file parsing."""
import json
from pkgutil import get_data

from qpid_dispatch_internal.compat import JSON_LOAD_KWARGS
from . import schema


class QdSchema(schema.Schema):
    """Qpid Dispatch Router management schema."""

    CONFIGURATION_ENTITY = "configurationEntity"
    OPERATIONAL_ENTITY = "operationalEntity"

    def __init__(self):
        """Load schema."""
        qd_schema = get_data('qpid_dispatch.management', 'qdrouter.json').decode('utf8')
        try:
            super(QdSchema, self).__init__(**json.loads(qd_schema, **JSON_LOAD_KWARGS))
        except Exception as e:
            raise ValueError("Invalid schema qdrouter.json: %s" % e)
        self.configuration_entity = self.entity_type(self.CONFIGURATION_ENTITY)
        self.operational_entity = self.entity_type(self.OPERATIONAL_ENTITY)

    def validate_add(self, attributes, entities):
        """
        Check that listeners and connectors can only have role=inter-router if the router has
        mode=interior.
        """
        entities = list(entities)  # Iterate twice
        super(QdSchema, self).validate_add(attributes, entities)
        entities.append(attributes)
        router_mode = listener_connector_role = listener_role = None
        for e in entities:
            short_type = self.short_name(e['type'])
            if short_type == "router":
                router_mode = e['mode']
            if short_type in ["listener", "connector"]:
                if short_type == "listener":
                    listener_role = e['role']
                list_conn_entity = e
                listener_connector_role = e['role']

            # There are 4 roles for listeners - normal, inter-router, route-container, edge
            if router_mode and listener_connector_role:
                # Standalone routers cannot have inter-router or edge listeners/connectors
                if router_mode == "standalone" and listener_connector_role in ('inter-router', 'edge'):
                    raise schema.ValidationError(
                        "role='standalone' not allowed to connect to or accept connections from other routers.")

                # Only interior routers can have inter-router listeners/connectors
                if router_mode != "interior" and listener_connector_role == "inter-router":
                    raise schema.ValidationError(
                        "role='inter-router' only allowed with router mode='interior' for %s" % list_conn_entity)

            if router_mode and listener_role:
                # Edge routers cannot have edge listeners. Other edge routers cannot make connections into this
                # edge router
                if router_mode == "edge" and listener_role == "edge":
                    raise schema.ValidationError(
                        "role='edge' only allowed with router mode='interior' for %s" % list_conn_entity)

    def is_configuration(self, entity_type):
        return entity_type and self.configuration_entity in entity_type.all_bases

    def is_operational(self, entity_type):
        return entity_type and self.operational_entity in entity_type.all_bases

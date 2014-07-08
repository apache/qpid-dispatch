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

import httplib, re
from qpid_dispatch_internal import dispatch_c
from dispatch import IoAdapter, LogAdapter, LOG_DEBUG, LOG_ERROR
from node import ManagementError
from schema import ValidationError
from entity import Entity, EntityList
from qdrouter import QdSchema
from ..router.message import Message
from traceback import format_exc


class Agent(object):
    def __init__(self, dispatch):
        self.qd = dispatch_c.instance()
        self.dispatch = self.qd.qd_dispatch_p(dispatch)
        # FIXME aconway 2014-06-26: $management
        self.io = [IoAdapter(self.receive, "$management2"),
                   IoAdapter(self.receive, "$management2", True)] # Global
        self.log = LogAdapter("AGENT").log
        self.schema = QdSchema()

    def respond(self, request, status=httplib.OK, description="OK", body=None):
        response = Message(
            address=request.reply_to,
            correlation_id=request.correlation_id,
            properties={'statusCode': status, 'statusDescription': description },
            body=body or {})
        self.log(LOG_DEBUG, "Agent response %s (to %s)"%(response, request))
        try:
            self.io[0].send(response)
        except:
            self.log(LOG_ERROR, "Can't respond to %s: %s"%(request, format_exc()))

    def receive(self, request, link_id):
        self.log(LOG_DEBUG, "Agent request %s on link %s"%(request, link_id))
        def error(e, trace):
            self.log(LOG_ERROR, "Error dispatching %s: %s\n%s"%(request, e, trace))
            self.respond(request, e.status, e.description)
        try:
            self.handle(request)
        except ManagementError, e:
            error(e, format_exc())
        except ValidationError, e:
            error(ManagementError(httplib.BAD_REQUEST, str(e)), format_exc())
        except Exception, e:
            error(ManagementError(httplib.INTERNAL_SERVER_ERROR,
                                  "%s: %s"%(type(e).__name__ , e)), format_exc())

    def handle(self, request):
        op = request.properties.get('operation')
        if not op:
            raise ManagementError(httplib.BAD_REQUEST, "No 'operation' property on %s"%request)
        method = getattr(self, op.lower(), None)
        if not method:
            raise ManagementError(httplib.NOT_IMPLEMENTED, op)
        method(request)

    def create(self, request):

        entity = Entity(request.body)
        for a in ['type', 'name']:
            if a not in request.properties:
                raise ManagementError(httplib.BAD_REQUEST,
                                      "No value for '%s' in request properties"%a)
            if a in entity and entity[a] != request.properties[a]:
                raise ManagementError(httplib.BAD_REQUEST, "Conflicting values for '%s'"%a)
            entity[a] = request.properties[a]
        self.schema.validate(EntityList([entity]), full=False)

        qd, dispatch = self.qd, self.dispatch

        class Creator(object):

            def log(self):
                qd.qd_log_entity(entity)

            def listener(self):
                qd.qd_dispatch_configure_listener(dispatch, entity)
                qd.qd_connection_manager_start(dispatch)

            def connector(self):
                qd.qd_dispatch_configure_connector(dispatch, entity)
                qd.qd_connection_manager_start(dispatch)

            def fixed_address(self):
                qd.qd_dispatch_configure_address(dispatch, entity)

            def waypoint(self):
                qd.qd_dispatch_configure_waypoint(dispatch, entity)
                qd.qd_waypoint_activate_all(dispatch);

            # FIXME aconway 2014-07-04: more types

        try:
            getattr(Creator(), re.sub('-', '_', entity.type))()
        except AttributeError:
            raise ManagementError(
                httplib.BAD_REQUEST, "Cannot create entity of type '%s'"%entity.type)

        self.respond(request, body=dict(entity))

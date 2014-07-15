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

import re
from qpid_dispatch_internal import dispatch_c
from dispatch import IoAdapter, LogAdapter, LOG_DEBUG, LOG_ERROR
from error import ManagementError, OK, BAD_REQUEST, INTERNAL_SERVER_ERROR, NOT_IMPLEMENTED, NOT_FOUND
from schema import ValidationError, Entity as SchemaEntity
from qdrouter import QdSchema
from ..router.message import Message
from traceback import format_exc
from ..compat import OrderedDict

def dictstr(d):
    """Stringify a dict in the form 'k=v, k=v ...' instead of '{k:v, ...}'"""
    return ", ".join("%s=%r" % (k, v) for k, v in d.iteritems())

def required_property(prop, request):
    if prop not in request.properties:
        raise ManagementError(BAD_REQUEST, "No '%s' property: %s"%(prop,request))
    return request.properties[prop]

def not_implemented(operation, type):
    raise ManagementError(NOT_IMPLEMENTED,
                          "Operation '%s' not implemented on %s"  % (operation, type))


class Entity(SchemaEntity):
    """
    Base class for agent entities with operations as well as attributes.

    Implements "read" operation to return attributes, other basic
    operations throw NOT_IMPLEMENTED, they must be implemented by subclasses.
    """

    def __init__(self, qd, dispatch, entity_type, attributes):
        """
        @para qd: Dispatch C library.
        @param dispatch: Pointer to qd_dispatch C object.
        @param entity_type: L{EntityType}
        @param attribute: Attribute name:value map
        """
        super(Entity, self).__init__(entity_type, attributes)
        # Direct __dict__ access to avoid validation as schema attributes
        self.__dict__['_qd'] = qd
        self.__dict__['_dispatch'] = dispatch

    def create(self, request): not_implemented('create', self.type)

    def read(self, request): return dict(self.attributes)

    def update(self, request): not_implemented('update', self.type)

    def delete(self, request): not_implemented('delete', self.type)


class ContainerEntity(Entity):
    def create(self, request):
        self._qd.qd_dispatch_configure_container(self._dispatch, request.body)


class RouterEntity(Entity):
    def create(self, request):
        self._qd.qd_dispatch_configure_router(self._dispatch, self)
        self._qd.qd_dispatch_prepare(self._dispatch)


class LogEntity(Entity):
    def create(self, request):
        self._qd.qd_log_entity(self)


class ListenerEntity(Entity):
    def create(self, request):
        self._qd.qd_dispatch_configure_listener(self._dispatch, self)
        self._qd.qd_connection_manager_start(self._dispatch)


class ConnectorEntity(Entity):
    def create(self, request):
        self._qd.qd_dispatch_configure_connector(self._dispatch, self)
        self._qd.qd_connection_manager_start(self._dispatch)


class FixedAddressEntity(Entity):
    def create(self, request):
        self._qd.qd_dispatch_configure_address(self._dispatch, self)


class WaypointEntity(Entity):
    def create(self, request):
        self._qd.qd_dispatch_configure_waypoint(self._dispatch, self)
        self._qd.qd_waypoint_activate_all(self._dispatch);


class Agent(object):

    def entity(self, attributes):
        """Create an instance of the implementation class for an entity"""
        if not 'type' in attributes:
            raise ManagementError(BAD_REQUEST, "No 'type' attributre in %s" % attributes)
        entity_type = self.schema.entity_type(attributes['type'])
        class_name = ''.join([n.capitalize() for n in entity_type.short_name.split('-')])
        class_name += 'Entity'
        entity_class = globals().get(class_name)
        if not entity_class:
            raise ManagementError(INTERNAL_SERVER_ERROR,
                                  "Can't find implementation for %s" % entity_type)
        return entity_class(self.qd, self.dispatch, entity_type, attributes)

    def __init__(self, dispatch, attribute_maps=None):
        self.qd = dispatch_c.instance()
        self.dispatch = dispatch
        # FIXME aconway 2014-06-26: merge with $management
        self.io = [IoAdapter(self.receive, "$management2"),
                   IoAdapter(self.receive, "$management2", True)] # Global
        self.log = LogAdapter("AGENT").log
        self.schema = QdSchema()
        self.entities = [self.entity(attributes) for attributes in attribute_maps or []]
        self.name = self.identity = 'self'
        self.type='org.amqp.management' # AMQP management node type

    def respond(self, request, status=OK, description="OK", body=None):
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
            self.respond(request, body=self.handle(request))
        except ManagementError, e:
            error(e, format_exc())
        except ValidationError, e:
            error(ManagementError(BAD_REQUEST, str(e)), format_exc())
        except Exception, e:
            error(ManagementError(INTERNAL_SERVER_ERROR,
                                  "%s: %s"%(type(e).__name__ , e)), format_exc())

    def handle(self, request):
        """
        Handle a request.
        Dispatch management node requests to self, entity requests to the entity.
        """
        operation = required_property('operation', request)
        type = required_property('type', request)
        try:
            if type == self.type or operation.lower() == 'create':
                # This is a management node request, handle it myself.
                method = getattr(self, operation.lower())
                return method(request)
            else:
                # This is an entity request, find the entity to handle it.
                entity = self.find_entity(request)
                method = getattr(entity, operation.lower())
                return method(request)
        except AttributeError:
            not_implemented(operation, self.type)

    def query(self, request):
        """Management node query operation"""
        entity_type = request.properties.get('entityType')
        if entity_type:
            try:
                entity_type = self.schema.entity_type(entity_type)
            except:
                raise ManagementError(BAD_REQUEST, "Unknown entity type '%s'" % entity_type)
        attribute_names = request.body.get('attributeNames')
        if not attribute_names:
            if entity_type:
                attribute_names = entity_type.attributes.keys()
            else:               # Every attribute in the schema!
                names = set()
                for e in self.schema.entity_types.itervalues():
                    names.update(e.attributes.keys())
                attribute_names = list(names)

        results = [[e.attributes.get(a) for a in attribute_names]
                   for e in self.entities
                   if not entity_type or e.type == entity_type.name]
        return { 'attributeNames': attribute_names, 'results': results }

    def create(self, request):
        """
        Create is special: it is directed at an entity but the entity
        does not yet exist so it is handled initially by the agent and
        then delegated to the new entity.
        """
        attributes = request.body
        for a in ['type', 'name']:
            value = required_property(a, request)
            if a in attributes and attributes[a] != value:
                raise ManagementError(BAD_REQUEST, "Conflicting values for '%s'"%a)
            attributes[a] = value
        entity = self.entity(attributes)
        # Validate in the context of the existing entities for uniqueness
        self.schema.validate_all([entity]+self.entities)
        entity.create(request)  # Send the create request to the entity
        self.entities.append(entity)
        return dict(entity.attributes)

    def find_entity(self, request):
        attr = [(name, value) for name, value in request.properties.iteritems()
                if name in ['name', 'identity'] and value is not None]
        if len(attr) != 1:
            raise ManagementError(
                BAD_REQUEST, "Specify exactly one of 'name' or 'identity': %s" % request)
        name, value = attr[0]
        try:
            entity = (e for e in self.entities if e.attributes.get(name) == value).next()
        except StopIteration:
            raise ManagementError(NOT_FOUND, "No entity with %s='%s'" % (name, value))
        expect_type = request.properties.get('type')
        if expect_type and entity.type != expect_type:
            raise ManagementError(
                NOT_FOUND, "Type mismatch, expected '%s', found '%s'" %
                (expect_type, entity.type))
        return entity

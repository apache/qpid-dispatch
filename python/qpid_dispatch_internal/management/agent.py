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
Pyton agent for dispatch router.

Implements the server side of the AMQP management protocol for the dispatch router.
Manages a set of manageable Entities that can be Created, Read, Updated and Deleted.
Entity types are described by the schema in qdrouter.json.
"""

from traceback import format_exc
from threading import Lock
from dispatch import IoAdapter, LogAdapter, LOG_DEBUG, LOG_ERROR
from qpid_dispatch.management.error import ManagementError, OK, CREATED, NO_CONTENT, STATUS_TEXT, \
    BadRequest, InternalServerError, NotImplemented, NotFound, Forbidden
from .. import dispatch_c
from .schema import ValidationError, Entity as SchemaEntity
from .qdrouter import QdSchema
from ..router.message import Message

def dictstr(d):
    """Stringify a dict in the form 'k=v, k=v ...' instead of '{k:v, ...}'"""
    return ", ".join("%s=%r" % (k, v) for k, v in d.iteritems())

def required_property(prop, request):
    """Raise exception if required property is missing"""
    if prop not in request.properties:
        raise BadRequest("No '%s' property: %s"%(prop, request))
    return request.properties[prop]

def not_implemented(operation, entity_type):
    """Raise NOT_IMPLEMENTED exception"""
    raise NotImplemented("Operation '%s' not implemented on %s"  % (operation, entity_type))

class AtomicCount(object):
    """Simple atomic counter"""
    def __init__(self, count=0):
        self.count = count
        self.lock = Lock()

    def next(self):
        with self.lock:
            self.count  += 1
            return self.count

class Entity(SchemaEntity):
    """
    Base class for agent entities with operations as well as attributes.
    """

    def __init__(self, agent, entity_type, attributes):
        """
        @para qd: Dispatch C library.
        @param dispatch: Pointer to qd_dispatch C object.
        @param entity_type: L{EntityType}
        @param attribute: Attribute name:value map
        """
        super(Entity, self).__init__(entity_type, attributes)
        # Direct __dict__ access to avoid validation as schema attributes
        self.__dict__['_agent'] = agent
        self.__dict__['_qd'] = agent.qd
        self.__dict__['_dispatch'] = agent.dispatch


    def create(self, request):
        """Subclasses can add extra create actions here"""
        pass

    def read(self, request):
        """Handle read request, default is to return attributes."""
        return (OK, self.attributes)

    def update(self, request):
        self.entity_type.allowed('update')
        self.attributes.update(request.body)
        self.validate()
        return (OK, self.attributes)

    def delete(self, request):
        self.entity_type.allowed('delete')
        self._agent.delete(self)
        return (NO_CONTENT, {})


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
        self._qd.qd_waypoint_activate_all(self._dispatch)

class DummyEntity(Entity):

    id_count = AtomicCount()

    def create(self, request):
        self['identity'] = self.next_id()
        return (OK, self.attributes)

    def next_id(self): return self.type+str(self.id_count.next())

    def callme(self, request):
        return (OK, dict(**request.properties))


class Agent(object):
    """AMQP managment agent"""

    def __init__(self, dispatch, attribute_maps=None):
        self.qd = dispatch_c.instance()
        self.dispatch = dispatch
        # FIXME aconway 2014-06-26: merge with $management
        self.io = [IoAdapter(self.receive, "$management2"),
                   IoAdapter(self.receive, "$management2", True)] # Global
        self.log = LogAdapter("PYAGENT").log                      # FIXME aconway 2014-09-08: AGENT
        self.schema = QdSchema()
        self.entities = [self.create_entity(attributes) for attributes in attribute_maps or []]
        self.name = self.identity = 'self'
        self.type = 'org.amqp.management' # AMQP management node type

    def create_entity(self, attributes):
        """Create an instance of the implementation class for an entity"""
        if not 'type' in attributes:
            raise BadRequest("No 'type' attribute in %s" % attributes)
        entity_type = self.schema.entity_type(attributes['type'])
        class_name = ''.join([n.capitalize() for n in entity_type.short_name.split('-')])
        class_name += 'Entity'
        entity_class = globals().get(class_name)
        if not entity_class:
            raise InternalServerError("Can't find implementation for %s" % entity_type)
        return entity_class(self, entity_type, attributes)

    def respond(self, request, status=OK, description=None, body=None):
        """Send a response to the client"""
        description = description or STATUS_TEXT[status]
        response = Message(
            address=request.reply_to,
            correlation_id=request.correlation_id,
            properties={'statusCode': status, 'statusDescription': description},
            body=body or {})
        self.log(LOG_DEBUG, "Agent response:\n  %s\n  Responding to: \n  %s"%(response, request))
        try:
            self.io[0].send(response)
        except:
            self.log(LOG_ERROR, "Can't respond to %s: %s"%(request, format_exc()))

    def receive(self, request, link_id):
        """Called when a management request is received."""
        self.log(LOG_DEBUG, "Agent request %s on link %s"%(request, link_id))
        def error(e, trace):
            """Raise an error"""
            self.log(LOG_ERROR, "Error dispatching %s: %s\n%s"%(request, e, trace))
            self.respond(request, e.status, e.description)
        try:
            status, body = self.handle(request)
            self.respond(request, status=status, body=body)
        except ManagementError, e:
            error(e, format_exc())
        except ValidationError, e:
            error(BadRequest(str(e)), format_exc())
        except Exception, e:
            error(InternalServerError("%s: %s"%(type(e).__name__, e)), format_exc())

    def handle(self, request):
        """
        Handle a request.
        Dispatch management node requests to self, entity requests to the entity.
        @return: (response-code, body)
        """
        operation = required_property('operation', request)
        type = required_property('type', request)
        if type == self.type or operation.lower() == 'create':
            target = self
        else:
            target = self.find_entity(request)
            target.entity_type.allowed(operation)
        try:
            method = getattr(target, operation.lower())
        except AttributeError:
            not_implemented(operation, type)
        return method(request)

    def query(self, request):
        """Management node query operation"""
        entity_type = request.properties.get('entityType')
        if entity_type:
            try:
                entity_type = self.schema.entity_type(entity_type)
            except:
                raise BadRequest("Unknown entity type '%s'" % entity_type)
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
        return (OK, {'attributeNames': attribute_names, 'results': results})

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
                raise BadRequest("Conflicting values for '%s'"%a)
            attributes[a] = value
        entity = self.create_entity(attributes)
        entity.entity_type.allowed('create')
        # Validate in the context of the existing entities for uniqueness
        self.schema.validate_all([entity]+self.entities)
        entity.create(request)  # Send the create request to the entity
        self.entities.append(entity)
        return (CREATED, entity.attributes)

    def find_entity(self, request):
        """Find the entity addressed by request"""
        attr = [(name, value) for name, value in request.properties.iteritems()
                if name in ['name', 'identity'] and value is not None]
        if len(attr) != 1:
            raise BadRequest("Specify exactly one of 'name' or 'identity': %s" % request)
        name, value = attr[0]
        try:
            entity = (e for e in self.entities if e.attributes.get(name) == value).next()
        except StopIteration:
            raise NotFound("No entity with %s='%s'" % (name, value))
        expect_type = request.properties.get('type')
        if expect_type and entity.type != expect_type:
            raise NotFound("Type mismatch, expected '%s', found '%s'" %
                               (expect_type, entity.type))
        return entity

    def delete(self, entity):
        try:
            self.entities.remove(entity)
        except ValueError:
            raise NotFound("Cannot delete, entity not found: %s"%entity)

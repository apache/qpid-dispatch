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

"""Agent implementing the server side of the AMQP management protocol.

Adapter layer between external attribute-value maps sent/received via the AMQP
management protocol and implementation objects (C or python) of the dispatch
router. Entity types are as described in qdrouter.json schema. Reading
configuration files is treated as a set of CREATE operations.

Maintains a set of L{EntityAdapter} that hold attribute maps reflecting the last
known attribute values of the implementation objects. Delegates management
operations to the correct adapter.

EntityAdapters are created/deleted in two ways:

- Externally by CREATE/DELETE operations (or loading config file)
- Internally by creation or deletion of corresponding implementation object.

Memory managment: The implementation is reponsible for informing the L{Agent}
when an implementation object is created and *before* it is deleted in the case
of a C object.

EntityAdapters can:

- Receive attribute maps via CREATE or UPDATE operations (reading configuration
  files is treated as a set of CREATE operations) and set configuration in the
  implementation objects.

- Refresh the adapters attribute map to reflect the current state of the
  implementation objects, to respond to READ or QUERY operations with up-to-date values.

To avoid confusion the term "update" is only used for the EntityAdapter updating
the implementation object. The term "refresh" is used for the EntityAdapter
getting current information from the implementation object.

## Threading:

The agent is locked to be thread safe, called in the following threads:
- Reading configuration file in initialization thread (no contention).
- Management requests arriving in multiple, concurrent connection threads.
- Implementation objects created/deleted in multiple, concurrent connection threads.

When refreshing attributes, the agent must also read C implementation object
data that may be updated in other threads.

"""
from __future__ import unicode_literals
from __future__ import division
from __future__ import absolute_import
from __future__ import print_function

import traceback, json, pstats
import socket
import unicodedata
from traceback import format_exc
from threading import Lock
from cProfile import Profile
try:
    # py2
    from cStringIO import StringIO
except ImportError:
    from io import StringIO

from ctypes import c_void_p, py_object, c_long
from subprocess import Popen
try:
    # Python 2
    from future_builtins import filter
except ImportError:
    # Python 3
    pass

from ..dispatch import IoAdapter, LogAdapter, LOG_INFO, LOG_WARNING, LOG_DEBUG, LOG_ERROR, TREATMENT_ANYCAST_CLOSEST
from qpid_dispatch.management.error import ManagementError, OK, CREATED, NO_CONTENT, STATUS_TEXT, \
    BadRequestStatus, InternalServerErrorStatus, NotImplementedStatus, NotFoundStatus, ForbiddenStatus
from qpid_dispatch.management.entity import camelcase
from .schema import ValidationError, SchemaEntity, EntityType
from .qdrouter import QdSchema
from ..router.message import Message
from ..router.address import Address
from ..policy.policy_manager import PolicyManager
from qpid_dispatch_internal.compat import dict_iteritems


def dictstr(d):
    """Stringify a dict in the form 'k=v, k=v ...' instead of '{k:v, ...}'"""
    return ", ".join("%s=%s" % (k, v) for k, v in dict_iteritems(d))

def required_property(prop, request):
    """Raise exception if required property is missing"""
    if not request.properties or prop not in request.properties:
        raise BadRequestStatus("No '%s' property: %s"%(prop, request))
    return request.properties[prop]

def not_implemented(operation, entity_type):
    """Raise NOT_IMPLEMENTED exception"""
    raise NotImplementedStatus("Operation '%s' not implemented on %s"  % (operation, entity_type))


class AtomicCount(object):
    """Simple atomic counter"""
    def __init__(self, count=0):
        self.count = count
        self.lock = Lock()

    def next(self):
        with self.lock:
            n = self.count
            self.count += 1
            return n


class Implementation(object):
    """Abstract implementation wrapper"""
    def __init__(self, entity_type, key):
        self.entity_type, self.key = entity_type, key


class CImplementation(Implementation):
    """Wrapper for a C implementation pointer"""
    def __init__(self, qd, entity_type, pointer):
        super(CImplementation, self).__init__(entity_type, pointer)
        fname = "qd_entity_refresh_" + entity_type.short_name.replace('.', '_')
        self.refreshfn = qd.function(fname, c_long, [py_object, c_void_p])

    def refresh_entity(self, attributes):
        return self.refreshfn(attributes, self.key) or True


class PythonImplementation(Implementation):
    """Wrapper for a Python implementation object"""
    def __init__(self, entity_type, impl):
        """impl.refresh_entity(attributes) must be a valid function call"""
        super(PythonImplementation, self).__init__(entity_type, id(impl))
        self.refresh_entity = impl.refresh_entity


class EntityAdapter(SchemaEntity):
    """
    Base class for agent entities with operations as well as attributes.
    """

    def __init__(self, agent, entity_type, attributes=None, validate=True):
        """
        @para agent: Containing L{Agent}
        @param entity_type: L{EntityType}
        @param attributes: Attribute name:value map
        @param validate: If true, validate the entity.
        """
        super(EntityAdapter, self).__init__(entity_type, attributes or {}, validate=validate)
        # Direct __dict__ access to avoid validation as schema attributes
        self.__dict__['_agent'] = agent
        self.__dict__['_log'] = agent.log
        self.__dict__['_qd'] = agent.qd
        self.__dict__['_dispatch'] = agent.dispatch
        self.__dict__['_policy'] = agent.policy
        self.__dict__['_implementations'] = []

    def validate(self, **kwargs):
        """Set default identity and name if not already set, then do schema validation"""
        identity = self.attributes.get("identity")
        name = self.attributes.get("name")
        if identity:
            if not name:
                self.attributes[u"name"] = "%s/%s" % (self.entity_type.short_name, self._identifier())
        else:
            self.attributes[u"identity"] = "%s/%s" % (self.entity_type.short_name, self._identifier())
            if not name:
                self.attributes.setdefault(u'name', self.attributes[u'identity'])

        super(EntityAdapter, self).validate(**kwargs)

    def _identifier(self):
        """
        Generate identifier. identity=type/identifier.
        Default is per-type counter, derived classes can override.
        """
        try:
            counter = type(self)._identifier_count
        except AttributeError:
            counter = type(self)._identifier_count = AtomicCount()
        return str(counter.next())

    def _refresh(self):
        """Refresh self.attributes from implementation object(s)."""
        for impl in self._implementations:
            impl.refresh_entity(self.attributes)
        return bool(self._implementations)

    def _add_implementation(self, impl):
        """Add an implementaiton object to use to refresh our attributes"""
        self._implementations.append(impl)

    def create(self):
        """Subclasses can add extra create actions here"""
        pass

    def read(self, request):
        """Handle read request, default is to return attributes."""
        request_type = self.entity_type.schema.long_name(request.properties.get('type'))
        if request_type and self.type != request_type:
            raise NotFoundStatus("Entity type '%s' does match requested type '%s'" %
                           (self.type, request_type))
        return (OK, self.attributes)

    def update(self, request):
        """Handle update request with new attributes from management client"""
        self.entity_type.update_check(request.body, self.attributes)
        newattrs = dict(self.attributes, **request.body)
        self.entity_type.validate(newattrs)
        self.attributes = newattrs
        self._update()
        return (OK, self.attributes)

    def _update(self):
        """Subclasses implement update logic here"""
        pass

    def delete(self, request):
        """Handle delete request from client"""
        self._delete()
        self._agent.remove(self)
        return (NO_CONTENT, {})

    def _delete(self):
        """Subclasses implement delete logic here"""
        pass

    def __str__(self):
        keys = sorted(self.attributes.keys())
        # If the attribute is hidden the attribute value will show up as stars ('*******').
        return "Entity(%s)" % ", ".join("%s=%s" % (k, '*******' if self.entity_type.attribute(k).hidden else self.attributes[k]) for k in keys)


class RouterEntity(EntityAdapter):
    def __init__(self, agent, entity_type, attributes=None):
        super(RouterEntity, self).__init__(agent, entity_type, attributes, validate=False)
        # Router is a mix of configuration and operational entity.
        # The statistics attributes are operational not configured.
        self._add_implementation(
            CImplementation(agent.qd, entity_type, self._dispatch))

    def _identifier(self):
        return self.attributes.get('id')

    def create(self):
        if u"id" in self.attributes.keys():
            for ch in self.attributes[u"id"]:
                try:
                    disallowed = unicodedata.category(ch)[0] in "CZ"
                except TypeError:
                    disallowed = unicodedata.category(ch.decode('utf-8'))[0] in "CZ"
                if disallowed:  # disallow control and whitespace characters
                    raise AttributeError("Router id attribute containing character '%s' in id '%s' is disallowed." % (ch, self.attributes[u"id"]))
        try:
            self.attributes[u"hostName"] = socket.gethostbyaddr(socket.gethostname())[0]
        except:
            self.attributes[u"hostName"] = ''
        self._qd.qd_dispatch_configure_router(self._dispatch, self)

    def __str__(self):
        return super(RouterEntity, self).__str__().replace("Entity(", "RouterEntity(")


class LogEntity(EntityAdapter):

    def __init__(self, agent, entity_type, attributes=None, validate=True):
        # Special defaults for DEFAULT module.
        if attributes.get("module") == "DEFAULT":
            defaults = dict(enable="info+", includeTimestamp=True, includeSource=False, outputFile="stderr")
            attributes = dict(defaults, **attributes)
        super(LogEntity, self).__init__(agent, entity_type, attributes, validate=True)

    def _identifier(self): return self.attributes.get('module')

    def create(self):
        self._qd.qd_log_entity(self)

    def _update(self):
        self._qd.qd_log_entity(self)

    def _delete(self):
        """Can't actually delete a log source but return it to the default state"""
        self._qd.qd_log_source_reset(self.attributes['module'])

    def __str__(self):
        return super(LogEntity, self).__str__().replace("Entity(", "LogEntity(")


class PolicyEntity(EntityAdapter):
    def __init__(self, agent, entity_type, attributes=None):
        super(PolicyEntity, self).__init__(agent, entity_type, attributes, validate=False)
        # Policy is a mix of configuration and operational entity.
        # The statistics attributes are operational not configured.
        self._add_implementation(
            CImplementation(agent.qd, entity_type, self._dispatch))

    def create(self):
        self._qd.qd_dispatch_configure_policy(self._dispatch, self)
        self._qd.qd_dispatch_register_policy_manager(self._dispatch, self._policy)

    def _identifier(self):
        return self.attributes.get('module')

    def __str__(self):
        return super(PolicyEntity, self).__str__().replace("Entity(", "PolicyEntity(")

class VhostEntity(EntityAdapter):
    def create(self):
        self._policy.create_ruleset(self.attributes)

    def _identifier(self):
        return self.attributes.get('hostname')

    def __str__(self):
        return super(VhostEntity, self).__str__().replace("Entity(", "VhostEntity(")

    def _delete(self):
        self._policy.delete_ruleset(self.hostname)

    def _update(self):
        self._policy.update_ruleset(self.attributes)


class VhostStatsEntity(EntityAdapter):
    def _identifier(self):
        return self.attributes.get('hostname')

    def __str__(self):
        return super(VhostStatsEntity, self).__str__().replace("Entity(", "VhostStatsEntity(")


def _host_port_name_identifier(entity):
    for attr in ['host', 'port', 'name']: # Set default values if need be
        entity.attributes.setdefault(
            attr, entity.entity_type.attribute(attr).missing_value())

    if entity.attributes.get('name'):
        return "%s:%s:%s" % (entity.attributes['host'], entity.attributes['port'], entity.attributes['name'])
    else:
        return "%s:%s" % (entity.attributes['host'], entity.attributes['port'])


class SslProfileEntity(EntityAdapter):
    def create(self):
        return self._qd.qd_dispatch_configure_ssl_profile(self._dispatch, self)

    def _delete(self):
        self._qd.qd_connection_manager_delete_ssl_profile(self._dispatch, self._implementations[0].key)
    def _identifier(self):
        return self.name

    def __str__(self):
        return super(SslProfileEntity, self).__str__().replace("Entity(", "SslProfileEntity(")

class AuthServicePluginEntity(EntityAdapter):
    def create(self):
        return self._qd.qd_dispatch_configure_sasl_plugin(self._dispatch, self)

    def _delete(self):
        self._qd.qd_connection_manager_delete_sasl_plugin(self._dispatch, self._implementations[0].key)
    def _identifier(self):
        return self.name

    def __str__(self):
        return super(AuthServicePluginEntity, self).__str__().replace("Entity(", "AuthServicePluginEntity(")

class ConnectionBaseEntity(EntityAdapter):
    """
    Provides validation of the openProperties attribute shared by Listener and
    Connector entities.
    """
    # qdrouterd reserves a set of connection-property keys as well as any key
    # that starts with certain prefixes
    _RESERVED_KEYS=['product',
                    'version',
                    'failover-server-list',
                    'network-host',
                    'port',
                    'scheme'
                    'hostname']
    _RESERVED_PREFIXES=['qd.', 'x-opt-qd.']

    def validate(self, **kwargs):
        super(ConnectionBaseEntity, self).validate(**kwargs)
        op = self.attributes.get('openProperties')
        if op:
            # key check
            msg = "Reserved key '%s' not allowed in openProperties"
            try:
                for key in op.keys():
                    if key in self._RESERVED_KEYS:
                        raise ValidationError(msg % key)
                    for prefix in self._RESERVED_PREFIXES:
                        if key.startswith(prefix):
                            raise ValidationError(msg % key)
            except ValidationError:
                raise
            except Exception as exc:
                raise ValidationError(str(exc))

            # role check - cannot allow user properties on router-2-router
            # connections
            role = self.attributes.get('role', 'normal')
            if role in ['inter-router', 'edge']:
                raise ValidationError("openProperties not allowed for role %s"
                                      % role);


class ListenerEntity(ConnectionBaseEntity):
    def create(self):
        config_listener = self._qd.qd_dispatch_configure_listener(self._dispatch, self)
        self._qd.qd_connection_manager_start(self._dispatch)
        return config_listener

    def _identifier(self):
        return _host_port_name_identifier(self)

    def __str__(self):
        return super(ListenerEntity, self).__str__().replace("Entity(", "ListenerEntity(")

    def _delete(self):
        self._qd.qd_connection_manager_delete_listener(self._dispatch, self._implementations[0].key)


class ConnectorEntity(ConnectionBaseEntity):
    def __init__(self, agent, entity_type, attributes=None, validate=True):
        super(ConnectorEntity, self).__init__(agent, entity_type, attributes,
                                              validate)

    def create(self):
        config_connector = self._qd.qd_dispatch_configure_connector(self._dispatch, self)
        self._qd.qd_connection_manager_start(self._dispatch)
        return config_connector

    def _delete(self):
        self._qd.qd_connection_manager_delete_connector(self._dispatch, self._implementations[0].key)

    def _identifier(self):
        return _host_port_name_identifier(self)

    def __str__(self):
        return super(ConnectorEntity, self).__str__().replace("Entity(", "ConnectorEntity(")

class AddressEntity(EntityAdapter):
    def create(self):
        self._qd.qd_dispatch_configure_address(self._dispatch, self)

    def __str__(self):
        return super(AddressEntity, self).__str__().replace("Entity(", "AddressEntity(")

class LinkRouteEntity(EntityAdapter):
    def create(self):
        self._qd.qd_dispatch_configure_link_route(self._dispatch, self)

    def __str__(self):
        return super(LinkRouteEntity, self).__str__().replace("Entity(", "LinkRouteEntity(")

class AutoLinkEntity(EntityAdapter):
    def create(self):
        self._qd.qd_dispatch_configure_auto_link(self._dispatch, self)

    def __str__(self):
        return super(AutoLinkEntity, self).__str__().replace("Entity(", "AutoLinkEntity(")

class ConsoleEntity(EntityAdapter):
    def __str__(self):
        return super(ConsoleEntity, self).__str__().replace("Entity(", "ConsoleEntity(")

    def create(self):
        # if a named listener is present, use its host:port
        self._agent.log(LOG_WARNING, "Console entity is deprecated: Use http:yes on listener entity instead")
        name = self.attributes.get('listener')
        if name:
            listeners = self._agent.find_entity_by_type("listener")
            for listener in listeners:
                if listener.name == name:
                    try:
                        #required
                        host   = listener.attributes['host']
                        port   = listener.attributes['port']
                        #optional
                        wsport = self.attributes.get('wsport')
                        home   = self.attributes.get('home')
                        args   = self.attributes.get('args')

                        pargs = []
                        pargs.append(self.attributes['proxy'])
                        if args:
                            # Replace any $port|$host|$wsport|$home
                            dargs = {'$port': port, '$host': host}
                            if wsport:
                                dargs['$wsport'] = wsport
                            if home:
                                dargs['$home'] = home
                            for k,v in dict_iteritems(dargs):
                                args = args.replace(k,str(v))
                            pargs += args.split()

                        #run the external program
                        Popen(pargs)
                    except:
                        self._agent.log(LOG_ERROR, "Can't parse console entity: %s" % (format_exc()))
                    break

class DummyEntity(EntityAdapter):
    def callme(self, request):
        return (OK, dict(**request.properties))


class RouterLinkEntity(EntityAdapter):
    def __str__(self):
        return super(RouterLinkEntity, self).__str__().replace("Entity(", "RouterLinkEntity(")


class RouterNodeEntity(EntityAdapter):
    def _identifier(self):
        return self.attributes.get('id')

    def __str__(self):
        return super(RouterNodeEntity, self).__str__().replace("Entity(", "RouterNodeEntity(")


class RouterAddressEntity(EntityAdapter):
    def _identifier(self):
        return self.attributes.get('key')

    def __str__(self):
        return super(RouterAddressEntity, self).__str__().replace("Entity(", "RouterAddressEntity(")


class LogStatsEntity(EntityAdapter):
    def _identifier(self):
        return self.attributes.get('identity')

    def __str__(self):
        return super(LogStatsEntity, self).__str__().replace("Entity(", "LogStatsEntity(")


class AllocatorEntity(EntityAdapter):
    def _identifier(self):
        return self.attributes.get('typeName')

    def __str__(self):
        return super(AllocatorEntity, self).__str__().replace("Entity(", "AllocatorEntity(")

class ExchangeEntity(EntityAdapter):
    def create(self):
        self._qd.qd_dispatch_configure_exchange(self._dispatch, self)

    def __str__(self):
        return super(ExchangeEntity, self).__str__().replace("Entity(", "ExchangeEntity(")

class BindingEntity(EntityAdapter):
    def create(self):
        self._qd.qd_dispatch_configure_binding(self._dispatch, self)

    def __str__(self):
        return super(BindingEntity, self).__str__().replace("Entity(", "BindingEntity(")


class EntityCache(object):
    """
    Searchable cache of entities, can be refreshed from implementation objects.
    """

    def __init__(self, agent):
        self.entities = []
        self.implementations = {}
        self.agent = agent
        self.qd = self.agent.qd
        self.schema = agent.schema
        self.log = self.agent.log

    def map_filter(self, function, test):
        """Filter with test then apply function."""
        if function is None:
            function = lambda x: x  # return results of filter
        return list(map(function, filter(test, self.entities)))

    def map_type(self, function, type):
        """Apply function to all entities of type, if type is None do all
        entities"""
        if function is None:
            function = lambda x: x
        if type is None:
            return list(map(function, self.entities))
        else:
            if not isinstance(type, EntityType):
                type = self.schema.entity_type(type)
            return list(map(function, filter(lambda e: e.entity_type.is_a(type),
                                             self.entities)))
    def validate_add(self, entity):
        self.schema.validate_add(entity, self.entities)

    def add(self, entity):
        """Add an entity to the agent"""
        self.log(LOG_DEBUG, "Add entity: %s" % entity)
        entity.validate()       # Fill in defaults etc.
        # Validate in the context of the existing entities for uniqueness
        self.validate_add(entity)
        self.entities.append(entity)

    def _add_implementation(self, implementation, adapter=None):
        """Create an adapter to wrap the implementation object and add it"""
        cls = self.agent.entity_class(implementation.entity_type)
        if not adapter:
            adapter = cls(self.agent, implementation.entity_type, validate=False)
        self.implementations[implementation.key] = adapter
        adapter._add_implementation(implementation)
        adapter._refresh()
        self.add(adapter)

    def add_implementation(self, implementation, adapter=None):
        self._add_implementation(implementation, adapter=adapter)

    def _remove(self, entity):
        try:
            self.entities.remove(entity)
            self.log(LOG_DEBUG, "Remove %s entity: %s" %
                     (entity.entity_type.short_name, entity.attributes['identity']))
        except ValueError:
            pass

    def remove(self, entity):
        self._remove(entity)

    def _remove_implementation(self, key):
        if key in self.implementations:
            entity = self.implementations[key]
            del self.implementations[key]
            self._remove(entity)

    def remove_implementation(self, key):
        self._remove_implementation(key)

    def refresh_from_c(self):
        """Refresh entities from the C dispatch runtime"""
        REMOVE, ADD = 0, 1

        def remove_redundant(events):
            """Remove redundant add/remove pairs of events."""
            add = {}            # add[pointer] = index of add event.
            redundant = []      # List of redundant event indexes.
            for i in range(len(events)):
                action, type, pointer = events[i]
                if action == ADD:
                    add[pointer] = i
                elif pointer in add: # action == REMOVE and there's an ADD
                    redundant.append(add[pointer])
                    redundant.append(i)
                    del add[pointer]
            for i in sorted(redundant, reverse=True):
                events.pop(i)

        self.qd.qd_dispatch_router_lock(self.agent.dispatch)
        try:
            events = []
            self.qd.qd_entity_refresh_begin(events)
            remove_redundant(events)
            for action, type, pointer in events:
                if action == REMOVE:
                    self._remove_implementation(pointer)
                elif action == ADD:
                    entity_type = self.schema.entity_type(type)
                    self._add_implementation(CImplementation(self.qd, entity_type, pointer))
            # Refresh the entity values while the lock is still held.
            for e in self.entities:
                e._refresh()
        finally:
            self.qd.qd_entity_refresh_end()
            self.qd.qd_dispatch_router_unlock(self.agent.dispatch)

class ManagementEntity(EntityAdapter):
    """An entity representing the agent itself. It is a singleton created by the agent."""

    def __init__(self, agent, entity_type, attributes, validate=True):
        attributes = {"identity": "self", "name": "self"}
        super(ManagementEntity, self).__init__(agent, entity_type, attributes, validate=validate)
        self.__dict__["_schema"] = entity_type.schema

    def requested_type(self, request):
        type = request.properties.get('entityType')
        if type:
            return self._schema.entity_type(type)
        else:
            return None

    def query(self, request):
        """Management node query operation"""
        entity_type = self.requested_type(request)
        if entity_type:
            all_attrs = list(entity_type.attributes.keys())
        else:
            all_attrs = list(self._schema.all_attributes)

        names = request.body.get('attributeNames')
        if names:
            unknown = set(names) - set(all_attrs)
            if unknown:
                if entity_type:
                    for_type = " for type %s" % entity_type.name
                else:
                    for_type = ""
                raise NotFoundStatus("Unknown attributes %s%s." % (list(unknown), for_type))
        else:
            names = all_attrs

        results = []
        def add_result(entity):
            result = []
            non_empty = False
            for name in names:
                result.append(entity.attributes.get(name))
                if result[-1] is not None:
                    non_empty = True
            if non_empty:
                results.append(result)

        self._agent.entities.map_type(add_result, entity_type)
        return (OK, {'attributeNames': names, 'results': results})

    def get_types(self, request):
        type = self.requested_type(request)
        return (OK, dict((t.name, [b.name for b in t.all_bases])
                         for t in self._schema.by_type(type)))

    def get_annotations(self, request):
        """
        We are not supporting any annotations at the moment.
        """
        return (OK, {})

    def get_operations(self, request):
        type = self.requested_type(request)
        return (OK, dict((t, et.operations)
                         for t, et in dict_iteritems(self._schema.entity_types)
                         if not type or type.name == t))

    def get_attributes(self, request):
        type = self.requested_type(request)
        return (OK, dict((t, [a for a in et.attributes])
                         for t, et in dict_iteritems(self._schema.entity_types)
                         if not type or type.name == t))

    def get_mgmt_nodes(self, request):
        router = self._agent.entities.map_type(None, 'router')[0]
        area = router.attributes['area']
        def node_address(node):
            return str(Address.topological(node.attributes['id'], "$management", area))
        return (OK, self._agent.entities.map_type(node_address, 'router.node'))

    def get_schema(self, request):
        return (OK, self._schema.dump())

    def _intprop(self, request, prop):
        value = request.properties.get(prop)
        if value is not None:
            value = int(value)
        return value

    def get_json_schema(self, request):
        return (OK, json.dumps(self._schema.dump(), indent=self._intprop(request, "indent")))

    def get_log(self, request):
        logs = self._qd.qd_log_recent_py(self._intprop(request, "limit") or -1)
        return (OK, logs)

    def profile(self, request):
        """Start/stop the python profiler, returns profile results"""
        profile = self.__dict__.get("_profile")
        if "start" in request.properties:
            if not profile:
                profile = self.__dict__["_profile"] = Profile()
            profile.enable()
            self._log(LOG_INFO, "Started python profiler")
            return (OK, None)
        if not profile:
            raise BadRequestStatus("Profiler not started")
        if "stop" in request.properties:
            profile.create_stats()
            self._log(LOG_INFO, "Stopped python profiler")
            out = StringIO()
            stats = pstats.Stats(profile, stream=out)
            try:
                stop = request.properties["stop"]
                if stop == "kgrind": # Generate kcachegrind output using pyprof2calltree
                    from pyprof2calltree import convert
                    convert(stats, out)
                elif stop == "visualize": # Start kcachegrind using pyprof2calltree
                    from pyprof2calltree import visualize
                    visualize(stats)
                else:
                    stats.print_stats() # Plain python profile stats
                return (OK, out.getvalue())
            finally:
                out.close()
        raise BadRequestStatus("Bad profile request %s" % (request))

class Agent(object):
    """AMQP managment agent. Manages entities, directs requests to the correct entity."""

    def __init__(self, dispatch, qd):
        self.qd = qd
        self.dispatch = dispatch
        self.schema = QdSchema()
        self.entities = EntityCache(self)
        self.request_lock = Lock()
        self.log_adapter = LogAdapter("AGENT")
        self.policy = PolicyManager(self)
        self.management = self.create_entity({"type": "management"})
        self.add_entity(self.management)

    def log(self, level, text):
        info = traceback.extract_stack(limit=2)[0] # Caller frame info
        self.log_adapter.log(level, text, info[0], info[1])

    def activate(self, address):
        """Register the management address to receive management requests"""
        self.entities.refresh_from_c()
        self.log(LOG_INFO, "Activating management agent on %s" % address)
        self.io = IoAdapter(self.receive, address, 'L', '0', TREATMENT_ANYCAST_CLOSEST)

    def entity_class(self, entity_type):
        """Return the class that implements entity_type"""
        class_name = camelcase(entity_type.short_name, capital=True) + 'Entity'
        entity_class = globals().get(class_name)
        if not entity_class:
            raise InternalServerErrorStatus(
                "Can't find implementation '%s' for '%s'" % (class_name, entity_type.name))
        return entity_class

    def create_entity(self, attributes):
        """Create an instance of the implementation class for an entity"""

        if attributes.get('identity') is not None:
            raise BadRequestStatus("'identity' attribute cannot be specified %s" % attributes)
        if attributes.get('type') is None:
            raise BadRequestStatus("No 'type' attribute in %s" % attributes)
        entity_type = self.schema.entity_type(attributes['type'])
        return self.entity_class(entity_type)(self, entity_type, attributes)

    def respond(self, request, status=OK, description=None, body=None):
        """Send a response to the client"""
        if body is None:
            body = {}
        description = description or STATUS_TEXT[status]
        response = Message(
            address=request.reply_to,
            correlation_id=request.correlation_id,
            properties={'statusCode': status, 'statusDescription': description},
            body=body)
        self.log(LOG_DEBUG, "Agent response:\n  %s\n  Responding to: \n  %s"%(response, request))
        try:
            self.io.send(response)
        except:
            self.log(LOG_ERROR, "Can't respond to %s: %s"%(request, format_exc()))

    def receive(self, request, unused_link_id, unused_cost):
        """Called when a management request is received."""
        def error(e, trace):
            """Raise an error"""
            self.log(LOG_ERROR, "Error performing %s: %s"%(request.properties.get('operation'), e.description))
            self.respond(request, e.status, e.description)

        # If there's no reply_to, don't bother to process the request.
        if not request.reply_to:
            return

        # Coarse locking, handle one request at a time.
        with self.request_lock:
            try:
                self.entities.refresh_from_c()
                self.log(LOG_DEBUG, "Agent request %s"% request)
                status, body = self.handle(request)
                self.respond(request, status=status, body=body)
            except ManagementError as e:
                error(e, format_exc())
            except ValidationError as e:
                error(BadRequestStatus(str(e)), format_exc())
            except Exception as e:
                error(InternalServerErrorStatus("%s: %s"%(type(e).__name__, e)), format_exc())

    def entity_type(self, type):
        try:
            return self.schema.entity_type(type)
        except ValidationError as e:
            raise NotFoundStatus(str(e))

    def handle(self, request):
        """
        Handle a request.
        Dispatch management node requests to self, entity requests to the entity.
        @return: (response-code, body)
        """
        operation = required_property('operation', request)
        if operation.lower() == 'create':
            # Create requests are entity requests but must be handled by the agent since
            # the entity does not yet exist.
            return self.create(request)
        else:
            target = self.find_entity(request)
            target.entity_type.allowed(operation, request.body)
            try:
                method = getattr(target, operation.lower().replace("-", "_"))
            except AttributeError:
                not_implemented(operation, target.type)
            return method(request)

    def _create(self, attributes):
        """Create an entity, called externally or from configuration file."""
        entity = self.create_entity(attributes)

        # DISPATCH-1622 - Call validate_add() *before* calling entity.create(). If the
        # validate_add() throws an Exception, we will save ourselves the
        # trouble of calling entity.create()
        self.entities.validate_add(entity)

        # DISPATCH-1622 - The following entity.create() is going to call the create method
        # of the actual entity implementation like a ConnectorEntity or a
        # ListenerEntity and so on which in turn ends up calling the c code.
        # The previous line calls validate_add BEFORE
        # entity.create() is called thus preventing the C code from being called.
        pointer = entity.create()
        if pointer:
            cimplementation = CImplementation(self.qd, entity.entity_type, pointer)
            self.entities.add_implementation(cimplementation, entity)
        else:
            self.add_entity(entity)
        return entity

    def create(self, request):
        """
        Create operation called from an external client.
        Create is special: it is directed at an entity but the entity
        does not yet exist so it is handled initially by the agent and
        then delegated to the new entity.
        """
        attributes = request.body
        for a in ['type', 'name']:
            prop = request.properties.get(a)
            if prop:
                old = attributes.setdefault(a, prop)
                if old is not None and old != prop:
                    raise BadRequestStatus("Conflicting values for '%s'" % a)
            attributes[a] = prop
        if attributes.get('type') is None:
            raise BadRequestStatus("No 'type' attribute in %s" % attributes)
        et = self.schema.entity_type(attributes['type'])
        et.allowed("CREATE", attributes)
        et.create_check(attributes)
        return (CREATED, self._create(attributes).attributes)

    def configure(self, attributes):
        """Created via configuration file"""
        self._create(attributes)

    def add_entity(self, entity):
        """Add an entity adapter"""
        self.entities.add(entity)

    def remove(self, entity):
        self.entities.remove(entity)

    def add_implementation(self, implementation, entity_type_name):
        """Add an internal python implementation object, it will be wrapped with an entity adapter"""
        self.entities.add_implementation(
            PythonImplementation(self.entity_type(entity_type_name), implementation))

    def remove_implementation(self, implementation):
        """Remove and internal python implementation object."""
        self.entities.remove_implementation(id(implementation))

    def find_entity(self, request):
        """Find the entity addressed by request"""

        requested_type = request.properties.get('type')
        if requested_type:
            requested_type = self.schema.entity_type(requested_type)
        # ids is a map of identifying attribute values
        ids = dict((k, request.properties.get(k))
                   for k in ['name', 'identity'] if k in request.properties)

        # Special case for management object: if no name/id and no conflicting type
        # then assume this is for "self"
        if not ids:
            if not requested_type or self.management.entity_type.is_a(requested_type):
                return self.management
            else:
                raise BadRequestStatus("%s: No name or identity provided" % requested_type)

        def attrvals():
            """String form of the id attribute values for error messages"""
            return " ".join(["%s=%s" % (k, v) for k, v in dict_iteritems(ids)])

        k, v = next(dict_iteritems(ids)) # Get the first id attribute
        found = self.entities.map_filter(None, lambda e: e.attributes.get(k) == v)
        if len(found) == 1:
            entity = found[0]
        elif len(found) > 1:
            raise InternalServerErrorStatus(
                "Duplicate (%s) entities with %s=%s" % (len(found), k, v))
        else:
            raise NotFoundStatus("No entity with %s" % attrvals())

        for k, v in dict_iteritems(ids):
            if entity[k] != v:
                raise BadRequestStatus("Conflicting %s" % attrvals())

        if requested_type:
            if not entity.entity_type.is_a(requested_type):
                raise BadRequestStatus("Entity type '%s' does not extend requested type '%s'" %
                                       (entity.entity_type.name, requested_type))

        return entity

    def find_entity_by_type(self, type):
        return self.entities.map_type(None, type)

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
AMQP management client for Qpid dispatch.
"""

import proton, re, threading
from .error import *
from .entity import Entity as BaseEntity, clean_dict

class Url(object):
    """Simple AMQP URL parser/constructor"""

    URL_RE = re.compile(r"""
    # [   <scheme>://  ] [   <user>   [    : <password> ]  @]      ( <host4>   | \[    <host6>    \] ) [   :<port>   ]   [ / path]
    ^ (?: ([^:/@]+)://)? (?: ([^:/@]+) (?: : ([^:/@]+)  )? @)? (?: ([^@:/\[]+) | \[ ([a-f0-9:.]+) \] ) (?: :([0-9]+))? (?: / (.*))? $
""", re.X | re.I)
    AMQPS = "amqps"
    AMQP = "amqp"

    def __init__(self, url=None, **kwargs):
        """
        @param url: String or Url instance to parse or copy.
        @param kwargs: URL fields: scheme, user, password, host, port, path.
            If specified, replaces corresponding component in url.
        """

        fields = ['scheme', 'user', 'password', 'host', 'port', 'path']

        for f in fields: setattr(self, f, None)
        for k in kwargs: getattr(self, k) # Check for invalid kwargs

        if isinstance(url, Url): # Copy from another Url instance.
            self.__dict__.update(url.__dict__)

        elif url is not None:   # Parse from url
            match = Url.URL_RE.match(url)
            if match is None:
                raise ValueError("Invalid AMQP URL: %s"%url)
            self.scheme, self.user, self.password, host4, host6, port, self.path = match.groups()
            self.host = host4 or host6
            self.port = port and int(port)

        # Let kwargs override values previously set from url
        for field in fields:
            setattr(self, field, kwargs.get(field, getattr(self, field)))

    def __repr__(self):
        return "Url(%r)" % str(self)

    def __str__(self):
        s = ""
        if self.scheme:
            s += "%s://" % self.scheme
        if self.user:
            s += self.user
        if self.password:
            s += ":%s@" % self.password
        if self.host and ':' in self.host:
            s += "[%s]" % self.host
        else:
            s += self.host or '0.0.0.0'
        if self.port:
            s += ":%s" % self.port
        if self.path:
            s += "/%s" % self.path
        return s

    def __eq__(self, url):
        if isinstance(url, basestring):
            url = Url(url)
        return \
            self.scheme == url.scheme and \
            self.user == url.user and self.password == url.password and \
            self.host == url.host and self.port == url.port and \
            self.path == url.path

    def __ne__(self, url):
        return not self.__eq__(url)

    def defaults(self):
        """"Fill in defaults for scheme and port if missing """
        if not self.scheme: self.scheme = self.AMQP
        if not self.port:
            if self.scheme == self.AMQP: self.port = 5672
            elif self.scheme == self.AMQPS: self.port = 5671
            else: raise ValueError("Invalid URL scheme: %s"%self.scheme)
        return self

class MessengerImpl(object):
    """
    Messaging implementation for L{Node} based on proton.Messenger
    @ivar reply_to: address for replies to the node.
    """

    def __init__(self, address, timeout=None):
        self.messenger = proton.Messenger()
        self.messenger.start()
        self.messenger.timeout = timeout
        subscribe_address = Url(address)
        subscribe_address.path = "#"
        self.subscription = self.messenger.subscribe(str(subscribe_address))
        self._flush()
        self.reply_to = self.subscription.address
        if not self.reply_to:
            raise ValueError("Failed to subscribe to %s"%subscribe_address)

    def send(self, request):
        """Send a message"""
        self.messenger.put(request)
        self.messenger.send()
        self._flush()

    def fetch(self):
        """Wait for a single message."""
        self.messenger.recv(1)
        response = proton.Message()
        self.messenger.get(response)
        return response

    def _flush(self):
        """Call self.messenger.work() till there is no work left."""
        while self.messenger.work(0.1): pass

    def stop(self):
        """Stop the messaging implementation"""
        self.messenger.stop()


class Entity(BaseEntity):
    """
    Proxy for an AMQP manageable entity.

    Modifying local attributes dict will not change the remote entity. Call
    update() to send the local attributes to the remote entity.

    The standard AMQP requests read, update and delete are defined
    here. create() is defined on L{Node}.

    Attribute access:
    - via index operator: entity['foo']
    - as python attributes: entity.foo (only if attribute name is a legal python identitfier)

    @ivar attributes: Map of attribute values for this entity.
    """

    def __init__(self, node, attributes=None, **kwattrs):
        super(Entity, self).__init__(attributes, **kwattrs)
        self.__dict__['_node'] = node # Avoid getattr recursion

    def call(self, operation, expect=OK, **arguments):
        """Call an arbitrary management method on this entity"""
        request = self._node.request(
            operation=operation, type=self.type, identity=self.identity, **arguments)
        return self._node.call(request, expect=expect).body

    def read(self):
        """Read the remote entity attributes into the local attributes."""
        self.attributes = self.call('READ', expect=OK)

    def update(self):
        """Update the remote entity attributes from the local attributes."""
        self.attributes = self.call('UPDATE', expect=OK, body=self.attributes)

    def delete(self):
        """Delete the remote entity"""
        self.call('DELETE', expect=NO_CONTENT)


class Node(object):
    """Client proxy for an AMQP management node"""

    def __init__(self, address=None, router=None, locales=None, timeout=10, message_impl=None):
        """
        @param address: AMQP address of the management node.
        @param router: If address does not contain a path, use the management node for this router ID.
            If not specified and address does not contain a path, use the default management node.
        @param locales: Default list of locales for management operations.
        """
        self.name = self.identity = 'self'
        self.type = 'org.amqp.management' # AMQP management node type

        self.address = Url(address).defaults()
        self.locales = locales
        if self.address.path is None:
            if router:
                self.address.path = '_topo/0/%s/$management' % router
            else:
                self.address.path = '$management'
        self.responses = {}
        self.message_impl = message_impl or MessengerImpl(self.address, timeout=timeout)
        self.reply_to = self.message_impl.reply_to

    def stop(self):
        """Shut down the node"""
        if not self.message_impl: return
        self.message_impl.stop()
        self.message_impl = None

    def __del__(self):
        if hasattr(self, 'message_impl'):
            try: self.stop()
            except: pass

    def __repr__(self):
        return "%s(%s)"%(self.__class__.__name__, self.address)

    CORRELATION_ID = 0
    CORRELATION_LOCK = threading.Lock()

    def correlation_id(self):
        """Get the next correlation ID. Thread safe."""
        with self.CORRELATION_LOCK:
            Node.CORRELATION_ID += 1
            return Node.CORRELATION_ID

    @staticmethod
    def check_response(response, request, expect=OK):
        """
        Check a management response message for errors and correlation ID.
        """
        code = response.properties.get('statusCode')
        if code != expect:
            if 200 <= code <= 299:
                raise ValueError("Response was %s(%s) but expected %s(%s): %s" % (
                    code, STATUS_TEXT[code], expect, STATUS_TEXT[expect],
                    response.properties.get('statusDescription')))
            else:
                raise ManagementError.create(code, response.properties.get('statusDescription'))
        if response.correlation_id != request.correlation_id:
            raise NotFoundStatus("Bad correlation id request=%s, response=%s" %
                               (request.correlation_id, response.correlation_id))


    def request(self, body=None, **properties):
        """
        Make a L{proton.Message} containining a management request.
        @param body: The request body, a dict or list.
        @param properties: Keyword arguments for application-properties of the request.
        @return: L{proton.Message} containining the management request.
        """
        if self.locales: properties.setdefault('locales', self.locales)
        request = proton.Message()
        request.address = str(self.address)
        request.reply_to = self.reply_to
        request.correlation_id = self.correlation_id()
        request.properties = clean_dict(properties)
        request.body = body or {}
        return request

    def node_request(self, body=None, **properties):
        """Construct a request for the managment node itself"""
        return self.request(body, name=self.name, type=self.type, **properties)

    def call(self, request, expect=OK):
        """
        Send a management request message, wait for a response.
        @return: Response message.
        """
        if not request.address:
            raise ValueError("Message must have an address")
        if not request.reply_to:
            raise ValueError("Message must have reply_to %s", request)
        self.message_impl.send(request)
        response = self.message_impl.fetch()
        self.check_response(response, request, expect=expect)
        return response

    class QueryResponse(object):
        """
        Result returned by L{query}.
        @ivar attribute_names: List of attribute names for the results.
        @ivar results: list of lists of attribute values in same order as attribute_names
        """
        def __init__(self, node, attribute_names, results):
            """
            @param response: the respose message to a query.
            """
            self.node = node
            self.attribute_names = attribute_names
            self.results = results

        def iter_dicts(self, clean=False):
            """
            Return an iterator that yields a dictionary for each result.
            @param clean: if True remove any None values from returned dictionaries.
            """
            for r in self.results:
                if clean: yield clean_dict(zip(self.attribute_names, r))
                else: yield dict(zip(self.attribute_names, r))

        def iter_entities(self, clean=False):
            """
            Return an iterator that yields an L{Entity} for each result.
            @param clean: if True remove any None values from returned dictionaries.
            """
            for d in self.iter_dicts(clean=clean): yield Entity(self.node, d)

        def get_dicts(self, clean=False):
            """Results as list of dicts."""
            return [d for d in self.iter_dicts(clean=clean)]

        def get_entities(self, clean=False):
            """Results as list of entities."""
            return [d for d in self.iter_entities(clean=clean)]

        def __repr__(self):
            return "QueryResponse(attribute_names=%r, results=%r"%(self.attribute_names, self.results)

    def query(self, type=None, attribute_names=None, offset=None, count=None):
        """
        Send an AMQP management query message and return the response.
        At least one of type, attribute_names must be specified.
        @keyword type: The type of entity to query.
        @keyword attribute_names: A list of attribute names to query.
        @keyword offset: An integer offset into the list of results to return.
        @keyword count: A count of the maximum number of results to return.
        @return: A L{QueryResponse}
        """
        request = self.node_request(
            {'attributeNames': attribute_names or []},
            operation='QUERY', entityType=type, offset=offset, count=count)

        response = self.call(request)
        return Node.QueryResponse(self, response.body['attributeNames'], response.body['results'])

    def create(self, attributes=None, type=None, name=None):
        """
        Create an entity.
        type and name can be specified in the attributes.

        @param attributes: Attributes for the new entity.
        @param type: Type of entity to create.
        @param name: Name for the new entity.
        @return: Entity proxy for the new entity.
        """
        attributes = attributes or {}
        type = type or attributes.get('type')
        name = name or attributes.get('name')
        request = self.request(operation='CREATE', type=type, name=name, body=attributes)
        return Entity(self, self.call(request, expect=CREATED).body)

    def read(self, type=None, name=None, identity=None):
        """
        Read an AMQP entity.
        If both name and identity are specified, only identity is used.

        @param type: Entity type.
        @param name: Entity name.
        @param identity: Entity identity.
        @return: An L{Entity}
        """
        if name and identity: name = None # Only specify one
        request = self.request(operation='READ', type=type, name=name, identity=identity)
        return Entity(self, self.call(request).body)

    def update(self, attributes, type=None, name=None, identity=None):
        """
        Update an entity with attributes.
        type, name and identity can be specified in the attributes.
        If both name and identity are specified, only identity is used.

        @param attributes: Attributes for the new entity.
        @param type: Entity type.
        @param name: Entity name.
        @param identity: Entity identity.
        @return: L{Entity} for the updated entity.

        """
        attributes = attributes or {}
        type = type or attributes.get('type')
        name = name or attributes.get('name')
        identity = identity or attributes.get('identity')
        if name and identity: name = None # Only send one
        request = self.request(operation='UPDATE', type=type, name=name,
                               identity=identity, body=attributes)
        return Entity(self, self.call(request).body)

    def delete(self, type=None, name=None, identity=None):
        """
        Delete the remote entity.
        If both name and identity are specified, only identity is used.

        @param type: Entity type.
        @param name: Entity name.
        @param identity: Entity identity.
        """
        if name and identity: name = None # Only specify one
        request = self.request(operation='DELETE', type=type, name=name,
                               identity=identity)
        self.call(request, expect=NO_CONTENT)

    def get_types(self, type=None):
        return self.call(self.node_request(operation="GET-TYPES", entityType=type)).body

    def get_attributes(self, type=None):
        return self.call(self.node_request(operation="GET-ATTRIBUTES", entityType=type)).body

    def get_operations(self, type=None):
        return self.call(self.node_request(operation="GET-OPERATIONS", entityType=type)).body

    def get_mgmt_nodes(self):
        return self.call(self.node_request(operation="GET-MGMT-NODES")).body


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

from __future__ import unicode_literals
from __future__ import division
from __future__ import absolute_import
from __future__ import print_function

import qpid_dispatch_site
import proton
from proton import Url
from .error import *        # import all error symbols for convenience to users.
from .entity import EntityBase, clean_dict
from proton.utils import SyncRequestResponse, BlockingConnection

class Entity(EntityBase):
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
        self.attributes = self.call(u'READ', expect=OK)

    def update(self):
        """Update the remote entity attributes from the local attributes."""
        self.attributes = self.call(u'UPDATE', expect=OK, body=self.attributes)

    def delete(self):
        """Delete the remote entity"""
        self.call(u'DELETE', expect=NO_CONTENT)


class Node(object):
    """Client proxy for an AMQP management node"""

    def clean_attrs(self, attrs):
        BOOL_VALUES = {"yes"  : True,
                       "true" : True,
                       "on"   : True,
                       "no"   : False,
                       "false": False,
                       "off"  : False}
        if isinstance(attrs, dict):
            for key in attrs.keys():
                if isinstance(attrs[key], str) and attrs[key] in BOOL_VALUES.keys():
                    attrs[key] = BOOL_VALUES[attrs[key]]

        return attrs

    @staticmethod
    def connection(url=None, router=None, timeout=10, ssl_domain=None, sasl=None, edge_router=None):
        """Return a BlockingConnection suitable for connecting to a management node
        @param url: URL of the management node.
        @param router: If address does not contain a path, use the management node for this router ID.
            If not specified and address does not contain a path, use the default management node.
        """
        url = Url(url)          # Convert string to Url class.

        if url.path is None:
            if router:
                url.path = u'_topo/0/%s/$management' % router
            elif edge_router:
                url.path = u'_edge/%s/$management' % edge_router
            else:
                url.path = u'$management'

        if ssl_domain:
            sasl_enabled = True
        else:
            sasl_enabled = True if sasl else False

        # if sasl_mechanism is unicode, convert it to python string
        return BlockingConnection(url,
                                  timeout=timeout,
                                  ssl_domain=ssl_domain,
                                  sasl_enabled=sasl_enabled,
                                  allowed_mechs=str(sasl.mechs) if sasl and sasl.mechs != None else None,
                                  user=str(sasl.user) if sasl and sasl.user != None else None,
                                  password=str(sasl.password) if sasl and sasl.password != None else None)

    @staticmethod
    def connect(url=None, router=None, timeout=10, ssl_domain=None, sasl=None,
                edge_router=None):
        """Return a Node connected with the given parameters, see L{connection}"""
        return Node(Node.connection(url, router, timeout, ssl_domain, sasl,
                                    edge_router=edge_router))

    def __init__(self, connection, locales=None):
        """
        Create a management node proxy using the given connection.
        @param locales: Default list of locales for management operations.
        @param connection: a L{BlockingConnection} to the management agent.
        """
        self.name = self.identity = u'self'
        self.type = u'org.amqp.management' # AMQP management node type
        self.locales = locales

        self.locales = locales
        self.url = connection.url
        self.client = SyncRequestResponse(connection, self.url.path)
        self.reply_to = self.client.reply_to
        self.connection = connection

    def set_client(self, url_path):
        if url_path:
            self.url.path = u'%s'%url_path
            self.client = SyncRequestResponse(self.connection, self.url.path)

    def close(self):
        """Shut down the node"""
        if self.client:
            self.client.connection.close()
            self.client = None

    def __repr__(self):
        return "%s(%s)"%(self.__class__.__name__, self.url)

    @staticmethod
    def check_response(response, expect=OK):
        """
        Check a management response message for errors and correlation ID.
        """
        code = response.properties.get(u'statusCode')
        if code != expect:
            if 200 <= code <= 299:
                raise ValueError("Response was %s(%s) but expected %s(%s): %s" % (
                    code, STATUS_TEXT[code], expect, STATUS_TEXT[expect],
                    response.properties.get(u'statusDescription')))
            else:
                raise ManagementError.create(code, response.properties.get(u'statusDescription'))

    def request(self, body=None, **properties):
        """
        Make a L{proton.Message} containining a management request.
        @param body: The request body, a dict or list.
        @param properties: Keyword arguments for application-properties of the request.
        @return: L{proton.Message} containining the management request.
        """
        if self.locales:
            properties.setdefault(u'locales', self.locales)
        request = proton.Message()
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
        response = self.client.call(request)
        self.check_response(response, expect=expect)
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
                if clean:
                    yield clean_dict(zip(self.attribute_names, r))
                else:
                    yield dict(zip(self.attribute_names, r))

        def iter_entities(self, clean=False):
            """
            Return an iterator that yields an L{Entity} for each result.
            @param clean: if True remove any None values from returned dictionaries.
            """
            for d in self.iter_dicts(clean=clean):
                yield Entity(self.node, d)

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

        # There is a bug in proton (PROTON-1846) wherein we cannot ask for
        # too many rows. So, as a safety we are going to ask only for
        # MAX_ALLOWED_COUNT_PER_REQUEST. Since this is used by both qdstat
        # and qdmanage, we have determined that the optimal value for
        # MAX_ALLOWED_COUNT_PER_REQUEST is 500
        MAX_ALLOWED_COUNT_PER_REQUEST = 500

        response_results = []
        response_attr_names = []
        if offset is None:
            offset = 0

        if count is None or count==0:
            # count has not been specified. For each request the
            # maximum number of rows we can get without proton
            # failing is MAX_ALLOWED_COUNT_PER_REQUEST
            request_count = MAX_ALLOWED_COUNT_PER_REQUEST
        else:
            request_count = min(MAX_ALLOWED_COUNT_PER_REQUEST, count)

        while True:
            request = self.node_request(
                {u'attributeNames': attribute_names or []},
                operation=u'QUERY', entityType=type, offset=offset,
                count=request_count)

            response = self.call(request)

            if not response_attr_names:
                response_attr_names += response.body[u'attributeNames']

            response_results += response.body[u'results']

            if len(response.body[u'results']) < request_count:
                break

            if count:
                len_response_results = len(response_results)
                if count == len_response_results:
                    break

                if count - len_response_results < request_count:
                    request_count = count - len_response_results

            offset += request_count

        query_reponse = Node.QueryResponse(self,
                                           response_attr_names,
                                           response_results)
        return query_reponse

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
        type = type or attributes.get(u'type')
        name = name or attributes.get(u'name')
        request = self.request(operation=u'CREATE', type=type, name=name, body=attributes)
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
        if name and identity:
            name = None # Only specify one
        request = self.request(operation=u'READ', type=type, name=name, identity=identity)
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
        type = type or attributes.get(u'type')
        name = name or attributes.get(u'name')
        identity = identity or attributes.get(u'identity')
        if name and identity:
            name = None # Only send one
        request = self.request(operation=U'UPDATE', type=type, name=name,
                               identity=identity, body=self.clean_attrs(attributes))
        return Entity(self, self.call(request).body)

    def delete(self, type=None, name=None, identity=None):
        """
        Delete the remote entity.
        If both name and identity are specified, only identity is used.

        @param type: Entity type.
        @param name: Entity name.
        @param identity: Entity identity.
        """
        if name and identity:
            name = None # Only specify one
        request = self.request(operation=U'DELETE', type=type, name=name,
                               identity=identity)
        self.call(request, expect=NO_CONTENT)

    def get_types(self, type=None):
        return self.call(self.node_request(operation=u"GET-TYPES", entityType=type)).body

    def get_annotations(self, type=None):
        return self.call(self.node_request(operation=u"GET-ANNOTATIONS", entityType=type)).body

    def get_attributes(self, type=None):
        return self.call(self.node_request(operation=u"GET-ATTRIBUTES", entityType=type)).body

    def get_operations(self, type=None):
        return self.call(self.node_request(operation=u"GET-OPERATIONS", entityType=type)).body

    def get_mgmt_nodes(self, type=None):
        return self.call(self.node_request(operation=u"GET-MGMT-NODES", entityType=type)).body

    def get_log(self, limit=None, type=None):
        return self.call(self.node_request(operation=u"GET-LOG", entityType=type, limit=limit)).body

    def get_schema(self, type=None):
        return self.call(self.node_request(operation=u"GET-SCHEMA")).body

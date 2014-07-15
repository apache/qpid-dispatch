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
AMQP management tools for Qpid dispatch.
"""

import proton, re, threading, httplib
from entity import Entity, EntityList

class ManagementError(Exception):
    """An AMQP management error. str() gives a string with status code and text.
    @ivar status: integer status code
    @ivar description: description text
    """
    def __init__(self, status, description):
        self.status, self.description = status, description

    def __str__(self):
        status_str = httplib.responses.get(self.status)
        if status_str in self.description: return self.description
        else: return "%s: %s"%(status_str, self.description)

    @classmethod
    def is_ok(cls, status):
        return status == httplib.OK

    @classmethod
    def raise_if(cls, status, response):
        if not cls.is_ok(status):
            raise ManagementError(status, response)


# TODO aconway 2014-06-03: proton URL class, conditional import?
class Url:
    """Simple AMQP URL parser/constructor"""

    RE = re.compile(r"""
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
        for field in fields: setattr(self, field, None)

        if isinstance(url, Url): # Copy from url
            for field in fields:
                setattr(self, field, getattr(url, field))
        elif url is not None: # Parse from url
            match = Url.RE.match(url)
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
             self.scheme==url.scheme and \
             self.user==url.user and self.password==url.password and \
             self.host==url.host and self.port==url.port and \
             self.path==url.path

    def __ne__(self, url):
         return not self.__eq__(url)

    def defaults(self):
        """"Fill in defaults for scheme and port if missing """
        if not self.scheme: self.scheme = self.AMQP
        if not self.port:
            if self.scheme==self.AMQP: self.port = 5672
            elif self.scheme==self.AMQPS: self.port = 5671
            else: raise ValueError("Invalid URL scheme: %s"%self.scheme)
        return self

def remove_none(d):
    """
    Remove any None values from a dictionary. Does not modify d.
    """
    return dict((k, v) for k, v in d.iteritems() if v is not None)

class Node(object):

    SELF='self'                 # AMQP management node name
    NODE_TYPE='org.amqp.management' # AMQP management node type
    NODE_PROPERTIES={'name':SELF, 'type':NODE_TYPE}

    def __init__(self, address=None, router=None, locales=None, timeout=10):
        """
        @param address: AMQP address of the management node.
        @param router: If address does not contain a path, use the management node for this router ID.
            If not specified and address does not contain a path, use the default management node.
        @param locales: Default list of locales for management operations.
        """
        self.address = Url(address).defaults()
        self.locales = locales
        if self.address.path is None:
            if router:
                self.address.path = '_topo/0/%s/$management' % router
            else:
                self.address.path = '$management'
        self.responses = {}

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

    def stop(self):
        if not self.messenger: return
        self.messenger.stop()
        self.messenger = None

    def __del__(self):
        if hasattr(self, 'messenger'):
            try: self.stop()
            except: pass

    def _flush(self):
        """Call self.messenger.work() till there is no work left."""
        while self.messenger.work(0.1):
            pass

    def __repr__(self):
        return "%s(%s)"%(self.__class__.__name__, self.address)

    CORRELATION_ID = 0
    CORRELATION_LOCK = threading.Lock()

    def correlation_id(self):
        """Get the next correlation ID. Thread safe."""
        with self.CORRELATION_LOCK:
            self.CORRELATION_ID += 1
            return self.CORRELATION_ID

    def check_response(self, response, request):
        """
        Check a manaement response message for errors and correlation ID.
        """
        properties = response.properties
        ManagementError.raise_if(properties.get('statusCode'), properties.get('statusDescription'))
        if response.correlation_id != request.correlation_id:
            raise ManagementError(httplib.NOT_FOUND, "Bad correlation id request=%s, response=%s"%(
                request.correlation_id, response.correlation_id))


    def request(self, body=None, **properties):
        """
        Make a L{proton.Message} containining a management request.
        @param body: The request body, a dict or list.
        @param properties: Keyword arguments for application-properties of the request.
        @return: L{proton.Message} containining the management request.
        """
        if self.locales: properties.setdefault('locales', self.locales)
        request = proton.Message()
        request.address=str(self.address)
        request.reply_to=self.reply_to
        request.correlation_id=self.correlation_id()
        request.properties=remove_none(properties)
        request.body=remove_none(body or {})
        return request

    def node_request(self, body=None, **properties):
        return self.request(body, name=self.SELF, type=self.NODE_TYPE, **properties)

    def op_request(self, operation, type, name, body=None, **properties):
        return self.request(body,  name=name, type=type, operation=operation, **properties)

    # TODO aconway 2014-06-03: async send/receive
    def call(self, request):
        """
        Send a management request message, wait for a response.
        @return: Response message.
        """
        if not request.address:
            raise ValueError("Message must have an address")
        if not request.reply_to:
            raise ValueError("Message must have reply_to %s", request)
        self.messenger.put(request)
        self.messenger.send()
        self._flush()
        self.messenger.recv(1)
        response = proton.Message()
        self.messenger.get(response)
        self.check_response(response, request)
        return response

    class QueryResponse(EntityList):
        """
        Result returned by L{query}.
        @ivar attribute_names: List of attribute names for the results.
        """
        def __init__(self, response):
            """
            @param response: the respose message to a query.
            """
            self.attribute_names = response.body['attributeNames']
            for r in response.body['results']:
                self.append(Entity(zip(self.attribute_names, r)))

    def query(self, type=None, attribute_names=None, offset=None, count=None):
        """
        Send an AMQP management query message and return the response.
        At least one of type, attribute_names must be specified.
        @keyword type: The type of entity to query.
        @keyword attribute_names: A list of attribute names to query.
        @keyword offset: An integer offset into the list of results to return.
        @keyword count: A count of the maximum number of results to return.
        @return: An L{EntityList}
        """
        attribute_names = attribute_names or []
        request = self.node_request(
            {'attributeNames':attribute_names},
            operation='QUERY', entityType=type, offset=offset, count=count)

        response = self.call(request)
        return Node.QueryResponse(response)

    def create(self, type, name, attributes=None):
        """
        Send an AMQP management create message and return the response.
        @param type: Type of entity to create.
        @param name: Name for the new entity.
        @param attributes: Map of attribute name:value for the new entity.
        @return: Map of actual attributes returned by create (with defaults etc.)
        """
        request = self.op_request(operation='CREATE', type=type, name=name, body=attributes or {})
        response = self.call(request)
        return response.body

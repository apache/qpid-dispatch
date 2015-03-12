
Management Schema
=================


This chapter documents the set of *management entity types* that define
configuration and management of a Dispatch Router. A management entity type has
a set of *attributes* that can be read, some attributes can also be
updated. Some entity types also support *operations* that can be called.

All management entity types have the following standard attributes:

*type*
  The fully qualified type of the entity,
  e.g. `org.apache.qpid.dispatch.router`. This document uses the short name
  without the `org.apache.qpid.dispatch` prefix e.g. `router`. The dispatch
  tools will accept the short or long name.

*identity*
  A system-generated identity of the entity. It includes
  the short type name and some identifying information. E.g. `log/AGENT` or
  `listener/localhost:amqp`

There are two main categories of management entity type.

*Configuration* Entities
  Parameters that can be set in the configuration file
  (see `qdrouterd.conf(5)` man page) or set at run-time with the `qdmanage(8)`
  tool.

*Operational* Entities
   Run-time status values that can be queried using `qdstat(8)` or `qdmanage(8)` tools.


Configuration Entities
----------------------


Configuration entities define the attributes allowed in the configuration file
(see `qdrouterd.conf(5)`) but you can also create entities once the router is
running using the `qdrouterd(8)` tool's `create` operation. Some entities can also
be modified using the `update` operation, see the entity descriptions below.


container
+++++++++

Attributes related to the AMQP container.

Operations allowed: `READ`



*containerName* (string, `CREATE`)
  The  name of the AMQP container.  If not specified, the container name will be set to a value of the container's choosing.  The automatically assigned container name is not guaranteed to be persistent across restarts of the container.

*workerThreads* (integer, default=1, `CREATE`)
  The number of threads that will be created to process message traffic and other application work (timers, non-amqp file descriptors, etc.) .

*debugDump* (path, `CREATE`)
  A file to dump debugging information that can't be logged normally.


router
++++++

Tracks peer routers and computes routes to destinations.

Operations allowed: `READ`



*routerId* (string, `CREATE`)
  Router's unique identity.

*mode* (One of ['standalone', 'interior', 'edge', 'endpoint'], default='standalone', `CREATE`)
  In standalone mode, the router operates as a single component.  It does not participate in the routing protocol and therefore will not cooperate with other routers. In interior mode, the router operates in cooperation with other interior routers in an interconnected network.  In edge mode, the router operates with an up link into an interior router network. Edge routers are typically used as connection concentrators or as security firewalls for access into the interior network.

*area* (string)
  Unused placeholder.

*helloInterval* (integer, default=1, `CREATE`)
  Interval in seconds between HELLO messages sent to neighbor routers.

*helloMaxAge* (integer, default=3, `CREATE`)
  Time in seconds after which a neighbor is declared lost if no HELLO is received.

*raInterval* (integer, default=30, `CREATE`)
  Interval in seconds between Router-Advertisements sent to all routers in a stable network.

*raIntervalFlux* (integer, default=4, `CREATE`)
  Interval in seconds between Router-Advertisements sent to all routers during topology fluctuations.

*remoteLsMaxAge* (integer, default=60, `CREATE`)
  Time in seconds after which link state is declared stale if no RA is received.

*mobileAddrMaxAge* (integer, default=60, `CREATE`)
  Deprecated - This value is no longer used in the router.

*addrCount* (integer)
  Number of addresses known to the router.

*linkCount* (integer)
  Number of links attached to the router node.

*nodeCount* (integer)
  Number of known peer router nodes.


listener
++++++++

Listens for incoming connections to the router.

Operations allowed: `CREATE`, `READ`



*addr* (string, default='0.0.0.0', `CREATE`)
  IP address: ipv4 or ipv6 literal or a host name.

*port* (string, default='amqp', `CREATE`)
  Port number or symbolic service name.

*role* (One of ['normal', 'inter-router', 'on-demand'], default='normal', `CREATE`)
  The role of an established connection. In the normal role, the connection is assumed to be used for AMQP clients that are doing normal message delivery over the connection.  In the inter-router role, the connection is assumed to be to another router in the network.  Inter-router discovery and routing protocols can only be used over inter-router connections.

*certDb* (path, `CREATE`)
  The path to the database that contains the public certificates of trusted certificate authorities (CA).

*certFile* (path, `CREATE`)
  The path to the file containing the PEM-formatted public certificate to be used on the local end of any connections using this profile.

*keyFile* (path, `CREATE`)
  The path to the file containing the PEM-formatted private key for the above certificate.

*passwordFile* (path, `CREATE`)
  If the above private key is password protected, this is the path to a file containing the password that unlocks the certificate key.

*password* (string, `CREATE`)
  An alternative to storing the password in a file referenced by passwordFile is to supply the password right here in the configuration file.  This option can be used by supplying the password in the 'password' option.  Don't use both password and passwordFile in the same profile.

*saslMechanisms* (string, required, `CREATE`)
  Comma separated list of accepted SASL authentication mechanisms.

*requirePeerAuth* (boolean, default=True, `CREATE`)
  Only for listeners using SSL.  If set to 'yes', attached clients will be required to supply a certificate.  If the certificate is not traceable to a CA in the ssl profile's cert-db, authentication fails for the connection.

*trustedCerts* (path, `CREATE`)
  This optional setting can be used to reduce the set of available CAs for client authentication.  If used, this setting must provide a path to a PEM file that contains the trusted certificates.

*allowUnsecured* (boolean, `CREATE`)
  For listeners using SSL only.  If set to 'yes' the listener will allow both SSL-secured clients and non-SSL clients to connect.

*allowNoSasl* (boolean, `CREATE`)
  If set to 'yes', this option causes the listener to allow clients to connect even if they skip the SASL authentication protocol.

*maxFrameSize* (integer, default=65536, `CREATE`)
  Defaults to 65536.  If specified, it is the maximum frame size in octets that will be used in the connection-open negotiation with a connected peer.  The frame size is the largest contiguous set of uninterrupted data that can be sent for a message delivery over the connection. Interleaving of messages on different links is done at frame granularity.


connector
+++++++++

Establishes an outgoing connections from the router.

Operations allowed: `CREATE`, `READ`



*addr* (string, default='0.0.0.0', `CREATE`)
  IP address: ipv4 or ipv6 literal or a host name.

*port* (string, default='amqp', `CREATE`)
  Port number or symbolic service name.

*role* (One of ['normal', 'inter-router', 'on-demand'], default='normal', `CREATE`)
  The role of an established connection. In the normal role, the connection is assumed to be used for AMQP clients that are doing normal message delivery over the connection.  In the inter-router role, the connection is assumed to be to another router in the network.  Inter-router discovery and routing protocols can only be used over inter-router connections.

*certDb* (path, `CREATE`)
  The path to the database that contains the public certificates of trusted certificate authorities (CA).

*certFile* (path, `CREATE`)
  The path to the file containing the PEM-formatted public certificate to be used on the local end of any connections using this profile.

*keyFile* (path, `CREATE`)
  The path to the file containing the PEM-formatted private key for the above certificate.

*passwordFile* (path, `CREATE`)
  If the above private key is password protected, this is the path to a file containing the password that unlocks the certificate key.

*password* (string, `CREATE`)
  An alternative to storing the password in a file referenced by passwordFile is to supply the password right here in the configuration file.  This option can be used by supplying the password in the 'password' option.  Don't use both password and passwordFile in the same profile.

*saslMechanisms* (string, required, `CREATE`)
  Comma separated list of accepted SASL authentication mechanisms.

*allowRedirect* (boolean, default=True, `CREATE`)
  Allow the peer to redirect this connection to another address.

*maxFrameSize* (integer, default=65536, `CREATE`)
  Maximum frame size in octets that will be used in the connection-open negotiation with a connected peer.  The frame size is the largest contiguous set of uninterrupted data that can be sent for a message delivery over the connection. Interleaving of messages on different links is done at frame granularity.


log
+++

Configure logging for a particular module. You can use the `UPDATE` operation to change log settings while the router is running.

Operations allowed: `UPDATE`, `READ`



*module* (One of ['ROUTER', 'ROUTER_HELLO', 'ROUTER_LS', 'ROUTER_MA', 'MESSAGE', 'SERVER', 'AGENT', 'CONTAINER', 'CONFIG', 'ERROR', 'DISPATCH', 'DEFAULT'], required)
  Module to configure. The special module 'DEFAULT' specifies defaults for all modules.

*enable* (string, default='default', required, `UPDATE`)
  Levels are: trace, debug, info, notice, warning, error, critical. The enable string is a comma-separated list of levels. A level may have a trailing '+' to enable that level and above. For example 'trace,debug,warning+' means enable trace, debug, warning, error and critical. The value 'none' means disable logging for the module. The value 'default' means use the value from the DEFAULT module.

*timestamp* (boolean, `UPDATE`)
  Include timestamp in log messages.

*source* (boolean, `UPDATE`)
  Include source file and line number in log messages.

*output* (string, `UPDATE`)
  Where to send log messages. Can be 'stderr', 'syslog' or a file name.


fixedAddress
++++++++++++

Establishes semantics for addresses starting with a prefix.

Operations allowed: `CREATE`, `READ`



*prefix* (string, required, `CREATE`)
  The address prefix (always starting with '/').

*phase* (integer, `CREATE`)
  The phase of a multi-hop address passing through one or more waypoints.

*fanout* (One of ['multiple', 'single'], default='multiple', `CREATE`)
  One of 'multiple' or 'single'.  Multiple fanout is a non-competing pattern.  If there are multiple consumers using the same address, each consumer will receive its own copy of every message sent to the address.  Single fanout is a competing pattern where each message is sent to only one consumer.

*bias* (One of ['closest', 'spread'], default='closest', `CREATE`)
  Only if fanout is single.  One of 'closest' or 'spread'.  Closest bias means that messages to an address will always be delivered to the closest (lowest cost) subscribed consumer. Spread bias will distribute the messages across subscribers in an approximately even manner.


waypoint
++++++++

A remote node that messages for an address pass through.

Operations allowed: `CREATE`, `READ`



*address* (string, required, `CREATE`)
  The AMQP address of the waypoint.

*connector* (string, required, `CREATE`)
  The name of the on-demand connector used to reach the waypoint's container.

*inPhase* (integer, default=-1, `CREATE`)
  The phase of the address as it is routed _to_ the waypoint.

*outPhase* (integer, default=-1, `CREATE`)
  The phase of the address as it is routed _from_ the waypoint.


linkRoutePattern
++++++++++++++++

A pattern to match a connected container to endpoints for routed links.

Operations allowed: `CREATE`, `READ`



*prefix* (string, required, `CREATE`)
  The AMQP address prefix for nodes on the container.

*connector* (string, `CREATE`)
  The name of the on-demand connector used to reach the waypoint's container.


Operational Entities
--------------------


Operational entities provide statistics and other run-time attributes of the router.
The `qdstat(8)` tool provides a convenient way to query run-time statistics.
You can also use the general-purpose management tool `qdmanage(8)` to query
operational attributes.


org.amqp.management
+++++++++++++++++++

The standard AMQP management node interface.

Operations allowed: `QUERY`, `GET-TYPES`, `GET-ANNOTATIONS`, `GET-OPERATIONS`, `GET-ATTRIBUTES`, `GET-MGMT-NODES`, `READ`




Operation GET-TYPES
^^^^^^^^^^^^^^^^^^^

Get the set of entity types and their inheritance relationships

**Request properties:**

*entityType* (string)
  If set, restrict query results to entities that extend (directly or indirectly) this type

*identity* (string)
  Set to the value `self`

**Response body**  (map)A map where each key is an entity type name (string) and the corresponding value is the list of the entity types (strings) that it extends.


Operation GET-ATTRIBUTES
^^^^^^^^^^^^^^^^^^^^^^^^

Get the set of entity types and the annotations they implement

**Request properties:**

*entityType* (string)
  If set, restrict query results to entities that extend (directly or indirectly) this type

*identity* (string)
  Set to the value `self`

**Response body**  (map)A map where each key is an entity type name (string) and the corresponding value is a list (of strings) of attributes on that entity type.


Operation GET-OPERATIONS
^^^^^^^^^^^^^^^^^^^^^^^^

Get the set of entity types and the operations they support

**Request properties:**

*entityType* (string)
  If set, restrict query results to entities that extend (directly or indirectly) this type

*identity* (string)
  Set to the value `self`

**Response body**  (map)A map where each key is an entity type name (string) and the corresponding value is the list of operation names (strings) that it supports.


Operation GET-ANNOTATIONS
^^^^^^^^^^^^^^^^^^^^^^^^^

**Request properties:**

*entityType* (string)
  If set, restrict query results to entities that extend (directly or indirectly) this type

*identity* (string)
  Set to the value `self`

**Response body**  (map)A map where each key is an entity type name (string) and the corresponding value is the list of annotations (strings) that it  implements.


Operation QUERY
^^^^^^^^^^^^^^^

Query for attribute values of multiple entities.

**Request body**  (map)A map containing the key `attributeNames` with value a list of (string) attribute names to return. If the list or the map is empty or the body is missing all attributes are returned.

**Request properties:**

*count* (integer)
  If set, specifies the number of entries from the result set to return. If not set return all from `offset`

*entityType* (string)
  If set, restrict query results to entities that extend (directly or indirectly) this type

*identity* (string)
  Set to the value `self`

*offset* (integer)
  If set, specifies the number of the first element of the result set to be returned.

**Response body**  (map)A map with two entries. `attributeNames` is a list of the attribute names returned. `results` is a list of lists each containing the attribute values for a single entity in the same order as the names in the `attributeNames` entry. If an attribute name is not applicable for an entity then the corresponding value is `null`

**Response properties:**

*count* (integer)
  Number of results returned

*identity* (string)
  Set to the value `self`


Operation GET-MGMT-NODES
^^^^^^^^^^^^^^^^^^^^^^^^

Get the addresses of all management nodes known to this router

**Request properties:**

*identity* (string)
  Set to the value `self`

**Response body**  (list)A list of addresses (strings) of management nodes known to this management node.


management
++++++++++

Qpid dispatch router extensions to the standard org.amqp.management interface.

Operations allowed: `GET-SCHEMA`, `GET-JSON-SCHEMA`, `QUERY`, `GET-TYPES`, `GET-ANNOTATIONS`, `GET-OPERATIONS`, `GET-ATTRIBUTES`, `GET-MGMT-NODES`, `READ`




Operation GET-SCHEMA-JSON
^^^^^^^^^^^^^^^^^^^^^^^^^

Get the qdrouterd schema for this router in JSON format

**Request properties:**

*indent* (integer)
  Number of spaces to indent the formatted result. If not specified, the result is in minimal format, no unnecessary spaces or newlines.

*identity* (string)
  Set to the value `self`

**Response body**  (string)The qdrouter schema as a JSON string.


Operation GET-SCHEMA
^^^^^^^^^^^^^^^^^^^^

Get the qdrouterd schema for this router in AMQP map format

**Request properties:**

*identity* (string)
  Set to the value `self`

**Response body**  (map)The qdrouter schema as a map.


router.link
+++++++++++

Link to another AMQP endpoint: router node, client or other AMQP process.

Operations allowed: `READ`



*linkName* (string)

*linkType* (One of ['endpoint', 'waypoint', 'inter-router', 'inter-area'])

*linkDir* (One of ['in', 'out'])

*owningAddr* (string)

*eventFifoDepth* (integer)

*msgFifoDepth* (integer)

*remoteContainer* (string)


router.address
++++++++++++++

AMQP address managed by the router.

Operations allowed: `READ`



*inProcess* (boolean)

*subscriberCount* (integer)

*remoteCount* (integer)

*deliveriesIngress* (integer)

*deliveriesEgress* (integer)

*deliveriesTransit* (integer)

*deliveriesToContainer* (integer)

*deliveriesFromContainer* (integer)

*key* (string)
  Internal unique (to this router) key to identify the address


router.node
+++++++++++

Remote router node connected to this router.

Operations allowed: `READ`



*routerId* (string)
  Remote node identifier.

*instance* (integer)
  Remote node boot number.

*linkState* (list)
  List of remote node's neighbours.

*nextHop* (string)
  Neighbour ID of next hop to remote node from here.

*validOrigins* (list)
  List of valid origin nodes for messages arriving via the remote node, used for duplicate elimination in redundant networks.

*address* (string)
  Address of the remote node

*routerLink* (entityId)
  Local link to remote node


connection
++++++++++

Connections to the router's container.

Operations allowed: `READ`



*container* (string)
  The container for this connection

*state* (One of ['connecting', 'opening', 'operational', 'failed', 'user'])

*host* (string)
  IP address and port number in the form addr:port.

*dir* (One of ['in', 'out'])
  Direction of connection establishment in or out of the router.

*role* (string)

*sasl* (string)
  SASL mechanism used for authentication.


allocator
+++++++++

Memory allocation pool.

Operations allowed: `READ`



*typeName* (string)

*typeSize* (integer)

*transferBatchSize* (integer)

*localFreeListMax* (integer)

*globalFreeListMax* (integer)

*totalAllocFromHeap* (integer)

*totalFreeToHeap* (integer)

*heldByThreads* (integer)

*batchesRebalancedToThreads* (integer)

*batchesRebalancedToGlobal* (integer)


Management Operations
---------------------


The `qdstat(8)` and `qdmanage(8)` tools allow you to view or modify management entity
attributes. They work by invoking *management operations*. You can invoke these operations
from any AMQP client by sending a message with the appropriate properties and body to the
`$management` address. The message should have a `reply-to` address indicating where the
response should be sent.


Operations for all entity types
+++++++++++++++++++++++++++++++


Operation READ
^^^^^^^^^^^^^^

Read attributes of an entity

**Request properties:**

*type* (string)
  Type of desired entity.

*name* (string)
  Name of desired entity. Must supply name or identity.

*identity* (string)
  Identity of desired entity. Must supply name or identity.

**Response body**  (map)Attributes of the entity


Operation CREATE
^^^^^^^^^^^^^^^^

Create a new entity.

**Request body**  (map, required)Attributes for the new entity. Can include name and/or type.

**Request properties:**

*type* (string, required)
  Type of new entity.

*name* (string)
  Name of new entity. Optional, defaults to identity.

**Response body**  (map)Attributes of the entity


Operation UPDATE
^^^^^^^^^^^^^^^^

Update attributes of an entity

**Request body**  (map)Attributes to update for the entity. Can include name or identity.

**Request properties:**

*type* (string)
  Type of desired entity.

*name* (string)
  Name of desired entity. Must supply name or identity.

*identity* (string)
  Identity of desired entity. Must supply name or identity.

**Response body**  (map)Updated attributes of the entity


Operation DELETE
^^^^^^^^^^^^^^^^

Delete an entity

**Request properties:**

*type* (string)
  Type of desired entity.

*name* (string)
  Name of desired entity. Must supply name or identity.

*identity* (string)
  Identity of desired entity. Must supply name or identity.


Operations for `org.amqp.management` entity type
++++++++++++++++++++++++++++++++++++++++++++++++


Operation GET-TYPES
^^^^^^^^^^^^^^^^^^^

Get the set of entity types and their inheritance relationships

**Request properties:**

*entityType* (string)
  If set, restrict query results to entities that extend (directly or indirectly) this type

*identity* (string)
  Set to the value `self`

**Response body**  (map)A map where each key is an entity type name (string) and the corresponding value is the list of the entity types (strings) that it extends.


Operation GET-ATTRIBUTES
^^^^^^^^^^^^^^^^^^^^^^^^

Get the set of entity types and the annotations they implement

**Request properties:**

*entityType* (string)
  If set, restrict query results to entities that extend (directly or indirectly) this type

*identity* (string)
  Set to the value `self`

**Response body**  (map)A map where each key is an entity type name (string) and the corresponding value is a list (of strings) of attributes on that entity type.


Operation GET-OPERATIONS
^^^^^^^^^^^^^^^^^^^^^^^^

Get the set of entity types and the operations they support

**Request properties:**

*entityType* (string)
  If set, restrict query results to entities that extend (directly or indirectly) this type

*identity* (string)
  Set to the value `self`

**Response body**  (map)A map where each key is an entity type name (string) and the corresponding value is the list of operation names (strings) that it supports.


Operation GET-ANNOTATIONS
^^^^^^^^^^^^^^^^^^^^^^^^^

**Request properties:**

*entityType* (string)
  If set, restrict query results to entities that extend (directly or indirectly) this type

*identity* (string)
  Set to the value `self`

**Response body**  (map)A map where each key is an entity type name (string) and the corresponding value is the list of annotations (strings) that it  implements.


Operation QUERY
^^^^^^^^^^^^^^^

Query for attribute values of multiple entities.

**Request body**  (map)A map containing the key `attributeNames` with value a list of (string) attribute names to return. If the list or the map is empty or the body is missing all attributes are returned.

**Request properties:**

*count* (integer)
  If set, specifies the number of entries from the result set to return. If not set return all from `offset`

*entityType* (string)
  If set, restrict query results to entities that extend (directly or indirectly) this type

*identity* (string)
  Set to the value `self`

*offset* (integer)
  If set, specifies the number of the first element of the result set to be returned.

**Response body**  (map)A map with two entries. `attributeNames` is a list of the attribute names returned. `results` is a list of lists each containing the attribute values for a single entity in the same order as the names in the `attributeNames` entry. If an attribute name is not applicable for an entity then the corresponding value is `null`

**Response properties:**

*count* (integer)
  Number of results returned

*identity* (string)
  Set to the value `self`


Operation GET-MGMT-NODES
^^^^^^^^^^^^^^^^^^^^^^^^

Get the addresses of all management nodes known to this router

**Request properties:**

*identity* (string)
  Set to the value `self`

**Response body**  (list)A list of addresses (strings) of management nodes known to this management node.


Operations for `management` entity type
+++++++++++++++++++++++++++++++++++++++


Operation GET-SCHEMA-JSON
^^^^^^^^^^^^^^^^^^^^^^^^^

Get the qdrouterd schema for this router in JSON format

**Request properties:**

*indent* (integer)
  Number of spaces to indent the formatted result. If not specified, the result is in minimal format, no unnecessary spaces or newlines.

*identity* (string)
  Set to the value `self`

**Response body**  (string)The qdrouter schema as a JSON string.


Operation GET-SCHEMA
^^^^^^^^^^^^^^^^^^^^

Get the qdrouterd schema for this router in AMQP map format

**Request properties:**

*identity* (string)
  Set to the value `self`

**Response body**  (map)The qdrouter schema as a map.


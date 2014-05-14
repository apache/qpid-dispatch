;;
;; Licensed to the Apache Software Foundation (ASF) under one
;; or more contributor license agreements.  See the NOTICE file
;; distributed with this work for additional information
;; regarding copyright ownership.  The ASF licenses this file
;; to you under the Apache License, Version 2.0 (the
;; "License"); you may not use this file except in compliance
;; with the License.  You may obtain a copy of the License at
;; 
;;   http://www.apache.org/licenses/LICENSE-2.0
;; 
;; Unless required by applicable law or agreed to in writing,
;; software distributed under the License is distributed on an
;; "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
;; KIND, either express or implied.  See the License for the
;; specific language governing permissions and limitations
;; under the License.
;;

# Dispatch AMQP Mapping

Dispatch Router is an AMQP router and as such, it provides extensions,
codepoints, and semantics for routing over AMQP.  This page documents
the details of Dispatch Router's use of AMQP.


## Message Annotations

The following Message Annotation fields are defined by Dispatch Router:

  || *Field* || *Type* || *Description* ||
  || <span style="white-space: nowrap;">x-opt-qd.ingress</span> || string || The identity of the ingress router for a message-routed message.  The ingress router is the first router encountered by a transiting message.  The router will, if this field is present, leave it unaltered.  If the field is not present, the router shall insert the field with its own identity. ||
  || <span style="white-space: nowrap;">x-opt-qd.trace</span> || list of string || The list of routers through which this message-routed message has transited.  If this field is not present, the router shall do nothing.  If the field is present, the router shall append its own identity to the end of the list. ||
  || x-opt-qd.to || string || To-Override for message-routed messages.  If this field is present, the address in this field shall be used for routing in lieu of the *to* field in the message properties.  A router may append, remove, or modify this annotation field depending on the policy in place for routing the message. ||
  || x-opt-qd.class || string || Message class.  This is used to allow the router to provide separate paths for different classes of traffic. ||


## Source/Target Capabilities

The following Capability values are used in Sources and Targets.

  || *Capability* || *Description* ||
  || qd.router || This capability is added to sources and targets that are used for inter-router message exchange. ||


## Addresses and Address Formats

The following AMQP addresses and address patterns are used within Dispatch Router.

### Address Patterns

  || *Pattern* || *Description* ||
  || /_local/&lt;addr&gt; || An address that references a locally attached endpoint.  Messages using this address pattern shall not be routed over more than one link. ||
  || <span style="white-space: nowrap;">/_topo/&lt;area&gt;/&lt;router&gt;/&lt;addr&gt;</span> || An address that references an endpoint attached to a specific router node in the network topology.  Messages with addresses that follow this pattern shall be routed along the shortest path to the specified router.  Note that addresses of this form are a-priori routable in that the address itself contains enough information to route the message to its destination. ||
  || /&lt;addr&gt; || A mobile address.  An address of this format represents an endpoint or a set of distinct endpoints that are attached to the network in arbitrary locations.  It is the responsibility of the router network to determine which router nodes are valid destinations for mobile addresses. ||

### Supported Addresses

  || *Address* || *Description* ||
  || /_local/$management || The management agent on the attached router/container.  This address would be used by an endpoint that is a management client/console/tool wishing to access management data from the attached container. ||
  || <span style="white-space: nowrap;">/_topo/0/Router.E/agent</span> || The management agent at Router.E in area 0.  This address would be used by a management client wishing to access management data from a specific container that is reachable within the network. ||
  || /_local/qdhello || The router entity in each of the connected routers.  This address is used to communicate with neighbor routers and is exclusively for the HELLO discovery protocol. ||
  || /_local/qdrouter || The router entity in each of the connected routers.  This address is used by a router to communicate with other routers in the network. ||
  || <span style="white-space: nowrap;">/_topo/0/Router.E/qdxrouter</span> || The router entity at the specifically indicated router.  This address form is used by a router to communicate with a specific router that may or may not be a neighbor. ||

## Implementation of the AMQP Management Specification

Qpid Dispatch is manageable remotely via AMQP.  It is compliant to a limited degree with the emerging AMQP Management specification.  This section provides the details of what is supported and what is not and what is planned and what is not.

Non-compliance occurs for one of the following reasons:

  - Implementation of an optional feature is not planned
  - Implementation is not complete as of the current time
  
### Compliance Matrix

  || *Operation/Feature* || *Requirement* || *Supported* || *Remarks* ||
  || CREATE || Should || No || There are currently no Manageable Entities for which this is appropriate ||
  || READ || Should || No || Not yet implemented ||
  || UPDATE || Should || No || There are currently no Manageable Entities for which this is appropriate ||
  || DELETE || Should || No || There are currently no Manageable Entities for which this is appropriate ||
  || QUERY || Should || Yes || Query requests must provide either an entityType or a set of attributeNames.  It will not support the general "get all attributes from all entityTypes" query. ||
  || GET-TYPES || Should || Yes || There are no types that implement base types ||
  || GET-ATTRIBUTES || Should || Yes || ||
  || GET-OPERATIONS || Should || Yes || ||
  || GET-MGMT-NODES || Should || Yes || This operation yields the addresses of all of the router nodes in the known network ||
  || REGISTER || May || Not Planned || The router has a specific way to discover peers that does not involve this operation ||
  || DEREGISTER || May || Not Planned || The router has a specific way to discover peers that does not involve this operation ||

### Manageable Entities

  || *Type Name* || *Description* ||
  || org.apache.qpid.dispatch.allocator || Per-type memory allocation statistics ||
  || org.apache.qpid.dispatch.connection || Connections to the router's container ||
  || org.apache.qpid.dispatch.router || General state and statistics for the router node ||
  || org.apache.qpid.dispatch.router.link || Per-link state and statistics for links attached to the router node ||
  || org.apache.qpid.dispatch.router.node || Per-node state and statistics for remote router nodes in the known network ||
  || org.apache.qpid.dispatch.router.address || Per-address state and statistics for addresses known to this router ||

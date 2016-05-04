.. Licensed to the Apache Software Foundation (ASF) under one
   or more contributor license agreements.  See the NOTICE file
   distributed with this work for additional information
   regarding copyright ownership.  The ASF licenses this file
   to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance
   with the License.  You may obtain a copy of the License at
     http://www.apache.org/licenses/LICENSE-2.0
   Unless required by applicable law or agreed to in writing,
   software distributed under the License is distributed on an
   "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
   KIND, either express or implied.  See the License for the
   specific language governing permissions and limitations
   under the License

qdstat manual page
==================

Synopsis
--------

qdstat [options]

Description
-----------

*qdstat* shows status information about networks of Dispatch routers. It
can display connections, network nodes and links, and router stats such
as memory use.

Options
-------

.. include:: qdstat_help.rst

Output Columns
--------------

qdstat -c
~~~~~~~~~
Id
:   Unique connection identifier

host
:   The hostname or internet address of the remotely connected AMQP container

container
:   The container-name of the remotely connected AMQP container

role
:   The configured role of the connection
  - normal - Normal connections from client to router
  - inter-router - Connection between routers to form a network
  - route-container - Connection to/from a Broker or other host to receive link-routes and waypoints

dir
:   The direction of connection establishment
  - in - The connection was initiated by the remote container
  - out - The connection was initiated by this router

security
:   A description of the security/encryption in effect for this connection

authentication
:   The method and user-id of the authenticated user for this connection

qdstat -l
~~~~~~~~~
type
:   Type of link
  - router-control - An inter-router link that is reserved for control messages exchanged between routers
  - inter-router - An inter-router link that is used for normal message-routed deliveries
  - endpoint - A normal link to an external endpoint container

dir
:   The direction of message flow on the link
  - in - Deliveries flow inbound to the router
  - out - Deliveries flow outbound from the router

conn id
:   Unique identifier of the connection over which this link is attached

id
:   Unique identifier of this link

peer
:   For link-routed links, the unique identifier of the peer link.  In link routing, an inbound link is paired with an outbound link

class
:   Class of the address bound to the link

addr
:   The address bound to the link

phs
:   The phase of the address bound to the link

cap
:   The capacity, in deliveries, of the link

undel
:   The number of undelivered messages stored on the link's FIFO

unsettled
:   The number of unsettled deliveries being tracked by the link

deliveries
:   The total number of deliveries that have transited this link

admin
:   The administrative status of the link
  - enabled - The link is enabled for normal operation
  - disabled - The link is disabled and should be quiescing or stopped (not yet supported)

oper
:   The operational status of the link
  - up - The link is operational
  - down - The link is not attached
  - quiescing - The link is in the process of quiescing (not yet supported)
  - idle - The link has completed quiescing and is idle (not yet supported)

name
:   The link name (only shown if the -v option is provided)

qdstat -n
~~~~~~~~~
router-id
:   Identifier of the router

next-hop
:   If this router is not a neighbor, identifies the next-hop neighbor used to reach this router

link
:   Unique identifier of the link to the neighbor router

cost
:   The topology cost to this remote router (with -v option only)

neighbors
:   The list of neighbor routers (the router's link-state) (with -v option only)

valid-origins
:   The list of origin routers for which the best path to the listed router passes through this router (with -v option only)

qdstat -a
~~~~~~~~~
class
:   The class of the listed address
  - local - Address that is local to this router
  - topo - Topological address used for router control messages
  - router - A summary router address used to route messages to a remote router's local addresses
  - mobile - A mobile address for an attached consumer or producer

addr
:   The address text

phs
:   For mobile addresses only, the phase of the address.  Direct addresses have only a phase 0.  Waypoint addresses have multiple phases, normally 0 and 1.

distrib
:   Distribution method used for this address
  - multicast - A copy of each message is delivered once to each consumer for the address
  - closest - Each message is delivered to only one consumer for the address.  The closest (lowest cost) consumer will be chosen.  If there are multiple lowest-cost consumers, deliveries will be spread across those consumers.
  - balanced - Each message is delivered to only one consumer for the address.  The consumer with the fewest outstanding (unsettled) deliveries will be chosen.  The cost of the route to the consumer is a threshold for delivery (i.e. higher cost consumers will only receive deliveries if closer consumers are backed up).
  - flood - Used only for router-control traffic.  This is multicast without prevention of duplicate deliveries.

in-proc
:   The number of in-process consumers for this address

local
:   The number of local (on this router) consumers for this address

remote
:   The number of remote routers that have at least one consumer for this address

cntnr
:   The number of locally attached containers that are destinations for link-routes on this address

in
:   The number of deliveries for this address that entered the network on this router

out
:   The number of deliveries for this address that exited the network on this router

thru
:   The number of deliveries for this address that were forwarded to other routers

to-proc
:   The number of deliveries for this address that were delivered to an in-process consumer

from-proc
:   The number of deliveries for this address that were received from an in-process producer

qdstat --linkroutes
~~~~~~~~~~~~~~~~~~~
prefix
:   The prefix for matching addresses of routed links

dir
:   The direction (from the router's perspective) of matching links

distrib
:   The distribution method used for routed links
  - linkBalanced - the only supported distribution for routed links

status
:   Operational status of the link route
  - active - Route is actively routing attaches (i.e. ready for use)
  - inactive - Route is inactive because there is no local destination connected

qstat --autolinks
~~~~~~~~~~~~~~~~~
addr
:   The address of the auto link

dir
:   The direction of message flow for the auto link
  - in - Messages flow in from the route-container to the router network
  - out - Messages flow out to the route-container from the router network

phs
:   Phase of the address for this auto link

link
:   Unique identifier of the link managed by this auto link

status
:   The operational status of this auto link
  - inactive - There is no connected container for this auto link
  - attaching - The link is attaching to the container
  - failed - The link-attach failed
  - active - The link is operational
  - quiescing - The link is quiescing (not yet supported)
  - idle - The link is idle (not yet supported)

lastErr
:   The description of the last attach failure that occurred on this auto link

See also
--------

*qdrouterd(8)*, *qdmanage(8)*, *qdrouterd.conf(5)*

http://qpid.apache.org/components/dispatch-router

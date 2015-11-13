> Licensed to the Apache Software Foundation (ASF) under one
> or more contributor license agreements.  See the NOTICE file
> distributed with this work for additional information
> regarding copyright ownership.  The ASF licenses this file
> to you under the Apache License, Version 2.0 (the
> "License"); you may not use this file except in compliance
> with the License.  You may obtain a copy of the License at
> 
>   http://www.apache.org/licenses/LICENSE-2.0
> 
> Unless required by applicable law or agreed to in writing,
> software distributed under the License is distributed on an
> "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
> KIND, either express or implied.  See the License for the
> specific language governing permissions and limitations
> under the License.

Connection Policy
=================

Qpid Dispatch Router (QDR) uses connection policies to limit
user access to QDR resources.

- Absolute TCP/IP connection count limit
- Per-listener TCP/IP connection count limit
- Per-listener TCP/IP host restrictions
- Per-listener TCP/IP user restrictions
- Per-listener AMQP user hostname access restrictions

# Absolute TCP/IP Connection Limit

QDR maintains a count of all TCP/IP connections from all remote clients and
enforces a global limit. This limit covers connections incoming to all QDR *listeners*.

## Policy Configuration
    "maximumConnections": {
      "type"       : "integer",
      "default"    : 0,
      "description": "The maximum number of concurrent client
                      connections allowed. Zero implies no limit.",
      "required"   : false}

## Policy Statistics
    "connectionsProcessed": {"type": "integer", "graph": true},
    "connectionsDenied":    {"type": "integer", "graph": true},
    "connectionsCurrent":   {"type": "integer", "graph": true}
    
## Usage Notes
 - If the policy configuration is absent or if the maximumConnections value specified in the policy is zero then absolute connection limits are not enforced. Note that connections are always tracked even when policy enforcement is disabled.
- Statistics show how many total connections have been processed, how many connections have been denied by policy, and how many connections are currently active.
- Outbound TCP/IP connections created by QDR *connectors* are not counted by the Absolute Connection Limit. That is, TCP/IP connections to waypoints or to other routers are not part of this accounting. 

# Per-listener Policy

Each QDR listener may be configured with some settings that:

- define simple sets of users organized by user *role*.
- define a simple ingress filter by matching user names against a list of external host addresses. Note that user identity is not defined by external host TCP/IP addresses. Rather, users may be restricted to connecting only from certain TCP/IP addresses.
- define AMQP Open parameters each user role may receive. (TBD)

## User

User identities are discovered during the TLS/SSL and SASL authentication phases of connection setup.  The user name used for policy specifications is stored in connection field *connection.user*.

The following special user names are defined for use in policy definitions:

- **$empty** placeholder for a blank user name.
- **\*** (asterisk) wildcard matches all user names including the blank user name.

User names are case sensitive.

## Role Statements

A *role* is a convenience name for a group of users. Role statements are the list of configuration lines that specify roles.

### Role Statement Examples

    |role   |rolename |defining elements|
    |-------|---------|-----------------|
    |role   |ACMEadmin|mike admin       |
    |role   |ACMEuser |alice bruce      |
    |role   |ACMEtest |zeke             |

Role names may be used in defining other roles.

    |role   |rolename |defining elements     |
    |-------|---------|----------------------|
    |role   |ACMEadmin|mike admin            |
    |role   |ACMEuser |alice bruce ACMEadmin |
    |role   |ACMEtest |zeke                  |

Role names may be used recursively to extend role definitions.

    |role   |rolename |defining elements     |
    |-------|---------|----------------------|
    |role   |ACMEuser |alice bruce charlie   |
    |role   |ACMEuser |ACMEuser dave ed fred |

Role names and user names may be used interchangeably in policy specifications.

Role names are case sensitive.

## Origin Statements

Origin statements are the list of configuration lines that specify ingress filter rules.

### Host Specification

A Host Specification (hostspec) may be one of

- Individual host
- Host range
- Wildcard

#### Individual Host Specification

Individual hosts may be specified by literal IP address or by hostname.

    127.0.0.1  - literal IPv4 address in dotted decimal notation
    [::1]      - literal IPv6 address decorated with square brackets
    boston.com - a named host

A named host may resolve to a single IP address

    > getent hosts test-ipv6.com
    216.218.228.119 test-ipv6.com

or a named host may resolve to a list of IP addresses

    > getent hosts unix.stackexchange.com
    104.16.116.182  unix.stackexchange.com
    104.16.119.182  unix.stackexchange.com
    104.16.115.182  unix.stackexchange.com
    104.16.117.182  unix.stackexchange.com
    104.16.118.182  unix.stackexchange.com

#### Host ranges

Host ranges are specified by two literal IP address hosts separated with a comma

    host1,host2

Where host1 has a numerically lower IP address than host2.

    10.0.0.0,10.0.0.255    is allowed
    10.0.0.100,10.0.0.1    is not allowed

The two hosts must be of the same IP address family, either IPv4 or IPv6.

    0.0.0.1,[::0.0.0.2]    is not allowed

Hosts specified by one IP address family, IPv4 or IPv6, will not match actual connections that arrive over the wire in the other address family, IPv6 or IPv4. 

IPv4 CIDR format is not supported. Instead, use a host range.

    10.24.0.0,10.24.255.255  specifies the 10.24.0.0/16 subnet

#### The wildcard

is "*". It stands for all addresses, IPv4 or IPv6. It may not be used in a host range statement.

### Origin Statement Rule Processing

Origin statements are all *allow* or *whitelist* rules that when matched allow the TCP/IP socket connection. Origin statements are idempotent and may be processed in any order but still achieve the same allow or deny decision.

### Origin Statement Examples

    |origin |role/user |IP addresses allowed    |Comment                           |
    |-------|----------|------------------------|----------------------------------|
    |origin |ACMEuser  |9.0.0.0,9.255.255.255   |users allowed from within a subnet|
    |origin |ACMEadmin |9.1.1.30                |admin locked to single IP         |
    |origin |ellen     |72.135.2.9              |random user allowed from anywhere |
    |origin |*         |10.18.0.0,10.18.255.255 |anyone allowed on local subnet    |
    |origin |ACMEtest  |*                       |role allowed from anywhere        |

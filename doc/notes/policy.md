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

# Connection Policy

Qpid Dispatch Router (QDR) uses connection and access policies to limit
user access to QDR resources.

- Absolute TCP/IP connection count limit
- Per-listener TCP/IP connection count limit
- Per-listener TCP/IP host restrictions
- Per-listener TCP/IP user restrictions
- Per-listener AMQP user hostname access restrictions (TBD)

The Absolute TCP/IP connection count limit is built in to QDR and is always available. The remaining policy controls are managed by a pluggable Policy Authority module. The Policy Authority may be:

- Absent. No per-listener policies are enforced.
- Local. A simple, built-in Policy Authority may be configured.
- User Defined. A user module may handle multi-router or centralized configurations.

The system interface to Local and User Defined modules is the same.
 
# Absolute TCP/IP Connection Limit

QDR maintains a count of all TCP/IP connections from all remote clients and
enforces a global limit. This limit covers connections incoming to all QDR *listeners*.

This limit protects QDR from running out of resources like file descriptors due to incoming connections.

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

# Policy Authority Run-time Interface

The Policy Authority is consulted at different times in a connection life cycle.

- A listener socket is accepted
- An AMQP Open performative is received.

## Policy Authority Listener Accept

The listener socket acceptance is performed before the AMQP handshake has started. At this point the remote host is known but not the user identity. This policy protects the listener from

- too many connection in total
- too many connection from one external host

#
    bool policy_listener_accept(
        const char *listenername,
        const char *hostname,
        void       *netaddr_info)

The policy engine returns an allow/deny decision after deciding if this host is allowed to connect the the listener. netaddr_info is some binary info to help the policy engine determine if the the hostname is within configured IP address ranges.

## Policy Authority AMQP Open

The AMQP Open performative received acceptance is called after the network socket is open and after the AMQP and security handshaking has completed. Now both the user identity and the target of the AMQP Open are both known. The open may be rejected and the connection is closed with an error. 

    void * policy_listener_open(
        const char          *listenername,
        const char          *hostname,
        const char	        *authid)

The policy engine returns a TBD policy allow/deny decision if user 'authid' connected through listenername is allowed to open a connection to hostname. If the connection is allowed then a policy configuration is returned. If the connection is denied then a null is returned and the connection is closed.

# Local Policy Authority (LocalPA)

Policy may be implemented in any number of ways: local configuration files, ldap, IPC requests, or whatever. The LocalPA is a compact, fully functional policy engine that is designed to help test the policy engine interfaces. In many instances LocalPA will be adequate to fully protect a QDR.

## LocalPA Per-listener TCP/IP Connection Policy

Each QDR listener may be configured with some settings that:

- define simple sets of users organized by user *role*.
- define a simple ingress filter by matching user names against a list of external host addresses. Note that user identity is not defined by external host TCP/IP addresses. Rather, users may be restricted to connecting only from certain TCP/IP addresses.
- define AMQP Open parameters each user role may receive. (TBD)

### User

User identities are discovered during the TLS/SSL and SASL authentication phases of connection setup.  The user name used for policy specifications is stored in connection field *connection.user*.

The following special user names are defined for use in policy definitions:

- **\*** (asterisk) wildcard matches all user names including the blank user name.

User names are case sensitive.

### Role Statements

A *role* is a convenience name for a group of users.

Role names and user names may be used interchangeably in policy specifications.

Role names are case sensitive.

### Origin Statements

Origin statements are the list of configuration lines that specify ingress filter rules.

#### Host Specification

A Host Specification (hostspec) may be one of

- Individual host
- Host range
- Wildcard

##### Individual Host Specification

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

##### Host ranges

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

## Example configuration file

LocalPA is configured with a single Python ConfigParser-format file.

    # qpid-dispatch simple policy listener configuration
    #
    [listener1]
    maximumConnections       : 10
    maximumConnectionsPerUser: 5
    maximumConnectionsPerHost: 5
    
    # roles is a map.
    # key  : the role name
    # value: list of users assigned to that role.
    roles: {
      'admin': ['alice', 'bob', 'charlie'],
      'users': ['u1', 'u2', 'u3', 'u4', 'u5', 'u6'],
      'test':  ['zeke']
      }
    
    # origins is a map. 
    # key  : IP address or range of IP addresses
    # value: list of roles/users allowed to connect from that address/range.
    origins: {
      '9.0.0.0,9.255.255.255':   ['users', 'twatson'],
      '10.18.0.0,10.18.255.255': ['*'],
      '72.135.2.9':              ['ellen'],
      '*':                       ['test']
      }
    
    [default]
    maximumConnections       : 2
    maximumConnectionsPerUser: 2
    maximumConnectionsPerHost: 3
    
    # roles is a map.
    # key  : the role name
    # value: list of users assigned to that role.
    roles: {
      }
    
    # origins is a map. 
    # key  : IP address or range of IP addresses
    # value: list of roles/users allowed to connect from that address/range.
    origins: {
      '127.0.0.1':   ['*'],
      '::1':         ['*']
      }

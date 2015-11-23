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

The system defines global settings:

- Absolute TCP/IP connection count limit

A pluggable Policy Authority defines per-listener, per-application settings:

- TCP/IP connection count limits
- TCP/IP connecting host/user restrictions
- TCP/IP user restrictions

# Absolute TCP/IP Connection Limit

QDR maintains a count of all TCP/IP connections from all remote clients and
enforces a global limit. This limit covers connections incoming to all QDR *listeners*.

This limit protects QDR from running out of resources like file descriptors due to incoming connections.

## Usage Notes
 - If the policy configuration is absent or if the maximumConnections value specified in the policy is zero then absolute connection limits are not enforced. Note that connections are always tracked even when policy enforcement is disabled.
- Statistics show how many total connections have been processed, how many connections have been denied by policy, and how many connections are currently active.
- Outbound TCP/IP connections created by QDR *connectors* are not counted by the Absolute Connection Limit. That is, TCP/IP connections to waypoints or to other routers are not part of this accounting. 

# Policy Authority Run-time Interface

The Policy Authority is consulted at different times in a connection life cycle.

- A listener socket is accepted
- An AMQP Open performative is received.
- An AMQP Begin performative is received.
- An AMQP Attach performative is received.

## Policy Authority Listener Accept

The listener socket acceptance is performed before the AMQP handshake has started. At this point the remote host is known but not the user identity. This policy protects the listener from

- too many connection in total
- too many connection from one external host

#
    @param[in] listenername : listener receiving the connection
    @param[in] originhost   : host name of the client system originating the connection
    
    @return the accept is allow and a socket is opened
    
    bool policy_listener_accept(
        const char *listenername,
        const char *originhost)

The policy engine returns an allow/deny decision after deciding if this client host is allowed to open a new socket to the listener.

## Policy Authority AMQP Open

The AMQP Open performative received acceptance is called after the network socket is open and after the AMQP and security handshaking has completed. Now both the user identity and the requested application name, contained in the hostname field of the AMQP Open, are known.

If the Open is allowed then the named policy is installed for this listener to control further operations on this application.

If the Open is denied then the error condition and description are returned to the listener. The listener is expected to close the connection.

        @param[in] listenername : listener receiving the Open
        @param[in] originhost   : host name of the client system originating the connection
        @param[in] authid       : authorized user name
        @param[in] application  : application named in the Open hostname field
    
        @param[in,out] policyname     : policy to use if Open is allowed
        @param[in]     policynamesize : size of policy name buffer
    
        @param[in,out] condition       : condition name to return if Open is denied
        @param[in]     conditionsize   : size of condition buffer
        @param[in,out] description     : condition description if Open is denied
        @param[in]     descriptionsize : size of description buffer
    
    bool policy_listener_open(
        const char *listenername,
        const char *originhost,
        const char *authid,
        const char *application,
    
        char * policyname,
        int    policynamesize,
    
        char * condition,
        int    conditionsize,
        char * description,
        int    descriptionsize)

### AMQP Open - Connection processing

Open processing is performed in two phases.

#### Open - user connection origin

First the user's connecting host origin address is checked. The connection may be denied if the user is not logging in from an acceptable host on the network.

#### Open - user access policy

Next the policy tables are consulted to see if this user has rights to the application. If the user is granted access by membership in a role then the user connection is allowed. Otherwise it is denied.

# Local Policy Authority (LocalPA)

Policy may be implemented in any number of ways: local configuration files, ldap, IPC requests, or whatever. The LocalPA is a compact, fully functional policy engine that is designed to help test the policy engine interfaces. In many instances LocalPA will be adequate to fully protect a QDR.

## LocalPA Per-listener TCP/IP Connection Policy

Each QDR listener may be configured with some settings that:

- define simple sets of users organized by user *role*.
- define a simple ingress filter by matching user names against a list of external host addresses. Note that user identity is not defined by external host TCP/IP addresses. Rather, users may be restricted to connecting only from certain TCP/IP addresses.
- define AMQP Open parameters each user role may receive.
- define which AMQP Attach sources and targets may be accessed by this connection.

### Aggregate Connection Limits

Each policy defines connection limits:

- maximumConnections
- maximumConnectionsPerUser
- maximumConnectionsPerHost

These limits are applied in addition to the system-wide absolute maximumConnections. They prevent a user from using all of a application's resources. 

### User

Authorized user identities are discovered during the TLS/SSL and SASL authentication phases of connection setup.  The authorized user name used for policy specifications is stored in connection field *connection.user*.

The special user name **\*** (asterisk) wildcard matches all user names.

User names are case sensitive.

### Role Statements

A *role* is a convenience name for a group of users.

Role names and user names may be used interchangeably in policy specifications.

Role names are case sensitive.

### connectionOrigin Statements

Origin statements specify groups of network hosts.

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

### connectionPolicy Statements

These rules create an ingress filter to allow a user to connect only from a defined set of IP addresses. If a user is included in one of the definitions for connectionPolicy then that user connection is allowed. Otherwise that user's access is controlled by the connectionAllowUnrestricted flag.

#### connectionAllowUnrestricted Flag

This flag offers a blanket allow-all or deny-all backstop to the connectionPolicy filter. A few simple filter rules illustrated in the sample configuration shown below ensure that the privileged users connect only from a few hosts and subnets. Now the question is what happens to users that were not filtered by the connectionPolicy rules? By having the flag set to true non-privileged users may connect from anywhere.

In a different application you may know all the users and all the places from which they can connect. In that case a connectionPolicy can completely define the allowed connections. Then the connectionAllowUnrestricted flag may be set to False to deny any other connections.

### policy Statements

The policy statements define the permissions a user gets when he is allowed to access a application. These permissions include:
 
- The **AMQP Open** performative ***channel-max*** upper limit value. This controls the number of sessions that may be created over this connection.
- The **AMQP Open** performative ***max-frame-size*** upper limit value. This controls the message buffering for this connections.
- The **AMQP Begin** performative ***handle-max*** upper limit value. This controls the number of links that each session may contain.
- The **AMQP Attach** performative ***max-messge-size*** upper limit value. This controls the message size and ultimately the memory that the connection can consume.
- The **AMQP Attach** performative ***source*** and ***target*** allowed values. This controls the read/write access to application resources.

## Example configuration file

LocalPA is configured with a single Python ConfigParser-format file. For example:

    # qpid-dispatch simple policy listener configuration for photoserver application
    #
    
    [photoserver]
    # versionId is a text string identifier
    versionId                : 8
    
    # Aggregate connection limits
    maximumConnections        : 10
    maximumConnectionsPerUser : 5
    maximumConnectionsPerHost : 5
    
    # A role is an group of authid names
    roles: {
      'anonymous'       : ['anonymous'],
      'users'           : ['u1', 'u2'],
      'paidsubscribers' : ['p1', 'p2'],
      'test'            : ['zeke', 'ynot'],
      'admin'           : ['alice', 'bob', 'ellen'],
      'superuser'       : ['ellen']
      }
    
    # A connectionOrigin is list of network host addresses or host address ranges
    connectionOrigins: {
      'Ten18':      ['10.18.0.0,10.18.255.255'],
      'EllensWS':   ['72.135.2.9'],
      'TheLabs':    ['10.48.0.0,10.48.255.255','192.168.100.0,192.168.100.255'],
      'Localhost':  ['127.0.0.1','::1'],
      }
    
    # connectionPolicy restricts users to connecting only from defined origins
    connectionPolicy: {
      'admin'      : ['Ten18', 'TheLabs', 'Localhost'],
      'test'       : ['TheLabs'],
      'superuser'  : ['Localhost', 'EllensWS']
      }
    
    # connectionAllowUnrestricted - If a user is not restricted by a connectionPolicy
    #                               then is this user allowed to connect?
    connectionAllowUnrestricted : True
    
    # policy - Based on the user's role what are his access rights?
    #
    # A policy contains:
    #  - values passed in AMQP Open and Attach performatives
    #  - allowed source and target names in AMQP Attach
    #
    policies: {
      'anonymous' : {
        'max_frame_size'         : 111111,
        'max_message_size'       : 111111,
        'max_session_window'     : 111111,
        'max_sessions'           : 1,
        'max_senders'            : 11,
        'max_receivers'          : 11,
        'allow_dynamic_src'      : False,
        'allow_anonymous_sender' : False,
        'sources'                : [public],
        'targets'                : []
        },
      'users' : {
        'max_frame_size'         : 222222,
        'max_message_size'       : 222222,
        'max_session_window'     : 222222,
        'max_sessions'           : 2,
        'max_senders'            : 22,
        'max_receivers'          : 22,
        'allow_dynamic_src'      : False,
        'allow_anonymous_sender' : False,
        'sources'                : [public, private],
        'targets'                : [public]
        },
      'paidsubscribers' : {
        'max_frame_size'         : 333333,
        'max_message_size'       : 333333,
        'max_session_window'     : 333333,
        'max_sessions'           : 3,
        'max_senders'            : 33,
        'max_receivers'          : 33,
        'allow_dynamic_src'      : True,
        'allow_anonymous_sender' : False,
        'sources'                : [public, private],
        'targets'                : [public, private]
        },
      'test' : {
        'max_frame_size'         : 444444,
        'max_message_size'       : 444444,
        'max_session_window'     : 444444,
        'max_sessions'           : 4,
        'max_senders'            : 44,
        'max_receivers'          : 44,
        'allow_dynamic_src'      : True,
        'allow_anonymous_sender' : True,
        'sources'                : [private],
        'targets'                : [private]
        },
      'admin' : {
        'max_frame_size'         : 555555,
        'max_message_size'       : 555555,
        'max_session_window'     : 555555,
        'max_sessions'           : 5,
        'max_senders'            : 55,
        'max_receivers'          : 55,
        'allow_dynamic_src'      : True,
        'allow_anonymous_sender' : True,
        'sources'                : [public, private, management],
        'targets'                : [public, private, management]
        },
      'superuser' : {
        'max_frame_size'         : 666666,
        'max_message_size'       : 666666,
        'max_session_window'     : 666666,
        'max_sessions'           : 6,
        'max_senders'            : 66,
        'max_receivers'          : 66,
        'allow_dynamic_src'      : False,
        'allow_anonymous_sender' : False,
        'sources'                : [public, private, management, root],
        'targets'                : [public, private, management, root]
        }
      }

### Example Configuration File Walkthrough

This section shows the processing behind various policy lookups. (TBD)

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

import socket
import binascii

#
#


class PolicyError(Exception):

    def __init__(self, value):
        self.value = value

    def __str__(self):
        return str(self.value)


def is_ipv6_enabled():
    """Returns true if IPV6 is enabled, false otherwise"""
    ipv6_enabled = True
    try:
        sock = socket.socket(socket.AF_INET6, socket.SOCK_STREAM)
        sock.bind(('::1', 0))
        sock.close()
    except Exception as e:
        ipv6_enabled = False

    return ipv6_enabled


class HostStruct:
    """
    HostStruct represents a single, binary socket address from getaddrinfo
        - name     : name given to constructor; numeric IP or host name
        - saddr    : net name resolved by getaddrinfo; numeric IP
        - family   : saddr.family; int
        - binary   : saddr packed binary address; binary string
    """
    families = [socket.AF_INET]
    famnames = ["IPv4"]
    if is_ipv6_enabled():
        families.append(socket.AF_INET6)
        famnames.append("IPv6")

    def __init__(self, hostname):
        """
        Given a host name text string, return the socket info for it.
        @param[in] hostname host IP address to parse
        """
        try:
            res = socket.getaddrinfo(hostname, 0, socket.AF_UNSPEC, socket.SOCK_STREAM)
            if len(res) == 0:
                raise PolicyError("HostStruct: '%s' did not resolve to an IP address" % hostname)
            foundFirst = False
            saddr = ""
            sfamily = socket.AF_UNSPEC
            for i0 in range(0, len(res)):
                family, dum0, dum1, dum2, sockaddr = res[i0]
                if not foundFirst:
                    if family in self.families:
                        saddr = sockaddr[0]
                        sfamily = family
                        foundFirst = True
                else:
                    if family in self.families:
                        if not saddr == sockaddr[0] or not sfamily == family:
                            raise PolicyError("HostStruct: '%s' resolves to multiple IP addresses" %
                                              hostname)
            if not foundFirst:
                raise PolicyError("HostStruct: '%s' did not resolve to one of the supported address family" %
                                  hostname)
            self.name = hostname
            self.saddr = saddr
            self.family = sfamily
            self.binary = socket.inet_pton(family, saddr)
            return
        except Exception as e:
            raise PolicyError("HostStruct: '%s' failed to resolve: '%s'" %
                              (hostname, e))

    def __str__(self):
        return self.name

    def __repr__(self):
        return self.__str__()

    def dump(self):
        return ("(%s, %s, %s, %s)" %
                (self.name,
                 self.saddr,
                 "AF_INET" if self.family == socket.AF_INET else "AF_INET6",
                 binascii.hexlify(self.binary)))

#
#


class HostAddr:
    """
    Provide HostIP address ranges and comparison functions.
    A HostIP may be:
    - single address:      10.10.1.1
    - a pair of addresses: 10.10.0.0,10.10.255.255
    - a wildcard:          *
    Only IPv4 and IPv6 are supported.
    - No unix sockets.
    HostIP names must resolve to a single IP address.
    Address pairs define a range.
    - The second address must be numerically larger than the first address.
    - The addresses must be of the same address 'family', IPv4 or IPv6.
    The wildcard '*' matches all address IPv4 or IPv6.
    IPv6 support is conditional based on underlying OS network options.
    Raises a PolicyError on validation error in constructor.
    """

    def __init__(self, hostspec, separator=","):
        """
        Parse host spec into binary structures to use for comparisons.
        Validate the hostspec to enforce usage rules.
        """
        self.hoststructs = []

        if hostspec == "*":
            self.wildcard = True
        else:
            self.wildcard = False

            hosts = [x.strip() for x in hostspec.split(separator)]

            # hosts must contain one or two host specs
            if len(hosts) not in [1, 2]:
                raise PolicyError("hostspec must contain 1 or 2 host names")
            self.hoststructs.append(HostStruct(hosts[0]))
            if len(hosts) > 1:
                self.hoststructs.append(HostStruct(hosts[1]))
                if not self.hoststructs[0].family == self.hoststructs[1].family:
                    raise PolicyError("mixed IPv4 and IPv6 host specs in range not allowed")
                c0 = self.memcmp(self.hoststructs[0].binary, self.hoststructs[1].binary)
                if c0 > 0:
                    raise PolicyError("host specs in range must have lower numeric address first")

    def __str__(self):
        if self.wildcard:
            return "*"
        res = self.hoststructs[0].name
        if len(self.hoststructs) > 1:
            res += "," + self.hoststructs[1].name
        return res

    def __repr__(self):
        return self.__str__()

    def dump(self):
        if self.wildcard:
            return "(*)"
        res = "(" + self.hoststructs[0].dump()
        if len(self.hoststructs) > 1:
            res += "," + self.hoststructs[1].dump()
        res += ")"
        return res

    def memcmp(self, a, b):
        res = 0
        for i in range(0, len(a)):
            if a[i] > b[i]:
                res = 1
                break
            elif a[i] < b[i]:
                res = -1
                break
        return res

    def match_bin(self, candidate):
        """
        Does the candidate hoststruct match the IP or range of IP addresses represented by this?
        @param[in] candidate the IP address to be tested
        @return candidate matches this or not
        """
        if self.wildcard:
            return True
        try:
            if not candidate.family == self.hoststructs[0].family:
                # sorry, wrong AF_INET family
                return False
            c0 = self.memcmp(candidate.binary, self.hoststructs[0].binary)
            if len(self.hoststructs) == 1:
                return c0 == 0
            c1 = self.memcmp(candidate.binary, self.hoststructs[1].binary)
            return c0 >= 0 and c1 <= 0  # pylint: disable=chained-comparison
        except PolicyError:
            return False
        except Exception as e:
            assert isinstance(candidate, HostStruct), \
                ("Wrong type. Expected HostStruct but received %s" % candidate.__class__.__name__)
            return False

    def match_str(self, candidate):
        """
        Does the candidate string match the IP or range represented by this?
        @param[in] candidate the IP address to be tested
        @return candidate matches this or not
        """
        try:
            hoststruct = HostStruct(candidate)
        except PolicyError:
            return False
        return self.match_bin(hoststruct)

#
#


class PolicyAppConnectionMgr:
    """
    Track policy user/host connection limits and statistics for one app.
    # limits - set at creation and by update()
    max_total            : 20
    max_per_user         : 5
    max_per_host         : 10
    # statistics - maintained for the lifetime of corresponding application
    connections_approved : N
    connections_denied   : N
    # live state - maintained for the lifetime of corresponding application
    connections_active   : 5
    per_host_state : { 'host1' : [conn1, conn2, conn3],
                       'host2' : [conn4, conn5] }
    per_user_state : { 'user1' : [conn1, conn2, conn3],
                       'user2' : [conn4, conn5] }
    """

    def __init__(self, maxconn, maxconnperuser, maxconnperhost):
        """
        The object is constructed with the policy limits and zeroed counts.
        @param[in] maxconn maximum total concurrent connections
        @param[in] maxconnperuser maximum total conncurrent connections for each user
        @param[in] maxconnperuser maximum total conncurrent connections for each host
        """
        if maxconn < 0 or maxconnperuser < 0 or maxconnperhost < 0:
            raise PolicyError("PolicyAppConnectionMgr settings must be >= 0")
        self.max_total    = maxconn
        self.max_per_user = maxconnperuser
        self.max_per_host = maxconnperhost
        self.connections_approved = 0
        self.connections_denied   = 0
        self.connections_active   = 0
        self.per_host_state = {}
        self.per_user_state = {}

    def __str__(self):
        res = ("Connection Limits: total: %s, per user: %s, per host: %s\n" %
               (self.max_total, self.max_per_user, self.max_per_host))
        res += ("Connections Statistics: total approved: %s, total denied: %s" %
                (self.connections_approved, self.connections_denied))
        res += ("Connection State: total current: %s" % self.connections_active)
        res += ("User state: %s\n" % self.per_user_state)
        res += ("Host state: %s"   % self.per_host_state)
        return res

    def __repr__(self):
        return self.__str__()

    def update(self, maxconn, maxconnperuser, maxconnperhost):
        """
        Reset connection limits
        @param[in] maxconn maximum total concurrent connections
        @param[in] maxconnperuser maximum total conncurrent connections for each user
        @param[in] maxconnperuser maximum total conncurrent connections for each host
        """
        if maxconn < 0 or maxconnperuser < 0 or maxconnperhost < 0:
            raise PolicyError("PolicyAppConnectionMgr settings must be >= 0")
        self.max_total    = maxconn
        self.max_per_user = maxconnperuser
        self.max_per_host = maxconnperhost

    def can_connect(self, conn_id, user, host, diags, grp_max_user, grp_max_host):
        """
        Register a connection attempt.
        If all the connection limit rules pass then add the
        user/host to the connection tables.
        @param[in] conn_id unique ID for connection, usually IP:port
        @param[in] user authenticated user ID
        @param[in] host IP address of host
        @param[out] diags on failure holds 1, 2, or 3 error strings
        @return connection is allowed and tracked in state tables
        """
        n_user = 0
        if user in self.per_user_state:
            n_user = len(self.per_user_state[user])
        n_host = 0
        if host in self.per_host_state:
            n_host = len(self.per_host_state[host])

        max_per_user = grp_max_user if grp_max_user is not None else self.max_per_user
        max_per_host = grp_max_host if grp_max_host is not None else self.max_per_host

        allowbytotal = self.connections_active < self.max_total
        allowbyuser  = n_user < max_per_user
        allowbyhost  = n_host < max_per_host

        if allowbytotal and allowbyuser and allowbyhost:
            if user not in self.per_user_state:
                self.per_user_state[user] = []
            self.per_user_state[user].append(conn_id)
            if host not in self.per_host_state:
                self.per_host_state[host] = []
            self.per_host_state[host].append(conn_id)
            self.connections_active += 1
            self.connections_approved += 1
            return True
        else:
            if not allowbytotal:
                diags.append("Connection denied by application connection limit")
            if not allowbyuser:
                diags.append("Connection denied by application per user limit")
            if not allowbyhost:
                diags.append("Connection denied by application per host limit")
            self.connections_denied += 1
            return False

    def disconnect(self, conn_id, user, host):
        """Unregister a connection"""
        assert self.connections_active > 0
        assert user in self.per_user_state
        assert conn_id in self.per_user_state[user]
        assert conn_id in self.per_host_state[host]
        self.connections_active -= 1
        self.per_user_state[user].remove(conn_id)
        self.per_host_state[host].remove(conn_id)

    def count_other_denial(self):
        """
        Record the statistic for a connection denied by some other process
        @return:
        """
        self.connections_denied += 1

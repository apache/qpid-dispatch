/*
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 *
 */

#include "socket_utils.hpp"

#include "SocketException.hpp"

#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <stdexcept>

void fillSockAddr(const std::string &address, unsigned short port, sockaddr_in &addr)
{
    zero(addr);

    hostent *host = gethostbyname(address.c_str());
    if (host == nullptr) {
        throw SocketException("Failed to resolve name (gethostbyname())");
    }

    addr.sin_port   = htons(port);
    addr.sin_family = host->h_addrtype;
    if (host->h_addrtype == AF_INET) {
        auto sin_addr        = reinterpret_cast<struct in_addr *>(host->h_addr_list[0]);
        addr.sin_addr.s_addr = sin_addr->s_addr;
    } else if (host->h_addrtype == AF_INET6) {
        throw std::invalid_argument("IPv6 addresses are not yet supported by the test");
        // auto sin_addr = reinterpret_cast<struct in6_addr *>(host->h_addr_list[0]);
    } else {
        throw SocketException("Name was not resolved to IPv4 (gethostbyname())");
    }
}

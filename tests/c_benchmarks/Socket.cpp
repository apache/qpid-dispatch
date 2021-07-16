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

#include "Socket.hpp"

#include "SocketException.hpp"
#include "socket_utils.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <unistd.h>

Socket::Socket(int type, int protocol) noexcept(false)
{
    mFileDescriptor = socket(PF_INET, type, protocol);
    if (mFileDescriptor < 0) {
        throw SocketException("Socket creation failed (socket())", true);
    }
}

Socket::Socket(int fd)
{
    this->mFileDescriptor = fd;
}

Socket::~Socket()
{
    if (mFileDescriptor < 0) {
        return;  // socket was moved out before
    }

    ::close(mFileDescriptor);
    mFileDescriptor = -1;
}

std::string Socket::getLocalAddress() const
{
    sockaddr_in addr;
    unsigned int addr_len = sizeof(addr);

    if (getsockname(mFileDescriptor, reinterpret_cast<sockaddr *>(&addr), &addr_len) < 0) {
        throw SocketException("Fetch of local address failed (getsockname())", true);
    }
    return inet_ntoa(addr.sin_addr);
}

unsigned short Socket::getLocalPort()
{
    sockaddr_in addr;
    unsigned int addr_len = sizeof(addr);

    if (getsockname(mFileDescriptor, reinterpret_cast<sockaddr *>(&addr), &addr_len) < 0) {
        throw SocketException("Fetch of local port failed (getsockname())", true);
    }
    return ntohs(addr.sin_port);
}

void Socket::setLocalPort(unsigned short localPort)
{
    // Bind the socket to its port
    sockaddr_in localAddr     = {};
    localAddr.sin_family      = AF_INET;
    localAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    localAddr.sin_port        = htons(localPort);

    if (bind(mFileDescriptor, reinterpret_cast<const sockaddr *>(&localAddr), sizeof(sockaddr_in)) < 0) {
        throw SocketException("Set of local port failed (bind())", true);
    }
}

void Socket::setLocalAddressAndPort(const std::string &localAddress, unsigned short localPort)
{
    // Get the address of the requested host
    sockaddr_in localAddr;
    fillSockAddr(localAddress, localPort, localAddr);

    if (bind(mFileDescriptor, reinterpret_cast<const sockaddr *>(&localAddr), sizeof(sockaddr_in)) < 0) {
        throw SocketException("Set of local address and port failed (bind())", true);
    }
}

unsigned short Socket::resolveService(const std::string &service, const std::string &protocol)
{
    struct servent *serv = getservbyname(service.c_str(), protocol.c_str());
    if (serv == nullptr) {
        return atoi(service.c_str());
    } else {
        return ntohs(serv->s_port);
    }
}

void Socket::connect(const std::string &remoteAddress, unsigned short remotePort) noexcept(false)
{
    sockaddr_in destAddr;
    fillSockAddr(remoteAddress, remotePort, destAddr);

    if (::connect(mFileDescriptor, reinterpret_cast<const sockaddr *>(&destAddr), sizeof(destAddr)) < 0) {
        throw SocketException("Connect failed (connect())", true);
    }
}

void Socket::send(const void *buffer, int bufferLen) noexcept(false)
{
    if (::send(mFileDescriptor, buffer, bufferLen, 0) < 0) {
        throw SocketException("Send failed (send())", true);
    }
}

int Socket::recv(void *buffer, int bufferLen) noexcept(false)
{
    int rtn = ::recv(mFileDescriptor, buffer, bufferLen, 0);
    if (rtn < 0) {
        throw SocketException("Received failed (recv())", true);
    }

    return rtn;
}

std::string Socket::getRemoteAddress() noexcept(false)
{
    sockaddr_in addr;
    unsigned int addr_len = sizeof(addr);

    if (getpeername(mFileDescriptor, reinterpret_cast<sockaddr *>(&addr), &addr_len) < 0) {
        throw SocketException("Fetch of remote address failed (getpeername())", true);
    }
    return inet_ntoa(addr.sin_addr);
}

unsigned short Socket::getRemotePort() noexcept(false)
{
    sockaddr_in addr;
    unsigned int addr_len = sizeof(addr);

    if (getpeername(mFileDescriptor, reinterpret_cast<sockaddr *>(&addr), &addr_len) < 0) {
        throw SocketException("Fetch of remote port failed (getpeername())", true);
    }
    return ntohs(addr.sin_port);
}

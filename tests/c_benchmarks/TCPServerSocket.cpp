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

#include "TCPServerSocket.hpp"

#include "SocketException.hpp"

#include <netinet/in.h>
#include <sys/socket.h>

#include <string>

TCPServerSocket::TCPServerSocket(unsigned short localPort, int queueLen) : Socket(SOCK_STREAM, IPPROTO_TCP)
{
    setLocalPort(localPort);
    setListen(queueLen);
}

TCPServerSocket::TCPServerSocket(const std::string &localAddress, unsigned short localPort, int queueLen)
    : Socket(SOCK_STREAM, IPPROTO_TCP)
{
    setLocalAddressAndPort(localAddress, localPort);
    setListen(queueLen);
}

TCPSocket *TCPServerSocket::accept()
{
    int newConnSD = ::accept(mFileDescriptor, nullptr, nullptr);
    if (newConnSD < 0) {
        throw SocketException("Accept failed (accept())", true);
    }

    return new TCPSocket(newConnSD);
}

void TCPServerSocket::shutdown()
{
    ::shutdown(this->mFileDescriptor, ::SHUT_RD);
}

void TCPServerSocket::setListen(int queueLen)
{
    if (listen(mFileDescriptor, queueLen) < 0) {
        throw SocketException("Set listening socket failed (listen())", true);
    }
}
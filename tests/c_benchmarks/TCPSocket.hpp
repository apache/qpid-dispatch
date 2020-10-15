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

#ifndef QPID_DISPATCH_TCPSOCKET_HPP
#define QPID_DISPATCH_TCPSOCKET_HPP

#include "Socket.hpp"
class TCPSocket : public Socket
{
   private:
    friend class TCPServerSocket;
    explicit TCPSocket(int newConnSD);

   public:
    TCPSocket();
    TCPSocket(const std::string& remoteAddress, unsigned short remotePort);
    TCPSocket(TCPSocket&& socket) noexcept : TCPSocket(socket.mFileDescriptor)
    {
        socket.mFileDescriptor = -1;
    };
};

#endif  // QPID_DISPATCH_TCPSOCKET_HPP

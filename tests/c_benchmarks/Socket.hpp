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

#ifndef QPID_DISPATCH_SOCKET_HPP
#define QPID_DISPATCH_SOCKET_HPP

#include <string>
class Socket
{
   public:
    ~Socket();
    std::string getLocalAddress() const;
    unsigned short getLocalPort();
    void setLocalPort(unsigned short localPort);
    void setLocalAddressAndPort(const std::string &localAddress, unsigned short localPort = 0);
    static unsigned short resolveService(const std::string &service, const std::string &protocol = "tcp");
    Socket(const Socket &&sock) noexcept : mFileDescriptor(sock.mFileDescriptor)
    {
    }

   private:
    Socket(const Socket &sock);
    void operator=(const Socket &sock);

   protected:
    int mFileDescriptor;
    Socket(int type, int protocol) noexcept(false);
    explicit Socket(int fd);

   public:
    void connect(const std::string &remoteAddress, unsigned short remotePort) noexcept(false);
    void send(const void *buffer, int bufferLen) noexcept(false);
    int recv(void *buffer, int bufferLen) noexcept(false);
    std::string getRemoteAddress() noexcept(false);
    unsigned short getRemotePort() noexcept(false);
};

#endif  // QPID_DISPATCH_SOCKET_HPP

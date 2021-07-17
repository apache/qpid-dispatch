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

#ifndef QPID_DISPATCH_SOCKET_UTILS_HPP
#define QPID_DISPATCH_SOCKET_UTILS_HPP

#include <netinet/in.h>

#include <cstring>
#include <string>

// Saw warning to not use `= {}` to initialize socket structs; it supposedly does not work right
//  due to casting and c-style polymorphism there. It's working just fine for me, though.
template <class T>
inline void zero(T &value)
{
    memset(&value, 0, sizeof(value));
}

void fillSockAddr(const std::string &address, unsigned short port, sockaddr_in &addr);

#endif  // QPID_DISPATCH_SOCKET_UTILS_HPP

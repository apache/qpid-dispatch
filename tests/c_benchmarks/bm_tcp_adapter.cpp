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

#include "../c_unittests/helpers.hpp"
#include "SocketException.hpp"
#include "TCPSocket.hpp"
#include "echo_server.hpp"

#include <benchmark/benchmark.h>

#include <iostream>

extern "C" {
#include "entity_cache.h"
#include "log_private.h"
#include "parse_tree.h"

#include "qpid/dispatch.h"

// declarations that don't have .h file
void qd_error_initialize();
}  // extern "C"

static TCPSocket try_to_connect(const std::string &servAddress, int echoServPort)
{
    auto then = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - then < std::chrono::seconds(3)) {
        try {
            TCPSocket sock(servAddress, echoServPort);
            return sock;
        } catch (SocketException &e) {
        }
    }
    throw std::runtime_error("Failed to connect in time");
}

class LatencyMeasure
{
    static const int RCVBUFSIZE = 32;
    char echoBuffer[RCVBUFSIZE + 1];  // '\0'

    std::string servAddress = "127.0.0.1";
    std::string echoString  = "echoString";
    int echoStringLen       = echoString.length();

   public:
    inline void latencyMeasureLoop(benchmark::State &state, unsigned short echoServerPort)
    {
        {
            TCPSocket sock = try_to_connect(servAddress, echoServerPort);
            latencyMeasureSendReceive(state, sock);  // run once outside benchmark to clean the pipes first

            for (auto _ : state) {
                latencyMeasureSendReceive(state, sock);
            }
        }
    }

    inline void latencyMeasureSendReceive(benchmark::State &state, TCPSocket &sock)
    {
        sock.send(echoString.c_str(), echoStringLen);

        int totalBytesReceived = 0;
        while (totalBytesReceived < echoStringLen) {
            int bytesReceived = sock.recv(echoBuffer, RCVBUFSIZE);
            if (bytesReceived <= 0) {
                state.SkipWithError("unable to read from socket");
            }
            totalBytesReceived += bytesReceived;
            echoBuffer[bytesReceived] = '\0';
        }
    }
};

/// Measures latency between a TCP send and a receive.
/// There is only one request in flight at all times, so this is the
///  lowest conceivable latency at the most ideal condition
/// In addition, all sends are of the same (tiny) size
static void BM_TCPEchoServerLatencyWithoutQDR(benchmark::State &state)
{
    EchoServerThread est;

    LatencyMeasure lm;
    lm.latencyMeasureLoop(state, est.port());
}

BENCHMARK(BM_TCPEchoServerLatencyWithoutQDR)->Unit(benchmark::kMillisecond);

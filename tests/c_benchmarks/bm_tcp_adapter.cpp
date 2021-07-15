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
#include <linux/prctl.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <csignal>
#include <iostream>

extern "C" {
#include "entity_cache.h"
#include "log_private.h"
#include "parse_tree.h"

#include "qpid/dispatch.h"

// declarations that don't have .h file
void qd_error_initialize();
}  // extern "C"

static unsigned short findFreePort()
{
    TCPServerSocket serverSocket(0);
    unsigned short port = serverSocket.getLocalPort();
    return port;
}

static std::stringstream oneRouterTcpConfig(const unsigned short tcpConnectorPort, unsigned short tcpListenerPort)
{
    std::stringstream router_config;
    router_config << R"END(
router {
    mode: standalone
    id : QDR
}

listener {
    port : 0
}

tcpListener {
    host : 0.0.0.0
    port : )END" << tcpListenerPort
                  << R"END(
    address : ES
    siteId : siteId
}

tcpConnector {
    host : 127.0.0.1
    port : )END" << tcpConnectorPort
                  << R"END(
    address : ES
    siteId : siteId
}

log {
    module: DEFAULT
    enable: warning+
})END";

    return router_config;
}

static std::stringstream multiRouterTcpConfig(std::string routerId, const std::vector<unsigned short> listenerPorts,
                                              const std::vector<unsigned short> connectorPorts,
                                              unsigned short tcpConnectorPort, unsigned short tcpListenerPort)
{
    std::stringstream router_config;
    router_config << R"END(
router {
    mode: interior
    id : )END" << routerId
                  << R"END(
})END";

    for (auto connectorPort : connectorPorts) {
        router_config << R"END(
connector {
    host: 127.0.0.1
    port : )END" << connectorPort
                      << R"END(
    role: inter-router
})END";
    }

    for (auto listenerPort : listenerPorts) {
        router_config << R"END(
listener {
    host: 0.0.0.0
        port : )END" << listenerPort
                      << R"END(
    role: inter-router
        })END";
    }

    if (tcpListenerPort != 0) {
        router_config << R"END(
tcpListener {
    host : 0.0.0.0
    port : )END" << tcpListenerPort
                      << R"END(
    address : ES
    siteId : siteId
})END";
    }
    if (tcpConnectorPort != 0) {
        router_config << R"END(
tcpConnector {
    host : 127.0.0.1
    port : )END" << tcpConnectorPort
                      << R"END(
    address : ES
    siteId : siteId
})END";
    }

    router_config << R"END(
log {
    module: DEFAULT
    enable: trace+
})END";

    return router_config;
}

static void writeRouterConfig(const std::string &configName, const std::stringstream &router_config)
{
    std::fstream f(configName, std::ios::out);
    f << router_config.str();
    f.close();
}

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

class DispatchRouterThreadTCPLatencyTest
{
    QDR mQdr{};
    std::thread mT;

   public:
    DispatchRouterThreadTCPLatencyTest(const std::string configName, const unsigned short tcpConnectorPort,
                                       const unsigned short tcpListenerPort)
    {
        Latch mx;

        std::stringstream router_config;
        router_config = oneRouterTcpConfig(tcpConnectorPort, tcpListenerPort);
        writeRouterConfig(configName, router_config);

        mT = std::thread([&mx, this, &configName]() {
            mQdr.initialize(configName);
            mQdr.wait();

            mx.notify();
            mQdr.run();

            mQdr.deinitialize(false);
        });

        mx.wait();
    }

    ~DispatchRouterThreadTCPLatencyTest()
    {
        mQdr.stop();
        mT.join();
    }
};

class DispatchRouterSubprocessTcpLatencyTest
{
    int pid;

   public:
    DispatchRouterSubprocessTcpLatencyTest(std::string configName)
    {
        pid = fork();
        if (pid == 0) {
            // https://stackoverflow.com/questions/10761197/prctlpr-set-pdeathsig-signal-is-called-on-parent-thread-exit-not-parent-proc
            prctl(PR_SET_PDEATHSIG, SIGHUP);
            QDR qdr{};
            qdr.initialize(configName);
            qdr.wait();

            qdr.run();

            exit(0);
        }
    }

    ~DispatchRouterSubprocessTcpLatencyTest()
    {
        int ret = kill(pid, SIGTERM);
        if (ret != 0) {
            perror("Killing router");
        }
        int status;
        ret = waitpid(pid, &status, 0);
        if (ret != pid) {
            perror("Waiting for child");
        }
    }
};

static void BM_TCPEchoServerLatency1QDRThread(benchmark::State &state)
{
    auto est                        = make_unique<EchoServerThread>();
    unsigned short tcpConnectorPort = est->port();
    unsigned short tcpListenerPort  = findFreePort();

    DispatchRouterThreadTCPLatencyTest drt{"BM_TCPEchoServerLatency1QDRThread", tcpConnectorPort, tcpListenerPort};

    {
        LatencyMeasure lm;
        lm.latencyMeasureLoop(state, tcpListenerPort);
    }
    // kill echo server first
    // when dispatch is stopped first, echo server then sometimes hangs on socket recv, and dispatch leaks more
    // (suppressed leaks):
    /*
        76: Assertion `leak_reports.length() == 0` failed in ../tests/c_benchmarks/../c_unittests/helpers.hpp line 136:
            alloc.c: Items of type 'qd_buffer_t' remain allocated at shutdown: 3 (SUPPRESSED)
        76: alloc.c: Items of type 'qd_message_t' remain allocated at shutdown: 2 (SUPPRESSED)
        76: alloc.c: Items of type 'qd_message_content_t' remain allocated at shutdown: 2 (SUPPRESSED)
        76: alloc.c: Items of type 'qdr_delivery_t' remain allocated at shutdown: 2 (SUPPRESSED)
     */
    est.reset();
}

//BENCHMARK(BM_TCPEchoServerLatency1QDRThread)->Unit(benchmark::kMillisecond);

static void BM_TCPEchoServerLatency1QDRSubprocess(benchmark::State &state)
{
    EchoServerThread est;
    unsigned short tcpConnectorPort = est.port();
    unsigned short tcpListenerPort  = findFreePort();

    std::string configName          = "BM_TCPEchoServerLatency1QDRSubprocess.conf";
    std::stringstream router_config = oneRouterTcpConfig(tcpConnectorPort, tcpListenerPort);
    writeRouterConfig(configName, router_config);

    DispatchRouterSubprocessTcpLatencyTest drt(configName);

    {
        LatencyMeasure lm;
        lm.latencyMeasureLoop(state, tcpListenerPort);
    }
}

BENCHMARK(BM_TCPEchoServerLatency1QDRSubprocess)->Unit(benchmark::kMillisecond);

static void BM_TCPEchoServerLatency2QDRSubprocess(benchmark::State &state)
{
    EchoServerThread est;

    unsigned short listener_2 = findFreePort();
    unsigned short tcpListener_2 = findFreePort();

    std::string configName_1          = "BM_TCPEchoServerLatency2QDRSubprocess_1.conf";
    std::stringstream router_config_1 = multiRouterTcpConfig("QDRL1", {}, {listener_2}, est.port(), 0);
    writeRouterConfig(configName_1, router_config_1);

    std::string configName_2          = "BM_TCPEchoServerLatency2QDRSubprocess_2.conf";
    std::stringstream router_config_2 = multiRouterTcpConfig("QDRL2", {listener_2}, {}, 0, tcpListener_2);
    writeRouterConfig(configName_2, router_config_2);

    int pid = fork();
    if (pid == 0) {
        QDR qdr1{};
        qdr1.initialize(configName_1);
        qdr1.wait();

        qdr1.run();  // this never returns, until signal is sent, and then process dies
        exit(0);
    }

    int pid2 = fork();
    if (pid2 == 0) {
        QDR qdr2{};
        qdr2.initialize(configName_2);
        qdr2.wait();

        qdr2.run();
        exit(0);
    }

    printf("going for the loop\n");
    {
        LatencyMeasure lm;
        lm.latencyMeasureLoop(state, tcpListener_2);
    }

    printf("running teardown\n");

    int ret = kill(pid, SIGTERM);
    if (ret != 0) {
        perror("Killing router 1");
    }
    int status;
    ret = waitpid(pid, &status, 0);
    if (ret != pid) {
        perror("Waiting for child");
    }
    ret = kill(pid2, SIGTERM);
    if (ret != 0) {
        perror("Killing router 2");
    }
    ret = waitpid(pid2, &status, 0);
    if (ret != pid2) {
        perror("Waiting for child2");
    }
}

BENCHMARK(BM_TCPEchoServerLatency2QDRSubprocess)->Unit(benchmark::kMillisecond);
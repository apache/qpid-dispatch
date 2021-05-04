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

#include "PracticalSocket.h"  // For Socket and SocketException
#include "echo_server.h"

#include <benchmark/benchmark.h>
#include <sys/wait.h>
#include <unistd.h>

#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <cstdlib>  // For atoi()
#include <cstring>
#include <iostream>  // For cerr and cout
#include <thread>
#include <sys/prctl.h>

using namespace std;

extern "C" {
#include "qpid/dispatch.h"
#include "log_private.h"
#include "parse_tree.h"
#include "entity_cache.h"

// declarations that don't have .h file
void qd_error_initialize();
}

class QDRMinimalEnv {
   public:
    QDRMinimalEnv() {
        qd_alloc_initialize();
        qd_log_initialize();
        qd_error_initialize();
    }

    ~QDRMinimalEnv() {
        qd_log_finalize();
        qd_alloc_finalize();
    }
};


void warmupIteration(TCPSocket &sock) {
    std::string echoString = "baf";
    int echoStringLen = echoString.length();
    const int RCVBUFSIZE = 32;

    //        cout << "sending" << endl;
    // Send the string to the echo server
    sock.send(echoString.c_str(), echoStringLen);

    char echoBuffer[RCVBUFSIZE + 1];    // Buffer for echo string + \0
    int bytesReceived = 0;              // Bytes read on each recv()
    int totalBytesReceived = 0;         // Total bytes read
    // Receive the same string back from the server
//            cout << "Received: ";               // Setup to print the echoed string
    while (totalBytesReceived < echoStringLen) {
        // Receive up to the buffer size bytes from the sender
        if ((bytesReceived = (sock.recv(echoBuffer, RCVBUFSIZE))) <= 0) {
            cerr << "Unable to read";
            return;
        }
        totalBytesReceived += bytesReceived;     // Keep tally of total bytes
        echoBuffer[bytesReceived] = '\0';        // Terminate the string!
//                cout << echoBuffer;                      // Print the echo buffer
    }
}

static void BM_AddRemovePattern(benchmark::State &state) {
    std::thread([&state]{
        QDRMinimalEnv env{};

        qd_iterator_t *piter  = qd_iterator_string("I.am.Sam", ITER_VIEW_ALL);
        qd_parse_tree_t *node = qd_parse_tree_new(QD_PARSE_TREE_ADDRESS);
        void *payload;

        for (auto _ : state) {
            qd_parse_tree_add_pattern(node, piter, &payload);
            qd_parse_tree_remove_pattern(node, piter);
        }

        qd_parse_tree_free(node);
        qd_iterator_free(piter);
    }).join();
}

BENCHMARK(BM_AddRemovePattern)->Unit(benchmark::kMicrosecond);


// https://github.com/apache/qpid-dispatch/pull/732/files
static void BM_RouterInitializeMinimalConfig(benchmark::State &state) {
    for(auto _: state) {
        std::thread([]() {
            QDR qdr{};
            qdr.initialize("minimal_silent.conf");
            qdr.wait();
            qdr.deinitialize();
        }).join();
    }
}

BENCHMARK(BM_RouterInitializeMinimalConfig)->Unit(benchmark::kMillisecond);

static void BM_ZZZTCPEchoServerLatency1QDR(benchmark::State &state) {
//    std::condition_variable cv;
//    std::unique_lock<std::mutex> lk(cv);
    std::mutex mx;
    mx.lock();
    std::mutex nx;
    nx.lock();
    QDR qdr{};
    auto t = std::thread([&mx, &qdr]() {
        qdr.initialize("tcp_benchmarks.conf");
        qdr.wait();

        mx.unlock();
        qdr.run();

        qdr.deinitialize(false);
    });

//    run_echo_server();
    auto u = std::thread([]() { run_echo_server(); });
    std::this_thread::sleep_for(std::chrono::seconds(1));


    const int RCVBUFSIZE = 32;    // Size of receive buffer

    string servAddress = "127.0.0.1"; // First arg: server address
    const char *echoString = "baf";   // Second arg: string to echo
    int echoStringLen = strlen(echoString);   // Determine input length
    unsigned short echoServPort = 5673;


    mx.lock();

    {
    TCPSocket sock(servAddress, echoServPort);
        warmupIteration(sock);

    for(auto _: state) {
//        cout << "sending" << endl;
        // Send the string to the echo server
        sock.send(echoString, echoStringLen);

        char echoBuffer[RCVBUFSIZE + 1];    // Buffer for echo string + \0
        int bytesReceived = 0;              // Bytes read on each recv()
        int totalBytesReceived = 0;         // Total bytes read
        // Receive the same string back from the server
//        cout << "Received: ";               // Setup to print the echoed string
        while (totalBytesReceived < echoStringLen) {
            // Receive up to the buffer size bytes from the sender
            if ((bytesReceived = (sock.recv(echoBuffer, RCVBUFSIZE))) <= 0) {
                cerr << "Unable to read";
                state.SkipWithError("unable to read");
            }
            totalBytesReceived += bytesReceived;     // Keep tally of total bytes
            echoBuffer[bytesReceived] = '\0';        // Terminate the string!
//            cout << echoBuffer;                      // Print the echo buffer
        }
    }
    }

    // if I kill dispatch first, this then may/will hang on socket recv (and dispatch leaks significantly more)
    stop_echo_server();
    u.join();

    qdr.stop();
    t.join();
}

TCPSocket try_to_connect(const std::string& servAddress, int echoServPort) {
    auto then = std::chrono::steady_clock::now();
    while(std::chrono::steady_clock::now() - then < std::chrono::seconds(30)) {
        try {
//            printf("trying to connect\n");
            TCPSocket sock(servAddress, echoServPort);
            return sock;
        } catch (SocketException &e) {
        }
    }
    throw std::runtime_error("Failed to connect in time");
}

static void BM_TCPEchoServerLatency1QDRSubprocess(benchmark::State &state) {
//    std::condition_variable cv;
//    std::unique_lock<std::mutex> lk(cv);
    std::mutex mx;
    mx.lock();
    std::mutex nx;
    nx.lock();

    pid_t leader = getpid();
    printf("getpid, %d", leader);
    int pid = fork();
    if (pid == 0) {
        // https://stackoverflow.com/questions/10761197/prctlpr-set-pdeathsig-signal-is-called-on-parent-thread-exit-not-parent-proc
        prctl(PR_SET_PDEATHSIG, SIGHUP);
        QDR qdr{};
      qdr.initialize("tcp_benchmarks.conf");
      qdr.wait();

      qdr.run();

      exit(0);
    };

    auto u = std::thread([]() { run_echo_server(); });
//    std::this_thread::sleep_for(std::chrono::seconds(1));

    const int RCVBUFSIZE = 32;    // Size of receive buffer

    string servAddress = "127.0.0.1"; // First arg: server address
    const char *echoString = "baf";   // Second arg: string to echo
    int echoStringLen = strlen(echoString);   // Determine input length
    unsigned short echoServPort = 5673;

    {
        TCPSocket sock = try_to_connect(servAddress, echoServPort);
//        TCPSocket sock(servAddress, echoServPort);
        warmupIteration(sock);

        for(auto _: state) {
//        cout << "sending" << endl;
            // Send the string to the echo server
            sock.send(echoString, echoStringLen);

            char echoBuffer[RCVBUFSIZE + 1];    // Buffer for echo string + \0
            int bytesReceived = 0;              // Bytes read on each recv()
            int totalBytesReceived = 0;         // Total bytes read
            // Receive the same string back from the server
//        cout << "Received: ";               // Setup to print the echoed string
            while (totalBytesReceived < echoStringLen) {
                // Receive up to the buffer size bytes from the sender
                if ((bytesReceived = (sock.recv(echoBuffer, RCVBUFSIZE))) <= 0) {
                    cerr << "Unable to read";
                    state.SkipWithError("unable to read");
                }
                totalBytesReceived += bytesReceived;     // Keep tally of total bytes
                echoBuffer[bytesReceived] = '\0';        // Terminate the string!
//            cout << echoBuffer;                      // Print the echo buffer
            }
        }
    }

    // if I kill dispatch first, this then may/will hang on socket recv (and dispatch leaks significantly more)
    stop_echo_server();
    u.join();

    int ret = kill(pid, SIGTERM);
    if (ret != 0) {
        perror("Killing router 1");
    }
    int status;
    ret = waitpid(pid, &status, 0);
    if (ret != pid) {
        perror("Waiting for child");
    }
}

BENCHMARK(BM_TCPEchoServerLatency1QDRSubprocess)->Unit(benchmark::kMillisecond);

/*
 * options:
 *  python, run as subprocesses, the test will also be a subprocess
 *  c++, need some fancy lib to manage subprocesses
 *  try figure out threads, asan?
 */
//
//

/*
 * must be subprocess, timer.c: static qd_timer_list_t  idle_timers = {0};
 * both dispatches try to init Python; can let them run startup one after another, ... but that's not a good solution
 */

static void BM_TCPEchoServerLatency2QDR(benchmark::State &state) {
//    std::condition_variable cv;
//    std::unique_lock<std::mutex> lk(cv);
    auto u = std::thread([]() { run_echo_server(); });

    std::mutex my;
    my.lock();
    std::mutex nx;
    nx.lock();

    QDR qdr2{};
    int pid = fork();
    if (pid == 0) {
//        auto t = std::thread([&mx, &qdr1]() {
//        std::mutex mx;
//        mx.lock();
        QDR qdr1{};
            qdr1.initialize("./l1.conf");
            qdr1.wait();

//            mx.unlock();
            qdr1.run();  // this never returns, until signal is sent, and then process dies
            printf("Subprocess router died\n");
            exit(0);
//
//            qdr1.stop();
//        });
    }
//    mx.lock();
    // if this does not work, I can always fork...
//    std::this_thread::sleep_for(std::chrono::seconds(3));

    int pid2 = fork();
    if (pid2 == 0) {
//    auto t2 = std::thread([&my, &qdr2]() {
        qdr2.initialize("./l2.conf");
        qdr2.wait();

//        my.unlock();
        qdr2.run();
        printf("calling exit(0) on l2\n");
        exit(0);

        qdr2.deinitialize();
//    });
//    my.lock();
    }

//    run_echo_server();

//    std::this_thread::sleep_for(std::chrono::seconds(1));

    // even more time to setup router network
    //    std::this_thread::sleep_for(std::chrono::seconds(3));

    const int RCVBUFSIZE = 32;    // Size of receive buffer

    string servAddress = "127.0.0.1"; // First arg: server address
    const char *echoString = "baf";   // Second arg: string to echo
    int echoStringLen = strlen(echoString);   // Determine input length
    unsigned short echoServPort = 5673;

    printf("going for the loop\n");
    {
        TCPSocket sock = try_to_connect(servAddress, echoServPort);
        warmupIteration(sock);

        for(auto _: state) {
//        cout << "sending" << endl;
            // Send the string to the echo server
            sock.send(echoString, echoStringLen);

            char echoBuffer[RCVBUFSIZE + 1];    // Buffer for echo string + \0
            int bytesReceived = 0;              // Bytes read on each recv()
            int totalBytesReceived = 0;         // Total bytes read
            // Receive the same string back from the server
//            cout << "Received: ";               // Setup to print the echoed string
            while (totalBytesReceived < echoStringLen) {
                // Receive up to the buffer size bytes from the sender
                if ((bytesReceived = (sock.recv(echoBuffer, RCVBUFSIZE))) <= 0) {
                    cerr << "Unable to read";
                    state.SkipWithError("unable to read");
                }
                totalBytesReceived += bytesReceived;     // Keep tally of total bytes
                echoBuffer[bytesReceived] = '\0';        // Terminate the string!
//                cout << echoBuffer;                      // Print the echo buffer
            }
        }
    }

    printf("running teardown\n");

    // if I kill dispatch first, this then may/will hang on socket recv (and dispatch leaks significantly more)
    stop_echo_server();
    u.join();

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
//    qdr1.stop_run();
//    t.join();
//    std::thread([&qdr2]() { qdr2.stop_run(); }).join();
//    qdr2.stop_run();
//    t2.join();
}

BENCHMARK(BM_TCPEchoServerLatency2QDR)->Unit(benchmark::kMillisecond);
    //->MinTime(2);
    //->Iterations(2000);

static void BM_TCPEchoServerLatencyWithoutQDR(benchmark::State &state) {
//    std::condition_variable cv;
//    std::unique_lock<std::mutex> lk(cv);
    std::mutex mx;
    mx.lock();
    std::mutex nx;
    nx.lock();
//    QDR qdr{};
//    auto t = std::thread([&mx, &qdr]() {
//        qdr.start();
//        qdr.wait();
//
//        mx.unlock();
//        qdr.run();
//
//        qdr.stop();
//    });

//    run_echo_server();
    auto u = std::thread([]() { run_echo_server(); });
//    std::this_thread::sleep_for(std::chrono::seconds(1));


    const int RCVBUFSIZE = 32;    // Size of receive buffer

    string servAddress = "127.0.0.1"; // First arg: server address
    const char *echoString = "baf";   // Second arg: string to echo
    int echoStringLen = strlen(echoString);   // Determine input length
    unsigned short echoServPort = 5674;


//    mx.lock();

    {
        TCPSocket sock = try_to_connect(servAddress, echoServPort);

        for(auto _: state) {
//            cout << "sending" << endl;
            // Send the string to the echo server
            sock.send(echoString, echoStringLen);

            char echoBuffer[RCVBUFSIZE + 1];    // Buffer for echo string + \0
            int bytesReceived = 0;              // Bytes read on each recv()
            int totalBytesReceived = 0;         // Total bytes read
            // Receive the same string back from the server
//            cout << "Received: ";               // Setup to print the echoed string
            while (totalBytesReceived < echoStringLen) {
                // Receive up to the buffer size bytes from the sender
                if ((bytesReceived = (sock.recv(echoBuffer, RCVBUFSIZE))) <= 0) {
                    cerr << "Unable to read";
                    state.SkipWithError("unable to read");
                }
                totalBytesReceived += bytesReceived;     // Keep tally of total bytes
                echoBuffer[bytesReceived] = '\0';        // Terminate the string!
//                cout << echoBuffer;                      // Print the echo buffer
            }
        }
    }
//    qdr.stop_run();

//    t.join();

    stop_echo_server();
    u.join();
}

BENCHMARK(BM_TCPEchoServerLatencyWithoutQDR)->Unit(benchmark::kMillisecond);

// breaks the two-router test if it runs before
BENCHMARK(BM_ZZZTCPEchoServerLatency1QDR)->Unit(benchmark::kMillisecond);

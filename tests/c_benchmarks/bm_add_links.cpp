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

#include <benchmark/benchmark.h>
#include <cstdio>
#include <cstring>
#include <thread>
#include <condition_variable>
#include "./helpers.hpp"

#include "echo_server.h"

#include "PracticalSocket.h"  // For Socket and SocketException
#include <iostream>           // For cerr and cout
#include <cstdlib>            // For atoi()

using namespace std;

extern "C" {
#include <qpid/dispatch.h>
#include "parse_tree.h"
#include "entity_cache.h"
}

static void BM_AddRemovePattern(benchmark::State &state) {
    qd_iterator_t *  piter = qd_iterator_string("I.am.Sam", ITER_VIEW_ALL);
    qd_parse_tree_t *node = qd_parse_tree_new(QD_PARSE_TREE_ADDRESS);
    void *           payload;

    for (auto _ : state) {
        return;
        qd_parse_tree_add_pattern(node, piter, &payload);
        qd_parse_tree_remove_pattern(node, piter);
    }

    qd_parse_tree_free(node);
    qd_iterator_free(piter);
}

//BENCHMARK(BM_AddRemovePattern)->Unit(benchmark::kMicrosecond);


// https://github.com/apache/qpid-dispatch/pull/732/files
static void BM_AddAutolink(benchmark::State &state) {
    for(auto _: state) {
        // TODO
        std::thread([]() {
            QDR qdr{};
            qdr.start();
            qdr.wait();
            qdr.stop();
        }).join();
    }
}

//BENCHMARK(BM_AddAutolink)->Unit(benchmark::kMillisecond);

static void BM_TCPEchoServerLatency1QDR(benchmark::State &state) {
//    std::condition_variable cv;
//    std::unique_lock<std::mutex> lk(cv);
    std::mutex mx;
    mx.lock();
    std::mutex nx;
    nx.lock();
    QDR qdr{};
    auto t = std::thread([&mx, &qdr]() {
        qdr.start();
        qdr.wait();

        mx.unlock();
        qdr.run();

        qdr.stop();
    });

//    run_echo_server();
    auto u = std::thread([]() { run_echo_server(); });
    std::this_thread::sleep_for(std::chrono::seconds(1));


    const int RCVBUFSIZE = 32;    // Size of receive buffer

    string servAddress = "127.0.0.1"; // First arg: server address
    char *echoString = "baf";   // Second arg: string to echo
    int echoStringLen = strlen(echoString);   // Determine input length
    unsigned short echoServPort = 5673;


    mx.lock();

    {
    TCPSocket sock(servAddress, echoServPort);

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

    qdr.stop_run();
    t.join();
}

BENCHMARK(BM_TCPEchoServerLatency1QDR)->Unit(benchmark::kMillisecond);

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
    std::this_thread::sleep_for(std::chrono::seconds(1));


    const int RCVBUFSIZE = 32;    // Size of receive buffer

    string servAddress = "127.0.0.1"; // First arg: server address
    char *echoString = "baf";   // Second arg: string to echo
    int echoStringLen = strlen(echoString);   // Determine input length
    unsigned short echoServPort = 5674;


//    mx.lock();

    {
        TCPSocket sock(servAddress, echoServPort);

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
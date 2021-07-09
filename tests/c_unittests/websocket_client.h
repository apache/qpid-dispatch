#ifndef QPID_DISPATCH_WEBSOCKET_CLIENT_H
#define QPID_DISPATCH_WEBSOCKET_CLIENT_H
/*
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
 */

#include "libwebsockets.h"
#include "qdr_doctest.h"

#include <atomic>
#include <condition_variable>
#include <string>
#include <utility>

int
callback_minimal(struct lws *wsi, enum lws_callback_reasons reason,
                 void *user, void *in, size_t len);

static const struct lws_protocols protocols[] = {
    { "amqp", callback_minimal, sizeof(void *), 0, },
    { nullptr, nullptr, 0, 0 }
};

class websocket_client
{
    struct lws_context *context;
    struct lws *web_socket = nullptr;

    std::string host;
    int port;

   public:
    std::mutex mutex {};
    std::condition_variable cv {};
    bool is_connected = false;
    std::atomic<bool> interrupt {false};

    websocket_client(std::string host, int port):
    host(std::move(host)), port(port)
    {
        struct lws_context_creation_info info{};
        info.options = LWS_SERVER_OPTION_DISABLE_OS_CA_CERTS; /* save time by skipping file i/o */
        info.port = CONTEXT_PORT_NO_LISTEN; /* we do not run any server */
        info.protocols = protocols;
        info.gid = -1;
        info.uid = -1;

        context = lws_create_context(&info);
        if (!context) {
            FAIL("lws init failed");
        }
    }

    ~websocket_client() noexcept {
        lws_context_destroy(context);
    }

    void loop() {
        while(!interrupt)
        {
            /* Connect if we are not connected to the server. */
            if(!web_socket)
            {
                struct lws_client_connect_info ccinfo = {nullptr};
                ccinfo.context = context;
                ccinfo.address = this->host.c_str();
                ccinfo.port = port;
                ccinfo.path = "/";
                ccinfo.host = lws_canonical_hostname(context);
                ccinfo.origin = "origin";
                ccinfo.protocol = "amqp";
                ccinfo.userdata = this;
                web_socket = lws_client_connect_via_info(&ccinfo);
            }

            lws_callback_on_writable(web_socket);

            lws_service(context, /* timeout_ms = */ 250);
        }
    }
};

#endif  // QPID_DISPATCH_WEBSOCKET_CLIENT_H

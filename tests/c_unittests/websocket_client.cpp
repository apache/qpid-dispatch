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

#include "websocket_client.h"

#include "qdr_doctest.h"

int
callback_minimal(struct lws *wsi, enum lws_callback_reasons reason,
                 void *user, void *in, size_t len)
{
    auto *wsc = reinterpret_cast<websocket_client *>(user);

    switch (reason) {

        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            FAIL("CLIENT_CONNECTION_ERROR: ", static_cast<char *>(in));
            break;

        case LWS_CALLBACK_CLIENT_ESTABLISHED: {
            std::unique_lock<std::mutex> lck(wsc->mutex);
            wsc->is_connected = true;
            lck.unlock();
            wsc->cv.notify_all();
            break;
        }

        default:
            break;
    }

    return lws_callback_http_dummy(wsi, reason, user, in, len);

    return 0;
}
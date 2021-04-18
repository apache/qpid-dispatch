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

#include "http1_private.h"

#include "qpid/dispatch/ctools.h"
#include "qpid/dispatch/server.h"

#include <inttypes.h>

static void _http1_record_request_info(qdr_http1_adaptor_t *adaptor, qdr_http1_request_base_t *request, const char *host, bool ingress)
{
     uint64_t in_octets, out_octets;
     h1_codec_request_state_counters(request->lib_rs, &in_octets, &out_octets);
     qd_http_record_request(adaptor->core, h1_codec_request_state_method(request->lib_rs), h1_codec_request_state_response_code(request->lib_rs),
                            request->hconn->cfg.address, host, request->hconn->cfg.site, request->site, ingress,
                            in_octets, out_octets, request->stop && request->start ? request->stop - request->start : 0);
}

void qdr_http1_record_client_request_info(qdr_http1_adaptor_t *adaptor, qdr_http1_request_base_t *request)
{
    char *host = qd_get_host_from_host_port(request->hconn->client.client_ip_addr);
    _http1_record_request_info(adaptor, request, host ? host : request->hconn->client.client_ip_addr, true);
    if (host) free(host);
}

void qdr_http1_record_server_request_info(qdr_http1_adaptor_t *adaptor, qdr_http1_request_base_t *request)
{
    _http1_record_request_info(adaptor, request, request->hconn->cfg.host, false);
}

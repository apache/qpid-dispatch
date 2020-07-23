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

#include <qpid/dispatch/amqp.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>

const char * const QD_MA_PREFIX  = "x-opt-qd.";
const char * const QD_MA_INGRESS = "x-opt-qd.ingress";
const char * const QD_MA_TRACE   = "x-opt-qd.trace";
const char * const QD_MA_TO      = "x-opt-qd.to";
const char * const QD_MA_PHASE   = "x-opt-qd.phase";
const char * const QD_MA_CLASS   = "x-opt-qd.class";
const int          QD_MA_MAX_KEY_LEN = 16;
const int          QD_MA_N_KEYS      = 4;  // max number of router annotations to send/receive
const int          QD_MA_FILTER_LEN  = 5;  // N tailing inbound entries to search for stripping

const char * const QD_CAPABILITY_ROUTER_CONTROL   = "qd.router";
const char * const QD_CAPABILITY_ROUTER_DATA      = "qd.router-data";
const char * const QD_CAPABILITY_EDGE_DOWNLINK    = "qd.router-edge-downlink";
const char * const QD_CAPABILITY_WAYPOINT_DEFAULT = "qd.waypoint";
const char * const QD_CAPABILITY_WAYPOINT1        = "qd.waypoint.1";
const char * const QD_CAPABILITY_WAYPOINT2        = "qd.waypoint.2";
const char * const QD_CAPABILITY_WAYPOINT3        = "qd.waypoint.3";
const char * const QD_CAPABILITY_WAYPOINT4        = "qd.waypoint.4";
const char * const QD_CAPABILITY_WAYPOINT5        = "qd.waypoint.5";
const char * const QD_CAPABILITY_WAYPOINT6        = "qd.waypoint.6";
const char * const QD_CAPABILITY_WAYPOINT7        = "qd.waypoint.7";
const char * const QD_CAPABILITY_WAYPOINT8        = "qd.waypoint.8";
const char * const QD_CAPABILITY_WAYPOINT9        = "qd.waypoint.9";
const char * const QD_CAPABILITY_FALLBACK         = "qd.fallback";
const char * const QD_CAPABILITY_ANONYMOUS_RELAY  = "ANONYMOUS-RELAY";

const char * const QD_DYNAMIC_NODE_PROPERTY_ADDRESS = "x-opt-qd.address";

const char * const QD_CONNECTION_PROPERTY_PRODUCT_KEY           = "product";
const char * const QD_CONNECTION_PROPERTY_PRODUCT_VALUE         = "qpid-dispatch-router";
const char * const QD_CONNECTION_PROPERTY_VERSION_KEY           = "version";
const char * const QD_CONNECTION_PROPERTY_COST_KEY              = "qd.inter-router-cost";
const char * const QD_CONNECTION_PROPERTY_CONN_ID               = "qd.conn-id";
const char * const QD_CONNECTION_PROPERTY_FAILOVER_LIST_KEY     = "failover-server-list";
const char * const QD_CONNECTION_PROPERTY_FAILOVER_NETHOST_KEY  = "network-host";
const char * const QD_CONNECTION_PROPERTY_FAILOVER_PORT_KEY     = "port";
const char * const QD_CONNECTION_PROPERTY_FAILOVER_SCHEME_KEY   = "scheme";
const char * const QD_CONNECTION_PROPERTY_FAILOVER_HOSTNAME_KEY = "hostname";

const char * const QD_TERMINUS_EDGE_ADDRESS_TRACKING = "_$qd.edge_addr_tracking";
const char * const QD_TERMINUS_ADDRESS_LOOKUP        = "_$qd.addr_lookup";

const qd_amqp_error_t QD_AMQP_OK = { 200, "OK" };
const qd_amqp_error_t QD_AMQP_CREATED = { 201, "Created" };
const qd_amqp_error_t QD_AMQP_NO_CONTENT = { 204, "No Content" }; // This is the response code if the delete of a manageable entity was successful.
const qd_amqp_error_t QD_AMQP_BAD_REQUEST = { 400, "Bad Request" };
const qd_amqp_error_t QD_AMQP_FORBIDDEN = { 403, "Forbidden" };
const qd_amqp_error_t QD_AMQP_NOT_FOUND = { 404, "Not Found" };
const qd_amqp_error_t QD_AMQP_NOT_IMPLEMENTED = { 501, "Not Implemented"};

const char * const QD_AMQP_COND_INTERNAL_ERROR = "amqp:internal-error";
const char * const QD_AMQP_COND_NOT_FOUND = "amqp:not-found";
const char * const QD_AMQP_COND_UNAUTHORIZED_ACCESS = "amqp:unauthorized-access";
const char * const QD_AMQP_COND_DECODE_ERROR = "amqp:decode-error";
const char * const QD_AMQP_COND_RESOURCE_LIMIT_EXCEEDED = "amqp:resource-limit-exceeded";
const char * const QD_AMQP_COND_NOT_ALLOWED = "amqp:not-allowed";
const char * const QD_AMQP_COND_INVALID_FIELD = "amqp:invalid-field";
const char * const QD_AMQP_COND_NOT_IMPLEMENTED = "amqp:not-implemented";
const char * const QD_AMQP_COND_RESOURCE_LOCKED = "amqp:resource-locked";
const char * const QD_AMQP_COND_PRECONDITION_FAILED = "amqp:precondition-failed";
const char * const QD_AMQP_COND_RESOURCE_DELETED = "amqp:resource-deleted";
const char * const QD_AMQP_COND_ILLEGAL_STATE = "amqp:illegal-state";
const char * const QD_AMQP_COND_FRAME_SIZE_TOO_SMALL = "amqp:frame-size-too-small";
const char * const QD_AMQP_COND_CONNECTION_FORCED = "amqp:connection:forced";
const char * const QD_AMQP_COND_MESSAGE_SIZE_EXCEEDED = "amqp:link:message-size-exceeded";

const char * const QD_AMQP_PORT_STR = "5672";
const char * const QD_AMQPS_PORT_STR = "5671";

const char * const QD_AMQP_DFLT_PROTO = "tcp";

int qd_port_int(const char* port_str) {
    errno = 0;
    unsigned long n = strtoul(port_str, NULL, 10);
    if (errno || n > 0xFFFF) return -1;

    // Port is not an integer (port = 'amqp' or 'amqps')
    if ( !n && strlen(port_str) > 0 ) {
        // Resolve service port
        struct servent serv_info;
        struct servent *serv_info_res;
        int buf_len = 4096;
        char *buf = calloc(buf_len, sizeof(char));

        // Service port is resolved
        if ( !getservbyname_r(port_str, QD_AMQP_DFLT_PROTO, &serv_info, buf, buf_len, &serv_info_res) ) {
            n = ntohs(serv_info.s_port);
        } else {
            n = -1;
        }
        free(buf);
    }

    return n;
}

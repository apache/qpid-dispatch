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

const char * const QD_MA_INGRESS = "x-opt-qd.ingress";
const char * const QD_MA_TRACE   = "x-opt-qd.trace";
const char * const QD_MA_TO      = "x-opt-qd.to";
const char * const QD_MA_CLASS   = "x-opt-qd.class";

const char * const QD_CAPABILITY_ROUTER          = "qd.router";
const char * const QD_CAPABILITY_ANONYMOUS_RELAY = "ANONYMOUS-RELAY";

const char * const QD_DYNAMIC_NODE_PROPERTY_ADDRESS = "x-opt-qd.address";

const char * const QD_INTERNODE_LINK_NAME_1 = "qd.internode.1";
const char * const QD_INTERNODE_LINK_NAME_2 = "qd.internode.2";

const qd_amqp_error_t QD_AMQP_OK = { 200, "OK" };
const qd_amqp_error_t QD_AMQP_BAD_REQUEST = { 400, "Bad Request" };
const qd_amqp_error_t QD_AMQP_NOT_FOUND = { 404, "Not Found" };
const qd_amqp_error_t QD_AMQP_NOT_IMPLEMENTED = { 501, "Not Implemented"};

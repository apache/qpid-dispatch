#ifndef __dispatch_amqp_h__
#define __dispatch_amqp_h__ 1
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

/**@file
 * AMQP constants.
 *
 *@defgroup amqp amqp
 *
 * AMQP related constant definitions.
 */
/// @{


/**
 * AMQP Constants
 */
typedef enum {
    QD_AMQP_MIN_MAX_FRAME_SIZE = 512,
    QD_AMQP_PORT_INT = 5672,
    QD_AMQPS_PORT_INT = 5671
} qd_amqp_constants_t;

extern const char * const QD_AMQP_PORT_STR;
extern const char * const QD_AMQPS_PORT_STR;

/* Returns -1 if string is not a number or recognized symbolic port name */
int qd_port_int(const char* port_str);

/**
 * AMQP Performative Tags
 */
typedef enum {
    QD_PERFORMATIVE_HEADER = 0x70,
    QD_PERFORMATIVE_DELIVERY_ANNOTATIONS = 0x71,
    QD_PERFORMATIVE_MESSAGE_ANNOTATIONS = 0x72  ,
    QD_PERFORMATIVE_PROPERTIES = 0x73,
    QD_PERFORMATIVE_APPLICATION_PROPERTIES = 0x74,
    QD_PERFORMATIVE_BODY_DATA = 0x75,
    QD_PERFORMATIVE_BODY_AMQP_SEQUENCE = 0x76,
    QD_PERFORMATIVE_BODY_AMQP_VALUE = 0x77,
    QD_PERFORMATIVE_FOOTER = 0x78,
} qd_amqp_performative_t;

/**
 * AMQP Type Tags
 */
typedef enum {
    QD_AMQP_NULL = 0x40,
    QD_AMQP_BOOLEAN = 0x56,
    QD_AMQP_TRUE = 0x41,
    QD_AMQP_FALSE = 0x42,
    QD_AMQP_UBYTE = 0x50,
    QD_AMQP_USHORT = 0x60,
    QD_AMQP_UINT = 0x70,
    QD_AMQP_SMALLUINT = 0x52,
    QD_AMQP_UINT0 = 0x43,
    QD_AMQP_ULONG = 0x80,
    QD_AMQP_SMALLULONG = 0x53,
    QD_AMQP_ULONG0 = 0x44,
    QD_AMQP_BYTE = 0x51,
    QD_AMQP_SHORT = 0x61,
    QD_AMQP_INT = 0x71,
    QD_AMQP_SMALLINT = 0x54,
    QD_AMQP_LONG = 0x81,
    QD_AMQP_SMALLLONG = 0x55,
    QD_AMQP_FLOAT = 0x72,
    QD_AMQP_DOUBLE = 0x82,
    QD_AMQP_DECIMAL32 = 0x74,
    QD_AMQP_DECIMAL64 = 0x84,
    QD_AMQP_DECIMAL128 = 0x94,
    QD_AMQP_UTF32 = 0x73,
    QD_AMQP_TIMESTAMP = 0x83,
    QD_AMQP_UUID = 0x98,
    QD_AMQP_VBIN8 = 0xa0,
    QD_AMQP_VBIN32 = 0xb0,
    QD_AMQP_STR8_UTF8 = 0xa1,
    QD_AMQP_STR32_UTF8 = 0xb1,
    QD_AMQP_SYM8 = 0xa3,
    QD_AMQP_SYM32 = 0xb3,
    QD_AMQP_LIST0 = 0x45,
    QD_AMQP_LIST8 = 0xc0,
    QD_AMQP_LIST32 = 0xd0,
    QD_AMQP_MAP8 = 0xc1,
    QD_AMQP_MAP32 = 0xd1,
    QD_AMQP_ARRAY8 = 0xe0,
    QD_AMQP_ARRAY32 = 0xf0,
} qd_amqp_type_t;

/** @name Message Annotation Headers */
/// @{
extern const char * const QD_MA_PREFIX;
extern const char * const QD_MA_INGRESS;  ///< Ingress Router
extern const char * const QD_MA_TRACE;    ///< Trace
extern const char * const QD_MA_TO;       ///< To-Override
extern const char * const QD_MA_PHASE;    ///< Phase for override address
extern const char * const QD_MA_CLASS;    ///< Message-Class

#define QD_MA_PREFIX_LEN  (9)
#define QD_MA_INGRESS_LEN (16)
#define QD_MA_TRACE_LEN   (14)
#define QD_MA_TO_LEN      (11)
#define QD_MA_PHASE_LEN   (14)
#define QD_MA_CLASS_LEN   (14)

extern const int          QD_MA_MAX_KEY_LEN;  ///< strlen of longest key name
extern const int          QD_MA_N_KEYS;       ///< number of router annotation keys
extern const int          QD_MA_FILTER_LEN;   ///< size of annotation filter buffer
/// @}

/** @name Container Capabilities */
/// @{
extern const char * const QD_CAPABILITY_ANONYMOUS_RELAY;
/// @}

/** @name Link Terminus Capabilities */
/// @{
extern const char * const QD_CAPABILITY_ROUTER_CONTROL;
extern const char * const QD_CAPABILITY_ROUTER_DATA;
extern const char * const QD_CAPABILITY_EDGE_DOWNLINK;
extern const char * const QD_CAPABILITY_WAYPOINT_DEFAULT;
extern const char * const QD_CAPABILITY_WAYPOINT1;
extern const char * const QD_CAPABILITY_WAYPOINT2;
extern const char * const QD_CAPABILITY_WAYPOINT3;
extern const char * const QD_CAPABILITY_WAYPOINT4;
extern const char * const QD_CAPABILITY_WAYPOINT5;
extern const char * const QD_CAPABILITY_WAYPOINT6;
extern const char * const QD_CAPABILITY_WAYPOINT7;
extern const char * const QD_CAPABILITY_WAYPOINT8;
extern const char * const QD_CAPABILITY_WAYPOINT9;
extern const char * const QD_CAPABILITY_FALLBACK;
/// @}

/** @name Dynamic Node Properties */
/// @{
extern const char * const QD_DYNAMIC_NODE_PROPERTY_ADDRESS;  ///< Address for routing dynamic sources
/// @}

/** @name Connection Properties */
/// @{
extern const char * const QD_CONNECTION_PROPERTY_PRODUCT_KEY;
extern const char * const QD_CONNECTION_PROPERTY_PRODUCT_VALUE;
extern const char * const QD_CONNECTION_PROPERTY_VERSION_KEY;
extern const char * const QD_CONNECTION_PROPERTY_COST_KEY;
extern const char * const QD_CONNECTION_PROPERTY_CONN_ID;
extern const char * const QD_CONNECTION_PROPERTY_FAILOVER_LIST_KEY;
extern const char * const QD_CONNECTION_PROPERTY_FAILOVER_NETHOST_KEY;
extern const char * const QD_CONNECTION_PROPERTY_FAILOVER_PORT_KEY;
extern const char * const QD_CONNECTION_PROPERTY_FAILOVER_SCHEME_KEY;
extern const char * const QD_CONNECTION_PROPERTY_FAILOVER_HOSTNAME_KEY;
/// @}

/** @name Terminus Addresses */
/// @{
extern const char * const QD_TERMINUS_EDGE_ADDRESS_TRACKING;
extern const char * const QD_TERMINUS_ADDRESS_LOOKUP;
/// @}

/** @name AMQP error codes. */
/// @{
/** AMQP management error status code and string description (HTTP-style)  */
typedef struct qd_amqp_error_t { int status; const char* description; } qd_amqp_error_t;
extern const qd_amqp_error_t QD_AMQP_OK;
extern const qd_amqp_error_t QD_AMQP_CREATED;
extern const qd_amqp_error_t QD_AMQP_NO_CONTENT;
extern const qd_amqp_error_t QD_AMQP_FORBIDDEN;
extern const qd_amqp_error_t QD_AMQP_BAD_REQUEST;
extern const qd_amqp_error_t QD_AMQP_NOT_FOUND;
extern const qd_amqp_error_t QD_AMQP_NOT_IMPLEMENTED;
/// @}

/** @name Standard AMQP error condition names. */
/// @{
extern const char * const QD_AMQP_COND_INTERNAL_ERROR;
extern const char * const QD_AMQP_COND_NOT_FOUND;
extern const char * const QD_AMQP_COND_UNAUTHORIZED_ACCESS;
extern const char * const QD_AMQP_COND_DECODE_ERROR;
extern const char * const QD_AMQP_COND_RESOURCE_LIMIT_EXCEEDED;
extern const char * const QD_AMQP_COND_NOT_ALLOWED;
extern const char * const QD_AMQP_COND_INVALID_FIELD;
extern const char * const QD_AMQP_COND_NOT_IMPLEMENTED;
extern const char * const QD_AMQP_COND_RESOURCE_LOCKED;
extern const char * const QD_AMQP_COND_PRECONDITION_FAILED;
extern const char * const QD_AMQP_COND_RESOURCE_DELETED;
extern const char * const QD_AMQP_COND_ILLEGAL_STATE;
extern const char * const QD_AMQP_COND_FRAME_SIZE_TOO_SMALL;
extern const char * const QD_AMQP_COND_MESSAGE_SIZE_EXCEEDED;

extern const char * const QD_AMQP_COND_CONNECTION_FORCED;
/// @};

/** @name AMQP link endpoint role. */
/// @{
#define QD_AMQP_LINK_ROLE_SENDER   false
#define QD_AMQP_LINK_ROLE_RECEIVER true
/// @};

#endif

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
enum {
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
const char * const QD_MA_INGRESS;  ///< Ingress Router
const char * const QD_MA_TRACE;    ///< Trace
const char * const QD_MA_TO;       ///< To-Override
const char * const QD_MA_CLASS;    ///< Message-Class
/// @}

/** @name Container Capabilities */
/// @{
const char * const QD_CAPABILITY_ANONYMOUS_RELAY;
/// @}

/** @name Link Terminus Capabilities */
/// @{
const char * const QD_CAPABILITY_ROUTER;
/// @}

/** @name Dynamic Node Properties */
/// @{
const char * const QD_DYNAMIC_NODE_PROPERTY_ADDRESS;  ///< Address for routing dynamic sources
/// @}

/** @name Miscellaneous Strings */
/// @{
const char * const QD_INTERNODE_LINK_NAME_1;
const char * const QD_INTERNODE_LINK_NAME_2;
/// @}

/** @name AMQP error codes. */
/// @{
/** An AMQP error status code and string description  */
typedef struct qd_amqp_error_t { int status; const char* description; } qd_amqp_error_t;
extern const qd_amqp_error_t QD_AMQP_OK;
extern const qd_amqp_error_t QD_AMQP_BAD_REQUEST;
extern const qd_amqp_error_t QD_AMQP_NOT_FOUND;
extern const qd_amqp_error_t QD_AMQP_NOT_IMPLEMENTED;
/// @}

/// @}

#endif

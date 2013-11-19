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

/**
 * AMQP Performative Tags
 */
#define QD_PERFORMATIVE_HEADER                  0x70
#define QD_PERFORMATIVE_DELIVERY_ANNOTATIONS    0x71
#define QD_PERFORMATIVE_MESSAGE_ANNOTATIONS     0x72  
#define QD_PERFORMATIVE_PROPERTIES              0x73
#define QD_PERFORMATIVE_APPLICATION_PROPERTIES  0x74
#define QD_PERFORMATIVE_BODY_DATA               0x75
#define QD_PERFORMATIVE_BODY_AMQP_SEQUENCE      0x76
#define QD_PERFORMATIVE_BODY_AMQP_VALUE         0x77
#define QD_PERFORMATIVE_FOOTER                  0x78


/**
 * AMQP Type Tags
 */
#define QD_AMQP_NULL        0x40
#define QD_AMQP_BOOLEAN     0x56
#define QD_AMQP_TRUE        0x41
#define QD_AMQP_FALSE       0x42
#define QD_AMQP_UBYTE       0x50
#define QD_AMQP_USHORT      0x60
#define QD_AMQP_UINT        0x70
#define QD_AMQP_SMALLUINT   0x52
#define QD_AMQP_UINT0       0x43
#define QD_AMQP_ULONG       0x80
#define QD_AMQP_SMALLULONG  0x53
#define QD_AMQP_ULONG0      0x44
#define QD_AMQP_BYTE        0x51
#define QD_AMQP_SHORT       0x61
#define QD_AMQP_INT         0x71
#define QD_AMQP_SMALLINT    0x54
#define QD_AMQP_LONG        0x81
#define QD_AMQP_SMALLLONG   0x55
#define QD_AMQP_FLOAT       0x72
#define QD_AMQP_DOUBLE      0x82
#define QD_AMQP_DECIMAL32   0x74
#define QD_AMQP_DECIMAL64   0x84
#define QD_AMQP_DECIMAL128  0x94
#define QD_AMQP_UTF32       0x73
#define QD_AMQP_TIMESTAMP   0x83
#define QD_AMQP_UUID        0x98
#define QD_AMQP_VBIN8       0xa0
#define QD_AMQP_VBIN32      0xb0
#define QD_AMQP_STR8_UTF8   0xa1
#define QD_AMQP_STR32_UTF8  0xb1
#define QD_AMQP_SYM8        0xa3
#define QD_AMQP_SYM32       0xb3
#define QD_AMQP_LIST0       0x45
#define QD_AMQP_LIST8       0xc0
#define QD_AMQP_LIST32      0xd0
#define QD_AMQP_MAP8        0xc1
#define QD_AMQP_MAP32       0xd1
#define QD_AMQP_ARRAY8      0xe0
#define QD_AMQP_ARRAY32     0xf0

/**
 * Delivery Annotation Headers
 */
const char * const QD_DA_INGRESS;  // Ingress Router
const char * const QD_DA_TRACE;    // Trace
const char * const QD_DA_TO;       // To-Override

/**
 * Link Terminus Capabilities
 */
const char * const QD_CAPABILITY_ROUTER;

/**
 * Miscellaneous Strings
 */
const char * const QD_INTERNODE_LINK_NAME_1;
const char * const QD_INTERNODE_LINK_NAME_2;

#endif


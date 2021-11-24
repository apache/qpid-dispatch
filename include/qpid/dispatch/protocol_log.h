#ifndef __protocol_log_h__
#define __protocol_log_h__ 1
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

#include "stdint.h"
#include "qpid/dispatch/message.h"
#include "qpid/dispatch/iterator.h"

typedef struct plog_record_t plog_record_t;

/**
 * Record hierarchy for parent-child relationships (CHILD ---> PARENT)
 *
 *   PROCESS ---> SITE
 *   LINK ---> ROUTER ---> SITE
 *   [FLOW --->]* FLOW ---> LISTENER ---> ROUTER ---> SITE
 *   [FLOW --->]* FLOW ---> CONNECTOR ---> ROUTER ---> SITE
 */
typedef enum plog_record_type {
    PLOG_RECORD_SITE       = 0x00,  // A cloud site involved in a VAN
    PLOG_RECORD_ROUTER     = 0x01,  // A VAN router deployed in a site
    PLOG_RECORD_LINK       = 0x02,  // An inter-router link to a different router
    PLOG_RECORD_CONTROLLER = 0x03,  // A VAN controller
    PLOG_RECORD_LISTENER   = 0x04,  // An ingress listener for an application protocol
    PLOG_RECORD_CONNECTOR  = 0x05,  // An egress connector for an application protocol
    PLOG_RECORD_FLOW       = 0x06,  // An application flow between a ingress and an egress
    PLOG_RECORD_PROCESS    = 0x07,  // A running process/pod/container that uses application protocols
} plog_record_type_t;

typedef enum plog_attribute {
    PLOG_ATTRIBUTE_IDENTITY         = 0x00,  // Reference (cannot be set by the user)
    PLOG_ATTRIBUTE_PARENT           = 0x01,  // Reference (set during object creation only)
    PLOG_ATTRIBUTE_SIBLING          = 0x02,  // Reference
    PLOG_ATTRIBUTE_PROCESS          = 0x03,  // Reference

    PLOG_ATTRIBUTE_SIBLING_ORDINAL  = 0x04,  // uint
    PLOG_ATTRIBUTE_LOCATION         = 0x05,  // String
    PLOG_ATTRIBUTE_PROVIDER         = 0x06,  // String
    PLOG_ATTRIBUTE_PLATFORM         = 0x07,  // String

    PLOG_ATTRIBUTE_NAMESPACE        = 0x08,  // String
    PLOG_ATTRIBUTE_MODE             = 0x09,  // String
    PLOG_ATTRIBUTE_SOURCE_HOST      = 0x0a,  // String
    PLOG_ATTRIBUTE_DESTINATION_HOST = 0x0b,  // String

    PLOG_ATTRIBUTE_PROTOCOL         = 0x0c,  // String
    PLOG_ATTRIBUTE_SOURCE_PORT      = 0x0d,  // String
    PLOG_ATTRIBUTE_DESTINATION_PORT = 0x0e,  // String
    PLOG_ATTRIBUTE_VAN_ADDRESS      = 0x0f,  // String

    PLOG_ATTRIBUTE_IMAGE_NAME       = 0x10,  // String
    PLOG_ATTRIBUTE_IMAGE_VERSION    = 0x11,  // String
    PLOG_ATTRIBUTE_HOST_NAME        = 0x12,  // String
    PLOG_ATTRIBUTE_FLOW_TYPE        = 0x13,

    PLOG_ATTRIBUTE_OCTETS           = 0x14,  // uint
    PLOG_ATTRIBUTE_START_LATENCY    = 0x15,  // uint
    PLOG_ATTRIBUTE_BACKLOG          = 0x16,  // uint
    PLOG_ATTRIBUTE_METHOD           = 0x17,  // String  

    PLOG_ATTRIBUTE_RESULT           = 0x18,  // String
    PLOG_ATTRIBUTE_REASON           = 0x19,  // String
    PLOG_ATTRIBUTE_NAME             = 0x1a,  // String
    PLOG_ATTRIBUTE_TRACE            = 0x1b,  // List of Strings (use plog_set_trace function)

    PLOG_ATTRIBUTE_BUILD_VERSION    = 0x1c,  // String
} plog_attribute_t;

#define VALID_REF_ATTRS    0x0000000c
#define VALID_UINT_ATTRS   0x00700010
#define VALID_STRING_ATTRS 0x1787ffe0
#define VALID_TRACE_ATTRS  0x08000000


/**
 * plog_start_record
 * 
 * Open a new protocol-log record, specifying the parent record that the new record is
 * a child of.
 * 
 * @param record_type The type for the newly opened record
 * @param parent Pointer to the parent record.  If NULL, it will reference the local SOURCE record.
 * @return Pointer to the new record
 */
plog_record_t *plog_start_record(plog_record_type_t record_type, plog_record_t *parent);

/**
 * plog_end_record
 * 
 * Close a record when it is no longer needed.  After a record is closed, it cannot be referenced
 * or accessed in any way thereafter.
 * 
 * @param record The record pointer returned by plog_start_record
 */
void plog_end_record(plog_record_t *record);

/**
 * plog_serialize_identity
 * 
 * Encode the identity of the indicated record into the supplied composed-field.
 * 
 * @param record Pointer to the record from which to obtain the identity
 * @param field Pointer to the composed-field into which to serialize the identity
 */
void plog_serialize_identity(const plog_record_t *record, qd_composed_field_t *field);


/**
 * plog_set_ref_from_record
 * 
 * Set a reference-typed attribute in a record from the ID of another record.
 * 
 * @param record The record pointer returned by plog_start_record
 * @param attribute_type The type of the attribute (see enumerated above) to be set
 * @param record Pointer to the referenced record.
 */
void plog_set_ref_from_record(plog_record_t *record, plog_attribute_t attribute_type, plog_record_t *referenced_record);


/**
 * plog_set_ref_from_parsed
 * 
 * Set a reference-typed attribute in a record from a parsed field (a serialized identity).
 * 
 * @param record The record pointer returned by plog_start_record
 * @param attribute_type The type of the attribute (see enumerated above) to be set
 * @param field Pointer to a parsed field containing the serialized form of a record identity
 */
void plog_set_ref_from_parsed(plog_record_t *record, plog_attribute_t attribute_type, qd_parsed_field_t *field);

/**
 * plog_set_string
 * 
 * Set a string-typed attribute in a record.
 * 
 * @param record The record pointer returned by plog_start_record
 * @param attribute_type The type of the attribute (see enumerated above) to be set
 * @param value The string value to be set
 */
void plog_set_string(plog_record_t *record, plog_attribute_t attribute_type, const char *value);

/**
 * plog_set_uint64
 * 
 * Set a uint64-typed attribute in a record.
 * 
 * @param record The record pointer returned by plog_start_record
 * @param attribute_type The type of the attribute (see enumerated above) to be set
 * @param value The unsigned integer value to be set
 */
void plog_set_uint64(plog_record_t *record, plog_attribute_t attribute_type, uint64_t value);


/**
 * plog_set_trace
 * 
 * Set the PLOG_ATTRIBUTE_TRACE attribute of the record using the trace annotation from the
 * referenced message.
 * 
 * @param record The record pointer returned by plog_start_record
 * @param msg Pointer to a message from which to extract the path trace
 */
void plog_set_trace(plog_record_t *record, qd_message_t *msg);

#endif

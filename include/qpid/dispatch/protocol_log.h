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

#include "qpid/dispatch/enum.h"
#include "stdint.h"

typedef struct plog_record_t plog_record_t;

/**
 * Record hierarchy for parent-child relationships (CHILD ---> PARENT)
 *
 *   PROCESS ---> SOURCE
 *   [FLOW --->]* FLOW ---> LISTENER ---> SOURCE
 *   [FLOW --->]* FLOW ---> CONNECTOR ---> SOURCE
 */
typedef enum plog_record_type {
    PLOG_RECORD_SOURCE    = 0x01,
    PLOG_RECORD_LISTENER  = 0x02,
    PLOG_RECORD_CONNECTOR = 0x03,
    PLOG_RECORD_FLOW      = 0x04,
    PLOG_RECORD_PROCESS   = 0x05,
} plog_record_type_t;
ENUM_DECLARE(plog_record_type);

typedef enum plog_attribute {
    PLOG_ATTRIBUTE_IDENTITY         = 0x01,  // uint
    PLOG_ATTRIBUTE_PARENT           = 0x02,  // Reference
    PLOG_ATTRIBUTE_SIBLING          = 0x03,  // Reference
    PLOG_ATTRIBUTE_PROCESS          = 0x04,  // Reference
    PLOG_ATTRIBUTE_SIBLING_ORDINAL  = 0x05,  // uint
    PLOG_ATTRIBUTE_LOCATION         = 0x06,  // String
    PLOG_ATTRIBUTE_PROVIDER         = 0x07,  // String
    PLOG_ATTRIBUTE_PLATFORM         = 0x08,  // String
    PLOG_ATTRIBUTE_NAMESPACE        = 0x09,  // String
    PLOG_ATTRIBUTE_MODE             = 0x0a,  // String
    PLOG_ATTRIBUTE_SOURCE_IP        = 0x0b,  // String
    PLOG_ATTRIBUTE_DESTINATION_IP   = 0x0c,  // String
    PLOG_ATTRIBUTE_PROTOCOL         = 0x0d,  // String
    PLOG_ATTRIBUTE_SOURCE_PORT      = 0x0e,  // String
    PLOG_ATTRIBUTE_DESTINATION_PORT = 0x0f,  // String
    PLOG_ATTRIBUTE_VAN_ADDRESS      = 0x10,  // String
    PLOG_ATTRIBUTE_IMAGE_NAME       = 0x11,  // String
    PLOG_ATTRIBUTE_IMAGE_VERSION    = 0x12,  // String
    PLOG_ATTRIBUTE_FLOW_TYPE        = 0x13,
    PLOG_ATTRIBUTE_OCTETS           = 0x14,  // uint
    PLOG_ATTRIBUTE_START_LATENCY    = 0x15,  // uint
    PLOG_ATTRIBUTE_BACKLOG          = 0x16,  // uint
    PLOG_ATTRIBUTE_METHOD           = 0x17,  // String  
    PLOG_ATTRIBUTE_RESULT           = 0x18,  // String
    PLOG_ATTRIBUTE_REASON           = 0x19,  // String
} plog_attribute_t;
ENUM_DECLARE(plog_attribute);

/**
 * plog_start_record
 * 
 * Open a new protocol-log record, specifying the parent record that the new record is
 * a child of.
 * 
 * @param record_type The type for the newly opened record
 * @param parent Pointer to the parent record
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
 * plog_flush_record
 * 
 * Optionally push out an active record.  This need not ever be invoked but may be useful in
 * cases where a record's attributes have been set and they will not be updated again soon.
 * 
 * @param record The record pointer returned by plog_start_record
 */
void plog_flush_record(plog_record_t *record);

/**
 * plog_set_ref
 * 
 * Set a reference-typed attribute in a record.
 * 
 * @param record The record pointer returned by plog_start_record
 * @param attribute_type The type of the attribute (see enumerated above) to be set
 * @param record Pointer to the referenced record.
 */
void plog_set_ref(plog_record_t *record, plog_attribute_t attribute_type, plog_record_t *referenced_record);

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

#endif

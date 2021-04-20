#ifndef __adaptor_utils_h__
#define __adaptor_utils_h__
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

#include "qpid/dispatch/buffer.h"

#include <proton/raw_connection.h>

// Get the raw connections remote address
// Caller must free() the result when done.
//
char *qda_raw_conn_get_address(pn_raw_connection_t *socket);

// Retrieve all available incoming data buffers from the raw connection.
// Return the result in blist, with the total number of read octets in *length
// Note: only those buffers containing data (size != 0) are returned.
//
void qda_raw_conn_get_read_buffers(pn_raw_connection_t *conn, qd_buffer_list_t *blist, uintmax_t *length);

// allocate empty read buffers to the raw connection.  This will provide enough buffers
// to fill the connections capacity.  Returns the number of buffers granted.
//
int qda_raw_conn_grant_read_buffers(pn_raw_connection_t *conn);


// Write blist buffers to the connection.  Buffers are removed from the HEAD of
// blist.  Returns the actual number of buffers taken.  offset is an optional
// offset to the first outgoing octet in the HEAD buffer.
//
int qda_raw_conn_write_buffers(pn_raw_connection_t *conn, qd_buffer_list_t *blist, size_t offset);


// release all sent buffers held by the connection
//
void qda_raw_conn_free_write_buffers(pn_raw_connection_t *conn);


#endif // __adaptor_utils_h__

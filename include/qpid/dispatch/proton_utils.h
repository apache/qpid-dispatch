#ifndef __proton_utils_h__
#define __proton_utils_h__ 1
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

#include <proton/codec.h>

/**
 * Get the contents of a data field as a string.
 *
 * @param data A proton data field
 * @return A heap allocated string representing the contents of the data.  The caller is
 *         responsible for freeing the string when it is no longer needed.
 */
char *qdpn_data_as_string(pn_data_t *data);


/**
 * Copy the data node at src to dest.  Both src and dest are advanced after this call.
 *
 * @param src A proton data field
 * @param dest A proton data field to which src will be added
 * @return 0 on success
 */
int qdpn_data_insert(pn_data_t *dest, pn_data_t *src);

#endif


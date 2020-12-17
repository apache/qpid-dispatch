#ifndef __dispatch_tempstore_h__
#define __dispatch_tempstore_h__ 1
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

/*
 * qd_temp_is_store_created
 *
 * Indicate whether or not the store has been created.
 *
 * @return true iff the store has been set up and has at least one file in it.
 */
bool qd_temp_is_store_created(void);

/*
 * qd_temp_get_path
 *
 * Given a file name, return the full path to the file in the store.
 * The returned string is allocated from the heap and must be freed by the caller.
 *
 * @param filename Name of the file in the store.  This must not contain '/' or '\' characters.
 * @return allocated string containing full path iff The file exists, else NULL
 */
char *qd_temp_get_path(const char* filename);

#endif


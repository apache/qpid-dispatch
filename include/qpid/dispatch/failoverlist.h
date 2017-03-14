#ifndef __failoverlist_h__
#define __failoverlist_h__ 1
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
 * qd_failover_list_t - This type stores one failover list.
 */
typedef struct qd_failover_list_t qd_failover_list_t;

/**
 * qd_failover_list
 *
 * Parse a configuration string for a failover list.  If well-formed, return
 * the pointer to a new failover list object.  If there is a parsing failure,
 * return NULL and set the error string for error reporting.
 *
 * The format of the failover string is a comma-separated list of failover
 * destinations.  Each destination has the following form:
 *
 *    [scheme://]hostname[:port]
 *
 * If scheme is not supplied, it defaults to _not present_.
 * If port is not specified, it defaults to "5672".
 */
qd_failover_list_t *qd_failover_list(const char *text, const char **error);

/**
 * qd_failover_list_free
 *
 * Free the resources use in storing the failover list.  The list cannot be
 * used again after invoking this function.
 */
void qd_failover_list_free(qd_failover_list_t *list);

/**
 * qd_failover_list_size
 *
 * Return the number of destinations in the failover list.
 */
int qd_failover_list_size(const qd_failover_list_t *list);

/**
 * qd_failover_list_scheme
 *
 * Return the scheme for the failover destination indicated by index (0..size-1).
 * If the scheme is not present, return NULL.
 */
const char *qd_failover_list_scheme(const qd_failover_list_t *list, int index);

/**
 * qd_failover_list_host
 *
 * Return the host for the failover destination indicated by index (0..size-1).
 */
const char *qd_failover_list_host(const qd_failover_list_t *list, int index);

/**
 * qd_failover_list_port
 *
 * Return the port for the failover destination indicated by index (0..size-1).
 */
const char *qd_failover_list_port(const qd_failover_list_t *list, int index);

/**
 * qd_failover_list_hostname
 *
 * Return the hostname field for the failover destination indicated by index (0..size-1).
 * NOTE: This is always NULL/not-present.
 */
const char *qd_failover_list_hostname(const qd_failover_list_t *list, int index);

#endif

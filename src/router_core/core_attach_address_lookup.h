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

#ifndef qd_router_core_attach_addr_lookup_types
#define qd_router_core_attach_addr_lookup_types 1

#include "router_core_private.h"
#endif

#ifndef qd_router_core_attach_addr_lookup
#define qd_router_core_attach_addr_lookup 1


/**
 * Handler - Look up the address on a received first-attach
 *
 * This handler is invoked upon receipt of a first-attach on a normal endpoint link.
 * The appropriate address, from source or target will be resolved to and address for
 * message or link routing.  This operation may be synchronoue (completed before it
 * returns) or asynchronous (completed later).
 *
 * @param context Module context for address-lookup module
 * @param conn Pointer to the connection over which the attach arrived.
 * @param link Pointer to the attaching link.
 * @param dir The direction of message flow for the link.
 * @param source The source terminus for the attach.
 * @param target The target terminus for the attach.
 */
typedef void (*qdrc_attach_addr_lookup_t) (void             *context,
                                           qdr_connection_t *conn,
                                           qdr_link_t       *link,
                                           qd_direction_t    dir,
                                           qdr_terminus_t   *source,
                                           qdr_terminus_t   *target);



#endif

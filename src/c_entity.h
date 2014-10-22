#ifndef C_ENTITY_H
#define C_ENTITY_H 1
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

/** @file
 *
 * Python-C interface for managed entity implementation objects.
 *
 * Maintain a cache of entity add and remove events that can be read by the
 * python agent.
 */

/** Initialize the module. */
void qd_c_entity_initialize(void);

/** Record an entity add event. */
void qd_c_entity_add(const char *type, void *object);

/** Record an entity remove event. */
void qd_c_entity_remove(const char *type, void *object);

/** Short entity type names (no org.apache.qpid.dispatch prefix). */
extern const char *QD_ALLOCATOR_TYPE;
extern const char *QD_CONNECTION_TYPE;
extern const char *QD_ROUTER_TYPE;
extern const char *QD_ROUTER_NODE_TYPE;
extern const char *QD_ROUTER_ADDRESS_TYPE;
extern const char *QD_ROUTER_LINK_TYPE;

/** Long entity type names (with org.apache.qpid.dispatch prefix). */
extern const char *QD_ALLOCATOR_TYPE_LONG;
extern const char *QD_CONNECTION_TYPE_LONG;
extern const char *QD_ROUTER_TYPE_LONG;
extern const char *QD_ROUTER_NODE_TYPE_LONG;
extern const char *QD_ROUTER_ADDRESS_TYPE_LONG;
extern const char *QD_ROUTER_LINK_TYPE_LONG;

#endif

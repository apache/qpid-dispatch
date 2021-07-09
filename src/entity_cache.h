#ifndef ENTITY_CACHE_H
#define ENTITY_CACHE_H 1
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
 * Add/remove runtime operational entities from the cache managed by the Python agent.
 *
 * New entities should be registered with qd_entity_cache_add, entities
 * that are due for deletion should be removed with qd_entity_cache_remove
 * *before* they are deleted.
 *
 * The cache is pure C, entites can be added to it before the python agent has
 * started.
 */

/** Initialize the module. */
void qd_entity_cache_initialize();

/** Free all entries in the cache. Avoids leaks in c_unittests. */
void qd_entity_cache_free_entries();

/** Add an entity to the agent cache. */
void qd_entity_cache_add(const char *type, void *object);

/** Remove an entity from the agent cache. Must be called before object is deleted. */
void qd_entity_cache_remove(const char *type, void *object);

#endif

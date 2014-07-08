#ifndef ENTITY_PRIVATE_H
#define ENTITY_PRIVATE_H 1
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

#include <stdbool.h>
#include <qpid/dispatch/dispatch.h>

void qd_entity_free(qd_entity_t* entity);

/** True if the entity has this attribute. */
bool qd_entity_has(qd_entity_t* entity, const char *attribute);

/** Get a string valued attribute.
 * @return New string value, caller must free.
 * Return NULL and set qd_error if there is an error.
 **/
char *qd_entity_string(qd_entity_t *entity, const char* attribute);

/** Get an integer valued attribute. Return -1 and set qd_error if there is an error. */
long qd_entity_long(qd_entity_t *entity, const char* attribute);

/** Get a boolean valued attribute. Return false and set qd_error if there is an error. */
bool qd_entity_bool(qd_entity_t *entity, const char *attribute);


/** Get a string valued attribute.
 * @return New string value, caller must free.
 *  Return copy of default_value if attribute is missing or there is an error.
 *  Return NULL and set qd_error if there is an error (missing attribute is not an error.)
 **/
char *qd_entity_opt_string(qd_entity_t *entity, const char *attribute, const char *default_value);

/** Get an integer valued attribute.
 * @return Attribute value or default_value if attribute is missing.
 * Set qd_error and return default_value if there is an error (missing attribute is not an error.)
 */
long qd_entity_opt_long(qd_entity_t *entity, const char *attribute, long default_value);

/** Get a boolean valued attribute. Return false and set qd_error if there is an error.
 * @return Attribute value or default_value if attribute is missing.
 * Set qd_error and return default_value if there is an error (missing attribute is not an error.)
 */
bool qd_entity_opt_bool(qd_entity_t *entity, const char *attribute, bool default_value);


#endif

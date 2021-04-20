#ifndef ENTITY_H
#define ENTITY_H 1
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

#include "qpid/dispatch/error.h"

#include <stdbool.h>

/**
 * @defgroup entity-attributes Set/get cached attributes of a managed entity.
 *
 * @{
 */

/** A managed entitie's attributes */
typedef struct qd_entity_t qd_entity_t;

/** True if the entity has this attribute. */
bool qd_entity_has(qd_entity_t* entity, const char *attribute);

/** Get a string valued attribute.
 * @return New string value, caller must free.
 * Return NULL and set qd_error if there is an error.
 **/
char *qd_entity_get_string(qd_entity_t *entity, const char* attribute);

/** Get an integer valued attribute. Return -1 and set qd_error if there is an error. */
long qd_entity_get_long(qd_entity_t *entity, const char* attribute);

/** Get a boolean valued attribute. Return false and set qd_error if there is an error. */
bool qd_entity_get_bool(qd_entity_t *entity, const char *attribute);


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

/** Get a map in pn_data_t format.  Return NULL if map not present or error parsing out map
 * Caller must free pn_data_t when no longer in use.
 */
struct pn_data_t *qd_entity_opt_map(qd_entity_t *entity, const char *attribute);

/** Set a string valued attribute, entity makes a copy.
 * If value is NULL clear the attribute.
 */
qd_error_t qd_entity_set_string(qd_entity_t *entity, const char* attribute, const char *value);

/** Set an integer valued attribute. */
qd_error_t qd_entity_set_long(qd_entity_t *entity, const char* attribute, long value);

/** Set a boolean valued attribute. */
qd_error_t qd_entity_set_bool(qd_entity_t *entity, const char *attribute, bool value);

/** Set an integer valued attribute. If value is NULL clear the attribute. */
qd_error_t qd_entity_set_longp(qd_entity_t *entity, const char* attribute, const long *value);

/** Set a boolean valued attribute. If value is NULL clear the attribute. */
qd_error_t qd_entity_set_boolp(qd_entity_t *entity, const char *attribute, const bool *value);

/** Clear the attribute  */
qd_error_t qd_entity_clear(qd_entity_t *entity, const char *attribute);

/**
 * Set the attribute to an empty list. Futher qd_entity_set* calls for the attribute
 * will append values to the list.
 */
qd_error_t qd_entity_set_list(qd_entity_t *entity, const char *attribute);


/**
 * Set the attribute to an empty dictionary. To further add key/value pairs to this new dict, use qd_entity_set_map_key_value
 */
qd_error_t qd_entity_set_map(qd_entity_t *entity, const char *attribute);

/**
 * Add a new key/value pair to the attribute which needs to be dict.
 * @return - QD_ERROR_NONE if there were no errors
 *         - QD_ERROR_PYTHON if there was an error from the Python side when setting the key/value error
 *         - QD_ERROR_VALUE if a non-dictionary attribute was specified
 */
qd_error_t qd_entity_set_map_key_value_string(qd_entity_t *entity, const char *attribute, const char *key, const char *value);

qd_error_t qd_entity_set_map_key_value_int(qd_entity_t *entity, const char *attribute, const char *key, int value);


/// @}

#endif

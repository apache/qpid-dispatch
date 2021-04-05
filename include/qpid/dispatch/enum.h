#ifndef ENUM_H
#define ENUM_H
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

#include "qpid/dispatch/static_assert.h"

/**@file
 *
 * Tools for working with enums and enum string names.
 *
 * Enums should follow this pattern:
 *
 * // .h file
 * typedef enum {
 *   FOO_VALUE1,
 *   FOO_VALUE2,
 *  } foo_t;
 * ENUM_DECLARE(foo); // Declares const char *foo_name(foo_t value)
 *
 * // .c file
 * static const char *const *foo_names = { "foo-value1", "foo-value2", ... };
 * ENUM_DEFINE(foo, FOO_ENUM_COUNT, foo_names); // Defines const char *foo_name(foo_t value)
 */

/** Declares:
 * const char *NAME_get_name(NAME_t value);  // get name for value or NULL if value is invalid.
 */
#define ENUM_DECLARE(NAME) const char *NAME##_name(NAME##_t value)

/** Defines:
 * const char *NAME_name(NAME_t value)
 */
#define ENUM_DEFINE(NAME, NAME_ARRAY)                            \
    const char *NAME##_name(NAME##_t value) {                           \
        static const size_t count = sizeof(NAME_ARRAY)/sizeof(NAME_ARRAY[0]); \
        return (0 <= value && value < count) ? NAME_ARRAY[value] : 0;   \
    }                                                                   \
    extern int NAME##__enum_dummy // So macro use can end with a ';'

#endif

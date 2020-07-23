#ifndef __terminus_private_h__
#define __terminus_private_h__ 1
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

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>

// DISPATCH-1461: snprintf() is evil - it returns >= size on overflow.  This
// wrapper will never return >= size, even if truncated.  This makes it safe to
// do pointer & length arithmetic without overflowing the destination buffer in
// qdr_terminus_format()
//
static inline size_t safe_snprintf(char *str, size_t size, const char *format, ...) {
    // max size allowed must be INT_MAX (since vsnprintf returns an int)
    if (size == 0 || size > INT_MAX) {
        return 0;
    }
    int max_possible_return_value = (int)(size - 1);
    va_list ap;
    va_start(ap, format);
    int rc = vsnprintf(str, size, format, ap);
    va_end(ap);

    if (rc < 0) {
        if (size > 0 && str) {
            *str = 0;
        }
        return 0;
    }

    if (rc > max_possible_return_value) {
        rc = max_possible_return_value;
    }
    return (size_t)rc;
}

#endif  // __terminus_private_h__

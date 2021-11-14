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

#include "aprintf.h"

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>

int vaprintf(char **begin, char *end, const char *format, va_list ap_in) {
    int size = end - *begin;
    if (size == 0) return EINVAL;
    va_list ap;
    va_copy(ap, ap_in);
    int n = vsnprintf(*begin, size, format, ap);
    va_end(ap);
    if (n < 0) return n;
    if (n >= size) {
        *begin = end-1;
        assert(**begin == '\0');
        return n;
    }
    *begin += n;
    assert(*begin < end);
    assert(**begin == '\0');
    return 0;
}

int aprintf(char **begin, char *end, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int n = vaprintf(begin, end, format, ap);
    va_end(ap);
    return n;
}

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

#include <strings.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <inttypes.h>
#include <limits.h>

bool mocked = false;
int mocked_rc = 0;

void mock(bool m) {
    mocked = m;
}
void mocked_value(int v)
{
    mocked_rc = v;
}

int mocked_vsnprintf(char *str, size_t size, const char *format, ...)
{
    if (mocked) {
        return mocked_rc;
    }
    va_list ap;
    va_start(ap, format);
    int rc = vsnprintf(str, size, format, ap);
    va_end(ap);
    return rc;
}

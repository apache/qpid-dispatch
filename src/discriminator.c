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

#include "qpid/dispatch/discriminator.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

void qd_generate_discriminator(char *string)
{
    static const char *table = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789+_";
    long int rnd1 = random();
    long int rnd2 = random();
    long int rnd3 = random();
    int      idx;
    int      cursor = 0;

    for (idx = 0; idx < 5; idx++) {
        string[cursor++] = table[(rnd1 >> (idx * 6)) & 63];
        string[cursor++] = table[(rnd2 >> (idx * 6)) & 63];
        string[cursor++] = table[(rnd3 >> (idx * 6)) & 63];
    }
    string[cursor] = '\0';
    assert(strlen(string) < QD_DISCRIMINATOR_SIZE);
}


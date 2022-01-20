#ifndef __dispatch_discriminator_h__
#define __dispatch_discriminator_h__ 1
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



// QD_DISCRIMINATOR_SIZE includes null terminator byte. The
// strlen() of a discriminator will be 15

#define QD_DISCRIMINATOR_SIZE 16

/**
 * Generate a random discriminator in the supplied string, not to exceed
 * QD_DISCRIMINATOR_SIZE characters.  Such a discriminator may be used in
 * identifiers, addresses, and other situations where a unique string is needed.
 */
void qd_generate_discriminator(char *string);


#endif


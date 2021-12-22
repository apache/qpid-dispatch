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

#ifndef QPID_DISPATCH_QDR_STUBBING_PROBE
#define QPID_DISPATCH_QDR_STUBBING_PROBE

/**
 *
 * This is an utility to perform ad-hoc check whether stubbing is succeeding.
 *
 * Note that `probe` is in a different compilation unit than `check_stubbing_works`.
 * This is important, because functions called from the same compilation unit can be stubbed only
 * with a Debug build (CMake profile), whereas functions in a different compilation unit
 * can be stubbed even in RelWithDebInfo build. Enabling LTO seems to make any stubbing impossible.
 */

/// dummy function returning abs(int), convenient target for stubbing
int probe(int);

/// attempts to stub `probe` and returns true if successful
bool check_stubbing_works();

#endif  // QPID_DISPATCH_QDR_STUBBING_PROBE

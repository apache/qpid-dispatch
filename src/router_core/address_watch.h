#ifndef qd_address_watch
#define qd_address_watch 1
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

#include <qpid/dispatch/alloc.h>
#include <qpid/dispatch/router_core.h>

typedef struct qdr_address_watch_t qdr_address_watch_t;
DEQ_DECLARE(qdr_address_watch_t, qdr_address_watch_list_t);

/**
 * qdr_trigger_address_watch_CT
 * 
 * This function is invoked after changes have been made to the address that affect
 * reachability (i.e. local and remote senders and receivers).
 * 
 * @param core Pointer to the router core state
 * @param addr Pointer to the address record that was modified
 */
void qdr_trigger_address_watch_CT(qdr_core_t *core, qdr_address_t *addr);

void qdr_address_watch_shutdown(qdr_core_t *core);

#endif

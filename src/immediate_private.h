#ifndef __immediate_private_h__
#define __immediate_private_h__ 1
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


#include <qpid/dispatch/dispatch.h>
#include <qpid/dispatch/server.h>
#include <stdint.h>

/*
 * Immediate actions - used by timer to optimize schedule(0)
 * disarm() and set_armed() are fast.
 * arm() and schedule_visit() are not.
*/


void qd_immediate_initialize(void);
void qd_immediate_finalize(void);

/* Call each armed action in the calling thread. */
void qd_immediate_visit(void);

typedef struct qd_immediate_t qd_immediate_t;

qd_immediate_t *qd_immediate(qd_dispatch_t *qd, void (*handler)(void*), void* context);

/* Arm causes a call to handler(context) ASAP in a server thread. */
void qd_immediate_arm(qd_immediate_t *);

/* After disarm() returns, there will be no handler() call unless re-armed. */
void qd_immediate_disarm(qd_immediate_t *);

/* Mark action as armed, handler will be called at earlist qd_immediate_visit() */
void qd_immediate_set_armed(qd_immediate_t *);

/* At least one call to qd_immediate_visit will be made. */
void qd_immediate_schedule_visit(qd_server_t*);

void qd_immediate_free(qd_immediate_t *);

#endif

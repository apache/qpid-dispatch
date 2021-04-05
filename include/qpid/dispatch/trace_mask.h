#ifndef __trace_mask_h__
#define __trace_mask_h__ 1
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

#include "qpid/dispatch/bitmask.h"
#include "qpid/dispatch/dispatch.h"
#include "qpid/dispatch/parse.h"

typedef struct qd_tracemask_t qd_tracemask_t;

/**
 * qd_tracemask
 *
 * Create a TraceMask object.
 */
qd_tracemask_t *qd_tracemask(void);

/**
 * qd_tracemask_free
 *
 * Destroy a TraceMask object and free its allocated resources.
 */
void qd_tracemask_free(qd_tracemask_t *tm);

/**
 * qd_tracemask_add_router
 *
 * Notify the TraceMask of a new router and its assigned mask bit.
 *
 * @param tm Tracemask created by qd_tracemask()
 * @param address The address of the remote router as reported by the router module.
 * @param maskbit The mask bit assigned to this router by the router module.
 */
void qd_tracemask_add_router(qd_tracemask_t *tm, const char *address, int maskbit);

/**
 * qd_tracemask_del_router
 *
 * Notify the TraceMask of the removal of a router.
 *
 * @param tm Tracemask created by qd_tracemask()
 * @param maskbit The mask bit assigned to this router by the router module.
 */
void qd_tracemask_del_router(qd_tracemask_t *tm, int maskbit);

/**
 * qd_tracemask_set_link
 *
 * Notify the TraceMask of a link connected to a neighbor router.
 *
 * @param tm Tracemask created by qd_tracemask()
 * @param router_maskbit The mask bit assigned to this router by the router module.
 * @param link_maskbit The mask bit assigned to the link by the router core.
 */
void qd_tracemask_set_link(qd_tracemask_t *tm, int router_maskbit, int link_maskbit);

/**
 * qd_tracemask_remove_link
 *
 * Notify the TraceMask of a disconnected neighbor router.
 *
 * @param tm Tracemask created by qd_tracemask()
 * @param router_maskbit The mask bit assigned to this router by the router module.
 */
void qd_tracemask_remove_link(qd_tracemask_t *tm, int router_maskbit);

/**
 * qd_tracemask_create
 *
 * Create a new bitmask with a bit set for every outgoing link to a neighbor mentioned
 * in the trace list.
 *
 * @param tm Tracemask created by qd_tracemask()
 * @param tracelist The parsed field from a message's trace header
 * @param ingress_index (out) The mask-bit for the first router in the trace list (the ingress router)
 * @return A new bit mask with a set-bit for each neighbor router in the list.  This must be freed
 *         by the caller when the caller is done with it.
 */
qd_bitmask_t *qd_tracemask_create(qd_tracemask_t *tm, qd_parsed_field_t *tracelist, int *ingress_index);

#endif

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
#include "qpid/dispatch/delivery_state.h"

#include "qpid/dispatch/alloc_pool.h"
#include "qpid/dispatch/router_core.h"

ALLOC_DECLARE(qd_delivery_state_t);
ALLOC_DEFINE(qd_delivery_state_t);

qd_delivery_state_t *qd_delivery_state()
{
    qd_delivery_state_t *dstate = new_qd_delivery_state_t();
    ZERO(dstate);
    return dstate;
}


qd_delivery_state_t *qd_delivery_state_from_error(qdr_error_t *err)
{
    if (err) {
        qd_delivery_state_t *dstate = qd_delivery_state();
        dstate->error = err;
        return dstate;
    }
    return 0;
}


void qd_delivery_state_free(qd_delivery_state_t *dstate)
{
    if (dstate) {
        qdr_error_free(dstate->error);
        if (dstate->annotations)
            pn_data_free(dstate->annotations);
        if (dstate->extension)
            pn_data_free(dstate->extension);
        free_qd_delivery_state_t(dstate);
    }
}


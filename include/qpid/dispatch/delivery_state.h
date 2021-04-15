#ifndef __delivery_state_h__
#define __delivery_state_h__ 1
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

#include <proton/disposition.h>

#include <stdbool.h>
#include <inttypes.h>

/**
 * AMQP 1.0 defines a delivery-state type property.  Delivery-state is passed
 * via the Disposition and transfer performatives and holds state data
 * associated with the delivery.
 *
 * Different delivery-state data are provided with the Modified and Rejected
 * terminal outcomes.
 */

typedef struct {

    //RECEIVED - see section 3.4.1
    uint32_t  section_number;
    uint64_t  section_offset;

    // REJECTED - see section 3.4.3
    struct qdr_error_t *error;

    // MODIFIED - see section 3.4.5
    struct pn_data_t *annotations;
    bool              delivery_failed;
    bool              undeliverable_here;

    // raw state data available for custom outcomes
    // Example: DECLARED and Transactional state
    struct pn_data_t *extension;

} qd_delivery_state_t;

// allocate
qd_delivery_state_t *qd_delivery_state();

// this constructor takes ownership of err. err will be freed by
// qd_delivery_state_free()
qd_delivery_state_t *qd_delivery_state_from_error(struct qdr_error_t *err);

// dispose
void qd_delivery_state_free(qd_delivery_state_t *ds);


// true if the state is final (an outcome). Once a terminal state
// is reached no further changes are allowed.
//
static inline bool qd_delivery_state_is_terminal(uint64_t type)
{
    return ((PN_ACCEPTED <= type && type <= PN_MODIFIED) ||
            type == 0x0033 /* See section 4.5.5 Declared */);
}

#endif


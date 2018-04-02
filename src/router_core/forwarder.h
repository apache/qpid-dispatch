#ifndef qd_router_core_forwarder
#define qd_router_core_forwarder 1
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

#include "router_core_private.h"

//
// NOTE: If the in_delivery argument is NULL, the resulting out deliveries
//       shall be pre-settled.
//
typedef int (*qdr_forward_message_t) (qdr_core_t      *core,
                                      qdr_address_t   *addr,
                                      qd_message_t    *msg,
                                      qdr_delivery_t  *in_delivery,
                                      bool             exclude_inprocess,
                                      bool             control);

typedef bool (*qdr_forward_attach_t) (qdr_core_t     *core,
                                      qdr_address_t  *addr,
                                      qdr_link_t     *link,
                                      qdr_terminus_t *source,
                                      qdr_terminus_t *target);

struct qdr_forwarder_t {
    qdr_forward_message_t forward_message;
    qdr_forward_attach_t  forward_attach;
    bool                  bypass_valid_origins;
};

qdr_forwarder_t *qdr_new_forwarder(qdr_forward_message_t fm, qdr_forward_attach_t fa, bool bypass_valid_origins);
#endif

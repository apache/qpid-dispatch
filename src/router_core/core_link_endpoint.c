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

#include "core_link_endpoint.h"


void qdrc_endpoint_bind_mobile_address(qdr_core_t                 *core,
                                       const char                 *address,
                                       int                         phase,
                                       qdrc_endpoint_descriptor_t *descriptor,
                                       void                       *bind_context)
{
}


qdrc_endpoint_t *qdrc_endpoint_create_link(qdr_core_t                 *core,
                                           qd_direction_t              dir,
                                           qdr_terminus_t             *source,
                                           qdr_terminus_t             *target,
                                           qdrc_endpoint_descriptor_t *descriptor,
                                           void                       *link_context)
{
    return 0;
}


qd_direction_t qdrc_endpoint_get_direction(const qdrc_endpoint_t *endpoint)
{
    return QD_INCOMING;
}


void qdrc_endpoint_flow(qdrc_endpoint_t *endpoint, int credit_added)
{
}


void qdrc_endpoint_send(qdrc_endpoint_t *endpoint, qdr_delivery_t *delivery)
{
}


void qdrc_endpoint_update(qdrc_endpoint_t *endpoint, qdr_delivery_t *delivery)
{
}


void qdrc_endpoint_detach(qdrc_endpoint_t *endpoint, qdr_error_t *error)
{
}

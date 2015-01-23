#ifndef __ext_container_private_h__
#define __ext_container_private_h__ 1
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

#include "dispatch_private.h"

/**
 * @file
 *
 * An external container is an object that defines an address prefix for link-routing
 * and associates that prefix with an on-demand connector.
 *
 * The address prefix is propagated across the network and can be used as a target for
 * routed links destined for this external container.
 */

qd_external_container_t *qd_external_container(qd_router_t *router, const char *prefix, const char *connector_name);

void qd_external_container_free(qd_external_container_t *ec);

void qd_external_container_free_all(void);


#endif

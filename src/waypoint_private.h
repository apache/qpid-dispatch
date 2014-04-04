#ifndef __waypoint_private_h__
#define __waypoint_private_h__ 1
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

#include <qpid/dispatch/server.h>
#include <qpid/dispatch/connection_manager.h>

typedef struct qd_dispatch_t qd_dispatch_t;
typedef struct qd_waypoint_t qd_waypoint_t;
typedef struct qd_address_t  qd_address_t;
typedef struct qd_link_t     qd_link_t;

void qd_waypoint_activate_all(qd_dispatch_t *qd);

void qd_waypoint_connection_opened(qd_dispatch_t *qd, qd_config_connector_t *cc, qd_connection_t *conn);

void qd_waypoint_new_incoming_link(qd_dispatch_t *qd, qd_waypoint_t *wp, qd_link_t *link);

void qd_waypoint_new_outgoing_link(qd_dispatch_t *qd, qd_waypoint_t *wp, qd_link_t *link);

void qd_waypoint_link_closed(qd_dispatch_t *qd, qd_waypoint_t *wp, qd_link_t *link);

void qd_waypoint_address_updated_LH(qd_dispatch_t *qd, qd_address_t *address);


#endif

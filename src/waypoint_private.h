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
#include "dispatch_private.h"

/**
 * @file
 *
 * A waypoint sends/receives messages to/from an external entity such as a
 * broker as part of a multi-phase address.
 *
 * An address can have multiple phases. Each phase acts like a separate address,
 * but sharing the same address string.
 *
 * Phases are not visible to normal senders/receivers, they are set by
 * waypoints. Messages from normal senders go to the phase=0 address.  Normal
 * subscribers subscribe to the highest phase defined for the address.
 *
 * A waypoint takes messages for its in-phase and sends them to the external
 * entity. Messages received from the external entity are given the waypoint's
 * out-phase. Waypoints can be "chained" with the out-phase of one equal to the
 * in-phase for the next. Thus waypoints provide a way to route messages via
 * multiple external entities between a sender and a subscriber using the same
 * address.
 */

void qd_waypoint_activate_all(qd_dispatch_t *qd);

/** Called when the router opens a connector associated with this waypoint */
void qd_waypoint_connection_opened(qd_dispatch_t *qd, qd_config_connector_t *cc, qd_connection_t *conn);

void qd_waypoint_new_incoming_link(qd_dispatch_t *qd, qd_waypoint_t *wp, qd_link_t *link);

void qd_waypoint_new_outgoing_link(qd_dispatch_t *qd, qd_waypoint_t *wp, qd_link_t *link);

void qd_waypoint_link_closed(qd_dispatch_t *qd, qd_waypoint_t *wp, qd_link_t *link);

void qd_waypoint_address_updated_LH(qd_dispatch_t *qd, qd_address_t *address);


#endif

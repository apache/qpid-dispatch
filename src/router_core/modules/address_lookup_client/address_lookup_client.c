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

#include "core_attach_address_lookup.h"
#include "core_client_api.h"
#include "core_events.h"
#include "module.h"
#include "router_core_private.h"

#include "qpid/dispatch/address_lookup_utils.h"
#include "qpid/dispatch/ctools.h"
#include "qpid/dispatch/discriminator.h"

#include <stdio.h>

static uint64_t on_reply(qdr_core_t    *core,
                         qdrc_client_t *api_client,
                         void          *user_context,
                         void          *request_context,
                         qd_iterator_t *app_properties,
                         qd_iterator_t *body);

static void on_request_done(qdr_core_t    *core,
                            qdrc_client_t *api_client,
                            void          *user_context,
                            void          *request_context,
                            const char    *error);


typedef struct qcm_addr_lookup_request_t {
    DEQ_LINKS(struct qcm_addr_lookup_request_t);
    qdr_connection_t_sp  conn_sp;
    qdr_link_t_sp        link_sp;
    qd_direction_t       dir;
    qdr_terminus_t      *source;
    qdr_terminus_t      *target;
} qcm_addr_lookup_request_t;

DEQ_DECLARE(qcm_addr_lookup_request_t, qcm_addr_lookup_request_list_t);
ALLOC_DECLARE(qcm_addr_lookup_request_t);
ALLOC_DEFINE(qcm_addr_lookup_request_t);


typedef struct qcm_lookup_client_t {
    qdr_core_t                     *core;
    qdrc_event_subscription_t      *event_sub;
    qdr_connection_t               *edge_conn;
    uint32_t                        request_credit;
    bool                            client_api_active;
    qdrc_client_t                  *client_api;
    qcm_addr_lookup_request_list_t  pending_requests;
    qcm_addr_lookup_request_list_t  sent_requests;
} qcm_lookup_client_t;


static char* disambiguated_link_name(qdr_connection_info_t *conn, char *original)
{
    size_t olen = strlen(original);
    size_t clen = strlen(conn->container);
    char *name = (char*) qd_malloc(olen + clen + 2);
    memset(name, 0, olen + clen + 2);
    strcat(name, original);
    name[olen] = '@';
    strcat(name + olen + 1, conn->container);
    return name;
}


/**
 * Generate a temporary routable address for a destination connected to this
 * router node. Caller must free() return value when done.
 */
static char *qdr_generate_temp_addr(qdr_core_t *core)
{
    static const char edge_template[] = "amqp:/_edge/%s/temp.%s";
    static const char topo_template[] = "amqp:/_topo/%s/%s/temp.%s";
    const size_t      max_template    = 19;  // printable chars
    char discriminator[QD_DISCRIMINATOR_SIZE];

    qd_generate_discriminator(discriminator);
    size_t len = max_template + QD_DISCRIMINATOR_SIZE +
        strlen(core->router_id) + strlen(core->router_area) + 1;

    int rc;
    char *buffer = qd_malloc(len);
    if (core->router_mode == QD_ROUTER_MODE_EDGE) {
        rc = snprintf(buffer, len, edge_template, core->router_id, discriminator);
    } else {
        rc = snprintf(buffer, len, topo_template, core->router_area, core->router_id, discriminator);
    }
    (void)rc; assert(rc < len);
    return buffer;
}


/**
 * Generate a temporary mobile address for a producer connected to this
 * router node. Caller must free() return value when done.
 */
static char *qdr_generate_mobile_addr(qdr_core_t *core)
{
    static const char mobile_template[] = "amqp:/_$temp.%s";
    const size_t      max_template      = 13; // printable chars
    char discriminator[QD_DISCRIMINATOR_SIZE];

    qd_generate_discriminator(discriminator);
    size_t len = max_template + QD_DISCRIMINATOR_SIZE + 1;
    char *buffer = qd_malloc(len);
    int rc = snprintf(buffer, len, mobile_template, discriminator);
    (void)rc; assert(rc < len);
    return buffer;
}


/**
 * qdr_lookup_terminus_address_CT
 *
 * Lookup a terminus address in the route table and possibly create a new address
 * if no match is found.
 *
 * @param core Pointer to the core object
 * @param dir Direction of the link for the terminus
 * @param conn The connection over which the terminus was attached
 * @param terminus The terminus containing the addressing information to be looked up
 * @param create_if_not_found Iff true, return a pointer to a newly created address record
 * @param accept_dynamic Iff true, honor the dynamic flag by creating a dynamic address
 * @param [out] link_route True iff the lookup indicates that an attach should be routed
 * @param [out] unavailable True iff this address is blocked as unavailable
 * @param [out] core_endpoint True iff this address is bound to a core-internal endpoint
 * @param [out] fallback True iff this terminus has fallback capability
 * @return Pointer to an address record or 0 if none is found
 */
static qdr_address_t *qdr_lookup_terminus_address_CT(qdr_core_t       *core,
                                                     qd_direction_t    dir,
                                                     qdr_connection_t *conn,
                                                     qdr_terminus_t   *terminus,
                                                     bool              create_if_not_found,
                                                     bool              accept_dynamic,
                                                     bool             *link_route,
                                                     bool             *unavailable,
                                                     bool             *core_endpoint,
                                                     bool             *fallback)
{
    qdr_address_t *addr = 0;

    //
    // Unless expressly stated, link routing is not indicated for this terminus.
    //
    *link_route    = false;
    *unavailable   = false;
    *core_endpoint = false;
    *fallback      = false;

    if (qdr_terminus_is_dynamic(terminus)) {
        //
        // The terminus is dynamic.  Check to see if there is an address provided
        // in the dynamic node properties.  If so, look that address up as a link-routed
        // destination.
        //
        qd_iterator_t *dnp_address = qdr_terminus_dnp_address(terminus);
        if (dnp_address) {
            qd_iterator_reset_view(dnp_address, ITER_VIEW_ADDRESS_WITH_SPACE);
            if (conn->tenant_space)
                qd_iterator_annotate_space(dnp_address, conn->tenant_space, conn->tenant_space_len);
            qd_parse_tree_retrieve_match(core->link_route_tree[dir], dnp_address, (void**) &addr);

            if (addr && conn->tenant_space) {
                //
                // If this link is in a tenant space, translate the dnp address to
                // the fully-qualified view
                //
                qdr_terminus_set_dnp_address_iterator(terminus, dnp_address);
            }

            qd_iterator_free(dnp_address);
            *link_route = true;
            return addr;
        }

        //
        // The dynamic terminus has no address in the dynamic-node-propteries.  If we are
        // permitted to generate dynamic addresses, create a new address that is local to
        // this router and insert it into the address table with a hash index.
        //
        if (!accept_dynamic)
            return 0;

        bool generating = true;
        while (generating) {
            //
            // The address-generation process is performed in a loop in case the generated
            // address collides with a previously generated address (this should be _highly_
            // unlikely).
            //
            char *temp_addr = 0;
            if (dir == QD_OUTGOING)
                temp_addr = qdr_generate_temp_addr(core);
            else
                temp_addr = qdr_generate_mobile_addr(core);

            qd_iterator_t *temp_iter = qd_iterator_string(temp_addr, ITER_VIEW_ADDRESS_HASH);
            qd_hash_retrieve(core->addr_hash, temp_iter, (void**) &addr);
            if (!addr) {
                addr = qdr_address_CT(core, QD_TREATMENT_ANYCAST_BALANCED, 0);
                qd_hash_insert(core->addr_hash, temp_iter, addr, &addr->hash_handle);
                DEQ_INSERT_TAIL(core->addrs, addr);
                qdr_terminus_set_address(terminus, temp_addr);
                generating = false;
            }
            qd_iterator_free(temp_iter);
            free(temp_addr);
        }
        return addr;
    }

    //
    // If the terminus is anonymous, there is no address to look up.
    //
    if (qdr_terminus_is_anonymous(terminus))
        return 0;

    //
    // The terminus has a non-dynamic address that we need to look up.  First, look for
    // a link-route destination for the address.
    //
    qd_iterator_t *iter = qdr_terminus_get_address(terminus);
    qd_iterator_reset_view(iter, ITER_VIEW_ADDRESS_WITH_SPACE);
    if (conn->tenant_space)
        qd_iterator_annotate_space(iter, conn->tenant_space, conn->tenant_space_len);
    qd_parse_tree_retrieve_match(core->link_route_tree[dir], iter, (void**) &addr);
    if (addr) {
        *link_route = true;

        //
        // If this link is in a tenant space, translate the terminus address to
        // the fully-qualified view
        //
        if (conn->tenant_space) {
            qdr_terminus_set_address_iterator(terminus, iter);
        }
        return addr;
    }

    //
    // There was no match for a link-route destination, look for a message-route address.
    //
    int  in_phase  = 0;
    int  out_phase = 0;
    char addr_phase;
    int  priority  = -1;
    qd_address_treatment_t  treat       = core->qd->default_treatment;
    qdr_address_config_t   *addr_config = qdr_config_for_address_CT(core, conn, iter);

    if (addr_config) {
        in_phase  = addr_config->in_phase;
        out_phase = addr_config->out_phase;
        priority  = addr_config->priority;
        treat     = addr_config->treatment;
    }

    //
    // If the terminus has a waypoint capability, override the configured phases and use the waypoint phases.
    //
    int waypoint_ordinal = qdr_terminus_waypoint_capability(terminus);
    if (waypoint_ordinal > 0) {
        in_phase  = waypoint_ordinal;
        out_phase = waypoint_ordinal - 1;
    }

    //
    // Determine if this endpoint is acting as a fallback destination for the address.
    //
    *fallback = qdr_terminus_has_capability(terminus, QD_CAPABILITY_FALLBACK);
    bool edge_link = conn->role == QDR_ROLE_EDGE_CONNECTION;

    qd_iterator_reset_view(iter, ITER_VIEW_ADDRESS_HASH);
    qd_iterator_annotate_prefix(iter, '\0'); // Cancel previous override
    addr_phase = dir == QD_INCOMING ?
        (*fallback && edge_link ? QD_ITER_HASH_PHASE_FALLBACK : in_phase + '0') :
        (*fallback ? QD_ITER_HASH_PHASE_FALLBACK : out_phase + '0');
    qd_iterator_annotate_phase(iter, addr_phase);

    qd_hash_retrieve(core->addr_hash, iter, (void**) &addr);

    if (addr && addr->treatment == QD_TREATMENT_UNAVAILABLE)
        *unavailable = true;

    //
    // If the address is a router-class address, change treatment to closest.
    //
    qd_iterator_reset(iter);
    if (qd_iterator_octet(iter) == (unsigned char) QD_ITER_HASH_PREFIX_ROUTER) {
        treat = QD_TREATMENT_ANYCAST_CLOSEST;

        //
        // It is not valid for an outgoing link to have a router-class address.
        //
        if (dir == QD_OUTGOING)
            return 0;
    }

    if (!addr && create_if_not_found) {
        addr = qdr_address_CT(core, treat, addr_config);
        if (addr) {
            qd_iterator_reset(iter);
            qd_hash_insert(core->addr_hash, iter, addr, &addr->hash_handle);
            DEQ_INSERT_TAIL(core->addrs, addr);

            //
            // If this address is configured with a fallback, set up the
            // fallback address linkage.
            //
            if (!!addr_config && addr_config->fallback && !addr->fallback)
                qdr_setup_fallback_address_CT(core, addr);
        }

        if (!addr && treat == QD_TREATMENT_UNAVAILABLE)
            *unavailable = true;
    }

    if (!!addr && addr->core_endpoint != 0)
        *core_endpoint = true;

    if (addr)
        addr->priority = priority;
    return addr;
}


static void qdr_link_react_to_first_attach_CT(qdr_core_t       *core,
                                              qdr_connection_t *conn,
                                              qdr_address_t    *addr,
                                              qdr_link_t       *link,
                                              qd_direction_t    dir,
                                              qdr_terminus_t   *source,  // must free when done
                                              qdr_terminus_t   *target,  // must free when done
                                              bool              link_route,
                                              bool              unavailable,
                                              bool              core_endpoint,
                                              bool              fallback)
{
    link->fallback = fallback;

    if (core_endpoint) {
        qdrc_endpoint_do_bound_attach_CT(core, addr, link, source, target);
        source = target = 0;  // ownership passed to qdrc_endpoint_do_bound_attach_CT
    }
    else if (unavailable && qdr_terminus_is_coordinator(dir == QD_INCOMING ? target : source) && !addr) {
        qdr_link_outbound_detach_CT(core, link, 0, QDR_CONDITION_COORDINATOR_PRECONDITION_FAILED, true);
    }
    else if (unavailable) {
        qdr_link_outbound_detach_CT(core, link, qdr_error(QD_AMQP_COND_NOT_FOUND, "Node not found"), 0, true);
    }
    else if (!addr) {
        //
        // No route to this destination, reject the link
        //
        qdr_link_outbound_detach_CT(core, link, 0, QDR_CONDITION_NO_ROUTE_TO_DESTINATION, true);
    }
    else if (link_route) {
        //
        // This is a link-routed destination, forward the attach to the next hop
        //
        qdr_terminus_t *term = dir == QD_INCOMING ? target : source;
        if (qdr_terminus_survives_disconnect(term) && !core->qd->allow_resumable_link_route) {
            qdr_link_outbound_detach_CT(core, link, 0, QDR_CONDITION_INVALID_LINK_EXPIRATION, true);
        } else {
            if (conn->role != QDR_ROLE_INTER_ROUTER && conn->connection_info) {
                link->disambiguated_name = disambiguated_link_name(conn->connection_info, link->name);
            }
            bool success = qdr_forward_attach_CT(core, addr, link, source, target);
            if (success) {
                source = target = 0;  // ownership passed to qdr_forward_attach_CT
            } else {
                qdr_link_outbound_detach_CT(core, link, 0, QDR_CONDITION_NO_ROUTE_TO_DESTINATION, true);
            }
        }
    }
    else if (dir == QD_INCOMING && qdr_terminus_is_coordinator(target)) {
        //
        // This target terminus is a coordinator.
        // If we got here, it means that the coordinator link attach could not be link routed to a broker (or to the next router).
        // The router should reject this link because the router cannot coordinate transactions itself.
        //
        // The attach response should have a null target to indicate refusal and the immediately coming detach.
        //
        qdr_link_outbound_second_attach_CT(core, link, source, 0);
        source = 0;  // ownership passed to qdr_link_outbound_second_attach_CT

        //
        // Now, send back a detach with the error amqp:precondition-failed
        //
        qdr_link_outbound_detach_CT(core, link, 0, QDR_CONDITION_COORDINATOR_PRECONDITION_FAILED, true);
    } else {
        //
        // Associate the link with the address.  With this association, it will be unnecessary
        // to do an address lookup for deliveries that arrive on this link.
        //
        qdr_core_bind_address_link_CT(core, addr, link);
        qdr_link_outbound_second_attach_CT(core, link, source, target);
        source = target = 0;  // ownership passed to qdr_link_outbound_second_attach_CT

        //
        // Issue the initial credit only if one of the following
        // holds:
        // - there are destinations for the address
        // - the address is that of an exchange (no subscribers allowed)
        //
        if (dir == QD_INCOMING
            && (DEQ_SIZE(addr->subscriptions)
                || DEQ_SIZE(addr->rlinks)
                || qd_bitmask_cardinality(addr->rnodes)
                || !!addr->exchange
                || (!!addr->fallback
                    && (DEQ_SIZE(addr->fallback->subscriptions)
                        || DEQ_SIZE(addr->fallback->rlinks)
                        || qd_bitmask_cardinality(addr->fallback->rnodes))))) {
            qdr_link_issue_credit_CT(core, link, link->capacity, false);
        }

        //
        // If this link came through an edge connection, raise a link event to
        // herald that fact.
        //
        if (link->conn->role == QDR_ROLE_EDGE_CONNECTION)
            qdrc_event_link_raise(core, QDRC_EVENT_LINK_EDGE_DATA_ATTACHED, link);
    }

    if (source)
        qdr_terminus_free(source);
    if (target)
        qdr_terminus_free(target);
}


static void qcm_addr_lookup_local_search(qcm_lookup_client_t *client, qcm_addr_lookup_request_t *request)
{
    bool              link_route;
    bool              unavailable;
    bool              core_endpoint;
    bool              fallback;
    qdr_connection_t *conn = safe_deref_qdr_connection_t(request->conn_sp);
    qdr_link_t       *link = safe_deref_qdr_link_t(request->link_sp);

    if (conn == 0 || link == 0)
        return;
    
    qdr_terminus_t *term = request->dir == QD_INCOMING ? request->target : request->source;
    qdr_address_t  *addr = qdr_lookup_terminus_address_CT(client->core,
                                                          request->dir,
                                                          conn,
                                                          term,
                                                          true,
                                                          true,
                                                          &link_route,
                                                          &unavailable,
                                                          &core_endpoint,
                                                          &fallback);
    qdr_link_react_to_first_attach_CT(client->core,
                                      conn,
                                      addr,
                                      link,
                                      request->dir,
                                      request->source,
                                      request->target,
                                      link_route,
                                      unavailable,
                                      core_endpoint,
                                      fallback);
}


static void qcm_addr_lookup_process_pending_requests_CT(qcm_lookup_client_t *client)
{
    const uint32_t timeout = 3;
    int result;

    while (client->request_credit > 0 && DEQ_SIZE(client->pending_requests) > 0) {
        qcm_addr_lookup_request_t *request = DEQ_HEAD(client->pending_requests);
        DEQ_REMOVE_HEAD(client->pending_requests);

        do {
            qd_composed_field_t *props;
            qd_composed_field_t *body;
            qd_iterator_t       *iter = qdr_terminus_get_address(request->dir == QD_INCOMING ? request->target : request->source);

            if (iter) {
                result = qcm_link_route_lookup_request(iter, request->dir, &props, &body);
                if (result == 0) {
                    result = qdrc_client_request_CT(client->client_api, request, props, body, timeout, on_reply, 0, on_request_done);
                    if (result == 0) {
                        DEQ_INSERT_TAIL(client->sent_requests, request);
                        client->request_credit--;
                        break;
                    }

                    //
                    // TODO - set a timer (or use a timeout in the client API)
                    //

                    qd_compose_free(props);
                    qd_compose_free(body);
                }
            }

            //
            // If we get here, we failed to launch the asynchronous lookup.  Fall back to a local,
            // synchronous lookup.
            //
            qcm_addr_lookup_local_search(client, request);
            free_qcm_addr_lookup_request_t(request);
        } while (false);
    }
}


static bool qcm_terminus_has_local_link_route(qdr_core_t *core, qdr_connection_t *conn, qdr_terminus_t *terminus, qd_direction_t dir)
{
    qdr_address_t *addr;
    qd_iterator_t *iter = qd_iterator_dup(qdr_terminus_get_address(terminus));
    qd_iterator_reset_view(iter, ITER_VIEW_ADDRESS_WITH_SPACE);
    if (conn->tenant_space)
        qd_iterator_annotate_space(iter, conn->tenant_space, conn->tenant_space_len);
    qd_parse_tree_retrieve_match(core->link_route_tree[dir], iter, (void**) &addr);
    qd_iterator_free(iter);
    return addr && (DEQ_SIZE(addr->conns) > 0);
}



//================================================================================
// Address Lookup Handler
//================================================================================

static void qcm_addr_lookup_CT(void             *context,
                               qdr_connection_t *conn,
                               qdr_link_t       *link,
                               qd_direction_t    dir,
                               qdr_terminus_t   *source,
                               qdr_terminus_t   *target)
{
    qcm_lookup_client_t *client = (qcm_lookup_client_t*) context;
    bool                 link_route;
    bool                 unavailable;
    bool                 core_endpoint;
    bool                 fallback;
    qdr_terminus_t      *term = dir == QD_INCOMING ? target : source;

    if (client->core->router_mode == QD_ROUTER_MODE_EDGE
        && client->client_api_active
        && conn != client->edge_conn
        && qdr_terminus_get_address(term) != 0
        && !qcm_terminus_has_local_link_route(client->core, conn, term, dir)) {
        //
        // We are in edge mode, there is an active edge connection, the terminus has an address,
        // and there is no local link route for this address.  Set up the asynchronous lookup.
        //
        qcm_addr_lookup_request_t *request = new_qcm_addr_lookup_request_t();
        DEQ_ITEM_INIT(request);
        set_safe_ptr_qdr_connection_t(conn, &request->conn_sp);
        set_safe_ptr_qdr_link_t(link, &request->link_sp);
        request->dir    = dir;
        request->source = source;
        request->target = target;

        DEQ_INSERT_TAIL(client->pending_requests, request);
        qcm_addr_lookup_process_pending_requests_CT(client);
        return;
    }

    //
    // If this lookup doesn't meet the criteria for asynchronous action, perform the built-in, synchronous address lookup
    //
    qdr_address_t *addr = qdr_lookup_terminus_address_CT(client->core, dir, conn, term, true, true,
                                                         &link_route, &unavailable, &core_endpoint, &fallback);
    qdr_link_react_to_first_attach_CT(client->core, conn, addr, link, dir, source, target,
                                      link_route, unavailable, core_endpoint, fallback);
}


//================================================================================
// Core Client API Handlers
//================================================================================

static void on_state(qdr_core_t    *core,
                     qdrc_client_t *api_client,
                     void          *user_context,
                     bool           active)
{
    qcm_lookup_client_t *client = (qcm_lookup_client_t*) user_context;

    client->client_api_active = active;
    if (!active) {
        //
        // Client-API links are down, set our available credit to zero.
        //
        client->request_credit = 0;

        //
        // Locally process all pending requests
        //
        qcm_addr_lookup_request_t *request = DEQ_HEAD(client->pending_requests);
        while (request) {
            DEQ_REMOVE_HEAD(client->pending_requests);
            qcm_addr_lookup_local_search(client, request);
            free_qcm_addr_lookup_request_t(request);
            request = DEQ_HEAD(client->pending_requests);
        }
    }
}


static void on_flow(qdr_core_t    *core,
                    qdrc_client_t *api_client,
                    void          *user_context,
                    int            more_credit,
                    bool           drain)
{
    qcm_lookup_client_t *client = (qcm_lookup_client_t*) user_context;

    client->request_credit += more_credit;

    //
    // If we have positive credit, process any pending requests
    //
    if (client->request_credit > 0)
        qcm_addr_lookup_process_pending_requests_CT(client);

    if (drain)
        client->request_credit = 0;
}


static uint64_t on_reply(qdr_core_t    *core,
                         qdrc_client_t *api_client,
                         void          *user_context,
                         void          *request_context,
                         qd_iterator_t *app_properties,
                         qd_iterator_t *body)
{
    qcm_lookup_client_t         *client  = (qcm_lookup_client_t*) user_context;
    qcm_addr_lookup_request_t   *request = (qcm_addr_lookup_request_t*) request_context;
    qcm_address_lookup_status_t  status;
    bool                         is_link_route;
    bool                         has_destinations;

    qdr_connection_t *conn = safe_deref_qdr_connection_t(request->conn_sp);
    qdr_link_t       *link = safe_deref_qdr_link_t(request->link_sp);

    //
    // If the connection or link pointers are NULL, exit without processing
    // because either the connection or link has been freed while the
    // request was in-flight.
    //
    if (conn == 0 || link == 0) {
        qdr_terminus_free(request->source);
        qdr_terminus_free(request->target);
        qd_iterator_free(body);
        qd_iterator_free(app_properties);
        return 0;
    }

    status = qcm_link_route_lookup_decode(app_properties, body, &is_link_route, &has_destinations);
    if (status == QCM_ADDR_LOOKUP_OK) {
        //
        // The lookup decode is of a valid service response.
        //
        if (!is_link_route)
            //
            // The address is not for a link route.  Use the local search.
            //
            qcm_addr_lookup_local_search(client, request);

        else if (!has_destinations)
            //
            // The address is for a link route, but there are no destinations upstream.  Fail with no-route.
            //
            qdr_link_outbound_detach_CT(core, link, 0, QDR_CONDITION_NO_ROUTE_TO_DESTINATION, true);
        else
            //
            // The address is for a link route and there are destinations upstream.  Directly forward the attach.
            //
            qdr_forward_link_direct_CT(core, client->edge_conn, link, request->source, request->target, 0, 0);

    } else {
        //
        // The reply was not a valid server response.  Fall back to the local search.
        //
        qcm_addr_lookup_local_search(client, request);
    }

    qd_iterator_free(body);
    qd_iterator_free(app_properties);

    return 0;
}


static void on_request_done(qdr_core_t    *core,
                            qdrc_client_t *api_client,
                            void          *user_context,
                            void          *request_context,
                            const char    *error)
{
    qcm_lookup_client_t       *client  = (qcm_lookup_client_t*) user_context;
    qcm_addr_lookup_request_t *request = (qcm_addr_lookup_request_t*) request_context;

    if (error) {
        qcm_addr_lookup_local_search(client, request);
    }

    DEQ_REMOVE(client->sent_requests, request);
    free_qcm_addr_lookup_request_t(request);
}


//================================================================================
// Event Handlers
//================================================================================

static void on_conn_event(void             *context,
                          qdrc_event_t      event_type,
                          qdr_connection_t *conn)
{
    qcm_lookup_client_t *client = (qcm_lookup_client_t*) context;

    switch (event_type) {
    case QDRC_EVENT_CONN_EDGE_ESTABLISHED:
        client->edge_conn      = conn;
        client->request_credit = 0;

        //
        // Set up a Client API session on the edge connection.
        //
        qdr_terminus_t *target = qdr_terminus(0);
        qdr_terminus_set_address(target, QD_TERMINUS_ADDRESS_LOOKUP);
        client->client_api = qdrc_client_CT(client->core,
                                            client->edge_conn,
                                            target,
                                            250,
                                            client,
                                            on_state,
                                            on_flow);
        break;

    case QDRC_EVENT_CONN_EDGE_LOST:
        client->edge_conn      = 0;
        client->request_credit = 0;

        //
        // Remove the Client API session.
        //
        qdrc_client_free_CT(client->client_api);
        client->client_api = 0;
        break;

    default:
        assert(false);
        break;
    }
}

//================================================================================
// Module Handlers
//================================================================================

static bool qcm_addr_lookup_client_enable_CT(qdr_core_t *core)
{
    return true;
}


static void qcm_addr_lookup_client_init_CT(qdr_core_t *core, void **module_context)
{
    assert(core->addr_lookup_handler == 0);
    qcm_lookup_client_t *client = NEW(qcm_lookup_client_t);
    ZERO(client);

    client->core      = core;
    client->event_sub = qdrc_event_subscribe_CT(client->core,
                                                QDRC_EVENT_CONN_EDGE_ESTABLISHED | QDRC_EVENT_CONN_EDGE_LOST,
                                                on_conn_event, 0, 0, 0,
                                                client);

    core->addr_lookup_handler = qcm_addr_lookup_CT;
    core->addr_lookup_context = client;
    *module_context           = client;
}


static void qcm_addr_lookup_client_final_CT(void *module_context)
{
    qcm_lookup_client_t *client = (qcm_lookup_client_t*) module_context;
    qdrc_event_unsubscribe_CT(client->core, client->event_sub);
    client->core->addr_lookup_handler = 0;
    qdrc_client_free_CT(client->client_api);
    free(client);
}


QDR_CORE_MODULE_DECLARE("address_lookup_client", qcm_addr_lookup_client_enable_CT, qcm_addr_lookup_client_init_CT, qcm_addr_lookup_client_final_CT)

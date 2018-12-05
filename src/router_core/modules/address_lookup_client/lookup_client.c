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

#include "module.h"
#include "core_attach_address_lookup.h"
#include "router_core_private.h"
#include <qpid/dispatch/discriminator.h>
#include <stdio.h>

static char* disambiguated_link_name(qdr_connection_info_t *conn, char *original)
{
    size_t olen = strlen(original);
    size_t clen = strlen(conn->container);
    char *name = (char*) malloc(olen + clen + 2);
    memset(name, 0, olen + clen + 2);
    strcat(name, original);
    name[olen] = '@';
    strcat(name + olen + 1, conn->container);
    return name;
}


/**
 * Generate a temporary routable address for a destination connected to this
 * router node.
 */
static void qdr_generate_temp_addr(qdr_core_t *core, char *buffer, size_t length)
{
    char discriminator[QD_DISCRIMINATOR_SIZE];
    qd_generate_discriminator(discriminator);
    if (core->router_mode == QD_ROUTER_MODE_EDGE)
        snprintf(buffer, length, "amqp:/_edge/%s/temp.%s", core->router_id, discriminator);
    else
        snprintf(buffer, length, "amqp:/_topo/%s/%s/temp.%s", core->router_area, core->router_id, discriminator);
}


/**
 * Generate a temporary mobile address for a producer connected to this
 * router node.
 */
static void qdr_generate_mobile_addr(qdr_core_t *core, char *buffer, size_t length)
{
    char discriminator[QD_DISCRIMINATOR_SIZE];
    qd_generate_discriminator(discriminator);
    snprintf(buffer, length, "amqp:/_$temp.%s", discriminator);
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
                                                     bool             *core_endpoint)
{
    qdr_address_t *addr = 0;

    //
    // Unless expressly stated, link routing is not indicated for this terminus.
    //
    *link_route    = false;
    *unavailable   = false;
    *core_endpoint = false;

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

        char temp_addr[200];
        bool generating = true;
        while (generating) {
            //
            // The address-generation process is performed in a loop in case the generated
            // address collides with a previously generated address (this should be _highly_
            // unlikely).
            //
            if (dir == QD_OUTGOING)
                qdr_generate_temp_addr(core, temp_addr, 200);
            else
                qdr_generate_mobile_addr(core, temp_addr, 200);

            qd_iterator_t *temp_iter = qd_iterator_string(temp_addr, ITER_VIEW_ADDRESS_HASH);
            qd_hash_retrieve(core->addr_hash, temp_iter, (void**) &addr);
            if (!addr) {
                addr = qdr_address_CT(core, QD_TREATMENT_ANYCAST_BALANCED);
                qd_hash_insert(core->addr_hash, temp_iter, addr, &addr->hash_handle);
                DEQ_INSERT_TAIL(core->addrs, addr);
                qdr_terminus_set_address(terminus, temp_addr);
                generating = false;
            }
            qd_iterator_free(temp_iter);
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
    int in_phase;
    int out_phase;
    int addr_phase;
    int priority;
    qd_address_treatment_t treat = qdr_treatment_for_address_CT(core, conn, iter, &in_phase, &out_phase, &priority);

    qd_iterator_reset_view(iter, ITER_VIEW_ADDRESS_HASH);
    qd_iterator_annotate_prefix(iter, '\0'); // Cancel previous override
    addr_phase = dir == QD_INCOMING ? in_phase : out_phase;
    qd_iterator_annotate_phase(iter, (char) addr_phase + '0');

    qd_hash_retrieve(core->addr_hash, iter, (void**) &addr);

    if (addr && addr->treatment == QD_TREATMENT_UNAVAILABLE)
        *unavailable = true;

    if (!addr && create_if_not_found) {
        //
        // If the address is a router-class address, change treatment to closest.
        //
        qd_iterator_reset(iter);
        if (qd_iterator_octet(iter) == (unsigned char) QD_ITER_HASH_PREFIX_ROUTER) {
            treat = QD_TREATMENT_ANYCAST_CLOSEST;
        }

        addr = qdr_address_CT(core, treat);
        if (addr) {
            qd_hash_insert(core->addr_hash, iter, addr, &addr->hash_handle);
            DEQ_INSERT_TAIL(core->addrs, addr);
        }

        if (!addr && treat == QD_TREATMENT_UNAVAILABLE)
            *unavailable = true;
    }

    if (qdr_terminus_is_coordinator(terminus))
        *unavailable = false;

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
                                              qdr_terminus_t   *source,
                                              qdr_terminus_t   *target,
                                              bool              link_route,
                                              bool              unavailable,
                                              bool              core_endpoint)
{
    if (core_endpoint) {
        qdrc_endpoint_do_bound_attach_CT(core, addr, link, source, target);
    }

    else if (unavailable) {
        qdr_link_outbound_detach_CT(core, link, qdr_error(QD_AMQP_COND_NOT_FOUND, "Node not found"), 0, true);
        qdr_terminus_free(source);
        qdr_terminus_free(target);
    }

    else if (!addr) {
        //
        // No route to this destination, reject the link
        //
        qdr_link_outbound_detach_CT(core, link, 0, QDR_CONDITION_NO_ROUTE_TO_DESTINATION, true);
        qdr_terminus_free(source);
        qdr_terminus_free(target);
    }

    else if (link_route) {
        //
        // This is a link-routed destination, forward the attach to the next hop
        //
        qdr_terminus_t *term = dir == QD_INCOMING ? target : source;
        if (qdr_terminus_survives_disconnect(term) && !core->qd->allow_resumable_link_route) {
            qdr_link_outbound_detach_CT(core, link, 0, QDR_CONDITION_INVALID_LINK_EXPIRATION, true);
            qdr_terminus_free(source);
            qdr_terminus_free(target);
        } else {
            if (conn->role != QDR_ROLE_INTER_ROUTER && conn->connection_info) {
                link->disambiguated_name = disambiguated_link_name(conn->connection_info, link->name);
            }
            bool success = qdr_forward_attach_CT(core, addr, link, source, target);
            if (!success) {
                qdr_link_outbound_detach_CT(core, link, 0, QDR_CONDITION_NO_ROUTE_TO_DESTINATION, true);
                qdr_terminus_free(source);
                qdr_terminus_free(target);
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

        //
        // Issue the initial credit only if one of the following
        // holds:
        // - there are destinations for the address
        // - if the address treatment is multicast
        // - the address is that of an exchange (no subscribers allowed)
        //
        if (dir == QD_INCOMING
            && (DEQ_SIZE(addr->subscriptions)
                || DEQ_SIZE(addr->rlinks)
                || qd_bitmask_cardinality(addr->rnodes)
                || qdr_is_addr_treatment_multicast(addr)
                || !!addr->exchange)) {
            qdr_link_issue_credit_CT(core, link, link->capacity, false);
        }

        //
        // If this link came through an edge connection, raise a link event to
        // herald that fact.
        //
        if (link->conn->role == QDR_ROLE_EDGE_CONNECTION)
            qdrc_event_link_raise(core, QDRC_EVENT_LINK_EDGE_DATA_ATTACHED, link);
    }
}


static void qcm_addr_lookup_CT(qdr_core_t       *core,
                               qdr_connection_t *conn,
                               qdr_link_t       *link,
                               qd_direction_t    dir,
                               qdr_terminus_t   *source,
                               qdr_terminus_t   *target)

{
    bool link_route;
    bool unavailable;
    bool core_endpoint;
    qdr_terminus_t *term = dir == QD_INCOMING ? target : source;

    qdr_address_t *addr = qdr_lookup_terminus_address_CT(core, dir, conn, term, true, true, &link_route, &unavailable, &core_endpoint);
    qdr_link_react_to_first_attach_CT(core, conn, addr, link, dir, source, target, link_route, unavailable, core_endpoint);
}


static bool qcm_addr_lookup_client_enable_CT(qdr_core_t *core)
{
    return true;
}


static void qcm_addr_lookup_client_init_CT(qdr_core_t *core, void **module_context)
{
    assert(core->addr_lookup_handler == 0);

    core->addr_lookup_handler = qcm_addr_lookup_CT;
    *module_context           = core;
}


static void qcm_addr_lookup_client_final_CT(void *module_context)
{
    qdr_core_t *core = (qdr_core_t*) module_context;
    core->addr_lookup_handler = 0;
}


QDR_CORE_MODULE_DECLARE("address_lookup_client", qcm_addr_lookup_client_enable_CT, qcm_addr_lookup_client_init_CT, qcm_addr_lookup_client_final_CT)

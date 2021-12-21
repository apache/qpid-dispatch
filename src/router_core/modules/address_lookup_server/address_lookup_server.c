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
#include "module.h"

#include "qpid/dispatch/address_lookup_utils.h"
#include "qpid/dispatch/ctools.h"

#include <inttypes.h>

#define CREDIT_WINDOW 32

typedef struct _endpoint_ref {
    DEQ_LINKS(struct _endpoint_ref);
    qdrc_endpoint_t *endpoint;
    const char *container_id;
} _endpoint_ref_t;
DEQ_DECLARE(_endpoint_ref_t, _endpoint_ref_list_t);
ALLOC_DECLARE(_endpoint_ref_t);
ALLOC_DEFINE(_endpoint_ref_t);


static struct {
    qdr_core_t           *core;
    _endpoint_ref_list_t  endpoints;
} _server_state;


/* parse out the opcode from the request
 */
static address_lookup_opcode_t _decode_opcode(qd_parsed_field_t *properties)
{
    if (!properties)
        return OPCODE_INVALID;
    qd_parsed_field_t *oc = qd_parse_value_by_key(properties, "opcode");
    if (!oc)
        return OPCODE_INVALID;
    uint32_t opcode = qd_parse_as_uint(oc);
    if (!qd_parse_ok(oc))
        return OPCODE_INVALID;
    return (address_lookup_opcode_t)opcode;
}


/* send a reply to a lookup request
 */
static uint64_t _send_reply(_endpoint_ref_t             *epr,
                            address_lookup_opcode_t      opcode,
                            qcm_address_lookup_status_t  status,
                            qd_iterator_t               *correlation_id,
                            qd_iterator_t               *reply_to,
                            qd_composed_field_t         *body)
{
    if (!correlation_id || !reply_to) {
        qd_log(_server_state.core->log, QD_LOG_ERROR,
               "Link route address reply failed - invalid request message properties"
               " (container=%s, endpoint=%p)",
               epr->container_id, (void *)epr->endpoint);
        qd_compose_free(body);
        return PN_REJECTED;
    }

    qd_composed_field_t *fld = qd_compose(QD_PERFORMATIVE_HEADER, 0);
    qd_compose_start_list(fld);
    qd_compose_insert_bool(fld, 0);     // durable
    qd_compose_end_list(fld);

    fld = qd_compose(QD_PERFORMATIVE_PROPERTIES, fld);
    qd_compose_start_list(fld);
    qd_compose_insert_null(fld);                    // message-id
    qd_compose_insert_null(fld);                    // user-id
    qd_compose_insert_typed_iterator(fld, reply_to); // to
    qd_compose_insert_null(fld);                    // subject
    qd_compose_insert_null(fld);                    // reply-to
    qd_compose_insert_typed_iterator(fld, correlation_id);
    qd_compose_end_list(fld);

    fld = qd_compose(QD_PERFORMATIVE_APPLICATION_PROPERTIES, fld);
    qd_compose_start_map(fld);
    qd_compose_insert_string(fld, "version");
    qd_compose_insert_uint(fld,   PROTOCOL_VERSION);
    qd_compose_insert_string(fld, "opcode");
    qd_compose_insert_uint(fld,   opcode);
    qd_compose_insert_string(fld, "status");
    qd_compose_insert_uint(fld,   status);
    qd_compose_end_map(fld);

    qd_message_t *msg = qd_message_compose(fld, body, 0, true);
    qdr_in_process_send_to_CT(_server_state.core, reply_to, msg, true, false);
    qd_message_free(msg);

    return PN_ACCEPTED;
}


/* perform a link route lookup
 */
static uint64_t _do_link_route_lookup(_endpoint_ref_t   *epr,
                                      qd_parsed_field_t *body,
                                      qd_iterator_t     *reply_to,
                                      qd_iterator_t     *cid)
{
    if (!body || !qd_parse_ok(body) || qd_parse_sub_count(body) < 2) {
        qd_log(_server_state.core->log, QD_LOG_ERROR,
               "Link route address lookup failed - invalid request body"
               " (container=%s, endpoint=%p)",
               epr->container_id, (void *)epr->endpoint);
        return PN_REJECTED;
    }

    //
    // body[0] == fully qualified address (string)
    // body[1] == direction (bool, true == receiver)
    //

    qd_iterator_t *addr_i = qd_parse_raw(qd_parse_sub_value(body, 0));
    qd_direction_t dir = (qd_parse_as_bool(qd_parse_sub_value(body, 1))
                          ? QD_INCOMING : QD_OUTGOING);

    bool is_link_route = false;
    bool has_destinations = false;
    qdr_address_t *addr = 0;
    qd_iterator_reset_view(addr_i, ITER_VIEW_ALL);
    qd_parse_tree_retrieve_match(_server_state.core->link_route_tree[dir], addr_i, (void**) &addr);
    if (addr) {
        is_link_route = true;
        has_destinations = !!(DEQ_SIZE(addr->conns) || DEQ_SIZE(addr->rlinks) || qd_bitmask_cardinality(addr->rnodes));
    }

    // out_body[0] == is_link_route (bool)
    // out_body[1] == has_destinations (bool)

    qd_composed_field_t *out_body = qd_compose(QD_PERFORMATIVE_BODY_AMQP_VALUE, 0);
    qd_compose_start_list(out_body);
    qd_compose_insert_bool(out_body, is_link_route);
    qd_compose_insert_bool(out_body, has_destinations);
    qd_compose_end_list(out_body);

    uint64_t rc = _send_reply(epr,
                              OPCODE_LINK_ROUTE_LOOKUP,
                              addr ? QCM_ADDR_LOOKUP_OK : QCM_ADDR_LOOKUP_NOT_FOUND,
                              cid,
                              reply_to,
                              out_body);

    if (qd_log_enabled(_server_state.core->log, QD_LOG_TRACE)) {
        char *as = (char *)qd_iterator_copy(addr_i);
        qd_log(_server_state.core->log, QD_LOG_TRACE,
               "Link route address lookup on %s - %sfound is link route=%s has_destinations=%s"
               " (container=%s, endpoint=%p)",
               as,
               (addr) ? "" : "not ",
               is_link_route ? "yes" : "no",
               has_destinations ? "yes" : "no",
               epr->container_id,
               (void *)epr->endpoint);
        free(as);
    }
    return rc;
}


/* handle lookup request from client
 */
void _on_transfer(void           *link_context,
                  qdr_delivery_t *delivery,
                  qd_message_t   *message)
{
    if (!qd_message_receive_complete(message))
        return;

    _endpoint_ref_t *epr = (_endpoint_ref_t *)link_context;
    qd_log(_server_state.core->log, QD_LOG_TRACE,
           "Address lookup request received (container=%s, endpoint=%p)",
           epr->container_id, (void *)epr->endpoint);

    uint64_t disposition = PN_ACCEPTED;
    qd_iterator_t *p_iter = qd_message_field_iterator(message, QD_FIELD_APPLICATION_PROPERTIES);
    qd_parsed_field_t *props = qd_parse(p_iter);
    if (!props || !qd_parse_ok(props) || !qd_parse_is_map(props)) {
        qd_log(_server_state.core->log, QD_LOG_ERROR,
               "Invalid address lookup request - no properties (container=%s, endpoint=%p)",
               epr->container_id, (void *)epr->endpoint);
        disposition = PN_REJECTED;
        goto exit;
    }

    qd_parsed_field_t *v = qd_parse_value_by_key(props, "version");
    if (!v) {
        qd_log(_server_state.core->log, QD_LOG_ERROR,
               "Invalid address lookup request - no version (container=%s, endpoint=%p)",
               epr->container_id, (void *)epr->endpoint);
        disposition = PN_REJECTED;
        goto exit;
    }

    uint32_t version = qd_parse_as_uint(v);
    if (!qd_parse_ok(v)) {
        qd_log(_server_state.core->log, QD_LOG_ERROR,
               "Invalid address lookup request - invalid version (container=%s, endpoint=%p)",
               epr->container_id, (void *)epr->endpoint);
        disposition = PN_REJECTED;
        goto exit;
    }

    if (version != PROTOCOL_VERSION) {
        qd_log(_server_state.core->log, QD_LOG_ERROR,
               "Invalid address lookup request - unknown version"
               " (container=%s, endpoint=%p, version=%"PRIu32")",
               epr->container_id, (void *)epr->endpoint, version);
        disposition = PN_REJECTED;
        goto exit;
        // @TODO(kgiusti) send reply with status QCM_ADDR_LOOKUP_BAD_VERSION
    }

    address_lookup_opcode_t opcode = _decode_opcode(props);
    switch (opcode) {
    case OPCODE_LINK_ROUTE_LOOKUP: {
        qd_iterator_t *b_iter = qd_message_field_iterator(message, QD_FIELD_BODY);
        qd_parsed_field_t *body = qd_parse(b_iter);
        qd_iterator_t *reply_to = qd_message_field_iterator_typed(message, QD_FIELD_REPLY_TO);
        qd_iterator_t *cid = qd_message_field_iterator_typed(message, QD_FIELD_CORRELATION_ID);
        disposition = _do_link_route_lookup(epr, body, reply_to, cid);
        qd_iterator_free(cid);
        qd_iterator_free(reply_to);
        qd_parse_free(body);
        qd_iterator_free(b_iter);
        break;
    }
    case OPCODE_INVALID:
    default:
        qd_log(_server_state.core->log, QD_LOG_ERROR,
               "Invalid address lookup request - invalid opcode"
               " (container=%s, endpoint=%p, opcode=%d)",
               epr->container_id, (void *)epr->endpoint, opcode);
        disposition = PN_REJECTED;
    }

exit:
    qd_parse_free(props);
    qd_iterator_free(p_iter);
    qdrc_endpoint_settle_CT(_server_state.core, delivery, disposition);
    qdrc_endpoint_flow_CT(_server_state.core, epr->endpoint, 1, false);
    return;
}


/* handle incoming attach to address lookup service
 */
static void _on_first_attach(void            *bind_context,
                             qdrc_endpoint_t *endpoint,
                             void            **link_context,
                             qdr_terminus_t  *remote_source,
                             qdr_terminus_t  *remote_target)
{
    //
    // Only accept incoming links initiated by the edge router. Detach all
    // other links
    //
    qdr_connection_t *conn = qdrc_endpoint_get_connection_CT(endpoint);
    if (qdrc_endpoint_get_direction_CT(endpoint) != QD_INCOMING ||
        conn->role != QDR_ROLE_EDGE_CONNECTION) {
        *link_context = 0;
        qdrc_endpoint_detach_CT(_server_state.core, endpoint, 0);
        qd_log(_server_state.core->log, QD_LOG_ERROR,
               "Attempt to attach to address lookup server rejected (container=%s)",
               (conn->connection_info) ? conn->connection_info->container : "<unknown>");
        qdr_terminus_free(remote_source);
        qdr_terminus_free(remote_target);
        return;
    }

    _endpoint_ref_t *epr = new__endpoint_ref_t();
    ZERO(epr);
    epr->endpoint = endpoint;
    epr->container_id = (conn->connection_info) ? conn->connection_info->container : "<unknown>";
    DEQ_INSERT_TAIL(_server_state.endpoints, epr);
    *link_context = epr;
    qdrc_endpoint_second_attach_CT(_server_state.core, endpoint, remote_source, remote_target);
    qdrc_endpoint_flow_CT(_server_state.core, endpoint, CREDIT_WINDOW, false);

    qd_log(_server_state.core->log, QD_LOG_TRACE,
           "Client attached to address lookup server (container=%s, endpoint=%p)",
           epr->container_id, (void *)endpoint);
}


/* handle incoming detach from client
 */
static void _on_first_detach(void *link_context,
                             qdr_error_t *error)
{
    _endpoint_ref_t *epr = (_endpoint_ref_t *)link_context;
    qd_log(_server_state.core->log, QD_LOG_TRACE,
           "Client detached from address lookup server (container=%s, endpoint=%p)",
           epr->container_id, (void *)epr->endpoint);
    qdrc_endpoint_detach_CT(_server_state.core, epr->endpoint, 0);
    DEQ_REMOVE(_server_state.endpoints, epr);
    qdr_error_free(error);
    free__endpoint_ref_t(epr);
}


static qdrc_endpoint_desc_t _endpoint_handlers =
{
    .label = "address lookup",
    .on_first_attach = _on_first_attach,
    .on_transfer = _on_transfer,
    .on_first_detach = _on_first_detach,
};


static bool _addres_lookup_enable_CT(qdr_core_t *core)
{
    return core->router_mode == QD_ROUTER_MODE_INTERIOR;
}


static void _address_lookup_init_CT(qdr_core_t *core, void **module_context)
{
    _server_state.core = core;

    //
    // Handle any incoming links to the QD_TERMINUS_ADDRESS_LOOKUP address
    //
    qdrc_endpoint_bind_mobile_address_CT(core,
                                         QD_TERMINUS_ADDRESS_LOOKUP,
                                         '0', // phase
                                         &_endpoint_handlers,
                                         &_server_state);
    *module_context = &_server_state;
}


static void _address_lookup_final_CT(void *module_context)
{
    _endpoint_ref_t *epr = DEQ_HEAD(_server_state.endpoints);
    while (epr) {
        DEQ_REMOVE_HEAD(_server_state.endpoints);
        free__endpoint_ref_t(epr);
        epr = DEQ_HEAD(_server_state.endpoints);
    }
}


QDR_CORE_MODULE_DECLARE("address_lookup_server", _addres_lookup_enable_CT, _address_lookup_init_CT, _address_lookup_final_CT)

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

//
// API for interacting with the core address lookup server
//

#include "qpid/dispatch/address_lookup_utils.h"

#include "qpid/dispatch/amqp.h"
#include "qpid/dispatch/parse.h"

/* create the message application properties and body for the link route lookup
 * request message
 */
int qcm_link_route_lookup_request(qd_iterator_t        *address,
                                  qd_direction_t        dir,
                                  qd_composed_field_t **properties,
                                  qd_composed_field_t **body)
{
    *properties = qd_compose(QD_PERFORMATIVE_APPLICATION_PROPERTIES, 0);
    if (!*properties)
        return -1;
    qd_compose_start_map(*properties);
    qd_compose_insert_string(*properties, "version");
    qd_compose_insert_uint(*properties,   PROTOCOL_VERSION);
    qd_compose_insert_string(*properties, "opcode");
    qd_compose_insert_uint(*properties,   OPCODE_LINK_ROUTE_LOOKUP);
    qd_compose_end_map(*properties);

    *body = qd_compose(QD_PERFORMATIVE_BODY_AMQP_VALUE, 0);
    if (!*body) {
        qd_compose_free(*properties);
        *properties = 0;
        return -1;
    }
    qd_compose_start_list(*body);
    qd_compose_insert_string_iterator(*body, address);
    qd_compose_insert_bool(*body, (dir == QD_INCOMING
                                   ? QD_AMQP_LINK_ROLE_RECEIVER
                                   : QD_AMQP_LINK_ROLE_SENDER));
    qd_compose_end_list(*body);
    return 0;
}


/* parse a reply to the link route lookup request
 */
qcm_address_lookup_status_t qcm_link_route_lookup_decode(qd_iterator_t *properties,
                                                         qd_iterator_t *body,
                                                         bool          *is_link_route,
                                                         bool          *has_destinations)
{
    qd_parsed_field_t *props = NULL;
    qd_parsed_field_t *bod = NULL;

    qcm_address_lookup_status_t rc = QCM_ADDR_LOOKUP_OK;
    *is_link_route = false;
    *has_destinations = false;

    props = qd_parse(properties);
    if (!props || !qd_parse_ok(props) || !qd_parse_is_map(props)) {
        rc = QCM_ADDR_LOOKUP_INVALID_REQUEST;
        goto exit;
    }

    bod = qd_parse(body);
    if (!bod || !qd_parse_ok(bod) || !qd_parse_is_list(bod)) {
        rc = QCM_ADDR_LOOKUP_INVALID_REQUEST;
        goto exit;
    }

    qd_parsed_field_t *tmp = qd_parse_value_by_key(props, "status");
    if (!tmp || !qd_parse_is_scalar(tmp)) {
        rc = QCM_ADDR_LOOKUP_INVALID_REQUEST;
        goto exit;
    } else {
        int32_t status = qd_parse_as_int(tmp);
        if (status != QCM_ADDR_LOOKUP_OK) {
            rc = (qcm_address_lookup_status_t) status;
            goto exit;
        }
    }

    // bod[0] == is_link_route (bool)
    // bod[1] == has_destinations (bool)

    if (qd_parse_sub_count(bod) < 2) {
        rc = QCM_ADDR_LOOKUP_INVALID_REQUEST;
        goto exit;
    }

    *is_link_route = qd_parse_as_bool(qd_parse_sub_value(bod, 0));
    *has_destinations = qd_parse_as_bool(qd_parse_sub_value(bod, 1));

exit:
    qd_parse_free(props);
    qd_parse_free(bod);
    return rc;
}

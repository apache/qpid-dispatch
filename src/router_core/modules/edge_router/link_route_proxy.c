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

#include "link_route_proxy.h"

#include "agent_conn_link_route.h"
#include "edge_mgmt.h"

#include <inttypes.h>
#include <stdio.h>

// Track the state of the link route configuration proxy on
// the uplinked interior router
//

typedef enum {
    QDR_LINK_ROUTE_PROXY_NEW = 0,   // needs to create proxy
    QDR_LINK_ROUTE_PROXY_CREATING,  // create request sent
    QDR_LINK_ROUTE_PROXY_CREATED,   // create request completed ok
    QDR_LINK_ROUTE_PROXY_CANCELLED, // deleted while waiting for create request reply
    QDR_LINK_ROUTE_PROXY_DELETED,   // needs to delete proxy
    QDR_LINK_ROUTE_PROXY_DELETING,  // delete request sent
} link_route_proxy_state_t;

typedef struct link_route_proxy_t link_route_proxy_t;
struct link_route_proxy_t {
    DEQ_LINKS(link_route_proxy_t);
    char                     *proxy_name;
    char                     *proxy_id;
    char                     *address;
    link_route_proxy_state_t  proxy_state;
    qd_direction_t            direction;
};
ALLOC_DECLARE(link_route_proxy_t);
ALLOC_DEFINE(link_route_proxy_t);
DEQ_DECLARE(link_route_proxy_t, link_route_proxy_list_t);


static link_route_proxy_list_t    _link_route_proxies;
static qdrc_event_subscription_t *_event_handle;
static int   _available_credit;


static uint64_t _on_create_reply_CT(qdr_core_t *, void *, int32_t, const char *, qd_iterator_t *);
static uint64_t _on_delete_reply_CT(qdr_core_t *, void *, int32_t, const char *, qd_iterator_t *);
static void     _on_create_error_CT(qdr_core_t *, void *, const char *);
static void     _on_delete_error_CT(qdr_core_t *, void *, const char *);


static void _free_link_route_proxy(link_route_proxy_t *lrp)
{
    if (!lrp)
        return;
    free(lrp->proxy_name);
    free(lrp->proxy_id);
    free(lrp->address);
    free_link_route_proxy_t(lrp);
}


// clean up the entire proxy list
static void _free_all_link_route_proxies(void)
{
    link_route_proxy_t *lrp = DEQ_HEAD(_link_route_proxies);
    while (lrp) {
        DEQ_REMOVE_HEAD(_link_route_proxies);
        _free_link_route_proxy(lrp);
        lrp = DEQ_HEAD(_link_route_proxies);
    }
}


// generate the body for a management CREATE message for a Connection Scoped
// Link Route
static qd_composed_field_t  *_create_body(link_route_proxy_t *lrp)
{
    qd_composed_field_t *body = qd_compose(QD_PERFORMATIVE_BODY_AMQP_VALUE, 0);
    qd_compose_start_map(body);

    qd_compose_insert_string(body, qdr_conn_link_route_columns[QDR_CONN_LINK_ROUTE_TYPE]);
    qd_compose_insert_string(body, CONN_LINK_ROUTE_TYPE);

    qd_compose_insert_string(body, qdr_conn_link_route_columns[QDR_CONN_LINK_ROUTE_PATTERN]);
    qd_compose_insert_string(body, lrp->address);

    qd_compose_insert_string(body, qdr_conn_link_route_columns[QDR_CONN_LINK_ROUTE_DIRECTION]);
    qd_compose_insert_string(body, lrp->direction == QD_INCOMING ? "in" : "out");

    qd_compose_insert_string(body, qdr_conn_link_route_columns[QDR_CONN_LINK_ROUTE_NAME]);
    qd_compose_insert_string(body, lrp->proxy_name);

    qd_compose_end_map(body);
    return body;
}


// check for any link route configuration entities that need to be
// synchronized with the peer interior router
//
static void _sync_interior_proxies(qdr_core_t *core)
{
    link_route_proxy_t *lrp = DEQ_HEAD(_link_route_proxies);
    while (lrp && _available_credit > 0) {

        if (lrp->proxy_state == QDR_LINK_ROUTE_PROXY_NEW) {

            qd_log(core->log, QD_LOG_TRACE,
                   "Creating proxy link route for address=%s named=%s",
                   lrp->address, lrp->proxy_name);

            lrp->proxy_state = QDR_LINK_ROUTE_PROXY_CREATING;
            qcm_edge_mgmt_request_CT(core,
                                     lrp, // context
                                     "CREATE",
                                     CONN_LINK_ROUTE_TYPE,
                                     0,  // id
                                     lrp->proxy_name,
                                     _create_body(lrp),
                                     10,  // timeout
                                     _on_create_reply_CT,
                                     _on_create_error_CT);
            _available_credit -= 1;

        } else if (lrp->proxy_state == QDR_LINK_ROUTE_PROXY_DELETED) {

            qd_log(core->log, QD_LOG_TRACE,
                   "Deleting proxy link route address=%s proxy-id=%s name=%s",
                   lrp->address, lrp->proxy_id, lrp->proxy_name);

            lrp->proxy_state = QDR_LINK_ROUTE_PROXY_DELETING;

            // empty body for delete
            qd_composed_field_t *body = qd_compose(QD_PERFORMATIVE_BODY_AMQP_VALUE, 0);
            qd_compose_start_map(body);
            qd_compose_end_map(body);

            qcm_edge_mgmt_request_CT(core,
                                     lrp, // context
                                     "DELETE",
                                     CONN_LINK_ROUTE_TYPE,
                                     lrp->proxy_id,
                                     lrp->proxy_name,
                                     body,
                                     10,  // timeout
                                     _on_delete_reply_CT,
                                     _on_delete_error_CT);
            _available_credit -= 1;
        }
        lrp = DEQ_NEXT(lrp);
    }
}


// handle the response to our create request message
static uint64_t _on_create_reply_CT(qdr_core_t    *core,
                                    void          *request_context,
                                    int32_t        statusCode,
                                    const char    *statusDescription,
                                    qd_iterator_t *body)
{
    link_route_proxy_t *lrp = (link_route_proxy_t *)request_context;
    uint64_t disposition = PN_ACCEPTED;

    if (statusCode == 201) {  // Created
        qd_parsed_field_t *parsed_body = qd_parse(body);
        qd_parsed_field_t *proxy_id = qd_parse_value_by_key(parsed_body,
                                                            "identity");
        if (!proxy_id) {
            // really should not happen (a bug)
            qd_log(core->log, QD_LOG_ERROR,
                   "Link route proxy CREATE failed: invalid response message,"
                   " address=%s proxy name=%s",
                   lrp->address, lrp->proxy_name);
            DEQ_REMOVE(_link_route_proxies, lrp);
            _free_link_route_proxy(lrp);
            disposition = PN_REJECTED;
        } else {
            lrp->proxy_id = (char *)qd_iterator_copy(qd_parse_raw(proxy_id));
            qd_log(core->log, QD_LOG_TRACE,
                   "link route proxy CREATE successful, address=%s peer-id=%s proxy name=%s)",
                   lrp->address, lrp->proxy_id, lrp->proxy_name);
            switch (lrp->proxy_state) {
            case QDR_LINK_ROUTE_PROXY_CREATING:
                lrp->proxy_state = QDR_LINK_ROUTE_PROXY_CREATED;
                break;
            case QDR_LINK_ROUTE_PROXY_CANCELLED:
                // the address was removed while waiting for the create
                // to complete.  Now forward the delete along to interior
                lrp->proxy_state = QDR_LINK_ROUTE_PROXY_DELETED;
                _sync_interior_proxies(core);
                break;
            default:
                assert(false);
            }
        }
        qd_parse_free(parsed_body);
    } else {
        // crap.  This is unexpected.  Perhaps a duplication?
        // only way to be sure is to now query using the proxy
        // name, which makes things complicated...
        qd_log(core->log, QD_LOG_ERROR,
               "link route proxy CREATE failed with error: (%"PRId32") %s,"
               " address=%s proxy_name=%s)",
               statusCode,
               statusDescription ? statusDescription : "unknown",
               lrp->address, lrp->proxy_name);
        // @TODO(kgiusti) - reset the connection
        DEQ_REMOVE(_link_route_proxies, lrp);
        _free_link_route_proxy(lrp);
    }

    qd_iterator_free(body);
    return disposition;
}


// create request failed to reach interior (or was rejected, released, etc)
static void _on_create_error_CT(qdr_core_t *core,
                                void       *request_context,
                                const char *error)
{
    link_route_proxy_t *lrp = (link_route_proxy_t *)request_context;

    // likely the link detached or conn coming down - preserve
    // the proxy and try again later
    qd_log(core->log, QD_LOG_DEBUG,
           "link route proxy CREATE failed: %s, address=%s name=%s",
           error ? error : "unknown",
           lrp->address, lrp->proxy_name);
    lrp->proxy_state = QDR_LINK_ROUTE_PROXY_NEW;
}


// handle the response to our delete request message
static uint64_t _on_delete_reply_CT(qdr_core_t    *core,
                                    void          *request_context,
                                    int32_t        statusCode,
                                    const char    *statusDescription,
                                    qd_iterator_t *body)
{
    link_route_proxy_t *lrp = (link_route_proxy_t *)request_context;

    qd_iterator_free(body);  // body is ignored

    switch (statusCode) {
    case 204:
    case 404:
        // consider No Content or Not Found as success
        qd_log(core->log, QD_LOG_TRACE,
               "link route proxy DELETE successful,"
               " address=%s proxy_id=%s proxy_name=%s (code=%d)",
               lrp->address, lrp->proxy_id, lrp->proxy_name,
               statusCode);
        break;
    default:
        // oh crap, this is unexpected and is probably a bug
        qd_log(core->log, QD_LOG_ERROR,
               "link route proxy DELETE failed with error: (%"PRId32") %s,"
               " address=%s proxy id=%s proxy name=%s)",
               statusCode,
               statusDescription ? statusDescription : "unknown",
               lrp->address, lrp->proxy_id, lrp->proxy_name);
    }
    DEQ_REMOVE(_link_route_proxies, lrp);
    _free_link_route_proxy(lrp);
    return PN_ACCEPTED;
}


// delete request failed to reach interior (or was rejected, released, etc)
static void _on_delete_error_CT(qdr_core_t *core,
                                void       *request_context,
                                const char *error)
{
    link_route_proxy_t *lrp = (link_route_proxy_t *)request_context;

    // likely the link detached or conn coming down - preserve
    // the proxy and try again later
    qd_log(core->log, QD_LOG_DEBUG,
           "link route proxy DELETE failed: %s, address=%s name=%s",
           error ? error : "unknown",
           lrp->address, lrp->proxy_name);
    lrp->proxy_state = QDR_LINK_ROUTE_PROXY_DELETED;
}


// called when a new link route is configured.  Create a proxy
// for the link route on the interior router
//
static void _link_route_added_CT(qdr_core_t *core, qdr_address_t *addr)
{
    const char *address = (const char *)qd_hash_key_by_handle(addr->hash_handle);
    qd_log(core->log, QD_LOG_TRACE,
           "edge creating proxy link route for '%s'", address);

    link_route_proxy_t *lrp = new_link_route_proxy_t();
    ZERO(lrp);

    if (QDR_IS_LINK_ROUTE_PREFIX(address[0])) {
        // connection scoped link routes only support patterns (since prefix is
        // a type of pattern).  Prefix address strings do not have the trailing
        // '#' in the address as it is inferred by the type.  So convert the
        // prefix address to an address pattern
        char *buf = malloc(strlen(address) + 2);  // skip prefix, add /#
        strcpy(buf, &address[1]);
        strcat(buf, "/#");
        lrp->address = buf;
    } else {  // already in pattern form
        lrp->address = strdup(&address[1]);  // skip prefix
    }
    lrp->proxy_state = QDR_LINK_ROUTE_PROXY_NEW;
    lrp->direction = QDR_LINK_ROUTE_DIR(address[0]);

    // construct a name for the proxy link route in the format of
    // <router-id>/proxyLinkRoute/<address>
    lrp->proxy_name = malloc(strlen(core->router_id)
                             + 16
                             + strlen(address) + 1);  // 16 == len("/proxyLinkRoute/")
    sprintf(lrp->proxy_name, "%s/proxyLinkRoute/%s", core->router_id, address);

    DEQ_INSERT_TAIL(_link_route_proxies, lrp);
}


// called by route control when an existing link route config entity is deleted
static void _link_route_deleted_CT(qdr_core_t *core, qdr_address_t *addr)
{
    const char *address = (const char *)qd_hash_key_by_handle(addr->hash_handle);
    qd_log(core->log, QD_LOG_TRACE,
           "edge deleting proxy link route for '%s'", address);

    size_t len = strlen(&address[1]);  // skip prefix
    if (QDR_IS_LINK_ROUTE_PREFIX(address[0])) {
        // see above comment re: prefix handling.  Need to ignore the trailing \#
        assert(len > 2);
        len -= 2;
    }

    qd_direction_t dir = QDR_LINK_ROUTE_DIR(address[0]);
    link_route_proxy_t *lrp = DEQ_HEAD(_link_route_proxies);
    DEQ_FIND(lrp, dir == lrp->direction && strncmp(lrp->address, &address[1], len) == 0);
    if (lrp) {
        switch (lrp->proxy_state) {
        case QDR_LINK_ROUTE_PROXY_NEW:
            // never created - no need to send a delete request
            DEQ_REMOVE(_link_route_proxies, lrp);
            _free_link_route_proxy(lrp);
            break;
        case QDR_LINK_ROUTE_PROXY_CREATED:
            lrp->proxy_state = QDR_LINK_ROUTE_PROXY_DELETED;
            break;
        case QDR_LINK_ROUTE_PROXY_CREATING:
            // uh oh - deleted before our outstanding create completed
            lrp->proxy_state = QDR_LINK_ROUTE_PROXY_CANCELLED;
            break;
        default:
            // close in process (connection dropped while link closing)
            break;
        }
    }
}

static void _on_conn_event(void             *context,
                           qdrc_event_t      event_type,
                           qdr_connection_t *conn)
{
    // we only receive edge loss events
    assert(event_type == QDRC_EVENT_CONN_EDGE_LOST);

    // the interior should purge all of the proxies since they are connection
    // scoped. Reset the proxy state to NEW or remove the proxy if deleted
    link_route_proxy_t *lrp = DEQ_HEAD(_link_route_proxies);
    while (lrp) {
        link_route_proxy_t *next = DEQ_NEXT(lrp);
        switch (lrp->proxy_state) {
        case QDR_LINK_ROUTE_PROXY_CREATING:
        case QDR_LINK_ROUTE_PROXY_CREATED:
            lrp->proxy_state = QDR_LINK_ROUTE_PROXY_NEW;
            free(lrp->proxy_id);
            lrp->proxy_id = NULL;
            break;
        case QDR_LINK_ROUTE_PROXY_CANCELLED:
        case QDR_LINK_ROUTE_PROXY_DELETED:
        case QDR_LINK_ROUTE_PROXY_DELETING:
            DEQ_REMOVE(_link_route_proxies, lrp);
            _free_link_route_proxy(lrp);
            break;
        default:
            break;
        }
        lrp = next;
    }
}

static void _on_addr_event(void          *context,
                           qdrc_event_t   event_type,
                           qdr_address_t *addr)
{
    qdr_core_t *core = (qdr_core_t *)context;
    const char *address = (const char *)qd_hash_key_by_handle(addr->hash_handle);

    if (!QDR_IS_LINK_ROUTE(*address))
        return;

    switch (event_type) {
    case QDRC_EVENT_ADDR_BECAME_LOCAL_DEST:
        _link_route_added_CT(core, addr);
        break;
    case QDRC_EVENT_ADDR_NO_LONGER_LOCAL_DEST:
        _link_route_deleted_CT(core, addr);
        break;
    }
    _sync_interior_proxies(core);
}

//
// Public API
//

// called by edge mgmt API when link(s) detach
void qcm_edge_link_route_proxy_state_CT(qdr_core_t *core, bool active)
{
    if (!active)
        _available_credit = 0;  // stop sending pending syncs
    else if (_available_credit > 0)
        _sync_interior_proxies(core);
}


// called by the edge mgmt API when credit has been granted:
void qcm_edge_link_route_proxy_flow_CT(qdr_core_t *core, int available_credit, bool drain)
{
    _available_credit += available_credit;
    _sync_interior_proxies(core);
    if (drain) {
        _available_credit = 0;
    }
}

// called by the edge router module init method:
void qcm_edge_link_route_init_CT(qdr_core_t *core)
{
    // need to know when the connection to the interior
    // fails so we can flush state
    _event_handle = qdrc_event_subscribe_CT(core,
                                            (QDRC_EVENT_CONN_EDGE_LOST
                                             | QDRC_EVENT_ADDR_NO_LONGER_LOCAL_DEST
                                             | QDRC_EVENT_ADDR_BECAME_LOCAL_DEST),
                                            _on_conn_event,
                                            0,
                                            _on_addr_event,
                                            0,
                                            core);
}


// called by the edge router module final method:
void qcm_edge_link_route_final_CT(qdr_core_t *core)
{
    qdrc_event_unsubscribe_CT(core, _event_handle);
    _free_all_link_route_proxies();
}



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

#include "edge_mgmt.h"

#include "core_client_api.h"
#include "link_route_proxy.h"

#include <errno.h>
#include <inttypes.h>

/*
 * an API that lets the core issue management requests
 */

static qdrc_client_t *_client;              // req/resp client
static qdrc_event_subscription_t *_event_handle;  // edge conn up/down


static void _mgmt_on_state_cb_CT(qdr_core_t *, qdrc_client_t *, void *, bool);
static void _mgmt_on_flow_cb_CT(qdr_core_t *, qdrc_client_t *, void *, int, bool);


//
// edge uplink connection event handler
//
static void _conn_event_CT(void *context, qdrc_event_t event_type, qdr_connection_t *conn)
{
    qdr_core_t *core = (qdr_core_t *) context;

    switch (event_type) {
    case QDRC_EVENT_CONN_EDGE_ESTABLISHED:
        // create a messaging client to the interior router's $management
        //
        qd_log(core->log, QD_LOG_TRACE,
               "starting edge mgmt client (id=%"PRIu64")", conn->identity);
        qdr_terminus_t *target = qdr_terminus(0);
        qdr_terminus_set_address(target, "$management");
        _client = qdrc_client_CT(core,
                                 conn,
                                 target,
                                 100,  // credit
                                 0, // user_context
                                 _mgmt_on_state_cb_CT,
                                 _mgmt_on_flow_cb_CT);
        if (!_client) {
            qd_log(core->log, QD_LOG_ERROR,
                   "Failed to start edge management client");
        }
        break;

    case QDRC_EVENT_CONN_EDGE_LOST:
        // clean up messaging client
        //
        qd_log(core->log, QD_LOG_TRACE,
               "stopping edge mgmt client (id=%"PRIu64")", conn->identity);
        qdrc_client_free_CT(_client);
        _client = NULL;
        break;
    }
}


// Per anagement request context
//
typedef struct qcm_edge_mgmt_request_t qcm_edge_mgmt_request_t;
struct qcm_edge_mgmt_request_t {
    void                     *req_context;
    qcm_edge_mgmt_reply_CT_t  reply_callback;
    qcm_edge_mgmt_error_CT_t  error_callback;
};
ALLOC_DECLARE(qcm_edge_mgmt_request_t);
ALLOC_DEFINE(qcm_edge_mgmt_request_t);


// utility to parse out status code from management response message
static int _extract_mgmt_status(qdr_core_t    *core,
                                qd_iterator_t *app_properties,
                                int32_t       *statusCode,
                                char          **statusDescription)
{
    int rc = 0;
    *statusDescription = NULL;
    *statusCode = 500;

    qd_parsed_field_t *properties = qd_parse(app_properties);
    if (!properties || !qd_parse_ok(properties) || !qd_parse_is_map(properties)) {
        qd_log(core->log, QD_LOG_ERROR, "bad edge management reply msg - invalid properties field");
        rc = EINVAL;
        goto exit;
    }
    qd_parsed_field_t *status_fld = qd_parse_value_by_key(properties, "statusCode");
    if (!status_fld) {
        qd_log(core->log, QD_LOG_ERROR, "bad edge management reply msg - statusCode field missing");
        rc = EINVAL;
        goto exit;
    }
    *statusCode = qd_parse_as_int(status_fld);
    if (!qd_parse_ok(status_fld)) {
        qd_log(core->log, QD_LOG_ERROR, "bad edge management reply msg - statusCode field invalid");
        rc = EINVAL;
        goto exit;
    }
    qd_parsed_field_t *desc_fld = qd_parse_value_by_key(properties, "statusDescription");
    if (desc_fld) {  // it's optional, so no error if unset
        qd_iterator_t *tmp = qd_parse_raw(desc_fld);
        *statusDescription = (char *)qd_iterator_copy(tmp);
    }

exit:
    if (properties)
        qd_parse_free(properties);
    return rc;
}


// mgmt client link state changed
static void _mgmt_on_state_cb_CT(qdr_core_t    *core,
                                 qdrc_client_t *client,
                                 void          *user_context,
                                 bool           active)
{
    qd_log(core->log, QD_LOG_TRACE,
           "edge mgmt client state change: uc=%p %s",
           user_context,
           (active) ? "active" : "down");

    qcm_edge_link_route_proxy_state_CT(core, active);
}


// mgmt client credit granted by interior router
static void _mgmt_on_flow_cb_CT(qdr_core_t    *core,
                                qdrc_client_t *client,
                                void          *user_context,
                                int            more_credit,
                                bool           drain)
{
    qd_log(core->log, QD_LOG_TRACE,
           "edge mgmt client flow: uc=%p c=%d d=%s",
           user_context, more_credit,
           (drain) ? "T" : "F");

    qcm_edge_link_route_proxy_flow_CT(core,
                                      more_credit,
                                      drain);
}


// terminal disposition set on sent request
static void _mgmt_on_ack_cb_CT(qdr_core_t    *core,
                               qdrc_client_t *client,
                               void          *user_context,
                               void          *request_context,
                               uint64_t       disposition)
{
    qcm_edge_mgmt_request_t *req = (qcm_edge_mgmt_request_t *)request_context;

    qd_log(core->log, QD_LOG_TRACE,
           "edge mgmt request update: rc=%p d=0x%"PRIx64,
           req->req_context, disposition);

    if (disposition != PN_ACCEPTED) {
        // failure - no reply will be sent to cleanup
        if (req->error_callback) {
            req->error_callback(core, req->req_context, "Request not accepted");
            req->error_callback = NULL;  // avoid recalling on mgmt done
        }
    }
}


// reply message received
static uint64_t _mgmt_on_reply_cb_CT(qdr_core_t    *core,
                                     qdrc_client_t *client,
                                     void          *user_context,
                                     void          *request_context,
                                     qd_iterator_t *app_properties,
                                     qd_iterator_t *body)
{
    int32_t statusCode = 0;
    char *statusDescription = 0;
    uint64_t disposition = PN_ACCEPTED;

    qcm_edge_mgmt_request_t *req = (qcm_edge_mgmt_request_t *)request_context;

    if (_extract_mgmt_status(core, app_properties, &statusCode, &statusDescription)) {
        // error - bad response
        statusCode = 500;
    }
    qd_iterator_free(app_properties);

    qd_log(core->log, QD_LOG_TRACE,
           "Edge management request reply:"
           " rc=%p status=%"PRId32": %s",
           req->req_context, statusCode,
           (statusDescription) ? statusDescription : "<no description>");

    if (req->reply_callback)
        disposition = req->reply_callback(core,
                                          req->req_context,
                                          statusCode,
                                          statusDescription,
                                          body);
    else
        qd_iterator_free(body);
    
    free(statusDescription);
    return disposition;
}


// request completed or aborted due to error
static void _mgmt_on_done_cb_CT(qdr_core_t    *core,
                                qdrc_client_t *client,
                                void          *user_context,
                                void          *request_context,
                                const char    *error)
{
    qcm_edge_mgmt_request_t *req = (qcm_edge_mgmt_request_t *)request_context;
    qd_log(core->log, QD_LOG_TRACE,
           "edge mgmt request done: uc=%p rc=%p %s",
           user_context, request_context, error ? error : "");

    if (error && req->error_callback)
        req->error_callback(core, req->req_context, error);

    free_qcm_edge_mgmt_request_t(req);
}


// send management request - takes ownership of body
int qcm_edge_mgmt_request_CT(qdr_core_t           *core,
                             void                 *request_context,
                             const char           *operation,
                             const char           *entity_type,
                             const char           *identity,
                             const char           *name,
                             qd_composed_field_t  *body,
                             uint32_t                 timeout,
                             qcm_edge_mgmt_reply_CT_t reply_cb,
                             qcm_edge_mgmt_error_CT_t error_cb)
{

    qd_log(core->log, QD_LOG_TRACE,
           "New Edge management request: rc=%p %s type=%s id=%s",
           request_context, operation, entity_type,
           (identity) ? identity : "<unset>");

    qcm_edge_mgmt_request_t *req = new_qcm_edge_mgmt_request_t();
    ZERO(req);
    req->req_context = request_context;
    req->reply_callback = reply_cb;
    req->error_callback = error_cb;

    // create a message containing a management request

    qd_composed_field_t *ap_fld = qd_compose(QD_PERFORMATIVE_APPLICATION_PROPERTIES, 0);
    qd_compose_start_map(ap_fld);
    qd_compose_insert_string(ap_fld, "operation");
    qd_compose_insert_string(ap_fld, operation);
    qd_compose_insert_string(ap_fld, "type");
    qd_compose_insert_string(ap_fld, entity_type);
    if (identity) {
        qd_compose_insert_string(ap_fld, "identity");
        qd_compose_insert_string(ap_fld, identity);
    }
    // note: if there is a name specified qdrouterd expects to find it in the
    // *properties* header, which doesn't jive with the current mgmt spec
    // (WD12)
    if (name) {
        qd_compose_insert_string(ap_fld, "name");
        qd_compose_insert_string(ap_fld, name);
    }
    qd_compose_end_map(ap_fld);

    return qdrc_client_request_CT(_client,
                                  req,   // request context
                                  ap_fld,
                                  body,
                                  timeout,
                                  _mgmt_on_reply_cb_CT,
                                  _mgmt_on_ack_cb_CT,
                                  _mgmt_on_done_cb_CT);
}


void qcm_edge_mgmt_init_CT(qdr_core_t *core)
{
    _event_handle = qdrc_event_subscribe_CT(core,
                                            (QDRC_EVENT_CONN_EDGE_ESTABLISHED
                                             | QDRC_EVENT_CONN_EDGE_LOST),
                                            _conn_event_CT,
                                            0,     // link event
                                            0,     // addr event
                                            0,     // router event
                                            core); // context
}


void qcm_edge_mgmt_final_CT(qdr_core_t *core)
{
    qdrc_event_unsubscribe_CT(core, _event_handle);
    qdrc_client_free_CT(_client);
    _client = NULL;
}

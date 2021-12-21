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

#include "core_client_api.h"

#include "core_link_endpoint.h"
#include "delivery.h"

#include <inttypes.h>
#include <stdio.h>
#include <time.h>

#define CORRELATION_ID_LEN 32
const char *CORRELATION_ID_FMT = "client-%016"PRIx64"%08"PRIx32;

typedef struct qdrc_client_request_t qdrc_client_request_t;
struct qdrc_client_request_t {
    DEQ_LINKS_N(SEND_Q, qdrc_client_request_t);
    DEQ_LINKS_N(UNSETTLED, qdrc_client_request_t);
    DEQ_LINKS_N(REPLY, qdrc_client_request_t);

    qdrc_client_t      *client;
    void               *req_context;

    char                 correlation_id[CORRELATION_ID_LEN];
    qd_iterator_t       *correlation_key;
    qd_hash_handle_t    *hash_handle;
    qdr_delivery_t      *delivery;
    qdr_core_timer_t    *timer;

    qd_composed_field_t *app_properties;
    qd_composed_field_t *body;

    bool                 on_send_queue;      // to be sent
    bool                 on_unsettled_list;  // awaiting disposition
    bool                 on_reply_list;      // awaiting reply message

    qdrc_client_on_reply_CT_t        on_reply_cb;
    qdrc_client_on_ack_CT_t          on_ack_cb;
    qdrc_client_request_done_CT_t    done_cb;
};
DEQ_DECLARE(qdrc_client_request_t, qdrc_client_request_list_t);
ALLOC_DECLARE(qdrc_client_request_t);
ALLOC_DEFINE(qdrc_client_request_t);


struct qdrc_client_t {
    qdr_core_t                 *core;
    qd_hash_t                  *correlations;

    qdrc_endpoint_t            *sender;  // for outgoing management request messages
    qdrc_endpoint_t            *receiver;  // for incoming management reply messages
    bool                        sender_up;
    bool                        receiver_up;
    bool                        active;
    char                       *reply_to;

    qdrc_client_request_list_t  send_queue;
    qdrc_client_request_list_t  unsettled_list;
    qdrc_client_request_list_t  reply_list;  // for expected reply

    uint32_t                    next_cid;   // correlation id generation
    uint32_t                    rx_credit_window;  // initial credit grant
    int                         tx_credit;

    void                       *user_context;
    qdrc_client_on_state_CT_t   on_state_cb;
    qdrc_client_on_flow_CT_t    on_flow_cb;

};
ALLOC_DECLARE(qdrc_client_t);
ALLOC_DEFINE(qdrc_client_t);


static void _send_request_CT(qdrc_client_t *client,
                             qdrc_client_request_t *req);
static void _flush_send_queue_CT(qdrc_client_t *client);

static void _state_updated_CT(qdrc_client_t *client);

static void _sender_second_attach_CT(void *client_context,
                                     qdr_terminus_t *remote_source,
                                     qdr_terminus_t *remote_target);
static void _receiver_second_attach_CT(void *client_context,
                                       qdr_terminus_t *remote_source,
                                       qdr_terminus_t *remote_target);
static void _sender_flow_CT(void *client_context,
                            int available_credit,
                            bool drain);
static void _sender_update_CT(void *client_context,
                              qdr_delivery_t *delivery,
                              bool settled,
                              uint64_t disposition);
static void _receiver_transfer_CT(void *client_context,
                                  qdr_delivery_t *delivery,
                                  qd_message_t *message);
static void _sender_detached_CT(void *client_context,
                                qdr_error_t *error);
static void _receiver_detached_CT(void *client_context,
                                  qdr_error_t *error);
static void _sender_cleanup_CT(void *client_context);
static void _receiver_cleanup_CT(void *client_context);

static void _free_request_CT(qdrc_client_t *client,
                             qdrc_client_request_t *req,
                             const char *error);
static qd_message_t *_create_message_CT(qdrc_client_t *client,
                                        qdrc_client_request_t *req);
static void _timer_expired(qdr_core_t *core, void *context);


static qdrc_endpoint_desc_t sender_endpoint = {
    .label            = "core client - sender",
    .on_second_attach = _sender_second_attach_CT,
    .on_flow          = _sender_flow_CT,
    .on_update        = _sender_update_CT,
    .on_first_detach  = _sender_detached_CT,
    .on_cleanup       = _sender_cleanup_CT
};

static qdrc_endpoint_desc_t receiver_endpoint = {
    .label            = "core client - receiver",
    .on_second_attach = _receiver_second_attach_CT,
    .on_transfer      = _receiver_transfer_CT,
    .on_first_detach  = _receiver_detached_CT,
    .on_cleanup       = _receiver_cleanup_CT
};

qdrc_client_t *qdrc_client_CT(qdr_core_t *core,
                              qdr_connection_t *conn,
                              qdr_terminus_t *target,
                              uint32_t credit_window,
                              void *user_context,
                              qdrc_client_on_state_CT_t on_state_cb,
                              qdrc_client_on_flow_CT_t on_flow_cb)
{
    qdrc_client_t *client = new_qdrc_client_t();
    if (!client)
        return NULL;

    ZERO(client);
    client->core = core;
    client->correlations = qd_hash(6, 4, 0);
    client->next_cid = rand();
    client->rx_credit_window = credit_window;
    client->user_context = user_context;
    client->on_state_cb = on_state_cb;
    client->on_flow_cb = on_flow_cb;

    // create links
    client->sender = qdrc_endpoint_create_link_CT(core,
                                                  conn,
                                                  QD_OUTGOING,
                                                  NULL, // source terminus
                                                  target,
                                                  &sender_endpoint,
                                                  client);
    // create receiver link for replies from interior management
    qdr_terminus_t *source = qdr_terminus(0);
    source->dynamic = true;
    client->receiver = qdrc_endpoint_create_link_CT(core,
                                                    conn,
                                                    QD_INCOMING,
                                                    source,
                                                    NULL,   // target terminus
                                                    &receiver_endpoint,
                                                    client);
    qd_log(core->log, QD_LOG_TRACE,
           "New core client created c=%p", client);
    return client;
}


void qdrc_client_free_CT(qdrc_client_t *client)
{
    if (!client)
        return;

    if (client->sender) {
        client->sender = NULL;
    }

    if (client->receiver) {
        client->receiver = NULL;
    }

    qdrc_client_request_t *req = DEQ_HEAD(client->send_queue);
    while (req) {
        _free_request_CT(client, req, NULL);  // removes from send_queue
        req = DEQ_HEAD(client->send_queue);
    }

    req = DEQ_HEAD(client->unsettled_list);
    while (req) {
        _free_request_CT(client, req, NULL);  // removes from unsettled_list
        req = DEQ_HEAD(client->unsettled_list);
    }

    req = DEQ_HEAD(client->reply_list);
    while (req) {
        _free_request_CT(client, req, NULL);  // removes from reply_list
        req = DEQ_HEAD(client->reply_list);
    }

    qd_hash_free(client->correlations);
    free(client->reply_to);

    qd_log(client->core->log, QD_LOG_TRACE,
           "Core client freed c=%p", client);

    free_qdrc_client_t(client);
}


// send a message
int qdrc_client_request_CT(qdrc_client_t                 *client,
                           void                          *request_context,
                           qd_composed_field_t           *app_properties,
                           qd_composed_field_t           *body,
                           uint32_t                       timeout,
                           qdrc_client_on_reply_CT_t      on_reply_cb,
                           qdrc_client_on_ack_CT_t        on_ack_cb,
                           qdrc_client_request_done_CT_t  done_cb)
{
    qd_log(client->core->log, QD_LOG_TRACE,
           "New core client request created c=%p, rc=%p",
           client, request_context);

    qdrc_client_request_t *req = new_qdrc_client_request_t();
    ZERO(req);
    req->client         = client;
    req->req_context    = request_context;
    req->app_properties = app_properties;
    req->body           = body;
    req->on_reply_cb    = on_reply_cb;
    req->on_ack_cb      = on_ack_cb;
    req->done_cb        = done_cb;
    if (timeout) {
        req->timer = qdr_core_timer_CT(client->core, _timer_expired, req);
        qdr_core_timer_schedule_CT(client->core, req->timer, timeout);
    }

    _send_request_CT(client, req);
    return 0;
}


// attempt to send a new request message
static void _send_request_CT(qdrc_client_t *client,
                             qdrc_client_request_t *req)
{
    DEQ_INSERT_TAIL_N(SEND_Q, client->send_queue, req);
    req->on_send_queue = true;
    _flush_send_queue_CT(client);
}


// send any pending messages on the send_queue
static void _flush_send_queue_CT(qdrc_client_t *client)
{
    qdrc_client_request_t *req = DEQ_HEAD(client->send_queue);

    while (req && client->tx_credit > 0) {
        bool presettled = (req->on_ack_cb == NULL);

        if (req->on_reply_cb && !client->reply_to) {
            // cannot send until receiver comes up
            break;
        }

        qd_message_t *msg = _create_message_CT(client, req);
        req->delivery = qdrc_endpoint_delivery_CT(client->core,
                                                  client->sender,
                                                  msg);
        qdr_delivery_incref(req->delivery, "core client send request");
        qdrc_endpoint_send_CT(client->core,
                              client->sender,
                              req->delivery,
                              presettled);
        DEQ_REMOVE_HEAD_N(SEND_Q, client->send_queue);
        req->on_send_queue = false;

        qd_log(client->core->log, QD_LOG_TRACE,
               "Core client request sent c=%p, rc=%p dlv=%p cid=%s",
               client, req->req_context, req->delivery,
               *req->correlation_id ? req->correlation_id : "<none>");

        if (!presettled && req->on_ack_cb) {
            DEQ_INSERT_TAIL_N(UNSETTLED, client->unsettled_list, req);
            req->on_unsettled_list = true;
        }
        if (req->on_reply_cb) {
            DEQ_INSERT_TAIL_N(REPLY, client->reply_list, req);
            req->on_reply_list = true;
        }
        if (!req->on_reply_list && !req->on_unsettled_list) {
            // "Fire and forget" no need to keep the request any longer
            _free_request_CT(client, req, NULL);
        }
        client->tx_credit -= 1;
        req = DEQ_HEAD(client->send_queue);
    }
}


static void _free_request_CT(qdrc_client_t *client,
                             qdrc_client_request_t *req,
                             const char *error)
{
    if (req->timer) {
        qdr_core_timer_free_CT(client->core, req->timer);
    }
    if (req->on_send_queue)
        DEQ_REMOVE_N(SEND_Q, client->send_queue, req);
    if (req->on_unsettled_list)
        DEQ_REMOVE_N(UNSETTLED, client->unsettled_list, req);
    if (req->on_reply_list)
        DEQ_REMOVE_N(REPLY, client->reply_list, req);

    if (req->hash_handle) {
        qd_hash_remove_by_handle(client->correlations, req->hash_handle);
        qd_hash_handle_free(req->hash_handle);
    }

    if (req->correlation_key) {
        qd_iterator_free(req->correlation_key);
    }

    if (req->body) {
        qd_compose_free(req->body);
    }

    if (req->app_properties) {
        qd_compose_free(req->app_properties);
    }

    if (req->delivery) {
        qdr_delivery_decref_CT(client->core, req->delivery, "core client send request");
    }

    // notify user that the request has completed
    if (req->done_cb) {
        req->done_cb(client->core,
                     client,
                     client->user_context,
                     req->req_context,
                     error);
    }

    qd_log(client->core->log, QD_LOG_TRACE,
           "Freeing core client request c=%p, rc=%p (%s)",
           client, req->req_context,
           error ? error : "request complete");

    free_qdrc_client_request_t(req);
}


// issue state change callbacks if necessary
static void _state_updated_CT(qdrc_client_t *client)
{
    if (client->on_state_cb) {
        bool new_state = (client->sender_up && client->receiver_up);
        if (new_state != client->active) {
            client->active = new_state;
            client->on_state_cb(client->core, client, client->user_context, new_state);
            if (client->active && client->tx_credit > 0)
                client->on_flow_cb(client->core,
                                   client,
                                   client->user_context,
                                   client->tx_credit,
                                   false);
        }
    }
}


static void _sender_second_attach_CT(void *context,
                                     qdr_terminus_t *remote_source,
                                     qdr_terminus_t *remote_target)
{
    qdrc_client_t *client = (qdrc_client_t *)context;

    qd_log(client->core->log, QD_LOG_TRACE,
           "Core client sender 2nd attach c=%p", client);

    if (!client->sender_up) {
        client->sender_up = true;
        _state_updated_CT(client);
    }
    qdr_terminus_free(remote_source);
    qdr_terminus_free(remote_target);
}


static void _receiver_second_attach_CT(void *context,
                                       qdr_terminus_t *remote_source,
                                       qdr_terminus_t *remote_target)
{
    qdrc_client_t *client = (qdrc_client_t *)context;

    qd_log(client->core->log, QD_LOG_TRACE,
           "Core client receiver 2nd attach c=%p", client);

    if (!client->receiver_up) {
        client->receiver_up = true;
        client->reply_to = qdr_field_copy(remote_source->address);
        qdrc_endpoint_flow_CT(client->core, client->receiver, client->rx_credit_window, false);
        _state_updated_CT(client);
    }
    qdr_terminus_free(remote_source);
    qdr_terminus_free(remote_target);
}


static void _sender_flow_CT(void *context,
                            int available_credit,
                            bool drain)
{
    qdrc_client_t *client = (qdrc_client_t *)context;
    qdr_core_t *core = client->core;

    client->tx_credit += available_credit;
    qd_log(core->log, QD_LOG_TRACE,
           "Core client sender flow granted c=%p credit=%d d=%s",
           client, client->tx_credit, (drain) ? "T" : "F");
    if (client->tx_credit > 0) {
        _flush_send_queue_CT(client);
    }

    if (client->active && client->on_flow_cb)
        client->on_flow_cb(core,
                           client,
                           client->user_context,
                           client->tx_credit,
                           drain);
    if (drain) {
        client->tx_credit = 0;
    }
}


// disposition update on sent request
static void _sender_update_CT(void *context,
                              qdr_delivery_t *delivery,
                              bool settled,
                              uint64_t disposition)
{
    qdrc_client_t *client = (qdrc_client_t *)context;

    qd_log(client->core->log, QD_LOG_TRACE,
           "Core client sender update c=%p dlv=%p d=%"PRIx64" %s",
           client, delivery, disposition,
           settled ? "settled" : "unsettled");

    if (disposition) {
        // should be on unsettled list
        qdrc_client_request_t *req = DEQ_HEAD(client->unsettled_list);
        DEQ_FIND_N(UNSETTLED, req, (req->delivery == delivery));
        if (req) {
            assert(req->on_ack_cb);
            req->on_ack_cb(client->core,
                           client,
                           client->user_context,
                           req->req_context,
                           disposition);
            // remove from unsettled list
            DEQ_REMOVE_N(UNSETTLED, client->unsettled_list, req);
            req->on_unsettled_list = false;

            // delivery no longer needed
            qdr_delivery_decref_CT(client->core, req->delivery, "core client send request");
            req->delivery = 0;

            if (!req->on_reply_list || disposition != PN_ACCEPTED) {
                // no reply is coming, release the request
                _free_request_CT(client, req, NULL);
            }
        } else {
            // may have received reply so this is not an error
            qd_log(client->core->log, QD_LOG_DEBUG,
                   "Core client could not find request for disposition update"
                   " client=%p delivery=%p",
                   client, delivery);
        }
    }
}


static void _receiver_transfer_CT(void *client_context,
                                  qdr_delivery_t *delivery,
                                  qd_message_t *message)
{
    qdrc_client_t *client = (qdrc_client_t *)client_context;
    qdr_core_t *core = client->core;
    bool complete = qd_message_receive_complete(message);

    qd_log(core->log, QD_LOG_TRACE,
           "Core client received msg c=%p complete=%s",
           client, complete ? "T" : "F");

    if (complete) {
        uint64_t disposition = PN_ACCEPTED;

        // lookup the corresponding request using the correlation-id

        qd_iterator_t *cid_iter = qd_message_field_iterator(message,
                                                            QD_FIELD_CORRELATION_ID);
        if (cid_iter) {
            qdrc_client_request_t *req = NULL;
            qd_hash_retrieve(client->correlations, cid_iter, (void **)&req);
            qd_iterator_free(cid_iter);
            if (req) {

                qd_log(core->log, QD_LOG_TRACE,
                       "Core client received msg c=%p rc=%p cid=%s",
                       client, req->req_context, req->correlation_id);

                qd_hash_remove_by_handle(client->correlations, req->hash_handle);
                qd_hash_handle_free(req->hash_handle);
                req->hash_handle = 0;

                assert(req->on_reply_list);
                DEQ_REMOVE_N(REPLY, client->reply_list, req);
                req->on_reply_list = false;

                qd_iterator_t *app_props = qd_message_field_iterator(message, QD_FIELD_APPLICATION_PROPERTIES);
                qd_iterator_t *body = qd_message_field_iterator(message, QD_FIELD_BODY);

                assert(req->on_reply_cb);
                disposition = req->on_reply_cb(core,
                                               client,
                                               client->user_context,
                                               req->req_context,
                                               app_props,
                                               body);
                // should we keep req if still waiting for disposition update
                // on sent message?  I say "no"...
                _free_request_CT(client, req, NULL);
            } else {
                // request may be old...
                qd_log(core->log, QD_LOG_WARNING,
                       "Core client reply message dropped: no matching correlation-id");
                disposition = PN_ACCEPTED;
            }
        } else {
            qd_log(core->log, QD_LOG_ERROR, "Invalid core client reply message: no correlation-id");
            disposition = PN_REJECTED;
        }

        qdrc_endpoint_settle_CT(core, delivery, disposition);
        qdrc_endpoint_flow_CT(core,
                              client->receiver,
                              1,
                              false);
    }
}


static void _sender_detached_CT(void *client_context,
                                qdr_error_t *error)
{
    qdrc_client_t *client = (qdrc_client_t *)client_context;

    qd_log(client->core->log, QD_LOG_TRACE,
           "Core client sender detached c=%p", client);

    if (client->sender_up) {
        client->sender_up = false;
        client->tx_credit = 0;

        // abort all pending and unsettled requests
        //
        qdrc_client_request_t *req = DEQ_HEAD(client->send_queue);
        while (req) {
            _free_request_CT(client, req, "link detached"); // removes from send_queue
            req = DEQ_HEAD(client->send_queue);
        }
        req = DEQ_HEAD(client->unsettled_list);
        while (req) {
            _free_request_CT(client, req, "link detached"); // removes from unsettled_list
            req = DEQ_HEAD(client->unsettled_list);
        }

        _state_updated_CT(client);
    }

    qdr_error_free(error);
    client->sender = NULL;
}


static void _receiver_detached_CT(void *client_context,
                                  qdr_error_t *error)
{
    qdrc_client_t *client = (qdrc_client_t *)client_context;

    qd_log(client->core->log, QD_LOG_TRACE,
           "Core client receiver detached c=%p", client);

    if (client->receiver_up) {
        client->receiver_up = false;
        free(client->reply_to);
        client->reply_to = 0;

        // abort all waiting requests
        //
        qdrc_client_request_t *req = DEQ_HEAD(client->reply_list);
        while (req) {
            _free_request_CT(client, req, "link detached"); // removes from reply list
            req = DEQ_HEAD(client->reply_list);
        }

        _state_updated_CT(client);
    }
    qdr_error_free(error);
    client->receiver = NULL;
}


static void _sender_cleanup_CT(void *client_context)
{
    _sender_detached_CT(client_context, NULL);
}


static void _receiver_cleanup_CT(void *client_context)
{
    _receiver_detached_CT(client_context, NULL);
}


static qd_message_t *_create_message_CT(qdrc_client_t *client,
                                        qdrc_client_request_t *req)
{
    // build necessary message headers, etc:
    qd_composed_field_t *fld = qd_compose(QD_PERFORMATIVE_HEADER, 0);
    qd_compose_start_list(fld);
    qd_compose_insert_bool(fld, 0);     // durable
    qd_compose_end_list(fld);

    if (req->on_reply_cb) {
        // generate unique correlation-id
        snprintf(req->correlation_id,
                 CORRELATION_ID_LEN, CORRELATION_ID_FMT,
                 (uint64_t)time(NULL), client->next_cid++);
        req->correlation_key = qd_iterator_string(req->correlation_id,
                                                  ITER_VIEW_ALL);
        qd_hash_insert(client->correlations,
                       req->correlation_key,
                       req,
                       &req->hash_handle);

        fld = qd_compose(QD_PERFORMATIVE_PROPERTIES, fld);
        qd_compose_start_list(fld);
        qd_compose_insert_null(fld);                    // message-id
        qd_compose_insert_null(fld);                    // user-id
        qd_compose_insert_null(fld);                    // to
        qd_compose_insert_null(fld);                    // subject
        assert(client->reply_to);
        qd_compose_insert_string(fld, client->reply_to);
        qd_compose_insert_string(fld, req->correlation_id);
        qd_compose_end_list(fld);
    }

    qd_message_t *message = 0;
    if (req->app_properties) {
        message = qd_message_compose(fld, req->app_properties, req->body, true);
    } else {
        message = qd_message_compose(fld, req->body, 0, true);
    }
    req->body = 0;
    req->app_properties = 0;

    return message;
}


// a request has timed out
static void _timer_expired(qdr_core_t *core, void *context)
{
    qdrc_client_request_t *req = (qdrc_client_request_t *)context;
    qdrc_client_t *client = req->client;
    _free_request_CT(client, req, "Timed out");
}

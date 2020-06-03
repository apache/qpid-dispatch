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

#include "qpid/dispatch/ctools.h"
#include "qpid/dispatch/protocol_adaptor.h"
#include "delivery.h"
#include "qpid/dispatch/timer.h"
#include "qpid/dispatch/message.h"
#include <stdio.h>
#include <inttypes.h>

typedef struct qdr_ref_adaptor_t {
    qdr_core_t             *core;
    qdr_protocol_adaptor_t *adaptor;
    qd_timer_t             *startup_timer;
    qd_timer_t             *activate_timer;
    qdr_connection_t       *conn;
    qdr_link_t             *out_link;
    qdr_link_t             *in_link;
} qdr_ref_adaptor_t;


void qdr_ref_connection_activate_CT(void *context, qdr_connection_t *conn)
{
    //
    // Use a zero-delay timer to defer this call to an IO thread
    //
    // Note that this may not be generally safe to do.  There's no guarantee that multiple
    // activations won't schedule multiple IO threads running this code concurrently.
    // Normally, we would rely on assurances provided by the IO scheduler (Proton) that no
    // connection shall ever be served by more than one thread concurrently.
    //
    qdr_ref_adaptor_t *adaptor = (qdr_ref_adaptor_t*) context;
    qd_timer_schedule(adaptor->activate_timer, 0);
}


static void qdr_ref_first_attach(void *context, qdr_connection_t *conn, qdr_link_t *link,
                                 qdr_terminus_t *source, qdr_terminus_t *target,
                                 qd_session_class_t session_class)
{
}


static void qdr_ref_second_attach(void *context, qdr_link_t *link,
                                  qdr_terminus_t *source, qdr_terminus_t *target)
{
    char ftarget[100];
    char fsource[100];

    ftarget[0] = '\0';
    fsource[0] = '\0';

    if (!!source) {
        size_t size = 100;
        qdr_terminus_format(source, fsource, &size);
    }

    if (!!target) {
        size_t size = 100;
        qdr_terminus_format(target, ftarget, &size);
    }

    printf("qdr_ref_second_attach: source=%s target=%s\n", fsource, ftarget);
}


static void qdr_ref_detach(void *context, qdr_link_t *link, qdr_error_t *error, bool first, bool close)
{
}


static void qdr_ref_flow(void *context, qdr_link_t *link, int credit)
{
    qdr_ref_adaptor_t *adaptor = (qdr_ref_adaptor_t*) context;
    qd_buffer_list_t   buffers;
    qd_buffer_t       *buf;
    
    printf("qdr_ref_flow: %d credits issued\n", credit);

    qd_message_t *msg = qd_message();
    DEQ_INIT(buffers);
    buf = qd_buffer();
    char *insert = (char*) qd_buffer_cursor(buf);
    strcpy(insert, "Test Payload");
    qd_buffer_insert(buf, 13);
    DEQ_INSERT_HEAD(buffers, buf);
    qd_message_compose_1(msg, "echo-service", &buffers);

    qdr_link_deliver(adaptor->out_link, msg, 0, false, 0, 0);
}


static void qdr_ref_offer(void *context, qdr_link_t *link, int delivery_count)
{
}


static void qdr_ref_drained(void *context, qdr_link_t *link)
{
}


static void qdr_ref_drain(void *context, qdr_link_t *link, bool mode)
{
}


static int qdr_ref_push(void *context, qdr_link_t *link, int limit)
{
    return 0;
}


static uint64_t qdr_ref_deliver(void *context, qdr_link_t *link, qdr_delivery_t *delivery, bool settled)
{
    return 0;
}


static int qdr_ref_get_credit(void *context, qdr_link_t *link)
{
    return 0;
}


static void qdr_ref_delivery_update(void *context, qdr_delivery_t *dlv, uint64_t disp, bool settled)
{
    char *dispname;

    switch (disp) {
    case PN_ACCEPTED: dispname = "ACCEPTED"; break;
    case PN_REJECTED: dispname = "REJECTED"; break;
    case PN_RELEASED: dispname = "RELEASED"; break;
    case PN_MODIFIED: dispname = "MODIFIED"; break;
    default:
        dispname = "<UNKNOWN>";
    }
    printf("qdr_ref_delivery_update: disp=%s settled=%s\n", dispname, settled ? "true" : "false");
}


static void qdr_ref_conn_close(void *context, qdr_connection_t *conn, qdr_error_t *error)
{
}


static void qdr_ref_conn_trace(void *context, qdr_connection_t *conn, bool trace)
{
}


static void on_startup(void *context)
{
    qdr_ref_adaptor_t *adaptor = (qdr_ref_adaptor_t*) context;

    qdr_connection_info_t *info = qdr_connection_info(false, //bool             is_encrypted,
                                                      false, //bool             is_authenticated,
                                                      true,  //bool             opened,
                                                      "",   //char            *sasl_mechanisms,
                                                      QD_INCOMING, //qd_direction_t   dir,
                                                      "127.0.0.1:47756",    //const char      *host,
                                                      "",    //const char      *ssl_proto,
                                                      "",    //const char      *ssl_cipher,
                                                      "",    //const char      *user,
                                                      "",    //const char      *container,
                                                      pn_data(0),     //pn_data_t       *connection_properties,
                                                      0,     //int              ssl_ssf,
                                                      false, //bool             ssl,
                                                      // set if remote is a qdrouter
                                                      0);    //const qdr_router_version_t *version)

    adaptor->conn = qdr_connection_opened(adaptor->core,
                                          adaptor->adaptor,
                                          true,
                                          QDR_ROLE_NORMAL,
                                          1,
                                          10000,  // get this from qd_connection_t
                                          0,
                                          0,
                                          false,
                                          false,
                                          false,
                                          false,
                                          250,
                                          0,
                                          info,
                                          0,
                                          0);

    uint64_t link_id;
    qdr_terminus_t *dynamic_source = qdr_terminus(0);
    qdr_terminus_set_dynamic(dynamic_source);
    qdr_terminus_t *target = qdr_terminus(0);
    qdr_terminus_set_address(target, "echo-service");

    adaptor->out_link = qdr_link_first_attach(adaptor->conn,
                                              QD_INCOMING,
                                              qdr_terminus(0),  //qdr_terminus_t   *source,
                                              target,           //qdr_terminus_t   *target,
                                              "ref.1",          //const char       *name,
                                              0,                //const char       *terminus_addr,
                                              &link_id);
    adaptor->in_link = qdr_link_first_attach(adaptor->conn,
                                             QD_OUTGOING,
                                             dynamic_source,   //qdr_terminus_t   *source,
                                             qdr_terminus(0),  //qdr_terminus_t   *target,
                                             "ref.2",          //const char       *name,
                                             0,                //const char       *terminus_addr,
                                             &link_id);
}


static void on_activate(void *context)
{
    qdr_ref_adaptor_t *adaptor = (qdr_ref_adaptor_t*) context;

    while (qdr_connection_process(adaptor->conn)) {}
}


/**
 * This initialization function will be invoked when the router core is ready for the protocol
 * adaptor to be created.  This function must:
 *
 *   1) Register the protocol adaptor with the router-core.
 *   2) Prepare the protocol adaptor to be configured.
 */
void qdr_ref_adaptor_init(qdr_core_t *core, void **adaptor_context)
{
    qdr_ref_adaptor_t *adaptor = NEW(qdr_ref_adaptor_t);
    adaptor->core    = core;
    adaptor->adaptor = qdr_protocol_adaptor(core,
                                            "reference", // name
                                            adaptor,     // context
                                            qdr_ref_connection_activate_CT,
                                            qdr_ref_first_attach,
                                            qdr_ref_second_attach,
                                            qdr_ref_detach,
                                            qdr_ref_flow,
                                            qdr_ref_offer,
                                            qdr_ref_drained,
                                            qdr_ref_drain,
                                            qdr_ref_push,
                                            qdr_ref_deliver,
                                            qdr_ref_get_credit,
                                            qdr_ref_delivery_update,
                                            qdr_ref_conn_close,
                                            qdr_ref_conn_trace);
    *adaptor_context = adaptor;

    // TEMPORARY //
    adaptor->startup_timer = qd_timer(core->qd, on_startup, adaptor);
    qd_timer_schedule(adaptor->startup_timer, 0);

    adaptor->activate_timer = qd_timer(core->qd, on_activate, adaptor);
}


void qdr_ref_adaptor_final(void *adaptor_context)
{
    qdr_ref_adaptor_t *adaptor = (qdr_ref_adaptor_t*) adaptor_context;
    qdr_protocol_adaptor_free(adaptor->core, adaptor->adaptor);
    qd_timer_free(adaptor->startup_timer);
    qd_timer_free(adaptor->activate_timer);
    free(adaptor);
}

/**
 * Declare the adaptor so that it will self-register on process startup.
 */
QDR_CORE_ADAPTOR_DECLARE("ref-adaptor", qdr_ref_adaptor_init, qdr_ref_adaptor_final)

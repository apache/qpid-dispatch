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



#include <cstddef>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>

#include "./helpers.hpp"
#include "./qdr_doctest.h"  // or .hpp, to make it clear this is a C++ header?

extern "C" {
#include <router_core/delivery.h>
#include <proton/message.h>
#include <router_core/agent_config_address.h>
#include <router_core/agent_config_auto_link.h>
#include <router_core/router_core_private.h>
#include <qpid/dispatch/protocol_adaptor.h>
}

static void check_query(const qdr_query_t *query);
static qdr_query_t *create_query(qdr_core_t *core, const qd_router_entity_type_t &type);
static qd_message_t *create_request(const std::map<std::string, std::string>& map);

static void add_linkRoute(const QDR &qdr) {
    qdr_core_t *core = qdr.qd->router->router_core;
    // qdr_route_table_setup_CT(core) happened in qd_router_setup_late

    qd_iterator_t *name = qd_iterator_string("I.am.Sam", ITER_VIEW_ALL);

    qd_router_entity_type_t type = QD_ROUTER_LINK;
    qdr_query_t *query = create_query(core, type);

    // TODO fix the following
    //  70: Error performing CREATE of org.apache.qpid.dispatch.router.config.autoLink: Body of request must be a map
    qd_message_t *msg = qd_message();
    qd_message_content_t *content = MSG_CONTENT(msg);

    pn_message_t *pn_msg = pn_message();
    pn_data_t *body;
    body = pn_message_body(pn_msg);
    pn_data_put_map(body);
    pn_data_enter(body);

    auto put_key_value = [&body](const std::string& key, const std::string& value) {
      pn_data_put_string(body, pn_bytes(key.length(), key.c_str()));
      pn_data_put_string(body, pn_bytes(value.length(), value.c_str()));
    };
    put_key_value("address", "aa");
    put_key_value("direction", "out");

    pn_data_exit(body);
    pn_rwbytes_t buf{};
    REQUIRE(pn_message_encode2(pn_msg, &buf) != 0);
    set_content(content, (unsigned char *)buf.start, buf.size);
    free(buf.start);

    pn_message_free(pn_msg);

    //        size_t       size = 10000;
    //        int result = pn_message_encode(pn_msg, (char *)buffer, &size);
    //        pn_message_free(pn_msg);

    qd_iterator_t* iter = qd_message_field_iterator(msg, QD_FIELD_BODY);
    qd_parsed_field_t *parsed_body = qd_parse(iter);
    qd_iterator_free(iter);
    //        qd_message_free(msg);  // DONT free this yet! references into msg are held

    // huh? TODO, need this to see error from CREATE
    qdr.qd->router->router_core->agent_log = qdr.qd->router->log_source;

    // sanity check the body is set correctly
    const int QDR_CONFIG_AUTO_LINK_ADDRESS = 3;
    qd_parsed_field_t *addr_field = qd_parse_value_by_key(parsed_body, qdr_config_auto_link_columns[QDR_CONFIG_AUTO_LINK_ADDRESS]);
    REQUIRE(addr_field != nullptr);

    qdra_config_auto_link_create_CT(core, name, query, parsed_body);
    // called qdr_route_add_auto_link_CT to actually add the auto link
    check_query(query);

    qd_parse_free(parsed_body);
    qd_message_free(msg);  // OK to free now
    qd_compose_free(query->body);
    qdr_query_free(query);
    qd_iterator_free(name);
}

static qdr_query_t *create_query(qdr_core_t *core, const qd_router_entity_type_t &type) {
    void *context = nullptr;
    uint64_t in_conn_id = 0;
    qd_composed_field_t *composed_body = qd_compose_subfield(nullptr);
    qdr_query_t *query = qdr_query(core, context, type, composed_body, in_conn_id);
    return query;
}

static void check_query(const qdr_query_t *query) {    // result is put into query; no need to read logs
    CHECK(query->status.status == QD_AMQP_CREATED.status);  // some smarter compare in doctest?
    CHECK(query->status.description == QD_AMQP_CREATED.description);
    // if query->body is null, it is not set
    if (query->body != nullptr) {  // nonsense, query would be freed at this point if query->body was == null
        //            CHECK(query->body)
        // todo, there will be map with 15 fields about the autoLink; looking in debugger, there's no map now...
        //  it's there, just hard to see in the buffer, had to do
        //  (char(*)[512])qd_buffer_base(query->body->buffers.head)
    }
}

static void add_waypoint(QDR qdr) {
    qdr_core_t *core = qdr.qd->router->router_core;

    qd_iterator_t *name = qd_iterator_string("I.am.Sam", ITER_VIEW_ALL);

    qd_router_entity_type_t type = QD_ROUTER_LINK;
    qdr_query_t *query = create_query(core, type);

    std::map<std::string, std::string> map { {"prefix", "baf"} };
    qd_message_t *msg = create_request(map);

    qd_iterator_t* iter = qd_message_field_iterator(msg, QD_FIELD_BODY);
    qd_parsed_field_t *parsed_body = qd_parse(iter);
    qd_iterator_free(iter);

    qdra_config_address_create_CT(core, name, query, parsed_body);
    check_query(query);

    qd_parse_free(parsed_body);
    qd_message_free(msg);  // OK to free now
    qd_compose_free(query->body);
    qdr_query_free(query);
    qd_iterator_free(name);
}

static qd_message_t *create_request(const std::map<std::string, std::string>& map) {
    qd_message_t *msg = qd_message();
    qd_message_content_t *content = MSG_CONTENT(msg);

    pn_message_t *pn_msg = pn_message();
    pn_data_t *body;
    body = pn_message_body(pn_msg);
    pn_data_put_map(body);
    pn_data_enter(body);

    auto put_key_value = [&body](const std::string& key, const std::string& value) {
      pn_data_put_string(body, pn_bytes(key.length(), key.c_str()));
      pn_data_put_string(body, pn_bytes(value.length(), value.c_str()));
    };

    for (const auto& kv : map) {
        put_key_value(kv.first, kv.second);
    }

    pn_data_exit(body);
    pn_rwbytes_t buf{};
    REQUIRE(pn_message_encode2(pn_msg, &buf) != 0);
    set_content(content, (unsigned char *)buf.start, buf.size);
    free(buf.start);

    pn_message_free(pn_msg);
    return msg;
}

TEST_CASE("waypoint_undeliverable" * doctest::skip(false)) {
    std::thread([]() {
        QDR qdr{};
        qdr.initialize();
        qdr.wait();

        // huh? TODO, need this to see error from CREATE
        qdr.qd->router->router_core->agent_log = qdr.qd->router->log_source;

        add_linkRoute(qdr);
        add_waypoint(qdr);

        // Router threading
        /**
         * qd_server_run starts the threads; new thread for core thread, new threads for worker threads (except one),
         * with the last worker running in the current thread.
         */

        // TODO simulate the https://issues.redhat.com/browse/ENTMQIC-2558 scenario
        // router receives a message from broker
        // thread_run
        // handle
            // container.c: qd_container_handle_event gets the proton event
                // container.c: do_receive
                    // AMQP_rx_handler (pointer indirection)
                        //  qdr_link_deliver_to
                        //  qdr_link_deliver
                        //  qdr_link_deliver_to_routed_link
        // client sets undeliverable-here = true
        // broker gets undeliverable-here = false
        // do I want to simulate opening links to broker/client, or just throw around messages
        //  as little as possible to start, can add more tests later

        // assume links are opened, credit was given, now the messages are flowing and all is well

        // message from broker enters the scene

        // create link, either qdr_create_link_CT, or new_qdr_link_t();
        // this deserves some wrapper, but I don't know how to write something generic and usable

        {  // dealloc uniqe ptrs created in this scope

//            std::unique_ptr<qdr_link_t, decltype(&free_qdr_link_t)> link{new_qdr_link_t(), free_qdr_link_t};
//            link->core = qdr.qd->router->router_core;


//            link->conn = new_qdr_connection_t();
//            link->conn->work_lock = sys_mutex();
//            link->conn->in_activate_list = true; // TODO don't know what this does; true codepath is simpler, though

          // alternative for the above, it is tedious to fill what I need
          char                   rversion[128];
          qdr_connection_info_t *connection_info = qdr_connection_info(false,
                                                                       false,
                                                                       true,
                                                                       nullptr,
                                                                       QD_INCOMING,
                                                                       "localhost",
                                                                       nullptr,
                                                                       nullptr,
                                                                       nullptr,
                                                                       nullptr,
                                                                       nullptr,
                                                                       0,
                                                                       false,
                                                                       rversion,
                                                                       false);

//          qdr_protocol_adaptor_t *adaptor = qdr_protocol_adaptor(
//              qdr.qd->router->router_core,
//              "http2",  // name
//              nullptr,  // context
//              [](void *notused, qdr_connection_t *c){},
//              nullptr,
//              nullptr,
//              nullptr,
//              nullptr,
//              nullptr,
//              nullptr,
//              nullptr,
//              nullptr,
//              nullptr,
//              nullptr,
//              nullptr,
//              nullptr,
//              nullptr);

          qdr_connection_t *conn = qdr_connection_opened(qdr.qd->router->router_core,
                                                         qdr.qd->router->router_core->protocol_adaptors.head,
                                                         true,  // incoming
                                                         QDR_ROLE_NORMAL,
                                                         1,  // cost
                                                         0,
                                                         nullptr,  // label
                                                         nullptr,  // remote container id
                                                         false,    // strip annotations in
                                                         false,    // strip annotations out
                                                         250, // link capacity  // TODO: had to update this due to production code update
                                                         0,    // allow dynamic link routes
                                                         0,    // allow admin status update
                                                         connection_info,
                                                         0,
                                                         0);

          // ok, lets make link using router
          qdr_link_t * qd_link = qdr_create_link_CT(qdr.qd->router->router_core,
                                         conn,
                                         QD_LINK_ROUTER,
                                         QD_INCOMING,
                                         nullptr,
                                         nullptr,
                                         QD_SSN_LINK_ROUTE);

          std::unique_ptr<qdr_link_t, decltype(&free_qdr_link_t)> link{qd_link, free_qdr_link_t};

          
          
          
            link->core_endpoint = NULL;
            link->connected_link = NULL;
            memset(&link->undelivered, 0, sizeof(link->undelivered));

//            link->owning_addr = new_qdr_address_t();
//            link->owning_addr->router_control_only = false; // TODO don't know what this is
//            link->owning_addr->rnodes = qd_bitmask(0); // TODO don't know what this is
//            link->owning_addr->exchange = NULL; // qd_exchange_t is private type
//            link->owning_addr->forwarder = NULL;

            // alternative for the commented block above; always better to use existing "constructors"
//            link->owning_addr = qdr_address_CT(qdr.qd->router->router_core, QD_TREATMENT_ANYCAST_BALANCED, nullptr);

            memset(&link->updated_deliveries, 0, sizeof(link->updated_deliveries));
            link->link_direction = QD_INCOMING;
            memset(&link->work_list, 0, sizeof(link->work_list));
            link->edge = false;
            link->priority = 1;
            link->fallback = true; // sgain, simpler
            link->drain_mode = false;

            qd_message_t * msg = NULL;
            qd_iterator_t *ingress = NULL;
            bool           settled = false;
            qd_bitmask_t * link_exclusion = NULL;
            int            ingress_index = 0;
            uint64_t       remote_disposition = 42;
            qd_delivery_state_t *remote_delivery_state = NULL;

            qdr_delivery_t *delivery = qdr_link_deliver(link.get(), msg, ingress, settled, link_exclusion,
                                                        ingress_index, remote_disposition, remote_delivery_state);  // TODO: had to update this, changed param type
            // qdr_link_deliver triggers async work, cannot check too soon
            qdr.wait();

            // expect one unsettled delivery reference
            qdr_delivery_ref_t *dref = DEQ_HEAD(link->updated_deliveries);
            REQUIRE(dref != nullptr);
//            free_qdr_delivery_ref_t(dref);
//            dref = DEQ_HEAD(link->updated_deliveries);
//            REQUIRE(dref == nullptr);

//          while (dref) {
//              conn->protocol_adaptor->delivery_update_handler(conn->protocol_adaptor->user_context, dref->dlv, dref->dlv->disposition, dref->dlv->settled);
//              qdr_delivery_decref(core, dref->dlv, "qdr_connection_process - remove from updated list");
//              qdr_del_delivery_ref(&updated_deliveries, dref);
//              dref = DEQ_HEAD(updated_deliveries);
//              event_count++;
//          }

            CHECK(delivery->presettled == false);
            // CHECK it all
//            qdr_delivery_decref(qdr.qd->router->router_core, delivery, "release protection of return from deliver");
//

//          qdr_link_cleanup_CT();
          qdr_connection_process(link->conn);
          qdr.wait();
          qdr_connection_process(link->conn);
          qdr.wait();
          qdr_connection_process(link->conn);
          qdr.wait();

          // qdr create link CT
//          void qdr_add_link_ref(qdr_link_ref_list_t *ref_list, qdr_link_t *link, int cls);
//          qdr_add_link_ref(&link->conn->links, link.get(), QDR_LINK_LIST_CLASS_CONNECTION);

          free_qdr_delivery_t(delivery);

          qdr_connection_closed(link->conn);
          qdr.wait();

//          link->conn->core = qdr.qd->router->router_core;
//          qdr_connection_closed(link->conn);
          //            free_qdr_connection_t(link->conn);
//            free_qd_bitmask
//            qd_bitmask_free(link->owning_addr->rnodes); // todo
//            free_qdr_address_t(link->owning_addr);
//            free_qdr_delivery_t(delivery);

//            qdr_protocol_adaptor_free(qdr.qd->router->router_core, adaptor);
          link.release();
        }
        qdr.deinitialize();



     }).join();
}

class RouterStartupLatch {
public:
  static std::mutex mut;
  static void wait_for_qdr(qd_dispatch_t *qd) {
     RouterStartupLatch::mut.try_lock();

     qdr_action_handler_t handler = [](qdr_core_t *core, qdr_action_t *action, bool discard) {
       printf("baf");
       RouterStartupLatch::mut.unlock();
     };
     qdr_action_t *action = qdr_action(handler, "my_action");
     qdr_action_enqueue(qd->router->router_core, action);

     RouterStartupLatch::mut.lock();
 }
};

std::mutex RouterStartupLatch::mut {};

TEST_CASE("qdr_action_enqueue" * doctest::skip(false)) {
    std::thread([]() {
        QDR qdr{};
        qdr.initialize();
        RouterStartupLatch{}.wait_for_qdr(qdr.qd);

        // huh? TODO, need this to see error from CREATE
        qdr.qd->router->router_core->agent_log = qdr.qd->router->log_source;


        qdr.deinitialize();
     }).join();
}
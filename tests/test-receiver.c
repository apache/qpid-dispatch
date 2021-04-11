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
 *
 */

#include "proton/connection.h"
#include "proton/delivery.h"
#include "proton/event.h"
#include "proton/handlers.h"
#include "proton/link.h"
#include "proton/message.h"
#include "proton/reactor.h"
#include "proton/session.h"

#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

bool stop = false;

int  credit_window = 1000;
char *source_address = "test-address";  // name of the source node to receive from
char _addr[] = "127.0.0.1:5672";
char *host_address = _addr;
char *container_name = "TestReceiver";
bool drop_connection = false;

pn_connection_t *pn_conn;
pn_session_t *pn_ssn;
pn_link_t *pn_link;
pn_reactor_t *reactor;
pn_message_t *in_message;       // holds the current received message

uint64_t count = 0;
uint64_t limit = 0;   // if > 0 stop after limit messages arrive


static void signal_handler(int signum)
{
    signal(SIGINT,  SIG_IGN);
    signal(SIGQUIT, SIG_IGN);

    switch (signum) {
    case SIGINT:
    case SIGQUIT:
        stop = true;
        break;
    default:
        break;
    }
}


// Called when reactor exits to clean up app_data
//
static void delete_handler(pn_handler_t *handler)
{
    if (in_message) {
        pn_message_free(in_message);
        in_message = NULL;
    }
}


/* Process each event posted by the reactor.
 */
static void event_handler(pn_handler_t *handler,
                          pn_event_t *event,
                          pn_event_type_t type)
{
    switch (type) {

    case PN_CONNECTION_INIT: {
        // Create and open all the endpoints needed to send a message
        //
        in_message = pn_message();
        pn_connection_open(pn_conn);
        pn_ssn = pn_session(pn_conn);
        pn_session_open(pn_ssn);
        pn_link = pn_receiver(pn_ssn, "MyReceiver");
        pn_terminus_set_address(pn_link_source(pn_link), source_address);
        pn_link_open(pn_link);
        // cannot receive without granting credit:
        pn_link_flow(pn_link, credit_window);
    } break;

    case PN_DELIVERY: {

        if (stop) break;  // silently discard any further messages

        bool rx_done = false;
        pn_delivery_t *dlv = pn_event_delivery(event);
        if (pn_delivery_readable(dlv)) {

             // Drain the data as it comes in rather than waiting for the
             // entire delivery to arrive. This allows the receiver to handle
             // messages that are way huge.

             ssize_t rc;
             static char discard_buffer[1024 * 1024];
             do {
                 rc = pn_link_recv(pn_delivery_link(dlv), discard_buffer, sizeof(discard_buffer));
             } while (rc > 0);
             rx_done = (rc == PN_EOS || rc < 0);
        }

        if (rx_done || !pn_delivery_partial(dlv)) {

            // A full message has arrived (or a failure occurred)
            count += 1;
            pn_delivery_update(dlv, PN_ACCEPTED);
            pn_delivery_settle(dlv);  // dlv is now freed

            if (pn_link_credit(pn_link) <= credit_window/2) {
                // Grant enough credit to bring it up to CAPACITY:
                pn_link_flow(pn_link, credit_window - pn_link_credit(pn_link));
            }

            if (limit && count == limit) {
                stop = true;
                pn_reactor_wakeup(reactor);
            }
        }
    } break;

    default:
        break;
    }
}

static void usage(void)
{
    printf("Usage: receiver <options>\n");
    printf("-a      \tThe address:port of the server [%s]\n", host_address);
    printf("-c      \tExit after N messages arrive (0 == run forever) [%"PRIu64"]\n", limit);
    printf("-i      \tContainer name [%s]\n", container_name);
    printf("-s      \tSource address [%s]\n", source_address);
    printf("-w      \tCredit window [%d]\n", credit_window);
    printf("-E      \tExit without cleanly closing the connection [off]\n");
    exit(1);
}


int main(int argc, char** argv)
{
    /* create a handler for the connection's events.
     */
    pn_handler_t *handler = pn_handler_new(event_handler, 0, delete_handler);
    pn_handler_add(handler, pn_handshaker());

    /* command line options */
    opterr = 0;
    int c;
    while((c = getopt(argc, argv, "i:a:s:hw:c:E")) != -1) {
        switch(c) {
        case 'h': usage(); break;
        case 'a': host_address = optarg; break;
        case 'c':
            if (sscanf(optarg, "%"PRIu64, &limit) != 1)
                usage();
            break;
        case 'i': container_name = optarg; break;
        case 's': source_address = optarg; break;
        case 'w':
            if (sscanf(optarg, "%d", &credit_window) != 1 || credit_window <= 0)
                usage();
            break;
        case 'E': drop_connection = true;  break;

        default:
            usage();
            break;
        }
    }

    signal(SIGQUIT, signal_handler);
    signal(SIGINT,  signal_handler);

    char *host = host_address;
    if (strncmp(host, "amqp://", 7) == 0)
        host += 7;
    char *port = strrchr(host, ':');
    if (port) {
        *port++ = 0;
    } else {
        port = "5672";
    }

    reactor = pn_reactor();
    pn_conn = pn_reactor_connection_to_host(reactor,
                                            host,
                                            port,
                                            handler);

    // the container name should be unique for each client
    pn_connection_set_container(pn_conn, container_name);
    pn_connection_set_hostname(pn_conn, host);

    // periodic wakeup
    pn_reactor_set_timeout(reactor, 1000);

    pn_reactor_start(reactor);

    while (pn_reactor_process(reactor)) {
        if (stop) {
            if (drop_connection)  // hard exit
                exit(0);
            // close the endpoints this will cause pn_reactor_process() to
            // eventually break the loop
            if (pn_link) pn_link_close(pn_link);
            if (pn_ssn) pn_session_close(pn_ssn);
            if (pn_conn) pn_connection_close(pn_conn);
        }
    }

    if (pn_link) pn_link_free(pn_link);
    if (pn_ssn) pn_session_free(pn_ssn);
    if (pn_conn) pn_connection_close(pn_conn);

    pn_reactor_free(reactor);

    return 0;
}

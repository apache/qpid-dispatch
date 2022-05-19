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
#include "proton/link.h"
#include "proton/message.h"
#include "proton/session.h"
#include "proton/proactor.h"

#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <assert.h>

#define BOOL2STR(b) ((b)?"true":"false")

bool stop = false;
bool verbose = false;
bool debug_mode = false;

int  credit_window = 1000;
char *source_address = "test-address";  // name of the source node to receive from
char _addr[] = "127.0.0.1:5672";
char *host_address = _addr;
char *container_name = "TestReceiver";
bool drop_connection = false;
char proactor_address[1024];

pn_connection_t *pn_conn;
pn_session_t *pn_ssn;
pn_link_t *pn_link;
pn_proactor_t *proactor;
pn_message_t *in_message;       // holds the current received message

uint64_t count = 0;
uint64_t limit = 0;   // if > 0 stop after limit messages arrive


void debug(const char *format, ...)
{
    va_list args;

    if (!debug_mode) return;

    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}


static void signal_handler(int signum)
{
    signal(SIGINT,  SIG_IGN);
    signal(SIGQUIT, SIG_IGN);

    switch (signum) {
    case SIGINT:
    case SIGQUIT:
        stop = true;
        if (proactor) pn_proactor_interrupt(proactor);
        break;
    default:
        break;
    }
}


/* Process each event posted by the proactor
 */
static bool event_handler(pn_event_t *event)
{
    const pn_event_type_t type = pn_event_type(event);
    debug("new event=%s\n", pn_event_type_name(type));
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
            }
        }
    } break;

    case PN_PROACTOR_TIMEOUT: {
        if (verbose) {
            fprintf(stdout, "Received:%"PRIu64" of %"PRIu64"\n", count, limit);
            fflush(stdout);
            if (!stop) {
                pn_proactor_set_timeout(proactor, 10 * 1000);
            }
        }
    } break;

    case PN_PROACTOR_INACTIVE: {
        assert(stop);  // expect: inactive due to stopping
        debug("proactor inactive!\n");
        return true;
    } break;

    default:
        break;
    }

    return false;
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
    printf("-d      \tPrint periodic status updates [%s]\n", BOOL2STR(verbose));
    printf("-D      \tPrint debug info [off]\n");
    exit(1);
}


int main(int argc, char** argv)
{
    /* command line options */
    opterr = 0;
    int c;
    while((c = getopt(argc, argv, "i:a:s:hdDw:c:E")) != -1) {
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
        case 'd': verbose = true;          break;
        case 'D': debug_mode = true;       break;

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

    pn_conn = pn_connection();
    // the container name should be unique for each client
    pn_connection_set_container(pn_conn, container_name);
    pn_connection_set_hostname(pn_conn, host);
    proactor = pn_proactor();
    pn_proactor_addr(proactor_address, sizeof(proactor_address), host, port);
    pn_proactor_connect2(proactor, pn_conn, 0, proactor_address);

    if (verbose) {
        // print status every 10 seconds..
        pn_proactor_set_timeout(proactor, 10 * 1000);
    }

    bool done = false;
    while (!done) {
        debug("Waiting for proactor event...\n");
        pn_event_batch_t *events = pn_proactor_wait(proactor);
        debug("Start new proactor batch\n");

        pn_event_t *event = pn_event_batch_next(events);
        while (!done && event) {
            done = event_handler(event);
            event = pn_event_batch_next(events);
        }

        debug("Proactor batch processing done\n");
        pn_proactor_done(proactor, events);

        if (stop) {
            pn_proactor_cancel_timeout(proactor);
            if (drop_connection) {  // hard stop
                exit(0);
            }
            if (pn_conn) {
                debug("Stop detected - closing connection...\n");
                if (pn_link) pn_link_close(pn_link);
                if (pn_ssn) pn_session_close(pn_ssn);
                pn_connection_close(pn_conn);
                pn_link = 0;
                pn_ssn = 0;
                pn_conn = 0;
            }
        }
    }

    pn_proactor_free(proactor);

    if (verbose) {
        fprintf(stdout, "Received:%"PRIu64" of %"PRIu64"\n", count, limit);
        fflush(stdout);
    }

    return 0;
}

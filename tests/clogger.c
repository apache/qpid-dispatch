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

/*
 * A test traffic generator that produces very long messages that are sent in
 * chunks with a delay between each chunk.  This client can be used to simulate
 * very large streaming messages and/or slow producers.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <assert.h>
#include <arpa/inet.h>
#include <stdarg.h>


#include "proton/reactor.h"
#include "proton/message.h"
#include "proton/connection.h"
#include "proton/session.h"
#include "proton/link.h"
#include "proton/delivery.h"
#include "proton/transport.h"
#include "proton/event.h"
#include "proton/handlers.h"

#define DEFAULT_MAX_FRAME  65535
#define BOOL2STR(b) ((b)?"true":"false")
#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))

bool stop = false;
bool verbose = false;

uint64_t limit = 1;               // # messages to send
uint64_t sent = 0;                // # sent
uint64_t acked = 0;               // # of received acks
uint64_t accepted = 0;
uint64_t not_accepted = 0;

bool use_anonymous = false;  // use anonymous link if true
bool presettle = false;      // true = send presettled
uint32_t body_length = 1024 * 1024; // # bytes in vbin32 payload
uint32_t pause_msec = 100;   // pause between sending chunks (milliseconds)

const char *target_address = "test-address";
const char *host_address = "127.0.0.1:5672";
const char *container_name = "Clogger";

pn_reactor_t    *reactor;
pn_connection_t *pn_conn;
pn_session_t    *pn_ssn;
pn_link_t       *pn_link;
pn_delivery_t   *pn_dlv;       // current in-flight delivery
pn_handler_t    *_handler;

uint32_t         bytes_sent;   // number of body data bytes written out link
uint32_t         remote_max_frame = DEFAULT_MAX_FRAME;  // used to limit amount written

#define AMQP_MSG_HEADER     0x70
#define AMQP_MSG_PROPERTIES 0x73
#define AMQP_MSG_DATA       0x75

// minimal AMQP header for a message that contains a single binary value
//
const uint8_t msg_header[] = {
    0x00,  // begin described type
    0x53,  // 1 byte ulong type
    0x70,  // HEADER section
    0x45,  // empty list
    0x00,  // begin described type
    0x53,  // 1 byte ulong type
    0x73,  // PROPERTIES section
    0x45,  // empty list
    0x00,  // begin described type
    0x53,  // 1 byte ulong type
    0x77,  // AMQP Value BODY section
    0xb0,  // Binary uint32 length
    // 4 bytes for length here
    // start of data...
};


void debug(const char *format, ...)
{
    va_list args;

    if (!verbose) return;

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
        if (reactor) pn_reactor_wakeup(reactor);
        break;
    default:
        break;
    }
}


void start_message()
{
    static long tag = 0;  // a simple tag generator

    if (!pn_link || !pn_conn) return;
    if (pn_dlv) {
        debug("Cannot create delivery - in process\n");
        abort();
    }

    debug("start message #%"PRIu64"!\n", sent);

    pn_dlv = pn_delivery(pn_link, pn_dtag((const char *)&tag, sizeof(tag)));
    ++tag;

    bytes_sent = 0;

    // send the message header
    ssize_t rc = pn_link_send(pn_link, (const char *)msg_header, sizeof(msg_header));
    if (rc != sizeof(msg_header)) {
        debug("Link send failed error=%ld\n", rc);
        abort();
    }

    // add the vbin32 length (in network order!!!)
    uint32_t len = htonl(body_length);
    rc = pn_link_send(pn_link, (const char *)&len, sizeof(len));
    if (rc != sizeof(len)) {
        debug("Link send failed error=%ld\n", rc);
        abort();
    }
}


/* return true when message transmit is complete */
bool send_message_data()
{
    static const char zero_block[DEFAULT_MAX_FRAME] = {0};

    if (!pn_dlv) return true;  // not sending

    if (bytes_sent < body_length) {

        uint32_t amount = MIN(body_length - bytes_sent, remote_max_frame);
        amount = MIN(amount, sizeof(zero_block));

        ssize_t rc = pn_link_send(pn_link, zero_block, amount);
        if (rc < 0) {
            debug("Link send failed error=%ld\n", rc);
            abort();
        }
        bytes_sent += rc;

        debug("message body bytes written=%zi total=%"PRIu32" body_length=%"PRIu32"\n",
              rc, bytes_sent, body_length);
    }

    if (bytes_sent == body_length) {
        debug("message #%"PRIu64" sent!\n", sent);
        pn_link_advance(pn_link);
        sent += 1;

        if (presettle) {
            pn_delivery_settle(pn_dlv);
            if (limit && sent == limit) {
                // no need to wait for acks
                debug("stopping...\n");
                stop = true;
                pn_reactor_wakeup(reactor);
            }
        }
        pn_dlv = 0;
        return true;
    }

    return false;
}

/* Process each event posted by the proactor.
   Return true if client has stopped.
 */
static void event_handler(pn_handler_t *handler,
                          pn_event_t *event,
                          pn_event_type_t etype)
{
    debug("new event=%s\n", pn_event_type_name(etype));

    switch (etype) {

    case PN_CONNECTION_INIT: {
        // Create and open all the endpoints needed to send a message
        //
        pn_connection_open(pn_conn);
        pn_ssn = pn_session(pn_conn);
        pn_session_open(pn_ssn);
        pn_link = pn_sender(pn_ssn, "MyClogger");
        if (!use_anonymous) {
            pn_terminus_set_address(pn_link_target(pn_link), target_address);
        }
        pn_link_open(pn_link);
    } break;


    case PN_CONNECTION_REMOTE_OPEN: {
        uint32_t rmf = pn_transport_get_remote_max_frame(pn_event_transport(event));
        remote_max_frame = (rmf != 0) ? rmf : DEFAULT_MAX_FRAME;
        debug("Remote MAX FRAME=%u\n", remote_max_frame);
    } break;

    case PN_LINK_FLOW: {
        // the remote has given us some credit, now we can send messages
        //
        if (limit == 0 || sent < limit) {
            if (pn_link_credit(pn_link) > 0) {
                if (!pn_dlv) {
                    start_message();
                    pn_reactor_schedule(reactor, pause_msec, _handler);   // send body after pause
                }
            }
        }
    } break;


    case PN_TRANSPORT: {
        ssize_t pending = pn_transport_pending(pn_event_transport(event));
        debug("PN_TRANSPORT pending=%ld\n", pending);
    } break;

    case PN_DELIVERY: {
        pn_delivery_t *dlv = pn_event_delivery(event);
        if (pn_delivery_updated(dlv)) {
            uint64_t rs = pn_delivery_remote_state(dlv);
            pn_delivery_clear(dlv);

            switch (rs) {
            case PN_RECEIVED:
                debug("PN_DELIVERY: received\n");
                // This is not a terminal state - it is informational, and the
                // peer is still processing the message.
                break;
            case PN_ACCEPTED:
                debug("PN_DELIVERY: accept\n");
                ++acked;
                ++accepted;
                pn_delivery_settle(dlv);
                break;
            case PN_REJECTED:
            case PN_RELEASED:
            case PN_MODIFIED:
            default:
                ++acked;
                ++not_accepted;
                pn_delivery_settle(dlv);
                debug("Message not accepted - code: 0x%lX\n", (unsigned long)rs);
                break;
            }

            if (limit && acked == limit) {
                // initiate clean shutdown of the endpoints
                debug("stopping...\n");
                stop = true;
                pn_reactor_wakeup(reactor);
            }
        }
    } break;

    case PN_TIMER_TASK: {
        if (!send_message_data()) {   // not done sending
            pn_reactor_schedule(reactor, pause_msec, _handler);
        } else if (limit == 0 || sent < limit) {
            if (pn_link_credit(pn_link) > 0) {
                // send next message
                start_message();
                pn_reactor_schedule(reactor, pause_msec, _handler);
            }
        }
    } break;

    default:
        break;
    }
}


static void delete_handler(pn_handler_t *handler)
{
}


static void usage(const char *prog)
{
    printf("Usage: %s <options>\n", prog);
    printf("-a      \tThe host address [%s]\n", host_address);
    printf("-c      \t# of messages to send, 0 == nonstop [%"PRIu64"]\n", limit);
    printf("-i      \tContainer name [%s]\n", container_name);
    printf("-n      \tUse an anonymous link [%s]\n", BOOL2STR(use_anonymous));
    printf("-s      \tBody size in bytes [%d]\n", body_length);
    printf("-t      \tTarget address [%s]\n", target_address);
    printf("-u      \tSend all messages presettled [%s]\n", BOOL2STR(presettle));
    printf("-D      \tPrint debug info [off]\n");
    printf("-P      \tPause between sending frames [%"PRIu32"]\n", pause_msec);
    exit(1);
}

int main(int argc, char** argv)
{
    /* command line options */
    opterr = 0;
    int c;
    while ((c = getopt(argc, argv, "ha:c:i:ns:t:uDP:")) != -1) {
        switch(c) {
        case 'h': usage(argv[0]); break;
        case 'a': host_address = optarg; break;
        case 'c':
            if (sscanf(optarg, "%"SCNu64, &limit) != 1)
                usage(argv[0]);
            break;
        case 'i': container_name = optarg; break;
        case 'n': use_anonymous = true; break;
        case 's':
            if (sscanf(optarg, "%"SCNu32, &body_length) != 1)
                usage(argv[0]);
            break;
        case 't': target_address = optarg; break;
        case 'u': presettle = true; break;
        case 'D': verbose = true; break;
        case 'P':
            if (sscanf(optarg, "%"SCNu32, &pause_msec) != 1)
                usage(argv[0]);
            break;
        default:
            usage(argv[0]);
            break;
        }
    }

    signal(SIGQUIT, signal_handler);
    signal(SIGINT,  signal_handler);

    // test infrastructure may add a "amqp[s]://" prefix to the address string.
    // That causes proactor much grief, so strip it off
    if (strncmp("amqps://", host_address, strlen("amqps://")) == 0) {
        host_address += strlen("amqps://"); // no! no ssl for you!
    } else if (strncmp("amqp://", host_address, strlen("amqp://")) == 0) {
        host_address += strlen("amqp://");
    }

    // convert host_address to hostname and port
    char *hostname = strdup(host_address);
    char *port = strchr(hostname, ':');
    if (!port) {
        port = "5672";
    } else {
        *port++ = 0;
    }

    _handler = pn_handler_new(event_handler, 0, delete_handler);
    pn_handler_add(_handler, pn_handshaker());

    reactor = pn_reactor();
    pn_conn = pn_reactor_connection_to_host(reactor,
                                            hostname,
                                            port,
                                            _handler);

    // the container name should be unique for each client
    pn_connection_set_container(pn_conn, container_name);
    pn_connection_set_hostname(pn_conn, hostname);

    // break out of pn_reactor_process once a second to check if done
    pn_reactor_set_timeout(reactor, 1000);

    pn_reactor_start(reactor);

    while (pn_reactor_process(reactor)) {
        if (stop) {
            // close the endpoints this will cause pn_reactor_process() to
            // eventually break the loop
            if (pn_link) pn_link_close(pn_link);
            if (pn_ssn) pn_session_close(pn_ssn);
            if (pn_conn) pn_connection_close(pn_conn);
            pn_link = 0;
            pn_ssn = 0;
            pn_conn = 0;
        }
    }

    if (pn_link) pn_link_free(pn_link);
    if (pn_ssn) pn_session_free(pn_ssn);
    if (pn_conn) pn_connection_close(pn_conn);

    pn_reactor_free(reactor);

    if (not_accepted) {
        printf("Sent: %" PRIu64 "  Accepted: %" PRIu64 " Not Accepted: %" PRIu64 "\n", sent, accepted, not_accepted);
        if (accepted + not_accepted != sent) {
            printf("FAILURE! Sent: %" PRIu64 "  Acked: %" PRIu64 "\n", sent, accepted + not_accepted);
            return 1;
        }
    }
    return 0;
}

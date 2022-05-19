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

#define ADD_ANNOTATIONS 1

#include "qpid/dispatch/buffer.h"
#include "qpid/dispatch/message.h"

#include "proton/connection.h"
#include "proton/delivery.h"
#include "proton/link.h"
#include "proton/message.h"
#include "proton/session.h"
#include "proton/proactor.h"

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <assert.h>

#define BOOL2STR(b) ((b)?"true":"false")

#define BODY_SIZE_SMALL  100L
#define BODY_SIZE_MEDIUM ((long int)((4 * 1024) + 1))
#define BODY_SIZE_LARGE  ((long int)((65 * 1024) + 1))
#define BODY_SIZE_HUGE   ((long int)((BUFFER_SIZE * QD_QLIMIT_Q3_UPPER * 3) + 1))

#define DEFAULT_PRIORITY 4

// body data - block of 0's
//

bool stop = false;
bool verbose = false;
bool debug_mode = false;

uint64_t limit = 1;               // # messages to send
uint64_t count = 0;               // # sent
uint64_t acked = 0;               // # of received acks

// outcome counts
uint64_t accepted = 0;
uint64_t rejected = 0;
uint64_t modified = 0;
uint64_t released = 0;

char body_data_pattern = 'B';  // fill body data
bool use_anonymous = false;    // use anonymous link if true
bool presettle = false;        // true = send presettled
bool add_annotations = false;
long int body_size = BODY_SIZE_SMALL;
bool drop_connection = false;
unsigned int priority = DEFAULT_PRIORITY;

// buffer for encoded message
pn_bytes_t body_data = {0};
char *encode_buffer = NULL;
size_t encode_buffer_size = 0;    // size of malloced memory
size_t encoded_data_size = 0;     // length of encoded content


char *target_address = "test-address";
char _addr[] = "127.0.0.1:5672";
char *host_address = _addr;
char *container_name = "TestSender";
char proactor_address[1024];

pn_connection_t *pn_conn;
pn_session_t *pn_ssn;
pn_link_t *pn_link;
pn_proactor_t *proactor;
pn_message_t *out_message;


// odd-length long string
const char big_string[] =
    "+"
    "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
    "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
    "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
    "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
    "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
    "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
    "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
    "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
    "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
    "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789";


void debug(const char *format, ...)
{
    va_list args;

    if (!debug_mode) return;

    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}


static void add_message_annotations(pn_message_t *out_message)
{
    // just a bunch of dummy MA
    pn_data_t *annos = pn_message_annotations(out_message);
    pn_data_clear(annos);
    pn_data_put_map(annos);
    pn_data_enter(annos);

    pn_data_put_symbol(annos, pn_bytes(strlen("my-key"), "my-key"));
    pn_data_put_string(annos, pn_bytes(strlen("my-data"), "my-data"));

    pn_data_put_symbol(annos, pn_bytes(strlen("my-other-key"), "my-other-key"));
    pn_data_put_string(annos, pn_bytes(strlen("my-other-data"), "my-other-data"));

    // embedded map
    pn_data_put_symbol(annos, pn_bytes(strlen("my-map"), "my-map"));
    pn_data_put_map(annos);
    pn_data_enter(annos);
    pn_data_put_symbol(annos, pn_bytes(strlen("my-map-key1"), "my-map-key1"));
    pn_data_put_char(annos, 'X');
    pn_data_put_symbol(annos, pn_bytes(strlen("my-map-key2"), "my-map-key2"));
    pn_data_put_byte(annos, 0x12);
    pn_data_put_symbol(annos, pn_bytes(strlen("my-map-key3"), "my-map-key3"));
    pn_data_put_string(annos, pn_bytes(strlen("Are We Not Men?"), "Are We Not Men?"));
    pn_data_put_symbol(annos, pn_bytes(strlen("my-last-key"), "my-last-key"));
    pn_data_put_binary(annos, pn_bytes(sizeof(big_string), big_string));
    pn_data_exit(annos);

    pn_data_put_symbol(annos, pn_bytes(strlen("my-ulong"), "my-ulong"));
    pn_data_put_ulong(annos, 0xDEADBEEFCAFEBEEF);

    // embedded list
    pn_data_put_symbol(annos, pn_bytes(strlen("my-list"), "my-list"));
    pn_data_put_list(annos);
    pn_data_enter(annos);
    pn_data_put_string(annos, pn_bytes(sizeof(big_string), big_string));
    pn_data_put_double(annos, 3.1415);
    pn_data_put_short(annos, 1966);
    pn_data_exit(annos);

    pn_data_put_symbol(annos, pn_bytes(strlen("my-bool"), "my-bool"));
    pn_data_put_bool(annos, false);

    pn_data_exit(annos);
}


void generate_message(void)
{
    if (!out_message) {
        out_message = pn_message();
    }

    if (use_anonymous) {
        pn_message_set_address(out_message, target_address);
    }

    if (priority != DEFAULT_PRIORITY) {
        pn_message_set_priority(out_message, (uint8_t)priority);
    }

    pn_data_t *body = pn_message_body(out_message);
    pn_data_clear(body);

    if (!body_data.start) {
        char *ptr = malloc(BODY_SIZE_HUGE);
        if (!ptr) {
            perror("Out of memory!");
            exit(-1);
        }
        memset(ptr, (int)body_data_pattern, BODY_SIZE_HUGE);
        body_data.start = ptr;
    }

    body_data.size = body_size;
    pn_data_put_binary(body, body_data);

    if (add_annotations) {
        add_message_annotations(out_message);
    }

    // now encode it

    pn_data_rewind(pn_message_body(out_message));
    if (!encode_buffer) {
        encode_buffer_size = body_size + 512;
        encode_buffer = malloc(encode_buffer_size);
    }

    int rc = 0;
    size_t len = encode_buffer_size;
    do {
        rc = pn_message_encode(out_message, encode_buffer, &len);
        if (rc == PN_OVERFLOW) {
            free(encode_buffer);
            encode_buffer_size *= 2;
            encode_buffer = malloc(encode_buffer_size);
            len = encode_buffer_size;
        }
    } while (rc == PN_OVERFLOW);

    if (rc) {
        perror("buffer encode failed");
        exit(-1);
    }

    encoded_data_size = len;
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


/* Process each event posted by the proactor.
 */
static bool event_handler(pn_event_t *event)
{
    const pn_event_type_t type = pn_event_type(event);
    debug("new event=%s\n", pn_event_type_name(type));
    switch (type) {

    case PN_CONNECTION_INIT: {
        // Create and open all the endpoints needed to send a message
        //
        pn_connection_open(pn_conn);
        pn_session_t *pn_ssn = pn_session(pn_conn);
        pn_session_open(pn_ssn);
        pn_link_t *pn_link = pn_sender(pn_ssn, "MySender");
        if (!use_anonymous) {
            pn_terminus_set_address(pn_link_target(pn_link), target_address);
        }
        pn_link_open(pn_link);

        acked = count;
        generate_message();

    } break;

    case PN_LINK_FLOW: {
        // the remote has given us some credit, now we can send messages
        //
        static long tag = 0;  // a simple tag generator
        pn_link_t *sender = pn_event_link(event);
        int credit = pn_link_credit(sender);

        while (!stop && credit > 0 && (limit == 0 || count < limit)) {
            --credit;
            ++count;
            ++tag;
            pn_delivery_t *delivery = pn_delivery(sender, pn_dtag((const char *)&tag, sizeof(tag)));
            pn_link_send(sender, encode_buffer, encoded_data_size);
            pn_link_advance(sender);
            if (presettle) {
                pn_delivery_settle(delivery);
                // fake terminal outcome
                ++acked;
                ++accepted;
                if (limit && count == limit) {
                    // no need to wait for acks
                    debug("stopping (presettled)...\n");
                    stop = true;
                }
            }
        }
    } break;

    case PN_DELIVERY: {
        pn_delivery_t *dlv = pn_event_delivery(event);
        if (pn_delivery_updated(dlv)) {
            uint64_t rs = pn_delivery_remote_state(dlv);
            switch (rs) {
            case PN_RECEIVED:
                // This is not a terminal state - it is informational, and the
                // peer is still processing the message.
                break;
            case PN_ACCEPTED:
                ++acked;
                ++accepted;
                pn_delivery_settle(dlv);
                break;
            case PN_REJECTED:
                ++acked;
                ++rejected;
                pn_delivery_settle(dlv);
                break;
            case PN_RELEASED:
                ++acked;
                ++released;
                pn_delivery_settle(dlv);
                break;
            case PN_MODIFIED:
                ++acked;
                ++modified;
                pn_delivery_settle(dlv);
                break;

            default:
                break;
            }

            if (limit && acked == limit) {
                // initiate clean shutdown of the endpoints
                debug("stopping...\n");
                stop = true;
            }
        }
    } break;

    case PN_PROACTOR_TIMEOUT: {
        if (verbose) {
            fprintf(stdout,
                    "Sent:%"PRIu64" Accepted:%"PRIu64" Rejected:%"PRIu64
                    " Released:%"PRIu64" Modified:%"PRIu64" Limit:%"PRIu64"\n",
                    count, accepted, rejected, released, modified, limit);
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
  printf("Usage: sender <options>\n");
  printf("-a      \tThe address:port of the server [%s]\n", host_address);
  printf("-c      \t# of messages to send, 0 == nonstop [%"PRIu64"]\n", limit);
  printf("-i      \tContainer name [%s]\n", container_name);
  printf("-n      \tUse an anonymous link [%s]\n", BOOL2STR(use_anonymous));
  printf("-s      \tBody size in bytes ('s'=%ld 'm'=%ld 'l'=%ld 'x'=%ld) [%ld]\n",
         BODY_SIZE_SMALL, BODY_SIZE_MEDIUM, BODY_SIZE_LARGE, BODY_SIZE_HUGE, body_size);
  printf("-t      \tTarget address [%s]\n", target_address);
  printf("-u      \tSend all messages presettled [%s]\n", BOOL2STR(presettle));
  printf("-M      \tAdd dummy Message Annotations section [off]\n");
  printf("-E      \tExit without cleanly closing the connection [off]\n");
  printf("-p      \tMessage priority [%d]\n", priority);
  printf("-X      \tMessage body data pattern [%c]\n", (char)body_data_pattern);
  printf("-d      \tPrint periodic status updates [%s]\n", BOOL2STR(verbose));
  printf("-D      \tPrint debug info [off]\n");
  exit(1);
}

int main(int argc, char** argv)
{
    /* command line options */
    opterr = 0;
    int c;
    while ((c = getopt(argc, argv, "ha:c:i:ns:t:udMEDp:X:")) != -1) {
        switch(c) {
        case 'h': usage(); break;
        case 'a': host_address = optarg; break;
        case 'c':
            if (sscanf(optarg, "%"PRIu64, &limit) != 1)
                usage();
            break;
        case 'i': container_name = optarg; break;
        case 'n': use_anonymous = true; break;
        case 's':
            switch (optarg[0]) {
            case 's': body_size = BODY_SIZE_SMALL; break;
            case 'm': body_size = BODY_SIZE_MEDIUM; break;
            case 'l': body_size = BODY_SIZE_LARGE; break;
            case 'x': body_size = BODY_SIZE_HUGE; break;
            default:
                usage();
            }
            break;
        case 't': target_address = optarg; break;
        case 'd': verbose = true;          break;
        case 'u': presettle = true;        break;
        case 'M': add_annotations = true;  break;
        case 'E': drop_connection = true;  break;
        case 'X': body_data_pattern = optarg[0];  break;
        case 'D': debug_mode = true; break;
        case 'p':
            if (sscanf(optarg, "%u", &priority) != 1)
                usage();
            break;

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
                fprintf(stdout,
                        "Sent:%"PRIu64" Accepted:%"PRIu64" Rejected:%"PRIu64
                        " Released:%"PRIu64" Modified:%"PRIu64"\n",
                        count, accepted, rejected, released, modified);
                fflush(stdout);
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
        fprintf(stdout,
                "Sent:%"PRIu64" Accepted:%"PRIu64" Rejected:%"PRIu64
                " Released:%"PRIu64" Modified:%"PRIu64" Limit:%"PRIu64"\n",
                count, accepted, rejected, released, modified, limit);
        fflush(stdout);
    }

    return 0;
}

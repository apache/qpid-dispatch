#ifndef http1_codec_H
#define http1_codec_H 1
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
#include "qpid/dispatch/buffer.h"
#include "qpid/dispatch/message.h"

#include <inttypes.h>
#include <stdbool.h>


// HTTP/1.x Encoder/Decoder Library
//
// This library provides an API for encoding and decoding HTTP/1.x messages.
//
// The decoder takes qd_buffer_t chains containing HTTP/1.x data read from the
// TCP connection and issues callbacks as various parts (headers, body, status)
// of the HTTP/1.x message are parsed.
//
// The encoder allows the application to construct an HTTP/1.x message. An API
// is provided for building the message and callbacks are invoked when the
// encoder has full qd_buffer_t or body_data to send out the TCP connection.
//
// This library provides two classes:
//
// * h1_codec_connection_t - a context for a single TCP connection over which
//   HTTP/1.x messages are exchanged.
//
// * h1_codec_request_state_t - a context which tracks the state of a single
//   HTTP/1.x Request <-> Response message exchange. Multiple
//   h1_codec_request_state_t can be associated with an h1_codec_connection_t due to
//   request pipelining.
//


#define HTTP1_VERSION_1_1  "HTTP/1.1"
#define HTTP1_VERSION_1_0  "HTTP/1.0"

typedef struct h1_codec_connection_t    h1_codec_connection_t;
typedef struct h1_codec_request_state_t h1_codec_request_state_t;


typedef enum {
    HTTP1_CONN_CLIENT,  // connection initiated by client
    HTTP1_CONN_SERVER,  // connection to server
} h1_codec_connection_type_t;


typedef enum {
    HTTP1_STATUS_BAD_REQ = 400,
    HTTP1_STATUS_SERVER_ERR = 500,
    HTTP1_STATUS_BAD_VERSION = 505,
    HTTP1_STATUS_SERVICE_UNAVAILABLE = 503,
} h1_codec_status_code_t;


typedef struct h1_codec_config_t {

    h1_codec_connection_type_t type;

    // Callbacks to send data out the raw connection.  These callbacks are
    // triggered by the message creation API (h1_codec_tx_*) Note well: these
    // callbacks are called in the order in which the data must be written out
    // the raw connection!

    // tx_buffers()
    // Send a list of buffers containing encoded HTTP message data. The caller
    // assumes ownership of the buffer list and must release the buffers when
    // done.  len is set to the total octets of data in the list.
    //
    void (*tx_buffers)(h1_codec_request_state_t *hrs, qd_buffer_list_t *data, unsigned int len);

    // tx_stream_data()
    // Called with stream_data containing encoded HTTP message data. Only
    // called if the outgoing HTTP message has a body. The caller assumes
    // ownership of the stream_data and must release it when done.
    //
    void (*tx_stream_data)(h1_codec_request_state_t *hrs, qd_message_stream_data_t *stream_data);

    //
    // RX message callbacks
    //
    // These callbacks should return 0 on success or non-zero on error.  A
    // non-zero return code is used as the return code from
    // h1_codec_connection_rx_data()
    //

    // HTTP request received - new h1_codec_request_state_t created (hrs).  This
    // hrs must be supplied in the h1_codec_tx_response() method when sending the
    // response.
    int (*rx_request)(h1_codec_request_state_t *hrs,
                      const char *method,
                      const char *target,
                      uint32_t   version_major,
                      uint32_t   version_minor);

    // HTTP response received - the h1_codec_request_state_t comes from the return
    // value of the h1_codec_tx_request() method used to create the corresponding
    // request.  Note well that if status_code is Informational (1xx) then this
    // response is NOT the last response for the current request (See RFC7231,
    // 6.2 Informational 1xx).  The request_done callback will be called after
    // the LAST response has been received.
    //
    int (*rx_response)(h1_codec_request_state_t *hrs,
                       int status_code,
                       const char *reason_phrase,
                       uint32_t version_major,
                       uint32_t version_minor);

    int (*rx_header)(h1_codec_request_state_t *hrs, const char *key, const char *value);
    int (*rx_headers_done)(h1_codec_request_state_t *hrs, bool has_body);

    int (*rx_body)(h1_codec_request_state_t *hrs, qd_buffer_list_t *body, size_t len, bool more);

    // Invoked after a received HTTP message has been completely parsed.
    //
    void (*rx_done)(h1_codec_request_state_t *hrs);

    // Invoked when the final response message has been decoded (server
    // connection) or encoded (client connection), or the request has been cancelled.
    // hrs is freed on return from this callback and must not be referenced further.
    void (*request_complete)(h1_codec_request_state_t *hrs,
                             bool cancelled);

} h1_codec_config_t;


// create a new connection and assign it a context
//
h1_codec_connection_t *h1_codec_connection(h1_codec_config_t *config, void *context);
void *h1_codec_connection_get_context(h1_codec_connection_t *conn);

// Release the codec.  This can only be done after all outstanding requests
// have been completed or cancelled.
//
void h1_codec_connection_free(h1_codec_connection_t *conn);

// Push inbound network data into the http1 library. All rx_*() callbacks occur
// during this call.  The return value is zero on success.  If a non-zero value
// is returned the codec state is unknown - the application must cancel all
// outstanding requests and destroy the conn by calling
// h1_codec_connection_free().
//
int h1_codec_connection_rx_data(h1_codec_connection_t *conn, qd_buffer_list_t *data, size_t len);

// Notify the codec that the endpoint closed the connection.  For server-facing
// connections it is safe to resume calling h1_codec_connection_rx_data() for
// the h1_codec_connection once the connection to the server is reestablished.
// Client-facing connections cannot be resumed after the connection has been
// closed. In the client case the  application must cancel all outstanding
// requests and then call h1_codec_connection_free() instead.
//
void h1_codec_connection_rx_closed(h1_codec_connection_t *conn);

void h1_codec_request_state_set_context(h1_codec_request_state_t *hrs, void *context);
void *h1_codec_request_state_get_context(const h1_codec_request_state_t *hrs);
h1_codec_connection_t *h1_codec_request_state_get_connection(const h1_codec_request_state_t *hrs);

// Cancel the request.  The h1_codec_request_state_t is freed during this call.
// The request_complete callback will be invoked during this call with
// cancelled=True.
//
void h1_codec_request_state_cancel(h1_codec_request_state_t *hrs);

// the lifecycle of the returned strings end when the hrs is released:
const char *h1_codec_request_state_method(const h1_codec_request_state_t *hrs);
const char *h1_codec_request_state_target(const h1_codec_request_state_t *hrs);
const uint32_t h1_codec_request_state_response_code(const h1_codec_request_state_t *hrs);

// true when codec has encoded/decoded a complete request message
bool h1_codec_request_complete(const h1_codec_request_state_t *hrs);

// true when codec has encoded/decoded a complete response message
bool h1_codec_response_complete(const h1_codec_request_state_t *hrs);

// query the amount of octets read (in) and written (out) for a request
void h1_codec_request_state_counters(const h1_codec_request_state_t *hrs,
                                     uint64_t *in_octets,
                                     uint64_t *out_octets);

// Utility for iterating over a list of HTTP tokens.
//
// start - begin search
// len - (output) length of token if non-null returned
// next - (output) address past token - for start of next search
// Returns a pointer to the first byte of the token, or 0 if no token found
//
const char *h1_codec_token_list_next(const char *start, size_t *len, const char **next);


//
// API for sending HTTP/1.x messages
//
// The tx_msg_headers and tx_msg_body callbacks can occur during any of these
// calls.
//


// initiate a request - this creates a new request state context
//
h1_codec_request_state_t *h1_codec_tx_request(h1_codec_connection_t *conn, const char *method, const char *target,
                                              uint32_t version_major, uint32_t version_minor);

// Respond to a received request - the request state context should be the one
// supplied during the corresponding rx_request callback.  It is required that
// the caller issues responses in the same order as requests arrive.
//
int h1_codec_tx_response(h1_codec_request_state_t *hrs, int status_code, const char *reason_phrase,
                         uint32_t version_major, uint32_t version_minor);

// add header to outgoing message
//
int h1_codec_tx_add_header(h1_codec_request_state_t *hrs, const char *key, const char *value);

// Stream outgoing body data.  Ownership of stream_data is passed to the caller.
//
int h1_codec_tx_body(h1_codec_request_state_t *hrs, qd_message_stream_data_t *stream_data);

// Write body as string
//
int h1_codec_tx_body_str(h1_codec_request_state_t *hrs, char *data);

// outgoing message construction complete.  The request_complete() callback MAY
// occur during this call.
//
// need_close: set to true if the outgoing message is an HTTP response that
// does not provide an explict body length. If true it is up to the caller to
// close the underlying socket connection after all outgoing data for this
// request has been sent.
//
int h1_codec_tx_done(h1_codec_request_state_t *hrs, bool *need_close);

// begin multipart content; this will generate a boundary marker and set the content type header
//
int h1_codec_tx_begin_multipart(h1_codec_request_state_t *hrs);

// begin a new multipart section
//
int h1_codec_tx_begin_multipart_section(h1_codec_request_state_t *hrs);

// mark the end of multipart data
//
int h1_codec_tx_end_multipart(h1_codec_request_state_t *hrs);

uint64_t h1_codec_tx_multipart_section_boundary_length();
uint64_t h1_codec_tx_multipart_end_boundary_length();


#endif // http1_codec_H

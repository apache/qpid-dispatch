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

#include "qpid/dispatch/http1_codec.h"

#include "qpid/dispatch/alloc_pool.h"
#include "qpid/dispatch/buffer.h"
#include "qpid/dispatch/discriminator.h"
#include "qpid/dispatch/iterator.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

//
// This file contains code for encoding/decoding an HTTP/1.x data stream.  See
// http1_codec.h for details.
//


// @TODO(kgiusti)
// - properly set 'more' flag on rx_body() callback


const uint8_t CR_TOKEN = '\r';
const uint8_t LF_TOKEN = '\n';
const char   *CRLF = "\r\n";
const char   *DOUBLE_HYPHEN = "--";
const char   *CONTENT_TYPE_KEY = "Content-Type";
const char   *MULTIPART_CONTENT_TYPE_PREFIX = "multipart/mixed; boundary=";
const qd_iterator_pointer_t NULL_I_PTR = {0};

// true for informational response codes
#define IS_INFO_RESPONSE(code) ((code) / 100 == 1)

// true if response code indicates that the response will NOT contain a body
// 204 = No Content
// 205 = Reset Content
// 304 = Not Modified
#define NO_BODY_RESPONSE(code) \
    ((code) == 204 ||          \
     (code) == 205 ||          \
     (code) == 304 ||          \
     IS_INFO_RESPONSE(code))


typedef enum {
    HTTP1_MSG_STATE_START = 0, // parsing start-line
    HTTP1_MSG_STATE_HEADERS,   // parsing headers
    HTTP1_MSG_STATE_BODY,      // parsing body
    HTTP1_MSG_STATE_DONE,      // parsing complete
} http1_msg_state_t;


typedef enum {
    HTTP1_CHUNK_HEADER = 0,    // waiting for chunk header
    HTTP1_CHUNK_DATA,          // reading chunk data
    HTTP1_CHUNK_TRAILERS,      // reading until lone CRLF
} http1_chunk_state_t;


typedef struct scratch_memory_t {
    uint8_t *buf;
    size_t   size;  // of allocated memory, not contents!
} scratch_memory_t;


// State for a single request-response transaction.
//
// A new state is created when a request starts (either via the rx_request
// callback in the case of client connections or the h1_codec_tx_request() call
// for server connections).
//
// For a connection to a server the rx_response callbacks will occur in the same
// order as h1_codec_tx_request calls are made.
//
// For a connection to a client the caller must ensure that calls to
// h1_codec_tx_response() must be made in the same order as rx_request callbacks
// occur.
//
struct h1_codec_request_state_t {
    DEQ_LINKS(struct h1_codec_request_state_t);
    void                *context;
    h1_codec_connection_t *conn;
    char                *method;
    uint32_t             response_code;

    uint64_t in_octets;     // # encoded octets arriving from endpoint
    uint64_t out_octets;    // # encoded octets written to endpoint

    bool no_body_method;    // true if request method is either HEAD or CONNECT
    bool request_complete;  // true when request message done encoding/decoding
    bool response_complete; // true when response message done encoding/decoding
    bool close_expected;    // if true do not signal request_complete cb until closed
};
DEQ_DECLARE(h1_codec_request_state_t, h1_codec_request_state_list_t);
ALLOC_DECLARE(h1_codec_request_state_t);
ALLOC_DEFINE(h1_codec_request_state_t);


// The HTTP/1.1 connection
//
struct h1_codec_connection_t {
    void *context;

    // http requests are added to tail,
    // in-progress response is at head
    h1_codec_request_state_list_t hrs_queue;

    // Decoder for current incoming msg.
    //
    // incoming: holds the raw data received by the proactor from this
    // connection.
    //
    // read_ptr: points to the next octet to be decoded on the incoming buffer
    // list. Remaining is the length of the raw data to be decoded.
    //
    // body_ptr: points to the first unconsumed octet of the message
    // body. Remaining is the number of octets that may be consumed.
    // Invariant: body_ptr.buffer always points to the incoming.head as body
    // data is being parsed.
    //
    struct decoder_t {
        qd_buffer_list_t       incoming;
        qd_iterator_pointer_t  read_ptr;
        qd_iterator_pointer_t  body_ptr;

        h1_codec_request_state_t *hrs;            // current request/response
        http1_msg_state_t       state;
        scratch_memory_t        scratch;
        const char             *error_msg;
        int                     error;

        intmax_t               content_length;
        http1_chunk_state_t    chunk_state;
        uint64_t               chunk_length;

        bool is_request;
        bool is_chunked;
        bool is_http10;

        // decoded headers
        bool hdr_transfer_encoding;
        bool hdr_content_length;
        bool hdr_conn_close;      // Connection: close
        bool hdr_conn_keep_alive;  // Connection: keep-alive
    } decoder;

    // Encoder for current outgoing msg.
    // outgoing: holds the encoded data that needs to be sent to proactor for
    // sending out this connection
    // write_ptr: points to the first empty octet to be written to by the
    // encoder.  Remaining is the total unused space in the outgoing list
    // (capacity)
    // Note that the outgoing list and the write_ptr are only used for the
    // start line and headers.  Body content buffer chains are past directly to
    // the connection without encoding.
    //
    struct encoder_t {
        qd_buffer_list_t       outgoing;
        qd_iterator_pointer_t  write_ptr;
        h1_codec_request_state_t *hrs;           // current request/response state

        bool headers_sent;  // true after all headers have been sent
        bool is_request;
        bool is_chunked;

        char *boundary_marker;//used for multipart content

        // headers provided
        bool hdr_content_length;
    } encoder;

    h1_codec_config_t config;
};
ALLOC_DECLARE(h1_codec_connection_t);
ALLOC_DEFINE(h1_codec_connection_t);

static void decoder_reset(struct decoder_t *d);
static void encoder_reset(struct encoder_t *e);


// Create a new request state - this is done when a new http request occurs
// Keep oldest outstanding tranfer at DEQ_HEAD(conn->hrs_queue)
static h1_codec_request_state_t *h1_codec_request_state(h1_codec_connection_t *conn)
{
    h1_codec_request_state_t *hrs = new_h1_codec_request_state_t();
    ZERO(hrs);
    hrs->conn = conn;
    DEQ_INSERT_TAIL(conn->hrs_queue, hrs);
    return hrs;
}


static void h1_codec_request_state_free(h1_codec_request_state_t *hrs)
{
    if (hrs) {
        h1_codec_connection_t *conn = hrs->conn;
        assert(conn->decoder.hrs != hrs);
        assert(conn->encoder.hrs != hrs);
        DEQ_REMOVE(conn->hrs_queue, hrs);
        free(hrs->method);
        free_h1_codec_request_state_t(hrs);
    }
}


h1_codec_connection_t *h1_codec_connection(h1_codec_config_t *config, void *context)
{
    h1_codec_connection_t *conn = new_h1_codec_connection_t();
    ZERO(conn);

    conn->context = context;
    conn->config = *config;
    DEQ_INIT(conn->hrs_queue);

    encoder_reset(&conn->encoder);
    DEQ_INIT(conn->encoder.outgoing);
    conn->encoder.write_ptr = NULL_I_PTR;

    decoder_reset(&conn->decoder);
    DEQ_INIT(conn->decoder.incoming);
    conn->decoder.read_ptr = NULL_I_PTR;

    return conn;
}


// Free the connection
//
void h1_codec_connection_free(h1_codec_connection_t *conn)
{
    if (conn) {
        // expect application to cancel all requests!
        assert(DEQ_IS_EMPTY(conn->hrs_queue));
        decoder_reset(&conn->decoder);
        encoder_reset(&conn->encoder);
        qd_buffer_list_free_buffers(&conn->decoder.incoming);
        qd_buffer_list_free_buffers(&conn->encoder.outgoing);
        free(conn->decoder.scratch.buf);

        free_h1_codec_connection_t(conn);
    }
}


// reset the rx decoder state after message received
//
static void decoder_reset(struct decoder_t *decoder)
{
    // do not touch the read_ptr or incoming buffer list as they
    // track the current position in the incoming data stream

    decoder->body_ptr = NULL_I_PTR;
    decoder->hrs = 0;
    decoder->state = HTTP1_MSG_STATE_START;
    decoder->content_length = 0;
    decoder->chunk_state = HTTP1_CHUNK_HEADER;
    decoder->chunk_length = 0;
    decoder->error = 0;
    decoder->error_msg = 0;
    decoder->is_request = false;
    decoder->is_chunked = false;
    decoder->is_http10 = false;
    decoder->hdr_transfer_encoding = false;
    decoder->hdr_content_length = false;
    decoder->hdr_conn_close = false;
    decoder->hdr_conn_keep_alive = false;
}


// reset the tx encoder after message sent
static void encoder_reset(struct encoder_t *encoder)
{
    // do not touch the write_ptr or the outgoing queue as there may be more messages to send.
    encoder->hrs = 0;
    encoder->headers_sent = false;
    encoder->is_request = false;
    encoder->is_chunked = false;
    encoder->hdr_content_length = false;
    if (encoder->boundary_marker) {
        free(encoder->boundary_marker);
        encoder->boundary_marker = 0;
    }
}


// convert a string representation of a Content-Length value to
// and integer.  Return true if parse succeeds
//
static bool _parse_content_length(const char *clen, intmax_t *value)
{
    // a valid value is an integer >= 0
    *value = 0;
    return sscanf(clen, "%"PRIdMAX, value) == 1 && *value > -1;
}


// Scan the value of a Transfer-Encoding header to see if the
// last encoding is chunked
//
static bool _is_transfer_chunked(const char *encoding)
{
    // "chunked" must be the last item in the value string.  And remember kids:
    // coding type names are case insensitive!
    //
    size_t len = strlen(encoding);
    if (len >= 7) {  // 7 = strlen("chunked")
        const char *ptr = encoding + len - 7;
        return strcasecmp("chunked", ptr) == 0;
    }
    return false;
}


// ensure the encoder has at least capacity octets available
//
static void ensure_outgoing_capacity(struct encoder_t *encoder, size_t capacity)
{
    while (encoder->write_ptr.remaining < capacity) {
        qd_buffer_t *buf = qd_buffer();
        DEQ_INSERT_TAIL(encoder->outgoing, buf);
        encoder->write_ptr.remaining += qd_buffer_capacity(buf);
    }
    if (!encoder->write_ptr.buffer) {
        encoder->write_ptr.buffer = DEQ_HEAD(encoder->outgoing);
        encoder->write_ptr.cursor = qd_buffer_cursor(encoder->write_ptr.buffer);
    }
}


// Write a C string to the encoder.
//
static void write_string(struct encoder_t *encoder, const char *string)
{
    size_t needed = strlen(string);
    if (needed == 0) return;

    ensure_outgoing_capacity(encoder, needed);

    encoder->hrs->out_octets += needed;
    qd_iterator_pointer_t *wptr = &encoder->write_ptr;
    while (needed) {
        if (qd_buffer_capacity(wptr->buffer) == 0) {
            wptr->buffer = DEQ_NEXT(wptr->buffer);
            wptr->cursor = qd_buffer_base(wptr->buffer);
        }

        size_t avail = MIN(needed, qd_buffer_capacity(wptr->buffer));
        memcpy(wptr->cursor, string, avail);
        qd_buffer_insert(wptr->buffer, avail);
        wptr->cursor += avail;
        wptr->remaining -= avail;
        string += avail;
        needed -= avail;
    }
}


//
static inline size_t skip_octets(qd_iterator_pointer_t *data, size_t amount)
{
    size_t count = 0;
    amount = MIN(data->remaining, amount);
    while (count < amount) {
        if (data->cursor == qd_buffer_cursor(data->buffer)) {
            data->buffer = DEQ_NEXT(data->buffer);
            assert(data->buffer);  // else data->remaining is bad
            data->cursor = qd_buffer_base(data->buffer);
        }
        size_t available = qd_buffer_cursor(data->buffer) - data->cursor;
        available = MIN(available, amount - count);
        data->cursor += available;
        count += available;
    }
    data->remaining -= amount;
    return amount;
}

// consume next octet and advance the pointer
static inline bool get_octet(qd_iterator_pointer_t *data, uint8_t *octet)
{
    if (data->remaining > 0) {
        if (data->cursor == qd_buffer_cursor(data->buffer)) {
            data->buffer = DEQ_NEXT(data->buffer);
            data->cursor = qd_buffer_base(data->buffer);
        }
        *octet = *data->cursor;
        data->cursor += 1;
        data->remaining -= 1;
        return true;
    }
    return false;
}


// True if line contains just "CRLF"
//
static bool is_empty_line(const qd_iterator_pointer_t *line)
{
    if (line->remaining == 2) {
        qd_iterator_pointer_t tmp = *line;
        uint8_t octet;
        return (get_octet(&tmp, &octet) && octet == CR_TOKEN
                && get_octet(&tmp, &octet) && octet == LF_TOKEN);
    }
    return false;
}


// for debug:
static void debug_print_iterator_pointer(const char *prefix, const qd_iterator_pointer_t *ptr)
{
#if 0
    qd_iterator_pointer_t tmp = *ptr;
    fprintf(stdout, "%s '", prefix);
    size_t len = MIN(tmp.remaining, 80);
    uint8_t octet;
    while (len-- > 0 && get_octet(&tmp, &octet)) {
        fputc(octet, stdout);
    }
    fprintf(stdout, "%s'\n", (tmp.remaining) ? " <truncated>" : "");
    fflush(stdout);
#endif
}


// read a CRLF terminated line starting at 'data'.
// On success, 'data' is advanced to the octet following the LF and 'line' is
// set to the read line (including trailing CRLF).  Returns false if no CRLF found
//
static bool read_line(qd_iterator_pointer_t *data, qd_iterator_pointer_t *line)
{
    qd_iterator_pointer_t tmp = *data;

    *line = *data;
    line->remaining = 0;

    bool   eol = false;

    uint8_t octet;
    while (!eol && get_octet(&tmp, &octet)) {
        line->remaining += 1;
        if (octet == CR_TOKEN) {
            if (get_octet(&tmp, &octet)) {
                line->remaining += 1;
                if (octet == LF_TOKEN) {
                    eol = true;
                }
            }
        }
    }

    if (eol) {
        *data = tmp;
        return true;
    } else {
        *line = NULL_I_PTR;
        return false;
    }
}


static bool ensure_scratch_size(scratch_memory_t *b, size_t required)
{
    if (b->size < required) {
        if (b->buf)
            free(b->buf);
        b->size = required;
        b->buf = malloc(b->size);
    }

    // @TODO(kgiusti): deal with malloc failure
    return true;
}


// return true if octet in str
static inline bool filter_str(const char *str, uint8_t octet)
{
    const char *ptr = strchr(str, (int)((unsigned int)octet));
    return ptr && *ptr != 0;
}


// trims any optional whitespace characters at the start of 'line'
// RFC7230 defines OWS as zero or more spaces or horizontal tabs
//
static void trim_whitespace(qd_iterator_pointer_t *line)
{
    qd_iterator_pointer_t ptr = *line;
    size_t skip = 0;
    uint8_t octet;
    while (get_octet(&ptr, &octet) && isblank(octet))
        skip += 1;
    if (skip)
        skip_octets(line, skip);
}

// copy out iterator to a buffer and null terminate. Return # of bytes written
// to str including terminating null.
static size_t pointer_2_str(const qd_iterator_pointer_t *line, unsigned char *str, size_t len)
{
    assert(len);
    qd_iterator_pointer_t tmp = *line;
    uint8_t *ptr = (uint8_t *)str;
    len -= 1;  // reserve for null terminator
    while (len-- > 0 && get_octet(&tmp, ptr))
        ++ptr;
    *ptr++ = 0;
    return ptr - (uint8_t *)str;
}


// Parse out a token as defined by RFC7230 and store the result in 'token'.
// 'line' is advanced past the token.  This is used for parsing fields that
// RFC7230 defines as 'tokens'.
//
static bool parse_token(qd_iterator_pointer_t *line, qd_iterator_pointer_t *token)
{
    static const char *TOKEN_EXTRA = "!#$%&’*+-.^_‘|~";

    trim_whitespace(line);
    qd_iterator_pointer_t tmp = *line;
    *token = tmp;
    size_t len = 0;
    uint8_t octet;
    while (get_octet(&tmp, &octet)
           && (('A' <= octet && octet <= 'Z') ||
               ('a' <= octet && octet <= 'z') ||
               ('0' <= octet && octet <= '9') ||
               (filter_str(TOKEN_EXTRA, octet)))) {
        len++;
    }

    if (len) {
        token->remaining = len;
        skip_octets(line, len);
        return true;
    }
    *token = NULL_I_PTR;
    return false;
}


// Parse out a text field delineated by whitespace.
// 'line' is advanced past the field.
//
static bool parse_field(qd_iterator_pointer_t *line, qd_iterator_pointer_t *field)
{
    trim_whitespace(line);
    qd_iterator_pointer_t tmp = *line;
    *field = tmp;
    size_t len = 0;
    uint8_t octet;
    while (get_octet(&tmp, &octet) && !isspace(octet))
        len++;

    if (len) {
        field->remaining = len;
        skip_octets(line, len);
        return true;
    }
    *field = NULL_I_PTR;
    return false;
}


// parse the HTTP/1.1 request line:
// "method SP request-target SP HTTP-version CRLF"
//
static bool parse_request_line(h1_codec_connection_t *conn, struct decoder_t *decoder, qd_iterator_pointer_t *line)
{
    qd_iterator_pointer_t method = {0};
    qd_iterator_pointer_t target = {0};
    qd_iterator_pointer_t version = {0};
    int in_octets = line->remaining;

    if (!parse_token(line, &method) ||
        !parse_field(line, &target) ||
        !parse_field(line, &version)) {

        decoder->error_msg = "Malformed request line";
        decoder->error = HTTP1_STATUS_BAD_REQ;
        return decoder->error;
    }

    // translate iterator pointers to C strings
    ensure_scratch_size(&decoder->scratch, method.remaining + target.remaining + version.remaining + 3);
    uint8_t *ptr = decoder->scratch.buf;
    size_t avail = decoder->scratch.size;

    uint8_t *method_str = ptr;
    size_t offset = pointer_2_str(&method, method_str, avail);
    ptr += offset;
    avail -= offset;

    uint8_t *target_str = ptr;
    offset = pointer_2_str(&target, target_str, avail);
    ptr += offset;
    avail += offset;

    uint8_t *version_str = ptr;
    pointer_2_str(&version, version_str, avail);

    uint32_t major = 0;
    uint32_t minor = 0;
    if (sscanf((char*)version_str, "HTTP/%"SCNu32".%"SCNu32, &major, &minor) != 2) {
        decoder->error_msg = "Malformed version in request";
        decoder->error = HTTP1_STATUS_BAD_REQ;
        return decoder->error;
    }

    if (major != 1 || minor > 1) {
        decoder->error_msg = "Unsupported HTTP version";
        decoder->error = HTTP1_STATUS_BAD_VERSION;
        return decoder->error;
    }

    decoder->is_http10 = minor == 0;

    h1_codec_request_state_t *hrs = h1_codec_request_state(conn);

    // check for methods that do not support body content in the response:
    hrs->no_body_method = (strcmp((char*)method_str, "HEAD") == 0 ||
                           strcmp((char*)method_str, "CONNECT") == 0);

    hrs->method = qd_strdup((char*) method_str);
    hrs->in_octets += in_octets;

    decoder->hrs = hrs;
    decoder->is_request = true;

    decoder->error = conn->config.rx_request(hrs, (char*)method_str, (char*)target_str, major, minor);
    if (decoder->error)
        decoder->error_msg = "hrs_rx_request callback error";
    return decoder->error;
}


// parse the HTTP/1.1 response line
// "HTTP-version SP status-code [SP reason-phrase] CRLF"
//
static int parse_response_line(h1_codec_connection_t *conn, struct decoder_t *decoder, qd_iterator_pointer_t *line)
{
    qd_iterator_pointer_t version = {0};
    qd_iterator_pointer_t status_code = {0};
    qd_iterator_pointer_t reason = {0};
    int in_octets = line->remaining;

    if (!parse_field(line, &version)
        || !parse_field(line, &status_code)
        || status_code.remaining != 3) {

        decoder->error_msg = "Malformed response status line";
        decoder->error = HTTP1_STATUS_SERVER_ERR;
        return decoder->error;
    }

    // Responses arrive in the same order as requests are generated so this new
    // response corresponds to head hrs
    h1_codec_request_state_t *hrs = DEQ_HEAD(conn->hrs_queue);
    if (!hrs) {
        // receiving a response without a corresponding request
        decoder->error_msg = "Spurious HTTP response received";
        decoder->error = HTTP1_STATUS_SERVER_ERR;
        return decoder->error;
    }

    assert(!decoder->hrs);   // state machine violation
    assert(hrs->response_code == 0);

    hrs->in_octets += in_octets;
    decoder->hrs = hrs;

    unsigned char code_str[4];
    pointer_2_str(&status_code, code_str, 4);
    hrs->response_code = atoi((char*) code_str);

    // the reason phrase is optional, and may contain spaces

    reason = *line;
    if (reason.remaining >= 2)  // expected for CRLF
        reason.remaining -= 2;
    trim_whitespace(&reason);

    // convert to C strings
    ensure_scratch_size(&decoder->scratch, version.remaining + reason.remaining + 2);
    uint8_t *ptr = decoder->scratch.buf;
    size_t avail = decoder->scratch.size;

    uint8_t *version_str = ptr;
    size_t offset = pointer_2_str(&version, version_str, avail);
    ptr += offset;
    avail -= offset;

    uint8_t *reason_str = ptr;
    offset = pointer_2_str(&reason, reason_str, avail);

    uint32_t major = 0;
    uint32_t minor = 0;
    if (sscanf((char*)version_str, "HTTP/%"SCNu32".%"SCNu32, &major, &minor) != 2) {
        decoder->error_msg = "Malformed version in response";
        decoder->error = HTTP1_STATUS_SERVER_ERR;
        return decoder->error;
    }

    if (major != 1 || minor > 1) {
        decoder->error_msg = "Unsupported HTTP version";
        decoder->error = HTTP1_STATUS_BAD_VERSION;
        return decoder->error;
    }

    decoder->is_request = false;
    decoder->is_http10 = minor == 0;

    decoder->error = conn->config.rx_response(decoder->hrs,
                                              hrs->response_code,
                                              (offset) ? (char*)reason_str: 0,
                                              major, minor);
    if (decoder->error)
        decoder->error_msg = "hrs_rx_response callback error";

    return decoder->error;
}


// parse the first line of an incoming http message
//
static bool parse_start_line(h1_codec_connection_t *conn, struct decoder_t *decoder)
{
    qd_iterator_pointer_t *rptr = &decoder->read_ptr;
    qd_iterator_pointer_t line;

    if (read_line(rptr, &line)) {
        debug_print_iterator_pointer("start line:", &line);

        if (!is_empty_line(&line)) {  // RFC7230: ignore any preceding CRLF
            if (conn->config.type == HTTP1_CONN_CLIENT) {
                parse_request_line(conn, decoder, &line);
            } else {
                parse_response_line(conn, decoder, &line);
            }
            conn->decoder.state = HTTP1_MSG_STATE_HEADERS;
        }
        return !!rptr->remaining;
    }

    return false;  // pend for more input
}

//
// Header parsing
//

// Called after the last incoming header was decoded and passed to the
// application
//
static bool process_headers_done(h1_codec_connection_t *conn, struct decoder_t *decoder)
{
    // Flush all buffers processed so far - no longer needed

    qd_buffer_t *head = DEQ_HEAD(decoder->incoming);
    while (head && head != decoder->read_ptr.buffer) {
        DEQ_REMOVE_HEAD(decoder->incoming);
        qd_buffer_free(head);
        head = DEQ_HEAD(decoder->incoming);
    }

    // perform any post-headers validation:

    if (decoder->is_request) {
        if (decoder->hdr_transfer_encoding && !decoder->is_chunked) {
            // RFC7230 Message Body Length: If a Transfer-Encoding header field
            // is present in a request and the chunked transfer coding is not
            // the final encoding, the message body length cannot be determined
            // reliably; the server MUST respond with the 400 (Bad Request)
            // status code and then close the connection.
            decoder->error_msg = "Non-chunked Tranfer-Encoding in request";
            decoder->error = HTTP1_STATUS_BAD_REQ;
            return false;
        }
    }

    // determine if a body is present (ref RFC7230 sec 3.3.3 Message Body Length)
    bool has_body;
    if (decoder->is_request) {
        // an http request will have a body ONLY if either chunked transfer or
        // non-zero Content-Length header was given.
        has_body = (decoder->is_chunked || decoder->content_length);

    } else {
        // An HTTP response has a body if request method is NOT HEAD or CONNECT AND
        // the response code indicates a body.  A body will either have a specific
        // size via Content-Length or chunked encoder, OR its length is unspecified
        // and the message body is terminated by closing the connection.
        //
        h1_codec_request_state_t *hrs = decoder->hrs;
        has_body = !(hrs->no_body_method || NO_BODY_RESPONSE(hrs->response_code));
        if (has_body) {
            // no body if explicit Content-Length of zero
            if (decoder->hdr_content_length && decoder->content_length == 0) {
                has_body = false;
            }
        }

        // In certain scenarios an HTTP server will close the connection to
        // indicate the end of a response message. This may happen even if
        // the request message has a known length (Content-Length or
        // Transfer-Encoding).  In these circumstances do NOT signal that
        // the request is complete (call request_complete() callback) until
        // the connection closes.  Otherwise the user may start sending the
        // next request message before the HTTP server closes the TCP
        // connection.  (see RFC7230, section Persistence)
        hrs->close_expected = decoder->hdr_conn_close
            || (decoder->is_http10 && !decoder->hdr_conn_keep_alive);
    }

    decoder->error = conn->config.rx_headers_done(decoder->hrs, has_body);
    if (decoder->error) {
        decoder->error_msg = "hrs_rx_headers_done callback error";
        return false;
    }

    if (has_body) {
        // start tracking the body buffer chain
        decoder->body_ptr = decoder->read_ptr;
        decoder->body_ptr.remaining = 0;
        decoder->state = HTTP1_MSG_STATE_BODY;
    } else {
        decoder->state = HTTP1_MSG_STATE_DONE;
    }

    return !!decoder->read_ptr.remaining;
}


// process a received header to determine message body length, etc.
//
static int process_header(h1_codec_connection_t *conn, struct decoder_t *decoder, const uint8_t *key, const uint8_t *value)
{
    int parse_error = decoder->is_request ? HTTP1_STATUS_BAD_REQ : HTTP1_STATUS_SERVER_ERR;

    if (strcasecmp("Content-Length", (char*) key) == 0) {
        intmax_t old = decoder->content_length;
        if (!_parse_content_length((char*) value, &decoder->content_length)) {
            decoder->error_msg = "Malformed Content-Length header";
            decoder->error = parse_error;
            return decoder->error;
        }
        if (old && old != decoder->content_length) {
            decoder->error_msg = "Invalid duplicate Content-Length header";
            decoder->error = parse_error;
            return decoder->error;
        }
        decoder->hdr_content_length = true;

    } else if (strcasecmp("Transfer-Encoding", (char*) key) == 0) {
        decoder->is_chunked = _is_transfer_chunked((char*) value);
        decoder->hdr_transfer_encoding = true;

    } else if (strcasecmp("Connection", (char*) key) == 0) {
        // parse out connection lifecycle options
        const char *token = 0;
        size_t len = 0;
        const char *next = (const char*) value;
        while (*next && (token = h1_codec_token_list_next(next, &len, &next)) != 0) {
            if (len == 5 && strncmp("close", token, 5) == 0) {
                decoder->hdr_conn_close = true;
            } else if (len == 10 && strncmp("keep-alive", token, 10) == 0) {
                decoder->hdr_conn_keep_alive = true;
            }
        }
    }

    return 0;
}


// Parse an HTTP header line.
// See RFC7230 for details.  If header line folding (obs-folding) is detected,
// replace the folding with spaces.
//
static bool parse_header(h1_codec_connection_t *conn, struct decoder_t *decoder)
{
    qd_iterator_pointer_t end_ptr = decoder->read_ptr;
    h1_codec_request_state_t *hrs = decoder->hrs;
    qd_iterator_pointer_t line;

    assert(hrs);  // else state machine busted

    if (!read_line(&end_ptr, &line))
        // need more data
        return false;

    if (is_empty_line(&line)) {
        decoder->read_ptr = end_ptr;
        hrs->in_octets += line.remaining;
        return process_headers_done(conn, decoder);
    }

    // check for header line folding

    bool obs_fold = false;
    while (true) {
        qd_iterator_pointer_t peek = end_ptr;
        uint8_t octet;
        if (!get_octet(&peek, &octet))
            // need more data
            return false;

        if (octet != ' ' && octet != '\t')
            break;

        obs_fold = true;

        if (!read_line(&end_ptr, &line))
            return false;
    }

    // end_ptr now points past the header line, advance decoder past header
    // line and set 'line' to hold header

    line = decoder->read_ptr;
    decoder->read_ptr = end_ptr;
    line.remaining -= end_ptr.remaining;

    debug_print_iterator_pointer("header:", &line);

    hrs->in_octets += line.remaining;

    // convert field to key and value strings

    qd_iterator_pointer_t key;
    if (!parse_token(&line, &key)) {
        decoder->error_msg = "Malformed Header";
        decoder->error = (decoder->is_request) ? HTTP1_STATUS_BAD_REQ
            : HTTP1_STATUS_SERVER_ERR;
        return false;
    }

    // advance line past the ':'
    uint8_t octet;
    while (get_octet(&line, &octet) && octet != ':')
        ;

    // line now contains the value. convert to C strings and post callback
    ensure_scratch_size(&decoder->scratch, key.remaining + line.remaining + 2);
    uint8_t *ptr = decoder->scratch.buf;
    size_t avail = decoder->scratch.size;

    uint8_t *key_str = ptr;
    size_t offset = pointer_2_str(&key, key_str, avail);
    ptr += offset;
    avail -= offset;

    uint8_t *value_str = ptr;
    pointer_2_str(&line, value_str, avail);

    // trim whitespace on both ends of value
    while (isspace(*value_str))
        ++value_str;
    ptr = value_str + strlen((char*) value_str);
    while (ptr-- > value_str) {
        if (!isspace(*ptr))
            break;
        *ptr = 0;
    }

    // remove header line folding by overwriting all <CR> and <LF> chars with
    // spaces as per RFC7230

    if (obs_fold) {
        ptr = value_str;
        while ((ptr = (uint8_t*) strpbrk((char*) ptr, CRLF)) != 0)
            *ptr = ' ';
    }

    process_header(conn, decoder, key_str, value_str);

    if (!decoder->error) {
        decoder->error = conn->config.rx_header(hrs, (char *)key_str, (char *)value_str);
        if (decoder->error)
            decoder->error_msg = "hrs_rx_header callback error";
    }

    return !!decoder->read_ptr.remaining;
}


//
// Chunked body encoding parser
//


// Pass message body data up to the application.
//
static inline int consume_stream_data(h1_codec_connection_t *conn, bool flush)
{
    struct decoder_t       *decoder = &conn->decoder;
    qd_iterator_pointer_t *body_ptr = &decoder->body_ptr;
    qd_iterator_pointer_t     *rptr = &decoder->read_ptr;
    qd_buffer_list_t          blist = DEQ_EMPTY;
    size_t                   octets = 0;

    // invariant:
    assert(DEQ_HEAD(decoder->incoming) == body_ptr->buffer);

    // The read pointer points to somewhere in the buffer chain that contains some
    // unparsed data.  Send any buffers preceding the current read pointer.
    while (body_ptr->remaining) {

        if (body_ptr->buffer == rptr->buffer && rptr->remaining > 0)
            break;

        DEQ_REMOVE_HEAD(decoder->incoming);

        size_t offset = body_ptr->cursor - qd_buffer_base(body_ptr->buffer);
        if (offset) {
            // most (all?) message buffer operations assume the message
            // data starts at the buffer_base. Adjust accordingly
            memmove(qd_buffer_base(body_ptr->buffer),
                    qd_buffer_base(body_ptr->buffer) + offset,
                    qd_buffer_size(body_ptr->buffer) - offset);
            body_ptr->cursor = qd_buffer_base(body_ptr->buffer);
            body_ptr->buffer->size -= offset;
        }

        if (qd_buffer_size(body_ptr->buffer) > 0) {
            DEQ_INSERT_TAIL(blist, body_ptr->buffer);
            octets += qd_buffer_size(body_ptr->buffer);
            body_ptr->remaining -= qd_buffer_size(body_ptr->buffer);
        } else {
            qd_buffer_free(body_ptr->buffer);
        }
        body_ptr->buffer = DEQ_HEAD(decoder->incoming);
        body_ptr->cursor = body_ptr->buffer ? qd_buffer_base(body_ptr->buffer) : 0;
    }

    // invariant:
    assert(body_ptr->remaining >= 0);

    // At this point if there is any body bytes remaining they are in the same
    // buffer as the unparsed input (rptr).

    if (flush && body_ptr->remaining) {
        // need to copy out remaining body octets into new buffer
        qd_buffer_t *tail = qd_buffer();

        assert(body_ptr->remaining <= qd_buffer_capacity(tail));
        memcpy(qd_buffer_cursor(tail), body_ptr->cursor, body_ptr->remaining);
        qd_buffer_insert(tail, body_ptr->remaining);
        DEQ_INSERT_TAIL(blist, tail);
        octets += body_ptr->remaining;
        *body_ptr = *rptr;
        body_ptr->remaining = 0;
    }

    if (octets) {
        decoder->hrs->in_octets += octets;
        decoder->error = conn->config.rx_body(decoder->hrs, &blist, octets, true);
    }
    return decoder->error;
}



// parsing the start of a chunked header:
// <chunk size in hex>CRLF
//
static bool parse_body_chunked_header(h1_codec_connection_t *conn, struct decoder_t *decoder)
{
    qd_iterator_pointer_t *rptr = &decoder->read_ptr;
    qd_iterator_pointer_t line;

    assert(decoder->chunk_state == HTTP1_CHUNK_HEADER);
    assert(decoder->chunk_length == 0);

    if (read_line(rptr, &line)) {
        decoder->body_ptr.remaining += line.remaining;

        ensure_scratch_size(&decoder->scratch, line.remaining + 1);
        uint8_t *ptr = decoder->scratch.buf;
        pointer_2_str(&line, (unsigned char*) ptr, line.remaining + 1);
        int rc = sscanf((char*) ptr, "%"SCNx64, &decoder->chunk_length);
        if (rc != 1) {
            decoder->error_msg = "Invalid chunk header";
            decoder->error = (decoder->is_request) ? HTTP1_STATUS_BAD_REQ
                : HTTP1_STATUS_SERVER_ERR;
            return false;
        }

        if (decoder->chunk_length == 0) {
            // last chunk
            decoder->chunk_state = HTTP1_CHUNK_TRAILERS;
        } else {
            decoder->chunk_state = HTTP1_CHUNK_DATA;

            // chunk_length does not include the CRLF trailer:
            decoder->chunk_length += 2;
        }


        return !!rptr->remaining;
    }

    return false;  // pend for more input
}


// Parse the data section of a chunk
//
static bool parse_body_chunked_data(h1_codec_connection_t *conn, struct decoder_t *decoder)
{
    qd_iterator_pointer_t *rptr = &decoder->read_ptr;
    qd_iterator_pointer_t *body_ptr = &decoder->body_ptr;

    assert(decoder->chunk_state == HTTP1_CHUNK_DATA);

    size_t skipped = skip_octets(rptr, decoder->chunk_length);
    decoder->chunk_length -= skipped;
    body_ptr->remaining += skipped;

    consume_stream_data(conn, false);

    if (decoder->chunk_length == 0) {
        // end of chunk
        decoder->chunk_state = HTTP1_CHUNK_HEADER;
    }

    return !!rptr->remaining;
}


// Keep reading chunk trailers until the terminating empty line is read
//
static bool parse_body_chunked_trailer(h1_codec_connection_t *conn, struct decoder_t *decoder)
{
    qd_iterator_pointer_t *rptr = &decoder->read_ptr;
    qd_iterator_pointer_t *body_ptr = &decoder->body_ptr;
    qd_iterator_pointer_t line;

    assert(decoder->chunk_state == HTTP1_CHUNK_TRAILERS);

    if (read_line(rptr, &line)) {
        body_ptr->remaining += line.remaining;
        if (is_empty_line(&line)) {
            // end of message
            consume_stream_data(conn, true);
            decoder->state = HTTP1_MSG_STATE_DONE;
        }

        return !!rptr->remaining;
    }

    return false;  // pend for full line
}


// parse an incoming message body which is chunk encoded
// Return True if there is more data pending to parse
static bool parse_body_chunked(h1_codec_connection_t *conn, struct decoder_t *decoder)
{
    bool more = true;
    switch (decoder->chunk_state) {

    case HTTP1_CHUNK_HEADER:
        more = parse_body_chunked_header(conn, decoder);
        break;

    case HTTP1_CHUNK_DATA:
        more = parse_body_chunked_data(conn, decoder);
        break;

    case HTTP1_CHUNK_TRAILERS:
        more = parse_body_chunked_trailer(conn, decoder);
        break;
    } // end switch

    return more;
}


// parse an incoming message body which is Content-Length bytes long
//
static bool parse_body_content(h1_codec_connection_t *conn, struct decoder_t *decoder)
{
    qd_iterator_pointer_t *rptr = &decoder->read_ptr;
    qd_iterator_pointer_t *body_ptr = &decoder->body_ptr;

    size_t skipped = skip_octets(rptr, decoder->content_length);
    decoder->content_length -= skipped;
    body_ptr->remaining += skipped;
    bool eom = decoder->content_length == 0;

    consume_stream_data(conn, eom);
    if (eom)
        decoder->state = HTTP1_MSG_STATE_DONE;

    return !!rptr->remaining;
}


static bool parse_body(h1_codec_connection_t *conn, struct decoder_t *decoder)
{
    if (decoder->is_chunked)
        return parse_body_chunked(conn, decoder);

    if (decoder->content_length)
        return parse_body_content(conn, decoder);

    // otherwise no explict body size, so just keep passing the entire unparsed
    // incoming chain along until the remote closes the connection
    if (decoder->read_ptr.remaining) {
        // extend body_ptr to consume all unparsed read data
        decoder->body_ptr.remaining += decoder->read_ptr.remaining;
        decoder->read_ptr.remaining = 0;
        decoder->read_ptr.buffer = DEQ_TAIL(decoder->incoming);
        decoder->read_ptr.cursor = qd_buffer_cursor(decoder->read_ptr.buffer);
        consume_stream_data(conn, true);
        decoder->body_ptr = decoder->read_ptr = NULL_I_PTR;
        DEQ_INIT(decoder->incoming);
    }

    return false;
}


// Called when incoming message is complete
//
static bool parse_done(h1_codec_connection_t *conn, struct decoder_t *decoder)
{
    h1_codec_request_state_t *hrs = decoder->hrs;
    bool is_response = !decoder->is_request;

    // signal the message receive is complete
    conn->config.rx_done(hrs);

    if (is_response) {
        // Informational 1xx response codes are NOT terminal - further responses are allowed!
        if (IS_INFO_RESPONSE(hrs->response_code)) {
            hrs->response_code = 0;
        } else {
            hrs->response_complete = true;
        }
    } else {
        hrs->request_complete = true;
    }

    decoder_reset(decoder);

    if (!hrs->close_expected) {
        if (hrs->request_complete && hrs->response_complete) {
            conn->config.request_complete(hrs, false);
            h1_codec_request_state_free(hrs);
        }
        return !!decoder->read_ptr.remaining;
    } else
        return false;  // stop parsing input, wait for close
}


// Main decode loop.
// Process received data until it is exhausted
//
static int decode_incoming(h1_codec_connection_t *conn)
{
    struct decoder_t *decoder = &conn->decoder;
    bool more = true;
    while (more && !decoder->error) {

        if (decoder->state == HTTP1_MSG_STATE_START)
            more = parse_start_line(conn, decoder);

        else if (decoder->state == HTTP1_MSG_STATE_HEADERS)
            more = parse_header(conn, decoder);

        else if (decoder->state ==  HTTP1_MSG_STATE_BODY)
            more = parse_body(conn, decoder);

        // Can reach DONE from any call above.
        if (decoder->state == HTTP1_MSG_STATE_DONE)
            more = parse_done(conn, decoder);
    }

    return decoder->error;
}


void *h1_codec_connection_get_context(h1_codec_connection_t *conn)
{
    return conn->context;
}

// Push inbound network data into the http1 protocol engine.
//
// All of the hrs_rx callback will occur in the context of this call. This
// returns zero on success otherwise an error code.  Any error occuring during
// a callback will be reflected in the return value of this function.
//
int h1_codec_connection_rx_data(h1_codec_connection_t *conn, qd_buffer_list_t *data, size_t len)
{
    struct decoder_t *decoder = &conn->decoder;
    bool init_ptrs = DEQ_IS_EMPTY(decoder->incoming);

    DEQ_APPEND(decoder->incoming, *data);

    if (init_ptrs) {
        decoder->read_ptr.buffer = DEQ_HEAD(decoder->incoming);
        decoder->read_ptr.cursor = qd_buffer_base(decoder->read_ptr.buffer);
        decoder->read_ptr.remaining = len;

        if (decoder->state == HTTP1_MSG_STATE_BODY) {
            decoder->body_ptr = decoder->read_ptr;
            decoder->body_ptr.remaining = 0;
        }
    } else {
        decoder->read_ptr.remaining += len;
    }

    return decode_incoming(conn);
}


// The read channel of the connection has closed.  If this is a connection to a
// server this may simply be the end of the response message.  If a message is
// currently being decoded see if it is valid to complete.
//
void h1_codec_connection_rx_closed(h1_codec_connection_t *conn)
{
    if (conn && conn->config.type == HTTP1_CONN_SERVER) {

        // terminate the in progress decode

        struct decoder_t *decoder = &conn->decoder;
        h1_codec_request_state_t *hrs = decoder->hrs;
        if (hrs) {
            // consider the response complete if length is unspecified since in
            // this case the server must close the connection to complete the
            // message body.  However if the response message is a "continue"
            // then the final response never arrived and the response is
            // incomplete
            if (decoder->state == HTTP1_MSG_STATE_BODY
                && !IS_INFO_RESPONSE(hrs->response_code)
                && !decoder->is_chunked
                && !decoder->hdr_content_length) {

                if (!hrs->response_complete) {
                    hrs->response_complete = true;
                    conn->config.rx_done(hrs);
                }
            }
        }

        decoder_reset(decoder);
        // since the underlying connection is gone discard all remaining
        // incoming data
        qd_buffer_list_free_buffers(&conn->decoder.incoming);
        decoder->read_ptr = NULL_I_PTR;

        // check if current request is completed
        hrs = DEQ_HEAD(conn->hrs_queue);
        if (hrs) {
            hrs->close_expected = false;   // the close just occurred
            if (hrs->response_complete && hrs->request_complete) {
                conn->config.request_complete(hrs, false);
                h1_codec_request_state_free(hrs);
            }
        }
    }
}


void h1_codec_request_state_set_context(h1_codec_request_state_t *hrs, void *context)
{
    hrs->context = context;
}


void *h1_codec_request_state_get_context(const h1_codec_request_state_t *hrs)
{
    return hrs->context;
}


h1_codec_connection_t *h1_codec_request_state_get_connection(const h1_codec_request_state_t *hrs)
{
    return hrs->conn;
}


const char *h1_codec_request_state_method(const h1_codec_request_state_t *hrs)
{
    return hrs->method;
}

const uint32_t h1_codec_request_state_response_code(const h1_codec_request_state_t *hrs)
{
    return hrs->response_code;
}


void h1_codec_request_state_cancel(h1_codec_request_state_t *hrs)
{
    if (hrs) {
        h1_codec_connection_t *conn = hrs->conn;
        if (hrs == conn->decoder.hrs) {
            decoder_reset(&conn->decoder);
        }
        if (hrs == conn->encoder.hrs) {
            encoder_reset(&conn->encoder);
        }
        conn->config.request_complete(hrs, true);
        h1_codec_request_state_free(hrs);
    }
}


// initiate a new HTTP request.  This creates a new request state.
// request = <method>SP<target>SP<version>CRLF
//
h1_codec_request_state_t *h1_codec_tx_request(h1_codec_connection_t *conn, const char *method, const char *target,
                                              uint32_t version_major, uint32_t version_minor)
{
    struct encoder_t *encoder = &conn->encoder;
    assert(!encoder->hrs);   // error: transfer already in progress
    assert(conn->config.type == HTTP1_CONN_SERVER);

    h1_codec_request_state_t *hrs = encoder->hrs = h1_codec_request_state(conn);
    encoder->is_request = true;
    encoder->headers_sent = false;

    hrs->method = qd_strdup(method);

    // check for methods that do not support body content in the response:
    hrs->no_body_method = (strcmp((char*)method, "HEAD") == 0 ||
                           strcmp((char*)method, "CONNECT") == 0);

    write_string(encoder, method);
    write_string(encoder, " ");
    write_string(encoder, target);
    write_string(encoder, " ");
    {
        char version[64];
        snprintf(version, 64, "HTTP/%"PRIu32".%"PRIu32, version_major, version_minor);
        write_string(encoder, version);
    }
    write_string(encoder, CRLF);

    return hrs;
}


// Send an HTTP response msg.  hrs must correspond to the "oldest" outstanding
// request that arrived via the hrs_rx_request callback for this connection.
// status_code is expected to be 100 <= status_code <= 999
// status-line = HTTP-version SP status-code SP reason-phrase CRLF
//
int h1_codec_tx_response(h1_codec_request_state_t *hrs, int status_code, const char *reason_phrase,
                         uint32_t version_major, uint32_t version_minor)
{
    h1_codec_connection_t *conn = h1_codec_request_state_get_connection(hrs);
    struct encoder_t *encoder = &conn->encoder;

    assert(conn->config.type == HTTP1_CONN_CLIENT);
    assert(!encoder->hrs);   // error: transfer already in progress
    assert(DEQ_HEAD(conn->hrs_queue) == hrs);   // error: response not in order!
    assert(hrs->response_code == 0);

    encoder->hrs = hrs;
    encoder->is_request = false;
    encoder->headers_sent = false;
    hrs->response_code = status_code;

    {
        char version[64];
        snprintf(version, 64, "HTTP/%"PRIu32".%"PRIu32, version_major, version_minor);
        write_string(encoder, version);
    }

    write_string(encoder, " ");

    {
        char code_str[32];
        snprintf(code_str, 32, "%d", status_code);
        write_string(encoder, code_str);
    }

    if (reason_phrase) {
        write_string(encoder, " ");
        write_string(encoder, reason_phrase);
    }
    write_string(encoder, CRLF);

    return 0;
}


// Add a header field to an outgoing message
// header-field   = field-name ":" OWS field-value OWS
int h1_codec_tx_add_header(h1_codec_request_state_t *hrs, const char *key, const char *value)
{
    h1_codec_connection_t *conn = h1_codec_request_state_get_connection(hrs);
    struct encoder_t *encoder = &conn->encoder;
    assert(encoder->hrs == hrs);  // hrs not current transfer

    write_string(encoder, key);
    write_string(encoder, ": ");
    write_string(encoder, value);
    write_string(encoder, CRLF);

    // determine if the body length is provided. If not
    // the caller will have to close the connection
    //
    if (strcasecmp("Content-Length", (char*) key) == 0) {
        encoder->hdr_content_length = true;
    } else if (strcasecmp("Transfer-Encoding", (char *)key) == 0) {
        encoder->is_chunked = _is_transfer_chunked(value);
    }

    // check to see if there are any full buffers that can be sent.

    qd_buffer_list_t blist = DEQ_EMPTY;
    qd_buffer_t *buf = DEQ_HEAD(encoder->outgoing);
    size_t octets = 0;
    while (buf && buf != encoder->write_ptr.buffer) {
        DEQ_REMOVE_HEAD(encoder->outgoing);
        DEQ_INSERT_TAIL(blist, buf);
        octets += qd_buffer_size(buf);
        buf = DEQ_HEAD(encoder->outgoing);
    }
    if (!DEQ_IS_EMPTY(blist))
        conn->config.tx_buffers(hrs, &blist, octets);

    return 0;
}


static inline void _flush_output(h1_codec_request_state_t *hrs, struct encoder_t *encoder)
{
    // flush all pending output.  From this point out the outgoing queue is
    // no longer used for this message
    hrs->conn->config.tx_buffers(hrs, &encoder->outgoing, qd_buffer_list_length(&encoder->outgoing));
    DEQ_INIT(encoder->outgoing);
    encoder->write_ptr = NULL_I_PTR;
}

static inline void _flush_headers(h1_codec_request_state_t *hrs, struct encoder_t *encoder)
{
    if (!encoder->headers_sent) {
        // need to terminate any headers by sending the plain CRLF that follows
        // the headers
        write_string(encoder, CRLF);
        _flush_output(hrs, encoder);
        encoder->headers_sent = true;
    }
}

int h1_codec_tx_begin_multipart(h1_codec_request_state_t *hrs)
{
    h1_codec_connection_t *conn = h1_codec_request_state_get_connection(hrs);
    struct encoder_t *encoder = &conn->encoder;
    encoder->boundary_marker = (char*) malloc(QD_DISCRIMINATOR_SIZE + 2);
    qd_generate_discriminator(encoder->boundary_marker);
    char *content_type = (char*) malloc(strlen(MULTIPART_CONTENT_TYPE_PREFIX) + strlen(encoder->boundary_marker) + 1);
    strcpy(content_type, MULTIPART_CONTENT_TYPE_PREFIX);
    strcpy(content_type + strlen(content_type), encoder->boundary_marker);
    h1_codec_tx_add_header(hrs, CONTENT_TYPE_KEY, content_type);
    free(content_type);

    return 0;
}

int h1_codec_tx_begin_multipart_section(h1_codec_request_state_t *hrs)
{
    h1_codec_connection_t *conn = h1_codec_request_state_get_connection(hrs);
    struct encoder_t *encoder = &conn->encoder;

    //reset headers_sent flag for the new section
    encoder->headers_sent = false;
    write_string(encoder, CRLF);
    write_string(encoder, DOUBLE_HYPHEN);
    write_string(encoder, encoder->boundary_marker);
    write_string(encoder, CRLF);

    return 0;
}

int h1_codec_tx_end_multipart(h1_codec_request_state_t *hrs)
{
    h1_codec_connection_t *conn = h1_codec_request_state_get_connection(hrs);
    struct encoder_t *encoder = &conn->encoder;

    write_string(encoder, CRLF);
    write_string(encoder, DOUBLE_HYPHEN);
    write_string(encoder, encoder->boundary_marker);
    write_string(encoder, DOUBLE_HYPHEN);
    write_string(encoder, CRLF);
    encoder->headers_sent = false;
    _flush_headers(hrs, encoder);

    free(encoder->boundary_marker);
    encoder->boundary_marker = 0;

    return 0;
}


uint64_t h1_codec_tx_multipart_section_boundary_length()
{
    return QD_DISCRIMINATOR_SIZE + 4 + 2;
}

uint64_t h1_codec_tx_multipart_end_boundary_length()
{
    return QD_DISCRIMINATOR_SIZE + 4 + 4;
}

// just forward the body chain along
int h1_codec_tx_body(h1_codec_request_state_t *hrs, qd_message_stream_data_t *stream_data)
{
    h1_codec_connection_t *conn = h1_codec_request_state_get_connection(hrs);
    struct encoder_t *encoder = &conn->encoder;

    if (!encoder->headers_sent)
        _flush_headers(hrs, encoder);

    // skip the outgoing queue and send directly
    hrs->out_octets += qd_message_stream_data_payload_length(stream_data);
    conn->config.tx_stream_data(hrs, stream_data);

    return 0;
}

int h1_codec_tx_body_str(h1_codec_request_state_t *hrs, char *data)
{
    h1_codec_connection_t *conn = h1_codec_request_state_get_connection(hrs);
    struct encoder_t *encoder = &conn->encoder;
    if (!encoder->headers_sent) {
        // need to terminate any headers by sending the plain CRLF that follows
        // the headers
        write_string(encoder, CRLF);
        encoder->headers_sent = true;
    }
    write_string(encoder, data);
    _flush_output(hrs, encoder);
    return 0;
}

int h1_codec_tx_done(h1_codec_request_state_t *hrs, bool *need_close)
{
    h1_codec_connection_t *conn = h1_codec_request_state_get_connection(hrs);
    struct encoder_t *encoder = &conn->encoder;
    if (need_close)
        *need_close = false;

    if (!encoder->headers_sent)
        _flush_headers(hrs, encoder);

    bool is_response = !encoder->is_request;

    if (is_response) {
        if (IS_INFO_RESPONSE(hrs->response_code)) {
            // this is a non-terminal response. Another response is expected
            // for this request so just reset the transfer state
            hrs->response_code = 0;
        } else {
            hrs->response_complete = true;
            if (need_close) {
                // if the message body size is not explicit the connection has
                // to be closed to indicate end of message
                if (!hrs->no_body_method &&
                    !NO_BODY_RESPONSE(hrs->response_code) &&
                    !encoder->is_chunked &&
                    !encoder->hdr_content_length) {

                    *need_close = true;
                }
            }
        }
    } else {
        hrs->request_complete = true;
    }

    encoder_reset(encoder);

    if (!hrs->close_expected) {
        if (hrs->request_complete && hrs->response_complete) {
            conn->config.request_complete(hrs, false);
            h1_codec_request_state_free(hrs);
        }
    }

    return 0;
}


bool h1_codec_request_complete(const h1_codec_request_state_t *hrs)
{
    return hrs && hrs->request_complete;
}


bool h1_codec_response_complete(const h1_codec_request_state_t *hrs)
{
    return hrs && hrs->response_complete;
}


void h1_codec_request_state_counters(const h1_codec_request_state_t *hrs,
                                     uint64_t *in_octets, uint64_t *out_octets)
{
    *in_octets = (hrs) ? hrs->in_octets : 0;
    *out_octets = (hrs) ? hrs->out_octets : 0;
}


const char *h1_codec_token_list_next(const char *start, size_t *len, const char **next)
{
    static const char *SKIPME = ", \t";

    *len = 0;
    *next = 0;

    if (!start) return 0;

    while (*start && filter_str(SKIPME, *start))
        ++start;

    if (!*start) return 0;

    const char *end = start;
    while (*end && !filter_str(SKIPME, *end))
        ++end;

    *len = end - start;
    while (*end && filter_str(SKIPME, *end))
        ++end;

    *next = end;
    return start;
}

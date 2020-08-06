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

#include <qpid/dispatch/http1_lib.h>

#include <qpid/dispatch/iterator.h>
#include <qpid/dispatch/buffer.h>
#include <qpid/dispatch/alloc_pool.h>

#include <ctype.h>
#include <stdio.h>
#include <string.h>


const uint8_t CR_TOKEN = '\r';
const uint8_t LF_TOKEN = '\n';
const char   *CRLF = "\r\n";

const qd_iterator_pointer_t NULL_I_PTR = {0};

// true for informational response codes
#define IS_INFO_RESPONSE(code) ((code) / 100 == 1)

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


typedef struct scratch_buffer_t {
    uint8_t *buf;
    size_t   size;  // of buffer, not contents!
} scratch_buffer_t;


// state for a single request-response transaction
//
struct http1_transfer_t {
    DEQ_LINKS(struct http1_transfer_t);
    void         *context;
    http1_conn_t *conn;
    uint32_t      response_code;

    bool close_on_done;     // true if connection must be closed when transfer completes
    bool is_head_method;    // true if request method is HEAD
    bool is_connect_method; // true if request method is CONNECT TODO(kgiusti): supported?
};
DEQ_DECLARE(http1_transfer_t, http1_transfer_list_t);
ALLOC_DECLARE(http1_transfer_t);
ALLOC_DEFINE(http1_transfer_t);


// The HTTP/1.1 connection
//
struct http1_conn_t {
    void *context;

    // http requests are added to tail,
    // in-progress response is at head
    http1_transfer_list_t xfers;

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

        http1_transfer_t      *xfer;            // current transfer
        http1_msg_state_t      state;
        scratch_buffer_t       scratch;
        int                    error;
        const char            *error_msg;

        uint64_t               content_length;
        http1_chunk_state_t    chunk_state;
        uint64_t               chunk_length;

        bool is_request;
        bool is_chunked;
        bool is_1_0;

        // decoded headers
        bool hdr_transfer_encoding;
        bool hdr_content_length;
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
        http1_transfer_t      *xfer;           // current transfer

        bool is_request;
        bool crlf_sent;    // true if the CRLF after headers has been sent
    } encoder;

    http1_conn_config_t config;
};
ALLOC_DECLARE(http1_conn_t);
ALLOC_DEFINE(http1_conn_t);

static void decoder_reset(struct decoder_t *d);
static void encoder_reset(struct encoder_t *e);


// Create a new transfer - this is done when a new http request occurs
// Keep oldest outstanding tranfer at DEQ_HEAD(conn->xfers)
static http1_transfer_t *http1_transfer(http1_conn_t *conn)
{
    http1_transfer_t *xfer = new_http1_transfer_t();
    ZERO(xfer);
    xfer->conn = conn;
    DEQ_INSERT_TAIL(conn->xfers, xfer);
    return xfer;
}


static void http1_transfer_free(http1_transfer_t *xfer)
{
    if (xfer) {
        http1_conn_t *conn = xfer->conn;
        assert(conn->decoder.xfer != xfer);
        assert(conn->encoder.xfer != xfer);
        DEQ_REMOVE(conn->xfers, xfer);
        free_http1_transfer_t(xfer);
    }
}


http1_conn_t *http1_connection(http1_conn_config_t *config, void *context)
{
    http1_conn_t *conn = new_http1_conn_t();
    ZERO(conn);

    conn->context = context;
    conn->config = *config;
    DEQ_INIT(conn->xfers);

    encoder_reset(&conn->encoder);
    DEQ_INIT(conn->encoder.outgoing);
    conn->encoder.write_ptr = NULL_I_PTR;

    decoder_reset(&conn->decoder);
    DEQ_INIT(conn->decoder.incoming);
    conn->decoder.read_ptr = NULL_I_PTR;

    return conn;
}


// Close the connection conn.
//
// This cancels all outstanding transfers and destroys the connection.  If
// there is an incoming response message body being parsed when this function
// is invoked it will signal the end of the message.
//
void http1_connection_close(http1_conn_t *conn)
{
    if (conn) {
        struct decoder_t *decoder = &conn->decoder;
        if (!decoder->error) {
            if (decoder->state == HTTP1_MSG_STATE_BODY
                && decoder->xfer) {

                // terminate the incoming message as the server has closed the
                // connection to indicate the end of the message
                conn->config.xfer_rx_done(decoder->xfer);
            }

            // notify any outstanding transfers
            http1_transfer_t *xfer = DEQ_HEAD(conn->xfers);
            while (xfer) {
                conn->config.xfer_done(xfer);
                xfer = DEQ_NEXT(xfer);
            }
        }

        decoder_reset(&conn->decoder);
        encoder_reset(&conn->encoder);
        qd_buffer_list_free_buffers(&conn->decoder.incoming);
        qd_buffer_list_free_buffers(&conn->encoder.outgoing);
        free(conn->decoder.scratch.buf);

        http1_transfer_t *xfer = DEQ_HEAD(conn->xfers);
        while (xfer) {
            http1_transfer_free(xfer);  // removes from conn->xfers list
            xfer = DEQ_HEAD(conn->xfers);
        }

        free_http1_conn_t(conn);
    }
}


// reset the rx decoder state after message received
//
static void decoder_reset(struct decoder_t *decoder)
{
    // do not touch the read_ptr or incoming buffer list as they
    // track the current position in the incoming data stream

    decoder->body_ptr = NULL_I_PTR;
    decoder->xfer = 0;
    decoder->state = HTTP1_MSG_STATE_START;
    decoder->content_length = 0;
    decoder->chunk_state = HTTP1_CHUNK_HEADER;
    decoder->chunk_length = 0;
    decoder->error = 0;
    decoder->error_msg = 0;

    decoder->is_request = false;
    decoder->is_chunked = false;
    decoder->is_1_0 = false;

    decoder->hdr_transfer_encoding = false;
    decoder->hdr_content_length = false;
}

// reset the tx encoder after message sent
static void encoder_reset(struct encoder_t *encoder)
{
    // do not touch the write_ptr or the outgoing queue as there may be more messages to send.
    encoder->xfer = 0;
    encoder->is_request = false;
    encoder->crlf_sent = false;
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
    ensure_outgoing_capacity(encoder, needed);

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

static void debug_print_iterator_pointer(const char *prefix, const qd_iterator_pointer_t *ptr)
{
    qd_iterator_pointer_t tmp = *ptr;
    fprintf(stdout, "%s '", prefix);
    size_t len = MIN(tmp.remaining, 80);
    uint8_t octet;
    while (len-- > 0 && get_octet(&tmp, &octet)) {
        fputc(octet, stdout);
    }
    fprintf(stdout, "%s'\n", (tmp.remaining) ? " <truncated>" : "");
    fflush(stdout);
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


static bool ensure_scratch_size(scratch_buffer_t *b, size_t required)
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
               (strchr(TOKEN_EXTRA, octet)))) {
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
static bool parse_request_line(http1_conn_t *conn, struct decoder_t *decoder, qd_iterator_pointer_t *line)
{
    qd_iterator_pointer_t method = {0};
    qd_iterator_pointer_t target = {0};
    qd_iterator_pointer_t version = {0};

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

    http1_transfer_t *xfer = http1_transfer(conn);

    // check for methods that do not support body content in the response:
    if (strcmp((char*)method_str, "HEAD") == 0)
        xfer->is_head_method = true;
    else if (strcmp((char*)method_str, "CONNECT") == 0)
        xfer->is_connect_method = true;

    decoder->xfer = xfer;
    decoder->is_request = true;
    decoder->is_1_0 = (minor == 0);

    decoder->error = conn->config.xfer_rx_request(xfer, (char*)method_str, (char*)target_str, (char*)version_str);
    if (decoder->error)
        decoder->error_msg = "xfer_rx_request callback error";
    return decoder->error;
}


// parse the HTTP/1.1 response line
// "HTTP-version SP status-code [SP reason-phrase] CRLF"
//
static int parse_response_line(http1_conn_t *conn, struct decoder_t *decoder, qd_iterator_pointer_t *line)
{
    qd_iterator_pointer_t version = {0};
    qd_iterator_pointer_t status_code = {0};
    qd_iterator_pointer_t reason = {0};

    if (!parse_field(line, &version)
        || !parse_field(line, &status_code)
        || status_code.remaining != 3) {

        decoder->error_msg = "Malformed response status line";
        decoder->error = HTTP1_STATUS_SERVER_ERR;
        return decoder->error;
    }

    // Responses arrive in the same order as requests are generated so this new
    // response corresponds to head xfer
    http1_transfer_t *xfer = DEQ_HEAD(conn->xfers);
    if (!xfer) {
        // receiving a response without a corresponding request
        decoder->error_msg = "Spurious HTTP response received";
        decoder->error = HTTP1_STATUS_SERVER_ERR;
        return decoder->error;
    }

    assert(!decoder->xfer);   // state machine violation
    assert(xfer->response_code == 0);

    decoder->xfer = xfer;

    unsigned char code_str[4];
    pointer_2_str(&status_code, code_str, 4);
    xfer->response_code = atoi((char*) code_str);

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
    decoder->is_1_0 = (minor == 0);

    decoder->error = conn->config.xfer_rx_response(decoder->xfer, (char*)version_str,
                                                   xfer->response_code,
                                                   (offset) ? (char*)reason_str: 0);
    if (decoder->error)
        decoder->error_msg = "xfer_rx_response callback error";

    return decoder->error;
}


// parse the first line of an incoming http message
//
static bool parse_start_line(http1_conn_t *conn, struct decoder_t *decoder)
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
static bool process_headers_done(http1_conn_t *conn, struct decoder_t *decoder)
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

    decoder->error = conn->config.xfer_rx_headers_done(decoder->xfer);
    if (decoder->error) {
        decoder->error_msg = "xfer_rx_headers_done callback error";
        return false;
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
        http1_transfer_t *xfer = decoder->xfer;
        has_body = !(xfer->is_head_method       ||
                     xfer->is_connect_method    ||
                     xfer->response_code == 204 ||     // No Content
                     xfer->response_code == 205 ||     // Reset Content
                     xfer->response_code == 304 ||     // Not Modified
                     IS_INFO_RESPONSE(xfer->response_code));
        if (has_body) {
            // no body if explicit Content-Length of zero
            if (decoder->hdr_content_length && decoder->content_length == 0) {
                has_body = false;
            }
        }
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
static int process_header(http1_conn_t *conn, struct decoder_t *decoder, const uint8_t *key, const uint8_t *value)
{
    int parse_error = decoder->is_request ? HTTP1_STATUS_BAD_REQ : HTTP1_STATUS_SERVER_ERR;

    if (strcasecmp("Content-Length", (char*) key) == 0) {
        uint64_t old = decoder->content_length;
        if (sscanf((char*)value, "%"PRIu64, &decoder->content_length) != 1) {
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

    } else if (strcasecmp("Transfer-Encoding", (char *)key) == 0) {
        // check if "chunked" is present and it is the last item in the value
        // string.  And remember kids: coding type names are case insensitive!
        // Also note "value" has already been trimmed of whitespace at both
        // ends.
        size_t len = strlen((char*)value);
        if (len >= 7) {  // 7 = strlen("chunked")
            const char *ptr = ((char*) value) + len - 7;
            decoder->is_chunked = strcasecmp("chunked", ptr) == 0;
        }
        decoder->hdr_transfer_encoding = true;
    }

    return 0;
}


// Parse out the header key and value
//
static bool parse_header(http1_conn_t *conn, struct decoder_t *decoder)
{
    qd_iterator_pointer_t *rptr = &decoder->read_ptr;
    qd_iterator_pointer_t line;
    http1_transfer_t *xfer = decoder->xfer;
    assert(xfer);  // else state machine busted

    if (read_line(rptr, &line)) {
        debug_print_iterator_pointer("header:", &line);

        if (is_empty_line(&line)) {
            // end of headers
            return process_headers_done(conn, decoder);
        }

        qd_iterator_pointer_t key = {0};

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

        process_header(conn, decoder, key_str, value_str);

        if (!decoder->error) {
            decoder->error = conn->config.xfer_rx_header(xfer, (char *)key_str, (char *)value_str);
            if (decoder->error)
                decoder->error_msg = "xfer_rx_header callback error";
        }

        return !!rptr->remaining;
    }

    return false;  // pend for more data
}

//
// Chunked body encoding parser
//


// Pass message body data up to the application.
//
static inline int consume_body_data(http1_conn_t *conn, bool flush)
{
    struct decoder_t *decoder = &conn->decoder;
    qd_iterator_pointer_t *body_ptr = &decoder->body_ptr;
    qd_iterator_pointer_t *rptr = &decoder->read_ptr;

    // shortcut: if no more data to parse send the entire incoming chain
    if (rptr->remaining == 0) {

        decoder->error = conn->config.xfer_rx_body(decoder->xfer, &decoder->incoming,
                                                   body_ptr->cursor - qd_buffer_base(body_ptr->buffer),
                                                   body_ptr->remaining);
        DEQ_INIT(decoder->incoming);
        *body_ptr = NULL_I_PTR;
        *rptr = NULL_I_PTR;
        return decoder->error;
    }

    // The read pointer points to somewhere in the buffer chain that contains some
    // unparsed data.  Send any buffers preceding the current read pointer.
    qd_buffer_list_t blist = DEQ_EMPTY;
    size_t octets = 0;
    const size_t body_offset = body_ptr->cursor - qd_buffer_base(body_ptr->buffer);

    // invariant:
    assert(DEQ_HEAD(decoder->incoming) == body_ptr->buffer);

    while (body_ptr->buffer && body_ptr->buffer != rptr->buffer) {
        DEQ_REMOVE_HEAD(decoder->incoming);
        DEQ_INSERT_TAIL(blist, body_ptr->buffer);
        octets += qd_buffer_cursor(body_ptr->buffer) - body_ptr->cursor;
        body_ptr->buffer = DEQ_HEAD(decoder->incoming);
        body_ptr->cursor = qd_buffer_base(body_ptr->buffer);
    }

    // invariant:
    assert(octets <= body_ptr->remaining);
    body_ptr->remaining -= octets;

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

    decoder->error = conn->config.xfer_rx_body(decoder->xfer, &blist, body_offset, octets);
    return decoder->error;
}



// parsing the start of a chunked header:
// <chunk size in hex>CRLF
//
static bool parse_body_chunked_header(http1_conn_t *conn, struct decoder_t *decoder)
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
static bool parse_body_chunked_data(http1_conn_t *conn, struct decoder_t *decoder)
{
    qd_iterator_pointer_t *rptr = &decoder->read_ptr;
    qd_iterator_pointer_t *body_ptr = &decoder->body_ptr;

    assert(decoder->chunk_state == HTTP1_CHUNK_DATA);

    size_t skipped = skip_octets(rptr, decoder->chunk_length);
    decoder->chunk_length -= skipped;
    body_ptr->remaining += skipped;

    if (decoder->chunk_length == 0) {
        // end of chunk
        decoder->chunk_state = HTTP1_CHUNK_HEADER;
        consume_body_data(conn, false);
    }

    return !!rptr->remaining;
}


// Keep reading chunk trailers until the terminating empty line is read
//
static bool parse_body_chunked_trailer(http1_conn_t *conn, struct decoder_t *decoder)
{
    qd_iterator_pointer_t *rptr = &decoder->read_ptr;
    qd_iterator_pointer_t *body_ptr = &decoder->body_ptr;
    qd_iterator_pointer_t line;

    assert(decoder->chunk_state == HTTP1_CHUNK_TRAILERS);

    if (read_line(rptr, &line)) {
        body_ptr->remaining += line.remaining;
        if (is_empty_line(&line)) {
            // end of message
            consume_body_data(conn, true);
            decoder->state = HTTP1_MSG_STATE_DONE;
        }

        return !!rptr->remaining;
    }

    return false;  // pend for full line
}


// parse an incoming message body which is chunk encoded
// Return True if there is more data pending to parse
static bool parse_body_chunked(http1_conn_t *conn, struct decoder_t *decoder)
{
    bool more;
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
static bool parse_body_content(http1_conn_t *conn, struct decoder_t *decoder)
{
    qd_iterator_pointer_t *rptr = &decoder->read_ptr;
    qd_iterator_pointer_t *body_ptr = &decoder->body_ptr;

    size_t skipped = skip_octets(rptr, decoder->content_length);
    decoder->content_length -= skipped;
    body_ptr->remaining += skipped;
    bool eom = decoder->content_length == 0;

    consume_body_data(conn, eom);
    if (eom)
        decoder->state = HTTP1_MSG_STATE_DONE;

    return !!rptr->remaining;
}


static bool parse_body(http1_conn_t *conn, struct decoder_t *decoder)
{
    if (decoder->is_chunked)
        return parse_body_chunked(conn, decoder);

    if (decoder->content_length)
        return parse_body_content(conn, decoder);

    // otherwise no explict body size, so just keep passing the entire unparsed
    // incoming chain along until the remote closes the connection
    decoder->error = conn->config.xfer_rx_body(decoder->xfer,
                                               &decoder->incoming,
                                               decoder->read_ptr.cursor
                                               - qd_buffer_base(decoder->read_ptr.buffer),
                                               decoder->read_ptr.remaining);
    if (decoder->error) {
        decoder->error_msg = "xfer_rx_body callback error";
        return false;
    }

    decoder->body_ptr = decoder->read_ptr = NULL_I_PTR;
    DEQ_INIT(decoder->incoming);

    return false;
}


// Called when incoming message is complete
//
static bool parse_done(http1_conn_t *conn, struct decoder_t *decoder)
{
    http1_transfer_t *xfer = decoder->xfer;
    bool is_response = !decoder->is_request;

    if (!decoder->error) {
        // signal the message receive is complete
        conn->config.xfer_rx_done(xfer);

        if (is_response) {   // request<->response transfer complete

            // Informational 1xx response codes are NOT teriminal - further responses are allowed!
            if (IS_INFO_RESPONSE(xfer->response_code)) {
                xfer->response_code = 0;
            } else {
                // The message exchange is complete
                conn->config.xfer_done(xfer);
                decoder->xfer = 0;
                http1_transfer_free(xfer);
            }
        }

        decoder_reset(decoder);
        return !!decoder->read_ptr.remaining;
    }
    return false;
}


// Main decode loop.
// Process received data until it is exhausted
//
static int decode_incoming(http1_conn_t *conn)
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


void *http1_connection_get_context(http1_conn_t *conn)
{
    return conn->context;
}

// Push inbound network data into the http1 protocol engine.
//
// All of the xfer_rx callback will occur in the context of this call. This
// returns zero on success otherwise an error code.  Any error occuring during
// a callback will be reflected in the return value of this function.  It is
// expected that the caller will call http1_connection_close on a non-zero
// return value.
//
int http1_connection_rx_data(http1_conn_t *conn, qd_buffer_list_t *data, size_t len)
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

void http1_transfer_set_context(http1_transfer_t *xfer, void *context)
{
    xfer->context = context;
}

void *http1_transfer_get_context(const http1_transfer_t *xfer)
{
    return xfer->context;
}

http1_conn_t *http1_transfer_get_connection(const http1_transfer_t *xfer)
{
    return xfer->conn;
}


// initiate a new HTTP request.  This creates a new transfer.
// request = <method>SP<target>SP<version>CRLF
// Expects version to be in the format "HTTP/X.Y"
//
http1_transfer_t *http1_tx_request(http1_conn_t *conn, const char *method, const char *target, const char *version)
{
    struct encoder_t *encoder = &conn->encoder;
    assert(!encoder->xfer);   // error: transfer already in progress
    assert(conn->config.type == HTTP1_CONN_SERVER);

    http1_transfer_t *xfer = encoder->xfer = http1_transfer(conn);
    encoder->is_request = true;
    encoder->crlf_sent = false;

    if (strcmp((char*)method, "HEAD") == 0)
        xfer->is_head_method = true;
    else if (strcmp((char*)method, "CONNECT") == 0)
        xfer->is_connect_method = true;

    write_string(encoder, method);
    write_string(encoder, " ");
    write_string(encoder, target);
    write_string(encoder, " ");
    write_string(encoder, version);
    write_string(encoder, CRLF);

    return xfer;
}


// Send an HTTP response msg.  xfer must correspond to the "oldest" outstanding
// request that arrived via the xfer_rx_request callback for this connection.
// version is expected to be in the form "HTTP/x.y"
// status_code is expected to be 100 <= status_code <= 999
// status-line = HTTP-version SP status-code SP reason-phrase CRLF
//
int http1_tx_response(http1_transfer_t *xfer, const char *version, int status_code, const char *reason_phrase)
{
    http1_conn_t *conn = http1_transfer_get_connection(xfer);
    struct encoder_t *encoder = &conn->encoder;

    assert(conn->config.type == HTTP1_CONN_CLIENT);
    assert(!encoder->xfer);   // error: transfer already in progress
    assert(DEQ_HEAD(conn->xfers) == xfer);   // error: response not in order!
    assert(xfer->response_code == 0);

    encoder->xfer = xfer;
    encoder->is_request = false;
    encoder->crlf_sent = false;
    xfer->response_code = status_code;

    char code_str[32];
    snprintf(code_str, 32, "%d", status_code);

    write_string(encoder, version);
    write_string(encoder, " ");
    write_string(encoder, code_str);
    if (reason_phrase) {
        write_string(encoder, " ");
        write_string(encoder, reason_phrase);
    }
    write_string(encoder, CRLF);

    return 0;
}


// Add a header field to an outgoing message
// header-field   = field-name ":" OWS field-value OWS
int http1_tx_add_header(http1_transfer_t *xfer, const char *key, const char *value)
{
    http1_conn_t *conn = http1_transfer_get_connection(xfer);
    struct encoder_t *encoder = &conn->encoder;
    assert(encoder->xfer == xfer);  // xfer not current transfer

    write_string(encoder, key);
    write_string(encoder, ": ");
    write_string(encoder, value);
    write_string(encoder, CRLF);

    // check to see if there are any full buffers that can be sent.

    qd_buffer_list_t blist = DEQ_EMPTY;
    qd_buffer_t *buf = DEQ_HEAD(encoder->outgoing);
    size_t octets = 0;
    while (buf && buf != encoder->write_ptr.buffer) {
        DEQ_REMOVE_HEAD(encoder->outgoing);
        DEQ_INSERT_TAIL(blist, buf);
        octets += qd_buffer_size(buf);
    }
    if (!DEQ_IS_EMPTY(blist))
        conn->config.conn_tx_data(conn, &blist, 0, octets);

    return 0;
}


// just forward the body chain along
int http1_tx_body(http1_transfer_t *xfer, qd_buffer_list_t *data, size_t offset, size_t len)
{
    http1_conn_t *conn = http1_transfer_get_connection(xfer);
    struct encoder_t *encoder = &conn->encoder;

    fprintf(stderr, "http1_tx_body(offset=%zu size=%zu)\n", offset, len);
    
    if (!encoder->crlf_sent) {
        // need to terminate any headers by sending the plain CRLF that follows
        // the headers
        write_string(encoder, CRLF);

        // flush all pending output.  From this point out the outgoing queue is
        // no longer used for this message
        fprintf(stderr, "Flushing before body: %u bytes\n", qd_buffer_list_length(&encoder->outgoing));
        conn->config.conn_tx_data(conn, &encoder->outgoing, 0, qd_buffer_list_length(&encoder->outgoing));
        DEQ_INIT(encoder->outgoing);
        encoder->write_ptr = NULL_I_PTR;
        encoder->crlf_sent = true;
    }

    // skip the outgoing queue and send directly
    fprintf(stderr, "Sending body data %zu bytes\n", len);
    conn->config.conn_tx_data(conn, data, offset, len);

    return 0;
}


int http1_tx_done(http1_transfer_t *xfer)
{
    http1_conn_t *conn = http1_transfer_get_connection(xfer);
    struct encoder_t *encoder = &conn->encoder;

    if (!encoder->crlf_sent) {
        // need to send the plain CRLF that follows the headers
        write_string(encoder, CRLF);

        // flush all pending output.

        fprintf(stderr, "Flushing at tx_done: %u bytes\n", qd_buffer_list_length(&encoder->outgoing));
        conn->config.conn_tx_data(conn, &encoder->outgoing, 0, qd_buffer_list_length(&encoder->outgoing));
        DEQ_INIT(encoder->outgoing);
        encoder->write_ptr = NULL_I_PTR;
        encoder->crlf_sent = true;
    }

    bool is_response = !encoder->is_request;
    encoder_reset(encoder);

    if (is_response) {
        if (IS_INFO_RESPONSE(xfer->response_code)) {
            // this is a non-terminal response. Another response is expected
            // for this request so just reset the transfer state
            xfer->response_code = 0;
        } else {
            // The message exchange is complete
            conn->config.xfer_done(xfer);
            http1_transfer_free(xfer);
        }
    }

    return 0;
}



#ifndef http1_lib_H
#define http1_lib_H 1
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
#include <qpid/dispatch/buffer.h>

#include <inttypes.h>

#define HTTP1_VERSION_1_1  "HTTP/1.1"
#define HTTP1_VERSION_1_0  "HTTP/1.0"

typedef struct http1_conn_t     http1_conn_t;
typedef struct http1_transfer_t http1_transfer_t;


typedef enum {
    HTTP1_CONN_CLIENT,  // connection initiated by client
    HTTP1_CONN_SERVER,  // connection to server
} http1_conn_type_t;

typedef enum {
    HTTP1_STATUS_BAD_REQ = 400,
    HTTP1_STATUS_SERVER_ERR = 500,
    HTTP1_STATUS_BAD_VERSION = 505,
} http1_status_code_t;

typedef struct http1_conn_config_t {

    http1_conn_type_t type;

    // called with output data to write to the network
    void (*conn_tx_data)(http1_conn_t *conn, qd_buffer_list_t *data, size_t offset, unsigned int len);

    // @TODO(kgiusti) - remove?
    //void (*conn_error)(http1_conn_t *conn, int code, const char *reason);

    //
    // RX message callbacks
    //

    // HTTP request received - new transfer created (xfer).  This xfer must be
    // supplied in the http1_response() method
    int (*xfer_rx_request)(http1_transfer_t *xfer,
                           const char *method,
                           const char *target,
                           const char *version);

    // HTTP response received - the transfer comes from the return value of the
    // corresponding http1_request method.  Note well that if status_code is
    // Informational (1xx) then this response is NOT the last response for the
    // current request (See RFC7231, 6.2 Informational 1xx).  The xfer_done
    // callback will be called after the LAST response has been received.
    //
    int (*xfer_rx_response)(http1_transfer_t *xfer,
                            const char *version,
                            int status_code,
                            const char *reason_phrase);

    int (*xfer_rx_header)(http1_transfer_t *xfer, const char *key, const char *value);
    int (*xfer_rx_headers_done)(http1_transfer_t *xfer);

    int (*xfer_rx_body)(http1_transfer_t *xfer, qd_buffer_list_t *body, size_t offset, size_t len);

    void (*xfer_rx_done)(http1_transfer_t *xfer);

    // Invoked when the request/response(s) exchange has completed
    //
    void (*xfer_done)(http1_transfer_t *xfer);
} http1_conn_config_t;


http1_conn_t *http1_connection(http1_conn_config_t *config, void *context);
void http1_connection_close(http1_conn_t *conn);
void *http1_connection_get_context(http1_conn_t *conn);

// push inbound network data into the http1 library
int http1_connection_rx_data(http1_conn_t *conn, qd_buffer_list_t *data, size_t len);


//
// API for sending HTTP/1.1 messages
//
void http1_transfer_set_context(http1_transfer_t *xfer, void *context);
void *http1_transfer_get_context(const http1_transfer_t *xfer);
http1_conn_t *http1_transfer_get_connection(const http1_transfer_t *xfer);

// initiate a request - this creates a new message transfer context
http1_transfer_t *http1_tx_request(http1_conn_t *conn, const char *method, const char *target, const char *version);

// respond to a received request - the transfer context should be from the corresponding xfer_rx_request callback
int http1_tx_response(http1_transfer_t *xfer, const char *version, int status_code, const char *reason_phrase);

int http1_tx_add_header(http1_transfer_t *xfer, const char *key, const char *value);
int http1_tx_body(http1_transfer_t *xfer, qd_buffer_list_t *data, size_t offset, size_t len);
int http1_tx_done(http1_transfer_t *xfer);





#endif // http1_lib_H

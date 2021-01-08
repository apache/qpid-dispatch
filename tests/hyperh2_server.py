#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#

import socket

import signal
import sys
import os
import h2.connection
import h2.events
import h2.config
import h2.errors

BYTES = 65535


def receive_signal(signalNumber, frame):
    print('Received:', signalNumber)
    sys.exit(0)


def handle_goaway_test_1(event, conn):
    """
    conn.close_connection() sends a goaway frame to the client
    and closes the connection.
    """
    # When a request is made on the URL "/goaway_test_1", we immediately close
    # the connection which sends a GOAWAY frame.
    conn.close_connection(error_code=h2.errors.ErrorCodes.NO_ERROR,
                          additional_data=None,
                          last_stream_id=0)


def handle_request(event, conn):
    request_headers = event.headers
    for request_header in request_headers:
        str_request_header = str(request_header[0], "utf-8")
        if str_request_header == ":path":
            request_path = str(request_header[1], "utf-8")
            if "goaway_test_1" in request_path:
                handle_goaway_test_1(event, conn)


def handle_events(conn, events):
    for event in events:
        if isinstance(event, h2.events.RequestReceived):
            handle_request(event, conn)


def handle(sock):
    config = h2.config.H2Configuration(client_side=False)
    conn = h2.connection.H2Connection(config=config)
    conn.initiate_connection()
    sock.sendall(conn.data_to_send())

    while True:
        data = None
        try:
            data = sock.recv(BYTES)
        except:
            pass
        if not data:
            break
        try:
            events = conn.receive_data(data)
        except Exception as e:
            print(e)
            break
        handle_events(conn, events)
        data_to_send = conn.data_to_send()
        if data_to_send:
            sock.sendall(data_to_send)


signal.signal(signal.SIGHUP, receive_signal)
signal.signal(signal.SIGINT, receive_signal)
signal.signal(signal.SIGQUIT, receive_signal)
signal.signal(signal.SIGILL, receive_signal)
signal.signal(signal.SIGTERM, receive_signal)

sock = socket.socket()
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
sock.bind(('0.0.0.0', int(os.getenv('SERVER_LISTEN_PORT'))))
sock.listen(5)

while True:
    # The accept method blocks until someone attempts to connect to our TCP
    # port: when they do, it returns a tuple: the first element is a new
    # socket object, the second element is a tuple of the address the new
    # connection is from
    handle(sock.accept()[0])

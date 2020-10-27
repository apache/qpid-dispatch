#!/usr/bin/env python

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

import argparse
import os
import selectors
import socket
import sys
import time
import traceback
import types

from system_test import Logger


class EchoLogger(Logger):
    def __init__(self, prefix="ECHO_LOGGER", title="EchoLogger", print_to_console=False, save_for_dump=False):
        self.prefix = prefix + ' ' if len(prefix) > 0 else ''
        super(EchoLogger, self).__init__(title=title, print_to_console=print_to_console, save_for_dump=save_for_dump)

    def log(self, msg):
        super(EchoLogger, self).log(self.prefix + msg)


def split_chunk_for_display(raw_bytes):
    """
    Given some raw bytes, return a display string
    Only show the beginning and end of largish (2xMAGIC_SIZE) arrays.
    :param raw_bytes:
    :return: display string
    """
    MAGIC_SIZE = 50  # Content repeats after chunks this big - used by echo client, too
    if len(raw_bytes) > 2 * MAGIC_SIZE:
        result = repr(raw_bytes[:MAGIC_SIZE]) + " ... " + repr(raw_bytes[-MAGIC_SIZE:])
    else:
        result = repr(raw_bytes)
    return result


def main_except(host, port, size, count, timeout, logger):
    '''
    :param host: connect to this host
    :param port: connect to this port
    :param size: size of individual payload chunks in bytes
    :param count: number of payload chunks
    :param strategy: "1" Send one payload;  # TODO
                         Recv one payload
    :param logger: Logger() object
    :return:
    '''
    # Start up
    start_time = time.time()
    logger.log('Connecting to host:%s, port:%d, size:%d, count:%d' % (host, port, size, count))
    keep_going = True
    total_sent = 0
    total_rcvd = 0

    # outbound payload
    payload_out = []
    out_list_idx = 0  # current _out array being sent
    out_byte_idx = 0  # next-to-send in current array
    out_ready_to_send = True
    # Generate unique content for each message so you can tell where the message
    # or fragment belongs in the whole stream. Chunks look like:
    #    b'[localhost:33333:6:0]ggggggggggggggggggggggggggggg'
    #    host: localhost
    #    port: 33333
    #    index: 6
    #    offset into message: 0
    MAGIC_SIZE = 50 # Content repeats after chunks this big - used by echo server, too
    for idx in range(count):
        body_msg = ""
        padchar = "abcdefghijklmnopqrstuvwxyz@#$%"[idx % 30]
        while len(body_msg) < size:
            chunk = "[%s:%d:%d:%d]" % (host, port, idx, len(body_msg))
            padlen = MAGIC_SIZE - len(chunk)
            chunk += padchar * padlen
            body_msg += chunk
        if len(body_msg) > size:
            body_msg = body_msg[:size]
        payload_out.append(bytearray(body_msg.encode()))
    # incoming payloads
    payload_in  = []
    in_list_idx = 0 # current _in array being received
    for i in range(count):
        payload_in.append(bytearray())

    # set up connection
    host_address = (host, port)
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect(host_address)
    sock.setblocking(False)

    # set up selector
    sel = selectors.DefaultSelector()
    sel.register(sock,
                 selectors.EVENT_READ | selectors.EVENT_WRITE)

    # event loop
    while keep_going:
        if timeout > 0.0:
            elapsed = time.time() - start_time
            if elapsed > timeout:
                logger.log("Exiting due to timeout. Total sent= %d, total rcvd= %d" % (total_sent, total_rcvd))
                break
        for key, mask in sel.select(timeout=0.1):
            sock = key.fileobj
            if mask & selectors.EVENT_READ:
                recv_data = sock.recv(1024)
                if recv_data:
                    total_rcvd = len(recv_data)
                    payload_in[in_list_idx].extend(recv_data)
                    if len(payload_in[in_list_idx]) == size:
                        logger.log("Rcvd message %d" % in_list_idx)
                        in_list_idx += 1
                        if in_list_idx == count:
                            # Received all bytes of all chunks - done.
                            keep_going = False
                            # Verify the received data
                            for idxc in range(count):
                                for idxs in range(size):
                                    ob = payload_out[idxc][idxs]
                                    ib = payload_in [idxc][idxs]
                                    if ob != ib:
                                        error = "CRITICAL Rcvd message verify fail. row:%d, col:%d, expected:%s, actual:%s" \
                                                % (idxc, idxs, repr(ob), repr(ib))
                                        logger.log(error)
                                        raise Exception(error)
                        else:
                            out_ready_to_send = True
                            sel.modify(sock, selectors.EVENT_READ | selectors.EVENT_WRITE)
                    elif len(payload_in[in_list_idx]) > size:
                        error = "CRITICAL Rcvd message too big. Expected:%d, actual:%d" % \
                                (size, len(payload_in[in_list_idx]))
                        logger.log(error)
                        raise Exception(error)
                    else:
                        pass # still accumulating a message
                else:
                    # socket closed
                    keep_going = False
            if mask & selectors.EVENT_WRITE:
                if out_ready_to_send:
                    n_sent = sock.send( payload_out[out_list_idx][out_byte_idx:] )
                    total_sent += n_sent
                    out_byte_idx += n_sent
                    if out_byte_idx == size:
                        logger.log("Sent message %d" % out_list_idx)
                        out_byte_idx = 0
                        out_list_idx += 1
                        sel.modify(sock, selectors.EVENT_READ) # turn off write events
                        out_ready_to_send = False # turn on when rcvr receives
                else:
                    logger.log("DEBUG: ignoring EVENT_WRITE")

    # shut down
    sel.unregister(sock)
    sock.close()

def main(argv):
    try:
        # parse args
        p = argparse.ArgumentParser()
        p.add_argument('--host', '-b',
                       help='Required target host')
        p.add_argument('--port', '-p', type=int,
                       help='Required target port number')
        p.add_argument('--size', '-s', type=int, default=100, const=1, nargs='?',
                       help='Size of payload in bytes')
        p.add_argument('--count', '-c', type=int, default=1, const=1, nargs='?',
                       help='Number of payloads to process')
        p.add_argument('--name',
                       help='Optional logger prefix')
        p.add_argument('--timeout', '-t', type=float, default=0.0, const=1, nargs="?",
                       help='Timeout in seconds. Default value "0" disables timeouts')
        p.add_argument('--log', '-l',
                       action='store_true',
                       help='Write activity log to console')
        del argv[0]
        args = p.parse_args(argv)

        # host
        if args.host is None:
            raise Exception("User must specify a host")
        host = args.host

        # port
        if args.port is None:
            raise Exception("User must specify a port number")
        port = args.port

        # size
        if args.size <= 0:
            raise Exception("Size must be greater than zero")
        size = args.size

        # count
        if args.count <= 0:
            raise Exception("Count must be greater than zero")
        count = args.count

        # name / prefix
        prefix = args.name if args.name is not None else "ECHO_CLIENT"

        # timeout
        if args.timeout < 0.0:
            raise Exception("Timeout must be greater than or equal to zero")

        # logging
        logger = EchoLogger(prefix=prefix,
                            title = "%s host:%s port %d size:%d count:%d" % (prefix, host, port, size, count),
                            print_to_console = args.log,
                            save_for_dump = False)

        main_except(host, port, size, count, args.timeout, logger)
        return 0

    except KeyboardInterrupt:
        logger.log("Exiting due to KeyboardInterrupt")
        return 0

    except Exception as e:
        traceback.print_exc()
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))

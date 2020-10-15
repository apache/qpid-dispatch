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
import traceback
import types

from system_test import Logger

HOST = '127.0.0.1'

def main_except(port, logger):
    '''
    :param port: port to listen on
    :param logger: Logger() object
    :return:
    '''
    # set up listening socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.bind((HOST, port))
    sock.listen()
    sock.setblocking(False)
    logger.log('Listening on host:%s, port:%d' % (HOST, port))

    # set up selector
    sel = selectors.DefaultSelector()
    sel.register(sock, selectors.EVENT_READ, data=None)

    # event loop
    while True:
        events = sel.select(timeout=None)
        for key, mask in events:
            if key.data is None:
                do_accept(key.fileobj, sel, logger)
            else:
                do_service(key, mask, sel, logger)

def do_accept(sock, sel, logger):
    conn, addr = sock.accept()
    logger.log('Accepted connection from %s:%d' % (addr[0], addr[1]))
    conn.setblocking(False)
    data = types.SimpleNamespace(addr=addr, inb=b'', outb=b'')
    events = selectors.EVENT_READ | selectors.EVENT_WRITE
    sel.register(conn, events, data=data)

def do_service(key, mask, sel, logger):
    sock = key.fileobj
    data = key.data
    if mask & selectors.EVENT_READ:
        recv_data = sock.recv(1024)
        if recv_data:
            data.outb += recv_data
            logger.log('ECHO read from: %s:%d len:%d: %s' % (data.addr[0], data.addr[1], len(recv_data), repr(recv_data)))
        else:
            logger.log('Closing connection to %s:%d' % (data.addr[0], data.addr[1]))
            sel.unregister(sock)
            sock.close()
    if mask & selectors.EVENT_WRITE:
        if data.outb:
            sent = sock.send(data.outb)
            if sent > 0:
                logger.log('ECHO write to : %s:%d len:%d: %s' % (data.addr[0], data.addr[1], sent, repr(data.outb[:sent])))
            else:
                logger.log('ECHO write to : %s:%d len:0' % (data.addr[0], data.addr[1]))
            data.outb = data.outb[sent:]


def main(argv):
    try:
        # parse args
        p = argparse.ArgumentParser()
        p.add_argument('--port', '-p',
                       help='Required listening port number')
        p.add_argument('--log', '-l',
                       action='store_true',
                       help='Write activity log to console')
        del argv[0]
        args = p.parse_args(argv)

        # port
        if args.port is None:
            raise Exception("User must specify a port number")
        port = int(args.port)

        # logging
        logger = Logger(title = "TCP_echo_server port %d" % port,
                        print_to_console = args.log,
                        save_for_dump = False)

        main_except(port, logger)
        return 0
    except Exception as e:
        traceback.print_exc()
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))

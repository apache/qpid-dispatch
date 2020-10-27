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
from signal import signal, SIGINT
import socket
import sys
import time
import traceback
import types

from system_test import Logger

HOST = '127.0.0.1'

class ClientRecord(object):
    """
    Object to register with the selector 'data' field
    for incoming user connections. This is *not* used
    for the listening socket.
    This object holds the socketId in the address and
    the inbound and outbound data list buffers for this
    socket's payload.
    """
    def __init__(self, address):
        self.addr = address
        self.inb = b''
        self.outb = b''

    def __repr__(self):
        return str(self.addr) + " len(in)=" + str(len(self.inb)) + " len(out)=" + str(len(self.outb))

    def __str__(self):
        return self.__repr__()


class EchoLogger(Logger):
    def __init__(self, prefix="ECHO_LOGGER", title="EchoLogger", print_to_console=False, save_for_dump=False):
        self.prefix = prefix + ' ' if len(prefix) > 0 else ''
        super(EchoLogger, self).__init__(title=title, print_to_console=print_to_console, save_for_dump=save_for_dump)
    
    def log(self, msg):
        super(EchoLogger, self).log(self.prefix + msg)


def main_except(sock, port, echo_count, timeout, logger):
    '''
    :param lsock: socket to listen on
    :param port: port to listen on
    :param echo_count: exit after echoing this many bytes
    :param timeout: exit after this many seconds
    :param logger: Logger() object
    :return:
    '''
    # set up spontaneous exit settings
    start_time = time.time()
    total_echoed = 0

    # set up listening socket
    sock.bind((HOST, port))
    sock.listen()
    sock.setblocking(False)
    logger.log('Listening on host:%s, port:%d' % (HOST, port))

    # set up selector
    sel = selectors.DefaultSelector()
    sel.register(sock, selectors.EVENT_READ, data=None)

    # event loop
    while True:
        if timeout > 0.0:
            elapsed = time.time() - start_time
            if elapsed > timeout:
                logger.log("Exiting due to timeout. Total echoed = %d" % total_echoed)
                break
        if echo_count > 0:
            if total_echoed >= echo_count:
                logger.log("Exiting due to echo byte count. Total echoed = %d" % total_echoed)
                break
        events = sel.select(timeout=0.1)
        if events:
            for key, mask in events:
                if key.data is None:
                    if key.fileobj is sock:
                        do_accept(key.fileobj, sel, logger)
                    else:
                        assert(False, "Only listener 'sock' has None in opaque data field")
                else:
                    total_echoed += do_service(key, mask, sel, logger)
        else:
            pass # select timeout. probably.

    sel.unregister(sock)
    sock.close()

def do_accept(sock, sel, logger):
    conn, addr = sock.accept()
    logger.log('Accepted connection from %s:%d' % (addr[0], addr[1]))
    conn.setblocking(False)
    events = selectors.EVENT_READ | selectors.EVENT_WRITE
    sel.register(conn, events, data=ClientRecord(addr))

def do_service(key, mask, sel, logger):
    retval = 0
    sock = key.fileobj
    data = key.data
    if mask & selectors.EVENT_READ:
        recv_data = sock.recv(1024)
        if recv_data:
            data.outb += recv_data
            logger.log('read from: %s:%d len:%d: %s' % (data.addr[0], data.addr[1], len(recv_data), repr(recv_data)))
            sel.modify(sock, selectors.EVENT_READ | selectors.EVENT_WRITE, data=data)
        else:
            logger.log('Closing connection to %s:%d' % (data.addr[0], data.addr[1]))
            sel.unregister(sock)
            sock.close()
    if mask & selectors.EVENT_WRITE:
        if data.outb:
            sent = sock.send(data.outb)
            retval += sent
            if sent > 0:
                logger.log('write to : %s:%d len:%d: %s' % (data.addr[0], data.addr[1], sent, repr(data.outb[:sent])))
            else:
                logger.log('write to : %s:%d len:0' % (data.addr[0], data.addr[1]))
            data.outb = data.outb[sent:]
        else:
            logger.log('write event with no data' + str(data))
            sel.modify(sock, selectors.EVENT_READ, data=data)
    return retval

def main(argv):
    retval = 0
    try:
        # parse args
        p = argparse.ArgumentParser()
        p.add_argument('--port', '-p',
                       help='Required listening port number')
        p.add_argument('--name',
                       help='Optional logger prefix')
        p.add_argument('--echo', '-e', type=int, default=0, const=1, nargs="?",
                       help='Exit after echoing this many bytes. Default value "0" disables exiting on byte count.')
        p.add_argument('--timeout', '-t', type=float, default=0.0, const=1, nargs="?",
                       help='Timeout in seconds. Default value "0" disables timeouts')
        p.add_argument('--log', '-l',
                       action='store_true',
                       help='Write activity log to console')
        del argv[0]
        args = p.parse_args(argv)

        lsock = None

        # port
        if args.port is None:
            raise Exception("User must specify a port number")
        port = int(args.port)

        # name / prefix
        prefix = args.name if args.name is not None else "ECHO_SERVER"

        # echo
        if args.echo < 0:
            raise Exception("Echo count must be greater than zero")

        # timeout
        if args.timeout < 0.0:
            raise Exception("Timeout must be greater than zero")

        # logging
        logger = EchoLogger(prefix = prefix,
                            title = "%s port %d" % (prefix, port),
                            print_to_console = args.log,
                            save_for_dump = False)

        # the listening socket
        lsock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

        main_except(lsock, port, args.echo, args.timeout, logger)

    except KeyboardInterrupt:
        pass

    except Exception as e:
        traceback.print_exc()
        retval = 1

    if lsock is not None:
        lsock.close()
    return retval


if __name__ == "__main__":
    sys.exit(main(sys.argv))

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

from __future__ import unicode_literals
from __future__ import division
from __future__ import absolute_import
from __future__ import print_function

import argparse
import os
import selectors
import signal
import socket
import sys
from threading import Thread
import time
import traceback
import types

from system_test import Logger
from system_test import TIMEOUT


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


class GracefulExitSignaler:
    kill_now = False

    def __init__(self):
        signal.signal(signal.SIGINT, self.exit_gracefully)
        signal.signal(signal.SIGTERM, self.exit_gracefully)

    def exit_gracefully(self, signum, frame):
        self.kill_now = True


def split_chunk_for_display(raw_bytes):
    """
    Given some raw bytes, return a display string
    Only show the beginning and end of largish (2x CONTENT_CHUNK_SIZE) arrays.
    :param raw_bytes:
    :return: display string
    """
    CONTENT_CHUNK_SIZE = 50  # Content repeats after chunks this big - used by echo client, too
    if len(raw_bytes) > 2 * CONTENT_CHUNK_SIZE:
        result = repr(raw_bytes[:CONTENT_CHUNK_SIZE]) + " ... " + repr(raw_bytes[-CONTENT_CHUNK_SIZE:])
    else:
        result = repr(raw_bytes)
    return result


class TcpEchoServer:

    def __init__(self, prefix="ECHO_SERVER", port="0", echo_count=0, timeout=0.0, logger=None, d1968=False):
        """
        Start echo server in separate thread

        :param prefix: log prefix
        :param port: port to listen on
        :param echo_count: exit after echoing this many bytes
        :param timeout: exit after this many seconds
        :param logger: Logger() object
        :return:
        """
        self.sock = None
        self.prefix = prefix
        self.port = int(port)
        self.echo_count = echo_count
        self.timeout = timeout
        self.logger = logger
        self.d1968 = d1968
        self.keep_running = True
        self.HOST = '127.0.0.1'
        self.is_running = False
        self.exit_status = None
        self.error = None
        self._thread = Thread(target=self.run)
        self._thread.daemon = True
        self._thread.start()

    def run(self):
        """
        Run server in daemon thread
        :return:
        """
        try:
            # set up spontaneous exit settings
            self.is_running = True
            start_time = time.time()
            total_echoed = 0

            # set up listening socket
            try:
                self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                self.sock.bind((self.HOST, self.port))
                self.sock.listen()
                self.sock.setblocking(False)
                self.logger.log('%s Listening on host:%s, port:%s' % (self.prefix, self.HOST, self.port))
            except Exception:
                self.error = ('%s Opening listen socket %s:%s exception: %s' %
                              (self.prefix, self.HOST, self.port, traceback.format_exc()))
                self.logger.log(self.error)
                return 1

            # set up selector
            sel = selectors.DefaultSelector()
            sel.register(self.sock, selectors.EVENT_READ, data=None)

            # event loop
            while True:
                if not self.keep_running:
                    self.exit_status = "INFO: command shutdown:"
                    break
                if self.timeout > 0.0:
                    elapsed = time.time() - start_time
                    if elapsed > self.timeout:
                        self.exit_status = "Exiting due to timeout. Total echoed = %d" % total_echoed
                        break
                if self.echo_count > 0:
                    if total_echoed >= self.echo_count:
                        self.exit_status = "Exiting due to echo byte count. Total echoed = %d" % total_echoed
                        break
                events = sel.select(timeout=0.1)
                if events:
                    for key, mask in events:
                        if key.data is None:
                            if key.fileobj is self.sock:
                                self.do_accept(key.fileobj, sel, self.logger)
                            else:
                                pass  # Only listener 'sock' has None in opaque data field
                        else:
                            n_echoed = self.do_service(key, mask, sel, self.logger, self.d1968)
                            if n_echoed < 0:
                                self.exit_status = "Exiting due to d1968 test socket closure. Total echoed = %d" % total_echoed
                                break
                            total_echoed += n_echoed
                else:
                    pass   # select timeout. probably.

            sel.unregister(self.sock)
            self.sock.close()

        except Exception:
            self.error = "ERROR: exception : '%s'" % traceback.format_exc()

        self.is_running = False

    def do_accept(self, sock, sel, logger):
        conn, addr = sock.accept()
        logger.log('%s Accepted connection from %s:%d' % (self.prefix, addr[0], addr[1]))
        conn.setblocking(False)
        events = selectors.EVENT_READ | selectors.EVENT_WRITE
        sel.register(conn, events, data=ClientRecord(addr))

    def do_service(self, key, mask, sel, logger, d1968):
        retval = 0
        sock = key.fileobj
        data = key.data
        if mask & selectors.EVENT_READ:
            try:
                recv_data = sock.recv(1024)
            except IOError:
                logger.log('%s Connection to %s:%d IOError: %s' %
                           (self.prefix, data.addr[0], data.addr[1], traceback.format_exc()))
                sel.unregister(sock)
                sock.close()
                return 0
            except Exception:
                self.error = ('%s Connection to %s:%d exception: %s' %
                              (self.prefix, data.addr[0], data.addr[1], traceback.format_exc()))
                logger.log(self.error)
                sel.unregister(sock)
                sock.close()
                return 1
            if recv_data:
                data.outb += recv_data
                logger.log('%s read from: %s:%d len:%d: %s' % (self.prefix, data.addr[0], data.addr[1], len(recv_data),
                                                               split_chunk_for_display(recv_data)))
                if d1968:
                    logger.log('%s Closing connection to %s:%d due to d1968' % (self.prefix, data.addr[0], data.addr[1]))
                    sel.unregister(sock)
                    sock.close()
                    return -1
                sel.modify(sock, selectors.EVENT_READ | selectors.EVENT_WRITE, data=data)
            else:
                logger.log('%s Closing connection to %s:%d' % (self.prefix, data.addr[0], data.addr[1]))
                sel.unregister(sock)
                sock.close()
                return 0
        if mask & selectors.EVENT_WRITE:
            if data.outb:
                try:
                    sent = sock.send(data.outb)
                except IOError:
                    logger.log('%s Connection to %s:%d IOError: %s' %
                               (self.prefix, data.addr[0], data.addr[1], traceback.format_exc()))
                    sel.unregister(sock)
                    sock.close()
                    return 0
                except Exception:
                    self.error = ('%s Connection to %s:%d exception: %s' %
                                  (self.prefix, data.addr[0], data.addr[1], traceback.format_exc()))
                    logger.log(self.error)
                    sel.unregister(sock)
                    sock.close()
                    return 1
                retval += sent
                if sent > 0:
                    logger.log('%s write to : %s:%d len:%d: %s' % (self.prefix, data.addr[0], data.addr[1], sent,
                                                                   split_chunk_for_display(data.outb[:sent])))
                else:
                    logger.log('%s write to : %s:%d len:0' % (self.prefix, data.addr[0], data.addr[1]))
                data.outb = data.outb[sent:]
            else:
                sel.modify(sock, selectors.EVENT_READ, data=data)
        return retval

    def wait(self, timeout=TIMEOUT):
        self.logger.log("%s Server is shutting down" % self.prefix)
        self.keep_running = False
        self._thread.join(timeout)


def main(argv):
    retval = 0
    logger = None
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
    p.add_argument('--D1968',
                   action='store_true',
                   help='Close client connection as soon as data arrives. TEST ONLY DISPATCH-1968')
    del argv[0]
    args = p.parse_args(argv)

    # port
    if args.port is None:
        raise Exception("User must specify a port number")
    port = args.port

    # name / prefix
    prefix = args.name if args.name is not None else "ECHO_SERVER (%s)" % (str(port))

    # echo
    if args.echo < 0:
        raise Exception("Echo count must be greater than zero")

    # timeout
    if args.timeout < 0.0:
        raise Exception("Timeout must be greater than or equal to zero")

    signaller = GracefulExitSignaler()
    server = None

    try:
        # logging
        logger = Logger(title="%s port %s" % (prefix, port),
                        print_to_console=args.log,
                        save_for_dump=False)

        server = TcpEchoServer(prefix, port, args.echo, args.timeout, logger, args.D1968)

        keep_running = True
        while keep_running:
            time.sleep(0.1)
            if server.error is not None:
                logger.log("%s Server stopped with error: %s" % (prefix, server.error))
                keep_running = False
                retval = 1
            if server.exit_status is not None:
                logger.log("%s Server stopped with status: %s" % (prefix, server.exit_status))
                keep_running = False
            if signaller.kill_now:
                logger.log("%s Process killed with signal" % prefix)
                keep_running = False
            if keep_running and not server.is_running:
                logger.log("%s Server stopped with no error or status" % prefix)
                keep_running = False

    except Exception:
        if logger is not None:
            logger.log("%s Exception: %s" % (prefix, traceback.format_exc()))
        retval = 1

    if server is not None and server.sock is not None:
        server.sock.close()

    return retval


if __name__ == "__main__":
    sys.exit(main(sys.argv))

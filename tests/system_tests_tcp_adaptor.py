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

import os
import time
import traceback
from threading import Event
from threading import Timer

from system_test import TestCase, Qdrouterd, main_module, TIMEOUT
from system_test import Logger
from system_test import QdManager
from system_test import unittest
from system_test import Process
from system_test import DIR
from qpid_dispatch.management.client import Node
from subprocess import PIPE, STDOUT
from TCP_echo_client import TcpEchoClient
from TCP_echo_server import TcpEchoServer


class TcpAdaptorOneRouterEcho(TestCase, Process):
    """
    Run echo tests through a stand-alone router
    """
    amqp_listener_port       = None
    tcp_client_listener_port = None
    tcp_server_listener_port = None

    echo_timeout = 30 # local timeout to wait for one echo client to finish

    @classmethod
    def setUpClass(cls):
        """Start a router"""
        super(TcpAdaptorOneRouterEcho, cls).setUpClass()

        def router(name, mode, l_amqp, l_tcp_client, l_tcp_server, addr, site, extra=None):
            config = [
                ('router', {'mode': mode, 'id': name}),
                ('listener', {'port': l_amqp, 'stripAnnotations': 'no'}),
                ('tcpConnector', {"host": "127.0.0.1",
                                  "port": l_tcp_server,
                                  "address": addr,
                                  "siteId": site}),
                ('tcpListener', {"host": "0.0.0.0",
                                 "port": l_tcp_client,
                                 "address": addr,
                                 "siteId": site})
            ]

            if extra:
                config.append(extra)
            config = Qdrouterd.Config(config)
            cls.routers.append(cls.tester.qdrouterd(name, config, wait=True))

        cls.routers = []

        cls.amqp_listener_port       = cls.tester.get_port()
        cls.tcp_client_listener_port = cls.tester.get_port()
        cls.tcp_server_listener_port = cls.tester.get_port()

        router('A', 'interior', cls.amqp_listener_port, cls.tcp_client_listener_port,
               cls.tcp_server_listener_port, "some_address", "best_site")

        cls.logger = Logger(title="TcpAdaptorOneRouterEcho-testClass",
                            print_to_console=True,
                            save_for_dump=False)
        cls.logger.log("  amqp_listener_port       : %d" % cls.amqp_listener_port)
        cls.logger.log("  tcp_client_listener_port : %d" % cls.tcp_client_listener_port)
        cls.logger.log("  tcp_server_listener_port : %d" % cls.tcp_server_listener_port)

    def do_test_echo(self, test_name, logger, host, port, size, count, print_client_logs):
        # Run echo client. Return true if it works.
        name = "%s_%s_%s_%s" % (test_name, port, size, count)
        client_prefix = "ECHO_CLIENT %s" % name
        client_logger = Logger(title=client_prefix,
                               print_to_console=print_client_logs,
                               save_for_dump=False)
        result = True # assume it works
        start_time = time.time()
        try:
            # start client
            client = TcpEchoClient(prefix=client_prefix,
                                   host=host,
                                   port=port,
                                   size=size,
                                   count=count,
                                   timeout=TIMEOUT,
                                   logger=client_logger)
            #assert client.is_running

            # wait for client to finish
            keep_waiting = True
            while keep_waiting:
                time.sleep(0.1)
                elapsed = time.time() - start_time
                if elapsed > self.echo_timeout:
                    client.error = "TIMEOUT - local wait time exceeded"
                    logger.log("%s %s" % (name, client.error))
                    keep_waiting = False
                    result = False
                if client.error is not None:
                    logger.log("%s Client stopped with error: %s" % (name, client.error))
                    keep_waiting = False
                    result = False
                if client.exit_status is not None:
                    logger.log("%s Client stopped with status: %s" % (name, client.exit_status))
                    keep_waiting = False
                    result = False
                if keep_waiting and not client.is_running:
                    logger.log("%s Client stopped with no error or status" % (name))
                    keep_waiting = False

        except Exception as exc:
            logger.log("EchoClient %s failed. Exception: %s" %
                       (name, traceback.format_exc()))
            result = False

        # wait for client to exit
        client.wait()

        if not result:
            pass

        return result

    def test_01_tcp_echo_one_router(self):
        """
        Run one echo server.
        Run many echo clients.
        :return:
        """
        # define logging
        print_logs_server = True
        print_logs_client = True

        # start echo server
        test_name = "test_01_tcp_echo_one_router"
        server_prefix = "ECHO_SERVER %s" % test_name
        server_logger = Logger(title=test_name,
                               print_to_console=print_logs_server,
                               save_for_dump=False)
        server = TcpEchoServer(prefix=server_prefix,
                               port=self.tcp_server_listener_port,
                               timeout=TIMEOUT,
                               logger=server_logger)
        assert server.is_running

        # run series of clients to test
        result = True
        for size in [1, 50, 1000]:
            for count in [1, 10]:
                # make sure server is still running
                if server.error is not None:
                    logger.log("%s Server stopped with error: %s" % (name, server.error))
                    result = False
                if server.exit_status is not None:
                    logger.log("%s Server stopped with status: %s" % (name, server.exit_status))
                    result = False
                # run another test client
                if result:
                    test_info = "Starting echo client %s host:localhost, port:%d, size:%d, count:%d" % \
                               (test_name, self.tcp_client_listener_port, size, count)
                    self.logger.log(test_info)
                    result = self.do_test_echo(test_name, self.logger, "localhost",
                                               self.tcp_client_listener_port, size, count,
                                               print_logs_client)
                if not result:
                    break
            if not result:
                break
        # stop echo server
        server.wait()
        assert result, "Test case failed %s" % test_info
        self.logger.log("Test test_01_tcp_echo_one_router: SUCCESS")

if __name__== '__main__':
    unittest.main(main_module())

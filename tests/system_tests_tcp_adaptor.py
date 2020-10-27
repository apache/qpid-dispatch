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
from time import sleep
from threading import Event
from threading import Timer

from proton import Message, Timeout
from system_test import TestCase, Qdrouterd, main_module, TIMEOUT, MgmtMsgProxy, TestTimeout, PollTimeout
from system_test import AsyncTestReceiver
from system_test import AsyncTestSender
from system_test import Logger
from system_test import QdManager
from system_test import unittest
from system_test import Process
from system_tests_link_routes import ConnLinkRouteService
from test_broker import FakeBroker
from test_broker import FakeService
from proton.handlers import MessagingHandler
from proton.reactor import Container, DynamicNodeProperties
from proton.utils import BlockingConnection
from qpid_dispatch.management.client import Node
from qpid_dispatch_internal.tools.command import version_supports_mutually_exclusive_arguments
from subprocess import PIPE, STDOUT
import re

class AddrTimer(object):
    def __init__(self, parent):
        self.parent = parent

    def on_timer_task(self, event):
        self.parent.check_address()


class TcpAdaptorOneRouterEcho(TestCase):
    """
    Run echo tests through a stand-alone router
    """
    amqp_listener_port       = None
    tcp_client_listener_port = None
    tcp_server_listener_port = None

    @classmethod
    def setUpClass(cls):
        """Start a router and echo server"""
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

        router('INT.A', 'interior', cls.amqp_listener_port, cls.tcp_client_listener_port,
               cls.tcp_server_listener_port, "some_address", "best_site")

        cls.logger = Logger(title="TCP echo one router", print_to_console=True)

    @classmethod
    def tearDownClass(cls):
        pass

    def spawn_echo_server(self, port, expect=None):
        cmd = ["TCP_echo_server.py",
               "--port", str(port),
               "--log"]
        return self.popen(cmd, name='echo-server', stdout=PIPE, expect=expect,
                          universal_newlines=True)

    def spawn_echo_client(self, logger, host, port, size, count, expect=None):
        if expect is None:
            expect = Process.EXIT_OK
        cmd = ["TCP_echo_client.py",
               "--host", host,
               "--port", str(port),
               "--size", str(size),
               "--count", str(count),
               "--log"]
        logger.log("Start client. cmd=%s" % str(cmd))
        return self.popen(cmd, name='echo-clint', stdout=PIPE, expect=expect,
                          universal_newlines=True)

    def do_test_echo(self, logger, host, port, size, count):
        # start echo client
        client = self.spawn_echo_client(logger, host, port, size, count)
        cl_text, cl_error = client.communicate(timeout=TIMEOUT)
        if client.returncode:
            raise Exception("Echo client failed size:%d, count:%d : %s %s" %
                            (size, count, cl_text, cl_error))

    def test_01_tcp_echo_one_router(self):
        # start echo server
        #server = self.spawn_echo_server(self.tcp_server_listener_port)

        #for size in [1, 5, 10, 50, 100]:
        #    for count in [1, 5, 10, 50, 100]:
        #        self.logger.log("Starting echo client host:localhost, port:%d, size:%d, count:%d" %
        #                   (self.tcp_client_listener_port, size, count))
        #        self.do_test_echo(self.logger, "localhost", self.tcp_client_listener_port, size, count)
        #server.join()
        pass

if __name__== '__main__':
    unittest.main(main_module())

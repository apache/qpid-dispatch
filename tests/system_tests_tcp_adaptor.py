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
from system_test import DIR
from system_test import SkipIfNeeded
from qpid_dispatch.management.client import Node
from subprocess import PIPE, STDOUT

try:
    from TCP_echo_client import TcpEchoClient
    from TCP_echo_server import TcpEchoServer
except ImportError:
    class TCP_echo_client(object):
        pass
    class TCP_echo_server(object):
        pass


DISABLE_SELECTOR_TESTS = False
DISABLE_SELECTOR_REASON = ''
try:
    import selectors
except ImportError:
    DISABLE_SELECTOR_TESTS = True
    DISABLE_SELECTOR_REASON = "Python selectors module is not available on this platform."


class TcpAdaptor(TestCase):
    """
    4 edge routers connected via 2 interior routers.
    6 echo servers are connected via tcpConnector, one to each router.
    Each router has 7 listeners, one for each server and
    another for which there is no server.
    """
    #  +-------+    +---------+    +---------+    +-------+
    #  |  EA1  |<-->|  INTA   |<==>|  INTB   |<-->|  EB1  |
    #  +-------+    |         |    |         |    +-------+
    #  +-------+    |         |    |         |    +-------+
    #  |  EA2  |<-->|         |    |         |<-->|  EB2  |
    #  +-------+    +---------+    +---------+    +-------+
    #
    # Each router tcp-connects to a like-named echo server.
    # Each router has tcp-listeners for ever echo server
    #
    #      +----+ +----+ +----+ +----+ +----+ +----+
    #   +--|tcp |-|tcp |-|tcp |-|tcp |-|tcp |-|tcp |--+
    #   |  |lsnr| |lsnr| |lsnr| |lsnr| |lsnr| |lsnr|  |
    #   |  |EA1 | |EA2 | |INTA| |INTB| |EB1 | |EB2 |  |
    #   |  +----+ +----+ +----+ +----+ +----+ +----+  |
    #   |                                          +---------+  +------+
    #   |          Router                          | tcp     |  | echo |
    #   |          EA1                             |connector|->|server|
    #   |                                          +---------+  | EA1  |
    #   |                                             |         +------+
    #   +---------------------------------------------+
    #

    # Allocate routers in this order
    router_order = ['INTA', 'INTB', 'EA1', 'EA2', 'EB1', 'EB2']

    # List indexed in router_order
    # First listener in each router is normal AMQP for test setup and mgmt.
    amqp_listener_ports       = {}

    # Each router listens for TCP where the tcp-address is the router name.
    # Each router has N listeners, one for the echo server connected to each router.
    tcp_client_listener_ports = {}

    # Each router connects to an echo server
    tcp_server_listener_ports = {}

    # Each router has a TCP listener that has no associated server
    nodest_listener_ports = {}

    # Each router has a console listener
    http_listener_ports = {}

    # local timeout in seconds to wait for one echo client to finish
    echo_timeout = 30

    # TCP siteId for listeners and connectors
    site = "mySite"

    @classmethod
    def setUpClass(cls):
        """Start a router"""
        super(TcpAdaptor, cls).setUpClass()

        if DISABLE_SELECTOR_TESTS:
            return

        def router(name, mode, connection, extra=None):
            """
            Launch a router through the system_test framework.
            For each router:
             * normal listener first
             * http listener for console connections
             * tcp listener for 'nodest', which will never exist
             * tcp connector to echo server whose address is the same as this router's name
             * six tcp listeners, one for each server on each router on the network
            :param name: router name
            :param mode: router mode: interior or edge
            :param connection: list of router-level connection/listener tuples
            :param extra: yet more configuation tuples. unused for now
            :return:
            """
            config = [
                ('router', {'mode': mode, 'id': name}),
                ('listener', {'port': cls.amqp_listener_ports[name]}),
                ('listener', {'port': cls.http_listener_ports[name], 'http': 'yes'}),
                ('tcpListener', {'host': "0.0.0.0",
                                 'port': cls.nodest_listener_ports[name],
                                 'address': 'nodest',
                                 'siteId': cls.site}),
                ('tcpConnector', {'host': "127.0.0.1",
                                  'port': cls.tcp_server_listener_ports[name],
                                  'address': name,
                                  'siteId': cls.site})
            ]
            if connection:
                config.extend(connection)
            listeners = []
            for rtr in cls.router_order:
                listener = {'host': "0.0.0.0",
                            'port': cls.tcp_client_listener_ports[name][rtr],
                            'address': rtr,
                            'siteId': cls.site}
                tup = [(('tcpListener', listener))]
                listeners.extend( tup )
            config.extend(listeners)

            if extra:
                config.extend(extra)

            config = Qdrouterd.Config(config)
            cls.routers.append(cls.tester.qdrouterd(name, config, wait=True))

        cls.routers = []

        # Allocate a sea of ports
        for rtr in cls.router_order:
            cls.amqp_listener_ports[rtr] = cls.tester.get_port()
            cls.tcp_server_listener_ports[rtr] = cls.tester.get_port()
            tl_ports = {}
            for tcp_listener in cls.router_order:
                tl_ports[tcp_listener] = cls.tester.get_port()
            cls.tcp_client_listener_ports[rtr] = tl_ports
            cls.nodest_listener_ports[rtr] = cls.tester.get_port()
            cls.http_listener_ports[rtr] = cls.tester.get_port()

        inter_router_port = cls.tester.get_port()
        cls.INTA_edge_port   = cls.tester.get_port()
        cls.INTB_edge_port   = cls.tester.get_port()

        cls.logger = Logger(title="TcpAdaptor-testClass",
                            print_to_console=True,
                            save_for_dump=False)

        # Create a scoreboard for the ports
        p_out = []
        for rtr in cls.router_order:
            p_out.append("%s_amqp=%d" %
                         (rtr, cls.amqp_listener_ports[rtr]))
            p_out.append("%s_echo_server=%d" %
                         (rtr, cls.tcp_server_listener_ports[rtr]))
            for tcp_listener in cls.router_order:
                p_out.append("%s_echo_listener_for_%s=%d" %
                             (rtr, tcp_listener, cls.tcp_client_listener_ports[rtr][tcp_listener]))
            p_out.append("%s_nodest_listener=%d" %
                         (rtr, cls.nodest_listener_ports[rtr]))
            p_out.append("%s_http_listener=%d" %
                         (rtr, cls.http_listener_ports[rtr]))
        p_out.append("inter_router_port=%d" % inter_router_port)
        p_out.append("INTA_edge_port=%d" % cls.INTA_edge_port)
        p_out.append("INTB_edge_port=%d" % cls.INTB_edge_port)
        # write to log
        for line in p_out:
            cls.logger.log("TCP_TEST %s" % line)
        # write to shell script
        with open("../setUpClass/TcpAdaptor-ports.sh", 'w') as o_file:
            for line in p_out:
                o_file.write("set %s\n" % line)

        # Launch the routers
        router('INTA', 'interior',
               [('listener', {'role': 'inter-router', 'port': inter_router_port}),
                ('listener', {'name': 'uplink', 'role': 'edge', 'port': cls.INTA_edge_port})])

        router('INTB', 'interior',
               [('connector', {'role': 'inter-router', 'port': inter_router_port}),
               ('listener',   {'name': 'uplink', 'role': 'edge', 'port': cls.INTB_edge_port})])

        router('EA1', 'edge',
               [('connector', {'name': 'uplink', 'role': 'edge', 'port': cls.INTA_edge_port})])
        router('EA2', 'edge',
               [('connector', {'name': 'uplink', 'role': 'edge', 'port': cls.INTA_edge_port})])
        router('EB1', 'edge',
               [('connector', {'name': 'uplink', 'role': 'edge', 'port': cls.INTB_edge_port})])
        router('EB2', 'edge',
               [('connector', {'name': 'uplink', 'role': 'edge', 'port': cls.INTB_edge_port})])

        cls.INTA = cls.routers[0]
        cls.INTB = cls.routers[1]
        cls.EA1 = cls.routers[2]
        cls.EA2 = cls.routers[3]
        cls.EB1 = cls.routers[4]
        cls.EB2 = cls.routers[5]

        cls.logger.log("INTA waiting for connection to INTB")
        cls.INTA.wait_router_connected('INTB')
        cls.logger.log("INTB waiting for connection to INTA")
        cls.INTB.wait_router_connected('INTA')

        # define logging
        cls.print_logs_server = True
        cls.print_logs_client = True

        # start echo servers
        cls.echo_servers = {}
        for rtr in cls.router_order:
            test_name = "TcpAdaptor"
            server_prefix = "ECHO_SERVER %s addr %s" % (test_name, rtr)
            server_logger = Logger(title=test_name,
                                   print_to_console=cls.print_logs_server,
                                   save_for_dump=False)
            cls.logger.log("TCP_TEST Launching echo server '%s'" % server_prefix)
            server = TcpEchoServer(prefix=server_prefix,
                                   port=cls.tcp_server_listener_ports[rtr],
                                   timeout=TIMEOUT,
                                   logger=server_logger)
            assert server.is_running
            cls.echo_servers[rtr] = server

        # sleep so user can verify the mess
        # cls.logger.log("Verify the setup while I sleep 5 min: firefox http://localhost:%d" % cls.http_listener_ports["INTA"])
        # time.sleep(300)

    @classmethod
    def tearDownClass(cls):
        # stop echo servers
        for rtr in cls.router_order:
            server = cls.echo_servers[rtr]
            cls.logger.log("TCP_TEST Stopping echo server %s" % rtr)
            server.wait()

    def do_test_echo(self, test_name, logger, client, server, size, count, print_client_logs):
        # Run echo client. Return true if it works.

        # Each router has a listener for the echo server attached to every router
        listener_port = self.tcp_client_listener_ports[client][server]

        name = "%s_%s_%s_%s_%s" % (test_name, client, server, size, count)
        client_prefix = "ECHO_CLIENT %s" % name
        client_logger = Logger(title=client_prefix,
                               print_to_console=print_client_logs,
                               save_for_dump=False)
        result = None # assume it works
        start_time = time.time()
        try:
            # start client
            e_client = TcpEchoClient(prefix=client_prefix,
                                     host='localhost',
                                     port=listener_port,
                                     size=size,
                                     count=count,
                                     timeout=TIMEOUT,
                                     logger=client_logger)

            # wait for client to finish
            keep_waiting = True
            while keep_waiting:
                time.sleep(0.1)
                elapsed = time.time() - start_time
                if elapsed > self.echo_timeout:
                    e_client.error = "TCP_TEST TIMEOUT - local wait time exceeded"
                    logger.log("%s %s" % (name, e_client.error))
                    keep_waiting = False
                    result = e_client.error
                    break
                if e_client.error is not None:
                    logger.log("TCP_TEST %s Client stopped with error: %s" % (name, e_client.error))
                    keep_waiting = False
                    result = e_client.error
                if e_client.exit_status is not None:
                    logger.log("TCP_TEST %s Client stopped with status: %s" % (name, e_client.exit_status))
                    keep_waiting = False
                    result = e_client.exit_status
                if keep_waiting and not e_client.is_running:
                    # this is how clients exit normally
                    s = "TCP_TEST %s Client exited normally with no error or status" % name
                    logger.log(s)
                    keep_waiting = False

            # wait for client to exit
            e_client.wait()

        except Exception as exc:
            e_client.error = "TCP_TEST EchoClient %s failed. Exception: %s" % \
                              (name, traceback.format_exc())
            logger.log(e_client.error)
            result = e_client.error

        return result

    def do_tcp_echo_two_router(self, test_name, client_rtr, server_rtr):
        """
        Server is running
        Run echo_client to client_rtr's listener port that goes to server_rtr's echo server
        :return:
        """
        client = client_rtr.name
        server = server_rtr.name
        self.logger.log("TCP_TEST Start do_tcp_echo_two_router client: %s, server:%s" % (client, server))
        result = None
        for size in [1]:
            for count in [1]:
                # make sure server is still running
                s_error = self.echo_servers[server].error
                s_status = self.echo_servers[server].exit_status
                if s_error is not None:
                    self.logger.log("TCP_TEST %s Server %s stopped with error: %s" % (test_name, server, s_error))
                    result = s_error
                if s_status is not None:
                    self.logger.log("TCP_TEST %s Server %s stopped with status: %s" % (test_name, server, s_status))
                    result = s_status
                # run another test client
                if result is None:
                    test_info = "TCP_TEST Starting echo client '%s' client_rtr:%s, server:%s size:%d, count:%d" % \
                               (test_name, client, server, size, count)
                    self.logger.log(test_info)
                    result = self.do_test_echo(test_name, self.logger,
                                               client, server, size, count,
                                               self.print_logs_client)
                    if result is not None:
                        self.logger.log("TCP_TEST test %s fail: %s" % (test_name, result))
                if result is not None:
                    break
            if result is not None:
                break

        self.logger.log("TCP_TEST Stop do_tcp_echo_two_router client: %s, server:%s" % (client, server))
        return result

    # Run a series of tests against the router/echo_server network

    @SkipIfNeeded(DISABLE_SELECTOR_TESTS, DISABLE_SELECTOR_REASON)
    def test_01_tcp_INTA_INTA(self):
        name = "test_01_tcp_INTA_INTA"
        self.logger.log("TCP_TEST Start %s" % name)
        result = self.do_tcp_echo_two_router("tcp", self.INTA, self.INTA)
        assert result is None, "TCP_TEST Stop %s FAIL: %s" % (name, result)
        self.logger.log("TCP_TEST Stop %s SUCCESS" % name)

    @SkipIfNeeded(DISABLE_SELECTOR_TESTS, DISABLE_SELECTOR_REASON)
    def test_02_tcp_INTB_INTB(self):
        name = "test_02_tcp_INTB_INTB"
        self.logger.log("TCP_TEST Start %s" % name)
        result = self.do_tcp_echo_two_router("tcp", self.INTB, self.INTB)
        assert result is None, "TCP_TEST Stop %s FAIL: %s" % (name, result)
        self.logger.log("TCP_TEST Stop %s SUCCESS" % name)

    @SkipIfNeeded(DISABLE_SELECTOR_TESTS, DISABLE_SELECTOR_REASON)
    def test_03_tcp_INTA_INTB(self):
        name = "test_03_tcp_INTA_INTB"
        self.logger.log("TCP_TEST Start %s" % name)
        result = self.do_tcp_echo_two_router("tcp", self.INTA, self.INTB)
        assert result is None, "TCP_TEST Stop %s FAIL: %s" % (name, result)
        self.logger.log("TCP_TEST Stop %s SUCCESS" % name)

    @SkipIfNeeded(DISABLE_SELECTOR_TESTS, DISABLE_SELECTOR_REASON)
    def test_04_tcp_EA1_EA1(self):
        name = "test_04_tcp_EA1_EA1"
        self.logger.log("TCP_TEST Start %s" % name)
        result = self.do_tcp_echo_two_router("tcp", self.EA1, self.EA1)
        assert result is None, "TCP_TEST Stop %s FAIL: %s" % (name, result)
        self.logger.log("TCP_TEST Stop %s SUCCESS" % name)

    @SkipIfNeeded(DISABLE_SELECTOR_TESTS, DISABLE_SELECTOR_REASON)
    def test_05_tcp_EA1_EA2(self):
        name = "test_05_tcp_EA1_EA2"
        self.logger.log("TCP_TEST Start %s" % name)
        result = self.do_tcp_echo_two_router("tcp", self.EA1, self.EA2)
        assert result is None, "TCP_TEST Stop %s FAIL: %s" % (name, result)
        self.logger.log("TCP_TEST Stop %s SUCCESS" % name)


if __name__== '__main__':
    unittest.main(main_module())

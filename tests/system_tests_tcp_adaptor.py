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
import sys
import time
import traceback
from threading import Event
from threading import Timer

from system_test import TestCase, Qdrouterd, main_module, TIMEOUT
from system_test import Timestamp
from system_test import QdManager
from system_test import unittest
from system_test import DIR
from system_test import SkipIfNeeded
from system_test import Process
from qpid_dispatch.management.client import Node
from subprocess import PIPE, STDOUT

# Tests in this file are organized by classes that inherit TestCase.
# The first instance is TcpAdaptor(TestCase).
# The tests emit files that are named starting with 'TcpAdaptor'. This includes
# logs and shell scripts.
# Subsequent TestCase subclasses must follow this pattern and emit files named
# with the test class name at the beginning of the emitted files.

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

class Logger():
    """
    Record event logs as existing Logger. Also add:
    * ofile  - optional file opened in 'append' mode to which each log line is written
    TODO: Replace system_test Logger with this after merging dev-protocol-adaptors branch
    """
    def __init__(self,
                 title="Logger",
                 print_to_console=False,
                 save_for_dump=True,
                 ofilename=None):
        self.title = title
        self.print_to_console = print_to_console
        self.save_for_dump = save_for_dump
        self.logs = []
        self.ofilename = ofilename

    def log(self, msg):
        ts = Timestamp()
        if self.save_for_dump:
            self.logs.append( (ts, msg) )
        if self.print_to_console:
            print("%s %s" % (ts, msg))
            sys.stdout.flush()
        if self.ofilename is not None:
            with open(self.ofilename, 'a') as f_out:
                f_out.write("%s %s\n" % (ts, msg))
                f_out.flush()

    def dump(self):
        print(self)
        sys.stdout.flush()

    def __str__(self):
        lines = []
        lines.append(self.title)
        for ts, msg in self.logs:
            lines.append("%s %s" % (ts, msg))
        res = str('\n'.join(lines))
        return res


class TcpAdaptor(TestCase):
    """
    6 edge routers connected via 3 interior routers.
    9 echo servers are connected via tcpConnector, one to each router.
    Each router has 10 listeners, one for each server and
    another for which there is no server.
    """
    #  +-------+    +---------+    +---------+    +---------+    +-------+
    #  |  EA1  |<-->|  INTA   |<==>|  INTB   |<==>|  INTC   |<-->|  EC1  |
    #  +-------+    |         |    |         |    |         |    +-------+
    #  +-------+    |         |    |         |    |         |    +-------+
    #  |  EA2  |<-->|         |    |         |    |         |<-->|  EC2  |
    #  +-------+    +---------+    +---------+    +---------+    +-------+
    #                                ^     ^
    #                                |     |
    #                          +-------+ +-------+
    #                          |  EB1  | |  EB2  |
    #                          +-------+ +-------+
    #
    # Each router tcp-connects to a like-named echo server.
    # Each router has tcp-listeners for every echo server
    #
    #      +----+ +----+ +----+ +----+ +----+ +----+ +----+ +----+ +----+
    #   +--|tcp |-|tcp |-|tcp |-|tcp |-|tcp |-|tcp |-|tcp |-|tcp |-|tcp |--+
    #   |  |lsnr| |lsnr| |lsnr| |lsnr| |lsnr| |lsnr| |lsnr| |lsnr| |lsnr|  |
    #   |  |EA1 | |EA2 | |INTA| |EB1 | |EB2 | |INTB| |EC1 | |EC2 | |INTC|  |
    #   |  +----+ +----+ +----+ +----+ +----+ +----+ +----+ +----+ +----+  |
    #   |                                                               +---------+  +------+
    #   |          Router                                               | tcp     |  | echo |
    #   |          EA1                                                  |connector|->|server|
    #   |                                                               +---------+  | EA1  |
    #   |                                                                  |         +------+
    #   +------------------------------------------------------------------+
    #

    # Allocate routers in this order
    router_order = ['INTA', 'INTB', 'INTC', 'EA1', 'EA2', 'EB1', 'EB2', 'EC1', 'EC2']

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
    #http_listener_ports = {}

    # local timeout in seconds to wait for one echo client to finish
    echo_timeout = 30

    # TCP siteId for listeners and connectors
    site = "mySite"

    # Each router has an echo server to which it connects
    echo_servers = {}

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
             #* http listener for console connections
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
                #('listener', {'port': cls.http_listener_ports[name], 'http': 'yes'}),
                ('tcpListener', {'host': "0.0.0.0",
                                 'port': cls.nodest_listener_ports[name],
                                 'address': 'nodest',
                                 'siteId': cls.site}),
                ('tcpConnector', {'host': "127.0.0.1",
                                  'port': cls.tcp_server_listener_ports[name],
                                  'address': 'ES_' + name,
                                  'siteId': cls.site})
            ]
            if connection:
                config.extend(connection)
            listeners = []
            for rtr in cls.router_order:
                listener = {'host': "0.0.0.0",
                            'port': cls.tcp_client_listener_ports[name][rtr],
                            'address': 'ES_' + rtr,
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
            #cls.http_listener_ports[rtr] = cls.tester.get_port()

        inter_router_port_AB  = cls.tester.get_port()
        inter_router_port_BC  = cls.tester.get_port()
        cls.INTA_edge_port = cls.tester.get_port()
        cls.INTB_edge_port = cls.tester.get_port()
        cls.INTC_edge_port = cls.tester.get_port()

        cls.logger = Logger(title="TcpAdaptor-testClass",
                            print_to_console=True,
                            save_for_dump=False,
                            ofilename='../setUpClass/TcpAdaptor.log')

        # Write a dummy log line for scraper.
        cls.logger.log("SERVER (info) Container Name: TCP_TEST")

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
            #p_out.append("%s_http_listener=%d" %
            #             (rtr, cls.http_listener_ports[rtr]))
        p_out.append("inter_router_port_AB=%d" % inter_router_port_AB)
        p_out.append("inter_router_port_BC=%d" % inter_router_port_BC)
        p_out.append("INTA_edge_port=%d" % cls.INTA_edge_port)
        p_out.append("INTB_edge_port=%d" % cls.INTB_edge_port)
        p_out.append("INTC_edge_port=%d" % cls.INTC_edge_port)
        # write to log
        for line in p_out:
            cls.logger.log("TCP_TEST %s" % line)
        # write to shell script
        with open("../setUpClass/TcpAdaptor-ports.sh", 'w') as o_file:
            for line in p_out:
                o_file.write("set %s\n" % line)

        # Write a script to run scraper on this test's log files
        scraper_abspath = os.path.join(os.environ.get('BUILD_DIR'), 'tests', 'scraper', 'scraper.py')
        logs_dir     = os.path.abspath("../setUpClass")
        main_log     = "TcpAdaptor.log"
        echo_logs    = "TcpAdaptor_echo*"
        big_test_log = "TcpAdaptor_all.log"
        int_logs     = "I*.log"
        edge_logs    = "E*.log"
        log_modules_spec = "--log-modules TCP_ADAPTOR,TCP_TEST,ECHO_SERVER,ECHO_CLIENT"
        html_output  = "TcpAdaptor.html"

        with open("../setUpClass/TcpAdaptor-run-scraper.sh", 'w') as o_file:
            o_file.write("#!/bin/bash\n\n")
            o_file.write("# Script to run scraper on test class TcpAdaptor test result\n")
            o_file.write("# cd into logs directory\n")
            o_file.write("cd %s\n\n" % logs_dir)
            o_file.write("# Concatenate test class logs into single file\n")
            o_file.write("cat %s %s > %s\n\n" % (main_log, echo_logs, big_test_log))
            o_file.write("# run scraper\n")
            o_file.write("python %s %s -f %s %s %s > %s\n\n" %
                         (scraper_abspath, log_modules_spec, int_logs, edge_logs, big_test_log, html_output))
            o_file.write("echo View the results by opening the html file\n")
            o_file.write("echo     firefox %s" % (os.path.join(logs_dir, html_output)))

        # Launch the routers
        router('INTA', 'interior',
               [('listener', {'role': 'inter-router', 'port': inter_router_port_AB}),
                ('listener', {'name': 'uplink', 'role': 'edge', 'port': cls.INTA_edge_port})])

        router('INTB', 'interior',
               [('connector', {'role': 'inter-router', 'port': inter_router_port_AB}),
                ('listener', {'role': 'inter-router', 'port': inter_router_port_BC}),
                ('listener', {'name': 'uplink', 'role': 'edge', 'port': cls.INTB_edge_port})])

        router('INTC', 'interior',
               [('connector', {'role': 'inter-router', 'port': inter_router_port_BC}),
                ('listener', {'name': 'uplink', 'role': 'edge', 'port': cls.INTC_edge_port})])

        router('EA1', 'edge',
               [('connector', {'name': 'uplink', 'role': 'edge', 'port': cls.INTA_edge_port})])
        router('EA2', 'edge',
               [('connector', {'name': 'uplink', 'role': 'edge', 'port': cls.INTA_edge_port})])
        router('EB1', 'edge',
               [('connector', {'name': 'uplink', 'role': 'edge', 'port': cls.INTB_edge_port})])
        router('EB2', 'edge',
               [('connector', {'name': 'uplink', 'role': 'edge', 'port': cls.INTB_edge_port})])
        router('EC1', 'edge',
               [('connector', {'name': 'uplink', 'role': 'edge', 'port': cls.INTC_edge_port})])
        router('EC2', 'edge',
               [('connector', {'name': 'uplink', 'role': 'edge', 'port': cls.INTC_edge_port})])

        cls.INTA = cls.routers[0]
        cls.INTB = cls.routers[1]
        cls.INTC = cls.routers[2]
        cls.EA1 = cls.routers[3]
        cls.EA2 = cls.routers[4]
        cls.EB1 = cls.routers[5]
        cls.EB2 = cls.routers[6]
        cls.EC1 = cls.routers[7]
        cls.EC2 = cls.routers[8]

        cls.router_dict = {}
        cls.router_dict['INTA'] = cls.INTA
        cls.router_dict['INTB'] = cls.INTB
        cls.router_dict['INTC'] = cls.INTC
        cls.router_dict['EA1'] = cls.EA1
        cls.router_dict['EA2'] = cls.EA2
        cls.router_dict['EB1'] = cls.EB1
        cls.router_dict['EB2'] = cls.EB2
        cls.router_dict['EC1'] = cls.EC1
        cls.router_dict['EC2'] = cls.EC2

        cls.logger.log("TCP_TEST INTA waiting for connection to INTB")
        cls.INTA.wait_router_connected('INTB')
        cls.logger.log("TCP_TEST INTB waiting for connection to INTA")
        cls.INTB.wait_router_connected('INTA')
        cls.logger.log("TCP_TEST INTB waiting for connection to INTC")
        cls.INTB.wait_router_connected('INTC')
        cls.logger.log("TCP_TEST INTC waiting for connection to INTB")
        cls.INTC.wait_router_connected('INTB')

        # define logging levels
        cls.print_logs_server = False
        cls.print_logs_client = True

        # start echo servers
        for rtr in cls.router_order:
            test_name = "TcpAdaptor"
            server_prefix = "ECHO_SERVER %s ES_%s" % (test_name, rtr)
            server_logger = Logger(title=test_name,
                                   print_to_console=cls.print_logs_server,
                                   save_for_dump=False,
                                   ofilename="../setUpClass/TcpAdaptor_echo_server_%s.log" % rtr)
            cls.logger.log("TCP_TEST Launching echo server '%s'" % server_prefix)
            server = TcpEchoServer(prefix=server_prefix,
                                   port=cls.tcp_server_listener_ports[rtr],
                                   logger=server_logger, d1968=True)
            assert server.is_running
            cls.echo_servers[rtr] = server

        # wait for server addresses (mobile ES_<rtr>) to propagate to all interior routers
        interior_rtrs = [rtr for rtr in cls.router_order if rtr.startswith('I')]
        found_all = False
        while not found_all:
            found_all = True
            cls.logger.log("TCP_TEST Poll wait for echo server addresses to propagate")
            for rtr in interior_rtrs:
                # query each interior for addresses
                p = Process(
                    ['qdstat', '-b', str(cls.router_dict[rtr].addresses[0]), '-a'],
                    name='qdstat-snap1', stdout=PIPE, expect=None,
                    universal_newlines=True)
                out = p.communicate()[0]
                # examine what this router can see; signal poll loop to continue or not
                lines = out.split("\n")
                server_lines = [line for line in lines if "mobile" in line and "ES_" in line]
                if not len(server_lines) == len(cls.router_order):
                    found_all = False
                    seen = []
                    for line in server_lines:
                        flds = line.split()
                        seen.extend([fld for fld in flds if fld.startswith("ES_")])
                    unseen = [srv for srv in cls.router_order if "ES_" + srv not in seen]
                    cls.logger.log("TCP_TEST Router %s sees only %d of %d addresses. Waiting for %s" %
                                   (rtr, len(server_lines), len(cls.router_order), unseen))
        cls.logger.log("TCP_TEST Done poll wait")

    @classmethod
    def tearDownClass(cls):
        # stop echo servers
        for rtr in cls.router_order:
            server = cls.echo_servers.get(rtr)
            if not server is None:
                cls.logger.log("TCP_TEST Stopping echo server %s" % rtr)
                server.wait()
        super(TcpAdaptor, cls).tearDownClass()

    #
    # Test concurrent clients
    #
    class EchoClientRunner():
        """
        Launch an echo client upon construction.
        Provide poll interface for checking done/error.
        Provide wait/join to shut down.
        """
        def __init__(self, test_name, client_n, logger, client, server, size, count, print_client_logs):
            """
            Launch an echo client upon construction.

            :param test_name: Unique name for log file prefix
            :param client_n: Client number for differentiating otherwise identical clients
            :param logger: parent logger for logging test activity vs. client activity
            :param client: router name to which the client connects
            :param server: name whose address the client is targeting
            :param size: length of messages in bytes
            :param count: number of messages to be sent/verified
            :param print_client_logs: verbosity switch
            :return Null if success else string describing error
            """
            self.test_name = test_name
            self.client_n = str(client_n)
            self.logger = logger
            self.client = client
            self.server = server
            self.size = size
            self.count = count
            self.print_client_logs = print_client_logs
            self.client_final = False

            # Each router has a listener for the echo server attached to every router
            self.listener_port = TcpAdaptor.tcp_client_listener_ports[self.client][self.server]

            self.name = "%s_%s_%s_%s" % (self.test_name, self.client_n, self.size, self.count)
            self.client_prefix = "ECHO_CLIENT %s" % self.name
            self.client_logger = Logger(title=self.client_prefix,
                                        print_to_console=self.print_client_logs,
                                        save_for_dump=False,
                                        ofilename="../setUpClass/TcpAdaptor_echo_client_%s.log" % self.name)

            try:
                self.e_client = TcpEchoClient(prefix=self.client_prefix,
                                              host='localhost',
                                              port=self.listener_port,
                                              size=self.size,
                                              count=self.count,
                                              timeout=TIMEOUT,
                                              logger=self.client_logger)

            except Exception as exc:
                self.e_client.error = "TCP_TEST TcpAdaptor_runner_%s failed. Exception: %s" % \
                                      (self.name, traceback.format_exc())
                self.logger.log(self.e_client.error)
                raise Exception(self.e_client.error)

        def client_error(self):
            return self.e_client.error

        def client_exit_status(self):
            return self.e_client.exit_status

        def client_running(self):
            return self.e_client.is_running

        def wait(self):
            # wait for client to exit
            # Return None if successful wait/join/exit/close else error message
            result = None
            try:
                self.e_client.wait()

            except Exception as exc:
                self.e_client.error = "TCP_TEST EchoClient %s failed. Exception: %s" % \
                                      (self.name, traceback.format_exc())
                self.logger.log(self.e_client.error)
                result = self.e_client.error
            return result

    class EchoPair():
        """
        For the concurrent tcp tests this class describes one of the client-
        server echo pairs and the traffic pattern between them.
        """
        def __init__(self, client_rtr, server_rtr, sizes=None, counts=None):
            self.client_rtr = client_rtr
            self.server_rtr = server_rtr
            self.sizes = [1] if sizes is None else sizes
            self.counts = [1] if counts is None else counts

    def do_tcp_echo_n_routers(self, test_name, echo_pair_list):
        """
        Launch all the echo pairs defined in the list
        Wait for completion.
        :param test_name test name
        :param echo_pair_list list of EchoPair objects describing the test
        :return: None if success else error message for ctest
        """
        self.logger.log("TCP_TEST %s Start do_tcp_echo_n_routers" % (test_name))
        result = None
        runners = []
        client_num = 0
        start_time = time.time()

        try:
            # Launch the runners
            for echo_pair in echo_pair_list:
                client = echo_pair.client_rtr.name
                server = echo_pair.server_rtr.name
                for size in echo_pair.sizes:
                    for count in echo_pair.counts:
                        log_msg = "TCP_TEST %s Running pair %d %s->%s size=%d count=%d" % \
                                  (test_name, client_num, client, server, size, count)
                        self.logger.log(log_msg)
                        runner = self.EchoClientRunner(test_name, client_num, self.logger,
                                                       client, server, size, count,
                                                       self.print_logs_client)
                        runners.append(runner)
                        client_num += 1

            # Loop until timeout, error, or completion
            while result is None:
                # Check for timeout
                time.sleep(0.1)
                elapsed = time.time() - start_time
                if elapsed > self.echo_timeout:
                    result = "TCP_TEST TIMEOUT - local wait time exceeded"
                    break
                # Make sure servers are still up
                for rtr in TcpAdaptor.router_order:
                    es =TcpAdaptor.echo_servers[rtr]
                    if es.error is not None:
                        self.logger.log("TCP_TEST %s Server %s stopped with error: %s" %
                                        (test_name, es.prefix, es.error))
                        result = es.error
                        break
                    if es.exit_status is not None:
                        self.logger.log("TCP_TEST %s Server %s stopped with status: %s" %
                                        (test_name, es.prefix, es.exit_status))
                        result = es.exit_status
                        break
                if result is not None:
                    break

                # Check for completion or runner error
                complete = True
                for runner in runners:
                    if not runner.client_final:
                        error = runner.client_error()
                        if error is not None:
                            self.logger.log("TCP_TEST %s Client %s stopped with error: %s" %
                                            (test_name, runner.name, error))
                            result = error
                            runner.client_final = True
                            break
                        status = runner.client_exit_status()
                        if status is not None:
                            self.logger.log("TCP_TEST %s Client %s stopped with status: %s" %
                                            (test_name, runner.name, status))
                            result = status
                            runner.client_final = True
                            break
                        running = runner.client_running()
                        if running:
                            complete = False
                        else:
                            self.logger.log("TCP_TEST %s Client %s exited normally" %
                                            (test_name, runner.name))
                            runner.client_final = True
                if complete and result is None:
                    self.logger.log("TCP_TEST %s SUCCESS" %
                                    test_name)
                    break

            # Wait/join all the runners
            for runner in runners:
                runner.wait()

            if result is not None:
                self.logger.log("TCP_TEST %s failed: %s" % (test_name, result))

        except Exception as exc:
            result = "TCP_TEST %s failed. Exception: %s" % \
                              (test_name, traceback.format_exc())

        return result

    #
    # A series of 1-byte messsages, one at a time, to prove general connectivity
    #
    @SkipIfNeeded(DISABLE_SELECTOR_TESTS, DISABLE_SELECTOR_REASON)
    def test_01_tcp_basic_connectivity(self):
        """
        Echo a series of 1-byte messages, one at a time, to prove general connectivity.
        Every listener is tried. Proves every router can forward to servers on
        every other router.
        """
        for l_rtr in self.router_order:
            for s_rtr in self.router_order:
                name = "test_01_tcp_%s_%s" % (l_rtr, s_rtr)
                self.logger.log("TCP_TEST test_01_tcp_basic_connectivity Start %s" % name)
                pairs = [self.EchoPair(self.router_dict[l_rtr], self.router_dict[s_rtr])]
                result = self.do_tcp_echo_n_routers(name, pairs)
                if result is not None:
                    print(result)
                    sys.stdout.flush()
                assert result is None, "TCP_TEST test_01_tcp_basic_connectivity Stop %s FAIL: %s" % (name, result)
                self.logger.log("TCP_TEST test_01_tcp_basic_connectivity Stop %s SUCCESS" % name)

    # larger messages
    @SkipIfNeeded(DISABLE_SELECTOR_TESTS, DISABLE_SELECTOR_REASON)
    def test_10_tcp_INTA_INTA_100(self):
        name = "test_10_tcp_INTA_INTA_100"
        self.logger.log("TCP_TEST Start %s" % name)
        pairs = [self.EchoPair(self.INTA, self.INTA, sizes=[100])]
        result = self.do_tcp_echo_n_routers(name, pairs)
        if result is not None:
            print(result)
            sys.stdout.flush()
        assert result is None, "TCP_TEST Stop %s FAIL: %s" % (name, result)
        self.logger.log("TCP_TEST Stop %s SUCCESS" % name)

    @SkipIfNeeded(DISABLE_SELECTOR_TESTS, DISABLE_SELECTOR_REASON)
    def test_11_tcp_INTA_INTA_1000(self):
        name = "test_11_tcp_INTA_INTA_1000"
        self.logger.log("TCP_TEST Start %s" % name)
        pairs = [self.EchoPair(self.INTA, self.INTA, sizes=[1000])]
        result = self.do_tcp_echo_n_routers(name, pairs)
        if result is not None:
            print(result)
            sys.stdout.flush()
        assert result is None, "TCP_TEST Stop %s FAIL: %s" % (name, result)
        self.logger.log("TCP_TEST Stop %s SUCCESS" % name)

    @SkipIfNeeded(DISABLE_SELECTOR_TESTS, DISABLE_SELECTOR_REASON)
    def test_12_tcp_INTA_INTA_500000(self):
        name = "test_12_tcp_INTA_INTA_500000"
        self.logger.log("TCP_TEST Start %s" % name)
        pairs = [self.EchoPair(self.INTA, self.INTA, sizes=[500000])]
        result = self.do_tcp_echo_n_routers(name, pairs)
        if result is not None:
            print(result)
            sys.stdout.flush()
        assert result is None, "TCP_TEST Stop %s FAIL: %s" % (name, result)
        self.logger.log("TCP_TEST Stop %s SUCCESS" % name)

    @SkipIfNeeded(DISABLE_SELECTOR_TESTS, DISABLE_SELECTOR_REASON)
    def test_13_tcp_EA1_EC2_500000(self):
        name = "test_12_tcp_EA1_EC2_500000"
        self.logger.log("TCP_TEST Start %s" % name)
        pairs = [self.EchoPair(self.INTA, self.INTA, sizes=[500000])]
        result = self.do_tcp_echo_n_routers(name, pairs)
        if result is not None:
            print(result)
            sys.stdout.flush()
        assert result is None, "TCP_TEST Stop %s FAIL: %s" % (name, result)
        self.logger.log("TCP_TEST Stop %s SUCCESS" % name)

    @SkipIfNeeded(DISABLE_SELECTOR_TESTS, DISABLE_SELECTOR_REASON)
    def test_20_tcp_connect_disconnect(self):
        name = "test_20_tcp_connect_disconnect"
        self.logger.log("TCP_TEST Start %s" % name)
        pairs = [self.EchoPair(self.INTA, self.INTA, sizes=[0])]
        result = self.do_tcp_echo_n_routers(name, pairs)
        if result is not None:
            print(result)
            sys.stdout.flush()
        assert result is None, "TCP_TEST Stop %s FAIL: %s" % (name, result)
        # TODO: This test passes but in passing router INTA crashes undetected.
        self.logger.log("TCP_TEST Stop %s SUCCESS" % name)

    # concurrent messages
    @SkipIfNeeded(DISABLE_SELECTOR_TESTS, DISABLE_SELECTOR_REASON)
    def test_50_concurrent(self):
        name = "test_50_concurrent_AtoA_BtoB"
        self.logger.log("TCP_TEST Start %s" % name)
        pairs = [self.EchoPair(self.INTA, self.INTA),
                 self.EchoPair(self.INTB, self.INTB)]
        result = self.do_tcp_echo_n_routers(name, pairs)
        if result is not None:
            print(result)
            sys.stdout.flush()
        assert result is None, "TCP_TEST Stop %s FAIL: %s" % (name, result)
        self.logger.log("TCP_TEST Stop %s SUCCESS" % name)


if __name__== '__main__':
    unittest.main(main_module())

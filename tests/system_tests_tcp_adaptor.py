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

import io
import json
import os
import socket
import sys
import time
import traceback
from subprocess import PIPE
from subprocess import STDOUT

from system_test import Logger
from system_test import main_module
from system_test import Process
from system_test import Qdrouterd
from system_test import TestCase
from system_test import TIMEOUT
from system_test import unittest

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
    class TCP_echo_client:
        pass

    class TCP_echo_server:
        pass


DISABLE_SELECTOR_TESTS = False
DISABLE_SELECTOR_REASON = ''
try:
    import selectors  # noqa F401: imported but unused (echo server and echo client import this, they run as subprocesses)  # pylint: disable=unused-import
except ImportError:
    DISABLE_SELECTOR_TESTS = True
    DISABLE_SELECTOR_REASON = "Python selectors module is not available on this platform."


# This code takes a wild guess how long an echo server must stall
# receiving input data before Q2 holdoff triggers in the host router
# on all the various CI systems out there.
Q2_DELAY_SECONDS = 1.0

# This code needs to know the size in bytes of the holdoff trigger threshold.
# Whitebox testing knows that the holdoff is specified in some number of
# buffers. What whitebox testing does not know how big the buffers are
# or the number of buffers or how many bytes are actually in each buffer.
# Today the holdoff is probably 128K bytes so use something bigger than that
# in the test to get the trigger to kick in.
# On top of that the echo server is undermined by having TCP window or python
# read the server socket in advance of the echo server asking it to.
# In a test case the adaptor logged writing almost 3MBytes
#   2021-02-26 19:11:20.831826 PN_RAW_CONNECTION_WRITTEN Wrote 8192 bytes. Total written 2777007 bytes
# well before the server started reading from the socket.
#   2021-02-26 19:11:21.534246 J0#206 TCP_TEST [] [] ECHO_SERVER TcpAdaptor NS_EC2_CONN_STALL Connection from 127.0.0.1:54410 stall end
#   2021-02-26 19:11:21.534801 J0#207 TCP_TEST [] [] ECHO_SERVER TcpAdaptor NS_EC2_CONN_STALL read from: 127.0.0.1:54410 len:1024:
# Giving the stalled server 10Mbytes seems to run the TCP window out of capacity
# so that it stops reading from the TcpConnector and Q2 finally kicks in.
Q2_TEST_MESSAGE_SIZE = 10000000

# local timeout in seconds to wait for one echo client to finish
echo_timeout = 30


def ncat_available():
    popen_args = ['ncat', '--version']
    try:
        process = Process(popen_args,
                          name='ncat_check',
                          stdout=PIPE,
                          expect=None,
                          universal_newlines=True)
        out = process.communicate()[0]
        return True
    except:
        return False


#
# Test concurrent clients
#
class EchoClientRunner():
    """
    Launch an echo client upon construction.
    Provide poll interface for checking done/error.
    Provide wait/join to shut down.
    """

    def __init__(self, test_name, client_n, logger, client, server, size,
                 count,
                 print_client_logs=True,
                 timeout=TIMEOUT,
                 port_override=None):
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
        self.timeout = timeout
        self.print_client_logs = print_client_logs
        self.client_final = False

        # Each router has a listener for the echo server attached to every router
        self.listener_port = TcpAdaptor.tcp_client_listener_ports[self.client][self.server] if port_override is None else port_override

        self.name = "%s_%s_%s_%s" % \
                    (self.test_name, self.client_n, self.size, self.count)
        self.client_prefix = "ECHO_CLIENT %s" % self.name

        parent_path = os.path.dirname(os.getcwd())
        self.client_logger = Logger(title=self.client_prefix,
                                    print_to_console=self.print_client_logs,
                                    save_for_dump=False,
                                    ofilename=os.path.join(parent_path, "setUpClass/TcpAdaptor_echo_client_%s.log" % self.name))

        try:
            self.e_client = TcpEchoClient(prefix=self.client_prefix,
                                          host='localhost',
                                          port=self.listener_port,
                                          size=self.size,
                                          count=self.count,
                                          timeout=self.timeout,
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
    # Router EC2 has naughty, misbehaving echo servers:
    #  * conn_stall - delays before reading socket to force triggering Q2 holdoff
    #
    # Routers EC2 has a TCP listener for conn_stall echo server.
    #  * Sending "large" messages through this listener should trigger Q2 holdoff
    #    on router EC1.
    #  * A similar listener on INTA does *not* trigger Q2 holdoff on EA1.

    # Allocate routers in this order
    router_order = ['INTA', 'INTB', 'INTC', 'EA1', 'EA2', 'EB1', 'EB2', 'EC1', 'EC2']

    # List indexed in router_order
    # First listener in each router is normal AMQP for test setup and mgmt.
    amqp_listener_ports = {}

    # Each router listens for TCP where the tcp-address is the router name.
    # Each router has N listeners, one for the echo server connected to each router.
    tcp_client_listener_ports = {}

    # Each router connects to an echo server
    tcp_server_listener_ports = {}

    # Each router has a TCP listener that has no associated server
    nodest_listener_ports = {}

    # Each router has a console listener
    # http_listener_ports = {}

    # TCP siteId for listeners and connectors
    site = "mySite"

    # Each router has an echo server to which it connects
    echo_servers = {}

    # Special echo servers
    echo_server_NS_CONN_STALL = None

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
                # ('listener', {'port': cls.http_listener_ports[name], 'http': 'yes'}),
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
                listeners.extend(tup)
            config.extend(listeners)

            if extra:
                config.extend(extra)

            config = Qdrouterd.Config(config)
            cls.routers.append(cls.tester.qdrouterd(name, config, wait=True))

        cls.routers = []

        # define logging levels
        cls.print_logs_server = False
        cls.print_logs_client = False
        parent_path = os.path.dirname(os.getcwd())
        cls.logger = Logger(title="TcpAdaptor-testClass",
                            print_to_console=True,
                            save_for_dump=False,
                            ofilename=os.path.join(parent_path, "setUpClass/TcpAdaptor.log"))
        # Write a dummy log line for scraper.
        cls.logger.log("SERVER (info) Container Name: TCP_TEST")

        # Start echo servers first, store their listening port numbers
        parent_path = os.path.dirname(os.getcwd())
        for rtr in cls.router_order:
            test_name = "TcpAdaptor"
            server_prefix = "ECHO_SERVER %s ES_%s" % (test_name, rtr)
            server_logger = Logger(title=test_name,
                                   print_to_console=cls.print_logs_server,
                                   save_for_dump=False,
                                   ofilename=os.path.join(parent_path, "setUpClass/TcpAdaptor_echo_server_%s.log" % rtr))
            cls.logger.log("TCP_TEST Launching echo server '%s'" % server_prefix)
            server = TcpEchoServer(prefix=server_prefix,
                                   port=0,
                                   logger=server_logger)
            assert server.is_running
            cls.tcp_server_listener_ports[rtr] = server.port
            cls.echo_servers[rtr] = server

        # start special naughty servers that misbehave on purpose
        server_prefix = "ECHO_SERVER TcpAdaptor NS_EC2_CONN_STALL"
        server_logger = Logger(title="TcpAdaptor",
                               print_to_console=cls.print_logs_server,
                               save_for_dump=False,
                               ofilename=os.path.join(parent_path, "setUpClass/TcpAdaptor_echo_server_NS_CONN_STALL.log"))
        cls.logger.log("TCP_TEST Launching echo server '%s'" % server_prefix)
        server = TcpEchoServer(prefix=server_prefix,
                               port=0,
                               logger=server_logger,
                               conn_stall=Q2_DELAY_SECONDS)
        assert server.is_running
        cls.EC2_conn_stall_connector_port = server.port
        cls.echo_server_NS_CONN_STALL = server

        # Allocate a sea of router ports
        for rtr in cls.router_order:
            cls.amqp_listener_ports[rtr] = cls.tester.get_port()
            tl_ports = {}
            for tcp_listener in cls.router_order:
                tl_ports[tcp_listener] = cls.tester.get_port()
            cls.tcp_client_listener_ports[rtr] = tl_ports
            cls.nodest_listener_ports[rtr] = cls.tester.get_port()

        inter_router_port_AB = cls.tester.get_port()
        cls.INTA_edge_port = cls.tester.get_port()
        cls.INTA_conn_stall_listener_port = cls.tester.get_port()

        # Launch the routers using the sea of router ports
        router('INTA', 'interior',
               [('listener', {'role': 'inter-router', 'port': inter_router_port_AB}),
                ('listener', {'name': 'uplink', 'role': 'edge', 'port': cls.INTA_edge_port}),
                ('tcpListener', {'host': "0.0.0.0", 'port': cls.INTA_conn_stall_listener_port,
                                 'address': 'NS_EC2_CONN_STALL', 'siteId': cls.site})])
        inter_router_port_BC = cls.tester.get_port()
        cls.INTB_edge_port = cls.tester.get_port()
        router('INTB', 'interior',
               [('connector', {'role': 'inter-router', 'port': inter_router_port_AB}),
                ('listener', {'role': 'inter-router', 'port': inter_router_port_BC}),
                ('listener', {'name': 'uplink', 'role': 'edge', 'port': cls.INTB_edge_port})])

        cls.INTC_edge_port = cls.tester.get_port()
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
        cls.EC2_conn_stall_listener_port = cls.tester.get_port()
        router('EC2', 'edge',
               [('connector', {'name': 'uplink', 'role': 'edge', 'port': cls.INTC_edge_port}),
                ('tcpConnector', {'host': "127.0.0.1", 'port': cls.EC2_conn_stall_connector_port,
                                  'address': 'NS_EC2_CONN_STALL', 'siteId': cls.site}),
                ('tcpListener', {'host': "0.0.0.0", 'port': cls.EC2_conn_stall_listener_port,
                                 'address': 'NS_EC2_CONN_STALL', 'siteId': cls.site})])

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
            # p_out.append("%s_http_listener=%d" %
            #             (rtr, cls.http_listener_ports[rtr]))
        p_out.append("inter_router_port_AB=%d" % inter_router_port_AB)
        p_out.append("inter_router_port_BC=%d" % inter_router_port_BC)
        p_out.append("INTA_edge_port=%d" % cls.INTA_edge_port)
        p_out.append("INTB_edge_port=%d" % cls.INTB_edge_port)
        p_out.append("INTC_edge_port=%d" % cls.INTC_edge_port)
        p_out.append("EC2_conn_stall_connector_port%d" % cls.EC2_conn_stall_connector_port)
        p_out.append("INTA_conn_stall_listener_port%d" % cls.INTA_conn_stall_listener_port)
        p_out.append("EC2_conn_stall_listener_port%d" % cls.EC2_conn_stall_listener_port)

        # write to log
        for line in p_out:
            cls.logger.log("TCP_TEST %s" % line)

        # write to shell script
        parent_path = os.path.dirname(os.getcwd())
        file_name = os.path.join(parent_path, "setUpClass/TcpAdaptor-ports.sh")
        with open(file_name, 'w') as o_file:
            for line in p_out:
                o_file.write("set %s\n" % line)

        # Write a script to run scraper on this test's log files
        scraper_abspath = os.path.join(os.environ.get('BUILD_DIR'), 'tests', 'scraper', 'scraper.py')
        logs_dir     = os.path.join(parent_path, "setUpClass")
        main_log     = "TcpAdaptor.log"
        echo_logs    = "TcpAdaptor_echo*"
        big_test_log = "TcpAdaptor_all.log"
        int_logs     = "I*.log"
        edge_logs    = "E*.log"
        log_modules_spec = "--log-modules TCP_ADAPTOR,TCP_TEST,ECHO_SERVER,ECHO_CLIENT"
        html_output  = "TcpAdaptor.html"
        with open(os.path.join(parent_path, "setUpClass/TcpAdaptor-run-scraper.sh"), 'w') as o_file:
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

        # wait for server addresses (mobile ES_<rtr>) to propagate to all interior routers
        interior_rtrs = [rtr for rtr in cls.router_order if rtr.startswith('I')]
        poll_loops = 100
        poll_loop_delay = 0.5  # seconds
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
                if poll_loops == 1:
                    # last poll loop
                    for line in lines:
                        cls.logger.log("TCP_TEST Router %s : %s" % (rtr, line))
            poll_loops -= 1
            if poll_loops == 0:
                assert False, "TCP_TEST TCP_Adaptor test setup failed. Echo tests never executed."
            else:
                time.sleep(poll_loop_delay)
        cls.logger.log("TCP_TEST Done poll wait")

    @classmethod
    def tearDownClass(cls):
        # stop echo servers
        for rtr in cls.router_order:
            server = cls.echo_servers.get(rtr)
            if server is not None:
                cls.logger.log("TCP_TEST Stopping echo server ES_%s" % rtr)
                server.wait()
        if cls.echo_server_NS_CONN_STALL is not None:
            cls.logger.log("TCP_TEST Stopping echo server NS_EC2_CONN_STALL")
            cls.echo_server_NS_CONN_STALL.wait()
        super(TcpAdaptor, cls).tearDownClass()

    def run_qdmanage(self, cmd, input=None, expect=Process.EXIT_OK, address=None):
        p = self.popen(
            ['qdmanage'] + cmd.split(' ') + ['--bus', address or str(self.router_dict['INTA'].addresses[0]),
                                             '--indent=-1', '--timeout', str(TIMEOUT)],
            stdin=PIPE, stdout=PIPE, stderr=STDOUT, expect=expect,
            universal_newlines=True)
        out = p.communicate(input)[0]
        try:
            p.teardown()
        except Exception as e:
            raise Exception(out if out else str(e))
        return out

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
                        runner = EchoClientRunner(test_name, client_num,
                                                  self.logger,
                                                  client, server, size, count,
                                                  self.print_logs_client)
                        runners.append(runner)
                        client_num += 1

            # Loop until timeout, error, or completion
            while result is None:
                # Check for timeout
                time.sleep(0.1)
                elapsed = time.time() - start_time
                if elapsed > echo_timeout:
                    result = "TCP_TEST TIMEOUT - local wait time exceeded"
                    break
                # Make sure servers are still up
                for rtr in TcpAdaptor.router_order:
                    es = TcpAdaptor.echo_servers[rtr]
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

    def do_tcp_echo_singleton(self, test_name, client, server, size, count, echo_port):
        """
        Launch a single echo client to the echo_port
        Wait for completion.
        Note that client and server do not define the port that the echo client
        must connect to. That is overridden by echo_port. Still client and server
        are passed to the EchoClientRunner
        :param test_name test name
        :param client router to which echo client attaches
        :param server router that has the connector to the echo server
        :param size size of message to be echoed
        :param count number of messages to be echoed
        :param echo_port the router network listener port
        :return: None if success else error message for ctest
        """
        self.logger.log("TCP_TEST %s Start do_tcp_echo_singleton" % test_name)
        result = None
        runners = []
        client_num = 0
        start_time = time.time()

        try:
            # Launch the runner
            log_msg = "TCP_TEST %s Running singleton %d %s->%s port %d, size=%d count=%d" % \
                      (test_name, client_num, client.name, server.name, echo_port, size, count)
            self.logger.log(log_msg)
            runner = EchoClientRunner(test_name, client_num, self.logger,
                                      client.name, server.name, size, count,
                                      self.print_logs_client,
                                      port_override=echo_port)
            runners.append(runner)
            client_num += 1

            # Loop until timeout, error, or completion
            while result is None:
                # Check for timeout
                time.sleep(0.1)
                elapsed = time.time() - start_time
                if elapsed > echo_timeout:
                    result = "TCP_TEST TIMEOUT - local wait time exceeded"
                    break
                # Make sure servers are still up
                for rtr in TcpAdaptor.router_order:
                    es = TcpAdaptor.echo_servers[rtr]
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
    # Tests run by ctest
    #
    @unittest.skipIf(DISABLE_SELECTOR_TESTS, DISABLE_SELECTOR_REASON)
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
    @unittest.skipIf(DISABLE_SELECTOR_TESTS, DISABLE_SELECTOR_REASON)
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

    @unittest.skipIf(DISABLE_SELECTOR_TESTS, DISABLE_SELECTOR_REASON)
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

    @unittest.skipIf(DISABLE_SELECTOR_TESTS, DISABLE_SELECTOR_REASON)
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

    @unittest.skipIf(DISABLE_SELECTOR_TESTS, DISABLE_SELECTOR_REASON)
    def test_13_tcp_EA1_EC2_500000(self):
        name = "test_12_tcp_EA1_EC2_500000"
        self.logger.log("TCP_TEST Start %s" % name)
        pairs = [self.EchoPair(self.INTA, self.INTA, sizes=[500000])]
        result = self.do_tcp_echo_n_routers(name, pairs)
        if result is not None:
            print(result)
            sys.stdout.flush()
        assert result is None, "TCP_TEST Stop %s FAIL: %s" % (name, result)

    @unittest.skipIf(DISABLE_SELECTOR_TESTS, DISABLE_SELECTOR_REASON)
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
    @unittest.skipIf(DISABLE_SELECTOR_TESTS, DISABLE_SELECTOR_REASON)
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

    # Q2 holdoff
    @unittest.skipIf(DISABLE_SELECTOR_TESTS, DISABLE_SELECTOR_REASON)
    def test_60_q2_holdoff(self):
        # for now, Q2 is disabled to avoid stalling TCP backpressure
        self.skipTest("Q2 is disabled on TCP adaptor")
        name = "test_60_q2_holdoff"
        self.logger.log("TCP_TEST Start %s" % name)

        # Verify going to EC2
        result = self.do_tcp_echo_singleton(name, self.EC2, self.EC2, Q2_TEST_MESSAGE_SIZE,
                                            1, self.EC2_conn_stall_listener_port)
        if result is not None:
            print(result)
            sys.stdout.flush()
        assert result is None, "TCP_TEST Stop %s FAIL: %s" % (name, result)

        # search the router log file to verify Q2 was hit
        for attempt in range(10):
            block_ct = 0
            unblock_ct = 0
            lines = 0
            with io.open(self.EC2.logfile_path) as f:
                for line in f:
                    lines += 1
                    if 'client link blocked on Q2 limit' in line:
                        block_ct += 1
                    if 'client link unblocked from Q2 limit' in line:
                        unblock_ct += 1
            if block_ct > 0 and block_ct == unblock_ct:
                break
            self.logger.log("Q2 holdoff from EC2 not detected. Wait for log file to update...")
            time.sleep(0.1)
        result = "failed" if block_ct == 0 or not block_ct == unblock_ct else "passed"
        self.logger.log("TCP_TEST %s EC2 log scrape %s. block_ct=%d, unblock_ct=%d, lines=%d" %
                        (name, result, block_ct, unblock_ct, lines))
        self.assertTrue(block_ct > 0)
        self.assertEqual(block_ct, unblock_ct)

        # Declare success
        self.logger.log("TCP_TEST Stop %s SUCCESS" % name)

    def run_ncat(self, port, logger, expect=Process.EXIT_OK, timeout=2, data=b'abcd'):
        ncat_cmd = ['ncat', '127.0.0.1', str(port)]
        logger.log("Starting ncat '%s' and input '%s'" % (ncat_cmd, str(data)))
        p = self.popen(
            ncat_cmd,
            stdin=PIPE, stdout=PIPE, stderr=PIPE, expect=expect,
            universal_newlines=True)
        out = p.communicate(input='abcd', timeout=timeout)[0]
        try:
            p.teardown()
        except Exception as e:
            raise Exception(out if out else str(e))
        return out

    def ncat_runner(self, tname, client, server, logger):
        name = "%s_%s_%s" % (tname, client, server)
        logger.log(name + " Start")
        out = self.run_ncat(TcpAdaptor.tcp_client_listener_ports[client][server], logger, data=b'abcd')
        logger.log("run_ncat returns: '%s'" % out)
        assert len(out) > 0
        assert "abcd" in out
        logger.log(tname + " Stop")

    # half-closed handling
    def test_70_half_closed(self):
        if DISABLE_SELECTOR_TESTS:
            self.skipTest(DISABLE_SELECTOR_REASON)
        if not ncat_available():
            self.skipTest("Ncat utility is not available")
        name = "test_70_half_closed"
        self.logger.log("TCP_TEST Start %s" % name)
        self.ncat_runner(name, "INTA", "INTA", self.logger)
        self.ncat_runner(name, "INTA", "INTB", self.logger)
        self.ncat_runner(name, "INTA", "INTC", self.logger)
        self.ncat_runner(name, "EA1",  "EA1", self.logger)
        self.ncat_runner(name, "EA1",  "EB1", self.logger)
        self.ncat_runner(name, "EA1",  "EC2", self.logger)
        self.logger.log("TCP_TEST Stop %s SUCCESS" % name)

    # connector/listener stats
    def test_80_stats(self):
        tname = "test_80 check stats in qdmanage"
        self.logger.log(tname + " START")
        # Verify listener stats
        query_command = 'QUERY --type=tcpListener'
        outputs = json.loads(self.run_qdmanage(query_command))
        for output in outputs:
            if output['name'].startswith("ES"):
                # Check only echo server listeners
                assert "connectionsOpened" in output
                assert output["connectionsOpened"] > 0
                assert output["connectionsOpened"] == output["connectionsClosed"]
                assert output["bytesIn"] == output["bytesOut"]
        # Verify connector stats
        query_command = 'QUERY --type=tcpConnector'
        outputs = json.loads(self.run_qdmanage(query_command))
        for output in outputs:
            assert output['address'].startswith("ES")
            assert "connectionsOpened" in output
            assert output["connectionsOpened"] > 0
            # egress_dispatcher connection opens and should never close
            assert output["connectionsOpened"] == output["connectionsClosed"] + 1
            assert output["bytesIn"] == output["bytesOut"]
        self.logger.log(tname + " SUCCESS")


class TcpAdaptorStuckDeliveryTest(TestCase):
    """
    Verify that the routers stuck delivery detection is not applied to TCP
    deliveries.  See Dispatch-2036.
    """
    @classmethod
    def setUpClass(cls):
        super(TcpAdaptorStuckDeliveryTest, cls).setUpClass()

        if DISABLE_SELECTOR_TESTS:
            return

        cls.test_name = 'TCPStuckDeliveryTest'

        # Topology: linear.
        # tcp-client->edge1->interior1->interior2->edge2->tcp-server

        cls.edge1_tcp_listener_port = cls.tester.get_port()
        cls.int1_edge_listener_port = cls.tester.get_port()
        cls.int2_interior_listener_port = cls.tester.get_port()
        cls.int2_edge_listener_port = cls.tester.get_port()
        cls.edge2_tcp_connector_port = cls.tester.get_port()

        def router(cls, name, mode, config):
            base_config = [
                ('router', {'mode': mode,
                            'id': name}),
                ('listener', {'role': 'normal',
                              'port': cls.tester.get_port()}),
                ('address', {'prefix': 'closest',   'distribution': 'closest'}),
                ('address', {'prefix': 'multicast', 'distribution': 'multicast'}),
            ]
            config = base_config + config
            config = Qdrouterd.Config(config)
            return cls.tester.qdrouterd(name, config, wait=False,
                                        # enable stuck delivery short timer
                                        # (3 seconds)
                                        cl_args=['-T'])

        cls.i_router1 = router(cls,
                               "%s_I1" % cls.test_name,
                               "interior",
                               [('listener', {'role': 'edge',
                                              'host': '0.0.0.0',
                                              'port':
                                              cls.int1_edge_listener_port}),
                                ('connector', {'role': 'inter-router',
                                               'port':
                                               cls.int2_interior_listener_port})])
        cls.i_router2 = router(cls,
                               "%s_I2" % cls.test_name,
                               "interior",
                               [('listener', {'role': 'edge',
                                              'host': '0.0.0.0',
                                              'port':
                                              cls.int2_edge_listener_port}),
                                ('listener', {'role': 'inter-router',
                                              'host': '0.0.0.0',
                                              'port':
                                              cls.int2_interior_listener_port})])
        cls.e_router1 = router(cls,
                               "%s_E1" % cls.test_name,
                               "edge",
                               [('tcpListener', {'host': "0.0.0.0",
                                                 'port':
                                                 cls.edge1_tcp_listener_port,
                                                 'address': 'nostuck'}),
                                ('connector', {'role': 'edge',
                                               'port':
                                               cls.int1_edge_listener_port})])
        cls.e_router2 = router(cls,
                               "%s_E2" % cls.test_name,
                               "edge",
                               [('tcpConnector', {'host': "127.0.0.1",
                                                  'port':
                                                  cls.edge2_tcp_connector_port,
                                                  'address': 'nostuck'}),
                                ('connector', {'role': 'edge',
                                               'port':
                                               cls.int1_edge_listener_port})])
        cls.i_router1.wait_ready()
        cls.i_router2.wait_ready()
        cls.e_router1.wait_ready()
        cls.e_router2.wait_ready()

    @unittest.skipIf(DISABLE_SELECTOR_TESTS, DISABLE_SELECTOR_REASON)
    def test_01_ignore_stuck_deliveries(self):
        """
        Create a TCP delivery, wait for it to be classified as stuck and ensure
        that no stuck delivery logs are posted
        """

        # step 1: create a TCP listener for edge2 to connect to
        try:
            server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            server.settimeout(TIMEOUT)
            server.bind(("", self.edge2_tcp_connector_port))
            server.listen(1)
        except Exception as exc:
            print("%s: failed creating tcp server: %s" % (self.test_name, exc),
                  flush=True)
            raise

        # step 2: initiate client connection to edge1.
        try:
            client_conn = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            client_conn.settimeout(TIMEOUT)
            client_conn.connect(('127.0.0.1', self.edge1_tcp_listener_port))
        except Exception as exc:
            print("%s: failed creating client connection: %s" % (self.test_name,
                                                                 exc),
                  flush=True)
            raise

        # step 3: accept the connection request from edge2:
        try:
            server_conn, addr = server.accept()
            server.close()
        except Exception as exc:
            print("%s: failed to accept at server: %s" % (self.test_name,
                                                          exc),
                  flush=True)
            raise

        # step 4: now wait long enough for the stuck delivery detection cycle
        # to run on each router (approx 5 seconds)
        time.sleep(12)
        client_conn.close()
        server_conn.close()

        # step 5: expect no log messages for stuck delivery were issued
        for router in [self.e_router1, self.i_router1, self.i_router2,
                       self.e_router2]:
            logfile = router.logfile_path
            router.teardown()
            with io.open(logfile) as f:
                for line in f:
                    self.assertNotIn("Stuck delivery: At least one delivery",
                                     line,
                                     "Stuck deliveries should not be logged!")


class TcpAdaptorManagementTest(TestCase):
    """Test Creation and deletion of TCP management entities"""
    @classmethod
    def setUpClass(cls):
        super(TcpAdaptorManagementTest, cls).setUpClass()

        if DISABLE_SELECTOR_TESTS:
            return

        cls.test_name = 'TCPMgmtTest'

        # create edge and interior routers.  The listener/connector will be on
        # the edge router.  It is expected that the edge will create proxy
        # links to the interior and remove them when the test is done.

        cls.interior_edge_port = cls.tester.get_port()
        cls.interior_mgmt_port = cls.tester.get_port()
        cls.edge_mgmt_port = cls.tester.get_port()

        cls.tcp_server_port = cls.tester.get_port()
        cls.tcp_listener_port = cls.tester.get_port()

        i_config = [
            ('router', {'mode': 'interior',
                        'id': 'TCPMgmtTestInterior'}),
            ('listener', {'role': 'normal',
                          'port': cls.interior_mgmt_port}),
            ('listener', {'role': 'edge', 'port': cls.interior_edge_port}),
            ('address', {'prefix': 'closest',   'distribution': 'closest'}),
            ('address', {'prefix': 'multicast', 'distribution': 'multicast'}),
        ]
        config = Qdrouterd.Config(i_config)
        cls.i_router = cls.tester.qdrouterd('TCPMgmtTestInterior', config, wait=False)

        e_config = [
            ('router', {'mode': 'edge',
                        'id': 'TCPMgmtTestEdge'}),
            ('listener', {'role': 'normal',
                          'port': cls.edge_mgmt_port}),
            ('connector', {'name': 'edge', 'role': 'edge',
                           'port': cls.interior_edge_port}),
            ('address', {'prefix': 'closest',   'distribution': 'closest'}),
            ('address', {'prefix': 'multicast', 'distribution': 'multicast'}),
        ]
        config = Qdrouterd.Config(e_config)
        cls.e_router = cls.tester.qdrouterd('TCPMgmtTestEdge', config,
                                            wait=False)

        cls.i_router.wait_ready()
        cls.e_router.wait_ready()

    def _query_links_by_addr(self, router_mgmt, owning_addr):
        oid = 'org.apache.qpid.dispatch.router.link'
        attrs = ['owningAddr', 'linkDir']

        links = []
        rc = router_mgmt.query(type=oid, attribute_names=attrs).results
        for link in rc:
            if link[0] is not None and link[0].endswith(owning_addr):
                links.append(link)
        return links

    @unittest.skipIf(DISABLE_SELECTOR_TESTS, DISABLE_SELECTOR_REASON)
    def test_01_mgmt(self):
        """
        Create and delete TCP connectors and listeners. Ensure that the service
        address is properly removed on the interior router.
        """
        LISTENER_TYPE = 'org.apache.qpid.dispatch.tcpListener'
        CONNECTOR_TYPE = 'org.apache.qpid.dispatch.tcpConnector'

        mgmt = self.e_router.management

        # When starting out, there should be no tcpListeners or tcpConnectors.
        self.assertEqual(0, len(mgmt.query(type=LISTENER_TYPE).results))
        self.assertEqual(0, len(mgmt.query(type=CONNECTOR_TYPE).results))

        connector_name = "ServerConnector"
        listener_name = "ClientListener"

        mgmt.create(type=LISTENER_TYPE,
                    name=listener_name,
                    attributes={'address': self.test_name,
                                'port': self.tcp_listener_port,
                                'host': '127.0.0.1'})
        mgmt.create(type=CONNECTOR_TYPE,
                    name=connector_name,
                    attributes={'address': self.test_name,
                                'port': self.tcp_server_port,
                                'host': '127.0.0.1'})

        # verify the entities have been created and tcp traffic works
        self.assertEqual(1, len(mgmt.query(type=LISTENER_TYPE).results))
        self.assertEqual(1, len(mgmt.query(type=CONNECTOR_TYPE).results))

        # now verify that the interior router sees the service address
        # and two proxy links are created
        self.i_router.wait_address(self.test_name, subscribers=1)
        while True:
            links = self._query_links_by_addr(self.i_router.management,
                                              self.test_name)
            if links:
                # expect a single consumer link that represents
                # the connector
                self.assertEqual(1, len(links))
                self.assertEqual("out", links[0][1])
                break
            time.sleep(0.25)

        # Delete the connector and listener
        out = mgmt.delete(type=CONNECTOR_TYPE, name=connector_name)  # pylint: disable=assignment-from-no-return
        self.assertIsNone(out)
        self.assertEqual(0, len(mgmt.query(type=CONNECTOR_TYPE).results))
        out = mgmt.delete(type=LISTENER_TYPE, name=listener_name)  # pylint: disable=assignment-from-no-return
        self.assertIsNone(out)
        self.assertEqual(0, len(mgmt.query(type=LISTENER_TYPE).results))

        # verify the service address and proxy links are no longer active on
        # the interior router
        self.i_router.wait_address_unsubscribed(self.test_name)
        while True:
            links = self._query_links_by_addr(self.i_router.management,
                                              self.test_name)
            if len(links) == 0:
                break
            time.sleep(0.25)

        # verify that clients can no longer connect to the listener
        client_conn = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        client_conn.setblocking(True)
        client_conn.settimeout(5)
        with self.assertRaises(ConnectionRefusedError):
            client_conn.connect(('127.0.0.1', self.tcp_listener_port))
        client_conn.close()


if __name__ == '__main__':
    unittest.main(main_module())

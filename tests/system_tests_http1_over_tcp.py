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

from http1_tests import CommonHttp1OneRouterTest, Http1OneRouterTestBase
from http1_tests import TestServer, RequestHandler10
from http1_tests import Http1Edge2EdgeTestBase
from http1_tests import CommonHttp1Edge2EdgeTest


class Http1OverTcpOneRouterTest(Http1OneRouterTestBase,
                                CommonHttp1OneRouterTest):
    """Test HTTP servers and clients attached to a standalone router"""
    @classmethod
    def setUpClass(cls):
        """Start a router"""
        super(Http1OverTcpOneRouterTest, cls).setUpClass()

        # configuration:
        #  One interior router, two servers (one running as HTTP/1.0)
        #
        #  +----------------+
        #  |     INT.A      |
        #  +----------------+
        #      ^         ^
        #      |         |
        #      V         V
        #  <clients>  <servers>

        cls.skip = {'test_000': 1}

        cls.routers = []
        super(Http1OverTcpOneRouterTest, cls).router('INT.A', 'standalone',
                                                     [('tcpConnector', {'port': cls.http_server11_port,
                                                                        'host': '127.0.0.1',
                                                                        'address': 'testServer11'}),
                                                      ('tcpConnector', {'port': cls.http_server10_port,
                                                                        'host': '127.0.0.1',
                                                                        'address': 'testServer10'}),
                                                      ('tcpListener', {'port': cls.http_listener11_port,
                                                                       'host': '127.0.0.1',
                                                                       'address': 'testServer11'}),
                                                      ('tcpListener', {'port': cls.http_listener10_port,
                                                                       'host': '127.0.0.1',
                                                                       'address': 'testServer10'})
                                                      ])

        cls.INT_A = cls.routers[0]
        cls.INT_A.listener = cls.INT_A.addresses[0]

        cls.http11_server = TestServer.new_server(cls.http_server11_port, cls.http_listener11_port, cls.TESTS_11)
        cls.http10_server = TestServer.new_server(cls.http_server10_port, cls.http_listener10_port, cls.TESTS_10,
                                                  handler_cls=RequestHandler10)
        cls.INT_A.wait_connectors()


class Http1OverTcpEdge2EdgeTest(Http1Edge2EdgeTestBase, CommonHttp1Edge2EdgeTest):
    """
    Test an HTTP servers and clients attached to edge routers separated by an
    interior router
    """
    @classmethod
    def setUpClass(cls):
        """Start a router"""
        super(Http1OverTcpEdge2EdgeTest, cls).setUpClass()

        # configuration:
        # one edge, one interior
        #
        #  +-------+    +---------+    +-------+
        #  |  EA1  |<==>|  INT.A  |<==>|  EA2  |
        #  +-------+    +---------+    +-------+
        #      ^                           ^
        #      |                           |
        #      V                           V
        #  <clients>                   <servers>

        super(Http1OverTcpEdge2EdgeTest, cls).\
            router('INT.A', 'interior', [('listener', {'role': 'edge', 'port': cls.INTA_edge1_port}),
                                         ('listener', {'role': 'edge', 'port': cls.INTA_edge2_port}),
                                         ])
        cls.INT_A = cls.routers[0]
        cls.INT_A.listener = cls.INT_A.addresses[0]

        super(Http1OverTcpEdge2EdgeTest, cls).\
            router('EA1', 'edge', [('connector', {'name': 'uplink', 'role': 'edge',
                                                  'port': cls.INTA_edge1_port}),
                                   ('httpListener', {'port': cls.http_listener11_port,
                                                     'protocolVersion': 'HTTP1',
                                                     'address': 'testServer11'}),
                                   ('httpListener', {'port': cls.http_listener10_port,
                                                     'protocolVersion': 'HTTP1',
                                                     'address': 'testServer10'})
                                   ])
        cls.EA1 = cls.routers[1]
        cls.EA1.listener = cls.EA1.addresses[0]

        super(Http1OverTcpEdge2EdgeTest, cls).\
            router('EA2', 'edge', [('connector', {'name': 'uplink', 'role': 'edge',
                                                  'port': cls.INTA_edge2_port}),
                                   ('httpConnector', {'port': cls.http_server11_port,
                                                      'protocolVersion': 'HTTP1',
                                                      'address': 'testServer11'}),
                                   ('httpConnector', {'port': cls.http_server10_port,
                                                      'protocolVersion': 'HTTP1',
                                                      'address': 'testServer10'})
                                   ])
        cls.EA2 = cls.routers[-1]
        cls.EA2.listener = cls.EA2.addresses[0]

        cls.INT_A.wait_address('EA1')
        cls.INT_A.wait_address('EA2')

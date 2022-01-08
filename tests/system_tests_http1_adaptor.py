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

#
# Test the HTTP/1.x Adaptor
#

import errno
import io
import select
import socket
from time import sleep, time
from http.client import HTTPConnection

from proton import Message
from system_test import TestCase, unittest, main_module, Qdrouterd, QdManager
from system_test import TIMEOUT, AsyncTestSender, AsyncTestReceiver
from http1_tests import http1_ping, TestServer, RequestHandler10
from http1_tests import RequestMsg
from http1_tests import ThreadedTestClient, Http1OneRouterTestBase
from http1_tests import CommonHttp1OneRouterTest
from http1_tests import CommonHttp1Edge2EdgeTest
from http1_tests import Http1Edge2EdgeTestBase
from http1_tests import Http1ClientCloseTestsMixIn
from http1_tests import Http1CurlTestsMixIn


class Http1AdaptorManagementTest(TestCase):
    """Test Creation and deletion of HTTP1 management entities."""
    @classmethod
    def setUpClass(cls):
        super(Http1AdaptorManagementTest, cls).setUpClass()

        cls.LISTENER_TYPE = 'org.apache.qpid.dispatch.httpListener'
        cls.CONNECTOR_TYPE = 'org.apache.qpid.dispatch.httpConnector'
        cls.CONNECTION_TYPE = 'org.apache.qpid.dispatch.connection'

        cls.interior_edge_port = cls.tester.get_port()
        cls.interior_mgmt_port = cls.tester.get_port()
        cls.edge_mgmt_port = cls.tester.get_port()

        cls.http_server_port = cls.tester.get_port()
        cls.http_listener_port = cls.tester.get_port()

        i_config = [
            ('router', {'mode': 'interior',
                        'id': 'HTTP1MgmtTestInterior'}),
            ('listener', {'role': 'normal',
                          'port': cls.interior_mgmt_port}),
            ('listener', {'role': 'edge', 'port': cls.interior_edge_port}),
            ('address', {'prefix': 'closest',   'distribution': 'closest'}),
            ('address', {'prefix': 'multicast', 'distribution': 'multicast'}),
        ]
        config = Qdrouterd.Config(i_config)
        cls.i_router = cls.tester.qdrouterd('HTTP1MgmtTestInterior', config, wait=False)

        e_config = [
            ('router', {'mode': 'edge',
                        'id': 'HTTP1MgmtTestEdge'}),
            ('listener', {'role': 'normal',
                          'port': cls.edge_mgmt_port}),
            ('connector', {'name': 'edge', 'role': 'edge',
                           'port': cls.interior_edge_port}),
            ('address', {'prefix': 'closest',   'distribution': 'closest'}),
            ('address', {'prefix': 'multicast', 'distribution': 'multicast'}),
        ]
        config = Qdrouterd.Config(e_config)
        cls.e_router = cls.tester.qdrouterd('HTTP1MgmtTestEdge', config,
                                            wait=False)

        cls.i_router.wait_ready()
        cls.e_router.wait_ready()

    def test_01_create_delete(self):
        """Create and delete HTTP1 connectors and listeners.  The
        connectors/listeners are created on the edge router.  Verify that the
        adaptor properly notifies the interior of the subscribers/producers.
        """
        e_mgmt = self.e_router.management
        self.assertEqual(0, len(e_mgmt.query(type=self.LISTENER_TYPE).results))
        self.assertEqual(0, len(e_mgmt.query(type=self.CONNECTOR_TYPE).results))

        e_mgmt.create(type=self.CONNECTOR_TYPE,
                      name="ServerConnector",
                      attributes={'address': 'closest/http1Service',
                                  'port': self.http_server_port,
                                  'protocolVersion': 'HTTP1'})

        e_mgmt.create(type=self.LISTENER_TYPE,
                      name="ClientListener",
                      attributes={'address': 'closest/http1Service',
                                  'port': self.http_listener_port,
                                  'protocolVersion': 'HTTP1'})

        # verify the entities have been created and http traffic works

        self.assertEqual(1, len(e_mgmt.query(type=self.LISTENER_TYPE).results))
        self.assertEqual(1, len(e_mgmt.query(type=self.CONNECTOR_TYPE).results))

        count, error = http1_ping(sport=self.http_server_port,
                                  cport=self.http_listener_port)
        self.assertIsNone(error)
        self.assertEqual(1, count)

        # now check the interior router for the closest/http1Service address
        self.i_router.wait_address("closest/http1Service", subscribers=1)

        #
        # delete the connector and listener; wait for the associated connection
        # to be removed
        #
        e_mgmt.delete(type=self.CONNECTOR_TYPE, name="ServerConnector")
        self.assertEqual(0, len(e_mgmt.query(type=self.CONNECTOR_TYPE).results))
        e_mgmt.delete(type=self.LISTENER_TYPE, name="ClientListener")
        self.assertEqual(0, len(e_mgmt.query(type=self.LISTENER_TYPE).results))

        # will hit test timeout on failure:
        while True:
            hconns = 0
            obj = e_mgmt.query(type=self.CONNECTION_TYPE,
                               attribute_names=["protocol"])
            for item in obj.get_dicts():
                if "http/1.x" in item["protocol"]:
                    hconns += 1
            if hconns == 0:
                break
            sleep(0.25)

        # When a connector is configured the router will periodically attempt
        # to connect to the server address. To prove that the connector has
        # been completely removed listen for connection attempts on the server
        # port.
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind(("", self.http_server_port))
        s.setblocking(True)
        s.settimeout(3)  # reconnect attempts every 2.5 seconds
        s.listen(1)
        with self.assertRaises(socket.timeout):
            conn, addr = s.accept()
        s.close()

        # Verify that the address is no longer bound on the interior
        self.i_router.wait_address_unsubscribed("closest/http1Service")

        #
        # re-create the connector and listener; verify it works
        #
        e_mgmt.create(type=self.CONNECTOR_TYPE,
                      name="ServerConnector",
                      attributes={'address': 'closest/http1Service',
                                  'port': self.http_server_port,
                                  'protocolVersion': 'HTTP1'})

        e_mgmt.create(type=self.LISTENER_TYPE,
                      name="ClientListener",
                      attributes={'address': 'closest/http1Service',
                                  'port': self.http_listener_port,
                                  'protocolVersion': 'HTTP1'})

        self.assertEqual(1, len(e_mgmt.query(type=self.LISTENER_TYPE).results))
        self.assertEqual(1, len(e_mgmt.query(type=self.CONNECTOR_TYPE).results))

        count, error = http1_ping(sport=self.http_server_port,
                                  cport=self.http_listener_port)
        self.assertIsNone(error)
        self.assertEqual(1, count)

        self.i_router.wait_address("closest/http1Service", subscribers=1)

        e_mgmt.delete(type=self.CONNECTOR_TYPE, name="ServerConnector")
        self.assertEqual(0, len(e_mgmt.query(type=self.CONNECTOR_TYPE).results))
        e_mgmt.delete(type=self.LISTENER_TYPE, name="ClientListener")
        self.assertEqual(0, len(e_mgmt.query(type=self.LISTENER_TYPE).results))

    def test_01_delete_active_connector(self):
        """Delete an HTTP1 connector that is currently connected to a server.
        Verify the connection is dropped.
        """
        e_mgmt = self.e_router.management
        self.assertEqual(0, len(e_mgmt.query(type=self.CONNECTOR_TYPE).results))

        e_mgmt.create(type=self.CONNECTOR_TYPE,
                      name="ServerConnector",
                      attributes={'address': 'closest/http1Service',
                                  'port': self.http_server_port,
                                  'protocolVersion': 'HTTP1'})

        # verify the connector has been created and attach a dummy server
        self.assertEqual(1, len(e_mgmt.query(type=self.CONNECTOR_TYPE).results))

        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind(("", self.http_server_port))
        server.setblocking(True)
        server.settimeout(5)
        server.listen(1)
        conn, _ = server.accept()
        server.close()

        # now check the interior router for the closest/http1Service address
        self.i_router.wait_address("closest/http1Service", subscribers=1)

        # delete the connector
        e_mgmt.delete(type=self.CONNECTOR_TYPE, name="ServerConnector")
        self.assertEqual(0, len(e_mgmt.query(type=self.CONNECTOR_TYPE).results))

        # expect socket to close
        while True:
            try:
                rd, _, _ = select.select([conn], [], [])
            except select.error as serror:
                if serror[0] == errno.EINTR:
                    print("ignoring interrupt from select(): %s" % str(serror))
                    continue
                raise  # assuming fatal...
            if len(conn.recv(10)) == 0:
                break

        conn.close()

        # Verify that the address is no longer bound on the interior
        self.i_router.wait_address_unsubscribed("closest/http1Service")


class Http1AdaptorOneRouterTest(Http1OneRouterTestBase,
                                CommonHttp1OneRouterTest):
    """Test HTTP servers and clients attached to a standalone router"""
    @classmethod
    def setUpClass(cls):
        """Start a router"""
        super(Http1AdaptorOneRouterTest, cls).setUpClass()

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

        cls.routers = []
        super(Http1AdaptorOneRouterTest, cls).\
            router('INT.A', 'standalone',
                   [('httpConnector', {'port': cls.http_server11_port,
                                       'host': '127.0.0.1',
                                       'protocolVersion': 'HTTP1',
                                       'address': 'testServer11'}),
                    ('httpConnector', {'port': cls.http_server10_port,
                                       'host': '127.0.0.1',
                                       'protocolVersion': 'HTTP1',
                                       'address': 'testServer10'}),
                    ('httpListener', {'port': cls.http_listener11_port,
                                      'protocolVersion': 'HTTP1',
                                      'host': '127.0.0.1',
                                      'address': 'testServer11'}),
                    ('httpListener', {'port': cls.http_listener10_port,
                                      'protocolVersion': 'HTTP1',
                                      'host': '127.0.0.1',
                                      'address': 'testServer10'})
                    ])

        cls.INT_A = cls.routers[0]
        cls.INT_A.listener = cls.INT_A.addresses[0]

        cls.http11_server = TestServer(server_port=cls.http_server11_port,
                                       client_port=cls.http_listener11_port,
                                       tests=cls.TESTS_11)
        cls.http10_server = TestServer(server_port=cls.http_server10_port,
                                       client_port=cls.http_listener10_port,
                                       tests=cls.TESTS_10,
                                       handler_cls=RequestHandler10)
        cls.INT_A.wait_connectors()

    def test_005_get_10(self):
        client = HTTPConnection("127.0.0.1:%s" % self.http_listener10_port,
                                timeout=TIMEOUT)
        self._do_request(client, self.TESTS_10["GET"])
        client.close()

    def test_000_stats(self):
        client = HTTPConnection("127.0.0.1:%s" % self.http_listener11_port,
                                timeout=TIMEOUT)
        self._do_request(client, self.TESTS_11["GET"])
        self._do_request(client, self.TESTS_11["POST"])
        client.close()
        qd_manager = QdManager(self, address=self.INT_A.listener)
        stats = qd_manager.query('org.apache.qpid.dispatch.httpRequestInfo')
        self.assertEqual(len(stats), 2)
        for s in stats:
            self.assertEqual(s.get('requests'), 10)
            self.assertEqual(s.get('details').get('GET:400'), 1)
            self.assertEqual(s.get('details').get('GET:200'), 6)
            self.assertEqual(s.get('details').get('GET:204'), 1)
            self.assertEqual(s.get('details').get('POST:200'), 2)

        def assert_approximately_equal(a, b):
            self.assertTrue((abs(a - b) / a) < 0.1)
        if stats[0].get('direction') == 'out':
            self.assertEqual(stats[1].get('direction'), 'in')
            assert_approximately_equal(stats[0].get('bytesOut'), 1059)
            assert_approximately_equal(stats[0].get('bytesIn'), 8849)
            assert_approximately_equal(stats[1].get('bytesOut'), 8830)
            assert_approximately_equal(stats[1].get('bytesIn'), 1059)
        else:
            self.assertEqual(stats[0].get('direction'), 'in')
            self.assertEqual(stats[1].get('direction'), 'out')
            assert_approximately_equal(stats[0].get('bytesOut'), 8849)
            assert_approximately_equal(stats[0].get('bytesIn'), 1059)
            assert_approximately_equal(stats[1].get('bytesOut'), 1059)
            assert_approximately_equal(stats[1].get('bytesIn'), 8830)


class Http1AdaptorEdge2EdgeTest(Http1Edge2EdgeTestBase,
                                CommonHttp1Edge2EdgeTest,
                                Http1ClientCloseTestsMixIn,
                                Http1CurlTestsMixIn):
    """
    Test an HTTP servers and clients attached to edge routers separated by an
    interior router
    """
    @classmethod
    def setUpClass(cls):
        """Start a router"""
        super(Http1AdaptorEdge2EdgeTest, cls).setUpClass()

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

        super(Http1AdaptorEdge2EdgeTest, cls).\
            router('INT.A', 'interior', [('listener', {'role': 'edge', 'port': cls.INTA_edge1_port}),
                                         ('listener', {'role': 'edge', 'port': cls.INTA_edge2_port}),
                                         ])
        cls.INT_A = cls.routers[0]
        cls.INT_A.listener = cls.INT_A.addresses[0]

        super(Http1AdaptorEdge2EdgeTest, cls).\
            router('EA1', 'edge',
                   [('connector', {'name': 'uplink', 'role': 'edge',
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

        super(Http1AdaptorEdge2EdgeTest, cls).\
            router('EA2', 'edge',
                   [('connector', {'name': 'uplink', 'role': 'edge',
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

    def test_1001_client_request_close(self):
        """Simulate an HTTP client drop while sending a very large PUT"""
        self.client_request_close_test(self.http_server11_port,
                                       self.http_listener11_port,
                                       self.EA2.management)

    def test_1002_client_response_close(self):
        """Simulate an HTTP client drop while server sends very large response"""
        self.client_response_close_test(self.http_server11_port,
                                        self.http_listener11_port)

    def test_2000_curl_get(self):
        """Perform a get via curl"""
        self.curl_get_test("127.0.0.1", self.http_listener11_port,
                           self.http_server11_port)

    def test_2001_curl_put(self):
        """Perform a put via curl"""
        self.curl_put_test("127.0.0.1", self.http_listener11_port,
                           self.http_server11_port)

    def test_2002_curl_post(self):
        """Perform a post via curl"""
        self.curl_post_test("127.0.0.1", self.http_listener11_port,
                            self.http_server11_port)


class FakeHttpServerBase:
    """A very base socket server to simulate HTTP server behaviors"""

    def __init__(self, host='', port=80, bufsize=1024):
        super(FakeHttpServerBase, self).__init__()
        self.host = host
        self.port = port
        self.listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.listener.settimeout(TIMEOUT)
        self.listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.listener.bind((host, port))
        self.listener.listen(1)
        self.conn, self.addr = self.listener.accept()
        self.do_connect()
        while True:
            data = self.conn.recv(bufsize)
            if not data:
                break
            self.do_data(data)
        self.do_close()

    def do_connect(self):
        pass

    def do_data(self, data):
        pass

    def do_close(self):
        self.listener.shutdown(socket.SHUT_RDWR)
        self.listener.close()
        del self.listener
        self.conn.shutdown(socket.SHUT_RDWR)
        self.conn.close()
        del self.conn
        sleep(0.5)  # fudge factor allow socket close to complete


class Http1AdaptorBadEndpointsTest(TestCase,
                                   Http1ClientCloseTestsMixIn):
    """Subject the router to mis-behaving HTTP endpoints."""
    @classmethod
    def setUpClass(cls):
        """
        Single router configuration with one HTTPListener and one
        HTTPConnector.
        """
        super(Http1AdaptorBadEndpointsTest, cls).setUpClass()
        cls.http_server_port = cls.tester.get_port()
        cls.http_listener_port = cls.tester.get_port()
        cls.http_fake_port = cls.tester.get_port()

        config = [
            ('router', {'mode': 'standalone',
                        'id': 'TestBadEndpoints',
                        'allowUnsettledMulticast': 'yes'}),
            ('listener', {'role': 'normal',
                          'port': cls.tester.get_port()}),
            ('httpConnector', {'port': cls.http_server_port,
                               'protocolVersion': 'HTTP1',
                               'address': 'testServer'}),
            ('httpListener', {'port': cls.http_listener_port,
                              'protocolVersion': 'HTTP1',
                              'address': 'testServer'}),
            ('httpListener', {'port': cls.http_fake_port,
                              'protocolVersion': 'HTTP1',
                              'address': 'fakeServer'}),
            ('address', {'prefix': 'closest',   'distribution': 'closest'}),
            ('address', {'prefix': 'multicast', 'distribution': 'multicast'}),
        ]
        config = Qdrouterd.Config(config)
        cls.INT_A = cls.tester.qdrouterd("TestBadEndpoints", config, wait=True)
        cls.INT_A.listener = cls.INT_A.addresses[0]

    def test_01_unsolicited_response(self):
        """
        Create a server that sends an immediate Request Timeout response
        without first waiting for a request to arrive.
        """
        class UnsolicitedResponse(FakeHttpServerBase):

            def __init__(self, host, port):
                self.request_sent = False
                super(UnsolicitedResponse, self).__init__(host, port)

            def do_connect(self):
                self.conn.sendall(b'HTTP/1.1 408 Request Timeout\r\n'
                                  + b'Content-Length: 10\r\n'
                                  + b'\r\n'
                                  + b'Bad Server')
                self.request_sent = True

        count, error = http1_ping(self.http_server_port,
                                  self.http_listener_port)
        self.assertIsNone(error)
        self.assertEqual(1, count)
        server = UnsolicitedResponse('127.0.0.1', self.http_server_port)
        self.assertTrue(server.request_sent)
        count, error = http1_ping(self.http_server_port,
                                  self.http_listener_port)
        self.assertIsNone(error)
        self.assertEqual(1, count)

    def test_02_bad_request_message(self):
        """Test various improperly constructed request messages"""
        server = TestServer(server_port=self.http_server_port,
                            client_port=self.http_listener_port,
                            tests={})

        body_filler = "?" * 1024 * 300  # Q2

        msg = Message(body="NOMSGID " + body_filler)
        ts = AsyncTestSender(address=self.INT_A.listener,
                             target="testServer",
                             message=msg)
        ts.wait()
        self.assertEqual(1, ts.rejected)

        msg = Message(body="NO REPLY TO " + body_filler)
        msg.id = 1
        ts = AsyncTestSender(address=self.INT_A.listener,
                             target="testServer",
                             message=msg)
        ts.wait()
        self.assertEqual(1, ts.rejected)

        msg = Message(body="NO SUBJECT " + body_filler)
        msg.id = 1
        msg.reply_to = "amqp://fake/reply_to"
        ts = AsyncTestSender(address=self.INT_A.listener,
                             target="testServer",
                             message=msg)
        ts.wait()
        self.assertEqual(1, ts.rejected)

        msg = Message(body="NO APP PROPERTIES " + body_filler)
        msg.id = 1
        msg.reply_to = "amqp://fake/reply_to"
        msg.subject = "GET"
        ts = AsyncTestSender(address=self.INT_A.listener,
                             target="testServer",
                             message=msg)
        ts.wait()
        self.assertEqual(1, ts.rejected)

        # TODO: fix body parsing (returns NEED_MORE)
        # msg = Message(body="INVALID BODY " + body_filler)
        # msg.id = 1
        # msg.reply_to = "amqp://fake/reply_to"
        # msg.subject = "GET"
        # msg.properties = {"http:target": "/Some/target"}
        # ts = AsyncTestSender(address=self.INT_A.listener,
        #                      target="testServer",
        #                      message=msg)
        # ts.wait()
        # self.assertEqual(1, ts.rejected);

        server.wait()

        # verify router is still sane:
        count, error = http1_ping(self.http_server_port,
                                  self.http_listener_port)
        self.assertIsNone(error)
        self.assertEqual(1, count)

    def test_03_bad_response_message(self):
        """Test various improperly constructed response messages"""
        DUMMY_TESTS = {
            "GET": [
                (RequestMsg("GET", "/GET/test_03_bad_response_message",
                            headers={"Content-Length": "000"}),
                 None,
                 None,
                 ),
            ]
        }

        body_filler = "?" * 1024 * 300  # Q2

        # fake server - just to create a sink for the "fakeServer" address so
        # credit will be granted.
        rx = AsyncTestReceiver(self.INT_A.listener,
                               source="fakeServer")

        # no correlation id:
        client = ThreadedTestClient(DUMMY_TESTS,
                                    self.http_fake_port)
        req = rx.queue.get(timeout=TIMEOUT)
        resp = Message(body="NO CORRELATION ID " + body_filler)
        resp.to = req.reply_to
        ts = AsyncTestSender(address=self.INT_A.listener,
                             target=req.reply_to,
                             message=resp)
        ts.wait()
        self.assertEqual(1, ts.rejected)
        client.wait()
        self.assertIsNotNone(client.error)

        # missing application properties
        client = ThreadedTestClient(DUMMY_TESTS,
                                    self.http_fake_port)
        req = rx.queue.get(timeout=TIMEOUT)

        resp = Message(body="NO APPLICATION PROPS " + body_filler)
        resp.to = req.reply_to
        resp.correlation_id = req.id
        ts = AsyncTestSender(address=self.INT_A.listener,
                             target=req.reply_to,
                             message=resp)
        ts.wait()
        self.assertEqual(1, ts.rejected)
        client.wait()
        self.assertIsNotNone(client.error)

        # no status application property
        client = ThreadedTestClient(DUMMY_TESTS,
                                    self.http_fake_port)
        req = rx.queue.get(timeout=TIMEOUT)
        resp = Message(body="MISSING STATUS HEADER " + body_filler)
        resp.to = req.reply_to
        resp.correlation_id = req.id
        resp.properties = {"stuff": "value"}
        ts = AsyncTestSender(address=self.INT_A.listener,
                             target=req.reply_to,
                             message=resp)
        ts.wait()
        self.assertEqual(1, ts.rejected)
        client.wait()
        self.assertIsNotNone(client.error)

        # TODO: fix body parsing (returns NEED_MORE)
        # # invalid body format
        # client = ThreadedTestClient(DUMMY_TESTS,
        #                             self.http_fake_port)
        # req = rx.queue.get(timeout=TIMEOUT)
        # resp = Message(body="INVALID BODY FORMAT " + body_filler)
        # resp.to = req.reply_to
        # resp.correlation_id = req.id
        # resp.properties = {"http:status": 200}
        # ts = AsyncTestSender(address=self.INT_A.listener,
        #                      target=req.reply_to,
        #                      message=resp)
        # ts.wait()
        # self.assertEqual(1, ts.rejected);
        # client.wait()
        # self.assertIsNotNone(client.error)

        rx.stop()
        sleep(0.5)  # fudge factor allow socket close to complete

        # verify router is still sane:
        count, error = http1_ping(self.http_server_port,
                                  self.http_listener_port)
        self.assertIsNone(error)
        self.assertEqual(1, count)

    def test_04_client_request_close(self):
        """Simulate an HTTP client drop while sending a very large PUT"""
        self.client_request_close_test(self.http_server_port,
                                       self.http_listener_port,
                                       self.INT_A.management)

    def test_05_client_response_close(self):
        """Simulate an HTTP client drop while server sends very large response"""
        self.client_response_close_test(self.http_server_port,
                                        self.http_listener_port)


class Http1AdaptorQ2Standalone(TestCase):
    """
    Force Q2 blocking/recovery on both client and server endpoints. This test
    uses a single router to ensure both client facing and server facing
    Q2 components of the HTTP/1.x adaptor are triggered.
    """
    @classmethod
    def setUpClass(cls):
        """
        Single router configuration with one HTTPListener and one
        HTTPConnector.
        """
        super(Http1AdaptorQ2Standalone, cls).setUpClass()

        cls.http_server_port = cls.tester.get_port()
        cls.http_listener_port = cls.tester.get_port()

        config = [
            ('router', {'mode': 'standalone',
                        'id': 'RowdyRoddyRouter',
                        'allowUnsettledMulticast': 'yes'}),
            ('listener', {'role': 'normal',
                          'port': cls.tester.get_port()}),
            ('httpListener', {'port': cls.http_listener_port,
                              'protocolVersion': 'HTTP1',
                              'address': 'testServer'}),
            ('httpConnector', {'port': cls.http_server_port,
                               'host': '127.0.0.1',
                               'protocolVersion': 'HTTP1',
                               'address': 'testServer'}),
            ('address', {'prefix': 'closest',   'distribution': 'closest'}),
            ('address', {'prefix': 'multicast', 'distribution': 'multicast'}),
        ]
        config = Qdrouterd.Config(config)
        cls.INT_A = cls.tester.qdrouterd("TestBadEndpoints", config, wait=True)
        cls.INT_A.listener = cls.INT_A.addresses[0]

    def _write_until_full(self, sock, data, timeout):
        """
        Write data to socket until either all data written or timeout.
        Return the number of bytes written, which will == len(data) if timeout
        not hit
        """
        sock.setblocking(0)
        sent = 0

        while sent < len(data):
            try:
                _, rw, _ = select.select([], [sock], [], timeout)
            except select.error as serror:
                if serror[0] == errno.EINTR:
                    print("ignoring interrupt from select(): %s" % str(serror))
                    continue
                raise  # assuming fatal...
            if rw:
                sent += sock.send(data[sent:])
            else:
                break  # timeout
        return sent

    def _read_until_empty(self, sock, timeout):
        """Read data from socket until timeout occurs.  Return read data."""
        sock.setblocking(0)
        data = b''

        while True:
            try:
                rd, _, _ = select.select([sock], [], [], timeout)
            except select.error as serror:
                if serror[0] == errno.EINTR:
                    print("ignoring interrupt from select(): %s" % str(serror))
                    continue
                raise  # assuming fatal...
            if rd:
                data += sock.recv(4096)
            else:
                break  # timeout
        return data

    def test_01_backpressure_client(self):
        """Trigger Q2 backpressure against the HTTP client."""
        # create a listener socket to act as the server service
        server_listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server_listener.settimeout(TIMEOUT)
        server_listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server_listener.bind(('', self.http_server_port))
        server_listener.listen(1)

        # block until router connects
        server_sock, host_port = server_listener.accept()
        server_sock.settimeout(0.5)
        server_sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

        # create a client connection to the router
        client_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        client_sock.settimeout(0.5)
        client_sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        client_sock.connect((host_port[0], self.http_listener_port))

        # send a Very Large PUSH request, expecting it to block at some point

        push_req_hdr = b'PUSH / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n'
        count = self._write_until_full(client_sock, push_req_hdr, 1.0)
        self.assertEqual(len(push_req_hdr), count)

        chunk = b'8000\r\n' + b'X' * 0x8000 + b'\r\n'
        last_chunk = b'0 \r\n\r\n'
        count = 0
        deadline = time() + TIMEOUT
        while deadline >= time():
            count = self._write_until_full(client_sock, chunk, 5.0)
            if count < len(chunk):
                break
        self.assertFalse(time() > deadline,
                         "Client never blocked as expected!")

        # client should now be in Q2 block. Drain the server to unblock Q2
        _ = self._read_until_empty(server_sock, 2.0)

        # finish the PUSH
        if count:
            remainder = self._write_until_full(client_sock, chunk[count:], 1.0)
            self.assertEqual(len(chunk), count + remainder)

        count = self._write_until_full(client_sock, last_chunk, 1.0)
        self.assertEqual(len(last_chunk), count)

        # receive the request and reply
        _ = self._read_until_empty(server_sock, 2.0)

        response = b'HTTP/1.1 201 Created\r\nContent-Length: 0\r\n\r\n'
        count = self._write_until_full(server_sock, response, 1.0)
        self.assertEqual(len(response), count)

        # complete the response read
        _ = self._read_until_empty(client_sock, 2.0)
        self.assertEqual(len(response), len(_))

        client_sock.shutdown(socket.SHUT_RDWR)
        client_sock.close()

        server_sock.shutdown(socket.SHUT_RDWR)
        server_sock.close()

        server_listener.shutdown(socket.SHUT_RDWR)
        server_listener.close()

        # search the router log file to verify Q2 was hit

        block_ct = 0
        unblock_ct = 0
        with io.open(self.INT_A.logfile_path) as f:
            for line in f:
                if 'client link blocked on Q2 limit' in line:
                    block_ct += 1
                if 'client link unblocked from Q2 limit' in line:
                    unblock_ct += 1
        self.assertTrue(block_ct > 0)
        self.assertEqual(block_ct, unblock_ct)

    def test_02_backpressure_server(self):
        """Trigger Q2 backpressure against the HTTP server."""
        small_get_req = b'GET / HTTP/1.1\r\nContent-Length: 0\r\n\r\n'

        # create a listener socket to act as the server service
        server_listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server_listener.settimeout(TIMEOUT)
        server_listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server_listener.bind(('', self.http_server_port))
        server_listener.listen(1)

        # block until router connects
        server_sock, host_port = server_listener.accept()
        server_sock.settimeout(0.5)
        server_sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

        # create a client connection to the router
        client_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        client_sock.settimeout(0.5)
        client_sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        client_sock.connect((host_port[0], self.http_listener_port))

        # send GET request - expect this to be successful
        count = self._write_until_full(client_sock, small_get_req, 1.0)
        self.assertEqual(len(small_get_req), count)

        request = self._read_until_empty(server_sock, 5.0)
        self.assertEqual(len(small_get_req), len(request))

        # send a Very Long response, expecting it to block at some point
        chunk = b'8000\r\n' + b'X' * 0x8000 + b'\r\n'
        last_chunk = b'0 \r\n\r\n'
        response = b'HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n'

        count = self._write_until_full(server_sock, response, 1.0)
        self.assertEqual(len(response), count)

        count = 0
        deadline = time() + TIMEOUT
        while deadline >= time():
            count = self._write_until_full(server_sock, chunk, 5.0)
            if count < len(chunk):
                break
        self.assertFalse(time() > deadline,
                         "Server never blocked as expected!")

        # server should now be in Q2 block. Drain the client to unblock Q2
        _ = self._read_until_empty(client_sock, 2.0)

        # finish the response
        if count:
            remainder = self._write_until_full(server_sock, chunk[count:], 1.0)
            self.assertEqual(len(chunk), count + remainder)

        count = self._write_until_full(server_sock, last_chunk, 1.0)
        self.assertEqual(len(last_chunk), count)
        server_sock.shutdown(socket.SHUT_RDWR)
        server_sock.close()

        _ = self._read_until_empty(client_sock, 1.0)
        client_sock.shutdown(socket.SHUT_RDWR)
        client_sock.close()

        server_listener.shutdown(socket.SHUT_RDWR)
        server_listener.close()

        # search the router log file to verify Q2 was hit

        block_ct = 0
        unblock_ct = 0
        with io.open(self.INT_A.logfile_path) as f:
            for line in f:
                if 'server link blocked on Q2 limit' in line:
                    block_ct += 1
                if 'server link unblocked from Q2 limit' in line:
                    unblock_ct += 1
        self.assertTrue(block_ct > 0)
        self.assertEqual(block_ct, unblock_ct)


if __name__ == '__main__':
    unittest.main(main_module())

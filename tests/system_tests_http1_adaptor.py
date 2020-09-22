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

from __future__ import unicode_literals
from __future__ import division
from __future__ import absolute_import
from __future__ import print_function


import sys
from threading import Thread
try:
    from http.server import HTTPServer, BaseHTTPRequestHandler
    from http.client import HTTPConnection
    from http.client import HTTPException
except ImportError:
    from BaseHTTPServer import HTTPServer, BaseHTTPRequestHandler
    from httplib import HTTPConnection, HTTPException

from proton.handlers import MessagingHandler
from proton.reactor import Container
from system_test import TestCase, unittest, main_module, Qdrouterd
from system_test import TIMEOUT, Logger


class RequestMsg(object):
    """
    A 'hardcoded' HTTP request message.  This class writes its request
    message to the HTTPConnection.
    """
    def __init__(self, method, target, headers=None, body=None):
        self.method = method
        self.target = target
        self.headers = headers or {}
        self.body = body

    def send_request(self, conn):
        conn.putrequest(self.method, self.target)
        for key, value in self.headers.items():
            conn.putheader(key, value)
        conn.endheaders()
        if self.body:
            conn.send(self.body)


class ResponseMsg(object):
    """
    A 'hardcoded' HTTP response message.  This class writes its response
    message when called by the HTTPServer via the BaseHTTPRequestHandler
    """
    def __init__(self, status, version=None, reason=None,
                 headers=None, body=None, eom_close=False, error=False):
        self.status = status
        self.version = version or "HTTP/1.1"
        self.reason = reason
        self.headers = headers or []
        self.body = body
        self.eom_close = eom_close
        self.error = error

    def send_response(self, handler):
        handler.protocol_version = self.version
        if self.error:
            handler.send_error(self.status,
                               message=self.reason,
                               explain=self.body)
            return

        handler.send_response(self.status, self.reason)
        for key, value in self.headers.items():
            handler.send_header(key, value)
        handler.end_headers()

        if self.body:
            handler.wfile.write(self.body)
            handler.wfile.flush()

        return self.eom_close


class ResponseValidator(object):
    """
    Validate a response as received by the HTTP client
    """
    def __init__(self, status=200, expect_headers=None, expect_body=None):
        if expect_headers is None:
            expect_headers = {}
        self.status = status
        self.expect_headers = expect_headers
        self.expect_body = expect_body

    def check_response(self, rsp):
        if self.status and rsp.status != self.status:
            raise Exception("Bad response code, expected %s got %s"
                            % (self.status, rsp.status))
        for key, value in self.expect_headers.items():
            if rsp.getheader(key) != value:
                raise Exception("Missing/bad header (%s), expected %s got %s"
                                % (key, value, rsp.getheader(key)))

        body = rsp.read()
        if (self.expect_body and self.expect_body != body):
            raise Exception("Bad response body expected %s got %s"
                            % (self.expect_body, body))
        return body


TEST_SCENARIOS = {

    #
    # GET
    #

    "GET": [
        (RequestMsg("GET", "/GET/error"),
         ResponseMsg(400, reason="Bad breath", error=True),
         ResponseValidator(status=400)),

        (RequestMsg("GET", "/GET/content_len"),
         ResponseMsg(200, reason="OK",
                     headers={"Content-Length": 1,
                              "Content-Type": "text/plain;charset=utf-8"},
                     body=b'?'),
         ResponseValidator(expect_headers={'Content-Length': '1'},
                           expect_body=b'?')),

        (RequestMsg("GET", "/GET/content_len_511"),
         ResponseMsg(200, reason="OK",
                     headers={"Content-Length": 511,
                              "Content-Type": "text/plain;charset=utf-8"},
                     body=b'X' * 511),
         ResponseValidator(expect_headers={'Content-Length': '511'},
                           expect_body=b'X' * 511)),

        (RequestMsg("GET", "/GET/content_len_4096"),
         ResponseMsg(200, reason="OK",
                     headers={"Content-Length": 4096,
                              "Content-Type": "text/plain;charset=utf-8"},
                     body=b'X' * 4096),
         ResponseValidator(expect_headers={'Content-Length': '4096'},
                           expect_body=b'X' * 4096)),

        (RequestMsg("GET", "/GET/chunked"),
         ResponseMsg(200, reason="OK",
                     headers={"transfer-encoding": "chunked",
                              "Content-Type": "text/plain;charset=utf-8"},
                     # note: the chunk length does not count the trailing CRLF
                     body=b'16\r\n'
                     + b'Mary had a little pug \r\n'
                     + b'1b\r\n'
                     + b'Its name was "Skupper-Jack"\r\n'
                     + b'0\r\n'
                     + b'Optional: Trailer\r\n'
                     + b'Optional: Trailer\r\n'
                     + b'\r\n'),
         ResponseValidator(expect_headers={'transfer-encoding': 'chunked'},
                           expect_body=b'Mary had a little pug Its name was "Skupper-Jack"')),

        (RequestMsg("GET", "/GET/chunked_large"),
         ResponseMsg(200, reason="OK",
                     headers={"transfer-encoding": "chunked",
                              "Content-Type": "text/plain;charset=utf-8"},
                     # note: the chunk length does not count the trailing CRLF
                     body=b'1\r\n'
                     + b'?\r\n'
                     + b'800\r\n'
                     + b'X' * 0x800 + b'\r\n'
                     + b'13\r\n'
                     + b'Y' * 0x13  + b'\r\n'
                     + b'0\r\n'
                     + b'Optional: Trailer\r\n'
                     + b'Optional: Trailer\r\n'
                     + b'\r\n'),
         ResponseValidator(expect_headers={'transfer-encoding': 'chunked'},
                           expect_body=b'?' + b'X' * 0x800 + b'Y' * 0x13)),

        (RequestMsg("GET", "/GET/info_content_len"),
         [ResponseMsg(100, reason="Continue",
                      headers={"Blab": 1, "Blob": "?"}),
          ResponseMsg(200, reason="OK",
                      headers={"Content-Length": 1,
                               "Content-Type": "text/plain;charset=utf-8"},
                      body=b'?')],
         ResponseValidator(expect_headers={'Content-Type': "text/plain;charset=utf-8"},
                           expect_body=b'?')),

        (RequestMsg("GET", "/GET/no_length"),
         ResponseMsg(200, reason="OK",
                     headers={"Content-Type": "text/plain;charset=utf-8",
                              #         ("connection", "close")
                     },
                     body=b'Hi! ' * 1024 + b'X',
                     eom_close=True),
         ResponseValidator(expect_body=b'Hi! ' * 1024 + b'X')),
    ],

    #
    # HEAD
    #

    "HEAD": [
        (RequestMsg("HEAD", "/HEAD/test_01"),
         ResponseMsg(200, headers={"App-Header-1": "Value 01",
                                   "Content-Length": "10",
                                   "App-Header-2": "Value 02"},
                     body=None),
         ResponseValidator(expect_headers={"App-Header-1": "Value 01",
                                        "Content-Length": "10",
                                        "App-Header-2": "Value 02"})
        ),
        (RequestMsg("HEAD", "/HEAD/test_02"),
         ResponseMsg(200, headers={"App-Header-1": "Value 01",
                                   "Transfer-Encoding": "chunked",
                                   "App-Header-2": "Value 02"}),
         ResponseValidator(expect_headers={"App-Header-1": "Value 01",
                                        "Transfer-Encoding": "chunked",
                                        "App-Header-2": "Value 02"})),

        (RequestMsg("HEAD", "/HEAD/test_03"),
         ResponseMsg(200, headers={"App-Header-3": "Value 03"}, eom_close=True),
         ResponseValidator(expect_headers={"App-Header-3": "Value 03"})),
    ],

    #
    # POST
    #

    "POST": [
        (RequestMsg("POST", "/POST/test_01",
                    headers={"App-Header-1": "Value 01",
                             "Content-Length": "18",
                             "Content-Type": "application/x-www-form-urlencoded"},
                    body=b'one=1&two=2&three=3'),
         ResponseMsg(200, reason="OK",
                     headers={"Response-Header": "whatever",
                              "Transfer-Encoding": "chunked"},
                     body=b'8\r\n'
                     + b'12345678\r\n'
                     + b'f\r\n'
                     + b'abcdefghijklmno\r\n'
                     + b'000\r\n'
                     + b'\r\n'),
         ResponseValidator(expect_body=b'12345678abcdefghijklmno')
        ),
        (RequestMsg("POST", "/POST/test_02",
                    headers={"App-Header-1": "Value 01",
                              "Transfer-Encoding": "chunked"},
                    body=b'01\r\n'
                    + b'!\r\n'
                    + b'0\r\n\r\n'),
         ResponseMsg(200, reason="OK",
                     headers={"Response-Header": "whatever",
                             "Content-Length": "9"},
                     body=b'Hi There!',
                     eom_close=True),
         ResponseValidator(expect_body=b'Hi There!')
        ),
    ],

    #
    # PUT
    #
    
    "PUT": [
        (RequestMsg("PUT", "/PUT/test_01",
                    headers={"Put-Header-1": "Value 01",
                             #"Transfer-Encoding": "beaten,robbed,chunked",
                             "Transfer-Encoding": "chunked",
                             "Content-Type": "text/plain;charset=utf-8"},
                    body=b'80\r\n'
                    + b'$' * 0x80 + b'\r\n'
                    + b'0\r\n\r\n'),
         ResponseMsg(201, reason="Created",
                     headers={"Response-Header": "whatever",
                              "Content-length": "3"},
                     body=b'ABC'),
         ResponseValidator(status=201, expect_body=b'ABC')
        ),

        (RequestMsg("PUT", "/PUT/test_02",
                    headers={"Put-Header-1": "Value 01",
                             "Content-length": "0",
                             "Content-Type": "text/plain;charset=utf-8"}),
         ResponseMsg(201, reason="Created",
                     headers={"Response-Header": "whatever",
                              # "Transfer-Encoding": "beaten,robbed,chunked"},
                              "Transfer-Encoding": "chunked"},
                     body=b'1\r\n$\r\n0\r\n\r\n',
                     eom_close=True),
         ResponseValidator(status=201, expect_body=b'$')
        ),
    ]
}


class RequestHandler(BaseHTTPRequestHandler):
    """
    Dispatches requests received by the HTTPServer based on the method
    """
    def _execute_request(self, tests):
        for req, resp, val in tests:
            if req.target == self.path:
                if not isinstance(resp, list):
                    resp = [resp]
                for r in resp:
                    r.send_response(self)
                    if r.eom_close:
                        self.close_connection = 1
                        self.server.system_test_server_done = True
                return

        resp = HttpResponse(404, reason="Not Found", error=True)
        resp.send_response(self)

    def do_GET(self):
        self._execute_request(TEST_SCENARIOS["GET"])

    def do_HEAD(self):
        self._execute_request(TEST_SCENARIOS["HEAD"])

    def do_POST(self):
        self._execute_request(TEST_SCENARIOS["POST"])

    def do_PUT(self):
        self._execute_request(TEST_SCENARIOS["PUT"])

    # these overrides just quiet the test output
    # comment them out to help debug:
    def log_request(self, code=None, size=None):
        pass

    def log_error(self, format=None, *args):
        pass

    def log_message(self, format=None, *args):
        pass


class MyHTTPServer(HTTPServer):
    """
    Adds a switch to the HTTPServer to allow it to exit gracefully
    """
    def __init__(self, addr, rh):
        self.system_test_server_done = False
        super(MyHTTPServer, self).__init__(addr, rh)


class TestServer(object):
    """
    A HTTPServer running in a separate thread
    """
    def __init__(self, port=8080):
        self._logger = Logger(title="TestServer", print_to_console=False)
        self._server_addr = ("", port)
        self._server = MyHTTPServer(self._server_addr, RequestHandler)
        self._server.allow_reuse_address = True
        self._thread = Thread(target=self._run)
        self._thread.daemon = True
        self._thread.start()

    def _run(self):
        self._logger.log("TestServer listening on %s:%s" % self._server_addr)
        try:
            while not self._server.system_test_server_done:
                self._server.handle_request()
        except Exception as exc:
            self._logger.log("TestServer %s:%s crash: %s" %
                             (self._server_addr, exc))
            raise
        self._logger.log("TestServer %s:%s closed" % self._server_addr)

    def wait(self, timeout=TIMEOUT):
        self._logger.log("TestServer %s:%s shutting down" % self._server_addr)
        self._thread.join(timeout=TIMEOUT)
        if self._server:
            self._server.server_close()


class Http1AdaptorOneRouterTest(TestCase):
    """
    Test an HTTP server and client attached to a standalone router
    """
    @classmethod
    def setUpClass(cls):
        """Start a router"""
        super(Http1AdaptorOneRouterTest, cls).setUpClass()

        def router(name, mode, extra):
            config = [
                ('router', {'mode': mode,
                            'id': name,
                            'allowUnsettledMulticast': 'yes'}),
                ('listener', {'role': 'normal',
                              'port': cls.tester.get_port()}),
                ('address', {'prefix': 'closest',   'distribution': 'closest'}),
                ('address', {'prefix': 'multicast', 'distribution': 'multicast'}),
            ]

            if extra:
                config.extend(extra)
            config = Qdrouterd.Config(config)
            cls.routers.append(cls.tester.qdrouterd(name, config, wait=True))
            return cls.routers[-1]

        # configuration:
        # interior
        #
        #  +----------------+
        #  |     INT.A      |
        #  +----------------+
        #      ^         ^
        #      |         |
        #      V         V
        #  <clients>  <server>

        cls.routers = []
        cls.http_server_port = cls.tester.get_port()
        cls.http_listener_port = cls.tester.get_port()

        router('INT.A', 'standalone',
               [('httpConnector', {'port': cls.http_server_port,
                                   'protocolVersion': 'HTTP1',
                                   'address': 'testServer'}),
                ('httpListener', {'port': cls.http_listener_port,
                                  'protocolVersion': 'HTTP1',
                                  'address': 'testServer'})
               ])
        cls.INT_A = cls.routers[0]
        cls.INT_A.listener = cls.INT_A.addresses[0]

    def _do_request(self, tests):
        server = TestServer(port=self.http_server_port)
        for req, _, val in tests:
            client = HTTPConnection("127.0.0.1:%s" % self.http_listener_port,
                                    timeout=TIMEOUT)
            req.send_request(client)
            rsp = client.getresponse()
            try:
                body = val.check_response(rsp)
            except Exception as exc:
                self.fail("request failed:  %s" % str(exc))

            if req.method is "BODY":
                self.assertEqual(b'', body)

            client.close()
        server.wait()

    def test_001_get(self):
        self._do_request(TEST_SCENARIOS["GET"])

    def test_002_head(self):
        self._do_request(TEST_SCENARIOS["HEAD"])

    def test_003_post(self):
        self._do_request(TEST_SCENARIOS["POST"])

    def test_004_put(self):
        self._do_request(TEST_SCENARIOS["PUT"])


class Http1AdaptorInteriorTest(TestCase):
    """
    Test an HTTP server connected to an interior router serving multiple HTTP
    clients
    """
    @classmethod
    def setUpClass(cls):
        """Start a router"""
        super(Http1AdaptorInteriorTest, cls).setUpClass()

        def router(name, mode, extra):
            config = [
                ('router', {'mode': mode,
                            'id': name,
                            'allowUnsettledMulticast': 'yes'}),
                ('listener', {'role': 'normal',
                              'port': cls.tester.get_port()}),
                ('address', {'prefix': 'closest',   'distribution': 'closest'}),
                ('address', {'prefix': 'multicast', 'distribution': 'multicast'}),
            ]

            if extra:
                config.extend(extra)
            config = Qdrouterd.Config(config)
            cls.routers.append(cls.tester.qdrouterd(name, config, wait=True))
            return cls.routers[-1]

        # configuration:
        # one edge, one interior
        #
        #  +-------+    +---------+
        #  |  EA1  |<==>|  INT.A  |
        #  +-------+    +---------+
        #      ^             ^
        #      |             |
        #      V             V
        #   <client>      <server>

        cls.routers = []
        cls.INTA_edge_port   = cls.tester.get_port()
        cls.http_server_port = cls.tester.get_port()
        cls.http_listener_port = cls.tester.get_port()

        router('INT.A', 'interior',
               [('listener', {'role': 'edge', 'port': cls.INTA_edge_port}),
                ('httpConnector', {'port': cls.http_server_port,
                                   'protocolVersion': 'HTTP1',
                                   'address': 'testServer'})
               ])
        cls.INT_A = cls.routers[0]
        cls.INT_A.listener = cls.INT_A.addresses[0]

        router('EA1', 'edge',
               [('connector', {'name': 'uplink', 'role': 'edge',
                               'port': cls.INTA_edge_port}),
                ('httpListener', {'port': cls.http_listener_port,
                                  'protocolVersion': 'HTTP1',
                                  'address': 'testServer'})
               ])
        cls.EA1 = cls.routers[1]
        cls.EA1.listener = cls.EA1.addresses[0]

        cls.EA1.wait_connectors()
        cls.INT_A.wait_address('EA1')


    def test_0(self):
        #TBD
        pass


if __name__ == '__main__':
    unittest.main(main_module())

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
import socket
import uuid
from threading import Thread

from time import sleep
from http.server import HTTPServer, BaseHTTPRequestHandler
from http.client import HTTPConnection
from http.client import HTTPException

from system_test import TestCase, TIMEOUT, Logger, Qdrouterd, unittest
from system_test import curl_available, run_curl


TEST_SERVER_ERROR = "TestServer failed to start due to port %s already in use issue"
CURL_VERSION = (7, 47, 0)   # minimum required


def _curl_ok():
    """
    Returns True if curl is installed and is the proper version for
    running http1.1
    """
    installed = curl_available()
    return installed and installed >= CURL_VERSION


class RequestHandler(BaseHTTPRequestHandler):
    """
    Dispatches requests received by the HTTPServer based on the method
    """
    protocol_version = 'HTTP/1.1'

    def _execute_request(self, tests):
        for req, resp, val in tests:
            if req.target == self.path:
                xhdrs = None
                if "test-echo" in self.headers:
                    xhdrs = {"test-echo":
                             self.headers["test-echo"]}
                self._consume_body()
                if not isinstance(resp, list):
                    resp = [resp]
                for r in resp:
                    r.send_response(self, extra_headers=xhdrs)
                self.server.request_count += 1
                return
        self.send_error(404, "Not Found")

    def do_GET(self):
        self._execute_request(self.server.system_tests["GET"])

    def do_HEAD(self):
        self._execute_request(self.server.system_tests["HEAD"])

    def do_POST(self):
        if self.path == "/SHUTDOWN":
            self.send_response(200, "OK")
            self.send_header("Content-Length", "13")
            self.end_headers()
            self.wfile.write(b'Server Closed')
            self.wfile.flush()
            self.close_connection = True  # server will close connection from router
            self.server.server_killed = True  # server will not accept a reconnect
            return
        self._execute_request(self.server.system_tests["POST"])

    def do_PUT(self):
        self._execute_request(self.server.system_tests["PUT"])

    # these overrides just quiet the test output
    # comment them out to help debug:
    def log_request(self, code=None, size=None):
        pass

    def log_message(self, *args, format=None):
        pass

    def _consume_body(self):
        """
        Read the entire body off the rfile.  This must be done to allow
        multiple requests on the same socket
        """
        if self.command == 'HEAD':
            return b''

        for key, value in self.headers.items():
            if key.lower() == 'content-length':
                return self.rfile.read(int(value))

            if key.lower() == 'transfer-encoding'  \
               and 'chunked' in value.lower():
                body = b''
                while True:
                    header = self.rfile.readline().strip().split(b';')[0]
                    hlen = int(header, base=16)
                    if hlen > 0:
                        data = self.rfile.read(hlen + 2)  # 2 = \r\n
                        body += data[:-2]
                    else:
                        self.rfile.readline()  # discard last \r\n
                        break
                return body
        return b''


class RequestHandler10(RequestHandler):
    """
    RequestHandler that forces the server to use HTTP version 1.0 semantics
    """
    protocol_version = 'HTTP/1.0'


class MyHTTPServer(HTTPServer):
    """
    Adds a switch to the HTTPServer to allow it to exit gracefully
    """

    def __init__(self, addr, handler_cls, testcases):
        self.system_tests = testcases
        self.request_count = 0
        super().__init__(addr, handler_cls)


class ThreadedTestClient:
    """
    An HTTP client running in a separate thread
    """

    def __init__(self, tests, port, repeat=1):
        self._id = uuid.uuid4().hex
        self._conn_addr = ("127.0.0.1:%s" % port)
        self._tests = tests
        self._repeat = repeat
        self._logger = Logger(title="TestClient: %s" % self._id,
                              print_to_console=False)
        self._thread = Thread(target=self._run)
        self.error = None
        self.count = 0
        self._thread.start()

    def _run(self):
        self._logger.log("TestClient connecting on %s" % self._conn_addr)
        client = HTTPConnection(self._conn_addr, timeout=TIMEOUT)
        try:
            self._logger.log("TestClient connected")
            for loop in range(self._repeat):
                self._logger.log("TestClient start request %d" % loop)
                for op, tests in self._tests.items():
                    for req, _, val in tests:
                        self._logger.log("TestClient sending %s %s request" % (op, req.target))
                        req.send_request(client,
                                         {"test-echo": "%s-%s-%s-%s" % (self._id,
                                                                        loop,
                                                                        op,
                                                                        req.target)})
                        self._logger.log("TestClient getting %s response" % op)
                        try:
                            rsp = client.getresponse()
                        except HTTPException as exc:
                            self._logger.log("TestClient response failed: %s" % exc)
                            self.error = str(exc)
                            return
                        self._logger.log("TestClient response %s received" % op)
                        if val:
                            try:
                                body = val.check_response(rsp)
                            except Exception as exc:
                                self._logger.log("TestClient response invalid: %s"
                                                 % str(exc))
                                self.error = "client failed: %s" % str(exc)
                                return

                            if req.method == "BODY" and body != b'':
                                self._logger.log("TestClient response invalid: %s"
                                                 % "body present!")
                                self.error = "error: body present!"
                                return
                        self.count += 1
                        self._logger.log("TestClient request %s %s completed!" %
                                         (op, req.target))
        finally:
            client.close()
            self._logger.log("TestClient to %s closed" % self._conn_addr)

    def wait(self, timeout=TIMEOUT):
        self._thread.join(timeout=timeout)
        self._logger.log("TestClient %s shut down" % self._conn_addr)

    def dump_log(self):
        self._logger.dump()


class TestServer:
    """
    A HTTPServer running in a separate thread
    """
    __test__ = False

    @classmethod
    def new_server(cls, server_port, client_port, tests, handler_cls=None):
        num_attempts = 0
        max_attempts = 4
        while num_attempts < max_attempts:
            try:
                # Create an instance of TestServer. This might fail because the port has
                # not been relinquished yet. Try for a max of 4 seconds before giving up.
                server11 = TestServer(server_port=server_port,
                                      client_port=client_port,
                                      tests=tests,
                                      handler_cls=handler_cls)
                # Return the successfully created server.
                return server11
            except OSError:
                # TestServer creation failed. Try again in one second, for a max of 4 seconds.
                num_attempts += 1
                sleep(1)

        return None

    def __init__(self, server_port, client_port, tests, handler_cls=None):
        self._logger = Logger(title="TestServer", print_to_console=False)
        self._client_port = client_port
        self._server_addr = ("", server_port)
        self._server = MyHTTPServer(self._server_addr,
                                    handler_cls or RequestHandler,
                                    tests)
        self._server.allow_reuse_address = True
        self._server.timeout = TIMEOUT
        self._thread = Thread(target=self._run)
        self._thread.start()

    def _run(self):
        self._logger.log("TestServer listening on %s:%s" % self._server_addr)
        try:
            self._server.server_killed = False
            while not self._server.server_killed:
                self._server.handle_request()
        except Exception as exc:
            self._logger.log("TestServer %s crash: %s" %
                             (self._server_addr, exc))
            raise
        self._logger.log("TestServer %s:%s closed" % self._server_addr)

    def wait(self, timeout=TIMEOUT):
        self._logger.log("TestServer %s:%s shutting down" % self._server_addr)
        self.request_count = 0

        self._send_shutdown_request()
        self._thread.join(timeout=timeout)
        self._server.server_close()
        self.request_count = self._server.request_count
        self._server = None

    def _send_shutdown_request(self):
        """Sends a POST request instructing the test HTTPServer to shut down.

        This is necessary because the test HTTPServer cannot be interrupted while there is an open
        connection to it. The incoming connection in question is from a qdrouterd that we keep up
        for the duration of the entire testclass. The only place for server to graciously die is
        after processing an incoming request and closing the connection, but before accepting a new one.
        """
        client = HTTPConnection("127.0.0.1:%s" % self._client_port,
                                timeout=TIMEOUT)
        try:
            client.putrequest("POST", "/SHUTDOWN")
            client.putheader("Content-Length", "0")
            client.endheaders()
            with client.getresponse() as response:
                body = response.read()
            assert body == b'Server Closed', f"Unexpectedly, response was {body}"
        finally:
            client.close()


def http1_ping(sport, cport):
    """
    Test the HTTP path by doing a simple GET request
    """
    TEST = {
        "GET": [
            (RequestMsg("GET", "/GET/ping",
                        headers={"Content-Length": 0}),
             ResponseMsg(200, reason="OK",
                         headers={"Content-Length": 4,
                                  "Content-Type": "text/plain;charset=utf-8"},
                         body=b'pong'),
             ResponseValidator(expect_body=b'pong'))
        ]
    }

    server = TestServer.new_server(sport, cport, TEST)
    client = ThreadedTestClient(tests=TEST, port=cport)
    client.wait()
    server.wait()
    return client.count, client.error


class ResponseMsg:
    """
    A 'hardcoded' HTTP response message.  This class writes its response
    message when called by the HTTPServer via the BaseHTTPRequestHandler
    """

    def __init__(self, status, version=None, reason=None,
                 headers=None, body=None, error=False):
        self.status = status
        self.version = version or "HTTP/1.1"
        self.reason = reason
        self.headers = headers or {}
        self.body = body
        self.error = error

    def send_response(self, handler, extra_headers=None):
        extra_headers = extra_headers or {}
        if self.error:
            handler.send_error(self.status,
                               message=self.reason)
            return

        handler.send_response(self.status, self.reason)
        for key, value in self.headers.items():
            handler.send_header(key, value)
        for key, value in extra_headers.items():
            handler.send_header(key, value)
        handler.end_headers()

        if self.body:
            handler.wfile.write(self.body)
        handler.wfile.flush()


class RequestMsg:
    """
    A 'hardcoded' HTTP request message.  This class writes its request
    message to the HTTPConnection.
    """

    def __init__(self, method, target, headers=None, body=None):
        self.method = method
        self.target = target
        self.headers = headers or {}
        self.body = body

    def send_request(self, conn: HTTPConnection, extra_headers=None):
        extra_headers = extra_headers or {}
        conn.putrequest(self.method, self.target)
        for key, value in self.headers.items():
            conn.putheader(key, value)
        for key, value in extra_headers.items():
            conn.putheader(key, value)
        conn.endheaders()
        if self.body:
            conn.send(self.body)


class ResponseValidator:
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
        # always fully read the response first
        body = rsp.read()

        if self.status and rsp.status != self.status:
            raise Exception("Bad response code, expected %s got %s"
                            % (self.status, rsp.status))
        for key, value in self.expect_headers.items():
            if rsp.getheader(key) != value:
                raise Exception("Missing/bad header (%s), expected %s got %s"
                                % (key, value, rsp.getheader(key)))

        if self.expect_body and self.expect_body != body:
            raise Exception("Bad response body expected %s got %s"
                            % (self.expect_body, body))
        return body


class CommonHttp1Edge2EdgeTest:
    def test_01_concurrent_requests(self):
        """
        Test multiple concurrent clients sending streaming messages
        """

        REQ_CT = 3  # 3 requests per TEST_*
        TESTS_11 = {
            "PUT": [
                (RequestMsg("PUT", "/PUT/test_01_concurrent_requests_11",
                            headers={
                                "Transfer-encoding": "chunked",
                                "Content-Type": "text/plain;charset=utf-8"
                            },
                            # ~384K to trigger Q2
                            body=b'20000\r\n' + b'1' * 0x20000 + b'\r\n'
                            + b'20000\r\n' + b'2' * 0x20000 + b'\r\n'
                            + b'20000\r\n' + b'3' * 0x20000 + b'\r\n'
                            + b'13\r\nEND OF TRANSMISSION\r\n'
                            + b'0\r\n\r\n'),
                 ResponseMsg(201, reason="Created",
                             headers={"Test-Header": "/PUT/test_01_concurrent_requests_11",
                                      "Content-Length": "0"}),
                 ResponseValidator(status=201)
                 )],

            "GET": [
                (RequestMsg("GET", "/GET/test_01_concurrent_requests_11_small",
                            headers={"Content-Length": "000"}),
                 ResponseMsg(200, reason="OK",
                             headers={
                                 "Content-Length": "19",
                                 "Content-Type": "text/plain;charset=utf-8",
                                 "Test-Header": "/GET/test_01_concurrent_requests_11_small"
                             },
                             body=b'END OF TRANSMISSION'),
                 ResponseValidator(status=200)),

                (RequestMsg("GET", "/GET/test_01_concurrent_requests_11",
                            headers={"Content-Length": "000"}),
                 ResponseMsg(200, reason="OK",
                             headers={
                                 "transfer-Encoding": "chunked",
                                 "Content-Type": "text/plain;charset=utf-8",
                                 "Test-Header": "/GET/test_01_concurrent_requests_11"
                             },
                             # ~384K to trigger Q2
                             body=b'20000\r\n' + b'1' * 0x20000 + b'\r\n'
                             + b'20000\r\n' + b'2' * 0x20000 + b'\r\n'
                             + b'20000\r\n' + b'3' * 0x20000 + b'\r\n'
                             + b'13\r\nEND OF TRANSMISSION\r\n'
                             + b'0\r\n\r\n'),
                 ResponseValidator(status=200)
                 )],
        }

        TESTS_10 = {
            "POST": [
                (RequestMsg("POST", "/POST/test_01_concurrent_requests_10",
                            headers={"Content-Type": "text/plain;charset=utf-8",
                                     "Content-Length": "393216"},
                            body=b'P' * 393197
                            + b'END OF TRANSMISSION'),
                 ResponseMsg(201, reason="Created",
                             headers={"Test-Header": "/POST/test_01_concurrent_requests_10",
                                      "Content-Length": "0"}),
                 ResponseValidator(status=201)
                 )],

            "GET": [
                (RequestMsg("GET", "/GET/test_01_concurrent_requests_10_small",
                            headers={"Content-Length": "000"}),
                 ResponseMsg(200, reason="OK",
                             # no content-length, server must close conn when done
                             headers={"Test-Header": "/GET/test_01_concurrent_requests_10_small",
                                      "Content-Type": "text/plain;charset=utf-8"},
                             body=b'END OF TRANSMISSION'),
                 ResponseValidator(status=200)),

                (RequestMsg("GET", "/GET/test_01_concurrent_requests_10",
                            headers={"Content-Length": "000"}),
                 ResponseMsg(200, reason="OK",
                             headers={"Test-Header": "/GET/test_01_concurrent_requests_10",
                                      "Content-Length": "393215",
                                      "Content-Type": "text/plain;charset=utf-8"},
                             body=b'G' * 393196
                             + b'END OF TRANSMISSION'),
                 ResponseValidator(status=200)
                 )],
        }

        server11 = TestServer.new_server(self.http_server11_port, self.http_listener11_port, TESTS_11)
        self.assertIsNotNone(server11, TEST_SERVER_ERROR % self.http_server11_port)
        server10 = TestServer.new_server(self.http_server10_port, self.http_listener10_port, TESTS_10,
                                         handler_cls=RequestHandler10)
        self.assertIsNotNone(server10, TEST_SERVER_ERROR % self.http_server10_port)
        self.EA2.wait_connectors()

        repeat_ct = 10
        client_ct = 4  # per version
        clients = []
        for _ in range(client_ct):
            clients.append(ThreadedTestClient(TESTS_11,
                                              self.http_listener11_port,
                                              repeat=repeat_ct))
            clients.append(ThreadedTestClient(TESTS_10,
                                              self.http_listener10_port,
                                              repeat=repeat_ct))
        for client in clients:
            client.wait()
            try:
                self.assertIsNone(client.error)
                self.assertEqual(repeat_ct * REQ_CT, client.count)
            except Exception:
                client.dump_log()
                raise

        server11.wait()
        self.assertEqual(client_ct * repeat_ct * REQ_CT,
                         server11.request_count)
        server10.wait()
        self.assertEqual(client_ct * repeat_ct * REQ_CT,
                         server10.request_count)

    def test_02_credit_replenish(self):
        """
        Verify credit is replenished by sending > the default credit window
        requests across the routers.  The default credit window is 250
        """

        TESTS = {
            "GET": [
                (RequestMsg("GET", "/GET/test_02_credit_replenish",
                            headers={"Content-Length": "000"}),
                 ResponseMsg(200, reason="OK",
                             headers={"Content-Length": "24",
                                      "Content-Type": "text/plain;charset=utf-8"},
                             body=b'test_02_credit_replenish'),
                 ResponseValidator(status=200),
                 ),
            ]
        }
        server = TestServer.new_server(self.http_server11_port, self.http_listener11_port, TESTS)
        try:
            self.assertIsNotNone(server, TEST_SERVER_ERROR % self.http_server11_port)

            self.EA2.wait_connectors()

            client = ThreadedTestClient(TESTS,
                                        self.http_listener11_port,
                                        repeat=300)
            client.wait()
            self.assertIsNone(client.error)
            self.assertEqual(300, client.count)
        finally:
            server.wait()

    def test_03_server_reconnect(self):
        """
        Verify server reconnect logic.
        """
        TESTS = {
            "GET": [
                (RequestMsg("GET", "/GET/test_03_server_reconnect",
                            headers={"Content-Length": "000"}),
                 ResponseMsg(200, reason="OK",
                             headers={"Content-Length": "24",
                                      "Content-Type": "text/plain;charset=utf-8"},
                             body=b'test_03_server_reconnect'),
                 ResponseValidator(status=200),
                 ),
            ]
        }

        # bring up the server and send some requests. This will cause the
        # router to grant credit for clients
        server = TestServer.new_server(self.http_server11_port, self.http_listener11_port, TESTS)
        self.assertIsNotNone(server, TEST_SERVER_ERROR % self.http_server11_port)
        self.EA2.wait_connectors()

        client = ThreadedTestClient(TESTS,
                                    self.http_listener11_port,
                                    repeat=2)
        client.wait()
        self.assertIsNone(client.error)
        self.assertEqual(2, client.count)

        # simulate server loss.  Fire up a client which should be granted
        # credit since the adaptor does not immediately teardown the server
        # links.  This will cause the adaptor to run qdr_connection_process
        # without a raw connection available to wake the I/O thread..
        server.wait()
        client = ThreadedTestClient(TESTS,
                                    self.http_listener11_port,
                                    repeat=2)
        # the adaptor will detach the links to the server if the connection
        # cannot be reestablished after 2.5 seconds.  Restart the server before
        # that occurrs to prevent client messages from being released with 503
        # status.
        server = TestServer.new_server(self.http_server11_port, self.http_listener11_port, TESTS)
        self.assertIsNotNone(server, TEST_SERVER_ERROR % self.http_server11_port)

        client.wait()
        self.assertIsNone(client.error)
        self.assertEqual(2, client.count)
        server.wait()

    def test_04_server_pining_for_the_fjords(self):
        """
        Test permanent loss of server
        """
        TESTS = {
            "GET": [
                (RequestMsg("GET", "/GET/test_04_fjord_pining",
                            headers={"Content-Length": "000"}),
                 ResponseMsg(200, reason="OK",
                             headers={"Content-Length": "20",
                                      "Content-Type": "text/plain;charset=utf-8"},
                             body=b'test_04_fjord_pining'),
                 ResponseValidator(status=200),
                 ),
            ]
        }

        # bring up the server and send some requests. This will cause the
        # router to grant credit for clients
        server = TestServer.new_server(self.http_server11_port, self.http_listener11_port, TESTS)
        self.assertIsNotNone(server, TEST_SERVER_ERROR % self.http_server11_port)

        self.EA2.wait_connectors()

        client = ThreadedTestClient(TESTS, self.http_listener11_port)
        client.wait()
        self.assertIsNone(client.error)
        self.assertEqual(1, client.count)

        TESTS_FAIL = {
            "GET": [
                (RequestMsg("GET", "/GET/test_04_fjord_pining",
                            headers={"Content-Length": "000"}),
                 ResponseMsg(200, reason="OK",
                             headers={"Content-Length": "20",
                                      "Content-Type": "text/plain;charset=utf-8"},
                             body=b'test_04_fjord_pining'),
                 ResponseValidator(status=503),
                 ),
            ]
        }

        # Kill the server then issue client requests. These requests will be
        # held on the server's outgoing links until they expire (2.5 seconds).
        # At that point the client will receive a 503 response.
        server.wait()
        client = ThreadedTestClient(TESTS_FAIL, self.http_listener11_port)
        client.wait()
        self.assertIsNone(client.error)
        self.assertEqual(1, client.count)

        # ensure links recover once the server re-appears
        server = TestServer.new_server(self.http_server11_port, self.http_listener11_port, TESTS)
        try:
            self.assertIsNotNone(server, TEST_SERVER_ERROR % self.http_server11_port)

            self.EA2.wait_connectors()

            client = ThreadedTestClient(TESTS, self.http_listener11_port)
            client.wait()
            self.assertIsNone(client.error)
            self.assertEqual(1, client.count)
        finally:
            server.wait()

    def test_05_large_streaming_msg(self):
        """
        Verify large streaming message transfer
        """
        TESTS_11 = {
            "PUT": [
                (RequestMsg("PUT", "/PUT/streaming_test_11",
                            headers={
                                "Transfer-encoding": "chunked",
                                "Content-Type": "text/plain;charset=utf-8"
                            },
                            # 4 chunks each ~= 600K
                            body=b'927C1\r\n' + b'0' * 0x927C0 + b'X\r\n'
                            + b'927C0\r\n' + b'1' * 0x927C0 + b'\r\n'
                            + b'927C1\r\n' + b'2' * 0x927C0 + b'X\r\n'
                            + b'927C0\r\n' + b'3' * 0x927C0 + b'\r\n'
                            + b'0\r\n\r\n'),

                 ResponseMsg(201, reason="Created",
                             headers={"Response-Header": "data",
                                      "Content-Length": "0"}),
                 ResponseValidator(status=201))
            ],

            "GET": [
                (RequestMsg("GET", "/GET/streaming_test_11",
                            headers={"Content-Length": "000"}),
                 ResponseMsg(200, reason="OK",
                             headers={
                                 "transfer-Encoding": "chunked",
                                 "Content-Type": "text/plain;charset=utf-8"
                             },
                             # two 1.2MB chunk
                             body=b'124f80\r\n' + b'4' * 0x124F80 + b'\r\n'
                             + b'124f80\r\n' + b'5' * 0x124F80 + b'\r\n'
                             + b'0\r\n\r\n'),
                 ResponseValidator(status=200))
            ],
        }

        TESTS_10 = {
            "POST": [
                (RequestMsg("POST", "/POST/streaming_test_10",
                            headers={"Header-1": "H" * 2048,
                                     "Content-Length": "2097155",
                                     "Content-Type": "text/plain;charset=utf-8"},
                            body=b'P' * 2097155),
                 ResponseMsg(201, reason="Created",
                             headers={"Response-Header": "data",
                                      "Content-Length": "0"}),
                 ResponseValidator(status=201))
            ],

            "GET": [
                (RequestMsg("GET", "/GET/streaming_test_10",
                            headers={"Content-Length": "000"}),
                 ResponseMsg(200, reason="OK",
                             headers={"Content-Length": "1999999",
                                      "Content-Type": "text/plain;charset=utf-8"},
                             body=b'G' * 1999999),
                 ResponseValidator(status=200))
            ],
        }
        server11 = TestServer.new_server(self.http_server11_port, self.http_listener11_port, TESTS_11)
        self.assertIsNotNone(server11, TEST_SERVER_ERROR % self.http_server11_port)
        server10 = TestServer.new_server(self.http_server10_port, self.http_listener10_port, TESTS_10,
                                         handler_cls=RequestHandler10)
        self.assertIsNotNone(server10, TEST_SERVER_ERROR % self.http_server10_port)

        self.EA2.wait_connectors()

        client11 = ThreadedTestClient(TESTS_11,
                                      self.http_listener11_port,
                                      repeat=2)
        client11.wait()
        self.assertIsNone(client11.error)
        self.assertEqual(4, client11.count)

        client10 = ThreadedTestClient(TESTS_10,
                                      self.http_listener10_port,
                                      repeat=2)
        client10.wait()
        self.assertIsNone(client10.error)
        self.assertEqual(4, client10.count)

        server11.wait()
        server10.wait()


class CommonHttp1OneRouterTest:
    TESTS_11 = {
        #
        # GET
        #
        "GET": [
            (RequestMsg("GET", "/GET/error",
                        headers={"Content-Length": 0}),
             ResponseMsg(400, reason="Bad breath", error=True),
             ResponseValidator(status=400)),

            (RequestMsg("GET", "/GET/no_content",
                        headers={"Content-Length": 0}),
             ResponseMsg(204, reason="No Content"),
             ResponseValidator(status=204)),

            (RequestMsg("GET", "/GET/content_len",
                        headers={"Content-Length": "00"}),
             ResponseMsg(200, reason="OK",
                         headers={"Content-Length": 1,
                                  "Content-Type": "text/plain;charset=utf-8"},
                         body=b'?'),
             ResponseValidator(expect_headers={'Content-Length': '1'},
                               expect_body=b'?')),

            (RequestMsg("GET", "/GET/content_len_511",
                        headers={"Content-Length": 0}),
             ResponseMsg(200, reason="OK",
                         headers={"Content-Length": 511,
                                  "Content-Type": "text/plain;charset=utf-8"},
                         body=b'X' * 511),
             ResponseValidator(expect_headers={'Content-Length': '511'},
                               expect_body=b'X' * 511)),

            (RequestMsg("GET", "/GET/content_len_4096",
                        headers={"Content-Length": 0}),
             ResponseMsg(200, reason="OK",
                         headers={"Content-Length": 4096,
                                  "Content-Type": "text/plain;charset=utf-8"},
                         body=b'X' * 4096),
             ResponseValidator(expect_headers={'Content-Length': '4096'},
                               expect_body=b'X' * 4096)),

            (RequestMsg("GET", "/GET/chunked",
                        headers={"Content-Length": 0}),
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

            (RequestMsg("GET", "/GET/chunked_large",
                        headers={"Content-Length": 0}),
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

            (RequestMsg("GET", "/GET/info_content_len",
                        headers={"Content-Length": 0}),
             [ResponseMsg(100, reason="Continue",
                          headers={"Blab": 1, "Blob": "?"}),
              ResponseMsg(200, reason="OK",
                          headers={"Content-Length": 1,
                                   "Content-Type": "text/plain;charset=utf-8"},
                          body=b'?')],
             ResponseValidator(expect_headers={'Content-Type': "text/plain;charset=utf-8"},
                               expect_body=b'?')),

            # (RequestMsg("GET", "/GET/no_length",
            #             headers={"Content-Length": "0"}),
            #  ResponseMsg(200, reason="OK",
            #              headers={"Content-Type": "text/plain;charset=utf-8",
            #                       "connection": "close"
            #              },
            #              body=b'Hi! ' * 1024 + b'X'),
            #  ResponseValidator(expect_body=b'Hi! ' * 1024 + b'X')),
        ],
        #
        # HEAD
        #
        "HEAD": [
            (RequestMsg("HEAD", "/HEAD/test_01",
                        headers={"Content-Length": "0"}),
             ResponseMsg(200, headers={"App-Header-1": "Value 01",
                                       "Content-Length": "10",
                                       "App-Header-2": "Value 02"},
                         body=None),
             ResponseValidator(expect_headers={"App-Header-1": "Value 01",
                                               "Content-Length": "10",
                                               "App-Header-2": "Value 02"})
             ),
            (RequestMsg("HEAD", "/HEAD/test_02",
                        headers={"Content-Length": "0"}),
             ResponseMsg(200, headers={"App-Header-1": "Value 01",
                                       "Transfer-Encoding": "chunked",
                                       "App-Header-2": "Value 02"}),
             ResponseValidator(expect_headers={"App-Header-1": "Value 01",
                                               "Transfer-Encoding": "chunked",
                                               "App-Header-2": "Value 02"})),

            (RequestMsg("HEAD", "/HEAD/test_03",
                        headers={"Content-Length": "0"}),
             ResponseMsg(200, headers={"App-Header-3": "Value 03"}),
             ResponseValidator(expect_headers={"App-Header-3": "Value 03"})),
        ],
        #
        # POST
        #
        "POST": [
            (RequestMsg("POST", "/POST/test_01",
                        headers={"App-Header-1": "Value 01",
                                 "Content-Length": "19",
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
                         body=b'Hi There!'),
             ResponseValidator(expect_body=b'Hi There!')
             ),
        ],
        #
        # PUT
        #
        "PUT": [
            (RequestMsg("PUT", "/PUT/test_01",
                        headers={"Put-Header-1": "Value 01",
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
                                  "Transfer-Encoding": "chunked"},
                         body=b'1\r\n$\r\n0\r\n\r\n'),
             ResponseValidator(status=201, expect_body=b'$')
             ),
        ]
    }

    # HTTP/1.0 compliant test cases (no chunked, response length unspecified)
    TESTS_10 = {
        #
        # GET
        #
        "GET": [
            (RequestMsg("GET", "/GET/error",
                        headers={"Content-Length": 0}),
             ResponseMsg(400, reason="Bad breath", error=True),
             ResponseValidator(status=400)),

            (RequestMsg("GET", "/GET/no_content",
                        headers={"Content-Length": 0}),
             ResponseMsg(204, reason="No Content"),
             ResponseValidator(status=204)),

            (RequestMsg("GET", "/GET/content_len_511",
                        headers={"Content-Length": 0}),
             ResponseMsg(200, reason="OK",
                         headers={"Content-Length": 511,
                                  "Content-Type": "text/plain;charset=utf-8"},
                         body=b'X' * 511),
             ResponseValidator(expect_headers={'Content-Length': '511'},
                               expect_body=b'X' * 511)),

            (RequestMsg("GET", "/GET/content_len_4096",
                        headers={"Content-Length": 0}),
             ResponseMsg(200, reason="OK",
                         headers={"Content-Type": "text/plain;charset=utf-8"},
                         body=b'X' * 4096),
             ResponseValidator(expect_headers={"Content-Type": "text/plain;charset=utf-8"},
                               expect_body=b'X' * 4096)),

            (RequestMsg("GET", "/GET/info_content_len",
                        headers={"Content-Length": 0}),
             ResponseMsg(200, reason="OK",
                         headers={"Content-Type": "text/plain;charset=utf-8"},
                         body=b'?'),
             ResponseValidator(expect_headers={'Content-Type': "text/plain;charset=utf-8"},
                               expect_body=b'?')),

            # test support for "folded headers"

            (RequestMsg("GET", "/GET/folded_header_01",
                        headers={"Content-Length": 0}),
             ResponseMsg(200, reason="OK",
                         headers={"Content-Type": "text/plain;charset=utf-8",
                                  "Content-Length": 1,
                                  "folded-header": "One\r\n \r\n\tTwo"},
                         body=b'X'),
             ResponseValidator(expect_headers={"Content-Type":
                                               "text/plain;charset=utf-8",
                                               "folded-header":
                                               "One     \tTwo"},
                               expect_body=b'X')),

            (RequestMsg("GET", "/GET/folded_header_02",
                        headers={"Content-Length": 0}),
             ResponseMsg(200, reason="OK",
                         headers={"Content-Type": "text/plain;charset=utf-8",
                                  "Content-Length": 1,
                                  "folded-header": "\r\n \r\n\tTwo",
                                  "another-header": "three"},
                         body=b'X'),
             ResponseValidator(expect_headers={"Content-Type":
                                               "text/plain;charset=utf-8",
                                               # trim leading and
                                               # trailing ws:
                                               "folded-header":
                                               "Two",
                                               "another-header":
                                               "three"},
                               expect_body=b'X')),
        ],
        #
        # HEAD
        #
        "HEAD": [
            (RequestMsg("HEAD", "/HEAD/test_01",
                        headers={"Content-Length": "0"}),
             ResponseMsg(200, headers={"App-Header-1": "Value 01",
                                       "Content-Length": "10",
                                       "App-Header-2": "Value 02"},
                         body=None),
             ResponseValidator(expect_headers={"App-Header-1": "Value 01",
                                               "Content-Length": "10",
                                               "App-Header-2": "Value 02"})
             ),

            (RequestMsg("HEAD", "/HEAD/test_03",
                        headers={"Content-Length": "0"}),
             ResponseMsg(200, headers={"App-Header-3": "Value 03"}),
             ResponseValidator(expect_headers={"App-Header-3": "Value 03"})),
        ],
        #
        # POST
        #
        "POST": [
            (RequestMsg("POST", "/POST/test_01",
                        headers={"App-Header-1": "Value 01",
                                 "Content-Length": "19",
                                 "Content-Type": "application/x-www-form-urlencoded"},
                        body=b'one=1&two=2&three=3'),
             ResponseMsg(200, reason="OK",
                         headers={"Response-Header": "whatever"},
                         body=b'12345678abcdefghijklmno'),
             ResponseValidator(expect_body=b'12345678abcdefghijklmno')
             ),
            (RequestMsg("POST", "/POST/test_02",
                        headers={"App-Header-1": "Value 01",
                                 "Content-Length": "5"},
                        body=b'01234'),
             ResponseMsg(200, reason="OK",
                         headers={"Response-Header": "whatever",
                                  "Content-Length": "9"},
                         body=b'Hi There!'),
             ResponseValidator(expect_body=b'Hi There!')
             ),
        ],
        #
        # PUT
        #
        "PUT": [
            (RequestMsg("PUT", "/PUT/test_01",
                        headers={"Put-Header-1": "Value 01",
                                 "Content-Length": "513",
                                 "Content-Type": "text/plain;charset=utf-8"},
                        body=b'$' * 513),
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
                         headers={"Response-Header": "whatever"},
                         body=b'No Content Length'),
             ResponseValidator(status=201, expect_body=b'No Content Length')
             ),
        ]
    }

    def _do_request(self, client, tests):
        for req, _, val in tests:
            req.send_request(client)
            rsp = client.getresponse()
            try:
                body = val.check_response(rsp)
            except Exception as exc:
                self.fail("request failed:  %s" % str(exc))

            if req.method == "BODY":
                self.assertEqual(b'', body)

    def test_001_get(self):
        client = HTTPConnection("127.0.0.1:%s" % self.http_listener11_port,
                                timeout=TIMEOUT)
        self._do_request(client, self.TESTS_11["GET"])
        client.close()

    def test_002_head(self):
        client = HTTPConnection("127.0.0.1:%s" % self.http_listener11_port,
                                timeout=TIMEOUT)
        self._do_request(client, self.TESTS_11["HEAD"])
        client.close()

    def test_003_post(self):
        client = HTTPConnection("127.0.0.1:%s" % self.http_listener11_port,
                                timeout=TIMEOUT)
        self._do_request(client, self.TESTS_11["POST"])
        client.close()

    def test_004_put(self):
        client = HTTPConnection("127.0.0.1:%s" % self.http_listener11_port,
                                timeout=TIMEOUT)
        self._do_request(client, self.TESTS_11["PUT"])
        client.close()

    def test_006_head_10(self):
        client = HTTPConnection("127.0.0.1:%s" % self.http_listener10_port,
                                timeout=TIMEOUT)
        self._do_request(client, self.TESTS_10["HEAD"])
        client.close()

    def test_007_post_10(self):
        client = HTTPConnection("127.0.0.1:%s" % self.http_listener10_port,
                                timeout=TIMEOUT)
        self._do_request(client, self.TESTS_10["POST"])
        client.close()

    def test_008_put_10(self):
        client = HTTPConnection("127.0.0.1:%s" % self.http_listener10_port,
                                timeout=TIMEOUT)
        self._do_request(client, self.TESTS_10["PUT"])
        client.close()


class Http1OneRouterTestBase(TestCase):
    # HTTP/1.1 compliant test cases

    @classmethod
    def router(cls, name, mode, extra):
        config = [
            ('router', {'mode': mode,
                        'id': name,
                        'allowUnsettledMulticast': 'yes'}),
            ('listener', {'role': 'normal',
                          'port': cls.tester.get_port()}),
            ('address', {'prefix': 'closest', 'distribution': 'closest'}),
            ('address',
             {'prefix': 'multicast', 'distribution': 'multicast'}),
        ]

        if extra:
            config.extend(extra)
        config = Qdrouterd.Config(config)
        cls.routers.append(cls.tester.qdrouterd(name, config, wait=True))
        return cls.routers[-1]

    @classmethod
    def setUpClass(cls):
        """Start a router"""
        super(Http1OneRouterTestBase, cls).setUpClass()

        cls.http_server11_port = cls.tester.get_port()
        cls.http_server10_port = cls.tester.get_port()
        cls.http_listener11_port = cls.tester.get_port()
        cls.http_listener10_port = cls.tester.get_port()


class Http1Edge2EdgeTestBase(TestCase):
    @classmethod
    def router(cls, name, mode, extra):
        config = [
            ('router', {'mode': mode,
                        'id': name,
                        'allowUnsettledMulticast': 'yes'}),
            ('listener', {'role': 'normal',
                          'port': cls.tester.get_port()}),
            ('address', {'prefix': 'closest', 'distribution': 'closest'}),
            ('address', {'prefix': 'multicast', 'distribution': 'multicast'}),
        ]

        if extra:
            config.extend(extra)
        config = Qdrouterd.Config(config)
        cls.routers.append(cls.tester.qdrouterd(name, config, wait=True))
        return cls.routers[-1]

    @classmethod
    def setUpClass(cls):
        """Start a router"""
        super(Http1Edge2EdgeTestBase, cls).setUpClass()
        cls.routers = []
        cls.INTA_edge1_port   = cls.tester.get_port()
        cls.INTA_edge2_port   = cls.tester.get_port()
        cls.http_server11_port = cls.tester.get_port()
        cls.http_listener11_port = cls.tester.get_port()
        cls.http_server10_port = cls.tester.get_port()
        cls.http_listener10_port = cls.tester.get_port()


class Http1ClientCloseTestsMixIn:
    """
    Generic test functions for simulating HTTP/1.x client connection drops.
    """
    def client_request_close_test(self, server_port, client_port, server_mgmt):
        """
        Simulate an HTTP client drop while sending a very large PUT request
        """
        PING = {
            "GET": [
                (RequestMsg("GET", "/GET/test_04_client_request_close/ping",
                            headers={"Content-Length": "0"}),
                 ResponseMsg(200, reason="OK",
                             headers={
                                 "Content-Length": "19",
                                 "Content-Type": "text/plain;charset=utf-8",
                             },
                             body=b'END OF TRANSMISSION'),
                 ResponseValidator(status=200)
                 )]
        }

        TESTS = {
            "PUT": [
                (RequestMsg("PUT", "/PUT/test_04_client_request_close",
                            headers={
                                "Content-Length": "500000",
                                "Content-Type": "text/plain;charset=utf-8"
                            },
                            body=b'4' * (500000 - 19) + b'END OF TRANSMISSION'),
                 ResponseMsg(201, reason="Created",
                             headers={"Test-Header": "/PUT/test_04_client_request_close",
                                      "Content-Length": "0"}),
                 ResponseValidator(status=201)
                 )]
        }
        TESTS.update(PING)

        server = TestServer(server_port=server_port,
                            client_port=client_port,
                            tests=TESTS)
        #
        # ensure the server has fully connected
        #
        client = ThreadedTestClient(PING, client_port)
        client.wait()

        #
        # Simulate an HTTP client that dies during the sending of the PUT
        # request
        #

        fake_request = b'PUT /PUT/test_04_client_request_close HTTP/1.1\r\n' \
            + b'Content-Length: 500000\r\n' \
            + b'Content-Type: text/plain;charset=utf-8\r\n' \
            + b'\r\n' \
            + b'?' * 50000
        fake_client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        fake_client.settimeout(5)
        fake_client.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        fake_client.connect(("127.0.0.1", client_port))
        fake_client.sendall(fake_request, socket.MSG_WAITALL)
        fake_client.close()

        # since socket I/O is asynchronous wait until the request arrives
        # at the server

        expected = len(fake_request)
        bytes_in = 0
        while expected > bytes_in:
            ri = server_mgmt.query(type="org.apache.qpid.dispatch.httpRequestInfo").get_entities()
            bytes_in = ri[-1]['bytesIn'] if ri else 0  # most recent request at tail
            sleep(0.1)

        # now ensure the connection between the router and the HTTP server
        # still functions:
        client = ThreadedTestClient(PING, client_port)
        client.wait()
        server.wait()

    def client_response_close_test(self, server_port, client_port):
        """
        Simulate an HTTP client drop while the server is sending a very large
        response message.
        """
        PING = {
            "PUT": [
                (RequestMsg("PUT", "/PUT/test_05_client_response_close/ping",
                            headers={"Content-Length": "1",
                                     "content-type":
                                     "text/plain;charset=utf-8"},
                            body=b'X'),
                 ResponseMsg(201, reason="Created",
                             headers={"Content-Length": "0"}),
                 ResponseValidator(status=201)
                 )]
        }

        big_headers = dict([('Huge%s' % i, chr(ord(b'0') + i) * 8000)
                            for i in range(10)])

        TESTS = {
            "GET": [
                (RequestMsg("GET", "/GET/test_05_client_response_close",
                            headers={
                                "Content-Length": "0",
                                "Content-Type": "text/plain;charset=utf-8"
                            }),
                 [ResponseMsg(100, reason="Continue", headers=big_headers),
                  ResponseMsg(100, reason="Continue", headers=big_headers),
                  ResponseMsg(100, reason="Continue", headers=big_headers),
                  ResponseMsg(100, reason="Continue", headers=big_headers),
                  ResponseMsg(200,
                              reason="OK",
                              headers={"Content-Length": 1000000,
                                       "Content-Type": "text/plain;charset=utf-8"},
                              body=b'?' * 1000000)],
                 ResponseValidator(status=200)
                 )]
        }
        TESTS.update(PING)

        server = TestServer(server_port=server_port,
                            client_port=client_port,
                            tests=TESTS)
        #
        # ensure the server has fully connected
        #
        client = ThreadedTestClient(PING, client_port)
        client.wait()

        #
        # Simulate an HTTP client that dies during the receipt of the
        # response
        #

        fake_request = b'GET /GET/test_05_client_response_close HTTP/1.1\r\n' \
            + b'Content-Length: 0\r\n' \
            + b'Content-Type: text/plain;charset=utf-8\r\n' \
            + b'\r\n'
        fake_client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        fake_client.settimeout(TIMEOUT)
        fake_client.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        fake_client.connect(("127.0.0.1", client_port))
        fake_client.sendall(fake_request, socket.MSG_WAITALL)
        fake_client.recv(1)
        fake_client.close()

        #
        # Verify the server is still reachable
        #
        client = ThreadedTestClient(PING, client_port)
        client.wait()
        server.wait()


class Http1CurlTestsMixIn:
    """
    Test cases using curl as the command line client
    """

    @unittest.skipIf(not _curl_ok(),
                     "required curl version >= %s" % str(CURL_VERSION))
    def curl_get_test(self, host, port, server_port):
        """
        Use curl to get a resource
        """
        CURL_TESTS = {
            "GET": [
                (RequestMsg("GET", "/GET/curl_get"),
                 ResponseMsg(200, reason="OK",
                             headers={
                                 "Content-Length": "19",
                                 "Content-Type": "text/plain;charset=utf-8",
                                 "Test-Header": "/GET/curl_get"
                             },
                             body=b'END OF TRANSMISSION'),
                 ResponseValidator())
            ],

            "HEAD": [
                (RequestMsg("HEAD", "/HEAD/curl_head",
                            headers={"Content-Length": "0"}),
                 ResponseMsg(200, headers={"App-Header-1": "Value 01",
                                           "Content-Length": "10",
                                           "App-Header-2": "Value 02"},
                             body=None),
                 ResponseValidator())
            ]
        }

        server = TestServer.new_server(server_port, port, CURL_TESTS)
        self.assertIsNotNone(server, TEST_SERVER_ERROR % server_port)

        get_url = "http://%s:%s/GET/curl_get" % (host, port)
        head_url = "http://%s:%s/HEAD/curl_head" % (host, port)

        try:
            status, out, err = run_curl(["--http1.1", "-G", get_url])
            self.assertEqual(0, status, "curl error '%s' '%s'" % (out, err))
            self.assertIn("END OF TRANSMISSION", out, "Unexpected out=%s (err=%s)"
                          % (out, err))

            status, out, err = run_curl(["--http1.1", "-I", head_url])
            self.assertEqual(0, status, "curl error '%s' '%s'" % (out, err))
            self.assertIn("App-Header-2", out, "Unexpected out=%s (err=%s)"
                          % (out, err))

            status, out, err = run_curl(["--http1.0", "-G", get_url])
            self.assertEqual(0, status, "curl error '%s' '%s'" % (out, err))
            self.assertIn("END OF TRANSMISSION", out, "Unexpected out=%s (err=%s)"
                          % (out, err))

            status, out, err = run_curl(["--http1.1", "-G", get_url])
            self.assertEqual(0, status, "curl error '%s' '%s'" % (out, err))
            self.assertIn("END OF TRANSMISSION", out, "Unexpected out=%s (err=%s)"
                          % (out, err))

        finally:
            server.wait()

    @unittest.skipIf(not _curl_ok(),
                     "required curl version >= %s" % str(CURL_VERSION))
    def curl_put_test(self, host, port, server_port):
        """
        Use curl to PUT a resource
        """

        CURL_TESTS = {
            "PUT": [
                (RequestMsg("PUT", "/PUT/curl_put"),
                 ResponseMsg(201, reason="Created",
                             headers={
                                 "Test-Header": "/PUT/curl_put",
                                 "content-length": "0"
                             }),
                 ResponseValidator())
            ],

            "HEAD": [
                (RequestMsg("HEAD", "/HEAD/curl_head",
                            headers={"Content-Length": "0"}),
                 ResponseMsg(200, headers={"App-Header-1": "Value 01",
                                           "Content-Length": "10",
                                           "App-Header-2": "Value 02"},
                             body=None),
                 ResponseValidator())
            ]
        }

        server = TestServer.new_server(server_port, port, CURL_TESTS)
        self.assertIsNotNone(server, TEST_SERVER_ERROR % server_port)

        put_url = "http://%s:%s/PUT/curl_put" % (host, port)
        head_url = "http://%s:%s/HEAD/curl_head" % (host, port)

        try:
            status, out, err = run_curl(["--http1.1", "-T", ".", put_url],
                                        input="Mary had a little pug."
                                        "\nIts fleece was brown as dirt."
                                        "\nIts color made Mary shrug."
                                        "\nShe should dress it in a shirt.")
            self.assertEqual(0, status, "curl error '%s' '%s'" % (out, err))

            status, out, err = run_curl(["--http1.1", "-I", head_url])
            self.assertEqual(0, status, "curl error '%s' '%s'" % (out, err))
            self.assertIn("App-Header-2", out, "Unexpected out=%s (err=%s)"
                          % (out, err))

            status, out, err = run_curl(["--http1.1", "-T", ".", put_url],
                                        input="Ph'nglui mglw'nafh Cthulhu"
                                        "\nR'lyeh wgah'nagl fhtagn")
            self.assertEqual(0, status, "curl error '%s' '%s'" % (out, err))

        finally:
            server.wait()

    @unittest.skipIf(not _curl_ok(),
                     "required curl version >= %s" % str(CURL_VERSION))
    def curl_post_test(self, host, port, server_port):
        """
        Use curl to post to a resource
        """

        CURL_TESTS = {
            "POST": [
                (RequestMsg("POST", "/POST/curl_post"),
                 ResponseMsg(201, reason="Created",
                             headers={
                                 "Test-Header": "/POST/curl_put",
                                 "content-length": "19",
                                 "Content-Type": "text/plain;charset=utf-8",
                             },
                             body=b'END OF TRANSMISSION'),
                 ResponseValidator())
            ],

            "GET": [
                (RequestMsg("GET", "/GET/curl_get",
                            headers={"Content-Length": "0"}),
                 ResponseMsg(200, reason="OK",
                             headers={"App-Header-1": "Value 01",
                                      "Content-Length": "10",
                                      "App-Header-2": "Value 02"},
                             body=b'0123456789'),
                 ResponseValidator())
            ]
        }

        server = TestServer.new_server(server_port, port, CURL_TESTS)
        self.assertIsNotNone(server, TEST_SERVER_ERROR % server_port)

        post_url = "http://%s:%s/POST/curl_post" % (host, port)
        get_url = "http://%s:%s/GET/curl_get" % (host, port)

        try:
            status, out, err = run_curl(["--http1.1", "-F", "name=Skupper",
                                         "-F", "breed=Pug", post_url])
            self.assertEqual(0, status, "curl error '%s' '%s'" % (out, err))
            self.assertIn("END OF TRANSMISSION", out, "Unexpected out=%s (err=%s)"
                          % (out, err))

            status, out, err = run_curl(["--http1.1", "-G", get_url])
            self.assertEqual(0, status, "curl error '%s' '%s'" % (out, err))
            self.assertIn("0123456789", out, "Unexpected out=%s (err=%s)"
                          % (out, err))

            status, out, err = run_curl(["--http1.1", "-F", "name=Coco",
                                         "-F", "breed=French Bulldog",
                                         post_url])
            self.assertEqual(0, status, "curl error '%s' '%s'" % (out, err))
            self.assertIn("END OF TRANSMISSION", out, "Unexpected out=%s (err=%s)"
                          % (out, err))
        finally:
            server.wait()

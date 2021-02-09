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

import os, sys
from time import sleep
import system_test
from system_test import TestCase, Qdrouterd, QdManager, Process, SkipIfNeeded
from subprocess import PIPE


def python_37_available():
    if sys.version_info >= (3, 7):
        return True

def curl_available():
    popen_args = ['curl', '--version']
    try:
        process = Process(popen_args,
                          name='curl_check',
                          stdout=PIPE,
                          expect=None,
                          universal_newlines=True)
        out = process.communicate()[0]
        return True
    except:
        return False

def quart_available():
    """
    Checks if quart version is greater than 0.13
    """
    popen_args = ['quart', '--version']
    try:
        process = Process(popen_args,
                          name='quart_check',
                          stdout=PIPE,
                          expect=None,
                          universal_newlines=True)
        out = process.communicate()[0]
        parts = out.split(".")
        major_version = parts[0]
        if int(major_version[-1]) > 0 or int(parts[1]) >= 13:
            return True
        return False
    except Exception as e:
        print (e)
        print("quart_not_available")
        return False

def skip_test():
    if python_37_available() and quart_available() and curl_available():
        return False
    return True

class Http2TestBase(TestCase):
    def run_curl(self, args=None, regexp=None, timeout=system_test.TIMEOUT, address=None):
        # Tell with -m / --max-time the maximum time, in seconds, that you
        # allow the command line to spend before curl exits with a
        # timeout error code (28).
        local_args = ["--http2-prior-knowledge"]
        if args:
            local_args =  args + ["--http2-prior-knowledge"]

        popen_args = ['curl',
                      str(address),
                      '--max-time', str(timeout)] + local_args
        p = self.popen(popen_args,
                       name='curl-' + self.id(), stdout=PIPE, expect=None,
                       universal_newlines=True)

        out = p.communicate()[0]
        assert (p.returncode == 0)
        return out


class CommonHttp2Tests():
    """
    Common Base class containing all tests. These tests are run by all
    topologies of routers.
    """
    @SkipIfNeeded(skip_test(), "Python 3.7 or greater, Quart 0.13.0 or greater and curl needed to run http2 tests")
    # Tests the HTTP2 head request
    def test_head_request(self):
        # Run curl 127.0.0.1:port --http2-prior-knowledge --head
        address = self.router_qdra.http_addresses[0]
        out = self.run_curl(args=["--head"], address=address)
        self.assertIn('HTTP/2 200', out)
        self.assertIn('server: hypercorn-h2', out)
        self.assertIn('content-type: text/html; charset=utf-8', out)

    @SkipIfNeeded(skip_test(), "Python 3.7 or greater, Quart 0.13.0 or greater and curl needed to run http2 tests")
    def test_get_request(self):
        # Run curl 127.0.0.1:port --http2-prior-knowledge
        address = self.router_qdra.http_addresses[0]
        out = self.run_curl(address=address)
        i = 0
        ret_string = ""
        while (i < 1000):
            ret_string += str(i) + ","
            i += 1
        self.assertIn(ret_string, out)

    #@SkipIfNeeded(skip_test(), "Python 3.7 or greater, Quart 0.13.0 or greater and curl needed to run http2 tests")
    #def test_large_get_request(self):
        # Tests a large get request. Response is more than 50k which means it
        # will span many qd_http2_buffer_t objects.
        # Run curl 127.0.0.1:port/largeget --http2-prior-knowledge
    #    address = self.router_qdra.http_addresses[0] + "/largeget"
    #    out = self.run_curl(address=address)
    #    self.assertIn("49996,49997,49998,49999", out)

    @SkipIfNeeded(skip_test(), "Python 3.7 or greater, Quart 0.13.0 or greater and curl needed to run http2 tests")
    def test_post_request(self):
        # curl -d "fname=John&lname=Doe" -X POST 127.0.0.1:9000/myinfo --http2-prior-knowledge
        address = self.router_qdra.http_addresses[0] + "/myinfo"
        out = self.run_curl(args=['-d', 'fname=John&lname=Doe', '-X', 'POST'], address=address)
        self.assertIn('Success! Your first name is John, last name is Doe', out)

    @SkipIfNeeded(skip_test(), "Python 3.7 or greater, Quart 0.13.0 or greater and curl needed to run http2 tests")
    def test_delete_request(self):
        # curl -X DELETE "http://127.0.0.1:9000/myinfo/delete/22122" -H  "accept: application/json" --http2-prior-knowledge
        address = self.router_qdra.http_addresses[0] + "/myinfo/delete/22122"
        out = self.run_curl(args=['-X', 'DELETE'], address=address)
        self.assertIn('{"fname": "John", "lname": "Doe", "id": "22122"}', out)


    @SkipIfNeeded(skip_test(), "Python 3.7 or greater, Quart 0.13.0 or greater and curl needed to run http2 tests")
    def test_put_request(self):
        # curl -d "fname=John&lname=Doe" -X PUT 127.0.0.1:9000/myinfo --http2-prior-knowledge
        address = self.router_qdra.http_addresses[0] + "/myinfo"
        out = self.run_curl(args=['-d', 'fname=John&lname=Doe', '-X', 'PUT'], address=address)
        self.assertIn('Success! Your first name is John, last name is Doe', out)

    @SkipIfNeeded(skip_test(), "Python 3.7 or greater, Quart 0.13.0 or greater and curl needed to run http2 tests")
    def test_patch_request(self):
        # curl -d "fname=John&lname=Doe" -X PATCH 127.0.0.1:9000/myinfo --http2-prior-knowledge
        address = self.router_qdra.http_addresses[0] + "/patch"
        out = self.run_curl(args=['--data', '{\"op\":\"add\",\"path\":\"/user\",\"value\":\"jane\"}', '-X', 'PATCH'], address=address)
        self.assertIn('"op":"add"', out)

    @SkipIfNeeded(skip_test(), "Python 3.7 or greater, Quart 0.13.0 or greater and curl needed to run http2 tests")
    def test_404(self):
        # Run curl 127.0.0.1:port/unavilable --http2-prior-knowledge
        address = self.router_qdra.http_addresses[0] + "/unavilable"
        out = self.run_curl(address=address)
        self.assertIn('404 Not Found', out)

    @SkipIfNeeded(skip_test(), "Python 3.7 or greater, Quart 0.13.0 or greater and curl needed to run http2 tests")
    def test_500(self):
        # Run curl 127.0.0.1:port/unavilable --http2-prior-knowledge
        address = self.router_qdra.http_addresses[0] + "/test/500"
        out = self.run_curl(address=address)
        self.assertIn('500 Internal Server Error', out)

    @SkipIfNeeded(skip_test(), "Python 3.7 or greater, Quart 0.13.0 or greater and curl needed to run http2 tests")
    def test_get_image_png(self):
        # Run curl 127.0.0.1:port --http2-prior-knowledge
        passed = False
        try:
            address = self.router_qdra.http_addresses[0] + "/images/balanced-routing.png"
            self.run_curl(address=address)
        except UnicodeDecodeError as u:
            if "codec can't decode byte 0x89" in str(u):
                passed = True
        self.assertTrue(passed)

    @SkipIfNeeded(skip_test(), "Python 3.7 or greater, Quart 0.13.0 or greater and curl needed to run http2 tests")
    def test_get_image_jpg(self):
        # Run curl 127.0.0.1:port --http2-prior-knowledge
        passed = False
        try:
            address = self.router_qdra.http_addresses[0] + "/images/apache.jpg"
            self.run_curl(address=address)
        except UnicodeDecodeError as u:
            print (u)
            if "codec can't decode byte 0xff" in str(u):
                passed = True
        self.assertTrue(passed)

    def check_connector_delete(self, client_addr, server_addr):
        # Run curl 127.0.0.1:port --http2-prior-knowledge
        # We are first making sure that the http request goes thru successfully.
        out = self.run_curl(address=client_addr)

        # Run a qdmanage query on connections to see how many qdr_connections are
        # there on the egress router
        qd_manager = QdManager(self, address=server_addr)

        connections = qd_manager.query('org.apache.qpid.dispatch.connection')

        self.assertGreaterEqual(len(connections), 2)

        server_conn_found = False
        for conn in connections:
            if os.environ['SERVER_LISTEN_PORT'] in conn['name']:
                server_conn_found = True
                break
        self.assertTrue(server_conn_found)

        # Run a qdmanage DELETE on the httpConnector
        http_connectors  = qd_manager.query('org.apache.qpid.dispatch.httpConnector')
        self.assertEqual(len(http_connectors), 1)

        # Delete the httpConnector
        qd_manager.delete("org.apache.qpid.dispatch.httpConnector", name=self.connector_name)

        # Make sure the connector is gone
        http_connectors  = qd_manager.query('org.apache.qpid.dispatch.httpConnector')
        self.assertEqual(len(http_connectors), 0)

        # Deleting the connector must have taken out the connection to the server.
        connections = qd_manager.query('org.apache.qpid.dispatch.connection')
        http_server_conn_found = False
        for conn in connections:
            if os.environ['SERVER_LISTEN_PORT'] in conn['name']:
                server_conn_found = True
                break
        self.assertFalse(http_server_conn_found)

        sleep(2)

        #Now, run a curl client GET request with a timeout
        request_timed_out = False
        try:
            out = self.run_curl(address=client_addr, timeout=5)
            print (out)
        except Exception as e:
            request_timed_out = True

        self.assertTrue(request_timed_out)

        # Add back the httpConnector
        # qdmanage CREATE type=httpConnector address=examples.com host=127.0.0.1 port=80 protocolVersion=HTTP2
        create_result = qd_manager.create("org.apache.qpid.dispatch.httpConnector", self.connector_props)
        num_tries = 2
        tries = 0
        conn_present = False
        while tries < num_tries:
            connections = qd_manager.query('org.apache.qpid.dispatch.connection')
            tries += 1
            if (len(connections) < 2):
                sleep(2)
            else:
                conn_present = True
        self.assertTrue(conn_present)

        out = self.run_curl(address=client_addr)
        ret_string = ""
        i = 0
        while (i < 1000):
            ret_string += str(i) + ","
            i += 1
        self.assertIn(ret_string, out)

class Http2TestOneStandaloneRouter(Http2TestBase, CommonHttp2Tests):
    @classmethod
    def setUpClass(cls):
        super(Http2TestOneStandaloneRouter, cls).setUpClass()
        if skip_test():
            return
        cls.http2_server_name = "http2_server"
        os.environ["QUART_APP"] = "http2server:app"
        os.environ['SERVER_LISTEN_PORT'] = str(cls.tester.get_port())
        cls.http2_server = cls.tester.http2server(name=cls.http2_server_name,
                                                  listen_port=int(os.getenv('SERVER_LISTEN_PORT')),
                                                  py_string='python3',
                                                  server_file="http2_server.py")
        name = "http2-test-standalone-router"
        cls.connector_name = 'connectorToBeDeleted'
        cls.connector_props = {
            'port': os.getenv('SERVER_LISTEN_PORT'),
            'address': 'examples',
            'host': '127.0.0.1',
            'protocolVersion': 'HTTP2',
            'name': cls.connector_name
        }
        config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'QDR'}),
            ('listener', {'port': cls.tester.get_port(), 'role': 'normal', 'host': '0.0.0.0'}),

            ('httpListener', {'port': cls.tester.get_port(), 'address': 'examples',
                              'host': '127.0.0.1', 'protocolVersion': 'HTTP2'}),
            ('httpConnector', cls.connector_props)
        ])
        cls.router_qdra = cls.tester.qdrouterd(name, config, wait=True)

    @SkipIfNeeded(skip_test(), "Python 3.7 or greater, Quart 0.13.0 or greater and curl needed to run http2 tests")
    def test_zzz_http_connector_delete(self):
        self.check_connector_delete(client_addr=self.router_qdra.http_addresses[0],
                                    server_addr=self.router_qdra.addresses[0])


    @SkipIfNeeded(skip_test(), "Python 3.7 or greater, Quart 0.13.0 or greater and curl needed to run http2 tests")
    def test_000_stats(self):
        # Run curl 127.0.0.1:port --http2-prior-knowledge
        address = self.router_qdra.http_addresses[0]
        qd_manager = QdManager(self, address=self.router_qdra.addresses[0])

        # First request
        out = self.run_curl(address=address)

        # Second request
        address = self.router_qdra.http_addresses[0] + "/myinfo"
        out = self.run_curl(args=['-d', 'fname=Mickey&lname=Mouse', '-X', 'POST'], address=address)
        self.assertIn('Success! Your first name is Mickey, last name is Mouse', out)

        stats = qd_manager.query('org.apache.qpid.dispatch.httpRequestInfo')
        self.assertEqual(len(stats), 2)

        # Give time for the core thread to augment the stats.
        i = 0
        while i < 3:
            if not stats or stats[0].get('requests') < 2:
                i += 1
                sleep(1)
                stats = qd_manager.query('org.apache.qpid.dispatch.httpRequestInfo')
            else:
                break

        for s in stats:
            self.assertEqual(s.get('requests'), 2)
            self.assertEqual(s.get('details').get('GET:200'), 1)
            self.assertEqual(s.get('details').get('POST:200'), 1)
        if stats[0].get('direction') == 'out':
            self.assertEqual(stats[1].get('direction'), 'in')
            self.assertEqual(stats[0].get('bytesOut'), 24)
            self.assertEqual(stats[0].get('bytesIn'), 3944)
            self.assertEqual(stats[1].get('bytesOut'), 3944)
            self.assertEqual(stats[1].get('bytesIn'), 24)
        else:
            self.assertEqual(stats[0].get('direction'), 'in')
            self.assertEqual(stats[1].get('direction'), 'out')
            self.assertEqual(stats[0].get('bytesOut'), 3944)
            self.assertEqual(stats[0].get('bytesIn'), 24)
            self.assertEqual(stats[1].get('bytesOut'), 24)
            self.assertEqual(stats[1].get('bytesIn'), 3944)

class Http2TestOneEdgeRouter(Http2TestBase, CommonHttp2Tests):
    @classmethod
    def setUpClass(cls):
        super(Http2TestOneEdgeRouter, cls).setUpClass()
        if skip_test():
            return
        cls.http2_server_name = "http2_server"
        os.environ["QUART_APP"] = "http2server:app"
        os.environ['SERVER_LISTEN_PORT'] = str(cls.tester.get_port())
        cls.http2_server = cls.tester.http2server(name=cls.http2_server_name,
                                                  listen_port=int(os.getenv('SERVER_LISTEN_PORT')),
                                                  py_string='python3',
                                                  server_file="http2_server.py")
        name = "http2-test-router"
        cls.connector_name = 'connectorToBeDeleted'
        cls.connector_props = {
            'port': os.getenv('SERVER_LISTEN_PORT'),
            'address': 'examples',
            'host': '127.0.0.1',
            'protocolVersion': 'HTTP2',
            'name': cls.connector_name
        }
        config = Qdrouterd.Config([
            ('router', {'mode': 'edge', 'id': 'QDR'}),
            ('listener', {'port': cls.tester.get_port(), 'role': 'normal', 'host': '0.0.0.0'}),

            ('httpListener', {'port': cls.tester.get_port(), 'address': 'examples',
                              'host': '127.0.0.1', 'protocolVersion': 'HTTP2'}),
            ('httpConnector', cls.connector_props)
        ])
        cls.router_qdra = cls.tester.qdrouterd(name, config, wait=True)

    @SkipIfNeeded(skip_test(), "Python 3.7 or greater, Quart 0.13.0 or greater and curl needed to run http2 tests")
    def test_zzz_http_connector_delete(self):
        self.check_connector_delete(client_addr=self.router_qdra.http_addresses[0],
                                    server_addr=self.router_qdra.addresses[0])

class Http2TestOneInteriorRouter(Http2TestBase, CommonHttp2Tests):
    @classmethod
    def setUpClass(cls):
        super(Http2TestOneInteriorRouter, cls).setUpClass()
        if skip_test():
            return
        cls.http2_server_name = "http2_server"
        os.environ["QUART_APP"] = "http2server:app"
        os.environ['SERVER_LISTEN_PORT'] = str(cls.tester.get_port())
        cls.http2_server = cls.tester.http2server(name=cls.http2_server_name,
                                                  listen_port=int(os.getenv('SERVER_LISTEN_PORT')),
                                                  py_string='python3',
                                                  server_file="http2_server.py")
        name = "http2-test-router"
        cls.connector_name = 'connectorToBeDeleted'
        cls.connector_props = {
            'port': os.getenv('SERVER_LISTEN_PORT'),
            'address': 'examples',
            'host': '127.0.0.1',
            'protocolVersion': 'HTTP2',
            'name': cls.connector_name
        }
        config = Qdrouterd.Config([
            ('router', {'mode': 'interior', 'id': 'QDR'}),
            ('listener', {'port': cls.tester.get_port(), 'role': 'normal', 'host': '0.0.0.0'}),

            ('httpListener', {'port': cls.tester.get_port(), 'address': 'examples',
                              'host': '127.0.0.1', 'protocolVersion': 'HTTP2'}),
            ('httpConnector', cls.connector_props)
        ])
        cls.router_qdra = cls.tester.qdrouterd(name, config, wait=True)

    @SkipIfNeeded(skip_test(), "Python 3.7 or greater, Quart 0.13.0 or greater and curl needed to run http2 tests")
    def test_zzz_http_connector_delete(self):
        self.check_connector_delete(client_addr=self.router_qdra.http_addresses[0],
                                    server_addr=self.router_qdra.addresses[0])

class Http2TestTwoRouter(Http2TestBase, CommonHttp2Tests):
    @classmethod
    def setUpClass(cls):
        super(Http2TestTwoRouter, cls).setUpClass()
        if skip_test():
            return
        cls.http2_server_name = "http2_server"
        os.environ["QUART_APP"] = "http2server:app"
        os.environ['SERVER_LISTEN_PORT'] = str(cls.tester.get_port())
        cls.http2_server = cls.tester.http2server(name=cls.http2_server_name,
                                                  listen_port=int(os.getenv('SERVER_LISTEN_PORT')),
                                                  py_string='python3',
                                                  server_file="http2_server.py")
        name = "http2-test-router"
        inter_router_port = cls.tester.get_port()

        config_qdra = Qdrouterd.Config([
            ('router', {'mode': 'interior', 'id': 'QDR.A'}),
            ('listener', {'port': cls.tester.get_port(), 'role': 'normal', 'host': '0.0.0.0'}),
            ('httpListener', {'port': cls.tester.get_port(), 'address': 'examples',
                              'host': '127.0.0.1', 'protocolVersion': 'HTTP2'}),
            ('listener', {'role': 'inter-router', 'port': inter_router_port})
        ])

        cls.connector_name = 'connectorToBeDeleted'
        cls.connector_props = {
            'port': os.getenv('SERVER_LISTEN_PORT'),
            'address': 'examples',
            'host': '127.0.0.1',
            'protocolVersion': 'HTTP2',
            'name': cls.connector_name
        }
        config_qdrb = Qdrouterd.Config([
            ('router', {'mode': 'interior', 'id': 'QDR.B'}),
            ('listener', {'port': cls.tester.get_port(), 'role': 'normal', 'host': '0.0.0.0'}),
            ('httpConnector', cls.connector_props),
            ('connector', {'name': 'connectorToA', 'role': 'inter-router',
                           'port': inter_router_port,
                           'verifyHostname': 'no'})

        ])

        cls.router_qdra = cls.tester.qdrouterd(name, config_qdra, wait=True)
        cls.router_qdrb = cls.tester.qdrouterd(name, config_qdrb, wait=True)

        cls.router_qdra.wait_router_connected('QDR.B')
        cls.router_qdrb.wait_router_connected('QDR.A')

        sleep(2)

    @SkipIfNeeded(skip_test(), "Python 3.7 or greater, Quart 0.13.0 or greater and curl needed to run http2 tests")
    def test_000_stats(self):
        # Run curl 127.0.0.1:port --http2-prior-knowledge
        address = self.router_qdra.http_addresses[0]
        qd_manager_a = QdManager(self, address=self.router_qdra.addresses[0])
        stats_a = qd_manager_a.query('org.apache.qpid.dispatch.httpRequestInfo')

        # First request
        self.run_curl(address=address)
        address = self.router_qdra.http_addresses[0] + "/myinfo"

        # Second request
        out = self.run_curl(args=['-d', 'fname=Mickey&lname=Mouse', '-X', 'POST'], address=address)
        self.assertIn('Success! Your first name is Mickey, last name is Mouse', out)

        # Give time for the core thread to augment the stats.
        i = 0
        while i < 3:
            if not stats_a or stats_a[0].get('requests') < 2:
                sleep(1)
                i += 1
                stats_a = qd_manager_a.query('org.apache.qpid.dispatch.httpRequestInfo')
            else:
                break

        self.assertEqual(len(stats_a), 1)
        self.assertEqual(stats_a[0].get('requests'), 2)
        self.assertEqual(stats_a[0].get('direction'), 'in')
        self.assertEqual(stats_a[0].get('bytesOut'), 3944)
        self.assertEqual(stats_a[0].get('bytesIn'), 24)
        qd_manager_b = QdManager(self, address=self.router_qdrb.addresses[0])
        stats_b = qd_manager_b.query('org.apache.qpid.dispatch.httpRequestInfo')
        self.assertEqual(len(stats_b), 1)

        i = 0
        while i < 3:
            s = stats_b[0]
            if not stats_b or stats_b[0].get('requests') < 2:
                i += 1
                sleep(1)
                stats_b = qd_manager_b.query('org.apache.qpid.dispatch.httpRequestInfo')
            else:
                break

        self.assertEqual(stats_b[0].get('requests'), 2)
        self.assertEqual(stats_b[0].get('direction'), 'out')
        self.assertEqual(stats_b[0].get('bytesOut'), 24)
        self.assertEqual(stats_b[0].get('bytesIn'), 3944)

    @SkipIfNeeded(skip_test(), "Python 3.7 or greater, Quart 0.13.0 or greater and curl needed to run http2 tests")
    def test_zzz_http_connector_delete(self):
        self.check_connector_delete(client_addr=self.router_qdra.http_addresses[0],
                                    server_addr=self.router_qdrb.addresses[0])

class Http2TestEdgeInteriorRouter(Http2TestBase, CommonHttp2Tests):
    """
    The interior router connects to the HTTP2 server and the curl client
    connects to the edge router.
    """
    @classmethod
    def setUpClass(cls):
        super(Http2TestEdgeInteriorRouter, cls).setUpClass()
        if skip_test():
            return
        cls.http2_server_name = "http2_server"
        os.environ["QUART_APP"] = "http2server:app"
        os.environ['SERVER_LISTEN_PORT'] = str(cls.tester.get_port())
        cls.http2_server = cls.tester.http2server(name=cls.http2_server_name,
                                                  listen_port=int(os.getenv('SERVER_LISTEN_PORT')),
                                                  py_string='python3',
                                                  server_file="http2_server.py")
        inter_router_port = cls.tester.get_port()
        config_edgea = Qdrouterd.Config([
            ('router', {'mode': 'edge', 'id': 'EDGE.A'}),
            ('listener', {'port': cls.tester.get_port(), 'role': 'normal', 'host': '0.0.0.0'}),
            ('httpListener', {'port': cls.tester.get_port(), 'address': 'examples',
                              'host': '127.0.0.1', 'protocolVersion': 'HTTP2'}),
            ('connector', {'name': 'connectorToA', 'role': 'edge',
                           'port': inter_router_port,
                           'verifyHostname': 'no'})
        ])

        config_qdrb = Qdrouterd.Config([
            ('router', {'mode': 'interior', 'id': 'QDR.A'}),
            ('listener', {'port': cls.tester.get_port(), 'role': 'normal', 'host': '0.0.0.0'}),
            ('listener', {'role': 'edge', 'port': inter_router_port}),
            ('httpConnector',
             {'port': os.getenv('SERVER_LISTEN_PORT'), 'address': 'examples',
              'host': '127.0.0.1', 'protocolVersion': 'HTTP2'})
        ])

        cls.router_qdrb = cls.tester.qdrouterd("interior-router", config_qdrb, wait=True)
        cls.router_qdra = cls.tester.qdrouterd("edge-router", config_edgea)
        sleep(3)


class Http2TestInteriorEdgeRouter(Http2TestBase, CommonHttp2Tests):
    """
    The edge router connects to the HTTP2 server and the curl client
    connects to the interior router.
    """
    @classmethod
    def setUpClass(cls):
        super(Http2TestInteriorEdgeRouter, cls).setUpClass()
        if skip_test():
            return
        cls.http2_server_name = "http2_server"
        os.environ["QUART_APP"] = "http2server:app"
        os.environ['SERVER_LISTEN_PORT'] = str(cls.tester.get_port())
        cls.http2_server = cls.tester.http2server(name=cls.http2_server_name,
                                                  listen_port=int(os.getenv('SERVER_LISTEN_PORT')),
                                                  py_string='python3',
                                                  server_file="http2_server.py")
        inter_router_port = cls.tester.get_port()
        config_edge = Qdrouterd.Config([
            ('router', {'mode': 'edge', 'id': 'EDGE.A'}),
            ('listener', {'port': cls.tester.get_port(), 'role': 'normal', 'host': '0.0.0.0'}),
            ('httpConnector',
             {'port': os.getenv('SERVER_LISTEN_PORT'), 'address': 'examples',
              'host': '127.0.0.1', 'protocolVersion': 'HTTP2'}),
            ('connector', {'name': 'connectorToA', 'role': 'edge',
                           'port': inter_router_port,
                           'verifyHostname': 'no'})
        ])

        config_qdra = Qdrouterd.Config([
            ('router', {'mode': 'interior', 'id': 'QDR.A'}),
            ('listener', {'port': cls.tester.get_port(), 'role': 'normal', 'host': '0.0.0.0'}),
            ('listener', {'role': 'edge', 'port': inter_router_port}),
            ('httpListener',
             {'port': cls.tester.get_port(), 'address': 'examples',
              'host': '127.0.0.1', 'protocolVersion': 'HTTP2'}),
        ])

        cls.router_qdra = cls.tester.qdrouterd("interior-router", config_qdra, wait=True)
        cls.router_qdrb = cls.tester.qdrouterd("edge-router", config_edge)
        sleep(3)



class Http2TestEdgeToEdgeViaInteriorRouter(Http2TestBase, CommonHttp2Tests):
    """
    The edge router connects to the HTTP2 server and the curl client
    connects to another edge router. The two edge routers are connected
    via an interior router.
    """
    @classmethod
    def setUpClass(cls):
        super(Http2TestEdgeToEdgeViaInteriorRouter, cls).setUpClass()
        if skip_test():
            return
        cls.http2_server_name = "http2_server"
        os.environ["QUART_APP"] = "http2server:app"
        os.environ['SERVER_LISTEN_PORT'] = str(cls.tester.get_port())
        cls.http2_server = cls.tester.http2server(name=cls.http2_server_name,
                                                  listen_port=int(os.getenv('SERVER_LISTEN_PORT')),
                                                  py_string='python3',
                                                  server_file="http2_server.py")

        cls.connector_name = 'connectorToBeDeleted'
        cls.connector_props = {
            'port': os.getenv('SERVER_LISTEN_PORT'),
            'address': 'examples',
            'host': '127.0.0.1',
            'protocolVersion': 'HTTP2',
            'name': cls.connector_name
        }
        inter_router_port = cls.tester.get_port()
        config_edge_b = Qdrouterd.Config([
            ('router', {'mode': 'edge', 'id': 'EDGE.A'}),
            ('listener', {'port': cls.tester.get_port(), 'role': 'normal', 'host': '0.0.0.0'}),
            ('httpConnector', cls.connector_props),
            ('connector', {'name': 'connectorToA', 'role': 'edge',
                           'port': inter_router_port,
                           'verifyHostname': 'no'})
        ])

        config_qdra = Qdrouterd.Config([
            ('router', {'mode': 'interior', 'id': 'QDR.A'}),
            ('listener', {'port': cls.tester.get_port(), 'role': 'normal', 'host': '0.0.0.0'}),
            ('listener', {'role': 'edge', 'port': inter_router_port}),
        ])

        config_edge_a = Qdrouterd.Config([
            ('router', {'mode': 'edge', 'id': 'EDGE.B'}),
            ('listener', {'port': cls.tester.get_port(), 'role': 'normal',
                          'host': '0.0.0.0'}),
            ('httpListener',
             {'port': cls.tester.get_port(), 'address': 'examples',
              'host': '127.0.0.1', 'protocolVersion': 'HTTP2'}),
            ('connector', {'name': 'connectorToA', 'role': 'edge',
                           'port': inter_router_port,
                           'verifyHostname': 'no'})
        ])

        cls.interior_qdr = cls.tester.qdrouterd("interior-router", config_qdra,
                                               wait=True)
        cls.router_qdra = cls.tester.qdrouterd("edge-router-a", config_edge_a)
        cls.router_qdrb = cls.tester.qdrouterd("edge-router-b", config_edge_b)
        sleep(5)

    @SkipIfNeeded(skip_test(), "Python 3.7 or greater, Quart 0.13.0 or greater and curl needed to run http2 tests")
    def test_zzz_http_connector_delete(self):
        self.check_connector_delete(client_addr=self.router_qdra.http_addresses[0],
                                    server_addr=self.router_qdrb.addresses[0])

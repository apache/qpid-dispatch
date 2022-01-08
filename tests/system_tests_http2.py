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

import os
import sys
import hashlib
import unittest
from subprocess import PIPE
from time import sleep

import system_test
from system_test import TestCase, Qdrouterd, QdManager, Process
from system_test import curl_available, TIMEOUT, skip_test_in_ci

h2hyper_installed = True
try:
    import h2.connection # noqa F401: imported but unused  # pylint: disable=unused-import
except ImportError:
    h2hyper_installed = False


def python_37_available():
    if sys.version_info >= (3, 7):
        return True


def quart_available():
    """Checks if quart version is greater than 0.13"""
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
        print(e)
        print("quart_not_available")
        return False


def skip_test():
    if python_37_available() and quart_available() and curl_available():
        return False
    return True


def skip_h2_test():
    if python_37_available() and h2hyper_installed and curl_available():
        return False
    return True


def get_digest(file_path):
    h = hashlib.sha256()

    with open(file_path, 'rb') as file:
        while True:
            # Reading is buffered, so we can read smaller chunks.
            chunk = file.read(h.block_size)
            if not chunk:
                break
            h.update(chunk)

    return h.hexdigest()


def image_file(name):
    return os.path.join(system_test.DIR, 'images', name)


class Http2TestBase(TestCase):

    def run_curl(self, address, args=None, input=None, timeout=TIMEOUT):
        """Run the curl command using the HTTP/2 protocol"""
        local_args = [str(address), "--http2-prior-knowledge"]
        if args:
            local_args +=  args

        status, out, err = system_test.run_curl(local_args, input=input,
                                                timeout=timeout)
        if status != 0:
            print("CURL ERROR (%s): %s %s" % (status, out, err), flush=True)

        assert status == 0
        return out


class CommonHttp2Tests:
    """
    Common Base class containing all tests. These tests are run by all
    topologies of routers.
    """

    @unittest.skipIf(skip_test(), "Python 3.7 or greater, Quart 0.13.0 or greater and curl needed to run http2 tests")
    # Tests the HTTP2 head request
    def test_head_request(self):
        # Run curl 127.0.0.1:port --http2-prior-knowledge --head
        address = self.router_qdra.http_addresses[0]
        out = self.run_curl(address, args=["--head"])
        self.assertIn('HTTP/2 200', out)
        self.assertIn('server: hypercorn-h2', out)
        self.assertIn('content-type: text/html; charset=utf-8', out)

    @unittest.skipIf(skip_test(), "Python 3.7 or greater, Quart 0.13.0 or greater and curl needed to run http2 tests")
    def test_get_request(self):
        # Run curl 127.0.0.1:port --http2-prior-knowledge
        address = self.router_qdra.http_addresses[0]
        out = self.run_curl(address)
        i = 0
        ret_string = ""
        while i < 1000:
            ret_string += str(i) + ","
            i += 1
        self.assertIn(ret_string, out)

    # @unittest.skipIf(skip_test(), "Python 3.7 or greater, Quart 0.13.0 or greater and curl needed to run http2 tests")
    # def test_large_get_request(self):
        # Tests a large get request. Response is more than 50k which means it
        # will span many qd_http2_buffer_t objects.
        # Run curl 127.0.0.1:port/largeget --http2-prior-knowledge
    #    address = self.router_qdra.http_addresses[0] + "/largeget"
    #    out = self.run_curl(address)
    #    self.assertIn("49996,49997,49998,49999", out)

    @unittest.skipIf(skip_test(), "Python 3.7 or greater, Quart 0.13.0 or greater and curl needed to run http2 tests")
    def test_post_request(self):
        # curl -d "fname=John&lname=Doe" -X POST 127.0.0.1:9000/myinfo --http2-prior-knowledge
        address = self.router_qdra.http_addresses[0] + "/myinfo"
        out = self.run_curl(address, args=['-d', 'fname=John&lname=Doe', '-X', 'POST'])
        self.assertIn('Success! Your first name is John, last name is Doe', out)

    skip_reason = 'Test skipped on certain Travis environments'

    @unittest.skipIf(skip_test(), "Python 3.7 or greater, Quart 0.13.0 or greater and curl needed to run http2 tests")
    @unittest.skipIf(skip_test_in_ci('QPID_SYSTEM_TEST_SKIP_HTTP2_LARGE_IMAGE_UPLOAD_TEST'), skip_reason)
    def test_post_upload_large_image_jpg(self):
        # curl  -X POST -H "Content-Type: multipart/form-data"  -F "data=@/home/gmurthy/opensource/test.jpg"
        # http://127.0.0.1:9000/upload --http2-prior-knowledge
        address = self.router_qdra.http_addresses[0] + "/upload"
        out = self.run_curl(address, args=['-X', 'POST', '-H', 'Content-Type: multipart/form-data',
                                           '-F', 'data=@' + image_file('test.jpg')])
        self.assertIn('Success', out)

    @unittest.skipIf(skip_test(), "Python 3.7 or greater, Quart 0.13.0 or greater and curl needed to run http2 tests")
    def test_delete_request(self):
        # curl -X DELETE "http://127.0.0.1:9000/myinfo/delete/22122" -H  "accept: application/json" --http2-prior-knowledge
        address = self.router_qdra.http_addresses[0] + "/myinfo/delete/22122"
        out = self.run_curl(address, args=['-X', 'DELETE'])
        self.assertIn('{"fname": "John", "lname": "Doe", "id": "22122"}', out)

    @unittest.skipIf(skip_test(), "Python 3.7 or greater, Quart 0.13.0 or greater and curl needed to run http2 tests")
    def test_put_request(self):
        # curl -d "fname=John&lname=Doe" -X PUT 127.0.0.1:9000/myinfo --http2-prior-knowledge
        address = self.router_qdra.http_addresses[0] + "/myinfo"
        out = self.run_curl(address, args=['-d', 'fname=John&lname=Doe', '-X', 'PUT'])
        self.assertIn('Success! Your first name is John, last name is Doe', out)

    @unittest.skipIf(skip_test(), "Python 3.7 or greater, Quart 0.13.0 or greater and curl needed to run http2 tests")
    def test_patch_request(self):
        # curl -d "fname=John&lname=Doe" -X PATCH 127.0.0.1:9000/myinfo --http2-prior-knowledge
        address = self.router_qdra.http_addresses[0] + "/patch"
        out = self.run_curl(address, args=['--data', '{\"op\":\"add\",\"path\":\"/user\",\"value\":\"jane\"}', '-X', 'PATCH'])
        self.assertIn('"op":"add"', out)

    @unittest.skipIf(skip_test(), "Python 3.7 or greater, Quart 0.13.0 or greater and curl needed to run http2 tests")
    def test_404(self):
        # Run curl 127.0.0.1:port/unavailable --http2-prior-knowledge
        address = self.router_qdra.http_addresses[0] + "/unavailable"
        out = self.run_curl(address=address)
        self.assertIn('404 Not Found', out)

    @unittest.skipIf(skip_test(), "Python 3.7 or greater, Quart 0.13.0 or greater and curl needed to run http2 tests")
    def test_500(self):
        # Run curl 127.0.0.1:port/test/500 --http2-prior-knowledge
        address = self.router_qdra.http_addresses[0] + "/test/500"
        out = self.run_curl(address)
        self.assertIn('500 Internal Server Error', out)

    @unittest.skipIf(skip_test(), "Python 3.7 or greater, Quart 0.13.0 or greater and curl needed to run http2 tests")
    def test_get_image_png(self):
        # Run curl 127.0.0.1:port --output images/balanced-routing.png --http2-prior-knowledge
        image_file_name = '/balanced-routing.png'
        address = self.router_qdra.http_addresses[0] + "/images" + image_file_name
        self.run_curl(address, args=['--output', self.router_qdra.outdir + image_file_name])
        digest_of_server_file = get_digest(image_file(image_file_name[1:]))
        digest_of_response_file = get_digest(self.router_qdra.outdir + image_file_name)
        self.assertEqual(digest_of_server_file, digest_of_response_file)

    @unittest.skipIf(skip_test(), "Python 3.7 or greater, Quart 0.13.0 or greater and curl needed to run http2 tests")
    def test_get_image_jpg(self):
        # Run curl 127.0.0.1:port --output images/apache.jpg --http2-prior-knowledge
        image_file_name = '/apache.jpg'
        address = self.router_qdra.http_addresses[0] + "/images" + image_file_name
        self.run_curl(address, args=['--output', self.router_qdra.outdir + image_file_name])
        digest_of_server_file = get_digest(image_file(image_file(image_file_name[1:])))
        digest_of_response_file = get_digest(self.router_qdra.outdir + image_file_name)
        self.assertEqual(digest_of_server_file, digest_of_response_file)

    def check_listener_delete(self, client_addr, server_addr):
        # Run curl 127.0.0.1:port --http2-prior-knowledge
        # We are first making sure that the http request goes thru successfully.
        out = self.run_curl(client_addr)
        ret_string = ""
        i = 0
        while i < 1000:
            ret_string += str(i) + ","
            i += 1
        self.assertIn(ret_string, out)

        qd_manager = QdManager(self, address=server_addr)
        http_listeners = qd_manager.query('org.apache.qpid.dispatch.httpListener')
        self.assertEqual(len(http_listeners), 1)

        # Run a qdmanage DELETE on the httpListener
        qd_manager.delete("org.apache.qpid.dispatch.httpListener", name=self.listener_name)

        # Make sure the listener is gone
        http_listeners  = qd_manager.query('org.apache.qpid.dispatch.httpListener')
        self.assertEqual(len(http_listeners), 0)

        # Try running a curl command against the listener to make sure it times out
        request_timed_out = False
        try:
            out = self.run_curl(client_addr, timeout=3)
        except Exception as e:
            request_timed_out = True
        self.assertTrue(request_timed_out)

        # Add back the listener and run a curl command to make sure that the newly added listener is
        # back up and running.
        create_result = qd_manager.create("org.apache.qpid.dispatch.httpListener", self.http_listener_props)
        sleep(2)
        out = self.run_curl(client_addr)
        self.assertIn(ret_string, out)

    def check_connector_delete(self, client_addr, server_addr):
        # Run curl 127.0.0.1:port --http2-prior-knowledge
        # We are first making sure that the http request goes thru successfully.
        out = self.run_curl(client_addr)

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

        # Now, run a curl client GET request with a timeout
        request_timed_out = False
        try:
            out = self.run_curl(client_addr, timeout=5)
            print(out)
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
            if len(connections) < 2:
                sleep(2)
            else:
                conn_present = True
        self.assertTrue(conn_present)

        out = self.run_curl(client_addr)
        ret_string = ""
        i = 0
        while i < 1000:
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

    @unittest.skipIf(skip_test(), "Python 3.7 or greater, Quart 0.13.0 or greater and curl needed to run http2 tests")
    def test_zzz_http_connector_delete(self):
        self.check_connector_delete(client_addr=self.router_qdra.http_addresses[0],
                                    server_addr=self.router_qdra.addresses[0])

    @unittest.skipIf(skip_test(), "Python 3.7 or greater, Quart 0.13.0 or greater and curl needed to run http2 tests")
    def test_000_stats(self):
        # Run curl 127.0.0.1:port --http2-prior-knowledge
        address = self.router_qdra.http_addresses[0]
        qd_manager = QdManager(self, address=self.router_qdra.addresses[0])

        # First request
        out = self.run_curl(address)

        # Second request
        address = self.router_qdra.http_addresses[0] + "/myinfo"
        out = self.run_curl(address, args=['-d', 'fname=Mickey&lname=Mouse', '-X', 'POST'])
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

    @unittest.skipIf(skip_test(), "Python 3.7 or greater, Quart 0.13.0 or greater and curl needed to run http2 tests")
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

    @unittest.skipIf(skip_test(), "Python 3.7 or greater, Quart 0.13.0 or greater and curl needed to run http2 tests")
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
        cls.http_listener_port = cls.tester.get_port()
        cls.listener_name = 'listenerToBeDeleted'
        cls.http_listener_props = {
            'port': cls.http_listener_port,
            'address': 'examples',
            'host': '127.0.0.1',
            'protocolVersion': 'HTTP2',
            'name': cls.listener_name
        }

        config_qdra = Qdrouterd.Config([
            ('router', {'mode': 'interior', 'id': 'QDR.A'}),
            ('listener', {'port': cls.tester.get_port(), 'role': 'normal', 'host': '0.0.0.0'}),
            ('httpListener', cls.http_listener_props),
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

    @unittest.skipIf(skip_test(), "Python 3.7 or greater, Quart 0.13.0 or greater and curl needed to run http2 tests")
    def test_000_stats(self):
        # Run curl 127.0.0.1:port --http2-prior-knowledge
        address = self.router_qdra.http_addresses[0]
        qd_manager_a = QdManager(self, address=self.router_qdra.addresses[0])
        stats_a = qd_manager_a.query('org.apache.qpid.dispatch.httpRequestInfo')

        # First request
        self.run_curl(address)
        address = self.router_qdra.http_addresses[0] + "/myinfo"

        # Second request
        out = self.run_curl(address, args=['-d', 'fname=Mickey&lname=Mouse', '-X', 'POST'])
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

    @unittest.skipIf(skip_test(), "Python 3.7 or greater, Quart 0.13.0 or greater and curl needed to run http2 tests")
    def test_yyy_http_listener_delete(self):
        self.check_listener_delete(client_addr=self.router_qdra.http_addresses[0],
                                   server_addr=self.router_qdra.addresses[0])

    @unittest.skipIf(skip_test(), "Python 3.7 or greater, Quart 0.13.0 or greater and curl needed to run http2 tests")
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


class Http2TestDoubleEdgeInteriorRouter(Http2TestBase):
    """
    There are two edge routers connecting to the same interior router. The two edge routers
    connect to a HTTP2 server. The curl client connects to the interior router and makes
    requests. Since the edge routers are each connected to the http2 server, one of the
    edge router will receive the curl request. We then take down the connector of one of the
    edge routers and make sure that a request is still routed via the the other edge router.
    We will then take down the connector on the other edge router and make sure that
    a curl request times out since the it has nowhere to go.
    """
    @classmethod
    def setUpClass(cls):
        super(Http2TestDoubleEdgeInteriorRouter, cls).setUpClass()
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
        cls.edge_a_connector_name = 'connectorFromEdgeAToIntA'
        cls.edge_a_http_connector_name = 'httpConnectorFromEdgeAToHttpServer'
        config_edgea = Qdrouterd.Config([
            ('router', {'mode': 'edge', 'id': 'EDGE.A'}),
            ('listener', {'port': cls.tester.get_port(), 'role': 'normal', 'host': '0.0.0.0'}),
            ('httpConnector',
             {'port': os.getenv('SERVER_LISTEN_PORT'),
              'address': 'examples',
              'name': cls.edge_a_http_connector_name,
              'host': '127.0.0.1',
              'protocolVersion': 'HTTP2'}),
            ('connector', {'name': cls.edge_a_connector_name,
                           'role': 'edge',
                           'port': inter_router_port,
                           'verifyHostname': 'no'})
        ])

        cls.edge_b_connector_name = 'connectorFromEdgeBToIntA'
        cls.edge_b_http_connector_name = 'httpConnectorFromEdgeBToHttpServer'
        config_edgeb = Qdrouterd.Config([
            ('router', {'mode': 'edge', 'id': 'EDGE.B'}),
            ('listener', {'port': cls.tester.get_port(), 'role': 'normal', 'host': '0.0.0.0'}),
            ('httpConnector',
             {'port': os.getenv('SERVER_LISTEN_PORT'),
              'address': 'examples',
              'name': cls.edge_b_http_connector_name,
              'host': '127.0.0.1',
              'protocolVersion': 'HTTP2'}),
            ('connector', {'name': cls.edge_b_connector_name,
                           'role': 'edge',
                           'port': inter_router_port,
                           'verifyHostname': 'no'})
        ])

        config_qdrc = Qdrouterd.Config([
            ('router', {'mode': 'interior', 'id': 'QDR.A'}),
            ('listener', {'port': cls.tester.get_port(), 'role': 'normal', 'host': '0.0.0.0'}),
            ('listener', {'role': 'edge', 'port': inter_router_port}),
            ('httpListener', {'port': cls.tester.get_port(),
                              'address': 'examples',
                              'host': '127.0.0.1',
                              'protocolVersion': 'HTTP2'}),
        ])

        cls.router_qdrc = cls.tester.qdrouterd("interior-router", config_qdrc, wait=True)
        cls.router_qdra = cls.tester.qdrouterd("edge-router-a", config_edgea, wait=True)
        cls.router_qdrb = cls.tester.qdrouterd("edge-router-b", config_edgeb, wait=True)
        sleep(3)

    @unittest.skipIf(skip_test(), "Python 3.7 or greater, Quart 0.13.0 or greater and curl needed to run http2 tests")
    def test_check_connector_delete(self):
        # We are first making sure that the http request goes thru successfully.
        # We are making the http request on the interior router.
        # The interior router will route this request to the http server via one of the edge routers.
        # Run curl 127.0.0.1:port --http2-prior-knowledge --head

        # There should be one proxy link for each edge router.
        self.router_qdrc.wait_address("examples", subscribers=2)

        address = self.router_qdrc.http_addresses[0]
        out = self.run_curl(address, args=["--head"])
        self.assertIn('HTTP/2 200', out)
        self.assertIn('server: hypercorn-h2', out)
        self.assertIn('content-type: text/html; charset=utf-8', out)

        # Now delete the httpConnector on the edge router config_edgea
        qd_manager = QdManager(self, address=self.router_qdra.addresses[0])
        qd_manager.delete("org.apache.qpid.dispatch.httpConnector", name=self.edge_a_http_connector_name)
        sleep(2)

        # now check the interior router for the examples address. Since the httpConnector on one of the
        # edge routers was deleted, the proxy link on that edge router must be gone leaving us with just one proxy
        # link on the other edge router.
        self.router_qdrc.wait_address("examples", subscribers=1)

        # Run the curl command again to make sure that the request completes again. The request is now routed thru
        # edge router B since the connector on  edge router A is gone
        address = self.router_qdrc.http_addresses[0]
        out = self.run_curl(address, args=["--head"])
        self.assertIn('HTTP/2 200', out)
        self.assertIn('server: hypercorn-h2', out)
        self.assertIn('content-type: text/html; charset=utf-8', out)

        # Now delete the httpConnector on the edge router config_edgeb
        qd_manager = QdManager(self, address=self.router_qdrb.addresses[0])
        qd_manager.delete("org.apache.qpid.dispatch.httpConnector", name=self.edge_b_http_connector_name)
        sleep(2)

        # Now, run a curl client GET request with a timeout.
        # Since both connectors on both edge routers are gone, the curl client will time out
        # The curl client times out instead of getting a 503 because the credit is not given on the interior
        # router to create an AMQP message because there is no destination for the router address.
        request_timed_out = False
        try:
            out = self.run_curl(address, args=["--head"], timeout=3)
            print(out)
        except Exception as e:
            request_timed_out = True
        self.assertTrue(request_timed_out)


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

    @unittest.skipIf(skip_test(), "Python 3.7 or greater, Quart 0.13.0 or greater and curl needed to run http2 tests")
    def test_zzz_http_connector_delete(self):
        self.check_connector_delete(client_addr=self.router_qdra.http_addresses[0],
                                    server_addr=self.router_qdrb.addresses[0])


class Http2TestGoAway(Http2TestBase):

    @classmethod
    def setUpClass(cls):
        super(Http2TestGoAway, cls).setUpClass()
        if skip_h2_test():
            return
        cls.http2_server_name = "hyperh2_server"
        os.environ['SERVER_LISTEN_PORT'] = str(cls.tester.get_port())
        cls.http2_server = cls.tester.http2server(name=cls.http2_server_name,
                                                  listen_port=int(os.getenv('SERVER_LISTEN_PORT')),
                                                  py_string='python3',
                                                  server_file="hyperh2_server.py")
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
            ('router', {'mode': 'standalone', 'id': 'QDR'}),
            ('listener', {'port': cls.tester.get_port(), 'role': 'normal', 'host': '0.0.0.0'}),

            ('httpListener', {'port': cls.tester.get_port(), 'address': 'examples',
                              'host': '127.0.0.1', 'protocolVersion': 'HTTP2'}),
            ('httpConnector', cls.connector_props)
        ])
        cls.router_qdra = cls.tester.qdrouterd(name, config, wait=True)

    @unittest.skipIf(skip_h2_test(),
                     "Python 3.7 or greater, hyper-h2 and curl needed to run hyperhttp2 tests")
    def test_goaway(self):
        # Executes a request against the router at the /goaway_test_1 URL
        # The router in turn forwards the request to the http2 server which
        # responds with a GOAWAY frame. The router propagates this
        # GOAWAY frame to the client and issues a HTTP 503 to the client
        address = self.router_qdra.http_addresses[0] + "/goaway_test_1"
        out = self.run_curl(address, args=["-i"])
        self.assertIn("HTTP/2 503", out)


class Http2Q2OneRouterTest(Http2TestBase):

    @classmethod
    def setUpClass(cls):
        super(Http2Q2OneRouterTest, cls).setUpClass()
        if skip_h2_test():
            return
        cls.http2_server_name = "http2_slow_q2_server"
        os.environ['SERVER_LISTEN_PORT'] = str(cls.tester.get_port())
        cls.http2_server = cls.tester.http2server(name=cls.http2_server_name,
                                                  listen_port=int(os.getenv('SERVER_LISTEN_PORT')),
                                                  py_string='python3',
                                                  server_file="http2_slow_q2_server.py")
        name = "http2-test-router"
        cls.connector_name = 'connectorToServer'
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

    @unittest.skipIf(skip_h2_test(),
                     "Python 3.7 or greater, hyper-h2 and curl needed to run hyperhttp2 tests")
    def test_q2_block_unblock(self):
        # curl  -X POST -H "Content-Type: multipart/form-data"  -F "data=@/home/gmurthy/opensource/test.jpg"
        # http://127.0.0.1:9000/upload --http2-prior-knowledge
        address = self.router_qdra.http_addresses[0] + "/upload"
        out = self.run_curl(address, args=['-X', 'POST', '-H', 'Content-Type: multipart/form-data',
                                           '-F', 'data=@' + image_file('test.jpg')])
        self.assertIn('Success', out)
        num_blocked = 0
        num_unblocked = 0
        blocked = "q2 is blocked"
        unblocked = "q2 is unblocked"
        with open(self.router_qdra.logfile_path, 'r') as router_log:
            log_lines = router_log.read().split("\n")
            for log_line in log_lines:
                if unblocked in log_line:
                    num_unblocked += 1
                elif blocked in log_line:
                    num_blocked += 1

        self.assertGreater(num_blocked, 0)
        self.assertGreater(num_unblocked, 0)


class Http2Q2TwoRouterTest(Http2TestBase):

    @classmethod
    def setUpClass(cls):
        super(Http2Q2TwoRouterTest, cls).setUpClass()
        if skip_h2_test():
            return
        cls.http2_server_name = "http2_server"
        os.environ['SERVER_LISTEN_PORT'] = str(cls.tester.get_port())
        cls.http2_server = cls.tester.http2server(name=cls.http2_server_name,
                                                  listen_port=int(os.getenv('SERVER_LISTEN_PORT')),
                                                  py_string='python3',
                                                  server_file="http2_slow_q2_server.py")
        qdr_a = "QDR.A"
        inter_router_port = cls.tester.get_port()
        config_qdra = Qdrouterd.Config([
            ('router', {'mode': 'interior', 'id': 'QDR.A'}),
            ('listener', {'port': cls.tester.get_port(), 'role': 'normal', 'host': '0.0.0.0'}),
            ('httpListener', {'port': cls.tester.get_port(), 'address': 'examples',
                              'host': '127.0.0.1', 'protocolVersion': 'HTTP2'}),
            ('connector', {'name': 'connectorToB', 'role': 'inter-router',
                           'port': inter_router_port,
                           'verifyHostname': 'no'})
        ])

        qdr_b = "QDR.B"
        cls.connector_name = 'serverConnector'
        cls.http_connector_props = {
            'port': os.getenv('SERVER_LISTEN_PORT'),
            'address': 'examples',
            'host': '127.0.0.1',
            'protocolVersion': 'HTTP2',
            'name': cls.connector_name
        }
        config_qdrb = Qdrouterd.Config([
            ('router', {'mode': 'interior', 'id': 'QDR.B'}),
            ('httpConnector', cls.http_connector_props),
            ('listener', {'role': 'inter-router', 'maxSessionFrames': '10', 'port': inter_router_port})
        ])
        cls.router_qdrb = cls.tester.qdrouterd(qdr_b, config_qdrb, wait=True)
        cls.router_qdra = cls.tester.qdrouterd(qdr_a, config_qdra, wait=True)
        cls.router_qdra.wait_router_connected('QDR.B')

    @unittest.skipIf(skip_h2_test(),
                     "Python 3.7 or greater, hyper-h2 and curl needed to run hyperhttp2 tests")
    def test_q2_block_unblock(self):
        # curl  -X POST -H "Content-Type: multipart/form-data"  -F "data=@/home/gmurthy/opensource/test.jpg"
        # http://127.0.0.1:9000/upload --http2-prior-knowledge
        address = self.router_qdra.http_addresses[0] + "/upload"
        out = self.run_curl(address, args=['-X', 'POST', '-H', 'Content-Type: multipart/form-data',
                                           '-F', 'data=@' + image_file('test.jpg')])
        self.assertIn('Success', out)
        num_blocked = 0
        num_unblocked = 0
        blocked = "q2 is blocked"
        unblocked = "q2 is unblocked"
        with open(self.router_qdra.logfile_path, 'r') as router_log:
            log_lines = router_log.read().split("\n")
            for log_line in log_lines:
                if unblocked in log_line:
                    num_unblocked += 1
                elif blocked in log_line:
                    num_blocked += 1

        self.assertGreater(num_blocked, 0)
        self.assertGreater(num_unblocked, 0)

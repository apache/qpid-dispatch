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
from time import sleep
import system_test
from system_test import TestCase, Qdrouterd, QdManager, Process
from system_test import curl_available, TIMEOUT, skip_test_in_ci
from system_tests_ssl import RouterTestSslBase
from system_test import DIR
from subprocess import PIPE
from proton import SASL
from system_tests_sasl_plain import RouterTestPlainSaslCommon
h2hyper_installed = True
try:
    import h2.connection # noqa F401: imported but unused
except ImportError:
    h2hyper_installed = False


def python_37_available():
    if sys.version_info >= (3, 7):
        return True


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
    @classmethod
    def setUpClass(cls, tls_v12=False):
        super(Http2TestBase, cls).setUpClass()
        cls.curl_args = None
        cls.tls_v12 = tls_v12

    def get_all_curl_args(self, args=None):
        if self.curl_args:
            if args:
                return self.curl_args + args
            return self.curl_args
        return args

    def run_curl(self, address, args=None, input=None, timeout=TIMEOUT,
                 http2_prior_knowledge=True,
                 no_alpn=False,
                 assert_status=True):
        """
        Run the curl command using the HTTP/2 protocol
        """
        local_args = [str(address)]
        if http2_prior_knowledge:
            local_args += ["--http2-prior-knowledge"]
        if no_alpn:
            local_args += ["--no-alpn"]

        if args:
            local_args += args

        status, out, err = system_test.run_curl(local_args, input=input, timeout=timeout)
        if status != 0:
            print("CURL ERROR (%s): %s %s" % (status, out, err), flush=True)

        if assert_status:
            assert status == 0
        if out:
            return out
        if err:
            return err
        return None


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
        out = self.run_curl(address, args=self.get_all_curl_args(['--head']))
        self.assertIn('HTTP/2 200', out)
        self.assertIn('server: hypercorn-h2', out)
        self.assertIn('content-type: text/html; charset=utf-8', out)

    @unittest.skipIf(skip_test(), "Python 3.7 or greater, Quart 0.13.0 or greater and curl needed to run http2 tests")
    def test_get_request(self):
        # Run curl 127.0.0.1:port --http2-prior-knowledge
        address = self.router_qdra.http_addresses[0]
        out = self.run_curl(address, args=self.get_all_curl_args())
        i = 0
        ret_string = ""
        while (i < 1000):
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
        out = self.run_curl(address, args=self.get_all_curl_args(['-d', 'fname=John&lname=Doe', '-X', 'POST']))
        self.assertIn('Success! Your first name is John, last name is Doe', out)

    skip_reason = 'Test skipped on certain Travis environments'

    @unittest.skipIf(skip_test(), "Python 3.7 or greater, Quart 0.13.0 or greater and curl needed to run http2 tests")
    @unittest.skipIf(skip_test_in_ci('QPID_SYSTEM_TEST_SKIP_HTTP2_LARGE_IMAGE_UPLOAD_TEST'), skip_reason)
    def test_post_upload_large_image_jpg(self):
        # curl  -X POST -H "Content-Type: multipart/form-data"  -F "data=@/home/gmurthy/opensource/test.jpg"
        # http://127.0.0.1:9000/upload --http2-prior-knowledge
        address = self.router_qdra.http_addresses[0] + "/upload"
        out = self.run_curl(address, args=self.get_all_curl_args(['-X', 'POST', '-H',
                                                                  'Content-Type: multipart/form-data',
                                                                  '-F', 'data=@' + image_file('test.jpg')]))
        self.assertIn('Success', out)

    @unittest.skipIf(skip_test(), "Python 3.7 or greater, Quart 0.13.0 or greater and curl needed to run http2 tests")
    def test_delete_request(self):
        #curl -X DELETE "http://127.0.0.1:9000/myinfo/delete/22122" -H
        # "accept: application/json" --http2-prior-knowledge
        address = self.router_qdra.http_addresses[0] + "/myinfo/delete/22122"
        out = self.run_curl(address, args=self.get_all_curl_args(['-X', 'DELETE']))
        self.assertIn('{"fname": "John", "lname": "Doe", "id": "22122"}', out)

    @unittest.skipIf(skip_test(), "Python 3.7 or greater, Quart 0.13.0 or greater and curl needed to run http2 tests")
    def test_put_request(self):
        # curl -d "fname=John&lname=Doe" -X PUT 127.0.0.1:9000/myinfo --http2-prior-knowledge
        address = self.router_qdra.http_addresses[0] + "/myinfo"
        out = self.run_curl(address, args=self.get_all_curl_args(['-d', 'fname=John&lname=Doe', '-X', 'PUT']))
        self.assertIn('Success! Your first name is John, last name is Doe', out)

    @unittest.skipIf(skip_test(), "Python 3.7 or greater, Quart 0.13.0 or greater and curl needed to run http2 tests")
    def test_patch_request(self):
        # curl -d "fname=John&lname=Doe" -X PATCH 127.0.0.1:9000/myinfo --http2-prior-knowledge
        address = self.router_qdra.http_addresses[0] + "/patch"
        out = self.run_curl(address, args=self.get_all_curl_args(['--data',
                                                                  '{\"op\":\"add\",\"path\":\"/user\",\"value\":\"jane\"}',
                                                                  '-X', 'PATCH']))
        self.assertIn('"op":"add"', out)

    @unittest.skipIf(skip_test(), "Python 3.7 or greater, Quart 0.13.0 or greater and curl needed to run http2 tests")
    def test_404(self):
        # Run curl 127.0.0.1:port/unavailable --http2-prior-knowledge
        address = self.router_qdra.http_addresses[0] + "/unavailable"
        out = self.run_curl(address=address, args=self.get_all_curl_args())
        self.assertIn('404 Not Found', out)

    @unittest.skipIf(skip_test(), "Python 3.7 or greater, Quart 0.13.0 or greater and curl needed to run http2 tests")
    def test_500(self):
        # Run curl 127.0.0.1:port/test/500 --http2-prior-knowledge
        address = self.router_qdra.http_addresses[0] + "/test/500"
        out = self.run_curl(address, args=self.get_all_curl_args())
        self.assertIn('500 Internal Server Error', out)

    @unittest.skipIf(skip_test(), "Python 3.7 or greater, Quart 0.13.0 or greater and curl needed to run http2 tests")
    def test_get_image_png(self):
        # Run curl 127.0.0.1:port --output images/balanced-routing.png --http2-prior-knowledge
        image_file_name = '/balanced-routing.png'
        address = self.router_qdra.http_addresses[0] + "/images" + image_file_name
        self.run_curl(address, args=self.get_all_curl_args(['--output', self.router_qdra.outdir + image_file_name]))
        digest_of_server_file = get_digest(image_file(image_file_name[1:]))
        digest_of_response_file = get_digest(self.router_qdra.outdir + image_file_name)
        self.assertEqual(digest_of_server_file, digest_of_response_file)

    @unittest.skipIf(skip_test(), "Python 3.7 or greater, Quart 0.13.0 or greater and curl needed to run http2 tests")
    def test_get_image_jpg(self):
        # Run curl 127.0.0.1:port --output images/apache.jpg --http2-prior-knowledge
        image_file_name = '/apache.jpg'
        address = self.router_qdra.http_addresses[0] + "/images" + image_file_name
        self.run_curl(address, args=self.get_all_curl_args(['--output', self.router_qdra.outdir + image_file_name]))
        digest_of_server_file = get_digest(image_file(image_file(image_file_name[1:])))
        digest_of_response_file = get_digest(self.router_qdra.outdir + image_file_name)
        self.assertEqual(digest_of_server_file, digest_of_response_file)

    def check_listener_delete(self, client_addr, server_addr):
        # Run curl 127.0.0.1:port --http2-prior-knowledge
        # We are first making sure that the http request goes thru successfully.
        out = self.run_curl(client_addr, args=self.get_all_curl_args())
        ret_string = ""
        i = 0
        while (i < 1000):
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
            self.run_curl(client_addr, args=self.get_all_curl_args(), timeout=3)
        except Exception as e:
            request_timed_out = True
        self.assertTrue(request_timed_out)

        # Add back the listener and run a curl command to make sure that the newly added listener is
        # back up and running.
        create_result = qd_manager.create("org.apache.qpid.dispatch.httpListener", self.http_listener_props)
        sleep(2)
        out = self.run_curl(client_addr, args=self.get_all_curl_args())
        self.assertIn(ret_string, out)

    def check_connector_delete(self, client_addr, server_addr):
        # Run curl 127.0.0.1:port --http2-prior-knowledge
        # We are first making sure that the http request goes thru successfully.
        out = self.run_curl(client_addr, args=self.get_all_curl_args())

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
            out = self.run_curl(client_addr, args=self.get_all_curl_args(), timeout=5)
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
            if (len(connections) < 2):
                sleep(2)
            else:
                conn_present = True
        self.assertTrue(conn_present)

        out = self.run_curl(client_addr, args=self.get_all_curl_args())
        ret_string = ""
        i = 0
        while (i < 1000):
            ret_string += str(i) + ","
            i += 1
        self.assertIn(ret_string, out)


class Http2TestOneStandaloneRouter(Http2TestBase, CommonHttp2Tests):
    @classmethod
    def setUpClass(cls, tls_v12=False):
        super(Http2TestOneStandaloneRouter, cls).setUpClass(tls_v12)
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
        out = self.run_curl(address, args=self.get_all_curl_args())

        # Second request
        address = self.router_qdra.http_addresses[0] + "/myinfo"
        out = self.run_curl(address, args=self.get_all_curl_args(['-d', 'fname=Mickey&lname=Mouse', '-X', 'POST']))
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


class Http2TestTlsStandaloneRouter(Http2TestOneStandaloneRouter, RouterTestSslBase):
    """
    This test has one standalone router QDR. It has one httpListener with an associated SSL Profile and has one
    unencrypted httpConnector.
    Does not authenticate the curl client connecting to the httpListener i.e. the curl client does not present a
    client certificate.
    """
    @classmethod
    def setUpClass(cls, tls_v12=False):
        super(Http2TestTlsStandaloneRouter, cls).setUpClass(tls_v12)
        if skip_test():
            return
        cls.http2_server_name = "http2_server"
        os.environ["QUART_APP"] = "http2server:app"
        os.environ['SERVER_LISTEN_PORT'] = str(cls.tester.get_port())
        cls.http2_server = cls.tester.http2server(name=cls.http2_server_name,
                                                  listen_port=int(os.getenv('SERVER_LISTEN_PORT')),
                                                  py_string='python3',
                                                  server_file="http2_server.py")
        name = "http2-tls-standalone-router"
        cls.connector_name = 'connectorToBeDeleted'
        cls.connector_props = {
            'port': os.getenv('SERVER_LISTEN_PORT'),
            'address': 'examples',
            'host': '127.0.0.1',
            'protocolVersion': 'HTTP2',
            'name': cls.connector_name
        }
        cls.listener_ssl_profile = {'name': 'http-listener-ssl-profile',
                                    'caCertFile': cls.ssl_file('ca-certificate.pem'),
                                    'certFile': cls.ssl_file('server-certificate.pem'),
                                    'privateKeyFile': cls.ssl_file('server-private-key.pem'),
                                    'password': 'server-password'}
        if cls.tls_v12:
            cls.listener_ssl_profile['protocols'] = 'TLSv1.2'
        else:
            cls.listener_ssl_profile['protocols'] = 'TLSv1.3'

        config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'QDR'}),
            ('listener', {'port': cls.tester.get_port(), 'role': 'normal', 'host': '0.0.0.0'}),
            # Only the listener side has the SSL Profile, the connector side does not.
            ('httpListener', {'port': cls.tester.get_port(), 'address': 'examples',
                              'host': 'localhost', 'protocolVersion': 'HTTP2',
                              'sslProfile': 'http-listener-ssl-profile'}),
            ('httpConnector', cls.connector_props),
            ('sslProfile', cls.listener_ssl_profile)
        ])
        cls.router_qdra = cls.tester.qdrouterd(name, config, wait=True)
        if cls.tls_v12:
            cls.curl_args = ['--cacert', cls.ssl_file('ca-certificate.pem'), '--cert-type', 'PEM', '--tlsv1.2']
        else:
            cls.curl_args = ['--cacert', cls.ssl_file('ca-certificate.pem'), '--cert-type', 'PEM', '--tlsv1.3']
        sleep(2)


class Http2TestAuthenticatePeerOneRouter(Http2TestBase, RouterTestSslBase):
    """
    This test has one standalone router QDR. It has one httpListener with an associated SSL Profile and has one
    unencrypted httpConnector.
    The curl client does not present a client certificate.
    Tests to make sure client cannot connect without client cert since authenticatePeer is set to 'yes'.
    """
    @classmethod
    def setUpClass(cls):
        super(Http2TestBase, cls).setUpClass()
        if skip_test():
            return
        cls.http2_server_name = "http2_server"
        os.environ["QUART_APP"] = "http2server:app"
        os.environ['SERVER_LISTEN_PORT'] = str(cls.tester.get_port())
        cls.http2_server = cls.tester.http2server(name=cls.http2_server_name,
                                                  listen_port=int(os.getenv('SERVER_LISTEN_PORT')),
                                                  py_string='python3',
                                                  server_file="http2_server.py")
        name = "http2-tls-auth-peer-router"
        cls.connector_name = 'connectorToBeDeleted'
        cls.connector_props = {
            'port': os.getenv('SERVER_LISTEN_PORT'),
            'address': 'examples',
            'host': '127.0.0.1',
            'protocolVersion': 'HTTP2',
            'name': cls.connector_name
        }

        cls.listener_ssl_profile = {
            'name': 'http-listener-ssl-profile',
            'caCertFile': cls.ssl_file('ca-certificate.pem'),
            'certFile': cls.ssl_file('server-certificate.pem'),
            'privateKeyFile': cls.ssl_file('server-private-key.pem'),
            'password': 'server-password',
            'protocols': 'TLSv1.3'
        }

        config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'QDR'}),
            ('listener', {'port': cls.tester.get_port(), 'role': 'normal', 'host': '0.0.0.0'}),
            # Only the listener side has the SSL Profile, the connector side does not.
            ('httpListener', {'port': cls.tester.get_port(), 'address': 'examples',
                              'host': 'localhost',
                              # Requires peer to be authenticated.
                              'authenticatePeer': 'no',
                              'protocolVersion': 'HTTP2',
                              'sslProfile': 'http-listener-ssl-profile'}),
            ('httpConnector', cls.connector_props),
            ('sslProfile', cls.listener_ssl_profile)
        ])
        cls.router_qdra = cls.tester.qdrouterd(name, config, wait=True)
        cls.curl_args = ['--cacert', cls.ssl_file('ca-certificate.pem'), '--cert-type', 'PEM', '--tlsv1.3']
        sleep(2)

    @unittest.skipIf(skip_test(), "Python 3.7 or greater, Quart 0.13.0 or greater and curl needed to run http2 tests")
    # Tests the HTTP2 head request
    def test_head_request(self):
        # Run curl 127.0.0.1:port --http2-prior-knowledge --head
        # This test should fail because the curl client is not presenting a client cert but the router has
        # authenticatePeer set to true.
        address = self.router_qdra.http_addresses[0]
        out = self.run_curl(address, args=self.get_all_curl_args(['--head']), timeout=5)
        print(out)
        self.assertIn('HTTP/2 200', out)
        self.assertIn('server: hypercorn-h2', out)
        self.assertIn('content-type: text/html; charset=utf-8', out)


class Http2ATlsV12TestStandaloneRouter(Http2TestTlsStandaloneRouter, RouterTestSslBase):
    """
    This test has one standalone router QDR. It has one httpListener with an associated SSL Profile and has one
    unencrypted httpConnector.
    Does not authenticate the curl client connecting to the httpListener i.e. the curl client does not present a
    client certificate.
    Tests to make sure TLS 1.2 works.
    """
    @classmethod
    def setUpClass(cls, tls_v12=True):
        super(Http2ATlsV12TestStandaloneRouter, cls).setUpClass(tls_v12)


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
        server_port = cls.tester.get_port()
        if os.getenv('SERVER_TLS'):
            del os.environ["SERVER_TLS"]
        os.environ['SERVER_LISTEN_PORT'] = str(server_port)
        cls.http2_server = cls.tester.http2server(name=cls.http2_server_name,
                                                  listen_port=int(server_port),
                                                  py_string='python3',
                                                  server_file="http2_server.py")
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
            'port': server_port,
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
                           'port': inter_router_port})

        ])

        cls.router_qdra = cls.tester.qdrouterd("http2-test-router-A", config_qdra, wait=True)
        cls.router_qdrb = cls.tester.qdrouterd("http2-test-router-B", config_qdrb, wait=True)

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
        self.run_curl(address, args=self.get_all_curl_args())
        address = self.router_qdra.http_addresses[0] + "/myinfo"

        # Second request
        out = self.run_curl(address, args=self.get_all_curl_args(['-d', 'fname=Mickey&lname=Mouse', '-X', 'POST']))
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


class Http2TestTlsTwoRouter(Http2TestTwoRouter, RouterTestSslBase):
    """
    Client authentication is required for curl to talk to the router http port.
    """
    @classmethod
    def setUpClass(cls):
        super(Http2TestBase, cls).setUpClass()
        if skip_test():
            return
        cls.http2_server_name = "http2_server_tls"
        os.environ["SERVER_TLS"] = "yes"
        os.environ["QUART_APP"] = "http2server:app"
        os.environ['SERVER_LISTEN_PORT'] = str(cls.tester.get_port())
        os.environ['SERVER_CERTIFICATE'] = cls.ssl_file('server-certificate.pem')
        os.environ['SERVER_PRIVATE_KEY'] = cls.ssl_file('server-private-key-no-pass.pem')
        os.environ['SERVER_CA_CERT'] = cls.ssl_file('ca-certificate.pem')
        cls.http2_server = cls.tester.http2server(name=cls.http2_server_name,
                                                  listen_port=os.getenv('SERVER_LISTEN_PORT'),
                                                  wait=False,
                                                  py_string='python3',
                                                  server_file="http2_server.py",
                                                  stdin=PIPE,
                                                  stdout=PIPE,
                                                  stderr=PIPE)
        inter_router_port = cls.tester.get_port()
        cls.listener_name = 'listenerToBeDeleted'
        cls.http_listener_props = {'port': cls.tester.get_port(),
                                   'address': 'examples',
                                   'host': 'localhost',
                                   'name': cls.listener_name,
                                   'protocolVersion': 'HTTP2',
                                   'authenticatePeer': 'yes',
                                   'sslProfile': 'http-listener-ssl-profile'}
        config_qdra = Qdrouterd.Config([
            ('router', {'mode': 'interior', 'id': 'QDR.A'}),
            ('listener', {'port': cls.tester.get_port(), 'role': 'normal', 'host': '0.0.0.0'}),
            # curl will connect to this httpListener and run the tests.
            ('httpListener', cls.http_listener_props),
            ('sslProfile', {'name': 'http-listener-ssl-profile',
                            'caCertFile': cls.ssl_file('ca-certificate.pem'),
                            'certFile': cls.ssl_file('server-certificate.pem'),
                            'privateKeyFile': cls.ssl_file('server-private-key.pem'),
                            'protocols': 'TLSv1.3',
                            'password': 'server-password'}),
            ('listener', {'role': 'inter-router', 'port': inter_router_port})
        ])

        cls.connector_name = 'connectorToBeDeleted'
        cls.connector_props = {
            'port': os.getenv('SERVER_LISTEN_PORT'),
            'address': 'examples',
            'host': 'localhost',
            'protocolVersion': 'HTTP2',
            'name': cls.connector_name,
            # Verifies host name. The host name in the certificate sent by the server must match 'localhost'
            'verifyHostname': 'yes',
            'sslProfile': 'http-connector-ssl-profile'
        }
        config_qdrb = Qdrouterd.Config([
            ('router', {'mode': 'interior', 'id': 'QDR.B'}),
            ('listener', {'port': cls.tester.get_port(), 'role': 'normal', 'host': '0.0.0.0'}),
            ('httpConnector', cls.connector_props),
            ('connector', {'name': 'connectorToA', 'role': 'inter-router',
                           'port': inter_router_port,
                           'verifyHostname': 'no'}),
            ('sslProfile', {'name': 'http-connector-ssl-profile',
                            'caCertFile': cls.ssl_file('ca-certificate.pem'),
                            'certFile': cls.ssl_file('client-certificate.pem'),
                            'privateKeyFile': cls.ssl_file('client-private-key.pem'),
                            'protocols': 'TLSv1.3',
                            'password': 'client-password'}),
        ])

        cls.router_qdra = cls.tester.qdrouterd("http2-two-router-tls-A", config_qdra, wait=True)
        cls.router_qdrb = cls.tester.qdrouterd("http2-two-router-tls-B", config_qdrb)
        cls.router_qdra.wait_router_connected('QDR.B')
        cls.router_qdrb.wait_router_connected('QDR.A')

        # curl will use these additional args to connect to the router.
        cls.curl_args = ['--cacert', cls.ssl_file('ca-certificate.pem'), '--cert-type', 'PEM',
                         '--cert', cls.ssl_file('client-certificate.pem') + ":client-password",
                         '--key', cls.ssl_file('client-private-key.pem')]
        sleep(1)

    @unittest.skipIf(skip_test(), "Python 3.7 or greater, Quart 0.13.0 or greater and curl needed to run http2 tests")
    # Tests the HTTP2 head request without http2-prior-knowledge
    def test_head_request_no_http2_prior_knowledge(self):
        # Run curl 127.0.0.1:port --head
        # In this test, we do not use curl's --http2-prior-knowledge flag. This means that curl client will first offer
        # http1 and http2 (h2) as the protocol list in ClientHello ALPN (since the curl client by default does
        # ALPN over TLS). The router will respond back with just h2 in its
        # ALPN response. curl will then know that the server (router) speaks only http2 and hence when the TLS handshake
        # between curl and the server (router) is successful, curl starts speaking http2 to the server (router).
        # If this test works, it is proof that ALPN over TLS is working.
        address = self.router_qdra.http_addresses[0]
        out = self.run_curl(address, args=self.get_all_curl_args(['--head']), http2_prior_knowledge=False)
        self.assertIn('HTTP/2 200', out)
        self.assertIn('server: hypercorn-h2', out)
        self.assertIn('content-type: text/html; charset=utf-8', out)

    @unittest.skipIf(skip_test(), "Python 3.7 or greater, Quart 0.13.0 or greater and curl needed to run http2 tests")
    # Tests the HTTP2 head request without APLN and without http2-prior-knowledge
    def test_head_request_no_alpn_no_http2_prior_knowledge(self):
        # Run curl 127.0.0.1:port --head --no-alpn
        # In this test, we do not use curl's --http2-prior-knowledge flag but instead use the --no-alpn flag.
        # This means that curl client will not offer an ALPN protocol at all. The router (server) sends back 'h2'
        # in its ALPN response which is ignored by the curl client.
        # The TLS handshake is successful and curl receives a http2 settings frame from the router which it does
        # not understand. Hence it complains with the error message
        # 'Received HTTP/0.9 when not allowed'
        address = self.router_qdra.http_addresses[0]
        out = self.run_curl(address,
                            args=self.get_all_curl_args(['--head']),
                            http2_prior_knowledge=False,
                            no_alpn=True, assert_status=False)
        self.assertIn('Received HTTP/0.9 when not allowed', out)


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
        out = self.run_curl(address, args=self.get_all_curl_args(["-i"]))
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
        out = self.run_curl(address, args=self.get_all_curl_args(['-X', 'POST',
                                                                  '-H', 'Content-Type: multipart/form-data',
                                                                  '-F', 'data=@' + image_file('test.jpg')]))
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
        out = self.run_curl(address, args=self.get_all_curl_args(['-X', 'POST',
                                                                  '-H', 'Content-Type: multipart/form-data',
                                                                  '-F', 'data=@' + image_file('test.jpg')]))
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


class Http2TlsQ2TwoRouterTest(RouterTestPlainSaslCommon, Http2TestBase, RouterTestSslBase):
    @staticmethod
    def sasl_file(name):
        return os.path.join(DIR, 'sasl_files', name)

    @classmethod
    def setUpClass(cls):
        super(Http2TlsQ2TwoRouterTest, cls).setUpClass()
        if skip_h2_test():
            return
        if not SASL.extended():
            return

        a_listener_port = cls.tester.get_port()
        b_listener_port = cls.tester.get_port()

        super(Http2TlsQ2TwoRouterTest, cls).createSaslFiles()

        # Start the HTTP2 Server
        cls.http2_server_name = "http2_server"
        os.environ["SERVER_TLS"] = "yes"
        os.environ["QUART_APP"] = "http2server:app"
        os.environ['SERVER_LISTEN_PORT'] = str(cls.tester.get_port())
        os.environ['SERVER_CERTIFICATE'] = cls.ssl_file('server-certificate.pem')
        os.environ['SERVER_PRIVATE_KEY'] = cls.ssl_file('server-private-key-no-pass.pem')
        os.environ['SERVER_CA_CERT'] = cls.ssl_file('ca-certificate.pem')
        env = {
            **os.environ
        }

        cls.http2_server = cls.tester.http2server(name=cls.http2_server_name,
                                                  listen_port=int(os.getenv('SERVER_LISTEN_PORT')),
                                                  wait=False,
                                                  py_string='python3',
                                                  server_file="http2_slow_q2_server.py",
                                                  stdin=PIPE,
                                                  stdout=PIPE,
                                                  stderr=PIPE,
                                                  env=env)

        config_qdra = Qdrouterd.Config([
            ('router', {'id': 'QDR.A', 'mode': 'interior'}),
            ('listener', {'host': '0.0.0.0', 'role': 'normal', 'port': cls.tester.get_port(),
                          'authenticatePeer': 'no'}),
            # curl will connect to this httpListener and run the tests.
            ('httpListener', {'port': cls.tester.get_port(),
                              'address': 'examples',
                              'host': 'localhost',
                              'protocolVersion': 'HTTP2',
                              'authenticatePeer': 'yes',
                              'sslProfile': 'http-listener-ssl-profile'}),
            ('sslProfile', {'name': 'http-listener-ssl-profile',
                            'caCertFile': cls.ssl_file('ca-certificate.pem'),
                            'certFile': cls.ssl_file('server-certificate.pem'),
                            'privateKeyFile': cls.ssl_file('server-private-key.pem'),
                            'protocols': 'TLSv1.3',
                            'password': 'server-password'}),
            ('sslProfile', {'name': 'inter-router-client-ssl-profile',
                            'caCertFile': cls.ssl_file('ca-certificate.pem')}),
            ('connector', {'host': 'localhost', 'role': 'inter-router', 'port': b_listener_port,
                           'sslProfile': 'inter-router-client-ssl-profile',
                           # Provide a sasl user name and password to connect to QDR.B
                           'saslMechanisms': 'PLAIN',
                           'saslUsername': 'test@domain.com',
                           'saslPassword': 'file:' + cls.sasl_file('password.txt')}),
        ])

        cls.connector_name = 'connectorToBeDeleted'
        cls.connector_props = {
            'port': os.getenv('SERVER_LISTEN_PORT'),
            'address': 'examples',
            'host': 'localhost',
            'protocolVersion': 'HTTP2',
            'name': cls.connector_name,
            'verifyHostname': 'no',
            'sslProfile': 'http-connector-ssl-profile'
        }

        config_qdrb = Qdrouterd.Config([
            # This router will act like a client. First an SSL connection will be established and then
            # we will have SASL plain authentication over SSL on the inter-router connection.
            ('router', {'mode': 'interior', 'id': 'QDR.B', 'saslConfigName': 'tests-mech-PLAIN',
                        'saslConfigDir': os.getcwd()}),
            ('listener', {'host': '0.0.0.0', 'role': 'inter-router', 'port': b_listener_port,
                          'sslProfile': 'inter-router-server-ssl-profile', 'maxSessionFrames': '10',
                          'saslMechanisms': 'PLAIN', 'authenticatePeer': 'yes'}),
            ('httpConnector', cls.connector_props),
            ('listener', {'host': '0.0.0.0', 'role': 'normal', 'port': cls.tester.get_port()}),
            ('sslProfile', {'name': 'http-connector-ssl-profile',
                            'caCertFile': cls.ssl_file('ca-certificate.pem'),
                            'certFile': cls.ssl_file('client-certificate.pem'),
                            'privateKeyFile': cls.ssl_file('client-private-key.pem'),
                            'protocols': 'TLSv1.3',
                            'password': 'client-password'}),
            ('sslProfile', {'name': 'inter-router-server-ssl-profile',
                            'certFile': cls.ssl_file('server-certificate.pem'),
                            'privateKeyFile': cls.ssl_file('server-private-key.pem'),
                            'ciphers': 'ECDH+AESGCM:DH+AESGCM:ECDH+AES256:DH+AES256:ECDH+AES128:DH+AES:RSA+AESGCM:RSA+AES:!aNULL:!MD5:!DSS',
                            'protocols': 'TLSv1.1 TLSv1.2',
                            'password': 'server-password'})
        ])

        cls.router_qdra = cls.tester.qdrouterd("http2-two-router-tls-q2-A", config_qdra, wait=False)
        cls.router_qdrb = cls.tester.qdrouterd("http2-two-router-tls-q2-B", config_qdrb, wait=False)
        cls.router_qdra.wait_router_connected('QDR.B')
        # curl will use these additional args to connect to the router.
        cls.curl_args = ['--cacert', cls.ssl_file('ca-certificate.pem'), '--cert-type', 'PEM',
                         '--cert', cls.ssl_file('client-certificate.pem') + ":client-password",
                         '--key', cls.ssl_file('client-private-key.pem')]
        sleep(3)

    @unittest.skipIf(skip_h2_test(),
                     "Python 3.7 or greater, hyper-h2 and curl needed to run hyperhttp2 tests")
    def test_q2_block_unblock(self):
        # curl  -X POST -H "Content-Type: multipart/form-data"  -F "data=@/home/gmurthy/opensource/test.jpg"
        # http://127.0.0.1:<port?>/upload --http2-prior-knowledge
        address = self.router_qdra.http_addresses[0] + "/upload"
        out = self.run_curl(address, args=self.get_all_curl_args(['-X', 'POST',
                                                                  '-H', 'Content-Type: multipart/form-data',
                                                                  '-F', 'data=@' + image_file('test.jpg')]))
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


class Http2TwoRouterTlsOverSASLExternal(RouterTestPlainSaslCommon,
                                        Http2TestBase,
                                        CommonHttp2Tests):

    @staticmethod
    def ssl_file(name):
        return os.path.join(DIR, 'ssl_certs', name)

    @staticmethod
    def sasl_file(name):
        return os.path.join(DIR, 'sasl_files', name)

    @classmethod
    def setUpClass(cls):
        """
        This test has two routers QDR.A and QDR.B, they talk to each other over TLS and are SASL authenticated.
        QDR.A has a httpListener (with an sslProfile) which accepts curl connections. curl talks to the httpListener
        over TLS. Client authentication is required for curl to talk to the httpListener.
        QDR.B has a httpConnector to an http2 Server and they talk to each other over an encrypted connection.
        """
        super(Http2TwoRouterTlsOverSASLExternal, cls).setUpClass()
        if skip_test():
            return

        if not SASL.extended():
            return

        super(Http2TwoRouterTlsOverSASLExternal, cls).createSaslFiles()

        cls.routers = []

        a_listener_port = cls.tester.get_port()
        b_listener_port = cls.tester.get_port()

        # Start the HTTP2 Server
        cls.http2_server_name = "http2_server"
        os.environ["SERVER_TLS"] = "yes"
        os.environ["QUART_APP"] = "http2server:app"
        os.environ['SERVER_LISTEN_PORT'] = str(cls.tester.get_port())
        os.environ['SERVER_CERTIFICATE'] = cls.ssl_file('server-certificate.pem')
        os.environ['SERVER_PRIVATE_KEY'] = cls.ssl_file('server-private-key-no-pass.pem')
        os.environ['SERVER_CA_CERT'] = cls.ssl_file('ca-certificate.pem')
        env = {
            **os.environ
        }
        cls.http2_server = cls.tester.http2server(name=cls.http2_server_name,
                                                  listen_port=os.getenv('SERVER_LISTEN_PORT'),
                                                  wait=False,
                                                  py_string='python3',
                                                  server_file="http2_server.py",
                                                  stdin=PIPE,
                                                  stdout=PIPE,
                                                  stderr=PIPE,
                                                  env=env)
        config_qdra = Qdrouterd.Config([
            ('listener', {'host': '0.0.0.0', 'role': 'inter-router', 'port': a_listener_port,
                          'sslProfile': 'inter-router-server-ssl-profile',
                          'saslMechanisms': 'PLAIN', 'authenticatePeer': 'yes'}),
            ('listener', {'host': '0.0.0.0', 'role': 'normal', 'port': cls.tester.get_port(),
                          'authenticatePeer': 'no'}),
            # curl will connect to this httpListener and run the tests.
            ('httpListener', {'port': cls.tester.get_port(),
                              'address': 'examples',
                              'host': 'localhost',
                              'protocolVersion': 'HTTP2',
                              'authenticatePeer': 'yes',
                              'sslProfile': 'http-listener-ssl-profile'}),
            ('sslProfile', {'name': 'http-listener-ssl-profile',
                            'caCertFile': cls.ssl_file('ca-certificate.pem'),
                            'certFile': cls.ssl_file('server-certificate.pem'),
                            'privateKeyFile': cls.ssl_file('server-private-key.pem'),
                            'protocols': 'TLSv1.3',
                            'password': 'server-password'}),
            ('sslProfile', {'name': 'inter-router-server-ssl-profile',
                            'certFile': cls.ssl_file('server-certificate.pem'),
                            'privateKeyFile': cls.ssl_file('server-private-key.pem'),
                            'ciphers': 'ECDH+AESGCM:DH+AESGCM:ECDH+AES256:DH+AES256:ECDH+AES128:DH+AES:RSA+AESGCM:RSA+AES:!aNULL:!MD5:!DSS',
                            'protocols': 'TLSv1.1 TLSv1.2',
                            'password': 'server-password'}),
            ('router', {'id': 'QDR.A',
                        'mode': 'interior',
                        'saslConfigName': 'tests-mech-PLAIN',
                        'saslConfigDir': os.getcwd()}),
        ])

        cls.connector_name = 'connectorToBeDeleted'
        cls.connector_props = {
            'port': os.getenv('SERVER_LISTEN_PORT'),
            'address': 'examples',
            'host': 'localhost',
            'protocolVersion': 'HTTP2',
            'name': cls.connector_name,
            'verifyHostname': 'no',
            'sslProfile': 'http-connector-ssl-profile'
        }

        config_qdrb = Qdrouterd.Config([
            # This router will act like a client. First an SSL connection will be established and then
            # we will have SASL plain authentication over SSL on the inter-router connection.
            ('router', {'mode': 'interior', 'id': 'QDR.B'}),
            ('httpConnector', cls.connector_props),
            ('connector', {'host': 'localhost', 'role': 'inter-router', 'port': a_listener_port,
                           'sslProfile': 'inter-router-client-ssl-profile',
                           # Provide a sasl user name and password to connect to QDR.X
                           'saslMechanisms': 'PLAIN',
                           'saslUsername': 'test@domain.com',
                           'saslPassword': 'file:' + cls.sasl_file('password.txt')}),
            ('listener', {'host': '0.0.0.0', 'role': 'normal', 'port': b_listener_port}),
            ('sslProfile', {'name': 'http-connector-ssl-profile',
                            'caCertFile': cls.ssl_file('ca-certificate.pem'),
                            'certFile': cls.ssl_file('client-certificate.pem'),
                            'privateKeyFile': cls.ssl_file('client-private-key.pem'),
                            'protocols': 'TLSv1.3',
                            'password': 'client-password'}),
            ('sslProfile', {'name': 'inter-router-client-ssl-profile',
                            'caCertFile': cls.ssl_file('ca-certificate.pem')}),
        ])
        cls.router_qdra = cls.tester.qdrouterd("http2-two-router-tls-A", config_qdra, wait=True)
        cls.router_qdrb = cls.tester.qdrouterd("http2-two-router-tls-B", config_qdrb)
        cls.router_qdrb.wait_router_connected('QDR.A')
        # curl will use these additional args to connect to the router.
        cls.curl_args = ['--cacert', cls.ssl_file('ca-certificate.pem'), '--cert-type', 'PEM',
                         '--cert', cls.ssl_file('client-certificate.pem') + ":client-password",
                         '--key', cls.ssl_file('client-private-key.pem')]
        sleep(2)
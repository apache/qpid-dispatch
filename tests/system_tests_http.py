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

import unittest2 as unittest
import os
import threading
import sys
import ssl

try:
    from urllib2 import urlopen, build_opener, HTTPSHandler
    from urllib2 import HTTPError, URLError
except ImportError:
    # python3
    from urllib.request import urlopen, build_opener, HTTPSHandler
    from urllib.error import HTTPError, URLError

from system_test import TIMEOUT, Process
from subprocess import PIPE, STDOUT
from system_test import TestCase, Qdrouterd, main_module, DIR


class RouterTestHttp(TestCase):

    @staticmethod
    def ssl_file(name):
        return os.path.join(DIR, 'ssl_certs', name)

    @classmethod
    def get(cls, url):
        return urlopen(url, cafile=cls.ssl_file('ca-certificate.pem')).read().decode('utf-8')

    @classmethod
    def get_cert(cls, url):
        context = ssl.create_default_context()
        context.load_cert_chain(cls.ssl_file('client-certificate.pem'),
                                cls.ssl_file('client-private-key.pem'),
                                'client-password')
        context.load_verify_locations(cls.ssl_file('ca-certificate.pem'))
        opener = build_opener(HTTPSHandler(context=context))
        return opener.open(url).read().decode('utf-8')

    def run_qdmanage(self, cmd, input=None, expect=Process.EXIT_OK, address=None):
        p = self.popen(
            ['qdmanage'] + cmd.split(' ') + ['--bus', address or self.address(), '--indent=-1', '--timeout', str(TIMEOUT)],
            stdin=PIPE, stdout=PIPE, stderr=STDOUT, expect=expect,
            universal_newlines=True)
        out = p.communicate(input)[0]
        try:
            p.teardown()
        except Exception as e:
            raise Exception(out if out else str(e))
        return out

    def assert_get(self, url):
        self.assertEqual(u'HTTP test\n', self.get("%s/system_tests_http.txt" % url))

    def assert_get_cert(self, url):
        self.assertEqual(u'HTTP test\n', self.get_cert("%s/system_tests_http.txt" % url))

    def test_listen_error(self):
        """Make sure a router exits if an initial HTTP listener fails, doesn't hang"""
        listen_port = self.get_port()
        config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'bad'}),
            ('listener', {'port': listen_port, 'maxFrameSize': '2048', 'stripAnnotations': 'no'}),
            ('listener', {'port': listen_port, 'http':True})])
        r = Qdrouterd(name="expect_fail", config=config, wait=False)
        self.assertEqual(1, r.wait())

    def test_http_listener_delete(self):
        name = 'delete_listener'
        normal_listen_port = self.get_port()
        http_delete_listen_port = self.get_port()
        config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'A'}),
            ('listener', {'port': normal_listen_port, 'maxFrameSize': '2048', 'stripAnnotations': 'no'}),
            ('listener', {'name': name, 'port': http_delete_listen_port, 'http': True})])
        router = self.qdrouterd(name="expect_fail_1", config=config, wait=True)
        exception_occurred = False

        def address():
            return router.addresses[0]

        long_type = 'org.apache.qpid.dispatch.listener'
        delete_command = 'DELETE --type=' + long_type + ' --name=' + name
        try:
            out = self.run_qdmanage(delete_command, address=address())
        except Exception as e:
            exception_occurred = True
            self.assertTrue("BadRequestStatus: HTTP listeners cannot be deleted" in str(e))

        self.assertTrue(exception_occurred)

    def test_http_get(self):
        config = Qdrouterd.Config([
            ('router', {'id': 'QDR.HTTP'}),
            # httpRoot has been deprecated. We are using it here to test backward compatibility.
            ('listener', {'port': self.get_port(), 'httpRoot': os.path.dirname(__file__)}),
            ('listener', {'port': self.get_port(), 'httpRootDir': os.path.dirname(__file__)}),
        ])
        r = self.qdrouterd('http-test-router', config)

        def test(port):
            self.assert_get("http://localhost:%d" % port)
            self.assertRaises(HTTPError, urlopen, "http://localhost:%d/nosuch" % port)

        # Sequential calls on multiple ports
        for port in r.ports: test(port)

        # Concurrent calls on multiple ports
        class TestThread(threading.Thread):
            def __init__(self, port):
                threading.Thread.__init__(self)
                self.port, self.ex = port, None
                self.start()
            def run(self):
                try: test(self.port)
                except Exception as e: self.ex = e
        threads = [TestThread(p) for p in r.ports + r.ports]
        for t in threads: t.join()
        for t in threads:
            if t.ex: raise t.ex

        # https not configured
        self.assertRaises(URLError, urlopen, "https://localhost:%d/nosuch" % r.ports[0])

    def test_http_metrics(self):

        if not sys.version_info >= (2, 7):
            return

        config = Qdrouterd.Config([
            ('router', {'id': 'QDR.METRICS'}),
            ('listener', {'port': self.get_port(), 'http': 'yes'}),
            ('listener', {'port': self.get_port(), 'httpRootDir': os.path.dirname(__file__)}),
        ])
        r = self.qdrouterd('metrics-test-router', config)

        def test(port):
            result = urlopen("http://localhost:%d/metrics" % port, cafile=self.ssl_file('ca-certificate.pem'))
            self.assertEqual(200, result.getcode())
            data = result.read().decode('utf-8')
            assert('connections' in data)
            assert('deliveries_ingress' in data)

        # Sequential calls on multiple ports
        for port in r.ports: test(port)

        # Concurrent calls on multiple ports
        class TestThread(threading.Thread):
            def __init__(self, port):
                threading.Thread.__init__(self)
                self.port, self.ex = port, None
                self.start()
            def run(self):
                try: test(self.port)
                except Exception as e: self.ex = e
        threads = [TestThread(p) for p in r.ports + r.ports]
        for t in threads: t.join()
        for t in threads:
            if t.ex: raise t.ex

    def test_http_healthz(self):

        if not sys.version_info >= (2, 7):
            return

        config = Qdrouterd.Config([
            ('router', {'id': 'QDR.HEALTHZ'}),
            ('listener', {'port': self.get_port(), 'http': 'yes'}),
            ('listener', {'port': self.get_port(), 'httpRootDir': os.path.dirname(__file__)}),
        ])
        r = self.qdrouterd('metrics-test-router', config)

        def test(port):
            result = urlopen("http://localhost:%d/healthz" % port, cafile=self.ssl_file('ca-certificate.pem'))
            self.assertEqual(200, result.getcode())

        # Sequential calls on multiple ports
        for port in r.ports: test(port)

        # Concurrent calls on multiple ports
        class TestThread(threading.Thread):
            def __init__(self, port):
                threading.Thread.__init__(self)
                self.port, self.ex = port, None
                self.start()
            def run(self):
                try: test(self.port)
                except Exception as e: self.ex = e
        threads = [TestThread(p) for p in r.ports + r.ports]
        for t in threads: t.join()
        for t in threads:
            if t.ex: raise t.ex

    def test_https_get(self):
        def listener(**kwargs):
            args = dict(kwargs)
            args.update({'port': self.get_port(), 'httpRootDir': os.path.dirname(__file__)})
            return ('listener', args)

        config = Qdrouterd.Config([
            ('router', {'id': 'QDR.HTTPS'}),
            ('sslProfile', {'name': 'simple-ssl',
                            'caCertFile': self.ssl_file('ca-certificate.pem'),
                            'certFile': self.ssl_file('server-certificate.pem'),
                            'privateKeyFile': self.ssl_file('server-private-key.pem'),
                            'ciphers': 'ECDH+AESGCM:DH+AESGCM:ECDH+AES256:DH+AES256:ECDH+AES128:DH+AES:RSA+AESGCM:RSA+AES:!aNULL:!MD5:!DSS',
                            'password': 'server-password'
            }),
            listener(sslProfile='simple-ssl', requireSsl=False, authenticatePeer=False),
            listener(sslProfile='simple-ssl', requireSsl=True, authenticatePeer=False),
            listener(sslProfile='simple-ssl', requireSsl=True, authenticatePeer=True)])
        # saslMechanisms='EXTERNAL'

        r = self.qdrouterd('https-test-router', config)
        r.wait_ready()

        self.assert_get("https://localhost:%s" % r.ports[0])
        # requireSsl=false Allows simple-ssl HTTP
        self.assert_get("http://localhost:%s" % r.ports[0])

        self.assert_get("https://localhost:%s" % r.ports[1])
        # requireSsl=True does not allow simple-ssl HTTP
        self.assertRaises(Exception, self.assert_get, "http://localhost:%s" % r.ports[1])

        # authenticatePeer=True requires a client cert
        self.assertRaises(URLError, self.assert_get, "https://localhost:%s" % r.ports[2])
        # Provide client cert
        self.assert_get_cert("https://localhost:%d" % r.ports[2])


if __name__ == '__main__':
    unittest.main(main_module())

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

import unittest, os, json, threading, sys, ssl, urllib2
import ssl
import run
from subprocess import PIPE, Popen, STDOUT
from system_test import TestCase, Qdrouterd, main_module, DIR, TIMEOUT, Process
from qpid_dispatch.management.client import Node

class RouterTestHttp(TestCase):

    @staticmethod
    def ssl_file(name):
        return os.path.join(DIR, 'ssl_certs', name)

    @classmethod
    def get(cls, url):
        return urllib2.urlopen(url, cafile=cls.ssl_file('ca-certificate.pem')).read()

    @classmethod
    def get_cert(cls, url):
        context = ssl.create_default_context()
        context.load_cert_chain(cls.ssl_file('client-certificate.pem'),
                                cls.ssl_file('client-private-key.pem'),
                                'client-password')
        context.load_verify_locations(cls.ssl_file('ca-certificate.pem'))
        opener = urllib2.build_opener(urllib2.HTTPSHandler(context=context))
        return opener.open(url).read()

    def assert_get(self, url):
        self.assertEqual("HTTP test\n", self.get("%s/system_tests_http.txt" % url))

    def assert_get_cert(self, url):
        self.assertEqual("HTTP test\n", self.get_cert("%s/system_tests_http.txt" % url))

    def test_http_get(self):
        config = Qdrouterd.Config([
            ('router', {'id': 'QDR.HTTP'}),
            ('listener', {'port': self.get_port(), 'httpRoot': os.path.dirname(__file__)}),
            ('listener', {'port': self.get_port(), 'httpRoot': os.path.dirname(__file__)}),
        ])
        r = self.qdrouterd('http-test-router', config)

        def test(port):
            self.assert_get("http://localhost:%d" % port)
            self.assertRaises(urllib2.HTTPError, urllib2.urlopen, "http://localhost:%d/nosuch" % port)

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
                except Exception, e: self.ex = e
        threads = [TestThread(p) for p in r.ports + r.ports]
        for t in threads: t.join()
        for t in threads:
            if t.ex: raise t.ex

        # https not configured
        self.assertRaises(urllib2.URLError, urllib2.urlopen, "https://localhost:%d/nosuch" % r.ports[0])

    def test_https_get(self):
        if run.use_valgrind(): self.skipTest("too slow for valgrind")

        def listener(**kwargs):
            args = dict(kwargs)
            args.update({'port': self.get_port(), 'httpRoot': os.path.dirname(__file__)})
            return ('listener', args)

        config = Qdrouterd.Config([
            ('router', {'id': 'QDR.HTTPS'}),
            ('sslProfile', {'name': 'simple-ssl',
                            'certDb': self.ssl_file('ca-certificate.pem'),
                            'certFile': self.ssl_file('server-certificate.pem'),
                            'keyFile': self.ssl_file('server-private-key.pem'),
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
        self.assertRaises(urllib2.URLError, self.assert_get, "https://localhost:%s" % r.ports[2])
        # Provide client cert
        self.assert_get_cert("https://localhost:%d" % r.ports[2])

if __name__ == '__main__':
    unittest.main(main_module())

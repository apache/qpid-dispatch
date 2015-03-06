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
# under the License
#

import os
import re
import system_test
import unittest
from subprocess import PIPE
from proton import Url

class QdstatTest(system_test.TestCase):
    """Test qdstat tool output"""

    @staticmethod
    def ssl_file(name):
        return os.path.join(os.path.dirname(__file__), 'ssl_certs', name)

    @classmethod
    def setUpClass(cls):
        super(QdstatTest, cls).setUpClass()
        config = system_test.Qdrouterd.Config([
            ('ssl-profile', {'name': 'server-ssl-strict',
                             'cert-db': cls.ssl_file('ca-certificate.pem'),
                             'cert-file': cls.ssl_file('server-certificate.pem'),
                             'key-file': cls.ssl_file('server-private-key.pem'),
                             'password': 'server-password',
                             'allow-unsecured': False,
                             'require-peer-auth': False}),
            ('ssl-profile', {'name': 'server-ssl-unsecured',
                             'cert-db': cls.ssl_file('ca-certificate.pem'),
                             'cert-file': cls.ssl_file('server-certificate.pem'),
                             'key-file': cls.ssl_file('server-private-key.pem'),
                             'password': 'server-password',
                             'allow-unsecured': True,
                             'require-peer-auth': False}),
            ('ssl-profile', {'name': 'server-ssl-auth',
                             'cert-db': cls.ssl_file('ca-certificate.pem'),
                             'cert-file': cls.ssl_file('server-certificate.pem'),
                             'key-file': cls.ssl_file('server-private-key.pem'),
                             'password': 'server-password',
                             'allow-unsecured': False,
                             'require-peer-auth': True}),
            ('listener', {'port': cls.tester.get_port()}),
            ('listener', {'port': cls.tester.get_port(), 'ssl-profile': 'server-ssl-strict'}),
            ('listener', {'port': cls.tester.get_port(), 'ssl-profile': 'server-ssl-unsecured'}),
            ('listener', {'port': cls.tester.get_port(), 'ssl-profile': 'server-ssl-auth'})
        ])
        cls.router = cls.tester.qdrouterd('test-router', config)

    def run_qdstat(self, args, regexp=None, address=None):
        p = self.popen(['qdstat', '--bus', str(address or self.router.addresses[0])] + args,
                       name='qdstat-'+self.id(), stdout=PIPE, expect=None)
        out = p.communicate()[0]
        assert p.returncode == 0, \
            "qdstat exit status %s, output:\n%s" % (p.returncode, out)
        if regexp: assert re.search(regexp, out, re.I), "Can't find '%s' in '%s'" % (regexp, out)
        return out

    def test_help(self):
        self.run_qdstat(['--help'], r'Usage: qdstat')

    def test_general(self):
        self.run_qdstat(['--general'], r'(?s)Router Statistics.*Mode\s*Standalone')

    def test_connections(self):
        self.run_qdstat(['--connections'], r'state.*host.*container')

    def test_links(self):
        self.run_qdstat(['--links'], r'endpoint.*out.*local.*temp.')

    def test_nodes(self):
        self.run_qdstat(['--nodes'], r'Router Nodes')

    def test_address(self):
        self.run_qdstat(['--address'], r'\$management')

    def test_memory(self):
        self.run_qdstat(['--memory'], r'qd_address_t\s+[0-9]+')

    def test_log(self):
        self.run_qdstat(['--log',  '--limit=5'], r'AGENT \(trace\).*GET-LOG')

    def test_ssl(self):
        """
        Test the matrix of dispatch and client SSL configuratoin and ensure we 
        can/can't connect as expected.
        """

        def do_test(url, args):
            self.run_qdstat(['--general'] + args,
                            regexp=r'(?s)Router Statistics.*Mode\s*Standalone',
                            address=str(url))

        trustfile = ['--ssl-trustfile', self.ssl_file('ca-certificate.pem')]
        bad_trustfile = ['--ssl-trustfile', self.ssl_file('bad-ca-certificate.pem')]
        client_cert = ['--ssl-certificate', self.ssl_file('client-certificate.pem')]
        client_key = ['--ssl-key', self.ssl_file('client-private-key.pem')]
        client_pass = ['--ssl-password', 'client-password']
        client_cert_all = client_cert + client_key + client_pass

        addrs = [self.router.addresses[i] for i in xrange(4)];
        none, strict, unsecured, auth = addrs
        none_s, strict_s, unsecured_s, auth_s = (Url(a, scheme="amqps") for a in addrs)

        # Non-SSL enabled listener should fail SSL connections.
        do_test(none, [])
        self.assertRaises(AssertionError, do_test, none_s, [])
        self.assertRaises(AssertionError, do_test, none, client_cert)

        # Strict SSL listener, SSL only
        self.assertRaises(AssertionError, do_test, strict, [])
        do_test(strict_s, [])
        do_test(strict_s, client_cert_all)
        do_test(strict, client_cert_all)
        do_test(strict, trustfile)
        do_test(strict, trustfile + client_cert_all)
        self.assertRaises(AssertionError, do_test, strict, bad_trustfile)

        # Requre-auth SSL listener
        self.assertRaises(AssertionError, do_test, auth, [])
        self.assertRaises(AssertionError, do_test, auth_s, [])
        self.assertRaises(AssertionError, do_test, auth, trustfile)
        do_test(auth, client_cert_all)
        do_test(auth, client_cert_all + trustfile)
        self.assertRaises(AssertionError, do_test, auth, client_cert_all + bad_trustfile)

        # Unsecured SSL listener, allows non-SSL
        do_test(unsecured_s, [])
        do_test(unsecured_s, client_cert_all)
        do_test(unsecured_s, trustfile)
        do_test(unsecured_s, client_cert_all + trustfile)
        do_test(unsecured_s, [])
        do_test(unsecured, []) # Allow unsecured
        self.assertRaises(AssertionError, do_test, auth, client_cert_all + bad_trustfile)

if __name__ == '__main__':
    unittest.main(system_test.main_module())

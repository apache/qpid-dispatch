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
from proton import Url, SSLDomain, SSLUnavailable, SASL

class QdstatTest(system_test.TestCase):
    """Test qdstat tool output"""
    @classmethod
    def setUpClass(cls):
        super(QdstatTest, cls).setUpClass()
        config = system_test.Qdrouterd.Config([
            ('router', {'id': 'QDR.A', 'workerThreads': 1}),
            ('listener', {'port': cls.tester.get_port()}),
        ])
        cls.router = cls.tester.qdrouterd('test-router', config)

    def run_qdstat(self, args, regexp=None, address=None):
        p = self.popen(
            ['qdstat', '--bus', str(address or self.router.addresses[0]), '--timeout', str(system_test.TIMEOUT) ] + args,
            name='qdstat-'+self.id(), stdout=PIPE, expect=None)

        out = p.communicate()[0]
        assert p.returncode == 0, \
            "qdstat exit status %s, output:\n%s" % (p.returncode, out)
        if regexp: assert re.search(regexp, out, re.I), "Can't find '%s' in '%s'" % (regexp, out)
        return out

    def test_help(self):
        self.run_qdstat(['--help'], r'Usage: qdstat')

    def test_general(self):
        out = self.run_qdstat(['--general'], r'(?s)Router Statistics.*Mode\s*Standalone')
        self.assertTrue("Connections  1" in out)
        self.assertTrue("Nodes        0" in out)
        self.assertTrue("Auto Links   0" in out)
        self.assertTrue("Link Routes  0" in out)
        self.assertTrue("Router Id    QDR.A" in out)
        self.assertTrue("Mode         standalone" in out)

    def test_connections(self):
        self.run_qdstat(['--connections'], r'host.*container.*role')

    def test_links(self):
        out = self.run_qdstat(['--links'], r'endpoint.*out.*local.*temp.')
        parts = out.split("\n")
        self.assertEqual(len(parts), 6)

    def test_links_with_limit(self):
        out = self.run_qdstat(['--links', '--limit=1'])
        parts = out.split("\n")
        self.assertEqual(len(parts), 5)

    def test_nodes(self):
        self.run_qdstat(['--nodes'], r'No Router List')

    def test_address(self):
        out = self.run_qdstat(['--address'], r'\$management')
        parts = out.split("\n")
        self.assertEqual(len(parts), 8)

    def test_address_with_limit(self):
        out = self.run_qdstat(['--address', '--limit=1'])
        parts = out.split("\n")
        self.assertEqual(len(parts), 5)

    def test_memory(self):
        out = self.run_qdstat(['--memory'])
        if out.strip() == "No memory statistics available":
            # router built w/o memory pools enabled]
            return self.skipTest("Router's memory pools disabled")
        regexp = r'qdr_address_t\s+[0-9]+'
        assert re.search(regexp, out, re.I), "Can't find '%s' in '%s'" % (regexp, out)

    def test_log(self):
        self.run_qdstat(['--log',  '--limit=5'], r'AGENT \(trace\).*GET-LOG')

try:
    SSLDomain(SSLDomain.MODE_CLIENT)
    class QdstatSslTest(system_test.TestCase):
        """Test qdstat tool output"""

        @staticmethod
        def ssl_file(name):
            return os.path.join(system_test.DIR, 'ssl_certs', name)

        @staticmethod
        def sasl_path():
            return os.path.join(system_test.DIR, 'sasl_configs')

        @classmethod
        def setUpClass(cls):
            super(QdstatSslTest, cls).setUpClass()
            # Write SASL configuration file:
            with open('tests-mech-EXTERNAL.conf', 'w') as sasl_conf:
                sasl_conf.write("mech_list: EXTERNAL ANONYMOUS DIGEST-MD5 PLAIN\n")
            # qdrouterd configuration:
            config = system_test.Qdrouterd.Config([
                ('router', {'id': 'QDR.B',
                            'saslConfigPath': os.getcwd(),
                            'workerThreads': 1,
                            'saslConfigName': 'tests-mech-EXTERNAL'}),
                ('sslProfile', {'name': 'server-ssl',
                                 'certDb': cls.ssl_file('ca-certificate.pem'),
                                 'certFile': cls.ssl_file('server-certificate.pem'),
                                 'keyFile': cls.ssl_file('server-private-key.pem'),
                                 'password': 'server-password'}),
                ('listener', {'port': cls.tester.get_port()}),
                ('listener', {'port': cls.tester.get_port(), 'sslProfile': 'server-ssl', 'authenticatePeer': 'no', 'requireSsl': 'yes'}),
                ('listener', {'port': cls.tester.get_port(), 'sslProfile': 'server-ssl', 'authenticatePeer': 'no', 'requireSsl': 'no'}),
                ('listener', {'port': cls.tester.get_port(), 'sslProfile': 'server-ssl', 'authenticatePeer': 'yes', 'requireSsl': 'yes',
                              'saslMechanisms': 'EXTERNAL'})
            ])
            cls.router = cls.tester.qdrouterd('test-router', config)

        def run_qdstat(self, args, regexp=None, address=None):
            p = self.popen(
                ['qdstat', '--bus', str(address or self.router.addresses[0]), '--ssl-disable-peer-name-verify',
                 '--timeout', str(system_test.TIMEOUT) ] + args,
                name='qdstat-'+self.id(), stdout=PIPE, expect=None)

            out = p.communicate()[0]
            assert p.returncode == 0, \
                "qdstat exit status %s, output:\n%s" % (p.returncode, out)
            if regexp: assert re.search(regexp, out, re.I), "Can't find '%s' in '%s'" % (regexp, out)
            return out

        def get_ssl_args(self):
            args = dict(
                trustfile = ['--ssl-trustfile', self.ssl_file('ca-certificate.pem')],
                bad_trustfile = ['--ssl-trustfile', self.ssl_file('bad-ca-certificate.pem')],
                client_cert = ['--ssl-certificate', self.ssl_file('client-certificate.pem')],
                client_key = ['--ssl-key', self.ssl_file('client-private-key.pem')],
                client_pass = ['--ssl-password', 'client-password'])
            args['client_cert_all'] = args['client_cert'] + args['client_key'] + args['client_pass']

            return args

        def ssl_test(self, url_name, arg_names):
            """Run simple SSL connection test with supplied parameters.
            See test_ssl_* below.
            """
            args = self.get_ssl_args()
            addrs = [self.router.addresses[i] for i in xrange(4)];
            urls = dict(zip(['none', 'strict', 'unsecured', 'auth'], addrs) +
                        zip(['none_s', 'strict_s', 'unsecured_s', 'auth_s'],
                            (Url(a, scheme="amqps") for a in addrs)))

            self.run_qdstat(['--general'] + sum([args[n] for n in arg_names], []),
                            regexp=r'(?s)Router Statistics.*Mode\s*Standalone',
                            address=str(urls[url_name]))

        def ssl_test_bad(self, url_name, arg_names):
            self.assertRaises(AssertionError, self.ssl_test, url_name, arg_names)

        # Non-SSL enabled listener should fail SSL connections.
        def test_ssl_none(self):
            self.ssl_test('none', [])

        def test_ssl_scheme_to_none(self):
            self.ssl_test_bad('none_s', [])

        def test_ssl_cert_to_none(self):
            self.ssl_test_bad('none', ['client_cert'])

        # Strict SSL listener, SSL only
        def test_ssl_none_to_strict(self):
            self.ssl_test_bad('strict', [])

        def test_ssl_schema_to_strict(self):
            self.ssl_test('strict_s', [])

        def test_ssl_cert_to_strict(self):
            self.ssl_test('strict_s', ['client_cert_all'])

        def test_ssl_trustfile_to_strict(self):
            self.ssl_test('strict_s', ['trustfile'])

        def test_ssl_trustfile_cert_to_strict(self):
            self.ssl_test('strict_s', ['trustfile', 'client_cert_all'])

        def test_ssl_bad_trustfile_to_strict(self):
            self.ssl_test_bad('strict_s', ['bad_trustfile'])


        # Require-auth SSL listener
        def test_ssl_none_to_auth(self):
            self.ssl_test_bad('auth', [])

        def test_ssl_schema_to_auth(self):
            self.ssl_test_bad('auth_s', [])

        def test_ssl_trustfile_to_auth(self):
            self.ssl_test_bad('auth_s', ['trustfile'])

        def test_ssl_cert_to_auth(self):
            self.ssl_test('auth_s', ['client_cert_all'])

        def test_ssl_trustfile_cert_to_auth(self):
            self.ssl_test('auth_s', ['trustfile', 'client_cert_all'])

        def test_ssl_bad_trustfile_to_auth(self):
            self.ssl_test_bad('auth_s', ['bad_trustfile', 'client_cert_all'])


        # Unsecured SSL listener, allows non-SSL
        def test_ssl_none_to_unsecured(self):
            self.ssl_test('unsecured', [])

        def test_ssl_schema_to_unsecured(self):
            self.ssl_test('unsecured_s', [])

        def test_ssl_cert_to_unsecured(self):
            self.ssl_test('unsecured_s', ['client_cert_all'])

        def test_ssl_trustfile_to_unsecured(self):
            self.ssl_test('unsecured_s', ['trustfile'])

        def test_ssl_trustfile_cert_to_unsecured(self):
            self.ssl_test('unsecured_s', ['trustfile', 'client_cert_all'])

        def test_ssl_bad_trustfile_to_unsecured(self):
            self.ssl_test_bad('unsecured_s', ['bad_trustfile'])

except SSLUnavailable:
    class QdstatSslTest(system_test.TestCase):
        def test_skip(self):
            self.skipTest("Proton SSL support unavailable.")

try:
    SSLDomain(SSLDomain.MODE_CLIENT)
    class QdstatSslTestSslPasswordFile(QdstatSslTest):
        """
        Tests the --ssl-password-file command line parameter
        """
        def get_ssl_args(self):
            args = dict(
                trustfile = ['--ssl-trustfile', self.ssl_file('ca-certificate.pem')],
                bad_trustfile = ['--ssl-trustfile', self.ssl_file('bad-ca-certificate.pem')],
                client_cert = ['--ssl-certificate', self.ssl_file('client-certificate.pem')],
                client_key = ['--ssl-key', self.ssl_file('client-private-key.pem')],
                client_pass = ['--ssl-password-file', self.ssl_file('client-password-file.txt')])
            args['client_cert_all'] = args['client_cert'] + args['client_key'] + args['client_pass']

            return args

except SSLUnavailable:
    class QdstatSslTest(system_test.TestCase):
        def test_skip(self):
            self.skipTest("Proton SSL support unavailable.")

try:
    SSLDomain(SSLDomain.MODE_CLIENT)
    class QdstatSslNoExternalTest(system_test.TestCase):
        """Test qdstat can't connect without sasl_mech EXTERNAL"""

        @staticmethod
        def ssl_file(name):
            return os.path.join(system_test.DIR, 'ssl_certs', name)

        @staticmethod
        def sasl_path():
            return os.path.join(system_test.DIR, 'sasl_configs')

        @classmethod
        def setUpClass(cls):
            super(QdstatSslNoExternalTest, cls).setUpClass()
            # Write SASL configuration file:
            with open('tests-mech-NOEXTERNAL.conf', 'w') as sasl_conf:
                sasl_conf.write("mech_list: ANONYMOUS DIGEST-MD5 PLAIN\n")
            # qdrouterd configuration:
            config = system_test.Qdrouterd.Config([
                ('router', {'id': 'QDR.C',
                            'saslConfigPath': os.getcwd(),
                            'workerThreads': 1,
                            'saslConfigName': 'tests-mech-NOEXTERNAL'}),
                ('sslProfile', {'name': 'server-ssl',
                                 'certDb': cls.ssl_file('ca-certificate.pem'),
                                 'certFile': cls.ssl_file('server-certificate.pem'),
                                 'keyFile': cls.ssl_file('server-private-key.pem'),
                                 'password': 'server-password'}),
                ('listener', {'port': cls.tester.get_port()}),
                ('listener', {'port': cls.tester.get_port(), 'sslProfile': 'server-ssl', 'authenticatePeer': 'no', 'requireSsl': 'yes'}),
                ('listener', {'port': cls.tester.get_port(), 'sslProfile': 'server-ssl', 'authenticatePeer': 'no', 'requireSsl': 'no'}),
                ('listener', {'port': cls.tester.get_port(), 'sslProfile': 'server-ssl', 'authenticatePeer': 'yes', 'requireSsl': 'yes',
                              'saslMechanisms': 'EXTERNAL'})
            ])
            cls.router = cls.tester.qdrouterd('test-router', config)

        def run_qdstat(self, args, regexp=None, address=None):
            p = self.popen(
                ['qdstat', '--bus', str(address or self.router.addresses[0]), '--timeout', str(system_test.TIMEOUT) ] + args,
                name='qdstat-'+self.id(), stdout=PIPE, expect=None)
            out = p.communicate()[0]
            assert p.returncode == 0, \
                "qdstat exit status %s, output:\n%s" % (p.returncode, out)
            if regexp: assert re.search(regexp, out, re.I), "Can't find '%s' in '%s'" % (regexp, out)
            return out

        def ssl_test(self, url_name, arg_names):
            """Run simple SSL connection test with supplied parameters.
            See test_ssl_* below.
            """
            args = dict(
                trustfile = ['--ssl-trustfile', self.ssl_file('ca-certificate.pem')],
                bad_trustfile = ['--ssl-trustfile', self.ssl_file('bad-ca-certificate.pem')],
                client_cert = ['--ssl-certificate', self.ssl_file('client-certificate.pem')],
                client_key = ['--ssl-key', self.ssl_file('client-private-key.pem')],
                client_pass = ['--ssl-password', 'client-password'])
            args['client_cert_all'] = args['client_cert'] + args['client_key'] + args['client_pass']

            addrs = [self.router.addresses[i] for i in xrange(4)];
            urls = dict(zip(['none', 'strict', 'unsecured', 'auth'], addrs) +
                        zip(['none_s', 'strict_s', 'unsecured_s', 'auth_s'],
                            (Url(a, scheme="amqps") for a in addrs)))

            self.run_qdstat(['--general'] + sum([args[n] for n in arg_names], []),
                            regexp=r'(?s)Router Statistics.*Mode\s*Standalone',
                            address=str(urls[url_name]))

        def ssl_test_bad(self, url_name, arg_names):
            self.assertRaises(AssertionError, self.ssl_test, url_name, arg_names)

        def test_ssl_cert_to_auth_fail_no_sasl_external(self):
            if not SASL.extended():
                self.skipTest("Cyrus library not available. skipping test")
            self.ssl_test_bad('auth_s', ['client_cert_all'])

        def test_ssl_trustfile_cert_to_auth_fail_no_sasl_external(self):
            self.ssl_test_bad('auth_s', ['trustfile', 'client_cert_all'])


except SSLUnavailable:
    class QdstatSslTest(system_test.TestCase):
        def test_skip(self):
            self.skipTest("Proton SSL support unavailable.")


if __name__ == '__main__':
    unittest.main(system_test.main_module())

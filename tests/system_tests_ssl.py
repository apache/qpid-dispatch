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

"""
Provides tests related with allowed TLS protocol version restrictions.
"""
import os
import ssl
import sys
import re
import time
from distutils.version import StrictVersion
from subprocess import Popen, PIPE

import cproton
import proton
from proton import SASL, Url, SSLDomain, SSLUnavailable
from proton.utils import BlockingConnection

from qpid_dispatch.management.client import Node
from system_test import TestCase, main_module, Qdrouterd, DIR
from system_test import unittest


class RouterTestSslBase(TestCase):
    """
    Base class to help with SSL related testing.
    """
    # If unable to determine which protocol versions are allowed system wide
    DISABLE_SSL_TESTING = False
    DISABLE_REASON = "Unable to determine MinProtocol"

    @staticmethod
    def ssl_file(name):
        """
        Returns fully qualified ssl certificate file name
        :param name:
        :return:
        """
        return os.path.join(DIR, 'ssl_certs', name)

    @classmethod
    def create_sasl_files(cls):
        """
        Creates the SASL DB
        :return:
        """
        # Create a sasl database.
        pipe = Popen(['saslpasswd2', '-c', '-p', '-f', 'qdrouterd.sasldb',
                      '-u', 'domain.com', 'test'],
                     stdin=PIPE, stdout=PIPE, stderr=PIPE,
                     universal_newlines=True)
        result = pipe.communicate('password')
        assert pipe.returncode == 0, \
            "saslpasswd2 exit status %s, output:\n%s" % (pipe.returncode, result)

        # Create a SASL configuration file.
        with open('tests-mech-PLAIN.conf', 'w') as sasl_conf:
            sasl_conf.write("""
pwcheck_method: auxprop
auxprop_plugin: sasldb
sasldb_path: qdrouterd.sasldb
mech_list: ANONYMOUS DIGEST-MD5 EXTERNAL PLAIN
# The following line stops spurious 'sql_select option missing' errors when cyrus-sql-sasl plugin is installed
sql_select: dummy select
""")


class RouterTestSslClient(RouterTestSslBase):
    """
    Starts a router with multiple listeners, all of them using an sslProfile.
    Then it runs multiple tests to validate that only the allowed protocol versions
    are being accepted through the related listener.
    """
    # Listener ports for each TLS protocol definition
    PORT_TLS1 = 0
    PORT_TLS11 = 0
    PORT_TLS12 = 0
    PORT_TLS13 = 0
    PORT_TLS1_TLS11 = 0
    PORT_TLS1_TLS12 = 0
    PORT_TLS11_TLS12 = 0
    PORT_TLS_ALL = 0
    PORT_TLS_SASL = 0
    PORT_SSL3 = 0
    TIMEOUT = 3

    # If using OpenSSL 1.1 or greater, TLSv1.2 is always being allowed
    OPENSSL_OUT_VER = None
    try:
        OPENSSL_VER_1_1_GT = ssl.OPENSSL_VERSION_INFO[:2] >= (1, 1)
    except AttributeError:
        OPENSSL_VER_1_1_GT = False

    # If still False, try getting it from "openssl version" (command output)
    # The version from ssl.OPENSSL_VERSION_INFO reflects OpenSSL version in which
    # Python was compiled with, not the one installed in the system.
    if not OPENSSL_VER_1_1_GT:
        print("Python libraries SSL Version < 1.1")
        try:
            p = Popen(['openssl', 'version'], stdout=PIPE, universal_newlines=True)
            openssl_out = p.communicate()[0]
            m = re.search(r'[0-9]+\.[0-9]+\.[0-9]+', openssl_out)
            assert m is not None
            OPENSSL_OUT_VER = m.group(0)
            OPENSSL_VER_1_1_GT = StrictVersion(OPENSSL_OUT_VER) >= StrictVersion('1.1')
            print("OpenSSL Version found = %s" % OPENSSL_OUT_VER)
        except:
            pass

    # Following variables define TLS versions allowed by openssl
    OPENSSL_MIN_VER = 0
    OPENSSL_MAX_VER = 9999
    OPENSSL_ALLOW_TLSV1 = True
    OPENSSL_ALLOW_TLSV1_1 = True
    OPENSSL_ALLOW_TLSV1_2 = True
    OPENSSL_ALLOW_TLSV1_3 = False

    # Test if OpenSSL has TLSv1_3
    #  (see https://mypy.readthedocs.io/en/stable/common_issues.html#python-version-and-system-platform-checks for mypy considerations)
    OPENSSL_HAS_TLSV1_3 = OPENSSL_VER_1_1_GT and sys.version_info >= (3, 7) and ssl.HAS_TLSv1_3

    # Test if Proton supports TLSv1_3
    try:
        dummydomain = SSLDomain(SSLDomain.MODE_CLIENT)
        PROTON_HAS_TLSV1_3 = cproton.PN_OK == cproton.pn_ssl_domain_set_protocols(dummydomain._domain, "TLSv1.3")
        print("TLSV1_3? Proton has: %s, OpenSSL has: %s" % (PROTON_HAS_TLSV1_3, OPENSSL_HAS_TLSV1_3))
    except SSLUnavailable:
        PROTON_HAS_TLSV1_3 = False

    # When using OpenSSL >= 1.1 and python >= 3.7, we can retrieve OpenSSL min and max protocols
    if OPENSSL_VER_1_1_GT:
        if sys.version_info >= (3, 7):
            if OPENSSL_HAS_TLSV1_3 and not PROTON_HAS_TLSV1_3:
                # If OpenSSL has 1.3 but proton won't let us turn it on and off then
                # this test fails because v1.3 runs unexpectedly.
                RouterTestSslBase.DISABLE_SSL_TESTING = True
                RouterTestSslBase.DISABLE_REASON = "Proton version does not support TLSv1.3 but OpenSSL does"
            else:
                OPENSSL_CTX = ssl.create_default_context()
                OPENSSL_MIN_VER = OPENSSL_CTX.minimum_version
                OPENSSL_MAX_VER = OPENSSL_CTX.maximum_version if OPENSSL_CTX.maximum_version > 0 else 9999
                OPENSSL_ALLOW_TLSV1 = OPENSSL_MIN_VER <= ssl.TLSVersion.TLSv1 <= OPENSSL_MAX_VER
                OPENSSL_ALLOW_TLSV1_1 = OPENSSL_MIN_VER <= ssl.TLSVersion.TLSv1_1 <= OPENSSL_MAX_VER
                OPENSSL_ALLOW_TLSV1_2 = OPENSSL_MIN_VER <= ssl.TLSVersion.TLSv1_2 <= OPENSSL_MAX_VER
                OPENSSL_ALLOW_TLSV1_3 = OPENSSL_HAS_TLSV1_3 and PROTON_HAS_TLSV1_3 \
                    and OPENSSL_MIN_VER <= ssl.TLSVersion.TLSv1_3 <= OPENSSL_MAX_VER
        else:
            # At this point we are not able to precisely determine what are the minimum and maximum
            # TLS versions allowed in the system, so tests will be disabled
            RouterTestSslBase.DISABLE_SSL_TESTING = True
            RouterTestSslBase.DISABLE_REASON = "OpenSSL >= 1.1 but Python < 3.7 - Unable to determine MinProtocol"
    else:
        if OPENSSL_HAS_TLSV1_3 and not PROTON_HAS_TLSV1_3:
            # If OpenSSL has 1.3 but proton won't let us turn it on and off then
            # this test fails because v1.3 runs unexpectedly.
            RouterTestSslBase.DISABLE_SSL_TESTING = True
            RouterTestSslBase.DISABLE_REASON = "Proton version does not support TLSv1.3 but OpenSSL does"

    @classmethod
    def setUpClass(cls):
        """
        Prepares a single router with multiple listeners, each one associated with a particular
        sslProfile and each sslProfile has its own specific set of allowed protocols.
        """
        super(RouterTestSslClient, cls).setUpClass()

        cls.routers = []

        if SASL.extended():
            router = ('router', {'id': 'QDR.A',
                                 'mode': 'interior',
                                 'saslConfigName': 'tests-mech-PLAIN',
                                 'saslConfigDir': os.getcwd()})

            # Generate authentication DB
            super(RouterTestSslClient, cls).create_sasl_files()
        else:
            router = ('router', {'id': 'QDR.A',
                                 'mode': 'interior'})

        # Saving listener ports for each TLS definition
        cls.PORT_TLS1 = cls.tester.get_port()
        cls.PORT_TLS11 = cls.tester.get_port()
        cls.PORT_TLS12 = cls.tester.get_port()
        cls.PORT_TLS13 = cls.tester.get_port()
        cls.PORT_TLS1_TLS11 = cls.tester.get_port()
        cls.PORT_TLS1_TLS12 = cls.tester.get_port()
        cls.PORT_TLS11_TLS12 = cls.tester.get_port()
        cls.PORT_TLS_ALL = cls.tester.get_port()
        cls.PORT_TLS_SASL = cls.tester.get_port()
        cls.PORT_SSL3 = cls.tester.get_port()

        conf = [
            router,
            # TLSv1 only
            ('listener', {'host': '0.0.0.0', 'role': 'normal', 'port': cls.PORT_TLS1,
                          'authenticatePeer': 'no',
                          'sslProfile': 'ssl-profile-tls1'}),
            # TLSv1.1 only
            ('listener', {'host': '0.0.0.0', 'role': 'normal', 'port': cls.PORT_TLS11,
                          'authenticatePeer': 'no',
                          'sslProfile': 'ssl-profile-tls11'}),
            # TLSv1.2 only
            ('listener', {'host': '0.0.0.0', 'role': 'normal', 'port': cls.PORT_TLS12,
                          'authenticatePeer': 'no',
                          'sslProfile': 'ssl-profile-tls12'}),
            # TLSv1 and TLSv1.1 only
            ('listener', {'host': '0.0.0.0', 'role': 'normal', 'port': cls.PORT_TLS1_TLS11,
                          'authenticatePeer': 'no',
                          'sslProfile': 'ssl-profile-tls1-tls11'}),
            # TLSv1 and TLSv1.2 only
            ('listener', {'host': '0.0.0.0', 'role': 'normal', 'port': cls.PORT_TLS1_TLS12,
                          'authenticatePeer': 'no',
                          'sslProfile': 'ssl-profile-tls1-tls12'}),
            # TLSv1.1 and TLSv1.2 only
            ('listener', {'host': '0.0.0.0', 'role': 'normal', 'port': cls.PORT_TLS11_TLS12,
                          'authenticatePeer': 'no',
                          'sslProfile': 'ssl-profile-tls11-tls12'}),
            # All TLS versions
            ('listener', {'host': '0.0.0.0', 'role': 'normal', 'port': cls.PORT_TLS_ALL,
                          'authenticatePeer': 'no',
                          'sslProfile': 'ssl-profile-tls-all'}),
            # Invalid protocol version
            ('listener', {'host': '0.0.0.0', 'role': 'normal', 'port': cls.PORT_SSL3,
                          'authenticatePeer': 'no',
                          'sslProfile': 'ssl-profile-ssl3'})
        ]

        # Adding SASL listener only when SASL is available
        if SASL.extended():
            conf += [
                # TLS 1 and 1.2 with SASL PLAIN authentication for proton client validation
                ('listener', {'host': '0.0.0.0', 'role': 'normal', 'port': cls.PORT_TLS_SASL,
                              'authenticatePeer': 'yes', 'saslMechanisms': 'PLAIN',
                              'requireSsl': 'yes', 'requireEncryption': 'yes',
                              'sslProfile': 'ssl-profile-tls1-tls12'})
            ]

        # Adding SSL profiles
        conf += [
            # SSL Profile for TLSv1
            ('sslProfile', {'name': 'ssl-profile-tls1',
                            'caCertFile': cls.ssl_file('ca-certificate.pem'),
                            'certFile': cls.ssl_file('server-certificate.pem'),
                            'privateKeyFile': cls.ssl_file('server-private-key.pem'),
                            'ciphers': 'ECDH+AESGCM:DH+AESGCM:ECDH+AES256:DH+AES256:ECDH+AES128:' \
                                       'DH+AES:RSA+AESGCM:RSA+AES:!aNULL:!MD5:!DSS',
                            'protocols': 'TLSv1',
                            'password': 'server-password'}),
            # SSL Profile for TLSv1.1
            ('sslProfile', {'name': 'ssl-profile-tls11',
                            'caCertFile': cls.ssl_file('ca-certificate.pem'),
                            'certFile': cls.ssl_file('server-certificate.pem'),
                            'privateKeyFile': cls.ssl_file('server-private-key.pem'),
                            'ciphers': 'ECDH+AESGCM:DH+AESGCM:ECDH+AES256:DH+AES256:ECDH+AES128:' \
                                       'DH+AES:RSA+AESGCM:RSA+AES:!aNULL:!MD5:!DSS',
                            'protocols': 'TLSv1.1',
                            'password': 'server-password'}),
            # SSL Profile for TLSv1.2
            ('sslProfile', {'name': 'ssl-profile-tls12',
                            'caCertFile': cls.ssl_file('ca-certificate.pem'),
                            'certFile': cls.ssl_file('server-certificate.pem'),
                            'privateKeyFile': cls.ssl_file('server-private-key.pem'),
                            'ciphers': 'ECDH+AESGCM:DH+AESGCM:ECDH+AES256:DH+AES256:ECDH+AES128:' \
                                       'DH+AES:RSA+AESGCM:RSA+AES:!aNULL:!MD5:!DSS',
                            'protocols': 'TLSv1.2',
                            'password': 'server-password'}),
            # SSL Profile for TLSv1 and TLSv1.1
            ('sslProfile', {'name': 'ssl-profile-tls1-tls11',
                            'caCertFile': cls.ssl_file('ca-certificate.pem'),
                            'certFile': cls.ssl_file('server-certificate.pem'),
                            'privateKeyFile': cls.ssl_file('server-private-key.pem'),
                            'ciphers': 'ECDH+AESGCM:DH+AESGCM:ECDH+AES256:DH+AES256:ECDH+AES128:' \
                                       'DH+AES:RSA+AESGCM:RSA+AES:!aNULL:!MD5:!DSS',
                            'protocols': 'TLSv1 TLSv1.1',
                            'password': 'server-password'}),
            # SSL Profile for TLSv1 and TLSv1.2
            ('sslProfile', {'name': 'ssl-profile-tls1-tls12',
                            'caCertFile': cls.ssl_file('ca-certificate.pem'),
                            'certFile': cls.ssl_file('server-certificate.pem'),
                            'privateKeyFile': cls.ssl_file('server-private-key.pem'),
                            'ciphers': 'ECDH+AESGCM:DH+AESGCM:ECDH+AES256:DH+AES256:ECDH+AES128:' \
                                       'DH+AES:RSA+AESGCM:RSA+AES:!aNULL:!MD5:!DSS',
                            'protocols': 'TLSv1 TLSv1.2',
                            'password': 'server-password'}),
            # SSL Profile for TLSv1.1 and TLSv1.2
            ('sslProfile', {'name': 'ssl-profile-tls11-tls12',
                            'caCertFile': cls.ssl_file('ca-certificate.pem'),
                            'certFile': cls.ssl_file('server-certificate.pem'),
                            'privateKeyFile': cls.ssl_file('server-private-key.pem'),
                            'ciphers': 'ECDH+AESGCM:DH+AESGCM:ECDH+AES256:DH+AES256:ECDH+AES128:' \
                                       'DH+AES:RSA+AESGCM:RSA+AES:!aNULL:!MD5:!DSS',
                            'protocols': 'TLSv1.1 TLSv1.2',
                            'password': 'server-password'}),
            # SSL Profile for all TLS versions (protocols element not defined)
            ('sslProfile', {'name': 'ssl-profile-tls-all',
                            'caCertFile': cls.ssl_file('ca-certificate.pem'),
                            'certFile': cls.ssl_file('server-certificate.pem'),
                            'privateKeyFile': cls.ssl_file('server-private-key.pem'),
                            'ciphers': 'ECDH+AESGCM:DH+AESGCM:ECDH+AES256:DH+AES256:ECDH+AES128:' \
                                       'DH+AES:RSA+AESGCM:RSA+AES:!aNULL:!MD5:!DSS',
                            'password': 'server-password'}),
            # SSL Profile for invalid protocol version SSLv23
            ('sslProfile', {'name': 'ssl-profile-ssl3',
                            'caCertFile': cls.ssl_file('ca-certificate.pem'),
                            'certFile': cls.ssl_file('server-certificate.pem'),
                            'privateKeyFile': cls.ssl_file('server-private-key.pem'),
                            'ciphers': 'ECDH+AESGCM:DH+AESGCM:ECDH+AES256:DH+AES256:ECDH+AES128:' \
                                       'DH+AES:RSA+AESGCM:RSA+AES:!aNULL:!MD5:!DSS',
                            'protocols': 'SSLv23',
                            'password': 'server-password'})
        ]

        if cls.OPENSSL_ALLOW_TLSV1_3:
            conf += [
                # TLSv1.3 only
                ('listener', {'host': '0.0.0.0', 'role': 'normal', 'port': cls.PORT_TLS13,
                              'authenticatePeer': 'no',
                              'sslProfile': 'ssl-profile-tls13'}),
                # SSL Profile for TLSv1.3
                ('sslProfile', {'name': 'ssl-profile-tls13',
                                'caCertFile': cls.ssl_file('ca-certificate.pem'),
                                'certFile': cls.ssl_file('server-certificate.pem'),
                                'privateKeyFile': cls.ssl_file('server-private-key.pem'),
                                'protocols': 'TLSv1.3',
                                'password': 'server-password'})

            ]

        config = Qdrouterd.Config(conf)

        cls.routers.append(cls.tester.qdrouterd("A", config, wait=False))
        cls.routers[0].wait_ports()

    def get_allowed_protocols(self, listener_port):
        """
        Loops through TLSv1, TLSv1.1 and TLSv1.2 and attempts to connect
        to the listener_port using each version. The result is a boolean list
        with results in respective order for TLSv1 [0], TLSv1.1 [1] and TLSv1.2 [2].
        :param listener_port:
        :return:
        """
        results = []

        for proto in ['TLSv1', 'TLSv1.1', 'TLSv1.2']:
            results.append(self.is_proto_allowed(listener_port, proto))
        if self.OPENSSL_ALLOW_TLSV1_3:
            results.append(self.is_proto_allowed(listener_port, 'TLSv1.3'))
        else:
            results.append(False)
        return results

    def is_proto_allowed(self, listener_port, tls_protocol):
        """
        Opens a simple proton client connection to the provided TCP port using
        a specific TLS protocol version and returns True in case connection
        was established and accepted or False otherwise.
        :param listener_port: TCP port number
        :param tls_protocol: TLSv1, TLSv1.1 or TLSv1.2 (string)
        :return:
        """
        # Management address to connect using the given TLS protocol
        url = Url("amqps://0.0.0.0:%d/$management" % listener_port)
        # Preparing SSLDomain (client cert) and SASL authentication info
        domain = SSLDomain(SSLDomain.MODE_CLIENT)
        domain.set_trusted_ca_db(self.ssl_file('ca-certificate.pem'))
        domain.set_peer_authentication(SSLDomain.VERIFY_PEER)
        # Enforcing given TLS protocol
        cproton.pn_ssl_domain_set_protocols(domain._domain, tls_protocol)

        # Try opening the secure and authenticated connection
        try:
            connection = BlockingConnection(url, sasl_enabled=False, ssl_domain=domain, timeout=self.TIMEOUT)
        except proton.Timeout:
            return False
        except proton.ConnectionException:
            return False
        except:
            return False

        # TLS version provided was accepted
        connection.close()
        return True

    def is_ssl_sasl_client_accepted(self, listener_port, tls_protocol):
        """
        Attempts to connect a proton client to the management address
        on the given listener_port using the specific tls_protocol provided.
        If connection was established and accepted, returns True and False otherwise.
        :param listener_port:
        :param tls_protocol:
        :return:
        """
        # Management address to connect using the given TLS protocol
        url = Url("amqps://0.0.0.0:%d/$management" % listener_port)
        # Preparing SSLDomain (client cert) and SASL authentication info
        domain = SSLDomain(SSLDomain.MODE_CLIENT)
        domain.set_credentials(self.ssl_file('client-certificate.pem'),
                               self.ssl_file('client-private-key.pem'),
                               'client-password')
        domain.set_trusted_ca_db(self.ssl_file('ca-certificate.pem'))
        domain.set_peer_authentication(SSLDomain.VERIFY_PEER)
        # Enforcing given TLS protocol
        cproton.pn_ssl_domain_set_protocols(domain._domain, tls_protocol)

        # Try opening the secure and authenticated connection
        try:
            connection = BlockingConnection(url,
                                            sasl_enabled=True,
                                            ssl_domain=domain,
                                            allowed_mechs='PLAIN',
                                            user='test@domain.com',
                                            password='password')
        except proton.ConnectionException:
            return False

        # TLS version provided was accepted
        connection.close()
        return True

    def get_expected_tls_result(self, expected_results):
        """
        Expects a list with three boolean elements, representing
        TLSv1, TLSv1.1 and TLSv1.2 (in the respective order).
        When using OpenSSL >= 1.1.x, allowance of a given TLS version is
        based on MinProtocol / MaxProtocol definitions.
        It is also important
        to mention that TLSv1.2 is being allowed even when not specified in a
        listener when using OpenSSL >= 1.1.x.

        :param expected_results:
        :return:
        """
        (tlsv1, tlsv1_1, tlsv1_2, tlsv1_3) = expected_results
        return [self.OPENSSL_ALLOW_TLSV1 and tlsv1,
                self.OPENSSL_ALLOW_TLSV1_1 and tlsv1_1,
                self.OPENSSL_ALLOW_TLSV1_2 and tlsv1_2,
                self.OPENSSL_ALLOW_TLSV1_3 and tlsv1_3]

    @unittest.skipIf(RouterTestSslBase.DISABLE_SSL_TESTING, RouterTestSslBase.DISABLE_REASON)
    def test_tls1_only(self):
        """
        Expects TLSv1 only is allowed
        """
        self.assertEqual(self.get_expected_tls_result([True, False, False, False]),
                         self.get_allowed_protocols(self.PORT_TLS1))

    @unittest.skipIf(RouterTestSslBase.DISABLE_SSL_TESTING, RouterTestSslBase.DISABLE_REASON)
    def test_tls11_only(self):
        """
        Expects TLSv1.1 only is allowed
        """
        self.assertEqual(self.get_expected_tls_result([False, True, False, False]),
                         self.get_allowed_protocols(self.PORT_TLS11))

    @unittest.skipIf(RouterTestSslBase.DISABLE_SSL_TESTING, RouterTestSslBase.DISABLE_REASON)
    def test_tls12_only(self):
        """
        Expects TLSv1.2 only is allowed
        """
        self.assertEqual(self.get_expected_tls_result([False, False, True, False]),
                         self.get_allowed_protocols(self.PORT_TLS12))

    @unittest.skipIf(RouterTestSslBase.DISABLE_SSL_TESTING, RouterTestSslBase.DISABLE_REASON)
    def test_tls13_only(self):
        """
        Expects TLSv1.3 only is allowed
        """
        self.assertEqual(self.get_expected_tls_result([False, False, False, True]),
                         self.get_allowed_protocols(self.PORT_TLS13))

    @unittest.skipIf(RouterTestSslBase.DISABLE_SSL_TESTING, RouterTestSslBase.DISABLE_REASON)
    def test_tls1_tls11_only(self):
        """
        Expects TLSv1 and TLSv1.1 only are allowed
        """
        self.assertEqual(self.get_expected_tls_result([True, True, False, False]),
                         self.get_allowed_protocols(self.PORT_TLS1_TLS11))

    @unittest.skipIf(RouterTestSslBase.DISABLE_SSL_TESTING, RouterTestSslBase.DISABLE_REASON)
    def test_tls1_tls12_only(self):
        """
        Expects TLSv1 and TLSv1.2 only are allowed
        """
        self.assertEqual(self.get_expected_tls_result([True, False, True, False]),
                         self.get_allowed_protocols(self.PORT_TLS1_TLS12))

    @unittest.skipIf(RouterTestSslBase.DISABLE_SSL_TESTING, RouterTestSslBase.DISABLE_REASON)
    def test_tls11_tls12_only(self):
        """
        Expects TLSv1.1 and TLSv1.2 only are allowed
        """
        self.assertEqual(self.get_expected_tls_result([False, True, True, False]),
                         self.get_allowed_protocols(self.PORT_TLS11_TLS12))

    @unittest.skipIf(RouterTestSslBase.DISABLE_SSL_TESTING, RouterTestSslBase.DISABLE_REASON)
    def test_tls_all(self):
        """
        Expects all supported versions: TLSv1, TLSv1.1, TLSv1.2 and TLSv1.3 to be allowed
        """
        self.assertEqual(self.get_expected_tls_result([True, True, True, True]),
                         self.get_allowed_protocols(self.PORT_TLS_ALL))

    @unittest.skipIf(RouterTestSslBase.DISABLE_SSL_TESTING, RouterTestSslBase.DISABLE_REASON)
    def test_ssl_invalid(self):
        """
        Expects connection is rejected as SSL is no longer supported
        """
        self.assertEqual(False, self.is_proto_allowed(self.PORT_SSL3, 'SSLv3'))

    @unittest.skipIf(RouterTestSslBase.DISABLE_SSL_TESTING or not SASL.extended(),
                     "Cyrus library not available. skipping test")
    def test_ssl_sasl_client_valid(self):
        """
        Attempts to connect a Proton client using a valid SASL authentication info
        and forcing the TLS protocol version, which should be accepted by the listener.
        :return:
        """
        if not SASL.extended():
            self.skipTest("Cyrus library not available. skipping test")

        exp_tls_results = self.get_expected_tls_result([True, False, True, False])
        self.assertEqual(exp_tls_results[0], self.is_ssl_sasl_client_accepted(self.PORT_TLS_SASL, "TLSv1"))
        self.assertEqual(exp_tls_results[2], self.is_ssl_sasl_client_accepted(self.PORT_TLS_SASL, "TLSv1.2"))

    @unittest.skipIf(RouterTestSslBase.DISABLE_SSL_TESTING or not SASL.extended(),
                     "Cyrus library not available. skipping test")
    def test_ssl_sasl_client_invalid(self):
        """
        Attempts to connect a Proton client using a valid SASL authentication info
        and forcing the TLS protocol version, which should be rejected by the listener.
        :return:
        """
        if not SASL.extended():
            self.skipTest("Cyrus library not available. skipping test")

        exp_tls_results = self.get_expected_tls_result([True, False, True, False])
        self.assertEqual(exp_tls_results[1], self.is_ssl_sasl_client_accepted(self.PORT_TLS_SASL, "TLSv1.1"))


class RouterTestSslInterRouter(RouterTestSslBase):
    """
    Starts 5 routers with several listeners and connectors and validate if communication
    between them is working as expected.
    """
    # Listener ports for each TLS protocol definition
    PORT_NO_SSL = 0
    PORT_TLS_ALL = 0
    PORT_TLS12 = 0
    PORT_TLS1_TLS12 = 0

    @classmethod
    def setUpClass(cls):
        """
        Prepares 5 routers to form a network. One of them will provide listeners with
        multiple sslProfiles, and the other 4 will try to connect with a respective listener.
        It expects that routers A to D will connect successfully, while E will not succeed due
        to an SSL handshake failure (as allowed TLS protocol versions won't match).
        """
        super(RouterTestSslInterRouter, cls).setUpClass()

        if not SASL.extended():
            return

        os.environ["ENV_SASL_PASSWORD"] = "password"

        # Generate authentication DB
        super(RouterTestSslInterRouter, cls).create_sasl_files()

        # Router expected to be connected
        cls.connected_tls_sasl_routers = ['QDR.A', 'QDR.B', 'QDR.C', 'QDR.D']

        # Generated router list
        cls.routers = []

        # Saving listener ports for each TLS definition
        cls.PORT_NO_SSL = cls.tester.get_port()
        cls.PORT_TLS_ALL = cls.tester.get_port()
        cls.PORT_TLS12 = cls.tester.get_port()
        cls.PORT_TLS1_TLS12 = cls.tester.get_port()

        config_a = Qdrouterd.Config([
            ('router', {'id': 'QDR.A',
                        'mode': 'interior',
                        'saslConfigName': 'tests-mech-PLAIN',
                        'saslConfigDir': os.getcwd()}),
            # No auth and no SSL
            ('listener', {'host': '0.0.0.0', 'role': 'normal', 'port': cls.PORT_NO_SSL}),
            # All TLS versions
            ('listener', {'host': '0.0.0.0', 'role': 'inter-router', 'port': cls.PORT_TLS_ALL,
                          'authenticatePeer': 'yes', 'saslMechanisms': 'PLAIN',
                          'requireEncryption': 'yes', 'requireSsl': 'yes',
                          'sslProfile': 'ssl-profile-tls-all'}),
            # TLSv1.2 only
            ('listener', {'host': '0.0.0.0', 'role': 'inter-router', 'port': cls.PORT_TLS12,
                          'authenticatePeer': 'yes', 'saslMechanisms': 'PLAIN',
                          'requireEncryption': 'yes', 'requireSsl': 'yes',
                          'sslProfile': 'ssl-profile-tls12'}),
            # TLSv1 and TLSv1.2 only
            ('listener', {'host': '0.0.0.0', 'role': 'inter-router', 'port': cls.PORT_TLS1_TLS12,
                          'authenticatePeer': 'yes', 'saslMechanisms': 'PLAIN',
                          'requireEncryption': 'yes', 'requireSsl': 'yes',
                          'sslProfile': 'ssl-profile-tls1-tls12'}),
            # SSL Profile for all TLS versions (protocols element not defined)
            ('sslProfile', {'name': 'ssl-profile-tls-all',
                            'caCertFile': cls.ssl_file('ca-certificate.pem'),
                            'certFile': cls.ssl_file('server-certificate.pem'),
                            'privateKeyFile': cls.ssl_file('server-private-key.pem'),
                            'ciphers': 'ECDH+AESGCM:DH+AESGCM:ECDH+AES256:DH+AES256:ECDH+AES128:' \
                                       'DH+AES:RSA+AESGCM:RSA+AES:!aNULL:!MD5:!DSS',
                            'password': 'server-password'}),
            # SSL Profile for TLSv1.2
            ('sslProfile', {'name': 'ssl-profile-tls12',
                            'caCertFile': cls.ssl_file('ca-certificate.pem'),
                            'certFile': cls.ssl_file('server-certificate.pem'),
                            'privateKeyFile': cls.ssl_file('server-private-key.pem'),
                            'ciphers': 'ECDH+AESGCM:DH+AESGCM:ECDH+AES256:DH+AES256:ECDH+AES128:' \
                                       'DH+AES:RSA+AESGCM:RSA+AES:!aNULL:!MD5:!DSS',
                            'protocols': 'TLSv1.2',
                            'password': 'server-password'}),
            # SSL Profile for TLSv1 and TLSv1.2
            ('sslProfile', {'name': 'ssl-profile-tls1-tls12',
                            'caCertFile': cls.ssl_file('ca-certificate.pem'),
                            'certFile': cls.ssl_file('server-certificate.pem'),
                            'privateKeyFile': cls.ssl_file('server-private-key.pem'),
                            'ciphers': 'ECDH+AESGCM:DH+AESGCM:ECDH+AES256:DH+AES256:ECDH+AES128:' \
                                       'DH+AES:RSA+AESGCM:RSA+AES:!aNULL:!MD5:!DSS',
                            'protocols': 'TLSv1 TLSv1.2',
                            'password': 'server-password'})

        ])

        # Router B will connect to listener that allows all protocols
        config_b = Qdrouterd.Config([
            ('router', {'id': 'QDR.B',
                        'mode': 'interior'}),
            # Connector to All TLS versions allowed listener
            ('connector', {'host': '0.0.0.0', 'role': 'inter-router', 'port': cls.PORT_TLS_ALL,
                           'verifyHostname': 'no', 'saslMechanisms': 'PLAIN',
                           'saslUsername': 'test@domain.com', 'saslPassword': 'pass:password',
                           'sslProfile': 'ssl-profile-tls-all'}),
            # SSL Profile for all TLS versions (protocols element not defined)
            ('sslProfile', {'name': 'ssl-profile-tls-all',
                            'caCertFile': cls.ssl_file('ca-certificate.pem'),
                            'certFile': cls.ssl_file('client-certificate.pem'),
                            'privateKeyFile': cls.ssl_file('client-private-key.pem'),
                            'ciphers': 'ECDH+AESGCM:DH+AESGCM:ECDH+AES256:DH+AES256:ECDH+AES128:' \
                                       'DH+AES:RSA+AESGCM:RSA+AES:!aNULL:!MD5:!DSS',
                            'password': 'client-password'})
        ])

        # Router C will connect to listener that allows TLSv1.2 only
        config_c = Qdrouterd.Config([
            ('router', {'id': 'QDR.C',
                        'mode': 'interior'}),
            # Connector to listener that allows TLSv1.2 only
            ('connector', {'host': '0.0.0.0', 'role': 'inter-router', 'port': cls.PORT_TLS12,
                           'verifyHostname': 'no', 'saslMechanisms': 'PLAIN',
                           'saslUsername': 'test@domain.com', 'saslPassword': 'env:ENV_SASL_PASSWORD',
                           'sslProfile': 'ssl-profile-tls12'}),
            # SSL Profile for TLSv1.2
            ('sslProfile', {'name': 'ssl-profile-tls12',
                            'caCertFile': cls.ssl_file('ca-certificate.pem'),
                            'certFile': cls.ssl_file('client-certificate.pem'),
                            'privateKeyFile': cls.ssl_file('client-private-key.pem'),
                            'ciphers': 'ECDH+AESGCM:DH+AESGCM:ECDH+AES256:DH+AES256:ECDH+AES128:' \
                                       'DH+AES:RSA+AESGCM:RSA+AES:!aNULL:!MD5:!DSS',
                            'protocols': 'TLSv1.2',
                            'password': 'client-password'})
        ])

        # Router D will connect to listener that allows TLSv1 and TLS1.2 only using TLSv1
        config_d = Qdrouterd.Config([
            ('router', {'id': 'QDR.D',
                        'mode': 'interior'}),
            # Connector to listener that allows TLSv1 and TLSv1.2 only
            ('connector', {'host': '0.0.0.0', 'role': 'inter-router', 'port': cls.PORT_TLS1_TLS12,
                           'verifyHostname': 'no', 'saslMechanisms': 'PLAIN',
                           'saslUsername': 'test@domain.com', 'saslPassword': 'pass:password',
                           'sslProfile': 'ssl-profile-tls1'}),
            # SSL Profile for TLSv1
            ('sslProfile', {'name': 'ssl-profile-tls1',
                            'caCertFile': cls.ssl_file('ca-certificate.pem'),
                            'certFile': cls.ssl_file('client-certificate.pem'),
                            'privateKeyFile': cls.ssl_file('client-private-key.pem'),
                            'ciphers': 'ECDH+AESGCM:DH+AESGCM:ECDH+AES256:DH+AES256:ECDH+AES128:' \
                                       'DH+AES:RSA+AESGCM:RSA+AES:!aNULL:!MD5:!DSS',
                            'protocols': 'TLSv1',
                            'password': 'client-password'})
        ])

        # Router E will try connect to listener that allows TLSv1 and TLS1.2 only using TLSv1.1
        config_e = Qdrouterd.Config([
            ('router', {'id': 'QDR.E',
                        'mode': 'interior'}),
            # Connector to listener that allows TLSv1 and TLSv1.2 only
            ('connector', {'host': '0.0.0.0', 'role': 'inter-router', 'port': cls.PORT_TLS1_TLS12,
                           'verifyHostname': 'no', 'saslMechanisms': 'PLAIN',
                           'saslUsername': 'test@domain.com', 'saslPassword': 'password',
                           'sslProfile': 'ssl-profile-tls11'}),
            # SSL Profile for TLSv1.1
            ('sslProfile', {'name': 'ssl-profile-tls11',
                            'caCertFile': cls.ssl_file('ca-certificate.pem'),
                            'certFile': cls.ssl_file('client-certificate.pem'),
                            'privateKeyFile': cls.ssl_file('client-private-key.pem'),
                            'ciphers': 'ECDH+AESGCM:DH+AESGCM:ECDH+AES256:DH+AES256:ECDH+AES128:' \
                                       'DH+AES:RSA+AESGCM:RSA+AES:!aNULL:!MD5:!DSS',
                            'protocols': 'TLSv1.1',
                            'password': 'client-password'})
        ])

        cls.routers.append(cls.tester.qdrouterd("A", config_a, wait=False))
        cls.routers.append(cls.tester.qdrouterd("B", config_b, wait=False))
        cls.routers.append(cls.tester.qdrouterd("C", config_c, wait=False))
        cls.routers.append(cls.tester.qdrouterd("D", config_d, wait=False))
        cls.routers.append(cls.tester.qdrouterd("E", config_e, wait=False))

        # Wait till listener is running and all expected connectors are connected
        cls.routers[0].wait_ports()
        for router in cls.connected_tls_sasl_routers[1:]:
            cls.routers[0].wait_router_connected(router)

    def get_router_nodes(self):
        """
        Retrieves connected router nodes.
        :return:
        """
        if not SASL.extended():
            self.skipTest("Cyrus library not available. skipping test")

        url = Url("amqp://0.0.0.0:%d/$management" % self.PORT_NO_SSL)
        node = Node.connect(url)
        response = node.query(type="org.apache.qpid.dispatch.router.node", attribute_names=["id"])
        router_nodes = []
        for resp in response.get_dicts():
            router_nodes.append(resp['id'])
        node.close()
        return router_nodes

    @unittest.skipIf(RouterTestSslBase.DISABLE_SSL_TESTING or not SASL.extended(),
                     "Cyrus library not available. skipping test")
    def test_connected_tls_sasl_routers(self):
        """
        Validates if all expected routers are connected in the network
        """
        if not SASL.extended():
            self.skipTest("Cyrus library not available. skipping test")

        router_nodes = self.get_router_nodes()
        self.assertTrue(router_nodes)
        for node in router_nodes:
            self.assertIn(node, self.connected_tls_sasl_routers)

        # Router A and B are always expected (no tls version restriction)
        expected_nodes = len(self.connected_tls_sasl_routers)

        # Router C only if TLSv1.2 is allowed
        if not RouterTestSslClient.OPENSSL_ALLOW_TLSV1_2:
            expected_nodes -= 1

        # Router D only if TLSv1.1 is allowed
        if not RouterTestSslClient.OPENSSL_ALLOW_TLSV1_1:
            expected_nodes -= 1

        self.assertEqual(len(router_nodes), expected_nodes)


class RouterTestSslInterRouterWithInvalidPathToCA(RouterTestSslBase):
    """
    DISPATCH-1762
    Starts 2 routers:
       Router A two listeners serve a normal, good certificate
       Router B two connectors configured with an invalid CA file path in its profile
          - one sets verifyHostname true, the other false.
    Test proves:
       Router B must not connect to A with mis-configured CA file path regardless of
       verifyHostname setting.
    """
    # Listener ports for each TLS protocol definition
    PORT_NO_SSL  = 0
    PORT_TLS_ALL = 0

    @classmethod
    def setUpClass(cls):
        """
        Prepares 2 routers to form a network.
        """
        super(RouterTestSslInterRouterWithInvalidPathToCA, cls).setUpClass()

        if not SASL.extended():
            return

        os.environ["ENV_SASL_PASSWORD"] = "password"

        # Generate authentication DB
        super(RouterTestSslInterRouterWithInvalidPathToCA, cls).create_sasl_files()

        # Router expected to be connected
        cls.connected_tls_sasl_routers = []

        # Generated router list
        cls.routers = []

        # Saving listener ports for each TLS definition
        cls.PORT_NO_SSL = cls.tester.get_port()
        cls.PORT_TLS_ALL_1 = cls.tester.get_port()
        cls.PORT_TLS_ALL_2 = cls.tester.get_port()

        # Configured connector host
        cls.CONNECTOR_HOST = "localhost"

        config_a = Qdrouterd.Config([
            ('router', {'id': 'QDR.A',
                        'mode': 'interior',
                        'saslConfigName': 'tests-mech-PLAIN',
                        'saslConfigDir': os.getcwd()}),
            # No auth and no SSL for management access
            ('listener', {'host': '0.0.0.0', 'role': 'normal', 'port': cls.PORT_NO_SSL}),
            # All TLS versions and normal, good sslProfile config
            ('listener', {'host': '0.0.0.0', 'role': 'inter-router', 'port': cls.PORT_TLS_ALL_1,
                          'saslMechanisms': 'PLAIN',
                          'requireEncryption': 'yes', 'requireSsl': 'yes',
                          'sslProfile': 'ssl-profile-tls-all'}),
            # All TLS versions and normal, good sslProfile config
            ('listener', {'host': '0.0.0.0', 'role': 'inter-router', 'port': cls.PORT_TLS_ALL_2,
                          'saslMechanisms': 'PLAIN',
                          'requireEncryption': 'yes', 'requireSsl': 'yes',
                          'sslProfile': 'ssl-profile-tls-all'}),
            # SSL Profile for all TLS versions (protocols element not defined)
            ('sslProfile', {'name': 'ssl-profile-tls-all',
                            'caCertFile': cls.ssl_file('ca-certificate.pem'),
                            'certFile': cls.ssl_file('server-certificate.pem'),
                            'privateKeyFile': cls.ssl_file('server-private-key.pem'),
                            'ciphers': 'ECDH+AESGCM:DH+AESGCM:ECDH+AES256:DH+AES256:ECDH+AES128:' \
                                       'DH+AES:RSA+AESGCM:RSA+AES:!aNULL:!MD5:!DSS',
                            'password': 'server-password'})
        ])

        # Router B has a connector to listener that allows all protocols but will not verify hostname.
        # The sslProfile has a bad caCertFile name and this router should not connect.
        config_b = Qdrouterd.Config([
            ('router', {'id': 'QDR.B',
                        'mode': 'interior'}),
            # Connector to All TLS versions allowed listener
            ('connector', {'name': 'connector1',
                           'host': cls.CONNECTOR_HOST, 'role': 'inter-router',
                           'port': cls.PORT_TLS_ALL_1,
                           'verifyHostname': 'no', 'saslMechanisms': 'PLAIN',
                           'saslUsername': 'test@domain.com', 'saslPassword': 'pass:password',
                           'sslProfile': 'ssl-profile-tls-all'}),
            # Connector to All TLS versions allowed listener
            ('connector', {'name': 'connector2',
                           'host': cls.CONNECTOR_HOST, 'role': 'inter-router',
                           'port': cls.PORT_TLS_ALL_2,
                           'verifyHostname': 'yes', 'saslMechanisms': 'PLAIN',
                           'saslUsername': 'test@domain.com', 'saslPassword': 'pass:password',
                           'sslProfile': 'ssl-profile-tls-all'}),
            # SSL Profile with an invalid caCertFile file path. The correct file path here would allow this
            # router to connect. The object is to trigger a specific failure in the ssl
            # setup chain of calls to pn_ssl_domain_* functions.
            ('sslProfile', {'name': 'ssl-profile-tls-all',
                            'caCertFile': cls.ssl_file('ca-certificate-INVALID-FILENAME.pem'),
                            'ciphers': 'ECDH+AESGCM:DH+AESGCM:ECDH+AES256:DH+AES256:ECDH+AES128:' \
                                       'DH+AES:RSA+AESGCM:RSA+AES:!aNULL:!MD5:!DSS'})
        ])

        cls.routers.append(cls.tester.qdrouterd("A", config_a, wait=False))
        cls.routers.append(cls.tester.qdrouterd("B", config_b, wait=False))

        # Wait until A is running
        cls.routers[0].wait_ports()

        # Can't wait until B is connected because it's not supposed to connect.

    def get_router_nodes(self):
        """
        Retrieves connected router nodes from QDR.A
        :return: list of connected router id's
        """
        if not SASL.extended():
            self.skipTest("Cyrus library not available. skipping test")

        url = Url("amqp://0.0.0.0:%d/$management" % self.PORT_NO_SSL)
        node = Node.connect(url)
        response = node.query(type="org.apache.qpid.dispatch.router.node", attribute_names=["id"])
        router_nodes = []
        for resp in response.get_dicts():
            router_nodes.append(resp['id'])
        node.close()
        return router_nodes

    def test_invalid_ca_path(self):
        """
        Prove sslProfile with invalid path to CA prevents the router from joining the network
        """
        if not SASL.extended():
            self.skipTest("Cyrus library not available. skipping test")

        # Poll for a while until the connector error shows up in router B's log
        pattern = " SERVER (error) SSL CA configuration failed"
        host_port_1 = self.CONNECTOR_HOST + ":" + str(self.PORT_TLS_ALL_1)
        host_port_2 = self.CONNECTOR_HOST + ":" + str(self.PORT_TLS_ALL_2)
        sleep_time = 0.1  # seconds
        poll_duration = 60.0  # seconds
        verified = False
        for tries in range(int(poll_duration / sleep_time)):
            logfile = os.path.join(self.routers[1].outdir, self.routers[1].logfile)
            if os.path.exists(logfile):
                with open(logfile, 'r') as router_log:
                    log_lines = router_log.read().split("\n")
                e1_lines = [s for s in log_lines if pattern in s and host_port_1 in s]
                e2_lines = [s for s in log_lines if pattern in s and host_port_2 in s]
                verified = len(e1_lines) > 0 and len(e2_lines) > 0
                if verified:
                    break
            time.sleep(sleep_time)
        self.assertTrue(verified, "Log line containing '%s' not seen for both connectors in QDR.B log" % pattern)

        verified = False
        pattern1 = "Connection to %s failed:" % host_port_1
        pattern2 = "Connection to %s failed:" % host_port_2
        for tries in range(int(poll_duration / sleep_time)):
            logfile = os.path.join(self.routers[1].outdir, self.routers[1].logfile)
            if os.path.exists(logfile):
                with open(logfile, 'r') as router_log:
                    log_lines = router_log.read().split("\n")
                e1_lines = [s for s in log_lines if pattern1 in s]
                e2_lines = [s for s in log_lines if pattern2 in s]
                verified = len(e1_lines) > 0 and len(e2_lines) > 0
                if verified:
                    break
            time.sleep(sleep_time)
        self.assertTrue(verified, "Log line containing '%s' or '%s' not seen in QDR.B log" % (pattern1, pattern2))

        # Show that router A does not have router B in its network
        router_nodes = self.get_router_nodes()
        self.assertTrue(router_nodes)
        node = "QDR.B"
        self.assertNotIn(node, router_nodes, msg=("%s should not be connected" % node))


class RouterTestSslInterRouterWithoutHostnameVerificationAndMismatchedCA(RouterTestSslBase):
    """
    DISPATCH-1762
    Starts 2 routers:
       Router A listener serves a normal, good certificate.
       Router B connector is configured with a CA cert that did not sign the server cert, and verifyHostname is false.
    Test proves:
       Router B must not connect to A.
    """
    # Listener ports for each TLS protocol definition
    PORT_NO_SSL  = 0
    PORT_TLS_ALL = 0

    @classmethod
    def setUpClass(cls):
        """
        Prepares 2 routers to form a network.
        """
        super(RouterTestSslInterRouterWithoutHostnameVerificationAndMismatchedCA, cls).setUpClass()

        if not SASL.extended():
            return

        os.environ["ENV_SASL_PASSWORD"] = "password"

        # Generate authentication DB
        super(RouterTestSslInterRouterWithoutHostnameVerificationAndMismatchedCA, cls).create_sasl_files()

        # Router expected to be connected
        cls.connected_tls_sasl_routers = []

        # Generated router list
        cls.routers = []

        # Saving listener ports for each TLS definition
        cls.PORT_NO_SSL = cls.tester.get_port()
        cls.PORT_TLS_ALL = cls.tester.get_port()

        config_a = Qdrouterd.Config([
            ('router', {'id': 'QDR.A',
                        'mode': 'interior',
                        'saslConfigName': 'tests-mech-PLAIN',
                        'saslConfigDir': os.getcwd()}),
            # No auth and no SSL for management access
            ('listener', {'host': '0.0.0.0', 'role': 'normal', 'port': cls.PORT_NO_SSL}),
            # All TLS versions and normal, good sslProfile config
            ('listener', {'host': '0.0.0.0', 'role': 'inter-router', 'port': cls.PORT_TLS_ALL,
                          'saslMechanisms': 'PLAIN',
                          'requireEncryption': 'yes', 'requireSsl': 'yes',
                          'sslProfile': 'ssl-profile-tls-all'}),
            # SSL Profile for all TLS versions (protocols element not defined)
            ('sslProfile', {'name': 'ssl-profile-tls-all',
                            'caCertFile': cls.ssl_file('ca-certificate.pem'),
                            'certFile': cls.ssl_file('server-certificate.pem'),
                            'privateKeyFile': cls.ssl_file('server-private-key.pem'),
                            'ciphers': 'ECDH+AESGCM:DH+AESGCM:ECDH+AES256:DH+AES256:ECDH+AES128:' \
                                       'DH+AES:RSA+AESGCM:RSA+AES:!aNULL:!MD5:!DSS',
                            'password': 'server-password'})
        ])

        # Router B has a connector to listener that allows all protocols but will not verify hostname.
        # The sslProfile has a caCertFile that does not sign the server cert, so this router should not connect.
        config_b = Qdrouterd.Config([
            ('router', {'id': 'QDR.B',
                        'mode': 'interior'}),
            # Connector to All TLS versions allowed listener
            ('connector', {'host': 'localhost', 'role': 'inter-router', 'port': cls.PORT_TLS_ALL,
                           'verifyHostname': 'no', 'saslMechanisms': 'PLAIN',
                           'saslUsername': 'test@domain.com', 'saslPassword': 'pass:password',
                           'sslProfile': 'ssl-profile-tls-all'}),
            # SSL Profile with caCertFile to cert that does not sign the server cert. The correct path here would allow this
            # router to connect. The object is to trigger a certificate verification failure while hostname verification is off.
            ('sslProfile', {'name': 'ssl-profile-tls-all',
                            'caCertFile': cls.ssl_file('bad-ca-certificate.pem'),
                            'ciphers': 'ECDH+AESGCM:DH+AESGCM:ECDH+AES256:DH+AES256:ECDH+AES128:' \
                                       'DH+AES:RSA+AESGCM:RSA+AES:!aNULL:!MD5:!DSS'})
        ])

        cls.routers.append(cls.tester.qdrouterd("A", config_a, wait=False))
        cls.routers.append(cls.tester.qdrouterd("B", config_b, wait=False))

        # Wait until A is running
        cls.routers[0].wait_ports()

        # Can't wait until B is connected because it's not supposed to connect.

    def get_router_nodes(self):
        """
        Retrieves connected router nodes from QDR.A
        :return: list of connected router id's
        """
        if not SASL.extended():
            self.skipTest("Cyrus library not available. skipping test")

        url = Url("amqp://0.0.0.0:%d/$management" % self.PORT_NO_SSL)
        node = Node.connect(url)
        response = node.query(type="org.apache.qpid.dispatch.router.node", attribute_names=["id"])
        router_nodes = []
        for resp in response.get_dicts():
            router_nodes.append(resp['id'])
        node.close()
        return router_nodes

    def test_mismatched_ca_and_no_hostname_verification(self):
        """
        Prove that improperly configured ssl-enabled connector prevents the router
        from joining the network
        """
        if not SASL.extended():
            self.skipTest("Cyrus library not available. skipping test")

        # Poll for a while until the connector error shows up in router B's log
        pattern = "Connection to localhost:%s failed:" % self.PORT_TLS_ALL
        sleep_time = 0.1  # seconds
        poll_duration = 60.0  # seconds
        verified = False
        for tries in range(int(poll_duration / sleep_time)):
            logfile = os.path.join(self.routers[1].outdir, self.routers[1].logfile)
            if os.path.exists(logfile):
                with open(logfile, 'r') as router_log:
                    log_lines = router_log.read().split("\n")
                e_lines = [s for s in log_lines if pattern in s]
                verified = len(e_lines) > 0
                if verified:
                    break
            time.sleep(sleep_time)
        self.assertTrue(verified, "Log line containing '%s' not seen in QDR.B log" % pattern)

        # Show that router A does not have router B in its network
        router_nodes = self.get_router_nodes()
        self.assertTrue(router_nodes)
        node = "QDR.B"
        self.assertNotIn(node, router_nodes, msg=("%s should not be connected" % node))


if __name__ == '__main__':
    unittest.main(main_module())

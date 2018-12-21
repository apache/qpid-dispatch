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
from subprocess import Popen, PIPE
from qpid_dispatch.management.client import Node
from system_test import TestCase, main_module, Qdrouterd, DIR, SkipIfNeeded
from proton import SASL, Url, SSLDomain
from proton.utils import BlockingConnection
import proton
import cproton
import unittest2 as unittest


class RouterTestSslBase(TestCase):
    """
    Base class to help with SSL related testing.
    """
    # If unable to determine which protocol versions are allowed system wide
    DISABLE_SSL_TESTING = False

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
    PORT_TLS1_TLS11 = 0
    PORT_TLS1_TLS12 = 0
    PORT_TLS11_TLS12 = 0
    PORT_TLS_ALL = 0
    PORT_TLS_SASL = 0
    PORT_SSL3 = 0
    TIMEOUT = 3

    # If using OpenSSL 1.1 or greater, TLSv1.2 is always being allowed
    OPENSSL_VER_1_1_GT = ssl.OPENSSL_VERSION_INFO[:2] >= (1, 1)

    # Following variables define TLS versions allowed by openssl
    OPENSSL_MIN_VER = 0
    OPENSSL_MAX_VER = 9999
    OPENSSL_ALLOW_TLSV1 = True
    OPENSSL_ALLOW_TLSV1_1 = True
    OPENSSL_ALLOW_TLSV1_2 = True

    # When using OpenSSL >= 1.1 and python >= 3.7, we can retrieve OpenSSL min and max protocols
    if OPENSSL_VER_1_1_GT:
        if sys.version_info >= (3, 7):
            OPENSSL_CTX = ssl.create_default_context()
            OPENSSL_MIN_VER = OPENSSL_CTX.minimum_version
            OPENSSL_MAX_VER = OPENSSL_CTX.maximum_version if OPENSSL_CTX.maximum_version > 0 else 9999
            OPENSSL_ALLOW_TLSV1 = OPENSSL_MIN_VER <= ssl.TLSVersion.TLSv1 <= OPENSSL_MAX_VER
            OPENSSL_ALLOW_TLSV1_1 = OPENSSL_MIN_VER <= ssl.TLSVersion.TLSv1_1 <= OPENSSL_MAX_VER
            OPENSSL_ALLOW_TLSV1_2 = OPENSSL_MIN_VER <= ssl.TLSVersion.TLSv1_2 <= OPENSSL_MAX_VER
        else:
            # At this point we are not able to precisely determine what are the minimum and maximum
            # TLS versions allowed in the system, so tests will be disabled
            RouterTestSslBase.DISABLE_SSL_TESTING = True

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
        # Enforcing given TLS protocol
        cproton.pn_ssl_domain_set_protocols(domain._domain, tls_protocol)

        # Try opening the secure and authenticated connection
        try:
            connection = BlockingConnection(url, sasl_enabled=False, ssl_domain=domain, timeout=self.TIMEOUT)
        except proton.Timeout:
            return False
        except proton.ConnectionException:
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
        (tlsv1, tlsv1_1, tlsv1_2) = expected_results
        return [self.OPENSSL_ALLOW_TLSV1 and tlsv1,
                self.OPENSSL_ALLOW_TLSV1_1 and tlsv1_1,
                self.OPENSSL_VER_1_1_GT or (self.OPENSSL_ALLOW_TLSV1_2 and tlsv1_2)]

    @SkipIfNeeded(RouterTestSslBase.DISABLE_SSL_TESTING, "Unable to determine MinProtocol")
    def test_tls1_only(self):
        """
        Expects TLSv1 only is allowed
        """
        self.assertEquals(self.get_expected_tls_result([True, False, False]),
                          self.get_allowed_protocols(self.PORT_TLS1))

    @SkipIfNeeded(RouterTestSslBase.DISABLE_SSL_TESTING, "Unable to determine MinProtocol")
    def test_tls11_only(self):
        """
        Expects TLSv1.1 only is allowed
        """
        self.assertEquals(self.get_expected_tls_result([False, True, False]),
                          self.get_allowed_protocols(self.PORT_TLS11))

    @SkipIfNeeded(RouterTestSslBase.DISABLE_SSL_TESTING, "Unable to determine MinProtocol")
    def test_tls12_only(self):
        """
        Expects TLSv1.2 only is allowed
        """
        self.assertEquals(self.get_expected_tls_result([False, False, True]),
                          self.get_allowed_protocols(self.PORT_TLS12))

    @SkipIfNeeded(RouterTestSslBase.DISABLE_SSL_TESTING, "Unable to determine MinProtocol")
    def test_tls1_tls11_only(self):
        """
        Expects TLSv1 and TLSv1.1 only are allowed
        """
        self.assertEquals(self.get_expected_tls_result([True, True, False]),
                          self.get_allowed_protocols(self.PORT_TLS1_TLS11))

    @SkipIfNeeded(RouterTestSslBase.DISABLE_SSL_TESTING, "Unable to determine MinProtocol")
    def test_tls1_tls12_only(self):
        """
        Expects TLSv1 and TLSv1.2 only are allowed
        """
        self.assertEquals(self.get_expected_tls_result([True, False, True]),
                          self.get_allowed_protocols(self.PORT_TLS1_TLS12))

    @SkipIfNeeded(RouterTestSslBase.DISABLE_SSL_TESTING, "Unable to determine MinProtocol")
    def test_tls11_tls12_only(self):
        """
        Expects TLSv1.1 and TLSv1.2 only are allowed
        """
        self.assertEquals(self.get_expected_tls_result([False, True, True]),
                          self.get_allowed_protocols(self.PORT_TLS11_TLS12))

    @SkipIfNeeded(RouterTestSslBase.DISABLE_SSL_TESTING, "Unable to determine MinProtocol")
    def test_tls_all(self):
        """
        Expects all supported versions: TLSv1, TLSv1.1 and TLSv1.2 to be allowed
        """
        self.assertEquals(self.get_expected_tls_result([True, True, True]),
                          self.get_allowed_protocols(self.PORT_TLS_ALL))

    @SkipIfNeeded(RouterTestSslBase.DISABLE_SSL_TESTING, "Unable to determine MinProtocol")
    def test_ssl_invalid(self):
        """
        Expects connection is rejected as SSL is no longer supported
        """
        self.assertEqual(False, self.is_proto_allowed(self.PORT_SSL3, 'SSLv3'))

    @SkipIfNeeded(not SASL.extended(), "Cyrus library not available. skipping test")
    def test_ssl_sasl_client_valid(self):
        """
        Attempts to connect a Proton client using a valid SASL authentication info
        and forcing the TLS protocol version, which should be accepted by the listener.
        :return:
        """
        if not SASL.extended():
            self.skipTest("Cyrus library not available. skipping test")

        self.assertTrue(self.is_ssl_sasl_client_accepted(self.PORT_TLS_SASL, "TLSv1"))
        self.assertTrue(self.is_ssl_sasl_client_accepted(self.PORT_TLS_SASL, "TLSv1.2"))

    @SkipIfNeeded(not SASL.extended(), "Cyrus library not available. skipping test")
    def test_ssl_sasl_client_invalid(self):
        """
        Attempts to connect a Proton client using a valid SASL authentication info
        and forcing the TLS protocol version, which should be rejected by the listener.
        :return:
        """
        if not SASL.extended():
            self.skipTest("Cyrus library not available. skipping test")

        self.assertFalse(self.is_ssl_sasl_client_accepted(self.PORT_TLS_SASL, "TLSv1.1"))


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
                           'saslUsername': 'test@domain.com', 'saslPassword': 'password',
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
                           'saslUsername': 'test@domain.com', 'saslPassword': 'password',
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
                           'saslUsername': 'test@domain.com', 'saslPassword': 'password',
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

    @SkipIfNeeded(not SASL.extended(), "Cyrus library not available. skipping test")
    def test_connected_tls_sasl_routers(self):
        """
        Validates if all expected routers are connected in the network
        """
        if not SASL.extended():
            self.skipTest("Cyrus library not available. skipping test")

        router_nodes = self.get_router_nodes()
        self.assertTrue(router_nodes)
        for node in router_nodes:
            self.assertTrue(node in self.connected_tls_sasl_routers,
                            "%s should not be connected" % node)
        self.assertEqual(len(router_nodes), 4)


if __name__ == '__main__':
    unittest.main(main_module())

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
import socket
import ssl
import os
import unittest2 as unittest
from system_test import TestCase, main_module, Qdrouterd, DIR


class RouterTestSsl(TestCase):

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
    PORT_SSL3 = 0
    TIMEOUT = 3

    @staticmethod
    def ssl_file(name):
        """
        Returns fully qualified ssl certificate file name
        :param name:
        :return:
        """
        return os.path.join(DIR, 'ssl_certs', name)

    @classmethod
    def setUpClass(cls):
        """
        Prepares a single router with multiple listeners, each one associated with a particular
        sslProfile and each sslProfile has its own specific set of allowed protocols.
        """
        super(RouterTestSsl, cls).setUpClass()

        cls.routers = []

        # Saving listener ports for each TLS definition
        cls.PORT_TLS1 = cls.tester.get_port()
        cls.PORT_TLS11 = cls.tester.get_port()
        cls.PORT_TLS12 = cls.tester.get_port()
        cls.PORT_TLS1_TLS11 = cls.tester.get_port()
        cls.PORT_TLS1_TLS12 = cls.tester.get_port()
        cls.PORT_TLS11_TLS12 = cls.tester.get_port()
        cls.PORT_TLS_ALL = cls.tester.get_port()
        cls.PORT_SSL3 = cls.tester.get_port()

        config = Qdrouterd.Config([
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
                          'sslProfile': 'ssl-profile-ssl3'}),
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
                            'password': 'server-password'}),
            ('router', {'id': 'QDR.A',
                        'mode': 'interior'})
        ])

        cls.routers.append(cls.tester.qdrouterd("A", config, wait=False))
        cls.routers[0].wait_ports()

    def is_proto_allowed(self, listener_port, tls_protocol):
        """
        :param listener_port: TCP port number
        :param tls_protocol: ssl.PROTOCOL_TLSv1, ssl.PROTOCOL_TLSv1_1 or ssl.PROTOCOL_TLSv1_2
        :return:
        """
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(self.TIMEOUT)
        ssl_sock = ssl.wrap_socket(sock, ssl_version=tls_protocol)

        try:
            ssl_sock.connect(("0.0.0.0", listener_port))
        except ssl.SSLError:
            return False
        except socket.error:
            return False
        finally:
            ssl_sock.close()

        return True

    def test_tls1_only(self):
        """
        Expects TLSv1 only is allowed
        """
        self.assertEqual(True, self.is_proto_allowed(self.PORT_TLS1, ssl.PROTOCOL_TLSv1))
        self.assertEqual(False, self.is_proto_allowed(self.PORT_TLS1, ssl.PROTOCOL_TLSv1_1))
        self.assertEqual(False, self.is_proto_allowed(self.PORT_TLS1, ssl.PROTOCOL_TLSv1_2))

    def test_tls11_only(self):
        """
        Expects TLSv1.1 only is allowed
        """
        self.assertEqual(False, self.is_proto_allowed(self.PORT_TLS11, ssl.PROTOCOL_TLSv1))
        self.assertEqual(True, self.is_proto_allowed(self.PORT_TLS11, ssl.PROTOCOL_TLSv1_1))
        self.assertEqual(False, self.is_proto_allowed(self.PORT_TLS11, ssl.PROTOCOL_TLSv1_2))

    def test_tls12_only(self):
        """
        Expects TLSv1.2 only is allowed
        """
        self.assertEqual(False, self.is_proto_allowed(self.PORT_TLS12, ssl.PROTOCOL_TLSv1))
        self.assertEqual(False, self.is_proto_allowed(self.PORT_TLS12, ssl.PROTOCOL_TLSv1_1))
        self.assertEqual(True, self.is_proto_allowed(self.PORT_TLS12, ssl.PROTOCOL_TLSv1_2))

    def test_tls1_tls11_only(self):
        """
        Expects TLSv1 and TLSv1.1 only are allowed
        """
        self.assertEqual(True, self.is_proto_allowed(self.PORT_TLS1_TLS11, ssl.PROTOCOL_TLSv1))
        self.assertEqual(True, self.is_proto_allowed(self.PORT_TLS1_TLS11, ssl.PROTOCOL_TLSv1_1))
        self.assertEqual(False, self.is_proto_allowed(self.PORT_TLS1_TLS11, ssl.PROTOCOL_TLSv1_2))

    def test_tls1_tls12_only(self):
        """
        Expects TLSv1 and TLSv1.2 only are allowed
        """
        self.assertEqual(True, self.is_proto_allowed(self.PORT_TLS1_TLS12, ssl.PROTOCOL_TLSv1))
        self.assertEqual(False, self.is_proto_allowed(self.PORT_TLS1_TLS12, ssl.PROTOCOL_TLSv1_1))
        self.assertEqual(True, self.is_proto_allowed(self.PORT_TLS1_TLS12, ssl.PROTOCOL_TLSv1_2))

    def test_tls11_tls12_only(self):
        """
        Expects TLSv1.1 and TLSv1.2 only are allowed
        """
        self.assertEqual(False, self.is_proto_allowed(self.PORT_TLS11_TLS12, ssl.PROTOCOL_TLSv1))
        self.assertEqual(True, self.is_proto_allowed(self.PORT_TLS11_TLS12, ssl.PROTOCOL_TLSv1_1))
        self.assertEqual(True, self.is_proto_allowed(self.PORT_TLS11_TLS12, ssl.PROTOCOL_TLSv1_2))

    def test_tls_all(self):
        """
        Expects all supported versions: TLSv1, TLSv1.1 and TLSv1.2 to be allowed
        """
        self.assertEqual(True, self.is_proto_allowed(self.PORT_TLS_ALL, ssl.PROTOCOL_TLSv1))
        self.assertEqual(True, self.is_proto_allowed(self.PORT_TLS_ALL, ssl.PROTOCOL_TLSv1_1))
        self.assertEqual(True, self.is_proto_allowed(self.PORT_TLS_ALL, ssl.PROTOCOL_TLSv1_2))

    def test_ssl_invalid(self):
        """
        Expects connection is rejected as SSL is no longer supported
        """
        self.assertEqual(False, self.is_proto_allowed(self.PORT_SSL3, ssl.PROTOCOL_SSLv23))


if __name__ == '__main__':
    unittest.main(main_module())

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

import unittest, os
from subprocess import PIPE

from system_test import TestCase, Qdrouterd, main_module

class RouterTestPlainSasl(TestCase):

    @classmethod
    def createSasldb(cls):
        pass


    @classmethod
    def setUpClass(cls):
        """
        Tests the sasl_username, sasl_password property of the dispatch router.

        Creates two routers (QDR.X and QDR.Y) and sets up PLAIN authentication on QDR.X.
        QDR.Y connects to QDR.X by providing a sasl_username and a sasl_password.

        """
        super(RouterTestPlainSasl, cls).setUpClass()

        def router(name, connection):

            config = [
                ('router', {'mode': 'interior', 'routerId': 'QDR.%s'%name}),
                ('fixedAddress', {'prefix': '/closest/', 'fanout': 'single', 'bias': 'closest'}),
                ('fixedAddress', {'prefix': '/spread/', 'fanout': 'single', 'bias': 'spread'}),
                ('fixedAddress', {'prefix': '/multicast/', 'fanout': 'multiple'}),
                ('fixedAddress', {'prefix': '/', 'fanout': 'multiple'}),

            ] + connection

            config = Qdrouterd.Config(config)
            cls.routers.append(cls.tester.qdrouterd(name, config, wait=False))

        cls.routers = []

        x_listener_port = cls.tester.get_port()
        y_listener_port = cls.tester.get_port()

        current_path = os.path.dirname(os.path.abspath(__file__))
        sasl_config_path = current_path[:len(current_path)-5] + 'build/tests/sasl_configs'

        router('X', [
                     ('listener', {'addr': '0.0.0.0', 'role': 'inter-router', 'port': x_listener_port,
                                   'saslMechanisms':'PLAIN DIGEST-MD5', 'authenticatePeer': 'yes'}),
                     # This unauthenticated listener is for qdstat to connect to it.
                     ('listener', {'addr': '0.0.0.0', 'role': 'normal', 'port': cls.tester.get_port(),
                                   'authenticatePeer': 'no'}),
                     ('container', {'workerThreads': 4, 'containerName': 'Qpid.Dispatch.Router.X',
                                    'saslConfigName': 'tests-mech-PLAIN',
                                    'saslConfigPath': sasl_config_path}),
        ])

        router('Y', [
                     ('connector', {'addr': '0.0.0.0', 'role': 'inter-router', 'port': x_listener_port,
                                    # Provide a sasl user name and password to connect to QDR.X
                                   'saslMechanisms': 'PLAIN DIGEST-MD5', 'saslUsername': 'test', 'saslPassword': 'password'}),
                     ('container', {'workerThreads': 4, 'containerName': 'Qpid.Dispatch.Router.Y'}),
                     ('listener', {'addr': '0.0.0.0', 'role': 'normal', 'port': y_listener_port}),
        ])

        cls.routers[1].wait_router_connected('QDR.X')

    def test_inter_router_plain_exists(self):
        """
        The setUpClass sets up two routers with SASL PLAIN enabled.
        This test makes executes a qdstat -c via an unauthenticated listener to QDR.X and makes sure that the output
        has an "inter-router" connection to QDR.Y whose authentication is PLAIN. This ensures that QDR.Y did not
        somehow use SASL ANONYMOUS to connect to QDR.X
        """
        p = self.popen(
            ['qdstat', '-b', str(self.routers[0].addresses[1]), '-c'],
            name='qdstat-'+self.id(), stdout=PIPE, expect=None)
        out = p.communicate()[0]
        assert p.returncode == 0, \
            "qdstat exit status %s, output:\n%s" % (p.returncode, out)

        assert "inter-router" in out
        assert "test(PLAIN)" in out

if __name__ == '__main__':
    unittest.main(main_module())


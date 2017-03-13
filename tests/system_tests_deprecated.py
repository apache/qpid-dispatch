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
from system_test import TestCase, Qdrouterd, TIMEOUT
from system_tests_sasl_plain import RouterTestPlainSaslCommon
from qpid_dispatch.management.client import Node
from proton import SASL

class RouterTestDeprecated(RouterTestPlainSaslCommon):

    @classmethod
    def setUpClass(cls):
        """
        Creates two routers (QDR.X and QDR.Y) and sets up PLAIN authentication on QDR.X.
        QDR.Y connects to QDR.X by providing a sasl_username and a sasl_password.

        """
        super(RouterTestDeprecated, cls).setUpClass()

        if not SASL.extended():
            return

        super(RouterTestDeprecated, cls).createSaslFiles()

        cls.routers = []

        x_listener_port = cls.tester.get_port()
        y_listener_port = cls.tester.get_port()

        super(RouterTestDeprecated, cls).router('X', [
                     ('listener', {'addr': '0.0.0.0', 'role': 'inter-router', 'port': x_listener_port,
                                   'saslMechanisms':'PLAIN', 'authenticatePeer': 'yes'}),
                     # This unauthenticated listener is for qdstat to connect to it.
                     ('listener', {'addr': '0.0.0.0', 'role': 'normal', 'port': cls.tester.get_port(),
                                   'authenticatePeer': 'no'}),
                     ('container', {'workerThreads': 1,
                                    'containerName': 'Qpid.Dispatch.Router.A',
                                    'saslConfigName': 'tests-mech-PLAIN',
                                    'saslConfigPath': os.getcwd()}),
                     ('linkRoutePattern', {'prefix': 'org.apache'}),
                     ('router', {'routerId': 'QDR.X', 'mode': 'interior'}),
                     ('fixedAddress', {'prefix': '/closest/', 'fanout': 'single', 'bias': 'closest'}),
                     ('fixedAddress', {'prefix': '/spread/', 'fanout': 'single', 'bias': 'spread'}),
                     ('fixedAddress', {'prefix': '/multicast/', 'fanout': 'multiple'}),
                     ('fixedAddress', {'prefix': '/', 'fanout': 'multiple'}),
        ])

        super(RouterTestDeprecated, cls).router('Y', [
                     ('connector', {'addr': '0.0.0.0', 'role': 'inter-router',
                                    'port': x_listener_port,
                                    'saslMechanisms': 'PLAIN',
                                    'saslUsername': 'test@domain.com',
                                    'saslPassword': 'password'}),

                     ('router', {'mode': 'interior',
                                 'routerId': 'QDR.Y'}),
                     ('linkRoutePattern', {'prefix': 'org.apache'}),
                     ('container', {'workerThreads': 1,
                                    'containerName': 'Qpid.Dispatch.Router.Y'}),

                     ('listener', {'addr': '0.0.0.0',
                                   'role': 'normal',
                                   'port': y_listener_port}),
                     ('fixedAddress', {'prefix': '/closest/', 'fanout': 'single', 'bias': 'closest'}),
                     ('fixedAddress', {'prefix': '/spread/', 'fanout': 'single', 'bias': 'spread'}),
                     ('fixedAddress', {'prefix': '/multicast/', 'fanout': 'multiple'}),
                     ('fixedAddress', {'prefix': '/', 'fanout': 'multiple'}),
        ])

        cls.routers[1].wait_router_connected('QDR.X')

    def test_deprecated(self):
        """
        Tests deprecated attributes like linkRoutePattern, container, fixedAddress etc.
        This test makes executes a query for type='org.apache.qpid.dispatch.connection' over
        an unauthenticated listener to
        QDR.X and makes sure that the output has an "inter-router" connection to
        QDR.Y whose authentication is PLAIN. This ensures that QDR.Y did not
        somehow use SASL ANONYMOUS to connect to QDR.X
        Also makes sure that TLSv1/SSLv3 was used as sslProto

        """
        if not SASL.extended():
            self.skipTest("Cyrus library not available. skipping test")
            
        local_node = Node.connect(self.routers[0].addresses[1], timeout=TIMEOUT)

        # saslConfigName and saslConfigPath were set in the ContainerEntity. This tests makes sure that the
        # saslConfigName and saslConfigPath were loaded properly from the ContainerEntity.
        # ContainerEntity has been deprecated.

        # role should be inter-router
        self.assertEqual(u'inter-router', local_node.query(type='org.apache.qpid.dispatch.connection').results[0][3])

        # sasl must be plain
        self.assertEqual(u'PLAIN', local_node.query(type='org.apache.qpid.dispatch.connection').results[0][6])

        # user must be test@domain.com
        self.assertEqual(u'test@domain.com', local_node.query(type='org.apache.qpid.dispatch.connection').results[0][8])

        # Make sure that the deprecated linkRoutePattern is set up correctly
        query_response = local_node.query(type='org.apache.qpid.dispatch.router.config.linkRoute')

        self.assertEqual(2, len(query_response.results))
        self.assertEqual("in", query_response.results[0][7])
        self.assertEqual("out", query_response.results[1][7])

        results = local_node.query(type='org.apache.qpid.dispatch.router.config.address').results

        multicast_found = False
        spread_found = False
        closest_found = False

        for result in results:
            if result[3] == 'closest':
                closest_found = True
                self.assertEqual(result[4], 'closest')
            if result[3] == 'spread':
                spread_found = True
                self.assertEqual(result[4], 'balanced')
            if result[3] == 'multicast':
                multicast_found = True
                self.assertEqual(result[4], 'multicast')

        self.assertTrue(multicast_found)
        self.assertTrue(spread_found)
        self.assertTrue(closest_found)






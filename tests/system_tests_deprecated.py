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

from system_test import TestCase, Qdrouterd


class RouterTestDeprecatedLinkRoute(TestCase):

    @classmethod
    def setUpClass(cls):
        super(RouterTestDeprecatedLinkRoute, cls).setUpClass()

        name = "test-router"

        config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'QDR.A'}),
            # We are trying to declare a link route with both 'dir' and 'direction' attributes.
            # The router must not start in this case.
            ('linkRoute', {'prefix': 'org.apache', 'containerId': 'QDR.A', 'direction': 'in', 'dir': 'in'}),
            ('linkRoute', {'prefix': 'org.apache', 'containerId': 'QDR.A', 'direction': 'out'}),
            ('listener', {'port': cls.tester.get_port(), 'maxFrameSize': '2048', 'stripAnnotations': 'in'}),
        ])

        cls.router = cls.tester.qdrouterd(name, config, wait=False, perform_teardown=False)

        # Try connecting for 4 seconds. If unable to connect, just move on.
        try:
            cls.router.wait_ready(timeout=4)
        except:
            pass

    def test_deprecated_link_route(self):
        with open(self.router.outfile + '.out', 'r') as router_log:
            log_lines = router_log.read().split("\n")
            search_lines = [s for s in log_lines if "org.apache.qpid.dispatch.router.config.linkRoute: Both 'dir' and 'direction' cannot be specified for entity 'linkRoute'" in s]
            self.assertTrue(len(search_lines) > 0)


class RouterTestDeprecatedLAutoLink(TestCase):

    @classmethod
    def setUpClass(cls):
        super(RouterTestDeprecatedLAutoLink, cls).setUpClass()

        name = "test-autolink"

        config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'QDR.B'}),
            # We are trying to declare a link route with both 'dir' and 'direction' attributes.
            # The router must not start in this case.
            ('autoLink', {'externalAddr': 'node.100', 'addr': 'node.10',
                          'containerId': 'container.1',
                          'direction': 'out'}),
            ('autoLink', {'addr': 'node.1', 'containerId': 'container.1', 'dir': 'in', 'direction': 'in'}),
            ('autoLink', {'addr': 'node.1', 'containerId': 'container.1', 'direction': 'out'}),

            ('listener', {'port': cls.tester.get_port()}),
        ])
        cls.router = cls.tester.qdrouterd(name, config, wait=False, perform_teardown=False)

        # Try connecting for 4 seconds. If unable to connect, just move on.
        try:
            cls.router.wait_ready(timeout=4)
        except:
            pass

    def test_deprecated_auto_link(self):
        with open(self.router.outfile + '.out', 'r') as router_log:
            log_lines = router_log.read().split("\n")
            search_lines = [s for s in log_lines if
                            "org.apache.qpid.dispatch.router.config.autoLink: Both 'dir' and 'direction' cannot be specified for entity 'autoLink'" in s]
            self.assertTrue(len(search_lines) > 0)

            external_addr_search_lines = [s for s in log_lines if
                                          "Attribute 'externalAddr' of entity 'autoLink' has been deprecated. Use 'externalAddress' instead" in s]
            self.assertTrue(len(external_addr_search_lines) > 0)

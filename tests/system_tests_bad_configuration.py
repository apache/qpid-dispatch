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
Ensure router continues to work when configuration has some configurations,
that might cause problems, or caused issues in the past.
For example, unresolvable host names.
"""

import time
import re
from subprocess import PIPE, STDOUT
from system_test import TestCase, Qdrouterd, TIMEOUT, Process

class RouterTestBadConfiguration(TestCase):

    """
    This test case sets up a router using configurations that are not
    well defined, but are not supposed to cause a crash to the router
    process.
    """
    @classmethod
    def setUpClass(cls):
        """
        Set up router instance configuration to be used for testing
        :return:
        """
        super(RouterTestBadConfiguration, cls).setUpClass()
        cls.name = "test-router"
        cls.config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'QDR.A'}),
            # Define a connector that uses an unresolvable hostname
            ('connector',
             {'name': 'UnresolvableConn',
              'host': 'unresolvable.host.name',
              'port': 'amqp'}),
            ('listener',
             {'port': cls.tester.get_port()}),
        ])

        try:
            cls.router = cls.tester.qdrouterd(cls.name, cls.config, wait=False)
            # Wait a few seconds so the error can be logged
            time.sleep(3)
        except OSError:
            pass

    @classmethod
    def tearDownClass(cls):
        super(RouterTestBadConfiguration, cls).tearDownClass()

    def address(self):
        return self.router.addresses[0]

    def test_unresolvable_host(self):
        """
        Validates if router was able to recover from an unresolvable hostname.
        :return:
        """
        with open('../setUpClass/test-router.log', 'r') as router_log:
            log_lines = router_log.read().split("\n")
            regex = ".*(getaddrinfo|proton:io Name or service not known).*"
            errors_caught = [line for line in log_lines if re.match(regex, line)]

            self.assertGreater(len(errors_caught), 0)

    def test_qdmanage_query(self):
        p = self.popen(
            ['qdmanage', '-b', self.address(), 'query', '--type=router', '--timeout', str(TIMEOUT)],
            stdin=PIPE, stdout=PIPE, stderr=STDOUT, expect=Process.EXIT_OK)
        out = p.communicate()[0]
        try:
            p.teardown()
        except Exception, e:
            raise Exception("%s\n%s" % (e, out))
        return out

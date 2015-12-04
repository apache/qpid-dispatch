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

import sys
import unittest
from time import sleep
from system_test import TestCase, Qdrouterd, main_module

from proton import Message
from proton import ConnectionException
from proton.reactor import AtMostOnce
from proton.utils import BlockingConnection, LinkDetached

from qpid_dispatch.management.client import Node
from system_test import TIMEOUT

from qpid_dispatch_internal.management.policy import Policy, HostAddr, PolicyError

class AbsoluteConnectionCountLimit(TestCase):
    """
    Verify that connections beyond the absolute limit are denied
    """
    @classmethod
    def setUpClass(cls):
        """Start the router"""
        super(AbsoluteConnectionCountLimit, cls).setUpClass()
        config = Qdrouterd.Config([
            ('container', {'workerThreads': 4, 'containerName': 'Qpid.Dispatch.Router.Policy'}),
            ('router', {'mode': 'standalone', 'routerId': 'QDR.Policy'}),
            ('listener', {'port': cls.tester.get_port()}),
            ('policy', {'maximumConnections': 2})
        ])

        cls.router = cls.tester.qdrouterd('conn-limit-router', config, wait=True)

    def address(self):
        return self.router.addresses[0]

    def test_aaa_verify_maximum_connections(self):
        addr = self.address()

        # two connections should be ok
        denied = False
        try:
            bc1 = BlockingConnection(addr)
            bc2 = BlockingConnection(addr)
        except ConnectionException:
            denied = True

        self.assertFalse(denied) # connections that should open did not open

        # third connection should be denied
        denied = False
        try:
            bc3 = BlockingConnection(addr)
        except ConnectionException:
            denied = True

        self.assertTrue(denied) # connection that should not open did open

        bc1.close()
        bc2.close()

class PolicyHostAddrTest(TestCase):

    def expect_deny(self, badhostname, msg):
        denied = False
        try:
            xxx = HostAddr(badhostname)
        except PolicyError:
            denied = True
        self.assertTrue(denied) # msg

    def test_policy_hostaddr_ipv4(self):
        # Create simple host and range
        aaa = HostAddr("192.168.1.1")
        bbb = HostAddr("1.1.1.1,1.1.1.255")
        # Verify host and range
        self.assertTrue ( aaa.match("192.168.1.1") )
        self.assertFalse( aaa.match("1.1.1.1") )
        self.assertFalse( aaa.match("192.168.1.2") )
        self.assertTrue ( bbb.match("1.1.1.1") )
        self.assertTrue ( bbb.match("1.1.1.254") )
        self.assertFalse( bbb.match("1.1.1.0") )
        self.assertFalse( bbb.match("1.1.2.0") )

    def test_policy_hostaddr_ipv6(self):
        if not HostAddr.has_ipv6:
            self.skipTest("System IPv6 support is not available")
        # Create simple host and range
        aaa = HostAddr("::1")
        bbb = HostAddr("::1,::ffff")
        ccc = HostAddr("ffff::0,ffff:ffff::0")
        # Verify host and range
        self.assertTrue ( aaa.match("::1") )
        self.assertFalse( aaa.match("::2") )
        self.assertFalse( aaa.match("ffff:ffff::0") )
        self.assertTrue ( bbb.match("::1") )
        self.assertTrue ( bbb.match("::fffe") )
        self.assertFalse( bbb.match("::1:0") )
        self.assertFalse( bbb.match("ffff::0") )
        self.assertTrue ( ccc.match("ffff::1") )
        self.assertTrue ( ccc.match("ffff:fffe:ffff:ffff::ffff") )
        self.assertFalse( ccc.match("ffff:ffff:1") )
        self.assertFalse( ccc.match("ffff:ffff:ffff:ffff::ffff") )

    def test_policy_malformed_hostaddr_ipv4(self):
        self.expect_deny( "0.0.0.0.0", "Name or service not known")
        self.expect_deny( "1.1.1.1,2.2.2.2,3.3.3.3", "arg count")
        self.expect_deny( "9.9.9.9,8.8.8.8", "a > b")

    def test_policy_malformed_hostaddr_ipv6(self):
        if not HostAddr.has_ipv6:
            self.skipTest("System IPv6 support is not available")
        self.expect_deny( "1::2::3", "Name or service not known")
        self.expect_deny( "::1,::2,::3", "arg count")
        self.expect_deny( "0:ff:0,0:fe:ffff:ffff::0", "a > b")

if __name__ == '__main__':
    unittest.main(main_module())

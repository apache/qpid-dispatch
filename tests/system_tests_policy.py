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

from qpid_dispatch_internal.management.policy import \
    Policy, HostAddr, PolicyError, HostStruct, PolicyConnStatsPerApp

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
            xxx = HostStruct(badhostname)
        except PolicyError:
            denied = True
        self.assertTrue(denied, ("%s" % msg))

    def check_hostaddr_match(self, tHostAddr, tString, expectOk=True):
        # check that the string is a match for the addr
        # check that the internal struct version matches, too
        ha = HostStruct(tString)
        if expectOk:
            self.assertTrue( tHostAddr.match_str(tString) )
            self.assertTrue( tHostAddr.match_bin(ha) )
        else:
            self.assertFalse( tHostAddr.match_str(tString) )
            self.assertFalse( tHostAddr.match_bin(ha) )

    def test_policy_hostaddr_ipv4(self):
        # Create simple host and range
        aaa = HostAddr("192.168.1.1")
        bbb = HostAddr("1.1.1.1,1.1.1.255")
        # Verify host and range
        self.check_hostaddr_match(aaa, "192.168.1.1")
        self.check_hostaddr_match(aaa, "1.1.1.1", False)
        self.check_hostaddr_match(aaa, "192.168.1.2", False)
        self.check_hostaddr_match(bbb, "1.1.1.1")
        self.check_hostaddr_match(bbb, "1.1.1.254")
        self.check_hostaddr_match(bbb, "1.1.1.0", False)
        self.check_hostaddr_match(bbb, "1.1.2.0", False)

    def test_policy_hostaddr_ipv6(self):
        if not HostAddr.has_ipv6:
            self.skipTest("System IPv6 support is not available")
        # Create simple host and range
        aaa = HostAddr("::1")
        bbb = HostAddr("::1,::ffff")
        ccc = HostAddr("ffff::0,ffff:ffff::0")
        # Verify host and range
        self.check_hostaddr_match(aaa, "::1")
        self.check_hostaddr_match(aaa, "::2", False)
        self.check_hostaddr_match(aaa, "ffff:ffff::0", False)
        self.check_hostaddr_match(bbb, "::1")
        self.check_hostaddr_match(bbb, "::fffe")
        self.check_hostaddr_match(bbb, "::1:0", False)
        self.check_hostaddr_match(bbb, "ffff::0", False)
        self.check_hostaddr_match(ccc, "ffff::1")
        self.check_hostaddr_match(ccc, "ffff:fffe:ffff:ffff::ffff")
        self.check_hostaddr_match(ccc, "ffff:ffff::1", False)
        self.check_hostaddr_match(ccc, "ffff:ffff:ffff:ffff::ffff", False)

    def test_policy_hostaddr_ipv4_wildcard(self):
        aaa = HostAddr("*")
        self.check_hostaddr_match(aaa,"0.0.0.0")
        self.check_hostaddr_match(aaa,"127.0.0.1")
        self.check_hostaddr_match(aaa,"255.254.253.252")


    def test_policy_hostaddr_ipv6_wildcard(self):
        if not HostAddr.has_ipv6:
            self.skipTest("System IPv6 support is not available")
        aaa = HostAddr("*")
        self.check_hostaddr_match(aaa,"::0")
        self.check_hostaddr_match(aaa,"::1")
        self.check_hostaddr_match(aaa,"ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff")

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

class PolicyFile(TestCase):

    policy = Policy("../../../tests/policy-1")

    def dict_compare(self, d1, d2):
        d1_keys = set(d1.keys())
        d2_keys = set(d2.keys())
        intersect_keys = d1_keys.intersection(d2_keys)
        added = d1_keys - d2_keys
        removed = d2_keys - d1_keys
        modified = {o : (d1[o], d2[o]) for o in intersect_keys if d1[o] != d2[o]}
        same = set(o for o in intersect_keys if d1[o] == d2[o])
        return len(added) == 0 and len(removed) == 0 and len(modified) == 0

    def test_policy1_test_zeke_ok(self):
        upolicy = {}
        self.assertTrue( 
            PolicyFile.policy.policy_lookup('192.168.100.5:33333', 'zeke', '192.168.100.5', 'photoserver', upolicy) )
        self.assertTrue(upolicy['policyVersion']             == '1')
        self.assertTrue(upolicy['max_frame_size']            == 444444)
        self.assertTrue(upolicy['max_message_size']          == 444444)
        self.assertTrue(upolicy['max_session_window']        == 444444)
        self.assertTrue(upolicy['max_sessions']              == 4)
        self.assertTrue(upolicy['max_senders']               == 44)
        self.assertTrue(upolicy['max_receivers']             == 44)
        self.assertTrue(upolicy['allow_anonymous_sender'])
        self.assertTrue(upolicy['allow_dynamic_src'])
        self.assertTrue(len(upolicy['targets']) == 1)
        self.assertTrue('private' in upolicy['targets'])
        self.assertTrue(len(upolicy['sources']) == 1)
        self.assertTrue('private' in upolicy['sources'])

    def test_policy1_test_zeke_bad_IP(self):
        upolicy = {}
        self.assertFalse(
            PolicyFile.policy.policy_lookup('192.168.100.5:33333', 'zeke', '10.18.0.1',    'photoserver', upolicy) )
        self.assertFalse(
            PolicyFile.policy.policy_lookup('192.168.100.5:33333', 'zeke', '72.135.2.9',   'photoserver', upolicy) )
        self.assertFalse(
            PolicyFile.policy.policy_lookup('192.168.100.5:33333', 'zeke', '127.0.0.1',    'photoserver', upolicy) )

    def test_policy1_test_zeke_bad_app(self):
        upolicy = {}
        self.assertFalse(
            PolicyFile.policy.policy_lookup('192.168.100.5:33333', 'zeke', '192.168.100.5','galleria', upolicy) )

    def test_policy1_test_users_same_permissions(self):
        zpolicy = {}
        self.assertTrue(
            PolicyFile.policy.policy_lookup('192.168.100.5:33333', 'zeke', '192.168.100.5', 'photoserver', zpolicy) )
        ypolicy = {}
        self.assertTrue(
            PolicyFile.policy.policy_lookup('192.168.100.5:33334', 'ynot', '10.48.255.254', 'photoserver', ypolicy) )
        self.assertTrue( self.dict_compare(zpolicy, ypolicy) )

    def test_policy1_superuser_aggregation(self):
        upolicy = {}
        self.assertTrue( 
            PolicyFile.policy.policy_lookup('192.168.100.5:33335', 'ellen', '72.135.2.9', 'photoserver', upolicy) )
        self.assertTrue(upolicy['policyVersion']             == '1')
        self.assertTrue(upolicy['max_frame_size']            == 666666)
        self.assertTrue(upolicy['max_message_size']          == 666666)
        self.assertTrue(upolicy['max_session_window']        == 666666)
        self.assertTrue(upolicy['max_sessions']              == 6)
        self.assertTrue(upolicy['max_senders']               == 66)
        self.assertTrue(upolicy['max_receivers']             == 66)
        self.assertTrue(upolicy['allow_anonymous_sender'])
        self.assertTrue(upolicy['allow_dynamic_src'])
        addrs = ['public', 'private','management', 'root']
        self.assertTrue(len(upolicy['targets']) == 4)
        self.assertTrue(len(upolicy['sources']) == 4)
        for s in addrs: self.assertTrue(s in upolicy['targets'])
        for s in addrs: self.assertTrue(s in upolicy['sources'])

class PolicyConnStatsPerAppTests(TestCase):

    def test_policy_app_conn_stats_fail_by_total(self):
        stats = PolicyConnStatsPerApp(1, 2, 2)
        diags = []
        self.assertTrue(stats.can_connect('10.10.10.10:10000', 'chuck', '10.10.10.10', diags))
        self.assertFalse(stats.can_connect('10.10.10.10:10001', 'chuck', '10.10.10.10', diags))
        self.assertTrue(len(diags) == 1)
        self.assertTrue('by total' in diags[0])

    def test_policy_app_conn_stats_fail_by_user(self):
        stats = PolicyConnStatsPerApp(3, 1, 2)
        diags = []
        self.assertTrue(stats.can_connect('10.10.10.10:10000', 'chuck', '10.10.10.10', diags))
        self.assertFalse(stats.can_connect('10.10.10.10:10001', 'chuck', '10.10.10.10', diags))
        self.assertTrue(len(diags) == 1)
        self.assertTrue('per user' in diags[0])

    def test_policy_app_conn_stats_fail_by_hosts(self):
        stats = PolicyConnStatsPerApp(3, 2, 1)
        diags = []
        self.assertTrue(stats.can_connect('10.10.10.10:10000', 'chuck', '10.10.10.10', diags))
        self.assertFalse(stats.can_connect('10.10.10.10:10001', 'chuck', '10.10.10.10', diags))
        self.assertTrue(len(diags) == 1)
        self.assertTrue('per host' in diags[0])

    def test_policy_app_conn_stats_fail_by_user_hosts(self):
        stats = PolicyConnStatsPerApp(3, 1, 1)
        diags = []
        self.assertTrue(stats.can_connect('10.10.10.10:10000', 'chuck', '10.10.10.10', diags))
        self.assertFalse(stats.can_connect('10.10.10.10:10001', 'chuck', '10.10.10.10', diags))
        self.assertTrue(len(diags) == 2)
        self.assertTrue('per user' in diags[0] or 'per user' in diags[1])
        self.assertTrue('per host' in diags[0] or 'per host' in diags[1])

    def test_policy_app_conn_stats_update(self):
        stats = PolicyConnStatsPerApp(3, 1, 2)
        diags = []
        self.assertTrue(stats.can_connect('10.10.10.10:10000', 'chuck', '10.10.10.10', diags))
        self.assertFalse(stats.can_connect('10.10.10.10:10001', 'chuck', '10.10.10.10', diags))
        self.assertTrue(len(diags) == 1)
        self.assertTrue('per user' in diags[0])
        diags = []
        stats.update(3, 2, 2)
        self.assertTrue(stats.can_connect('10.10.10.10:10001', 'chuck', '10.10.10.10', diags))

    def test_policy_app_conn_stats_create_bad_settings(self):
        denied = False
        try:
            stats = PolicyConnStatsPerApp(-3, 1, 2)
        except PolicyError:
            denied = True
        self.assertTrue(denied, "Failed to detect negative setting value.")

    def test_policy_app_conn_stats_update_bad_settings(self):
        denied = False
        try:
            stats = PolicyConnStatsPerApp(0, 0, 0)
        except PolicyError:
            denied = True
        self.assertFalse(denied, "Should allow all zeros.")
        try:
            stats.update(0, -1, 0)
        except PolicyError:
            denied = True
        self.assertTrue(denied, "Failed to detect negative setting value.")

if __name__ == '__main__':
    unittest.main(main_module())

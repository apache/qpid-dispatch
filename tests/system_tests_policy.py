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

import unittest, json
import os
from system_test import TestCase, Qdrouterd, main_module, Process, TIMEOUT, DIR
from subprocess import PIPE, STDOUT
from proton import ConnectionException
from proton.utils import BlockingConnection, LinkDetached

class AbsoluteConnectionCountLimit(TestCase):
    """
    Verify that connections beyond the absolute limit are denied
    """
    @classmethod
    def setUpClass(cls):
        """Start the router"""
        super(AbsoluteConnectionCountLimit, cls).setUpClass()
        config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'QDR.Policy'}),
            ('listener', {'port': cls.tester.get_port()}),
            ('policy', {'maximumConnections': 2})
        ])

        cls.router = cls.tester.qdrouterd('conn-limit-router', config, wait=True)

    def address(self):
        return self.router.addresses[0]

    def test_verify_maximum_connections(self):
        addr = self.address()

        # two connections should be ok
        denied = False
        try:
            bc1 = BlockingConnection(addr)
            bc2 = BlockingConnection(addr)
        except ConnectionException:
            denied = True

        self.assertFalse(denied) # assert if connections that should open did not open

        # third connection should be denied
        denied = False
        try:
            bc3 = BlockingConnection(addr)
        except ConnectionException:
            denied = True

        self.assertTrue(denied) # assert if connection that should not open did open

        bc1.close()
        bc2.close()

class LoadPolicyFromFolder(TestCase):
    """
    Verify that specifying a policy folder from the router conf file
    effects loading the policies in that folder.
    This test relies on qdmanage utility.
    """
    @classmethod
    def setUpClass(cls):
        """Start the router"""
        super(LoadPolicyFromFolder, cls).setUpClass()
        policy_config_path = os.path.join(DIR, 'policy-1')
        config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'QDR.Policy'}),
            ('listener', {'port': cls.tester.get_port()}),
            ('policy', {'maximumConnections': 2, 'policyFolder': policy_config_path, 'enableAccessRules': 'true'})
        ])

        cls.router = cls.tester.qdrouterd('conn-limit-router', config, wait=True)

    def address(self):
        return self.router.addresses[0]

    def run_qdmanage(self, cmd, input=None, expect=Process.EXIT_OK):
        p = self.popen(
            ['qdmanage'] + cmd.split(' ') + ['--bus', 'u1:password@' + self.address(), '--indent=-1', '--timeout', str(TIMEOUT)],
            stdin=PIPE, stdout=PIPE, stderr=STDOUT, expect=expect)
        out = p.communicate(input)[0]
        try:
            p.teardown()
        except Exception, e:
            raise Exception("%s\n%s" % (e, out))
        return out

    def test_verify_policies_are_loaded(self):
        addr = self.address()

        rulesets = json.loads(self.run_qdmanage('query --type=policyRuleset'))
        self.assertEqual(len(rulesets), 5)

class SenderReceiverLimits(TestCase):
    """
    Verify that specifying a policy folder from the router conf file
    effects loading the policies in that folder.
    This test relies on qdmanage utility.
    """
    @classmethod
    def setUpClass(cls):
        """Start the router"""
        super(SenderReceiverLimits, cls).setUpClass()
        policy_config_path = os.path.join(DIR, 'policy-3')
        config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'QDR.Policy'}),
            ('listener', {'port': cls.tester.get_port()}),
            ('policy', {'maximumConnections': 2, 'policyFolder': policy_config_path, 'enableAccessRules': 'true'})
        ])

        cls.router = cls.tester.qdrouterd('SenderReceiverLimits', config, wait=True)

    def address(self):
        return self.router.addresses[0]

    def test_verify_n_receivers(self):
        n = 4
        addr = self.address()

        # connection should be ok
        denied = False
        try:
            br1 = BlockingConnection(addr)
        except ConnectionException:
            denied = True

        self.assertFalse(denied) # assert if connections that should open did not open

        # n receivers OK
        try:
            r1 = br1.create_receiver(address="****YES_1of4***")
            r2 = br1.create_receiver(address="****YES_20f4****")
            r3 = br1.create_receiver(address="****YES_3of4****")
            r4 = br1.create_receiver(address="****YES_4of4****")
        except Exception:
            denied = True

        self.assertFalse(denied) # n receivers should have worked

        # receiver n+1 should be denied
        try:
            r5 = br1.create_receiver("****NO****")
        except Exception:
            denied = True

        self.assertTrue(denied) # receiver n+1 should have failed

        br1.close()

    def test_verify_n_senders(self):
        n = 2
        addr = self.address()

        # connection should be ok
        denied = False
        try:
            bs1 = BlockingConnection(addr)
        except ConnectionException:
            denied = True

        self.assertFalse(denied) # assert if connections that should open did not open

        # n senders OK
        try:
            s1 = bs1.create_sender(address="****YES_1of2****")
            s2 = bs1.create_sender(address="****YES_2of2****")
        except Exception:
            denied = True

        self.assertFalse(denied) # n senders should have worked

        # receiver n+1 should be denied
        try:
            s3 = bs1.create_sender("****NO****")
        except Exception:
            denied = True

        self.assertTrue(denied) # sender n+1 should have failed

        bs1.close()

if __name__ == '__main__':
    unittest.main(main_module())

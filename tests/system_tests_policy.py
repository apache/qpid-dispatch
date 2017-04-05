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
from qpid_dispatch_internal.policy.policy_util import is_ipv6_enabled

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
            ('policy', {'maxConnections': 2, 'enableVhostPolicy': 'false'})
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

        ipv6_enabled = is_ipv6_enabled()

        policy_config_path = os.path.join(DIR, 'policy-1')
        replacements = {'{IPV6_LOOPBACK}':', ::1'}
        for f in os.listdir(policy_config_path):
            if f.endswith(".json.in"):
                with open(policy_config_path+"/"+f[:-3], 'w') as outfile:
                    with open(policy_config_path + "/" + f) as infile:
                        for line in infile:
                            for src, target in replacements.iteritems():
                                if ipv6_enabled:
                                    line = line.replace(src, target)
                                else:
                                    line = line.replace(src, '')
                            outfile.write(line)

        config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'QDR.Policy'}),
            ('listener', {'port': cls.tester.get_port()}),
            ('policy', {'maxConnections': 2, 'policyDir': policy_config_path, 'enableVhostPolicy': 'true'})
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

        rulesets = json.loads(self.run_qdmanage('query --type=vhost'))
        self.assertEqual(len(rulesets), 5)

    def new_policy(self):
        return """
{
    "id": "dispatch-494",
    "maxConnections": 50,
    "maxConnectionsPerHost": 20,
    "maxConnectionsPerUser": 8,
    "allowUnknownUser": true,
    "groups": {
        "$default": {
            "allowAnonymousSender": true,
            "maxReceivers": 99,
            "users": "*",
            "maxSessionWindow": 9999,
            "maxFrameSize": 222222,
            "sources": "public, private, $management",
            "maxMessageSize": 222222,
            "allowDynamicSource": true,
            "remoteHosts": "*",
            "maxSessions": 2,
            "targets": "public, private, $management",
            "maxSenders": 22
        }
    }
}
"""

    def updated_policy(self):
        return """
{
    "id": "dispatch-494",
    "maxConnections": 500,
    "maxConnectionsPerHost": 2,
    "maxConnectionsPerUser": 30,
    "allowUnknownUser": true,
    "groups": {
        "$default": {
            "allowAnonymousSender": true,
            "maxReceivers": 123,
            "users": "*",
            "maxSessionWindow": 9999,
            "maxFrameSize": 222222,
            "sources": "public, private, $management",
            "maxMessageSize": 222222,
            "allowDynamicSource": true,
            "remoteHosts": "*",
            "maxSessions": 2,
            "targets": "public, private, $management",
            "maxSenders": 222
        }
    }
}
"""

    def test_verify_policy_add_update_delete(self):
        # verify current vhost count
        rulesets = json.loads(self.run_qdmanage('query --type=vhost'))
        self.assertEqual(len(rulesets), 5)

        # create
        self.run_qdmanage('create --type=vhost --name=dispatch-494 --stdin', input=self.new_policy())
        rulesets = json.loads(self.run_qdmanage('query --type=vhost'))
        self.assertEqual(len(rulesets), 6)
        found = False
        for ruleset in rulesets:
            if ruleset['id'] == 'dispatch-494':
                found = True
                self.assertEqual(ruleset['maxConnections'], 50)
                self.assertEqual(ruleset['maxConnectionsPerHost'], 20)
                self.assertEqual(ruleset['maxConnectionsPerUser'], 8)
                break
        self.assertTrue(found)

        # update
        self.run_qdmanage('update --type=vhost --name=dispatch-494 --stdin', input=self.updated_policy())
        rulesets = json.loads(self.run_qdmanage('query --type=vhost'))
        self.assertEqual(len(rulesets), 6)
        found = False
        for ruleset in rulesets:
            if ruleset['id'] == 'dispatch-494':
                found = True
                self.assertEqual(ruleset['maxConnections'], 500)
                self.assertEqual(ruleset['maxConnectionsPerHost'], 2)
                self.assertEqual(ruleset['maxConnectionsPerUser'], 30)
                break
        self.assertTrue(found)

        # delete
        self.run_qdmanage('delete --type=vhost --name=dispatch-494')
        rulesets = json.loads(self.run_qdmanage('query --type=vhost'))
        self.assertEqual(len(rulesets), 5)
        absent = True
        for ruleset in rulesets:
            if ruleset['id'] == 'dispatch-494':
                absent = False
                break
        self.assertTrue(absent)

    def test_repeated_create_delete(self):
        for i in range(0, 10):
            rulesets = json.loads(self.run_qdmanage('query --type=vhost'))
            self.assertEqual(len(rulesets), 5)

            # create
            self.run_qdmanage('create --type=vhost --name=dispatch-494 --stdin', input=self.new_policy())
            rulesets = json.loads(self.run_qdmanage('query --type=vhost'))
            self.assertEqual(len(rulesets), 6)
            found = False
            for ruleset in rulesets:
                if ruleset['id'] == 'dispatch-494':
                    found = True
                    break
            self.assertTrue(found)

            # delete
            self.run_qdmanage('delete --type=vhost --name=dispatch-494')
            rulesets = json.loads(self.run_qdmanage('query --type=vhost'))
            self.assertEqual(len(rulesets), 5)
            absent = True
            for ruleset in rulesets:
                if ruleset['id'] == 'dispatch-494':
                    absent = False
                    break
            self.assertTrue(absent)


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
            ('policy', {'maxConnections': 2, 'policyDir': policy_config_path, 'enableVhostPolicy': 'true'})
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

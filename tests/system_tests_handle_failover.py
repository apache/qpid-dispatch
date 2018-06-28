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

from __future__ import unicode_literals
from __future__ import division
from __future__ import absolute_import
from __future__ import print_function

from threading import Timer
import unittest2 as unittest
import json, re
from system_test import main_module, TIMEOUT
from system_test import TestCase, Qdrouterd, Process, TIMEOUT
from subprocess import PIPE, STDOUT


class FailoverTest(TestCase):
    inter_router_port = None

    @classmethod
    def router(cls, name, config):
        config = Qdrouterd.Config(config)

        cls.routers.append(cls.tester.qdrouterd(name, config, wait=True))

    @classmethod
    def setUpClass(cls):
        super(FailoverTest, cls).setUpClass()

        cls.routers = []

        cls.inter_router_port = cls.tester.get_port()
        cls.inter_router_port_1 = cls.tester.get_port()
        cls.backup_port = cls.tester.get_port()
        cls.backup_url = 'amqp://0.0.0.0:' + str(cls.backup_port)

        cls.failover_list = 'amqp://third-host:5671, ' + cls.backup_url

        #
        # Router A tries to connect to Router B via its connectorToB. Router B responds with an open frame which will
        # have the failover-server-list as one of its connection properties like the following -
        # [0x13024d0]:0 <- @open(16) [container-id="Router.A", max-frame-size=16384, channel-max=32767,
        # idle-time-out=8000, offered-capabilities=:"ANONYMOUS-RELAY",
        # properties={:product="qpid-dispatch-router", :version="1.0.0",
        #  :"failover-server-list"=[{:"network-host"="some-host", :port="35000"},
        #  {:"network-host"="0.0.0.0", :port="25000"}]}]
        #
        # The suite of tests determine if the router receiving this open frame stores it properly and if the
        # original connection goes down, check that the router is trying to make connections to the failover urls.
        #
        FailoverTest.router('B', [
                        ('router', {'mode': 'interior', 'id': 'B'}),
                        ('listener', {'host': '0.0.0.0', 'role': 'inter-router', 'port': cls.inter_router_port,
                                      'failoverUrls': cls.failover_list}),
                        ('listener', {'host': '0.0.0.0', 'role': 'normal', 'port': cls.tester.get_port()}),
                        ]
              )

        FailoverTest.router('A',
                    [
                        ('router', {'mode': 'interior', 'id': 'A'}),
                        ('listener', {'host': '0.0.0.0', 'role': 'normal', 'port': cls.tester.get_port()}),
                        ('connector', {'name': 'connectorToB', 'role': 'inter-router',
                                       'port': cls.inter_router_port, 'verifyHostname': 'no'}),
                    ]
               )

        FailoverTest.router('C', [
                            ('router', {'mode': 'interior', 'id': 'C'}),
                            ('listener', {'host': '0.0.0.0', 'role': 'inter-router', 'port': cls.backup_port}),
                            ('listener', {'host': '0.0.0.0', 'role': 'normal', 'port': cls.tester.get_port()}),
                        ]
              )

        cls.routers[1].wait_router_connected('B')

    def __init__(self, test_method):
        TestCase.__init__(self, test_method)
        self.success = False
        self.timer_delay = 2
        self.max_attempts = 5
        self.attempts = 0

    def address(self):
        return self.routers[1].addresses[0]

    def run_qdmanage(self, cmd, input=None, expect=Process.EXIT_OK, address=None):
        p = self.popen(
            ['qdmanage'] + cmd.split(' ') + ['--bus', address or self.address(), '--indent=-1', '--timeout', str(TIMEOUT)],
            stdin=PIPE, stdout=PIPE, stderr=STDOUT, expect=expect,
            universal_newlines=True)
        out = p.communicate(input)[0]
        try:
            p.teardown()
        except Exception as e:
            raise Exception("%s\n%s" % (e, out))
        return out

    def run_qdstat(self, args, regexp=None, address=None):
        p = self.popen(
            ['qdstat', '--bus', str(address or self.router.addresses[0]), '--timeout', str(TIMEOUT) ] + args,
            name='qdstat-'+self.id(), stdout=PIPE, expect=None,
            universal_newlines=True)

        out = p.communicate()[0]
        assert p.returncode == 0, \
            "qdstat exit status %s, output:\n%s" % (p.returncode, out)
        if regexp: assert re.search(regexp, out, re.I), "Can't find '%s' in '%s'" % (regexp, out)
        return out

    def test_1_connector_has_failover_list(self):
        """
        Makes a qdmanage connector query and checks if Router A is storing the failover information received from
        Router B.
        :return:
        """
        long_type = 'org.apache.qpid.dispatch.connector'
        query_command = 'QUERY --type=' + long_type
        output = json.loads(self.run_qdmanage(query_command))
        self.assertEqual("amqp://127.0.0.1:" + str(FailoverTest.inter_router_port) + ", " + FailoverTest.failover_list,
                         output[0]['failoverUrls'])

    def schedule_B_to_C_failover_test(self):
        if self.attempts < self.max_attempts:
            if not self.success:
                Timer(self.timer_delay, self.check_C_connector).start()
                self.attempts += 1

    def check_C_connector(self):
        # Router A should now try to connect to Router C. Router C does NOT have failoverUrls.
        # Query Router A which previously had failoverUrls in its connector (because Router B sent it failoverUrls)
        # does not have it anymore.
        long_type = 'org.apache.qpid.dispatch.connector'
        query_command = 'QUERY --type=' + long_type
        output = json.loads(self.run_qdmanage(query_command, address=self.routers[1].addresses[0]))

        expected = FailoverTest.backup_url  + ", " + "amqp://127.0.0.1:" + str(FailoverTest.inter_router_port)

        if output[0].get('failoverUrls') == expected:
            self.success = True
        else:
            self.schedule_B_to_C_failover_test()

    def can_terminate(self):
        if self.attempts == self.max_attempts:
            return True

        if self.success:
            return True

        return False

    def test_2_remove_router_B(self):
        # First make sure there are no inter-router connections on router C
        outs = self.run_qdstat(['--connections'], address=self.routers[2].addresses[1])

        inter_router = 'inter-router' in outs
        self.assertFalse(inter_router)

        # Kill the router B
        FailoverTest.routers[0].teardown()

        # Schedule a test to make sure that the failover url is available
        self.schedule_B_to_C_failover_test()

        while not self.can_terminate():
            pass

        self.assertTrue(self.success)

        # Since router B has been killed, router A should now try to connect to a listener on router C.
        # Use qdstat to connect to router C and determine that there is an inter-router connection with router A.
        self.run_qdstat(['--connections'], regexp=r'A.*inter-router.*', address=self.routers[2].addresses[1])

    def schedule_C_to_B_failover_test(self):
        if self.attempts < self.max_attempts:
            if not self.success:
                Timer(self.timer_delay, self.check_B_connector).start()
                self.attempts += 1

    def check_B_connector(self):
        # Router A should now try to connect to Router B again since we killed Router C.
        long_type = 'org.apache.qpid.dispatch.connector'
        query_command = 'QUERY --type=' + long_type
        output = json.loads(self.run_qdmanage(query_command, address=self.routers[1].addresses[0]))

        expected = "amqp://127.0.0.1:" + str(FailoverTest.inter_router_port) + ", " + FailoverTest.failover_list

        if output[0].get('failoverUrls') == expected:
            self.success = True
        else:
            self.schedule_C_to_B_failover_test()

    def test_3_reinstate_router_B(self):
        FailoverTest.router('B', [
                        ('router', {'mode': 'interior', 'id': 'B'}),
                        ('listener', {'host': '0.0.0.0', 'role': 'inter-router', 'port': FailoverTest.inter_router_port,
                                      'failoverUrls': FailoverTest.failover_list}),
                        ('listener', {'host': '0.0.0.0', 'role': 'normal', 'port': FailoverTest.tester.get_port()}),
                        ]
              )

        FailoverTest.routers[3].wait_ready()

        # Kill the router C.
        # Now since Router B is up and running, router A should try to re-connect to Router B.
        # This will prove that the router A is preserving the original connector information specified in its config.
        FailoverTest.routers[2].teardown()

        self.success = False
        self.attempts = 0

        # Schedule a test to make sure that the failover url is available
        self.schedule_C_to_B_failover_test()

        while not self.can_terminate():
            pass

        self.assertTrue(self.success)


if __name__ == '__main__':
    unittest.main(main_module())
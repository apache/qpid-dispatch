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
import json
from subprocess import PIPE, STDOUT
from threading import Timer

from system_test import TestCase, Process, Qdrouterd, TIMEOUT


class ConnectorStatusTest(TestCase):

    inter_router_port = None
    mgmt_port_a = None
    mgmt_port_b = None

    @classmethod
    def setUpClass(cls):
        super(ConnectorStatusTest, cls).setUpClass()

        def router(name, config):
            config = Qdrouterd.Config(config)
            cls.routers.append(cls.tester.qdrouterd(name, config))

        cls.routers = []
        inter_router_port = cls.tester.get_port()
        mgmt_port_a = cls.tester.get_port()
        mgmt_port_b = cls.tester.get_port()

        config_a = [
            ('router', {'mode': 'interior', 'id': 'QDR.A'}),
            ('listener', {'port': mgmt_port_a, 'host': '0.0.0.0'}),
            ('listener', {'role': 'inter-router', 'port': inter_router_port, 'host': '0.0.0.0'}),
        ]

        router('QDR.A', config_a)

        config_b = [
            ('router', {'mode': 'interior', 'id': 'QDR.B'}),
            ('listener', {'port': mgmt_port_b, 'host': '0.0.0.0'}),
            ('connector', {'name': 'connectorToA', 'role': 'inter-router',
                           'port': inter_router_port}),
        ]

        router('QDR.B', config_b)

        cls.routers[0].wait_ports()
        cls.routers[1].wait_ports()

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

    def can_terminate(self):
        if self.attempts == self.max_attempts:
            return True

        if self.success:
            return True

        return False

    def schedule_B_connector_test(self):
        if self.attempts < self.max_attempts:
            if not self.success:
                Timer(self.timer_delay, self.check_B_connector).start()
                self.attempts += 1

    def check_B_connector(self):
        # Router A should now try to connect to Router B again since we killed Router C.
        long_type = 'org.apache.qpid.dispatch.connector'
        query_command = 'QUERY --type=' + long_type
        output = json.loads(self.run_qdmanage(query_command, address=self.address()))

        conn_status = output[0].get('connectionStatus')
        conn_msg = output[0].get('connectionMsg')

        if conn_status == 'CONNECTING' and "Connection" in conn_msg and "failed" in conn_msg:
            self.success = True
        else:
            self.schedule_B_connector_test()

    def test_conn_status_before_connect(self):
        # The routers have connected and begun talking to each other
        # Verify that the connectionStatus field of the connector is set to SUCCESS.
        # Also make sure that the connectionMsg field of the connector has "Connection opened" in it.
        long_type = 'org.apache.qpid.dispatch.connector'
        query_command = 'QUERY --type=' + long_type
        output = json.loads(self.run_qdmanage(query_command))
        connection_msg = output[0]['connectionMsg']
        self.assertEqual('SUCCESS', output[0]['connectionStatus'])
        conn_opened = False
        if "Connection Opened: dir=out" in connection_msg:
            conn_opened = True
        self.assertEqual(True, conn_opened)

        # Now tear down Router QDR.A. On doing this, router QDR.B will lose connectivity to
        # QDR.A.
        # QDR.B will continually attempy to connect to QDR.A but will be unsuccessful.
        # The status of the connector of B must now be 'CONNECTING'.
        ConnectorStatusTest.routers[0].teardown()

        self.schedule_B_connector_test()

        while not self.can_terminate():
            pass

        self.assertTrue(self.success)

        # NOTE: Since the router continually tries the re-establish a connection
        # if it is lost, the router connection status goes between FAILED and
        # SUCCESS. There is no good way to test if the connection status ever
        # reaches the FAILED state because the router immediately tries to
        # re-connect thus setting the status to CONNECTING in the process.

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

import time

from proton import Message
from proton.utils import BlockingConnection
from proton.reactor import AtMostOnce
from proton.reactor import Container

from system_test import TestCase, Qdrouterd
from system_test import QdManager

apply_options = AtMostOnce()


class ManyLogFilesTest(TestCase):

    @classmethod
    def setUpClass(cls):
        super(ManyLogFilesTest, cls).setUpClass()
        name = "test-router"
        LogLevelUpdateTest.listen_port = cls.tester.get_port()
        config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'QDR'}),
            ('listener', {'port': LogLevelUpdateTest.listen_port}),

            ('address', {'prefix': 'closest', 'distribution': 'closest'}),
            ('address', {'prefix': 'balanced', 'distribution': 'balanced'}),
            ('address', {'prefix': 'multicast', 'distribution': 'multicast'}),

            # We are sending three different module trace logs to three different
            # files and we will make sure that these files exist and these
            # files contain only logs pertinent to the module in question
            ('log', {'module': 'SERVER', 'enable': 'trace+',
                     'includeSource': 'true', 'outputFile': name + '-server.log'}),
            ('log', {'module': 'ROUTER_CORE', 'enable': 'trace+',
                     'includeSource': 'true',
                     'outputFile': name + '-core.log'}),
            ('log', {'module': 'PROTOCOL', 'enable': 'trace+',
                     'includeSource': 'true',
                     'outputFile': name + '-protocol.log'}),

            # try two modules to the same file.
            # Put the ROUTER_CORE and ROUTER module logs into the same log file
            ('log', {'module': 'ROUTER', 'enable': 'trace+',
                     'includeSource': 'true',
                     'outputFile': name + '-core.log'}),

        ])
        cls.router = cls.tester.qdrouterd(name, config)
        cls.router.wait_ready()
        cls.address = cls.router.addresses[0]

    def test_multiple_log_file(self):
        blocking_connection = BlockingConnection(self.address)

        TEST_ADDRESS = "test_multiple_log_file"

        blocking_receiver = blocking_connection.create_receiver(address=TEST_ADDRESS)
        blocking_sender = blocking_connection.create_sender(address=TEST_ADDRESS, options=apply_options)

        TEST_MSG = "LOGTEST"
        msg = Message(body=TEST_MSG)
        blocking_sender.send(msg)
        received_message = blocking_receiver.receive()
        self.assertEqual(TEST_MSG, received_message.body)
        server_log_found = True
        all_server_logs = True
        try:
            with open(self.router.outdir + '/test-router-server.log', 'r') as server_log:
                for line in server_log:
                    parts = line.split(" ")
                    if parts[3] != "SERVER":
                        all_server_logs = False
                        break
        except:
            server_log_found = False

        self.assertTrue(all_server_logs)
        self.assertTrue(server_log_found)

        protocol_log_found = True
        all_protocol_logs = True
        try:
            with open(self.router.outdir + '/test-router-protocol.log', 'r') as protocol_log:
                for line in protocol_log:
                    parts = line.split(" ")
                    if parts[3] != "PROTOCOL":
                        all_protocol_logs = False
                        break
        except:
            protocol_log_found = False

        self.assertTrue(protocol_log_found)
        self.assertTrue(all_protocol_logs)

        core_router_log_found = True
        all_core_router_logs = True
        try:
            with open(self.router.outdir + '/test-router-core.log', 'r') as core_log:
                for line in core_log:
                    parts = line.split(" ")
                    if parts[3] != "ROUTER_CORE" and parts[3] != "ROUTER":
                        all_core_router_logs = False
                        break

        except:
            core_router_log_found = False

        self.assertTrue(core_router_log_found)
        self.assertTrue(all_core_router_logs)


class LogModuleProtocolTest(TestCase):

    @classmethod
    def setUpClass(cls):
        super(LogModuleProtocolTest, cls).setUpClass()
        name = "test-router"
        LogModuleProtocolTest.listen_port = cls.tester.get_port()
        config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'QDR'}),
            ('listener', {'port': LogModuleProtocolTest.listen_port}),
            ('address', {'prefix': 'closest', 'distribution': 'closest'}),
            ('address', {'prefix': 'balanced', 'distribution': 'balanced'}),
            ('address', {'prefix': 'multicast', 'distribution': 'multicast'})
        ])
        cls.router = cls.tester.qdrouterd(name, config)
        cls.router.wait_ready()
        cls.address = cls.router.addresses[0]

    def create_sender_receiver(self, test_address, test_msg, blocking_connection=None):
        if not blocking_connection:
            blocking_connection = BlockingConnection(self.address)
        blocking_receiver = blocking_connection.create_receiver(address=test_address)
        blocking_sender = blocking_connection.create_sender(address=test_address, options=apply_options)
        msg = Message(body=test_msg)
        blocking_sender.send(msg)
        received_message = blocking_receiver.receive()
        self.assertEqual(test_msg, received_message.body)

    def test_turn_on_protocol_trace(self):
        hello_world_0 = "Hello World_0!"
        qd_manager = QdManager(self, self.address)
        blocking_connection = BlockingConnection(self.address)

        TEST_ADDR = "moduletest0"
        self.create_sender_receiver(TEST_ADDR, hello_world_0, blocking_connection)

        num_attaches = 0
        logs = qd_manager.get_log()
        for log in logs:
            if 'PROTOCOL' in log[0]:
                if "@attach" in log[2] and TEST_ADDR in log[2]:
                    num_attaches += 1

        # num_attaches for address TEST_ADDR must be 4, two attaches to/from sender and receiver
        self.assertTrue(num_attaches == 4)

        # Turn off trace logging using qdmanage
        qd_manager.update("org.apache.qpid.dispatch.log", {"enable": "info+"}, name="log/DEFAULT")

        # Turn on trace (not trace+) level logging for the PROTOCOL module. After doing
        # this we will create a sender and a receiver and make sure that the PROTOCOL module
        # is emitting proton frame trace messages.

        # Before DISPATCH-1558, the only way to turn on proton frame trace logging was to set
        # enable to trace on the SERVER or the DEFAULT module. Turning on trace for the SERVER
        # module would also spit out dispatch trace level messages from the SERVER module.
        # DISPATCH-1558 adds the new PROTOCOL module which moves all protocol traces into
        # that module.
        qd_manager.update("org.apache.qpid.dispatch.log", {"enable": "trace+"}, name="log/PROTOCOL")

        TEST_ADDR = "moduletest1"
        hello_world_1 = "Hello World_1!"
        self.create_sender_receiver(TEST_ADDR, hello_world_1, blocking_connection)

        num_attaches = 0
        logs = qd_manager.get_log()
        for log in logs:
            if 'PROTOCOL' in log[0]:
                if "@attach" in log[2] and TEST_ADDR in log[2]:
                    num_attaches += 1

        # num_attaches for address TEST_ADDR must be 4, two attaches to/from sender and receiver
        self.assertTrue(num_attaches == 4)

        # Now turn off trace logging for the PROTOCOL module and make sure
        # that there is no more proton frame trace messages appearing in the log
        qd_manager.update("org.apache.qpid.dispatch.log",
                          {"enable": "info+"}, name="log/PROTOCOL")

        TEST_ADDR = "moduletest2"
        hello_world_2 = "Hello World_2!"
        self.create_sender_receiver(TEST_ADDR, hello_world_2, blocking_connection)

        num_attaches = 0
        logs = qd_manager.get_log()
        for log in logs:
            if 'PROTOCOL' in log[0]:
                if "@attach" in log[2] and TEST_ADDR in log[2]:
                    num_attaches += 1

        # num_attaches for address TEST_ADDR must be 4, two attaches to/from sender and receiver
        self.assertTrue(num_attaches == 0)


class EnableConnectionLevelInterRouterTraceTest(TestCase):

    inter_router_port = None

    @classmethod
    def setUpClass(cls):
        super(EnableConnectionLevelInterRouterTraceTest, cls).setUpClass()

        def router(name, connection):

            config = [
                ('router', {'mode': 'interior', 'id': 'QDR.%s' % name}),
                ('listener', {'port': cls.tester.get_port()}),
                connection
            ]

            config = Qdrouterd.Config(config)

            cls.routers.append(cls.tester.qdrouterd(name, config, wait=True))

        cls.routers = []

        inter_router_port = cls.tester.get_port()

        router('A',
               ('listener', {'role': 'inter-router', 'port': inter_router_port}))

        router('B',
               ('connector', {'name': 'connectorToA', 'role': 'inter-router', 'port': inter_router_port}))

        cls.routers[0].wait_router_connected('QDR.B')
        cls.routers[1].wait_router_connected('QDR.A')

        cls.address = cls.routers[1].addresses[0]

    def _get_transfer_frame_count(self, conn_id):
        inter_router_cid = "[C" + conn_id + "]"
        num_transfers = 0
        with open(self.routers[1].logfile_path) as router_log:
            for log_line in router_log:
                log_components = log_line.split(" ")
                if len(log_components) > 8 and 'PROTOCOL' in log_components[3]:
                    if inter_router_cid in log_components[5] and '@transfer' in log_components[8]:
                        num_transfers += 1
        return num_transfers

    def test_inter_router_protocol_trace(self):
        qd_manager = QdManager(self, self.address)

        # The router already has trace logging turned on for all connections.
        # Get the connection id of the inter-router connection
        results = qd_manager.query("org.apache.qpid.dispatch.connection")
        conn_id = None
        for result in results:
            if result['role'] == 'inter-router':
                conn_id = result['identity']

        # Turn off trace logging for the inter-router connection. This update command is run async by the router
        # so we need to sleep a bit before the operation is actually completed.
        qd_manager.update("org.apache.qpid.dispatch.connection", {"enableProtocolTrace": "false"}, identity=conn_id)
        time.sleep(1)

        num_transfers = self._get_transfer_frame_count(conn_id)

        # Create a receiver. This will send an MAU update to the other router but we should not see any of that
        # in the log since the trace logging for the inter-router connection has been turned off.
        TEST_ADDR_1 = "EnableConnectionLevelProtocolTraceTest1"
        conn_2 = BlockingConnection(self.address)
        conn_2.create_receiver(address=TEST_ADDR_1)
        # Give some time for the MAU to go over the inter-router connection.
        time.sleep(2)
        num_transfers_after_update = self._get_transfer_frame_count(conn_id)

        # Since there will be no transfer frames printed in the log, there should be no more new transfers in the
        # log file.
        self.assertEqual(num_transfers_after_update, num_transfers)

        # Turn on trace logging for the inter-router connection
        qd_manager.update("org.apache.qpid.dispatch.connection", {"enableProtocolTrace": "yes"}, identity=conn_id)

        # Create a receiver and make sure the MAU update is NOT seen on the inter-router connection log
        TEST_ADDR_2 = "EnableConnectionLevelProtocolTraceTest2"
        conn_1 = BlockingConnection(self.address)
        conn_1.create_receiver(address=TEST_ADDR_2)

        # Give time for the MAU to be generated.
        time.sleep(2)

        num_transfers_after_update = self._get_transfer_frame_count(conn_id)

        # Since we have now turned on trace logging for the inter-router connection, we should see
        # additional transfer frames in the log and we check that here.
        self.assertGreater(num_transfers_after_update, num_transfers)
        conn_1.close()
        conn_2.close()


class EnableConnectionLevelProtocolTraceTest(TestCase):

    @classmethod
    def setUpClass(cls):
        super(EnableConnectionLevelProtocolTraceTest, cls).setUpClass()
        name = "test-router"
        LogLevelUpdateTest.listen_port = cls.tester.get_port()
        config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'QDR'}),
            ('listener', {'port': LogLevelUpdateTest.listen_port}),
        ])
        cls.router = cls.tester.qdrouterd(name, config)
        cls.router.wait_ready()
        cls.address = cls.router.addresses[0]

    def test_enable_protocol_trace_on_non_existent_connection(self):
        qd_manager = QdManager(self, self.address)

        bad_request = False

        try:
            # Turn on trace logging for connection with invalid or non-existent identity
            outs = qd_manager.update("org.apache.qpid.dispatch.connection", {"enableProtocolTrace": "true"}, identity='G10000')
        except Exception as e:
            if "BadRequestStatus" in str(e):
                bad_request = True

        self.assertTrue(bad_request)

    def test_single_connection_protocol_trace(self):
        qd_manager = QdManager(self, self.address)

        # Turn off trace logging on all connections.
        qd_manager.update("org.apache.qpid.dispatch.log", {"enable": "info+"},
                          name="log/DEFAULT")

        TEST_ADDR_1 = "EnableConnectionLevelProtocolTraceTest1"
        MSG_BODY = "EnableConnectionLevelProtocolTraceTestMessage1"
        CONTAINER_ID_1 = "CONTAINERID_1"
        container_1 = Container()
        container_1.container_id = CONTAINER_ID_1
        conn_1 = BlockingConnection(self.address, container=container_1)

        TEST_ADDR_2 = "EnableConnectionLevelProtocolTraceTest1"
        CONTAINER_ID_2 = "CONTAINERID_2"
        container_2 = Container()
        container_2.container_id = CONTAINER_ID_2
        conn_2 = BlockingConnection(self.address, container=container_2)

        results = qd_manager.query("org.apache.qpid.dispatch.connection")
        conn_id = None
        for result in results:
            if result['container'] == CONTAINER_ID_1:
                conn_id = result['identity']

        # Turn on trace logging for connection with identity conn_id
        qd_manager.update("org.apache.qpid.dispatch.connection", {"enableProtocolTrace": "true"}, identity=conn_id)

        blocking_receiver_1 = conn_1.create_receiver(address=TEST_ADDR_1)
        blocking_sender_1 = conn_1.create_sender(address=TEST_ADDR_1, options=apply_options)

        blocking_receiver_2 = conn_2.create_receiver(address=TEST_ADDR_2)
        blocking_sender_2 = conn_2.create_sender(address=TEST_ADDR_2, options=apply_options)

        num_attaches_1 = 0
        num_attaches_2 = 0
        logs = qd_manager.get_log()
        for log in logs:
            if 'PROTOCOL' in log[0]:
                if "@attach" in log[2] and TEST_ADDR_1 in log[2]:
                    num_attaches_1 += 1
                elif "@attach" in log[2] and TEST_ADDR_2 in log[2]:
                    num_attaches_2 += 1

        # num_attaches_1 for address TEST_ADDR_1 must be 4, two attaches to/from sender and receiver
        self.assertTrue(num_attaches_1 == 4)

        # num_attaches_2 for address TEST_ADDR_2 must be 0 since trace was not
        # turned on for that connection
        self.assertTrue(num_attaches_2 == 0)

        # Now turn off the connection tracing on that connection
        qd_manager.update("org.apache.qpid.dispatch.connection",
                          {"enableProtocolTrace": "off"},
                          identity=conn_id)
        blocking_receiver_1.close()
        blocking_sender_1.close()

        # Since tracing was turned off, there should be no detaches
        logs = qd_manager.get_log()
        num_detaches = 0
        for log in logs:
            if 'PROTOCOL' in log[0]:
                if "@detach" in log[2]:
                    num_detaches += 1
        self.assertTrue(num_detaches == 0)
        blocking_receiver_2.close()
        blocking_sender_2.close()
        conn_1.close()
        conn_2.close()


class LogLevelUpdateTest(TestCase):

    @classmethod
    def setUpClass(cls):
        super(LogLevelUpdateTest, cls).setUpClass()
        name = "test-router"
        LogLevelUpdateTest.listen_port = cls.tester.get_port()
        config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'QDR'}),
            ('listener', {'port': LogLevelUpdateTest.listen_port}),

            ('address', {'prefix': 'closest', 'distribution': 'closest'}),
            ('address', {'prefix': 'balanced', 'distribution': 'balanced'}),
            ('address', {'prefix': 'multicast', 'distribution': 'multicast'})
        ])
        cls.router = cls.tester.qdrouterd(name, config)
        cls.router.wait_ready()
        cls.address = cls.router.addresses[0]
        cls.closest_count = 1

    def create_sender_receiver(self, test_address, test_msg, blocking_connection=None):
        if not blocking_connection:
            blocking_connection = BlockingConnection(self.address)
        blocking_receiver = blocking_connection.create_receiver(address=test_address)
        blocking_sender = blocking_connection.create_sender(address=test_address, options=apply_options)
        msg = Message(body=test_msg)
        blocking_sender.send(msg)
        received_message = blocking_receiver.receive()
        self.assertEqual(test_msg, received_message.body)

    def test_01_toggle_default_trace_logging(self):
        hello_world_1 = "Hello World_1!"
        hello_world_2 = "Hello World_2!"
        hello_world_3 = "Hello World_3!"
        hello_world_4 = "Hello World_4!"
        qd_manager = QdManager(self, self.address)

        blocking_connection = BlockingConnection(self.address)
        TEST_ADDR = "apachetest1"
        self.create_sender_receiver(TEST_ADDR, hello_world_1, blocking_connection)

        # STEP 1: Make sure that proton trace logging is turned on already.
        # Search for attach frames in the log for address TEST_ADDR. There should be 4 attaches
        num_attaches = 0
        logs = qd_manager.get_log()
        for log in logs:
            if 'PROTOCOL' in log[0]:
                if "@attach" in log[2] and TEST_ADDR in log[2]:
                    num_attaches += 1
        # num_attaches for address TEST_ADDR must be 4, two attaches to/from sender and receiver
        self.assertTrue(num_attaches == 4)

        # STEP 2: Turn off trace logging using qdmanage
        qd_manager.update("org.apache.qpid.dispatch.log", {"enable": "info+"}, name="log/DEFAULT")

        # Step 3: Now, router trace logging is turned off (has been set to info+)
        # Create the sender and receiver again on a different address and make
        # sure that the attaches are NOT showing in the log for that address.

        TEST_ADDR = "apachetest2"
        self.create_sender_receiver(TEST_ADDR, hello_world_2, blocking_connection)

        # STEP 3: Count the nimber of attaches for address TEST_ADDR, there should be none
        num_attaches = 0
        logs = qd_manager.get_log()
        for log in logs:
            if 'PROTOCOL' in log[0]:
                if "@attach" in log[2] and TEST_ADDR in log[2]:
                    num_attaches += 1
        # There should be no attach frames with address TEST_ADDR
        # because we turned of trace logging.
        self.assertTrue(num_attaches == 0)

        # STEP 4: Tuen trace logging back on again and make sure num_attaches = 4
        TEST_ADDR = "apachetest3"
        qd_manager.update("org.apache.qpid.dispatch.log", {"enable": "trace+"}, name="log/DEFAULT")
        self.create_sender_receiver(TEST_ADDR, hello_world_3, blocking_connection)

        # STEP 3: Count the number of attaches for address TEST_ADDR, there should be 4
        num_attaches = 0
        logs = qd_manager.get_log()
        for log in logs:
            if 'PROTOCOL' in log[0]:
                if "@attach" in log[2] and TEST_ADDR in log[2]:
                    num_attaches += 1
        # There should be 4 attach frames with address TEST_ADDR
        # because we turned on trace logging.
        self.assertTrue(num_attaches == 4)

        # Create a brand new blocking connection  and make sure that connection
        # is logging at trace level as well.
        num_attaches = 0
        TEST_ADDR = "apachetest4"
        self.create_sender_receiver(TEST_ADDR, hello_world_4)
        num_attaches = 0
        logs = qd_manager.get_log()
        for log in logs:
            if 'PROTOCOL' in log[0]:
                if "@attach" in log[2] and TEST_ADDR in log[2]:
                    num_attaches += 1

        self.assertTrue(num_attaches == 4)

    def test_02_toggle_server_trace_logging(self):
        """
        This test is similar to test_01_toggle_default_trace_logging but it tests the
        SERVER log level.
        """
        hello_world_5 = "Hello World_5!"
        hello_world_6 = "Hello World_6!"
        hello_world_7 = "Hello World_7!"
        TEST_ADDR = "apachetest5"

        # Step 1. Turn off trace logging for module DEFAULT and enable trace logging
        #         for the PROTOCOL module and make sure it works.
        qd_manager = QdManager(self, self.address)
        # Set log level to info+ on the DEFAULT module
        qd_manager.update("org.apache.qpid.dispatch.log", {"enable": "info+"}, name="log/DEFAULT")
        # Set log level to trace+ on the PROTOCOL module
        qd_manager.update("org.apache.qpid.dispatch.log", {"enable": "trace+"}, name="log/PROTOCOL")
        blocking_connection = BlockingConnection(self.address)

        self.create_sender_receiver(TEST_ADDR, hello_world_5,
                                    blocking_connection)
        # Count the number of attaches for address TEST_ADDR, there should be 4
        num_attaches = 0
        logs = qd_manager.get_log()
        for log in logs:
            if 'PROTOCOL' in log[0]:
                if "@attach" in log[2] and TEST_ADDR in log[2]:
                    num_attaches += 1
        # There should be 4 attach frames with address TEST_ADDR
        # because we turned on trace logging.
        self.assertTrue(num_attaches == 4)

        TEST_ADDR = "apachetest6"
        qd_manager.update("org.apache.qpid.dispatch.log", {"enable": "info+"}, name="log/PROTOCOL")

        self.create_sender_receiver(TEST_ADDR, hello_world_6, blocking_connection)

        # Count the number of attaches for address TEST_ADDR, there should be 0
        num_attaches = 0
        logs = qd_manager.get_log()
        for log in logs:
            if 'PROTOCOL' in log[0]:
                if "@attach" in log[2] and TEST_ADDR in log[2]:
                    num_attaches += 1
        self.assertTrue(num_attaches == 0)

        # Create a brand new blocking connection  and make sure that connection
        # is logging at info level as well.
        TEST_ADDR = "apachetest7"
        self.create_sender_receiver(TEST_ADDR, hello_world_7)
        num_attaches = 0
        logs = qd_manager.get_log()
        for log in logs:
            if 'PROTOCOL' in log[0]:
                if "@attach" in log[2] and TEST_ADDR in log[2]:
                    num_attaches += 1

        self.assertTrue(num_attaches == 0)


class RouterCoreModuleLogTest(TestCase):

    @classmethod
    def setUpClass(cls):
        super(RouterCoreModuleLogTest, cls).setUpClass()
        name = "test-router"
        LogLevelUpdateTest.listen_port = cls.tester.get_port()
        config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'QDR'}),
            ('listener', {'port': LogLevelUpdateTest.listen_port}),

            ('address', {'prefix': 'closest', 'distribution': 'closest'}),
            ('address', {'prefix': 'balanced', 'distribution': 'balanced'}),
            ('address', {'prefix': 'multicast', 'distribution': 'multicast'}),
            ('log', {'module': 'ROUTER_CORE', 'enable': 'trace+',
                     'includeSource': 'true',
                     'outputFile': name + '-core.log'})

        ])
        cls.router = cls.tester.qdrouterd(name, config)
        cls.router.wait_ready()
        cls.address = cls.router.addresses[0]

    def test_router_core_logger(self):
        blocking_connection = BlockingConnection(self.address)

        TEST_ADDRESS = "test_multiple_log_file"

        blocking_receiver = blocking_connection.create_receiver(address=TEST_ADDRESS)
        blocking_sender = blocking_connection.create_sender(address=TEST_ADDRESS, options=apply_options)

        TEST_MSG_BODY = "LOGTEST"
        msg = Message(body=TEST_MSG_BODY)
        blocking_sender.send(msg)
        received_message = blocking_receiver.receive()
        self.assertEqual(TEST_MSG_BODY, received_message.body)
        qd_manager = QdManager(self, self.address)
        logs = qd_manager.get_log()

        router_core_found = False
        for log in logs:
            if 'ROUTER_CORE' in log[0]:
                router_core_found = True
                break

        self.assertTrue(router_core_found)

        core_log_file_found = True
        all_lines_router_core = True
        try:
            # Before the fix to DISPATCH-1575, this file will not be
            # created because the router core module was logging to the ROUTER
            # module instead of the ROUTER_CORE module.
            with open(self.router.outdir + '/test-router-core.log', 'r') as core_log:
                for line in core_log:
                    # Every line in the file must log to the router core module.
                    if "ROUTER_CORE" not in line:
                        all_lines_router_core = False
                        break
        except:
            core_log_file_found = False

        self.assertTrue(core_log_file_found)
        self.assertTrue(all_lines_router_core)

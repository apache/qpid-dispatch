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
import os

from system_test import TestCase, Qdrouterd, main_module, TIMEOUT, DIR, Process
from system_test import QdManager
from proton.utils import BlockingConnection
from proton import Message
from proton.reactor import AtMostOnce

apply_options = AtMostOnce()

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
        blocking_sender = blocking_connection.create_sender(address=test_address,  options=apply_options)
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
            if u'SERVER' in log[0]:
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
            if u'SERVER' in log[0]:
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
            if u'SERVER' in log[0]:
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
            if u'SERVER' in log[0]:
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
        #         for the SERVER module and make sure it works.
        qd_manager = QdManager(self, self.address)
        # Set log level to info+ on the DEFAULT module
        qd_manager.update("org.apache.qpid.dispatch.log", {"enable": "info+"}, name="log/DEFAULT")
        # Set log level to trace+ on the SERVER module
        qd_manager.update("org.apache.qpid.dispatch.log", {"enable": "trace+"}, name="log/SERVER")
        blocking_connection = BlockingConnection(self.address)

        self.create_sender_receiver(TEST_ADDR, hello_world_5,
                                    blocking_connection)
        # Count the nimber of attaches for address TEST_ADDR, there should be 4
        num_attaches = 0
        logs = qd_manager.get_log()
        for log in logs:
            if u'SERVER' in log[0]:
                if "@attach" in log[2] and TEST_ADDR in log[2]:
                    num_attaches += 1
        # There should be 4 attach frames with address TEST_ADDR
        # because we turned on trace logging.
        self.assertTrue(num_attaches == 4)

        TEST_ADDR = "apachetest6"
        qd_manager.update("org.apache.qpid.dispatch.log", {"enable": "info+"}, name="log/SERVER")

        self.create_sender_receiver(TEST_ADDR, hello_world_6, blocking_connection)

        # Count the nimber of attaches for address TEST_ADDR, there should be 0
        num_attaches = 0
        logs = qd_manager.get_log()
        for log in logs:
            if u'SERVER' in log[0]:
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
            if u'SERVER' in log[0]:
                if "@attach" in log[2] and TEST_ADDR in log[2]:
                    num_attaches += 1

        self.assertTrue(num_attaches == 0)







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

import unittest
from proton import Message, Delivery, PENDING, ACCEPTED, REJECTED
from system_test import TestCase, Qdrouterd, main_module
from proton.handlers import MessagingHandler
from proton.reactor import Container, AtMostOnce, AtLeastOnce
from proton.utils import BlockingConnection, SyncRequestResponse
from qpid_dispatch.management.client import Node
from proton import ConnectionException

class MaxFrameMaxSessionFramesTest(TestCase):
    """System tests setting proton negotiated size max-frame-size and incoming-window"""
    @classmethod
    def setUpClass(cls):
        '''Start a router'''
        super(MaxFrameMaxSessionFramesTest, cls).setUpClass()
        name = "MaxFrameMaxSessionFrames"
        config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'QDR'}),

            ('listener', {'host': '0.0.0.0', 'port': cls.tester.get_port(), 'maxFrameSize': '2048', 'maxSessionFrames': '10'}),
        ])
        cls.router = cls.tester.qdrouterd(name, config)
        cls.router.wait_ready()
        cls.address = cls.router.addresses[0]

    def test_max_frame_max_session_frames__max_sessions_default(self):
        # Set up a connection to get the Open and a receiver to get a Begin frame in the log
        bc = BlockingConnection(self.router.addresses[0])
        bc.create_receiver("xxx")
        bc.close()

        with  open('../setUpClass/MaxFrameMaxSessionFrames.log', 'r') as router_log:
            log_lines = router_log.read().split("\n")
            open_lines = [s for s in log_lines if "-> @open" in s]
            # max-frame is from the config
            self.assertTrue(' max-frame-size=2048,' in open_lines[0])
            # channel-max is default
            self.assertTrue(" channel-max=32767" in open_lines[0])
            begin_lines = [s for s in log_lines if "-> @begin" in s]
            # incoming-window is from the config
            self.assertTrue(" incoming-window=10," in begin_lines[0] )


class MaxSessionsTest(TestCase):
    """System tests setting proton channel-max"""
    @classmethod
    def setUpClass(cls):
        """Start a router and a messenger"""
        super(MaxSessionsTest, cls).setUpClass()
        name = "MaxSessions"
        config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'QDR'}),

            ('listener', {'host': '0.0.0.0', 'port': cls.tester.get_port(), 'maxSessions': '10'}),
        ])
        cls.router = cls.tester.qdrouterd(name, config)
        cls.router.wait_ready()
        cls.address = cls.router.addresses[0]

    def test_max_sessions(self):
        # Set up a connection to get the Open and a receiver to get a Begin frame in the log
        bc = BlockingConnection(self.router.addresses[0])
        bc.create_receiver("xxx")
        bc.close()

        with  open('../setUpClass/MaxSessions.log', 'r') as router_log:
            log_lines = router_log.read().split("\n")
            open_lines = [s for s in log_lines if "-> @open" in s]
            # channel-max is 10
            self.assertTrue(" channel-max=9" in open_lines[0])


class MaxSessionsZeroTest(TestCase):
    """System tests setting proton channel-max"""
    @classmethod
    def setUpClass(cls):
        """Start a router and a messenger"""
        super(MaxSessionsZeroTest, cls).setUpClass()
        name = "MaxSessionsZero"
        config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'QDR'}),

            ('listener', {'host': '0.0.0.0', 'port': cls.tester.get_port(), 'maxSessions': '0'}),
        ])
        cls.router = cls.tester.qdrouterd(name, config)
        cls.router.wait_ready()
        cls.address = cls.router.addresses[0]

    def test_max_sessions_zero(self):
        # Set up a connection to get the Open and a receiver to get a Begin frame in the log
        bc = BlockingConnection(self.router.addresses[0])
        bc.create_receiver("xxx")
        bc.close()

        with  open('../setUpClass/MaxSessionsZero.log', 'r') as router_log:
            log_lines = router_log.read().split("\n")
            open_lines = [s for s in log_lines if "-> @open" in s]
            # channel-max is 0. Should get proton default 32767
            self.assertTrue(" channel-max=32767" in open_lines[0])


class MaxSessionsLargeTest(TestCase):
    """System tests setting proton channel-max"""
    @classmethod
    def setUpClass(cls):
        """Start a router and a messenger"""
        super(MaxSessionsLargeTest, cls).setUpClass()
        name = "MaxSessionsLarge"
        config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'QDR'}),

            ('listener', {'host': '0.0.0.0', 'port': cls.tester.get_port(), 'maxSessions': '500000'}),
        ])
        cls.router = cls.tester.qdrouterd(name, config)
        cls.router.wait_ready()
        cls.address = cls.router.addresses[0]

    def test_max_sessions_large(self):
        # Set up a connection to get the Open and a receiver to get a Begin frame in the log
        bc = BlockingConnection(self.router.addresses[0])
        bc.create_receiver("xxx")
        bc.close()

        with  open('../setUpClass/MaxSessionsLarge.log', 'r') as router_log:
            log_lines = router_log.read().split("\n")
            open_lines = [s for s in log_lines if "-> @open" in s]
            # channel-max is 0. Should get proton default 32767
            self.assertTrue(" channel-max=32767" in open_lines[0])


class MaxFrameSmallTest(TestCase):
    """System tests setting proton max-frame-size"""
    @classmethod
    def setUpClass(cls):
        """Start a router and a messenger"""
        super(MaxFrameSmallTest, cls).setUpClass()
        name = "MaxFrameSmall"
        config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'QDR'}),

            ('listener', {'host': '0.0.0.0', 'port': cls.tester.get_port(), 'maxFrameSize': '2'}),
        ])
        cls.router = cls.tester.qdrouterd(name, config)
        cls.router.wait_ready()
        cls.address = cls.router.addresses[0]

    def test_max_frame_small(self):
        # Set up a connection to get the Open and a receiver to get a Begin frame in the log
        bc = BlockingConnection(self.router.addresses[0])
        bc.create_receiver("xxx")
        bc.close()

        with  open('../setUpClass/MaxFrameSmall.log', 'r') as router_log:
            log_lines = router_log.read().split("\n")
            open_lines = [s for s in log_lines if "-> @open" in s]
            # if frame size <= 512 proton set min of 512
            self.assertTrue(" max-frame-size=512" in open_lines[0])


class MaxFrameDefaultTest(TestCase):
    """System tests setting proton max-frame-size"""
    @classmethod
    def setUpClass(cls):
        """Start a router and a messenger"""
        super(MaxFrameDefaultTest, cls).setUpClass()
        name = "MaxFrameDefault"
        config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'QDR'}),

            ('listener', {'host': '0.0.0.0', 'port': cls.tester.get_port()}),
        ])
        cls.router = cls.tester.qdrouterd(name, config)
        cls.router.wait_ready()
        cls.address = cls.router.addresses[0]

    def test_max_frame_default(self):
        # Set up a connection to get the Open and a receiver to get a Begin frame in the log
        bc = BlockingConnection(self.router.addresses[0])
        bc.create_receiver("xxx")
        bc.close()

        with  open('../setUpClass/MaxFrameDefault.log', 'r') as router_log:
            log_lines = router_log.read().split("\n")
            open_lines = [s for s in log_lines if "-> @open" in s]
            # if frame size not set then a default is used
            self.assertTrue(" max-frame-size=16384" in open_lines[0])


class MaxSessionFramesDefaultTest(TestCase):
    """System tests setting proton max-frame-size"""
    @classmethod
    def setUpClass(cls):
        """Start a router and a messenger"""
        super(MaxSessionFramesDefaultTest, cls).setUpClass()
        name = "MaxSessionFramesDefault"
        config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'QDR'}),

            ('listener', {'host': '0.0.0.0', 'port': cls.tester.get_port()}),
        ])
        cls.router = cls.tester.qdrouterd(name, config)
        cls.router.wait_ready()
        cls.address = cls.router.addresses[0]

    def test_max_session_frames_default(self):
        # Set up a connection to get the Open and a receiver to get a Begin frame in the log
        bc = BlockingConnection(self.router.addresses[0])
        bc.create_receiver("xxx")
        bc.close()

        with  open('../setUpClass/MaxSessionFramesDefault.log', 'r') as router_log:
            log_lines = router_log.read().split("\n")
            open_lines = [s for s in log_lines if "-> @open" in s]
            # if frame size not set then a default is used
            self.assertTrue(" max-frame-size=16384" in open_lines[0])
            begin_lines = [s for s in log_lines if "-> @begin" in s]
            # incoming-window is from the config
            self.assertTrue(" incoming-window=100," in begin_lines[0])


class MaxFrameMaxSessionFramesTooBigTest(TestCase):
    """
    System tests setting proton negotiated size max-frame-size and incoming-window
    when the product of the two is > 2^31-1. There must be a warning and the incoming
    window will be reduced to 2^31-1 / max-frame-size
    """
    @classmethod
    def setUpClass(cls):
        '''Start a router'''
        super(MaxFrameMaxSessionFramesTooBigTest, cls).setUpClass()
        name = "MaxFrameMaxSessionFramesTooBig"
        config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'QDR'}),

            ('listener', {'host': '0.0.0.0', 'port': cls.tester.get_port(), 'maxFrameSize': '1000000', 'maxSessionFrames': '5000000'}),
        ])
        cls.router = cls.tester.qdrouterd(name, config)
        cls.router.wait_ready()
        cls.address = cls.router.addresses[0]

    def test_max_frame_max_session_too_big(self):
        # Set up a connection to get the Open and a receiver to get a Begin frame in the log
        bc = BlockingConnection(self.router.addresses[0])
        bc.create_receiver("xxx")
        bc.close()

        with  open('../setUpClass/MaxFrameMaxSessionFramesTooBig.log', 'r') as router_log:
            log_lines = router_log.read().split("\n")
            open_lines = [s for s in log_lines if "-> @open" in s]
            # max-frame is from the config
            self.assertTrue(' max-frame-size=1000000,' in open_lines[0])
            begin_lines = [s for s in log_lines if "-> @begin" in s]
            # incoming-window is truncated
            self.assertTrue(" incoming-window=2147," in begin_lines[0])
            warning_lines = [s for s in log_lines if "(warning)" in s]
            self.assertTrue(len(warning_lines) == 1)
            self.assertTrue("requested maxSessionFrames truncated from 5000000 to 2147" in warning_lines[0])


class MaxFrameMaxSessionFramesZeroTest(TestCase):
    """
    System tests setting proton negotiated size max-frame-size and incoming-window
    when they are both zero. Frame size is bumped up to the minimum and capacity is
    bumped up to have an incoming window of 1
    """
    @classmethod
    def setUpClass(cls):
        '''Start a router'''
        super(MaxFrameMaxSessionFramesZeroTest, cls).setUpClass()
        name = "MaxFrameMaxSessionFramesZero"
        config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'QDR'}),

            ('listener', {'host': '0.0.0.0', 'port': cls.tester.get_port(), 'maxFrameSize': '0', 'maxSessionFrames': '0'}),
        ])
        cls.router = cls.tester.qdrouterd(name, config)
        cls.router.wait_ready()
        cls.address = cls.router.addresses[0]

    def test_max_frame_max_session_zero(self):
        # Set up a connection to get the Open and a receiver to get a Begin frame in the log
        bc = BlockingConnection(self.router.addresses[0])
        bc.create_receiver("xxx")
        bc.close()

        with  open('../setUpClass/MaxFrameMaxSessionFramesZero.log', 'r') as router_log:
            log_lines = router_log.read().split("\n")
            open_lines = [s for s in log_lines if "-> @open" in s]
            # max-frame gets set to protocol min
            self.assertTrue(' max-frame-size=512,' in open_lines[0])
            begin_lines = [s for s in log_lines if "-> @begin" in s]
            # incoming-window is promoted to 1
            self.assertTrue(" incoming-window=1," in begin_lines[0])


if __name__ == '__main__':
    unittest.main(main_module())

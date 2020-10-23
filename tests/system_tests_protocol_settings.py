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

from system_test import TestCase, Qdrouterd, main_module
from system_test import unittest
from proton.utils import BlockingConnection
import subprocess
import sys

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
            # channel-max is 9
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
            # incoming-window should be 2^31-1 (64-bit) or
            # (2^31-1) / max-frame-size (32-bit)
            is_64bits = sys.maxsize > 2 ** 32
            expected = " incoming-window=2147483647," if is_64bits else \
                (" incoming-window=%d," % int(2147483647 / 16384))
            self.assertTrue(expected in begin_lines[0],
                            "Expected:'%s' not found in '%s'" % (expected, begin_lines[0]))


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
            # incoming-window should be 2^31-1 (64-bit) or
            # (2^31-1) / max-frame-size (32-bit)
            is_64bits = sys.maxsize > 2 ** 32
            expected = " incoming-window=2147483647," if is_64bits else \
                (" incoming-window=%d," % int(2147483647 / 512))
            self.assertTrue(expected in begin_lines[0],
                            "Expected:'%s' not found in '%s'" % (expected, begin_lines[0]))


class ConnectorSettingsDefaultTest(TestCase):
    """
    The internal logic for protocol settings in listener and connector
    is common code. This test makes sure that defaults in the connector
    config make it to the wire.
    """
    inter_router_port = None

    @staticmethod
    def ssl_config(client_server, connection):
        return []  # Over-ridden by RouterTestSsl

    @classmethod
    def setUpClass(cls):
        """Start two routers"""
        super(ConnectorSettingsDefaultTest, cls).setUpClass()

        def router(name, client_server, connection):
            config = cls.ssl_config(client_server, connection) + [
                ('router', {'mode': 'interior', 'id': 'QDR.%s' % name}),

                ('listener', {'port': cls.tester.get_port()}),
                connection
            ]

            config = Qdrouterd.Config(config)

            cls.routers.append(cls.tester.qdrouterd(name, config, wait=True))

        cls.routers = []

        inter_router_port = cls.tester.get_port()

        router('A', 'server',
               ('listener', {'role': 'inter-router', 'port': inter_router_port}))
        router('B', 'client',
               ('connector', {'name': 'connectorToA', 'role': 'inter-router', 'port': inter_router_port}))

        cls.routers[0].wait_router_connected('QDR.B')
        cls.routers[1].wait_router_connected('QDR.A')

    def test_connector_default(self):
        with  open('../setUpClass/A.log', 'r') as router_log:
            log_lines = router_log.read().split("\n")
            open_lines = [s for s in log_lines if "<- @open" in s]
            # defaults
            self.assertTrue(' max-frame-size=16384,' in open_lines[0])
            self.assertTrue(' channel-max=32767,' in open_lines[0])
            begin_lines = [s for s in log_lines if "<- @begin" in s]
            # incoming-window should be 2^31-1 (64-bit) or
            # (2^31-1) / max-frame-size (32-bit)
            is_64bits = sys.maxsize > 2 ** 32
            expected = " incoming-window=2147483647," if is_64bits else \
                (" incoming-window=%d," % int(2147483647 / 16384))
            self.assertTrue(expected in begin_lines[0],
                            "Expected:'%s' not found in '%s'" % (expected, begin_lines[0]))


class ConnectorSettingsNondefaultTest(TestCase):
    """
    The internal logic for protocol settings in listener and connector
    is common code. This test makes sure that settings in the connector
    config make it to the wire. The listener tests test the setting logic.
    """
    inter_router_port = None

    @staticmethod
    def ssl_config(client_server, connection):
        return []  # Over-ridden by RouterTestSsl

    @classmethod
    def setUpClass(cls):
        """Start two routers"""
        super(ConnectorSettingsNondefaultTest, cls).setUpClass()

        def router(name, client_server, connection):
            config = cls.ssl_config(client_server, connection) + [
                ('router', {'mode': 'interior', 'id': 'QDR.%s' % name}),

                ('listener', {'port': cls.tester.get_port()}),
                connection
            ]

            config = Qdrouterd.Config(config)

            cls.routers.append(cls.tester.qdrouterd(name, config, wait=True))

        cls.routers = []

        inter_router_port = cls.tester.get_port()

        router('A', 'server',
               ('listener', {'role': 'inter-router', 'port': inter_router_port}))
        router('B', 'client',
               ('connector', {'name': 'connectorToA', 'role': 'inter-router', 'port': inter_router_port,
                              'maxFrameSize': '2048', 'maxSessionFrames': '10', 'maxSessions': '20'}))

        cls.routers[0].wait_router_connected('QDR.B')
        cls.routers[1].wait_router_connected('QDR.A')

    def test_connector_default(self):
        with  open('../setUpClass/A.log', 'r') as router_log:
            log_lines = router_log.read().split("\n")
            open_lines = [s for s in log_lines if "<- @open" in s]
            # nondefaults
            self.assertTrue(' max-frame-size=2048,' in open_lines[0])
            self.assertTrue(' channel-max=19,' in open_lines[0])
            begin_lines = [s for s in log_lines if "<- @begin" in s]
            # nondefaults
            self.assertTrue(" incoming-window=10," in begin_lines[0])


if __name__ == '__main__':
    unittest.main(main_module())

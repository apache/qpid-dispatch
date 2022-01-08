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

from system_test import TestCase, Qdrouterd, main_module
from system_test import unittest, TIMEOUT, TestTimeout
from proton.handlers import MessagingHandler
from proton.reactor import Container


class MaxFrameMaxSessionFramesTest(TestCase):
    """System tests setting proton negotiated size max-frame-size and incoming-window"""
    @classmethod
    def setUpClass(cls):
        super(MaxFrameMaxSessionFramesTest, cls).setUpClass()
        name = "MaxFrameMaxSessionFrames"
        config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'QDR'}),

            ('listener', {'host': '0.0.0.0', 'port': cls.tester.get_port(), 'maxFrameSize': '2048', 'maxSessionFrames': '10'}),
        ])
        cls.router = cls.tester.qdrouterd(name, config)
        cls.router.wait_ready()
        cls.address = cls.router.addresses[0]

    def test_max_frame_max_session_frames_max_sessions_default(self):
        sniffer = ProtocolSettingsSniffer(self.router.addresses[0], "xxx")
        sniffer.run()
        self.assertEqual(2048, sniffer.remote_max_frame)
        self.assertEqual(32767, sniffer.remote_channel_max)


class MaxSessionsTest(TestCase):
    """System tests setting proton channel-max"""
    @classmethod
    def setUpClass(cls):
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
        sniffer = ProtocolSettingsSniffer(self.router.addresses[0], "xxx")
        sniffer.run()
        self.assertEqual(9, sniffer.remote_channel_max)


class MaxSessionsZeroTest(TestCase):
    """System tests setting proton channel-max"""
    @classmethod
    def setUpClass(cls):
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
        sniffer = ProtocolSettingsSniffer(self.router.addresses[0], "xxx")
        sniffer.run()
        self.assertEqual(32767, sniffer.remote_channel_max)


class MaxSessionsLargeTest(TestCase):
    """System tests setting proton channel-max"""
    @classmethod
    def setUpClass(cls):
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
        sniffer = ProtocolSettingsSniffer(self.router.addresses[0], "xxx")
        sniffer.run()
        self.assertEqual(32767, sniffer.remote_channel_max)


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
        sniffer = ProtocolSettingsSniffer(self.router.addresses[0], "xxx")
        sniffer.run()
        self.assertEqual(512, sniffer.remote_max_frame)


class MaxFrameDefaultTest(TestCase):
    """System tests setting proton max-frame-size"""
    @classmethod
    def setUpClass(cls):
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
        sniffer = ProtocolSettingsSniffer(self.router.addresses[0], "xxx")
        sniffer.run()
        self.assertEqual(16384, sniffer.remote_max_frame)


class ConnectorSettingsDefaultTest(TestCase):
    """
    The internal logic for protocol settings in listener and connector
    is common code. This test makes sure that defaults in the connector
    config make it to the wire.
    """
    @classmethod
    def setUpClass(cls):
        super(ConnectorSettingsDefaultTest, cls).setUpClass()

        def router(name, client_server, connection):
            config = [
                ('router', {'mode': 'interior', 'id': 'QDR.%s' % name}),

                ('listener', {'port': cls.tester.get_port()}),
                connection
            ]

            config = Qdrouterd.Config(config)

            cls.routers.append(cls.tester.qdrouterd(name, config, wait=False))

        cls.routers = []

        cls.connector_port = cls.tester.get_port()

        router('A', 'server',
               ('connector', {'name': 'testconnector', 'role': 'route-container', 'port': cls.connector_port}))

    def test_connector_default(self):
        sniffer = ConnectorSettingsSniffer(self.routers[0].connector_addresses[0])
        sniffer.run()
        self.assertEqual(16384, sniffer.remote_max_frame)
        self.assertEqual(32767, sniffer.remote_channel_max)


class ConnectorSettingsNondefaultTest(TestCase):
    """
    The internal logic for protocol settings in listener and connector
    is common code. This test makes sure that settings in the connector
    config make it to the wire.
    """
    @classmethod
    def setUpClass(cls):
        super(ConnectorSettingsNondefaultTest, cls).setUpClass()

        def router(name, client_server, connection):
            config = [
                ('router', {'mode': 'interior', 'id': 'QDR.%s' % name}),

                ('listener', {'port': cls.tester.get_port()}),
                connection
            ]

            config = Qdrouterd.Config(config)

            cls.routers.append(cls.tester.qdrouterd(name, config, wait=False))

        cls.routers = []

        cls.connector_port = cls.tester.get_port()

        router('A', 'server',
               ('connector', {'name': 'testconnector', 'role': 'route-container', 'port': cls.connector_port,
                              'maxFrameSize': '2048', 'maxSessionFrames': '10', 'maxSessions': '20'}))

    def test_connector_default(self):
        sniffer = ConnectorSettingsSniffer(self.routers[0].connector_addresses[0])
        sniffer.run()
        self.assertEqual(2048, sniffer.remote_max_frame)
        self.assertEqual(19, sniffer.remote_channel_max)


class ProtocolSettingsSniffer(MessagingHandler):
    """Create a Connection/Session/Link and capture the various protocol
    settings sent by the peer.
    """
    def __init__(self, router_addr, source_addr="xxx", **kwargs):
        super(ProtocolSettingsSniffer, self).__init__(**kwargs)
        self.router_addr = router_addr
        self.source_addr = source_addr
        self.conn = None
        self.remote_max_frame = None
        self.remote_channel_max = None

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.conn = event.container.connect(self.router_addr)
        self.receiver = event.container.create_receiver(self.conn, self.source_addr)

    def timeout(self):
        self.error = "Timeout Expired - could not connect to router"
        self.conn.close()

    def on_link_opened(self, event):
        tport = event.transport
        self.remote_max_frame = tport.remote_max_frame_size
        self.remote_channel_max = tport.remote_channel_max
        self.conn.close()
        self.timer.cancel()

    def run(self):
        Container(self).run()


class ConnectorSettingsSniffer(MessagingHandler):
    """Similar to ProtocolSettingsSniffer, but for router-initiated connections"""
    def __init__(self, url, **kwargs):
        super(ConnectorSettingsSniffer, self).__init__(**kwargs)
        self.listener_addr = url
        self.acceptor = None
        self.remote_max_frame = None
        self.remote_channel_max = None

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, TestTimeout(self))
        self.acceptor = event.container.listen(self.listener_addr)

    def timeout(self):
        self.error = "Timeout Expired - router not connecting"
        if self.acceptor:
            self.acceptor.close()

    def on_connection_opened(self, event):
        tport = event.transport
        self.remote_max_frame = tport.remote_max_frame_size
        self.remote_channel_max = tport.remote_channel_max
        event.connection.close()
        self.acceptor.close()
        self.timer.cancel()

    def run(self):
        Container(self).run()


if __name__ == '__main__':
    unittest.main(main_module())

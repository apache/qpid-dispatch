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
# under the License
#

import unittest

from system_test import Qdrouterd
from system_test import main_module, TestCase, Process


class WebsocketsConsoleTest(TestCase):
    """Run websockets tests connecting to the console"""

    @classmethod
    def setUpClass(cls):
        super().setUpClass()

        cls.http_port = cls.tester.get_port()

        config = [
            ('router', {'mode': 'interior', 'id': 'A'}),
            ('listener', {'role': 'normal', 'port': cls.http_port, 'http': True})
        ]
        config = Qdrouterd.Config(config)
        cls.router: Qdrouterd = cls.tester.qdrouterd('A', config, wait=True, expect=Process.EXIT_OK)

    def test_stopping_broker_while_websocket_is_connected_does_not_crash(self):
        import asyncio
        import websockets

        async def run():
            uri = f"ws://localhost:{self.http_port}"
            async with websockets.connect(uri, subprotocols=['amqp']) as websocket:
                self.router.terminate()
                self.router.wait()

        asyncio.get_event_loop().run_until_complete(run())


if __name__ == '__main__':
    unittest.main(main_module())

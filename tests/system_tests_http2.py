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
from time import sleep

from system_test import TestCase, Qdrouterd, Process


class Http2Test(TestCase):
    @classmethod
    def setUpClass(cls):
        super(Http2Test, cls).setUpClass()
        name = "http2-test-router"
        cls.router_http2_listen_port = cls.tester.get_port()
        config = Qdrouterd.Config([
            ('router', {'mode': 'standalone', 'id': 'QDR'}),
            ('listener', {'port': cls.router_http2_listen_port, 'role': 'normal', 'host': '0.0.0.0'}),
            ('httpListener', {'port': cls.tester.get_port(), 'address': 'examples',
                              'host': '0.0.0.0'})

        ])
        cls.router = cls.tester.qdrouterd(name, config, wait=True)
        cls.http2_server_name = "http2_server"
        #cls.server_http2_listen_port = cls.tester.get_port()
        cls.server_http2_listen_port = 5000
        os.environ["QUART_APP"] = "http2server:app"
        cls.http2_server = cls.tester.httpserver(name=cls.http2_server_name,
                                                 listen_port=cls.server_http2_listen_port,
                                                 py_string='python3',
                                                 server_file="http2_server.py")
        sleep(5)

    def test_head_request(self):
        pass


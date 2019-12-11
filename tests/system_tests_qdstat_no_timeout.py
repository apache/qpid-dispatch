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
import time
import threading
from subprocess import PIPE

from proton import Url
from proton.handlers import MessagingHandler
from proton.reactor import Container

from system_test import TestCase, Qdrouterd

total_links_count = 0

class Links(MessagingHandler):
    total_links_count = 0
    def __init__(self, source, links, connection_id):
        super(Links, self).__init__()
        self.source = source
        self.links = links
        self.link_count = 0
        self.connection_id = connection_id

    def create_link(self, container, connection):
            container.create_receiver(connection, source="%s.%s.%s" % (self.source, self.link_count, self.connection_id), handler=self)

    def on_link_opened(self, event):
        self.link_count += 1
        Links.total_links_count += 1
        #print("link opened %s of %s" % (self.link_count, self.links))
        if self.link_count < self.links:
            self.create_link(event.container, event.connection)

class Test(MessagingHandler, threading.Thread):
    def __init__(self, url, links, connections):
        super(Test, self).__init__()
        threading.Thread.__init__(self)
        self.url = Url(url)
        self.links = links
        self.connections = connections
        self.connections_list = []
        self.connection_count = 0
        self.link_count = 0
        self.receiver = None

    def on_start(self, event):
        main_c = event.container.connect(self.url)
        self.connections_list.append(main_c)
        self.receiver = event.container.create_receiver(main_c, "stop")

    def close_connections(self):
        for c in self.connections_list:
            c.close()

    def kill(self):
        self.close_connections()

    def run(self):
        self.container = Container(self)
        self.container.run()

    def on_connection_opened(self, event):
        self.connection_count += 1
        #print("connection opened %s of %s" % (self.connection_count, self.connections))
        Links(self.url.path, self.links, self.connection_count).create_link(event.container, event.connection)
        if self.connection_count < self.connections:
            self.connections_list.append(event.container.connect(self.url))

class TestMobileAddress(TestCase):
    @classmethod
    def setUpClass(cls):
        super(TestMobileAddress, cls).setUpClass()

        cls.listen_port_1 = cls.tester.get_port()
        cls.listen_port_2 = cls.tester.get_port()
        cls.listen_port_inter_router = cls.tester.get_port()

        config_1 = Qdrouterd.Config([
            ('router', {'mode': 'interior', 'id': 'A'}),
            ('listener', {'port': cls.listen_port_1, 'authenticatePeer': False, 'saslMechanisms': 'ANONYMOUS'}),
            ('listener', {'role': 'inter-router', 'port': cls.listen_port_inter_router, 'authenticatePeer': False, 'saslMechanisms': 'ANONYMOUS'}),
           ])

        config_2 = Qdrouterd.Config([
            ('router', {'mode': 'interior', 'id': 'B'}),
            ('listener', {'port': cls.listen_port_2, 'authenticatePeer': False, 'saslMechanisms': 'ANONYMOUS'}),
            ('connector', {'name': 'connectorToA', 'role': 'inter-router',
                           'port': cls.listen_port_inter_router,
                           'verifyHostname': 'no'}),
            ])

        cls.routers = []
        cls.routers.append(cls.tester.qdrouterd("A", config_1, wait=True))
        cls.routers.append(cls.tester.qdrouterd("B", config_2, wait=True))
        cls.routers[1].wait_router_connected('A')

    def test_qdstat_on_a_lot_of_connections(self):
        A_LOT_OF_LINKS = 100
        A_LOT_OF_CONNECTIONS = 500
        t = Test("127.0.0.1:%s/some/address" % self.listen_port_1,
                       A_LOT_OF_LINKS, A_LOT_OF_CONNECTIONS)

        t.start()

        start = time.time()
        TIMEOUT_S = 60
        while(Links.total_links_count < A_LOT_OF_CONNECTIONS * A_LOT_OF_LINKS):
            self.assertTrue(time.time() - start < TIMEOUT_S)
            time.sleep(1)

        popen_args = ["qdstat", "-a", "-b", "127.0.0.1:%s" % self.listen_port_2,
                      "-r", "A"]

        p = self.popen(popen_args,
            name='qdstat-'+self.id(), stdout=PIPE, expect=None,
            universal_newlines=True)
        stdout, stderr = p.communicate()
        #assert something on STDOUT CONTENT?

        t.kill()
        self.assertEqual(p.returncode, 0)

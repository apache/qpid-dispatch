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

import os
import sys
from time import sleep
from signal import SIGINT
from subprocess import PIPE

from proton import Message

from system_test import TestCase
from system_test import Qdrouterd
from system_test import main_module
from system_test import TIMEOUT
from system_test import Process
from system_test import AsyncTestSender
from system_test import unittest


class ThreeRouterTest(TestCase):

    @classmethod
    def setUpClass(cls):
        """
        Create a mesh of three routers.  Reject any links or messages sent to
        an unavailable address.
        """
        super(ThreeRouterTest, cls).setUpClass()

        def router(name, extra_config):
            config = [

                ('router', {'id': name,
                            'mode': 'interior',
                            "defaultDistribution": "unavailable"}),
                ('listener', {'role': 'normal',
                              'port': cls.tester.get_port(),
                              "linkCapacity": '100'}),

                ('address', {'prefix': 'closest', 'distribution': 'closest'}),
                ('address', {'prefix': 'balanced', 'distribution': 'balanced'}),
                ('address', {'prefix': 'multicast', 'distribution': 'multicast'}),
            ] + extra_config

            config = Qdrouterd.Config(config)

            cls.routers.append(cls.tester.qdrouterd(name, config, wait=False))

        cls.routers = []

        inter_router_A = cls.tester.get_port()
        inter_router_B = cls.tester.get_port()
        inter_router_C = cls.tester.get_port()

        router('RouterA',
               [('listener', {'role': 'inter-router', 'port': inter_router_A}),
                ('connector', {'role': 'inter-router', 'port': inter_router_B})])

        router('RouterB',
               [('listener', {'role': 'inter-router', 'port': inter_router_B}),
                ('connector', {'role': 'inter-router', 'port': inter_router_C})])

        router('RouterC',
               [('listener', {'role': 'inter-router', 'port': inter_router_C}),
                ('connector', {'role': 'inter-router', 'port': inter_router_A})])

        cls.RouterA = cls.routers[0]
        cls.RouterB = cls.routers[1]
        cls.RouterC = cls.routers[2]

        cls.RouterA.wait_router_connected('RouterB')
        cls.RouterA.wait_router_connected('RouterC')
        cls.RouterB.wait_router_connected('RouterA')
        cls.RouterB.wait_router_connected('RouterC')
        cls.RouterC.wait_router_connected('RouterA')
        cls.RouterC.wait_router_connected('RouterB')

    def server_address(self, router):
        return router.addresses[0]

    def server_port(self, router):
        return router.ports[0]  # first listener is for client connection

    def server_host(self, router):
        fam = router.ports_family
        return router.get_host(fam.get(self.server_port(router),
                                       "IPv4"))

    def spawn_receiver(self, router, count, address, *extra_args):
        cmd = ["test-receiver",
               "-a", "%s:%s" % (self.server_host(router),
                                self.server_port(router)),
               "-c", str(count), "-s", address] + list(extra_args)
        # env = dict(os.environ, PN_TRACE_FRM="1")
        # return self.popen(cmd, expect=Process.EXIT_OK, env=env)
        return self.popen(cmd, expect=Process.EXIT_OK)

    def spawn_sender(self, router, count, address, *extra_args):
        cmd = ["test-sender",
               "-a", "%s:%s" % (self.server_host(router),
                                self.server_port(router)),
               "-c", str(count), "-t", address] + list(extra_args)
        # env = dict(os.environ, PN_TRACE_FRM="1")
        # return self.popen(cmd, expect=Process.EXIT_OK, env=env)
        return self.popen(cmd, expect=Process.EXIT_OK)

    def _rx_failover(self, extra_tx_args=None, extra_rx_args=None):
        # Have a single sender transmit unsettled as fast as possible
        # non-stop.  Have a single receiver that consumes a small number of
        # messages before failing over to a different router in the mesh
        extra_tx = extra_tx_args or []
        extra_rx = extra_rx_args or []
        total = 100
        router_index = 0
        tx = self.spawn_sender(self.RouterC, 0, "balanced/foo", *extra_tx)
        while total > 0:
            rx = self.spawn_receiver(self.routers[router_index], 5,
                                     "balanced/foo", *extra_rx)
            if rx.wait(timeout=TIMEOUT):
                raise Exception("Receiver failed to consume all messages")
            total -= 5
            router_index += 1
            if router_index == len(self.routers):
                router_index = 0
        tx.send_signal(SIGINT)
        out_text, out_err = tx.communicate(timeout=TIMEOUT)
        if tx.returncode:
            raise Exception("Sender failed: %s %s"
                            % (out_text, out_err))

    def test_01_rx_failover_clean(self):
        """
        Runs the receiver failover test.  In this test the receiver cleanly
        shuts down the AMQP endpoint before failing over.
        """
        self._rx_failover()


    def test_02_rx_failover_dirty(self):
        """
        Runs the receiver failover test.  In this test the receiver abruptly
        drops the TCP connection simulating a client crash.
        """
        tcp_drop = ["-E"]
        self._rx_failover(extra_rx_args=tcp_drop)

    def test_03_unavailable_link_attach(self):
        """
        Attempt to attach a link to an unavailable address, expect the router
        to detach it
        """
        ats = AsyncTestSender(self.server_address(self.RouterA),
                              "an/unavailable/address")
        try:
            ats.wait()
            self.assertTrue(False)  # expect exception
        except AsyncTestSender.TestSenderException as exc:
            self.assertIn("link error", ats.error)

    def test_04_unavailable_anonymous_link_attach(self):
        """
        Attempt to attach an anonymous link and send a message to an
        unavailable address.  Expect to allow the link, but REJECT the message
        """
        message = Message(body="REJECTED!!!")
        message.address = "another/unavailable/address"
        ats = AsyncTestSender(self.server_address(self.RouterA),
                              target=None,
                              message=message)
        ats.wait()
        self.assertEqual(0, ats.accepted)
        self.assertEqual(1, ats.rejected)

    def test_05_unavailable_anonymous_link_send(self):
        """
        Attach an anonymous link and send to a configured address (no
        subscribers).  Expect to allow the link, but RELEASE the message
        """
        message = Message(body="Release me, let me go...")
        message.address = "closest/foo"
        ats = AsyncTestSender(self.server_address(self.RouterA),
                              target=None,
                              message=message)
        ats.wait()
        self.assertEqual(0, ats.accepted)
        self.assertEqual(1, ats.released)
        self.assertEqual(0, ats.rejected)

    def test_06_parallel_priority(self):
        """
        Create 10 senders each with a different priority.  Send large messages
        - large enough to trigger Qx flow control (sender argument "-sx").
        Ensure all messages arrive as expected.
        """
        priorities = 10
        send_batch = 25

        total = priorities * send_batch
        rx = self.spawn_receiver(self.RouterC,
                                 count=total,
                                 address="closest/test_06_address")
        self.RouterA.wait_address("closest/test_06_address")

        senders = [self.spawn_sender(self.RouterA,
                                     send_batch,
                                     "closest/test_06_address",
                                     "-sx", "-p%s" % p)
                   for p in range(priorities)]

        if rx.wait(timeout=TIMEOUT):
            raise Exception("Receiver failed to consume all messages")
        for tx in senders:
            out_text, out_err = tx.communicate(timeout=TIMEOUT)
            if tx.returncode:
                raise Exception("Sender failed: %s %s" % (out_text, out_err))


if __name__ == '__main__':
    unittest.main(main_module())

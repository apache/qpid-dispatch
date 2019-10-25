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

from time import sleep
import json, os
import logging
from threading import Timer
from subprocess import PIPE, STDOUT
from proton import Message, Timeout, Delivery
from system_test import TestCase, Process, Qdrouterd, main_module, TIMEOUT, DIR
from system_test import AsyncTestReceiver
from system_test import AsyncTestSender
from system_test import unittest

from proton.handlers import MessagingHandler
from proton.reactor import Container, AtLeastOnce
from proton.utils import BlockingConnection
from qpid_dispatch.management.client import Node
CONNECTION_PROPERTIES_UNICODE_STRING = {u'connection': u'properties', u'int_property': 6451}

class TwoRouterTest(TestCase):

    inter_router_port = None

    @classmethod
    def setUpClass(cls):
        """Start a router and a messenger"""
        super(TwoRouterTest, cls).setUpClass()

        def router(name, client_server, connection):
            policy_config_path = os.path.join(DIR, 'two-router-policy')

            config = [
                # Use the deprecated attributes helloInterval, raInterval, raIntervalFlux, remoteLsMaxAge
                # The routers should still start successfully after using these deprecated entities.
                ('router', {'remoteLsMaxAge': 60, 'helloInterval': 1, 'raInterval': 30, 'raIntervalFlux': 4,
                            'mode': 'interior', 'id': 'QDR.%s'%name, 'allowUnsettledMulticast': 'yes'}),
                ('listener', {'port': cls.tester.get_port(), 'stripAnnotations': 'no', 'linkCapacity': 500}),

                ('listener', {'port': cls.tester.get_port(), 'stripAnnotations': 'no'}),
                ('listener', {'port': cls.tester.get_port(), 'stripAnnotations': 'both'}),
                ('listener', {'port': cls.tester.get_port(), 'stripAnnotations': 'out'}),
                ('listener', {'port': cls.tester.get_port(), 'stripAnnotations': 'in'}),

                ('address', {'prefix': 'closest', 'distribution': 'closest'}),
                ('address', {'prefix': 'balanced', 'distribution': 'balanced'}),
                ('address', {'prefix': 'multicast', 'distribution': 'multicast'}),

                # for testing pattern matching
                ('address', {'pattern': 'a.b.c.d',
                             'distribution': 'closest'}),
                ('address', {'pattern': '#.b.c.d',
                             'distribution': 'multicast'}),
                ('address', {'pattern': 'a/*/#/d',
                             'distribution': 'closest'}),
                ('address', {'pattern': '*/b/c/d',
                             'distribution': 'multicast'}),
                ('address', {'pattern': 'a.x.d',
                             'distribution': 'closest'}),
                ('address', {'pattern': 'a.*.d',
                             'distribution': 'multicast'}),
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
                              'verifyHostname': 'no'}))

        cls.routers[0].wait_router_connected('QDR.B')
        cls.routers[1].wait_router_connected('QDR.A')

    def address(self):
        return self.routers[0].addresses[0]

    def run_qdmanage(self, cmd, input=None, expect=Process.EXIT_OK, address=None):
        p = self.popen(
            ['qdmanage'] + cmd.split(' ') + ['--bus', address or self.address(), '--indent=-1', '--timeout', str(TIMEOUT)],
            stdin=PIPE, stdout=PIPE, stderr=STDOUT, expect=expect,
            universal_newlines=True)
        out = p.communicate(input)[0]
        try:
            p.teardown()
        except Exception as e:
            raise Exception(out if out else str(e))
        return out

    def test_01_pre_settled(self):
        test = DeliveriesInTransit(self.routers[0].addresses[0], self.routers[1].addresses[0])
        test.run()
        self.assertEqual(None, test.error)

        local_node = Node.connect(self.routers[0].addresses[0], timeout=TIMEOUT)
        outs = local_node.query(type='org.apache.qpid.dispatch.router')

        # deliveriesTransit must most surely be greater than num_msgs
        pos = outs.attribute_names.index("deliveriesTransit")
        results = outs.results[0]
        self.assertTrue(results[pos] > 104)

    def test_02a_multicast_unsettled(self):
        test = MulticastUnsettled(self.routers[0].addresses[0])
        test.run()
        self.assertEqual(None, test.error)

    def test_02c_sender_settles_first(self):
        test = SenderSettlesFirst(self.routers[0].addresses[0], self.routers[1].addresses[0])
        test.run()
        self.assertEqual(None, test.error)

    def test_03_message_annotations(self):
        test = MessageAnnotationsTest(self.routers[0].addresses[0], self.routers[1].addresses[0])
        test.run()
        self.assertEqual(None, test.error)

    def test_03a_test_strip_message_annotations_no(self):
        test = MessageAnnotationsStripTest(self.routers[0].addresses[1], self.routers[1].addresses[1])
        test.run()
        self.assertEqual(None, test.error)

    def test_03a_test_strip_message_annotations_no_add_trace(self):
        test = MessageAnnotationsStripAddTraceTest(self.routers[0].addresses[1], self.routers[1].addresses[1])
        test.run()
        self.assertEqual(None, test.error)

    def test_03a_test_strip_message_annotations_both_add_ingress_trace(self):
        test = MessageAnnotationsStripBothAddIngressTrace(self.routers[0].addresses[2], self.routers[1].addresses[2])
        test.run()
        self.assertEqual(None, test.error)

    def test_03a_test_strip_message_annotations_out(self):
        test = MessageAnnotationsStripMessageAnnotationsOut(self.routers[0].addresses[3], self.routers[1].addresses[3])
        test.run()
        self.assertEqual(None, test.error)

    def test_03a_test_strip_message_annotations_in(self):
        test = MessageAnnotationStripMessageAnnotationsIn(self.routers[0].addresses[4], self.routers[1].addresses[4])
        test.run()
        self.assertEqual(None, test.error)

    def test_04_management(self):
        test = ManagementTest(self.routers[0].addresses[0])
        test.run()
        self.assertEqual(None, test.error)

    def test_06_semantics_closest_is_local(self):
        test = SemanticsClosestIsLocal(self.routers[0].addresses[0], self.routers[1].addresses[0])
        test.run()
        self.assertEqual(None, test.error)

    def test_07_semantics_closest_is_remote(self):
        test = SemanticsClosestIsRemote(self.routers[0].addresses[0], self.routers[1].addresses[0])
        test.run()
        self.assertEqual(None, test.error)

    def test_08_semantics_balanced(self):
        test = SemanticsBalanced(self.routers[0].addresses[0], self.routers[0].addresses[1],
                                 self.routers[1].addresses[0])
        test.run()
        self.assertEqual(None, test.error)

    def test_09_to_override(self):
        test = MessageAnnotaionsPreExistingOverride(self.routers[0].addresses[0], self.routers[1].addresses[0])
        test.run()
        self.assertEqual(None, test.error)

    def test_10_propagated_disposition(self):
        test = PropagatedDisposition(self, self.routers[0].addresses[0], self.routers[1].addresses[0])
        test.run()
        self.assertTrue(test.passed)

    def test_11_three_ack(self):
        test = ThreeAck(self, self.routers[0].addresses[0], self.routers[1].addresses[0])
        test.run()

    def test_12_excess_deliveries_released(self):
        """
        Message-route a series of deliveries where the receiver provides credit for a subset and
        once received, closes the link.  The remaining deliveries should be released back to the sender.
        """
        test = ExcessDeliveriesReleasedTest(self.routers[0].addresses[0], self.routers[1].addresses[0])
        test.run()
        self.assertEqual(None, test.error)

    def test_15_attach_on_inter_router(self):
        test = AttachOnInterRouterTest(self.routers[0].addresses[5])
        test.run()
        self.assertEqual(None, test.error)

    def test_17_address_wildcard(self):
        # verify proper distribution is selected by wildcard
        addresses = [
            # (address, count of messages expected to be received)
            ('a.b.c.d',   1), # closest 'a.b.c.d'
            ('b.c.d',     2), # multi   '#.b.c.d'
            ('f.a.b.c.d', 2), # multi   '#.b.c.d
            ('a.c.d',     2), # multi   'a.*.d'
            ('a/c/c/d',   1), # closest 'a/*/#.d
            ('a/x/z/z/d', 1), # closest 'a/*/#.d
            ('a/x/d',     1), # closest 'a.x.d'
            ('a.x.e',     1), # balanced  ----
            ('m.b.c.d',   2)  # multi   '*/b/c/d'
        ]

        # two receivers per address - one for each router
        receivers = []
        for a in addresses:
            for x in range(2):
                ar = AsyncTestReceiver(address=self.routers[x].addresses[0],
                                       source=a[0])
                receivers.append(ar)

        # wait for the consumer info to propagate
        for a in addresses:
            self.routers[0].wait_address(a[0], 1, 1)
            self.routers[1].wait_address(a[0], 1, 1)

        # send one message to each address
        conn = BlockingConnection(self.routers[0].addresses[0])
        sender = conn.create_sender(address=None, options=AtLeastOnce())
        for a in addresses:
            sender.send(Message(address=a[0], body={'address': a[0]}))

        # count received messages by address
        msgs_recvd = {}
        for M in receivers:
            try:
                while True:
                    i = M.queue.get(timeout=0.2).body.get('address', "ERROR")
                    if i not in msgs_recvd:
                        msgs_recvd[i] = 0
                    msgs_recvd[i] += 1
            except AsyncTestReceiver.Empty:
                pass

        # verify expected count == actual count
        self.assertTrue("ERROR" not in msgs_recvd)
        for a in addresses:
            self.assertTrue(a[0] in msgs_recvd)
            self.assertEqual(a[1], msgs_recvd[a[0]])

        for M in receivers:
            M.stop()
        conn.close()

    def test_17_large_streaming_test(self):
        test = LargeMessageStreamTest(self.routers[0].addresses[0], self.routers[1].addresses[0])
        test.run()
        self.assertEqual(None, test.error)

    def test_18_single_char_dest_test(self):
        test = SingleCharacterDestinationTest(self.routers[0].addresses[0], self.routers[1].addresses[0])
        test.run()
        self.assertEqual(None, test.error)

    def test_19_delete_inter_router_connection(self):
        """
        This test tries to delete an inter-router connection but is
        prevented from doing so.
        """
        query_command = 'QUERY --type=connection'
        outputs = json.loads(self.run_qdmanage(query_command))
        identity = None
        passed = False

        for output in outputs:
            if "inter-router" == output['role']:
                identity = output['identity']
                if identity:
                    update_command = 'UPDATE --type=connection adminStatus=deleted --id=' + identity
                    try:
                        json.loads(self.run_qdmanage(update_command))
                    except Exception as e:
                        if "Forbidden" in str(e):
                            passed = True

        # The test has passed since we were forbidden from deleting
        # inter-router connections even though we are allowed to update the adminStatus field.
        self.assertTrue(passed)

    def test_20_delete_connection(self):
        """
        This test creates a blocking connection and tries to delete that connection.
        Since  there is no policy associated with this router, the default for allowAdminStatusUpdate is true,
        the delete operation will be permitted.
        """

        # Create a connection with some properties so we can easily identify the connection
        connection = BlockingConnection(self.address(),
                                        properties=CONNECTION_PROPERTIES_UNICODE_STRING)
        query_command = 'QUERY --type=connection'
        outputs = json.loads(self.run_qdmanage(query_command))
        identity = None
        passed = False

        print ()

        for output in outputs:
            if output.get('properties'):
                conn_properties = output['properties']
                # Find the connection that has our properties - CONNECTION_PROPERTIES_UNICODE_STRING
                # Delete that connection and run another qdmanage to see
                # if the connection is gone.
                if conn_properties.get('int_property'):
                    identity = output.get("identity")
                    if identity:
                        update_command = 'UPDATE --type=connection adminStatus=deleted --id=' + identity
                        try:
                            self.run_qdmanage(update_command)
                            query_command = 'QUERY --type=connection'
                            outputs = json.loads(
                                self.run_qdmanage(query_command))
                            no_properties = True
                            for output in outputs:
                                if output.get('properties'):
                                    no_properties = False
                                    conn_properties = output['properties']
                                    if conn_properties.get('int_property'):
                                        passed = False
                                        break
                                    else:
                                        passed = True
                            if no_properties:
                                passed = True
                        except Exception as e:
                            passed = False

        # The test has passed since we were allowed to delete a connection
        # because we have the policy permission to do so.
        self.assertTrue(passed)

    def test_21_delete_connection_with_receiver(self):
        test = DeleteConnectionWithReceiver(self.routers[0].addresses[0])
        self.assertEqual(test.error, None)
        test.run()

    def test_30_huge_address(self):
        # try a link with an extremely long address
        # DISPATCH-1461
        addr = "A" * 2019
        rx = AsyncTestReceiver(self.routers[0].addresses[0],
                               source=addr)
        tx = AsyncTestSender(self.routers[1].addresses[0],
                             target=addr,
                             count=100)
        tx.wait()

        i = 100
        while i:
            try:
                rx.queue.get(timeout=TIMEOUT)
                i -= 1
            except AsyncTestReceiver.Empty:
                break;
        self.assertEqual(0, i)
        rx.stop()


class DeleteConnectionWithReceiver(MessagingHandler):
    def __init__(self, address):
        super(DeleteConnectionWithReceiver, self).__init__()
        self.address = address
        self.mgmt_receiver = None
        self.mgmt_receiver_1 = None
        self.mgmt_receiver_2 = None
        self.conn_to_kill = None
        self.mgmt_conn = None
        self.mgmt_sender = None
        self.success = False
        self.error = None


    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))

        # Create a receiver connection with some properties so it
        # can be easily identified.
        self.conn_to_kill = event.container.connect(self.address, properties=CONNECTION_PROPERTIES_UNICODE_STRING)
        self.receiver_to_kill = event.container.create_receiver(self.conn_to_kill, "hello_world")
        self.mgmt_conn = event.container.connect(self.address)
        self.mgmt_sender = event.container.create_sender(self.mgmt_conn)
        self.mgmt_receiver = event.container.create_receiver(self.mgmt_conn, None, dynamic=True)
        self.mgmt_receiver_1 = event.container.create_receiver(self.mgmt_conn,
                                                             None,
                                                             dynamic=True)
        self.mgmt_receiver_2 = event.container.create_receiver(self.mgmt_conn,
                                                             None,
                                                             dynamic=True)
    def timeout(self):
        self.error = "Timeout Expired: sent=%d, received=%d" % (self.n_sent, self.n_received)
        self.mgmt_conn.close()

    def bail(self, error):
        self.error = error
        self.timer.cancel()
        self.mgmt_conn.close()
        self.conn_to_kill.close()

    def on_link_opened(self, event):
        if event.receiver == self.mgmt_receiver:
            request = Message()
            request.address = "amqp:/_local/$management"
            request.properties = {
                u'type': u'org.apache.qpid.dispatch.connection',
                u'operation': u'QUERY'}
            request.reply_to = self.mgmt_receiver.remote_source.address
            self.mgmt_sender.send(request)

    def on_message(self, event):
        if event.receiver == self.mgmt_receiver:
            attribute_names = event.message.body['attributeNames']
            property_index = attribute_names .index('properties')
            identity_index = attribute_names .index('identity')

            for result in event.message.body['results']:
                if result[property_index]:
                    properties = result[property_index]
                    if properties.get('int_property'):
                        identity = result[identity_index]
                        if identity:
                            request = Message()
                            request.address = "amqp:/_local/$management"
                            request.properties = {
                                u'identity': identity,
                                u'type': u'org.apache.qpid.dispatch.connection',
                                u'operation': u'UPDATE'
                            }
                            request.body = {
                                u'adminStatus':  u'deleted'}
                            request.reply_to = self.mgmt_receiver_1.remote_source.address
                            self.mgmt_sender.send(request)
        elif event.receiver == self.mgmt_receiver_1:
            if event.message.properties['statusDescription'] == 'OK' and event.message.body['adminStatus'] == 'deleted':
                request = Message()
                request.address = "amqp:/_local/$management"
                request.properties = {u'type': u'org.apache.qpid.dispatch.connection',
                                      u'operation': u'QUERY'}
                request.reply_to = self.mgmt_receiver_2.remote_source.address
                self.mgmt_sender.send(request)

        elif event.receiver == self.mgmt_receiver_2:
            attribute_names = event.message.body['attributeNames']
            property_index = attribute_names .index('properties')
            identity_index = attribute_names .index('identity')

            for result in event.message.body['results']:
                if result[property_index]:
                    properties = result[property_index]
                    if properties and properties.get('int_property'):
                        self.bail("Connection not deleted")
            self.bail(None)

    def run(self):
        Container(self).run()

class Timeout(object):
    def __init__(self, parent):
        self.parent = parent

    def on_timer_task(self, event):
        self.parent.timeout()


class SingleCharacterDestinationTest(MessagingHandler):
    def __init__(self, address1, address2):
        super(SingleCharacterDestinationTest, self).__init__()
        self.address1 = address1
        self.address2 = address2
        self.dest = "x"
        self.error = None
        self.conn1 = None
        self.conn2 = None
        self.count = 1
        self.n_sent = 0
        self.timer = None
        self.sender = None
        self.receiver = None
        self.n_received = 0
        self.body = "xyz"

    def check_if_done(self):
        if self.n_received == self.count:
            self.timer.cancel()
            self.conn1.close()
            self.conn2.close()

    def timeout(self):
        self.error = "Timeout Expired: sent=%d, received=%d" % (self.n_sent, self.n_received)
        self.conn1.close()
        self.conn2.close()

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.conn1 = event.container.connect(self.address1)
        self.conn2 = event.container.connect(self.address2)
        self.sender = event.container.create_sender(self.conn1, self.dest)
        self.receiver = event.container.create_receiver(self.conn2, self.dest)

    def on_sendable(self, event):
        if self.n_sent < self.count:
            msg = Message(body=self.body)
            event.sender.send(msg)
            self.n_sent += 1

    def on_message(self, event):
        self.n_received += 1
        self.check_if_done()

    def run(self):
        Container(self).run()


class LargeMessageStreamTest(MessagingHandler):
    def __init__(self, address1, address2):
        super(LargeMessageStreamTest, self).__init__()
        self.address1 = address1
        self.address2 = address2
        self.dest = "LargeMessageStreamTest"
        self.error = None
        self.conn1 = None
        self.conn2 = None
        self.count = 10
        self.n_sent = 0
        self.timer = None
        self.sender = None
        self.receiver = None
        self.n_received = 0
        self.body = ""
        for i in range(10000):
            self.body += "0123456789101112131415"

    def check_if_done(self):
        if self.n_received == self.count:
            self.timer.cancel()
            self.conn1.close()
            self.conn2.close()

    def timeout(self):
        self.error = "Timeout Expired: sent=%d, received=%d" % (self.n_sent, self.n_received)
        self.conn1.close()
        self.conn2.close()

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.conn1 = event.container.connect(self.address1)
        self.conn2 = event.container.connect(self.address2)
        self.sender = event.container.create_sender(self.conn1, self.dest)
        self.receiver = event.container.create_receiver(self.conn2, self.dest)

    def on_sendable(self, event):
        if self.n_sent < self.count:
            msg = Message(body=self.body)
            # send(msg) calls the stream function which streams data from sender to the router
            event.sender.send(msg)
            self.n_sent += 1

    def on_message(self, event):
        self.n_received += 1
        self.check_if_done()

    def run(self):
        Container(self).run()


class ExcessDeliveriesReleasedTest(MessagingHandler):
    def __init__(self, address1, address2):
        super(ExcessDeliveriesReleasedTest, self).__init__(prefetch=0)
        self.address1 = address1
        self.address2 = address2
        self.dest = "closest.EDRtest"
        self.error = None
        self.sender = None
        self.receiver = None
        self.n_sent  = 0
        self.n_received = 0
        self.n_accepted = 0
        self.n_released = 0
        self.timer = None
        self.conn1 = None
        self.conn2 = None

    def timeout(self):
        self.error = "Timeout Expired"
        self.conn1.close()
        self.conn2.close()

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.conn1 = event.container.connect(self.address1)
        self.conn2 = event.container.connect(self.address2)
        self.sender   = event.container.create_sender(self.conn1, self.dest)
        self.receiver = event.container.create_receiver(self.conn2, self.dest)
        self.receiver.flow(6)

    def on_sendable(self, event):
        for i in range(10 - self.n_sent):
            msg = Message(body=i)
            event.sender.send(msg)
            self.n_sent += 1

    def on_accepted(self, event):
        self.n_accepted += 1

    def on_released(self, event):
        self.n_released += 1
        if self.n_released == 4:
            if self.n_accepted != 6:
                self.error = "Expected 6 accepted, got %d" % self.n_accepted
            if self.n_received != 6:
                self.error = "Expected 6 received, got %d" % self.n_received
            self.conn1.close()
            self.conn2.close()
            self.timer.cancel()

    def on_message(self, event):
        self.n_received += 1
        if self.n_received == 6:
            self.receiver.close()

    def run(self):
        Container(self).run()


class AttachOnInterRouterTest(MessagingHandler):
    """Expect an error when attaching a link to an inter-router listener"""
    def __init__(self, address):
        super(AttachOnInterRouterTest, self).__init__(prefetch=0)
        self.address = address
        self.dest = "AOIRtest"
        self.error = None
        self.sender = None
        self.timer = None
        self.conn = None

    def timeout(self):
        self.error = "Timeout Expired"
        self.conn.close()

    def on_start(self, event):
        self.timer  = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.conn   = event.container.connect(self.address)
        self.sender = event.container.create_sender(self.conn, self.dest)

    def on_link_remote_close(self, event):
        self.conn.close()
        self.timer.cancel()

    def run(self):
        logging.disable(logging.ERROR) # Hide expected log errors
        try:
            Container(self).run()
        finally:
            logging.disable(logging.NOTSET) # Restore to normal


class DeliveriesInTransit(MessagingHandler):
    def __init__(self, address1, address2):
        super(DeliveriesInTransit, self).__init__()
        self.address1 = address1
        self.address2 = address2
        self.dest = "pre_settled.1"
        self.error = "All messages not received"
        self.n_sent = 0
        self.timer = None
        self.conn1 = None
        self.conn2 = None
        self.sender = None
        self.num_msgs = 104
        self.sent_count = 0
        self.received_count = 0
        self.receiver = None

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.conn1 = event.container.connect(self.address1)
        self.sender = event.container.create_sender(self.conn1, self.dest)
        self.conn2 = event.container.connect(self.address2)
        self.receiver = event.container.create_receiver(self.conn2, self.dest)

    def on_sendable(self, event):
        if self.n_sent <= self.num_msgs-1:
            msg = Message(body="Hello World")
            self.sender.send(msg)
            self.n_sent += 1

    def check_if_done(self):
        if self.n_sent == self.received_count:
            self.error = None
            self.timer.cancel()
            self.conn1.close()
            self.conn2.close()

    def on_message(self, event):
        self.received_count+=1
        self.check_if_done()

    def run(self):
        Container(self).run()


class MessageAnnotationsTest(MessagingHandler):
    def __init__(self, address1, address2):
        super(MessageAnnotationsTest, self).__init__()
        self.address1 = address1
        self.address2 = address2
        self.dest = "ma/1"
        self.error = "Message annotations not found"
        self.timer = None
        self.conn1 = None
        self.conn2 = None
        self.sender = None
        self.receiver = None
        self.sent_count = 0
        self.msg_not_sent = True

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.conn1 = event.container.connect(self.address1)
        self.sender = event.container.create_sender(self.conn1, self.dest)
        self.conn2 = event.container.connect(self.address2)
        self.receiver = event.container.create_receiver(self.conn2, self.dest)

    def on_sendable(self, event):
        if self.msg_not_sent:
            msg = Message(body={'number': 0})
            event.sender.send(msg)
            self.msg_not_sent = False

    def on_message(self, event):
        if 0 == event.message.body['number']:
            ma = event.message.annotations
            if ma['x-opt-qd.ingress'] == '0/QDR.A' and ma['x-opt-qd.trace'] == ['0/QDR.A', '0/QDR.B']:
                self.error = None
        self.accept(event.delivery)
        self.timer.cancel()
        self.conn1.close()
        self.conn2.close()

    def run(self):
        Container(self).run()


class MessageAnnotationsStripTest(MessagingHandler):
    def __init__(self, address1, address2):
        super(MessageAnnotationsStripTest, self).__init__()
        self.address1 = address1
        self.address2 = address2
        self.dest = "message_annotations_strip_no/1"
        self.error = "Message annotations not found"
        self.timer = None
        self.conn1 = None
        self.conn2 = None
        self.sender = None
        self.receiver = None
        self.sent_count = 0
        self.msg_not_sent = True

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.conn1 = event.container.connect(self.address1)
        self.sender = event.container.create_sender(self.conn1, self.dest)
        self.conn2 = event.container.connect(self.address2)
        self.receiver = event.container.create_receiver(self.conn2, self.dest)

    def on_sendable(self, event):
        if self.msg_not_sent:
            msg = Message(body={'number': 0})
            ingress_message_annotations = {'work': 'hard', 'stay': 'humble'}
            msg.annotations = ingress_message_annotations
            event.sender.send(msg)
            self.msg_not_sent = False

    def on_message(self, event):
        if 0 == event.message.body['number']:
            ma = event.message.annotations
            if ma['x-opt-qd.ingress'] == '0/QDR.A' and ma['x-opt-qd.trace'] == ['0/QDR.A', '0/QDR.B'] \
                    and ma['work'] == 'hard' and ma['stay'] == 'humble':
                self.error = None
        self.accept(event.delivery)
        self.timer.cancel()
        self.conn1.close()
        self.conn2.close()

    def run(self):
        Container(self).run()


class ManagementTest(MessagingHandler):
    def __init__(self, address):
        super(ManagementTest, self).__init__()
        self.address = address
        self.timer = None
        self.conn = None
        self.sender = None
        self.receiver = None
        self.sent_count = 0
        self.msg_not_sent = True
        self.error = None
        self.response1 = False
        self.response2 = False

    def timeout(self):
        if not self.response1:
            self.error = "Incorrect response received for message with correlation id C1"
        if not self.response1:
            self.error = self.error + "Incorrect response received for message with correlation id C2"

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.conn = event.container.connect(self.address)
        self.sender = event.container.create_sender(self.conn)
        self.receiver = event.container.create_receiver(self.conn, None, dynamic=True)

    def on_link_opened(self, event):
        if event.receiver == self.receiver:
            request = Message()
            request.correlation_id = "C1"
            request.address = "amqp:/_local/$management"
            request.properties = {u'type': u'org.amqp.management', u'name': u'self', u'operation': u'GET-MGMT-NODES'}
            request.reply_to = self.receiver.remote_source.address
            self.sender.send(request)

            request = Message()
            request.address = "amqp:/_topo/0/QDR.B/$management"
            request.correlation_id = "C2"
            request.reply_to = self.receiver.remote_source.address
            request.properties = {u'type': u'org.amqp.management', u'name': u'self', u'operation': u'GET-MGMT-NODES'}
            self.sender.send(request)

    def on_message(self, event):
        if event.receiver == self.receiver:
            if event.message.correlation_id == "C1":
                if event.message.properties['statusCode'] == 200 and \
                        event.message.properties['statusDescription'] is not None \
                        and 'amqp:/_topo/0/QDR.B/$management' in event.message.body:
                    self.response1 = True
            elif event.message.correlation_id == "C2":
                if event.message.properties['statusCode'] == 200 and \
                        event.message.properties['statusDescription'] is not None \
                        and 'amqp:/_topo/0/QDR.A/$management' in event.message.body:
                    self.response2 = True

        if self.response1 and self.response2:
            self.error = None

        if self.error is None:
            self.timer.cancel()
            self.conn.close()

    def run(self):
        Container(self).run()


class MessageAnnotationStripMessageAnnotationsIn(MessagingHandler):
    def __init__(self, address1, address2):
        super(MessageAnnotationStripMessageAnnotationsIn, self).__init__()
        self.address1 = address1
        self.address2 = address2
        self.dest = "strip_message_annotations_in/1"
        self.error = "Message annotations not found"
        self.timer = None
        self.conn1 = None
        self.conn2 = None
        self.sender = None
        self.receiver = None
        self.sent_count = 0
        self.msg_not_sent = True

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.conn1 = event.container.connect(self.address1)
        self.sender = event.container.create_sender(self.conn1, self.dest)
        self.conn2 = event.container.connect(self.address2)
        self.receiver = event.container.create_receiver(self.conn2, self.dest)

    def on_sendable(self, event):
        if self.msg_not_sent:
            msg = Message(body={'number': 0})
            #
            # Pre-existing ingress and trace
            #
            ingress_message_annotations = {'x-opt-qd.ingress': 'ingress-router', 'x-opt-qd.trace': ['X/QDR']}
            msg.annotations = ingress_message_annotations
            event.sender.send(msg)
            self.msg_not_sent = False

    def on_message(self, event):
        if 0 == event.message.body['number']:
            if event.message.annotations['x-opt-qd.ingress'] == '0/QDR.A' \
                    and event.message.annotations['x-opt-qd.trace'] == ['0/QDR.A', '0/QDR.B']:
                self.error = None
        self.timer.cancel()
        self.conn1.close()
        self.conn2.close()

    def run(self):
        Container(self).run()


class MessageAnnotaionsPreExistingOverride(MessagingHandler):
    def __init__(self, address1, address2):
        super(MessageAnnotaionsPreExistingOverride, self).__init__()
        self.address1 = address1
        self.address2 = address2
        self.dest = "toov/1"
        self.error = "Pre-existing x-opt-qd.to has been stripped"
        self.timer = None
        self.conn1 = None
        self.conn2 = None
        self.sender = None
        self.receiver = None
        self.msg_not_sent = True

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.conn1 = event.container.connect(self.address1)
        self.sender = event.container.create_sender(self.conn1, self.dest)
        self.conn2 = event.container.connect(self.address2)
        self.receiver = event.container.create_receiver(self.conn2, self.dest)

    def on_sendable(self, event):
        if self.msg_not_sent:
            msg = Message(body={'number': 0})
            msg.annotations = {'x-opt-qd.to': 'toov/1'}
            event.sender.send(msg)
            self.msg_not_sent = False

    def on_message(self, event):
        if 0 == event.message.body['number']:
            ma = event.message.annotations
            if ma['x-opt-qd.to'] == 'toov/1':
                self.error = None
        self.timer.cancel()
        self.conn1.close()
        self.conn2.close()

    def run(self):
        Container(self).run()


class MessageAnnotationsStripMessageAnnotationsOut(MessagingHandler):
    def __init__(self, address1, address2):
        super(MessageAnnotationsStripMessageAnnotationsOut, self).__init__()
        self.address1 = address1
        self.address2 = address2
        self.dest = "strip_message_annotations_out/1"
        self.error = "Message annotations not found"
        self.timer = None
        self.conn1 = None
        self.conn2 = None
        self.sender = None
        self.receiver = None
        self.sent_count = 0
        self.msg_not_sent = True

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.conn1 = event.container.connect(self.address1)
        self.sender = event.container.create_sender(self.conn1, self.dest)
        self.conn2 = event.container.connect(self.address2)
        self.receiver = event.container.create_receiver(self.conn2, self.dest)

    def on_sendable(self, event):
        if self.msg_not_sent:
            msg = Message(body={'number': 0})
            event.sender.send(msg)
            self.msg_not_sent = False

    def on_message(self, event):
        if 0 == event.message.body['number']:
            if event.message.annotations is None:
                self.error = None
        self.timer.cancel()
        self.conn1.close()
        self.conn2.close()

    def run(self):
        Container(self).run()


class MessageAnnotationsStripBothAddIngressTrace(MessagingHandler):
    def __init__(self, address1, address2):
        super(MessageAnnotationsStripBothAddIngressTrace, self).__init__()
        self.address1 = address1
        self.address2 = address2
        self.dest = "strip_message_annotations_both_add_ingress_trace/1"
        self.error = "Message annotations not found"
        self.timer = None
        self.conn1 = None
        self.conn2 = None
        self.sender = None
        self.receiver = None
        self.sent_count = 0
        self.msg_not_sent = True

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.conn1 = event.container.connect(self.address1)
        self.sender = event.container.create_sender(self.conn1, self.dest)
        self.conn2 = event.container.connect(self.address2)
        self.receiver = event.container.create_receiver(self.conn2, self.dest)

    def on_sendable(self, event):
        if self.msg_not_sent:
            msg = Message(body={'number': 0})
            ingress_message_annotations = {'work': 'hard',
                                           'x-opt-qd': 'humble',
                                           'x-opt-qd.ingress': 'ingress-router',
                                           'x-opt-qd.trace': ['0/QDR.A']}
            msg.annotations = ingress_message_annotations
            event.sender.send(msg)
            self.msg_not_sent = False

    def on_message(self, event):
        if 0 == event.message.body['number']:
            if event.message.annotations == {'work': 'hard', 'x-opt-qd': 'humble'}:
                self.error = None
        self.timer.cancel()
        self.conn1.close()
        self.conn2.close()

    def run(self):
        Container(self).run()


class MessageAnnotationsStripAddTraceTest(MessagingHandler):
    def __init__(self, address1, address2):
        super(MessageAnnotationsStripAddTraceTest, self).__init__()
        self.address1 = address1
        self.address2 = address2
        self.dest = "message_annotations_strip_no/1"
        self.error = "Message annotations not found"
        self.timer = None
        self.conn1 = None
        self.conn2 = None
        self.sender = None
        self.receiver = None
        self.sent_count = 0
        self.msg_not_sent = True

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.conn1 = event.container.connect(self.address1)
        self.sender = event.container.create_sender(self.conn1, self.dest)
        self.conn2 = event.container.connect(self.address2)
        self.receiver = event.container.create_receiver(self.conn2, self.dest)

    def on_sendable(self, event):
        if self.msg_not_sent:
            msg = Message(body={'number': 0})
            ingress_message_annotations = {'x-opt-qd.trace': ['0/QDR.1']}
            msg.annotations = ingress_message_annotations
            event.sender.send(msg)
            self.msg_not_sent = False

    def on_message(self, event):
        if 0 == event.message.body['number']:
            ma = event.message.annotations
            if ma['x-opt-qd.ingress'] == '0/QDR.A' and ma['x-opt-qd.trace'] == ['0/QDR.1', '0/QDR.A', '0/QDR.B']:
                self.error = None
        self.accept(event.delivery)
        self.timer.cancel()
        self.conn1.close()
        self.conn2.close()

    def run(self):
        Container(self).run()



class SenderSettlesFirst(MessagingHandler):
    def __init__(self, address1, address2):
        super(SenderSettlesFirst, self).__init__(auto_accept=False)
        self.address1 = address1
        self.address2 = address2
        self.dest = "closest.senderfirst.1"
        self.error = "Message body received differs from the one sent"
        self.n_sent = 0
        self.timer = None
        self.conn1 = None
        self.conn2 = None
        self.sender = None
        self.sent_count = 0
        self.received_count = 0
        self.receiver = None
        self.msg_not_sent = True

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.conn1 = event.container.connect(self.address1)
        self.sender = event.container.create_sender(self.conn1, self.dest)
        self.conn2 = event.container.connect(self.address2)
        self.receiver = event.container.create_receiver(self.conn2, self.dest)

    def on_sendable(self, event):
        if self.msg_not_sent:
            msg = Message(body={'number': 0})
            dlv = event.sender.send(msg)
            dlv.settle()
            self.msg_not_sent = False

    def on_message(self, event):
        if 0 == event.message.body['number']:
            self.error = None
        self.accept(event.delivery)
        self.timer.cancel()
        self.conn1.close()
        self.conn2.close()

    def run(self):
        Container(self).run()


class MulticastUnsettled(MessagingHandler):
    def __init__(self, address):
        super(MulticastUnsettled, self).__init__()
        self.address = address
        self.dest = "multicast.2"
        self.error = None
        self.n_sent = 0
        self.count = 3
        self.n_received_a = 0
        self.n_received_b = 0
        self.n_received_c = 0
        self.timer = None
        self.conn = None
        self.sender = None
        self.receiver_a = None
        self.receiver_b = None
        self.receiver_c = None

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.conn = event.container.connect(self.address)
        self.sender = event.container.create_sender(self.conn, self.dest)
        self.receiver_a = event.container.create_receiver(self.conn, self.dest, name="A")
        self.receiver_b = event.container.create_receiver(self.conn, self.dest, name="B")
        self.receiver_c = event.container.create_receiver(self.conn, self.dest, name="C")

    def timeout(self):
        self.error = "Timeout Expired: sent=%d rcvd=%d/%d/%d" % \
                     (self.n_sent, self.n_received_a, self.n_received_b, self.n_received_c)
        self.conn.close()

    def check_if_done(self):
        if self.n_received_a + self.n_received_b + self.n_received_c == self.count:
            self.timer.cancel()
            self.conn.close()

    def on_sendable(self, event):
        if self.n_sent == 0:
            msg = Message(body="MulticastUnsettled-Test")
            self.sender.send(msg)
            self.n_sent += 1

    def on_message(self, event):
        if event.receiver == self.receiver_a:
            self.n_received_a += 1
        if event.receiver == self.receiver_b:
            self.n_received_b += 1
        if event.receiver == self.receiver_c:
            self.n_received_c += 1

    def on_accepted(self, event):
        self.check_if_done()

    def run(self):
        Container(self).run()


class SemanticsClosestIsLocal(MessagingHandler):
    def __init__(self, address1, address2):
        super(SemanticsClosestIsLocal, self).__init__()
        self.address1 = address1
        self.address2 = address2
        self.dest = "closest.1"
        self.timer = None
        self.conn1 = None
        self.conn2 = None
        self.sender = None
        self.receiver_a = None
        self.receiver_b = None
        self.receiver_c = None
        self.num_messages = 100
        self.n_received_a = 0
        self.n_received_b = 0
        self.n_received_c = 0
        self.error = None
        self.n_sent = 0

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.conn1 = event.container.connect(self.address1)
        self.conn2 = event.container.connect(self.address2)
        self.sender = event.container.create_sender(self.conn1, self.dest)
        # Receiver on same router as the sender must receive all the messages. The other two
        # receivers are on the other router
        self.receiver_a = event.container.create_receiver(self.conn1, self.dest, name="A")
        self.receiver_b = event.container.create_receiver(self.conn2, self.dest, name="B")
        self.receiver_c = event.container.create_receiver(self.conn2, self.dest, name="C")

    def timeout(self):
        self.error = "Timeout Expired: sent=%d rcvd=%d/%d/%d" % \
                     (self.n_sent, self.n_received_a, self.n_received_b, self.n_received_c)
        self.conn1.close()
        self.conn2.close()

    def check_if_done(self):
        if self.n_received_a == 100 and self.n_received_b + self.n_received_c == 0:
            self.timer.cancel()
            self.conn1.close()
            self.conn2.close()

    def on_sendable(self, event):
        if self.n_sent < self.num_messages:
            msg = Message(body="SemanticsClosestIsLocal-Test")
            self.sender.send(msg)
            self.n_sent += 1

    def on_message(self, event):
        if event.receiver == self.receiver_a:
            self.n_received_a += 1
        if event.receiver == self.receiver_b:
            self.n_received_b += 1
        if event.receiver == self.receiver_c:
            self.n_received_c += 1

    def on_accepted(self, event):
        self.check_if_done()

    def run(self):
        Container(self).run()


class SemanticsClosestIsRemote(MessagingHandler):
    def __init__(self, address1, address2):
        super(SemanticsClosestIsRemote, self).__init__()
        self.address1 = address1
        self.address2 = address2
        self.dest = "closest.1"
        self.timer = None
        self.conn1 = None
        self.conn2 = None
        self.sender = None
        self.receiver_a = None
        self.receiver_b = None
        self.receiver_c = None
        self.num_messages = 100
        self.n_received_a = 0
        self.n_received_b = 0
        self.error = None
        self.n_sent = 0

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.conn1 = event.container.connect(self.address1)
        self.conn2 = event.container.connect(self.address2)
        self.sender = event.container.create_sender(self.conn1, self.dest)
        # Receiver on same router as the sender must receive all the messages. The other two
        # receivers are on the other router
        self.receiver_a = event.container.create_receiver(self.conn2, self.dest, name="A")
        self.receiver_b = event.container.create_receiver(self.conn2, self.dest, name="B")

    def timeout(self):
        self.error = "Timeout Expired: sent=%d rcvd=%d/%d" % \
                     (self.n_sent, self.n_received_a, self.n_received_b)
        self.conn1.close()
        self.conn2.close()

    def check_if_done(self):
        if self.n_received_a + self.n_received_b == 100 and self.n_received_a > 0 and self.n_received_b > 0:
            self.timer.cancel()
            self.conn1.close()
            self.conn2.close()

    def on_sendable(self, event):
        if self.n_sent < self.num_messages:
            msg = Message(body="SemanticsClosestIsRemote-Test")
            self.sender.send(msg)
            self.n_sent += 1

    def on_message(self, event):
        if event.receiver == self.receiver_a:
            self.n_received_a += 1
        if event.receiver == self.receiver_b:
            self.n_received_b += 1

    def on_accepted(self, event):
        self.check_if_done()

    def run(self):
        Container(self).run()


class CustomTimeout(object):
    def __init__(self, parent):
        self.parent = parent

    def addr_text(self, addr):
        if not addr:
            return ""
        if addr[0] == 'M':
            return addr[2:]
        else:
            return addr[1:]

    def on_timer_task(self, event):
        local_node = Node.connect(self.parent.address1, timeout=TIMEOUT)

        res = local_node.query('org.apache.qpid.dispatch.router.address')
        name = res.attribute_names.index('name')
        found = False
        for results in res.results:
            if "balanced.1" == self.addr_text(results[name]):
                found = True
                break

        if found:
            self.parent.cancel_custom()
            self.parent.create_sender(event)

        else:
            event.reactor.schedule(2, self)


class SemanticsBalanced(MessagingHandler):
    def __init__(self, address1, address2, address3):
        super(SemanticsBalanced, self).__init__(auto_accept=False, prefetch=0)
        self.address1 = address1
        self.address2 = address2
        self.address3 = address3
        self.dest = "balanced.1"
        self.timer = None
        self.conn1 = None
        self.conn2 = None
        self.conn3 = None
        self.sender = None
        self.receiver_a = None
        self.receiver_b = None
        self.receiver_c = None
        self.num_messages = 400
        self.n_received_a = 0
        self.n_received_b = 0
        self.n_received_c = 0
        self.error = None
        self.n_sent = 0
        self.rx_set = []
        self.custom_timer = None

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.custom_timer = event.reactor.schedule(2, CustomTimeout(self))
        self.conn1 = event.container.connect(self.address1)
        self.conn2 = event.container.connect(self.address2)
        self.conn3 = event.container.connect(self.address3)

        # This receiver is on the same router as the sender
        self.receiver_a = event.container.create_receiver(self.conn2, self.dest, name="A")

        # These two receivers are connected to a different router than the sender
        self.receiver_b = event.container.create_receiver(self.conn3, self.dest, name="B")
        self.receiver_c = event.container.create_receiver(self.conn3, self.dest, name="C")

        self.receiver_a.flow(300)
        self.receiver_b.flow(300)
        self.receiver_c.flow(300)

    def cancel_custom(self):
        self.custom_timer.cancel()

    def create_sender(self, event):
        self.sender = event.container.create_sender(self.conn1, self.dest)

    def timeout(self):
        self.error = "Timeout Expired: sent=%d rcvd=%d/%d/%d" % \
                     (self.n_sent, self.n_received_a, self.n_received_b, self.n_received_c)
        self.conn1.close()
        self.conn2.close()
        self.conn3.close()

    def check_if_done(self):
        if self.n_received_a + self.n_received_b + self.n_received_c == self.num_messages and \
                self.n_received_a > 0 and self.n_received_b > 0 and self.n_received_c > 0:
            self.rx_set.sort()
            all_messages_received = True
            for i in range(self.num_messages):
                if not i == self.rx_set[i]:
                    all_messages_received = False

            if all_messages_received:
                self.timer.cancel()
                self.conn1.close()
                self.conn2.close()
                self.conn3.close()

    def on_sendable(self, event):
        if self.n_sent < self.num_messages:
            msg = Message(body={'number': self.n_sent})
            self.sender.send(msg)
            self.n_sent += 1

    def on_message(self, event):
        if event.receiver ==    self.receiver_a:
            self.n_received_a += 1
            self.rx_set.append(event.message.body['number'])
        elif event.receiver == self.receiver_b:
            self.n_received_b += 1
            self.rx_set.append(event.message.body['number'])
        elif event.receiver == self.receiver_c:
            self.n_received_c += 1
            self.rx_set.append(event.message.body['number'])

        self.check_if_done()

    def run(self):
        Container(self).run()


class PropagatedDisposition(MessagingHandler):
    def __init__(self, test, address1, address2):
        super(PropagatedDisposition, self).__init__(auto_accept=False)
        self.address1 = address1
        self.address2 = address2
        self.settled = []
        self.test = test
        self.sender_conn = None
        self.receiver_conn = None
        self.passed = False

    def on_start(self, event):
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))
        self.sender_conn = event.container.connect(self.address1)
        self.receiver_conn = event.container.connect(self.address2)
        addr = "unsettled/2"
        self.sender = event.container.create_sender(self.sender_conn, addr)
        self.receiver = event.container.create_receiver(self.receiver_conn, addr)
        self.receiver.flow(2)
        self.trackers = {}
        for b in ['accept', 'reject']:
            self.trackers[self.sender.send(Message(body=b))] = b

    def timeout(self):
        unique_list = sorted(list(dict.fromkeys(self.settled)))
        self.error = "Timeout Expired: Expected ['accept', 'reject'] got %s" % unique_list
        self.sender_conn.close()
        self.receiver_conn.close()

    def check(self):
        unique_list = sorted(list(dict.fromkeys(self.settled)))
        if unique_list == [u'accept', u'reject']:
            self.passed = True
            self.sender_conn.close()
            self.receiver_conn.close()
            self.timer.cancel()

    def on_message(self, event):
        if event.message.body == u'accept':
            event.delivery.update(Delivery.ACCEPTED)
            event.delivery.settle()
        elif event.message.body == u'reject':
            event.delivery.update(Delivery.REJECTED)
            event.delivery.settle()

    def on_accepted(self, event):
        self.test.assertEqual(Delivery.ACCEPTED, event.delivery.remote_state)
        self.test.assertEqual('accept', self.trackers[event.delivery])
        self.settled.append('accept')
        self.check()

    def on_rejected(self, event):
        self.test.assertEqual(Delivery.REJECTED, event.delivery.remote_state)
        self.test.assertEqual('reject', self.trackers[event.delivery])
        self.settled.append('reject')
        self.check()

    def run(self):
        Container(self).run()


class ThreeAck(MessagingHandler):
    def __init__(self, test, address1, address2):
        super(ThreeAck, self).__init__(auto_accept=False, auto_settle=False)
        self.addrs = [address1, address2]
        self.settled = []
        self.test = test
        self.phase = 0

    def on_start(self, event):
        connections = [event.container.connect(a) for a in self.addrs]
        addr = "three_ack/1"
        self.sender = event.container.create_sender(connections[0], addr)
        self.receiver = event.container.create_receiver(connections[1], addr)
        self.receiver.flow(1)
        self.tracker = self.sender.send(Message('hello'))

    def on_message(self, event):
        self.test.assertEqual(0, self.phase)
        self.phase = 1
        self.test.assertFalse(event.delivery.settled)
        self.test.assertEqual(0, self.tracker.local_state)
        self.test.assertEqual(0, self.tracker.remote_state)
        event.delivery.update(Delivery.ACCEPTED)
        # NOTE: we don't settle yet for 3-ack

    def on_accepted(self, event):
        self.test.assertTrue(event.sender)
        self.test.assertEqual(1, self.phase)
        self.phase = 2
        self.test.assertEqual(Delivery.ACCEPTED, event.delivery.remote_state)
        self.test.assertFalse(event.delivery.settled)
        self.test.assertEqual(0, event.delivery.local_state)
        event.delivery.settle()
        self.test.assertFalse(event.delivery.settled)
        event.connection.close()

    def on_settled(self, event):
        self.test.assertTrue(event.receiver)
        self.test.assertEqual(2, self.phase)
        self.phase = 3
        event.connection.close()

    def run(self):
        Container(self).run()
        self.test.assertEqual(3, self.phase)


class TwoRouterConnection(TestCase):
    def __init__(self, test_method):
        TestCase.__init__(self, test_method)
        self.success = False
        self.timer_delay = 4
        self.max_attempts = 2
        self.attempts = 0
        self.local_node = None

    @classmethod
    def router(cls, name, config):
        config = Qdrouterd.Config(config)

        cls.routers.append(cls.tester.qdrouterd(name, config, wait=True))

    @classmethod
    def setUpClass(cls):
        super(TwoRouterConnection, cls).setUpClass()

        cls.routers = []

        cls.B_normal_port_1 = cls.tester.get_port()
        cls.B_normal_port_2 = cls.tester.get_port()

        TwoRouterConnection.router('A', [
                        ('router', {'mode': 'interior', 'id': 'A'}),
                        ('listener', {'host': '0.0.0.0', 'role': 'normal',
                                      'port': cls.tester.get_port()}),
                        ]
              )

        TwoRouterConnection.router('B',
                    [
                        ('router', {'mode': 'interior', 'id': 'B'}),
                        ('listener', {'host': '0.0.0.0', 'role': 'normal',
                                      'port': cls.B_normal_port_1}),
                        ('listener', {'host': '0.0.0.0', 'role': 'normal',
                                      'port': cls.B_normal_port_2}),

                    ]
               )

    def address(self):
        return self.routers[0].addresses[0]

    def run_qdmanage(self, cmd, input=None, expect=Process.EXIT_OK, address=None):
        p = self.popen(
            ['qdmanage'] + cmd.split(' ') + ['--bus', address or self.address(), '--indent=-1', '--timeout', str(TIMEOUT)],
            stdin=PIPE, stdout=PIPE, stderr=STDOUT, expect=expect,
            universal_newlines=True)
        out = p.communicate(input)[0]
        try:
            p.teardown()
        except Exception as e:
            raise Exception(out if out else str(e))
        return out

    def can_terminate(self):
        if self.attempts == self.max_attempts:
            return True

        if self.success:
            return True

        return False

    def check_connections(self):
        res = self.local_node.query(type='org.apache.qpid.dispatch.connection')
        results = res.results

        # If DISPATCH-1093 was not fixed, there would be an additional
        # connection created and hence the len(results) would be 4

        # Since DISPATCH-1093 is fixed, len(results would be 3 which is what
        # we would expect.

        if len(results) != 3:
            self.schedule_num_connections_test()
        else:
            self.success = True

    def schedule_num_connections_test(self):
        if self.attempts < self.max_attempts:
            if not self.success:
                Timer(self.timer_delay, self.check_connections).start()
                self.attempts += 1

    def test_create_connectors(self):
        self.local_node = Node.connect(self.routers[0].addresses[0],
                                       timeout=TIMEOUT)

        res = self.local_node.query(type='org.apache.qpid.dispatch.connection')
        results = res.results

        self.assertEqual(1, len(results))

        long_type = 'org.apache.qpid.dispatch.connector' ''

        create_command = 'CREATE --type=' + long_type + ' --name=foo' + ' host=0.0.0.0 port=' + str(TwoRouterConnection.B_normal_port_1)

        self.run_qdmanage(create_command)

        create_command = 'CREATE --type=' + long_type + ' --name=bar' + ' host=0.0.0.0 port=' + str(TwoRouterConnection.B_normal_port_2)

        self.run_qdmanage(create_command)

        self.schedule_num_connections_test()

        while not self.can_terminate():
            pass

        self.assertTrue(self.success)

class PropagationTest(TestCase):

    inter_router_port = None

    @classmethod
    def setUpClass(cls):
        """Start a router and a messenger"""
        super(PropagationTest, cls).setUpClass()

        def router(name, extra_config):

            config = [
                ('router', {'mode': 'interior', 'id': 'QDR.%s'%name}),

                ('listener', {'port': cls.tester.get_port()}),

            ] + extra_config

            config = Qdrouterd.Config(config)

            cls.routers.append(cls.tester.qdrouterd(name, config, wait=True))

        cls.routers = []

        inter_router_port = cls.tester.get_port()
        router('A', [('listener', {'role': 'inter-router', 'port': inter_router_port}), ('address', {'prefix': 'multicast', 'distribution': 'multicast'})])
        router('B', [('connector', {'role': 'inter-router', 'port': inter_router_port})])

        cls.routers[0].wait_router_connected('QDR.B')
        cls.routers[1].wait_router_connected('QDR.A')

    def test_propagation_of_locally_undefined_address(self):
        test = MulticastTestClient(self.routers[0].addresses[0], self.routers[1].addresses[0])
        test.run()
        self.assertEqual(None, test.error)
        self.assertEqual(test.received, 2)

class CreateReceiver(MessagingHandler):
    def __init__(self, connection, address):
        super(CreateReceiver, self).__init__()
        self.connection = connection
        self.address = address

    def on_timer_task(self, event):
        event.container.create_receiver(self.connection, self.address)

class DelayedSend(MessagingHandler):
    def __init__(self, connection, address, message):
        super(DelayedSend, self).__init__()
        self.connection = connection
        self.address = address
        self.message = message

    def on_timer_task(self, event):
        event.container.create_sender(self.connection, self.address).send(self.message)

class MulticastTestClient(MessagingHandler):
    def __init__(self, router1, router2):
        super(MulticastTestClient, self).__init__()
        self.routers = [router1, router2]
        self.received = 0
        self.error = None

    def on_start(self, event):
        self.connections = [event.container.connect(r) for r in self.routers]
        event.container.create_receiver(self.connections[0], "multicast")
        # wait for knowledge of receiver1 to propagate to second router
        event.container.schedule(5, CreateReceiver(self.connections[1], "multicast"))
        event.container.schedule(7, DelayedSend(self.connections[1], "multicast", Message(body="testing1,2,3")))
        self.timer = event.reactor.schedule(TIMEOUT, Timeout(self))

    def on_message(self, event):
        self.received += 1
        event.connection.close()
        if self.received == 2:
            self.timer.cancel()

    def timeout(self):
        self.error = "Timeout Expired:received=%d" % self.received
        for c in self.connections:
            c.close()

    def run(self):
        Container(self).run()

if __name__ == '__main__':
    unittest.main(main_module())

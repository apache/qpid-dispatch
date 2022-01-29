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


from proton import Message
from proton.handlers import MessagingHandler
from proton.reactor import Container

from qpid_dispatch_internal.compat import UNICODE

from system_test import TestCase, Qdrouterd, main_module, unittest

# ------------------------------------------------
# Helper classes for all tests.
# ------------------------------------------------


class Timeout:
    """
    Named timeout object can handle multiple simultaneous
    timers, by telling the parent which one fired.
    """

    def __init__(self, parent, name):
        self.parent = parent
        self.name   = name

    def on_timer_task(self, event):
        self.parent.timeout(self.name)


class ManagementMessageHelper:
    """
    Format management messages.
    """

    def __init__(self, reply_addr):
        self.reply_addr = reply_addr

    def make_router_link_query(self) :
        props = {'count':      '100',
                 'operation':  'QUERY',
                 'entityType': 'org.apache.qpid.dispatch.router.link',
                 'name':       'self',
                 'type':       'org.amqp.management'
                 }
        attrs = []
        attrs.append(UNICODE('linkType'))
        attrs.append(UNICODE('linkDir'))
        attrs.append(UNICODE('deliveryCount'))
        attrs.append(UNICODE('priority'))

        msg_body = {}
        msg_body['attributeNames'] = attrs
        return Message(body=msg_body, properties=props, reply_to=self.reply_addr)


# ================================================================
#     Setup
# ================================================================

class PriorityTests (TestCase):

    @classmethod
    def setUpClass(cls):
        super(PriorityTests, cls).setUpClass()

        def router(name, more_config):

            config = [('router',  {'mode': 'interior', 'id': name, 'workerThreads': 4}),
                      ('address', {'prefix': 'closest',   'distribution': 'closest'}),
                      ('address', {'prefix': 'balanced',  'distribution': 'balanced'}),
                      ('address', {'prefix': 'multicast', 'distribution': 'multicast'})
                      ]    \
                + more_config

            config = Qdrouterd.Config(config)

            cls.routers.append(cls.tester.qdrouterd(name, config, wait=True))

        cls.routers = []

        # The sender will send all its messages with magic_message_priority.
        # The first router will set target addr priority to magic_address_priority.
        # It is important *not* to choose 4 for either of these priorities,
        # since that is the default message priority.
        cls.magic_message_priority = 3
        cls.magic_address_priority = 7

        link_cap = 100
        A_client_port = cls.tester.get_port()
        B_client_port = cls.tester.get_port()
        C_client_port = cls.tester.get_port()

        A_inter_router_port = cls.tester.get_port()
        B_inter_router_port = cls.tester.get_port()
        C_inter_router_port = cls.tester.get_port()

        A_config = [
            ('listener',
             {'port'             : A_client_port,
              'role'             : 'normal',
              'linkCapacity'     : link_cap,
              'stripAnnotations' : 'no'
              }
             ),
            ('listener',
             {'role'             : 'inter-router',
              'port'             : A_inter_router_port,
              'linkCapacity'     : link_cap,
              'stripAnnotations' : 'no'
              }
             ),
            ('address',
             {'prefix'       : 'speedy',
              'priority'     : cls.magic_address_priority,
              'distribution' : 'closest'
              }
             ),
        ]

        cls.B_config = [
            ('listener',
             {'port'             : B_client_port,
              'role'             : 'normal',
              'linkCapacity'     : link_cap,
              'stripAnnotations' : 'no'
              }
             ),
            ('listener',
             {'role'             : 'inter-router',
              'port'             : B_inter_router_port,
              'linkCapacity'     : link_cap,
              'stripAnnotations' : 'no'
              }
             ),
            ('connector',
             {'name'             : 'BA_connector',
              'role'             : 'inter-router',
              'port'             : A_inter_router_port,
              'linkCapacity'     : link_cap,
              'stripAnnotations' : 'no'
              }
             )
        ]

        C_config = [
            ('listener',
             {'port'             : C_client_port,
              'role'             : 'normal',
              'linkCapacity'     : link_cap,
              'stripAnnotations' : 'no'
              }
             ),
            ('listener',
             {'role'             : 'inter-router',
              'port'             : C_inter_router_port,
              'linkCapacity'     : link_cap,
              'stripAnnotations' : 'no'
              }
             ),
            ('connector',
             {'name'             : 'CB_connector',
              'role'             : 'inter-router',
              'port'             : B_inter_router_port,
              'linkCapacity'     : link_cap,
              'stripAnnotations' : 'no'
              }
             )
        ]

        router('A', A_config)
        router('B', cls.B_config)
        router('C', C_config)

        router_A = cls.routers[0]
        router_B = cls.routers[1]
        router_C = cls.routers[2]

        router_A.wait_router_connected('B')
        router_A.wait_router_connected('C')

        cls.client_addrs = (router_A.addresses[0],
                            router_B.addresses[0],
                            router_C.addresses[0]
                            )

    def test_priority(self):
        name = 'test_01'
        test = Priority(self,
                        name,
                        self.client_addrs,
                        "speedy/01",
                        self.magic_message_priority,
                        self.magic_address_priority
                        )
        test.run()
        self.assertIsNone(test.error)


# ================================================================
#     Tests
# ================================================================


class Priority (MessagingHandler):

    # In this test we will have a linear network of 3 routers.
    # The sender attaches at A, and the receiver at C.
    #
    #  receiver <--- C <--- B <--- A <--- sender
    #
    # Priority -- whether message or address -- only operates
    # on inter-router links. The links from A to B will show
    # address-priority overriding message-priority. When a
    # router does not set any message priority, then messages
    # are routed acording to their intrinsic priority which
    # was assigned by the sender. This will be shown by the
    # connection from router B to C.
    #
    # The address that the clients use has a prefix of 'speedy'.
    # Router A will assign a priority of magic_addr_priority to all
    # 'speedy' addresses.
    # No other routers will assign any address priorities.
    #
    # The sending client will assign a priority of magic_msg_priority
    # to all the messages it sends.
    #
    # So what should happen is:
    #
    # 1. at router A, all the 'speedy' messages go out with
    #    magic_addr_priority, because addr priority takes precedence.
    #
    # 2. at router B, they all go out with magic_msg_priority,
    #    because that router has not assigned any addr priority,
    #    so the intrinsic message priorities are used.
    #
    # 3. Nothing special happens at router C, because it is sending
    #    the messages out over a connection to an endpoint, which
    #    is not an inter-router connection.
    #
    # In this test we will send a known number of messages and
    # then send management queries to A and B to learn at what
    # priorities the messages actually travelled.

    def __init__(self, parent, test_name, client_addrs, destination, magic_msg_priority, magic_addr_priority):
        super(Priority, self).__init__(prefetch=10)

        self.parent          = parent
        self.client_addrs    = client_addrs
        self.dest            = destination

        self.magic_msg_priority  = magic_msg_priority
        self.magic_addr_priority = magic_addr_priority

        self.error           = None
        self.sender          = None
        self.receiver        = None
        self.send_timer      = None
        self.n_messages      = 100
        self.n_sent          = 0
        self.send_conn       = None
        self.recv_conn       = None
        self.n_received      = 0
        self.reactor         = None
        self.timer_count     = 0
        self.sent_queries    = False
        self.finishing       = False
        self.goals           = 0
        self.n_goals         = 2
        self.connections     = list()
        self.A_addr          = self.client_addrs[0]
        self.B_addr          = self.client_addrs[1]
        self.C_addr          = self.client_addrs[2]
        self.routers = {
            'A' : dict(),
            'B' : dict()
        }

    # Shut down everything and exit.
    def bail(self, text):
        self.send_timer.cancel()
        self.finishing = True
        self.error = text
        for conn in self.connections :
            conn.close()

    def make_connection(self, event, addr) :
        cnx = event.container.connect(addr)
        self.connections.append(cnx)
        return cnx

    def on_start(self, event):
        self.reactor = event.reactor
        self.send_conn  = self.make_connection(event, self.A_addr)
        self.recv_conn  = self.make_connection(event, self.C_addr)

        self.sender     = event.container.create_sender(self.send_conn, self.dest)
        self.receiver   = event.container.create_receiver(self.recv_conn, self.dest)
        self.receiver.flow(100)

        self.routers['A']['mgmt_conn']     = self.make_connection(event, self.A_addr)
        self.routers['A']['mgmt_receiver'] = event.container.create_receiver(self.routers['A']['mgmt_conn'], dynamic=True)
        self.routers['A']['mgmt_sender']   = event.container.create_sender(self.routers['A']['mgmt_conn'], "$management")

        self.routers['B']['mgmt_conn']     = self.make_connection(event, self.B_addr)
        self.routers['B']['mgmt_receiver'] = event.container.create_receiver(self.routers['B']['mgmt_conn'], dynamic=True)
        self.routers['B']['mgmt_sender']   = event.container.create_sender(self.routers['B']['mgmt_conn'], "$management")

        self.send_timer = event.reactor.schedule(2, Timeout(self, "send"))

    def timeout(self, name):
        if name == 'send':
            self.send()
            if not self.sent_queries :
                self.test_timer = self.reactor.schedule(1, Timeout(self, "send"))

    def on_link_opened(self, event) :
        # A mgmt link has opened. Create its management helper.
        # ( Now we know the address that the management helper should use as
        # the "reply-to" in its management message. )
        if event.receiver == self.routers['A']['mgmt_receiver'] :
            event.receiver.flow(1000)
            self.routers['A']['mgmt_helper'] = ManagementMessageHelper(event.receiver.remote_source.address)

        elif event.receiver == self.routers['B']['mgmt_receiver'] :
            event.receiver.flow(1000)
            self.routers['B']['mgmt_helper'] = ManagementMessageHelper(event.receiver.remote_source.address)

    def send(self) :
        if self.sender.credit <= 0:
            self.receiver.flow(100)
            return

        # First send the payload messages.
        if self.n_sent < self.n_messages :
            for i in range(50) :
                msg = Message(body=self.n_sent)
                msg.priority = 3
                self.sender.send(msg)
                self.n_sent += 1
        # Then send the management queries.
        # But only send them once.
        elif not self.sent_queries  :
            # Query router A.
            mgmt_helper = self.routers['A']['mgmt_helper']
            mgmt_sender = self.routers['A']['mgmt_sender']
            msg = mgmt_helper.make_router_link_query()
            mgmt_sender.send(msg)

            # Query router B.
            mgmt_helper = self.routers['B']['mgmt_helper']
            mgmt_sender = self.routers['B']['mgmt_sender']
            msg = mgmt_helper.make_router_link_query()
            mgmt_sender.send(msg)

            self.sent_queries = True

    # This test has two goals: get the response from router A
    # and from router B. As they come in, we check them. If
    # the response is unsatisfactory we bail out
    def goal_satisfied(self) :
        self.goals += 1
        if self.goals >= self.n_goals :
            self.bail(None)

    def on_message(self, event) :

        # Don't take any more messages if 'bail' has been called.
        if self.finishing :
            return

        msg = event.message

        if event.receiver == self.routers['A']['mgmt_receiver'] :
            # Router A has only one set of outgoing links, and it
            # has set a priority for our target address. We should
            # see all the messages we sent go out with that priority.
            magic = self.magic_addr_priority
            if 'results' in msg.body :
                results = msg.body['results']
                # I do not want to trust the possibility that the
                # results will be returned to me in priority-order.
                # Instead, I explicitly asked for the link priority
                # in the management query that was sent. Now I will
                # loop through all the results, and look for the one
                # with the desired priority.
                for i in range(len(results)) :
                    result = results[i]
                    role          = result[0]
                    dir           = result[1]
                    message_count = result[2]
                    priority      = result[3]
                    if role == "inter-router" and dir == "out" and priority == magic  :
                        if message_count >= self.n_messages :
                            self.goal_satisfied()
                            return
                        else :
                            self.bail("Router A priority %d had %d messages instead of %d." %
                                      (magic, message_count, self.n_messages))
                            return

        elif event.receiver == self.routers['B']['mgmt_receiver'] :
            # Router B has two sets of outgoing links, and it has not
            # set a priority for the target address. We should see all
            # of our messages going out over the message-intrinsic
            # priority that the sending client used -- one one of those
            # two sets of outgoing links.
            magic = self.magic_msg_priority
            if 'results' in msg.body :
                message_counts = list()
                results = msg.body['results']
                for i in range(len(results)) :
                    result = results[i]
                    role          = result[0]
                    dir           = result[1]
                    message_count = result[2]
                    priority      = result[3]
                    if role == "inter-router" and dir == "out" :
                        if priority == magic :
                            message_counts.append(message_count)

                if self.n_messages in message_counts :
                    self.goal_satisfied()
                else :
                    self.bail("No outgoing link on router B had %d messages at priority 3" % self.n_messages)

        else :
            # This is a payload message -- not management. Just count it.
            self.n_received += 1

    def run(self):
        Container(self).run()


if __name__ == '__main__':
    unittest.main(main_module())

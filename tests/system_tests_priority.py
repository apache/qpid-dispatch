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

import unittest2 as unittest
from proton          import Message, Timeout
from system_test     import TestCase, Qdrouterd, main_module
from proton.handlers import MessagingHandler
from proton.reactor  import Container

import time
import math

import pdb


#================================================================
# Helper classes for all tests.
#================================================================

class Timeout(object):
    """
    Named timeout object can handle multiple simultaneous
    timers, by telling the parent which one fired.
    """
    def __init__ ( self, parent, name ):
        self.parent = parent
        self.name   = name

    def on_timer_task ( self, event ):
        self.parent.timeout ( self.name )





#================================================================
#     Setup
#================================================================

class PriorityTests ( TestCase ):

    @classmethod
    def setUpClass(cls):
        super(PriorityTests, cls).setUpClass()

        def router(name, more_config):

            config = [ ('router',  {'mode': 'interior', 'id': name, 'workerThreads': 4}),
                       ('address', {'prefix': 'closest',   'distribution': 'closest'}),
                       ('address', {'prefix': 'balanced',  'distribution': 'balanced'}),
                       ('address', {'prefix': 'multicast', 'distribution': 'multicast'})
                     ]    \
                     + more_config

            config = Qdrouterd.Config(config)

            cls.routers.append(cls.tester.qdrouterd(name, config, wait=True))

        cls.routers = []

        link_cap = 100
        A_client_port = cls.tester.get_port()
        B_client_port = cls.tester.get_port()

        A_inter_router_port = cls.tester.get_port()
        B_inter_router_port = cls.tester.get_port()

        A_config = [
                     ( 'listener',
                       { 'port': A_client_port,
                         'role': 'normal',
                         'stripAnnotations': 'no',
                         'linkCapacity': link_cap
                       }
                     ),
                     ( 'listener',
                       { 'role': 'inter-router',
                         'port': A_inter_router_port,
                         'stripAnnotations': 'no',
                         'linkCapacity': link_cap
                       }
                     )
                   ]

        cls.B_config = [
                         ( 'listener',
                           { 'port': B_client_port,
                             'role': 'normal',
                             'stripAnnotations': 'no',
                             'linkCapacity': link_cap
                           }
                         ),
                         ( 'listener',
                           { 'role': 'inter-router',
                             'port': B_inter_router_port,
                             'stripAnnotations': 'no',
                             'linkCapacity': link_cap
                           }
                         ),
                         ( 'connector',
                           {  'name': 'BA_connector',
                              'role': 'inter-router',
                              'port': A_inter_router_port,
                              'verifyHostname': 'no',
                              'stripAnnotations': 'no',
                              'linkCapacity': link_cap
                           }
                         )
                      ]


        router ( 'A', A_config )
        router ( 'B', cls.B_config )


        router_A = cls.routers[0]
        router_B = cls.routers[1]

        router_A.wait_router_connected('B')

        cls.client_addrs = ( router_A.addresses[0],
                             router_B.addresses[0]
                           )

    def kill_router_B ( self ) :
        status = self.routers[1].poll()
        self.routers[1].teardown()
        status = self.routers[1].poll()
        del self.routers[1]

    
    def new_router_B ( self ) :
        name = 'B'
        config = [ ('router',  {'mode': 'interior', 'id': name}),
                   ('address', {'prefix': 'closest',   'distribution': 'closest'}),
                   ('address', {'prefix': 'balanced',  'distribution': 'balanced'}),
                   ('address', {'prefix': 'multicast', 'distribution': 'multicast'})
                 ]    \
                 + self.B_config

        config = Qdrouterd.Config(config)
        self.routers.append(self.tester.qdrouterd(name, config, wait=False))
        # We need a little pause here, or A will always show 
        # a status of None (i.e. Good) even if it is, in fact, 
        # dying. (i.e. Bad)
        time.sleep ( 2 )
        status = self.routers[0].poll()
        return status

    
    def test_priority ( self ):
        name = 'test_01'
        test = Priority ( self,
                          name,
                          self.client_addrs,
                          "closest/01"
                        )
        test.run()
        self.assertEqual ( None, test.error )



#================================================================
#     Tests
#================================================================

class Priority ( MessagingHandler ):

    def __init__ ( self, parent, test_name, client_addrs, destination ):
        super(Priority, self).__init__(prefetch=10)

        self.parent          = parent
        self.client_addrs    = client_addrs
        self.dest            = destination

        self.error           = None
        self.sender          = None
        self.receiver        = None
        self.test_timer      = None
        self.fail_timer      = None
        self.n_sent          = 0
        self.n_accepted      = 0
        self.n_released      = 0
        self.send_conn       = None
        self.recv_conn       = None
        self.n_messages      = 100
        self.n_received      = 0
        self.test_start_time = None
        self.test_end_time   = None
        self.timer_count     = 0
        self.reactor         = None
        self.router_B_count  = 0


    # Shut down everything and exit.
    def bail ( self, text ):
        self.error = text

        self.send_conn.close()
        self.recv_conn.close ( )
        self.test_timer.cancel ( )
        self.fail_timer.cancel ( )


    # There is no need in this test to send messages at all. Just the
    # process of killing and replacing router B will kill router A if
    # it is not cleaning up its sheafs of prioritized links properly.
    #
    # Each time this timer goes off we will kill router B and make a 
    # replacement for it, then make sure that A has survived the process.
    #
    # To see A die in this test, comment out the call to qdr_reset_sheaf()
    # in connections.c, rebuild, and run this test.
    def timeout ( self, name ):
        if name == 'fail':
            self.bail ( "Test hanging." )
            return
        if name == 'test':
            self.timer_count += 1
            if 0 == (self.timer_count % 3 ) :
                self.parent.kill_router_B ( )
                self.kill_B = 0
                time.sleep(2)
                # Make a new B router, and see if A survives.
                A_status = self.parent.new_router_B ( )
                if A_status != None :
                    self.bail ( "Router A died when new router B was created." )
                    return
                if self.router_B_count >= 2 :
                    # We have killed & restarted B 2 times. Good enough.
                    self.bail ( None )
                    return
                self.router_B_count += 1
            self.test_timer = self.reactor.schedule ( 1, Timeout(self, "test") )



    def on_start ( self, event ):
        self.reactor = event.reactor
        self.test_timer = event.reactor.schedule ( 1,  Timeout(self, "test") )
        self.fail_timer = event.reactor.schedule ( 30, Timeout(self, "fail") )
        self.send_conn  = event.container.connect ( self.client_addrs[0] ) # A
        self.recv_conn  = event.container.connect ( self.client_addrs[1] ) # B


    def run(self):
        Container(self).run()


if __name__ == '__main__':
    unittest.main(main_module())

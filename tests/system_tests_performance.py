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

import unittest, os, json
import subprocess
from subprocess      import PIPE, STDOUT
from proton          import Message, PENDING, ACCEPTED, REJECTED, RELEASED, SSLDomain, SSLUnavailable, Timeout
from system_test     import TestCase, Qdrouterd, main_module, DIR, TIMEOUT, Process
from proton.handlers import MessagingHandler
from proton.reactor  import Container, AtMostOnce, AtLeastOnce, DynamicNodeProperties, LinkOption, ApplicationEvent, EventInjector
from proton.utils    import BlockingConnection
from qpid_dispatch.management.client import Node

import time
import datetime
import pdb
import inspect
import numpy

import time
import math
import router_network
import json



# PROTON-828:
try:
    from proton import MODIFIED
except ImportError:
    from proton import PN_STATUS_MODIFIED as MODIFIED




#------------------------------------------------
# Helper classes for all tests.
#------------------------------------------------

class Timeout(object):
    """
    Named timeout object can handle multiple simultaneous
    timers, by telling the parent which one fired.
    """
    def __init__ ( self, parent, name ):
        self.parent = parent
        self.name   = name

    def on_timer_task ( self, event ):
        #print "MDEBUG: Timeout::on_timer_task -- ", self.name
        self.parent.timeout ( self.name )



class ManagementMessageHelper ( object ) :
    """
    Format management messages.
    """
    def __init__ ( self, reply_addr ) :
        self.reply_addr = reply_addr

    def make_connector_query ( self, connector_name ) :
        props = {'operation': 'READ', 'type': 'org.apache.qpid.dispatch.connector', 'name' : connector_name }
        msg = Message ( properties=props, reply_to=self.reply_addr )
        return msg

    def make_connector_delete_command ( self, connector_name ) :
        props = {'operation': 'DELETE', 'type': 'org.apache.qpid.dispatch.connector', 'name' : connector_name }
        msg = Message ( properties=props, reply_to=self.reply_addr )
        return msg

    def make_router_link_query ( self ) :
        props = { 'count':      '100', 
                  'operation':  'QUERY', 
                  'entityType': 'org.apache.qpid.dispatch.router.link', 
                  'name':       'self', 
                  'type':       'org.amqp.management' 
                }
        attrs = []
        attrs.append ( unicode('linkType') )
        attrs.append ( unicode('linkDir') )
        attrs.append ( unicode('linkName') )
        attrs.append ( unicode('owningAddr') )
        attrs.append ( unicode('capacity') )
        attrs.append ( unicode('undeliveredCount') )
        attrs.append ( unicode('unsettledCount') )
        attrs.append ( unicode('acceptedCount') )
        attrs.append ( unicode('rejectedCount') )
        attrs.append ( unicode('releasedCount') )
        attrs.append ( unicode('modifiedCount') )

        msg_body = { }
        msg_body [ 'attributeNames' ] = attrs
        return Message ( body=msg_body, properties=props, reply_to=self.reply_addr )


#------------------------------------------------
# END Helper classes for all tests.
#------------------------------------------------





#================================================================
#     Setup
#================================================================

class PerfTests ( TestCase ):

    @classmethod
    def setUpClass(cls):
        super(PerfTests, cls).setUpClass()

        # 1 means skip that test.
        cls.skip = { 'test_01_latency_01' : 0,
                   }


    def test_01_latency ( self ):
        name = 'test_01_latency_01'
        if self.skip [ name ] :
            self.skipTest ( "Test skipped during development." )

        test = Latency ( name, 'closest/latency_01' )
        test.run()
        self.assertEqual ( None, test.error )





#================================================================
#     Tests
#================================================================

class Latency ( MessagingHandler ):
    def __init__ ( self, test_name, addr ):
        super ( Latency, self).__init__(prefetch=100)
        self.test_name = test_name
        self.addr      = addr

        self.n_messages   = 2000
        self.body_size    = 100
        self.network      = None
        self.error        = None
        self.test_timer   = None
        self.send_timer   = None
        self.deadline     = 60
        self.router_path  = os.getenv ( 'DISPATCH_PERF_ROUTER_PATH' )
        self.client_path  = os.getenv ( 'DISPATCH_PERF_CLIENT_PATH' )
        self.results_root = os.getenv ( 'DISPATCH_PERF_RESULTS_ROOT', default='/tmp' )

        now = datetime.datetime.now()
        self.timestamp = "%d_%02d_%02d_%02d_%02d" % (now.year, now.month, now.day, now.hour, now.minute)
        self.results_path = self.results_root + '/' + self.timestamp

        self.debug = False

        self.test_description = dict()
        self.test_description [ 'test' ]      = 'latency'
        self.test_description [ 'client' ]    = 'proton-c'
        self.test_description [ 'timestamp' ] = self.timestamp
        self.test_description [ 'n-messages'] = self.n_messages


    def debug_print ( self, text ) :
        if self.debug == True:
            print "%.6lf %s" % ( time.time(), text )


    # Shut down everything and exit.
    def bail ( self, text ):
        self.test_timer.cancel ( )
        self.error = text


    def timeout ( self, name ):
        if name == 'test':
            self.bail ( "Test is hanging -- timeout expired." )


    def on_start ( self, event ):
        self.test_timer = event.reactor.schedule ( self.deadline, Timeout(self, "test") )

        if self.client_path == None :
            self.bail ( 'need env var DISPATCH_PERF_CLIENT_PATH' )
            return

        if self.router_path == None :
            self.bail ( 'need env var DISPATCH_PERF_ROUTER_PATH' )
            return

        if os.path.exists ( self.results_path ) :
            self.bail ( 'Results path already exists: |%s|' % self.results_path )
            return
        os.mkdir( self.results_path, 0755 )

        #====================================================
        # Configure the network.
        #====================================================
        self.network = router_network.RouterNetwork ( n_nodes     = 3,
                                                      router_path = self.router_path,
                                                      test_path   = './test',
                                                      client_path = self.client_path
                                                    )
        self.network.add_client_listener ( 'A' )
        self.network.add_client_listener ( 'B' )
        self.network.add_client_listener ( 'C' )
        self.network.connect ( 'A', 'B' )
        self.network.connect ( 'B', 'C' )

        #====================================================
        # Add the clients.
        #====================================================
        router_name = 'A'
        client_name = 'receiver'
        action      = 'receive'

        self.network.add_client ( router_name = router_name,
                                  action      = action,
                                  name        = client_name,
                                  addr        = self.addr,
                                  n_messages  = self.n_messages,
                                  body_size   = self.body_size
                                )

        router_name = 'C'
        client_name = 'sender'
        action      = 'send'

        self.network.add_client ( router_name = router_name,
                                  action      = action,
                                  name        = client_name,
                                  addr        = self.addr,
                                  n_messages  = self.n_messages,
                                  body_size   = self.body_size
                                )

        #====================================================
        # Run everything.
        #====================================================
        self.network.run ( )

        sleeps = 0
        output = None

        while True :
            self.debug_print ( "main program running" )
            time.sleep ( 3 )
            sleeps += 1

            still_running = self.network.check_clients ( )
            self.debug_print ( "still running: %s" % still_running )

            if len ( still_running ) < 2 :
                self.debug_print ( "halting network" )
                self.network.halt ( )
                output = self.network.get_client_output ( 'receiver' )
                break

        #====================================================
        # Process the output.
        #====================================================
        flight_times = list()
        sum = 0

        for result in  output.split ( ) :
            _, start_time, stop_time = result.split ( ',' )
            msec = int(stop_time) - int(start_time)
            sum += msec
            flight_times.append ( float(msec) )

        n                 = len ( flight_times )
        mean              = float(sum) / float ( n )
        variances         = [ float(msec - mean) for msec in flight_times ]
        squared_variances = [ v**2 for v in variances ]
        sum_sq_var = 0
        for sv in squared_variances :
            sum_sq_var += sv
        sigma = math.sqrt ( sum_sq_var / n )

        result_file_name = self.results_path + '/result'
        with open ( result_file_name, 'w+' ) as file :
            self.test_description [ 'mean' ] = mean
            self.test_description [ 'sigma' ] = sigma
            json.dump ( self.test_description, file, indent=4, sort_keys=True )
        self.debug_print ( 'results written to |%s|' % result_file_name )
        self.bail ( None )


    def run(self):
        Container(self).run()





if __name__ == '__main__':
    unittest.main(main_module())

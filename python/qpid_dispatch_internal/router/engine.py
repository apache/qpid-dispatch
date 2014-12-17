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

from data import MessageHELLO, MessageRA, MessageLSU, MessageMAU, MessageMAR, MessageLSR
from neighbor import NeighborEngine
from link import LinkStateEngine
from path import PathEngine
from mobile import MobileAddressEngine
from routing import RoutingTableEngine
from node import NodeTracker
from message import Message

import sys
import traceback
import time

##
## Import the Dispatch adapters from the environment.  If they are not found
## (i.e. we are in a test bench, etc.), load the stub versions.
##
from dispatch import IoAdapter, LogAdapter, LOG_TRACE, LOG_DEBUG, LOG_INFO, LOG_ERROR

class RouterEngine:
    """
    """

    def __init__(self, router_adapter, router_id, area, max_routers, config_override={}):
        """
        Initialize an instance of a router for a domain.
        """
        ##
        ## Record important information about this router instance
        ##
        self.domain         = "domain"
        self.router_adapter = router_adapter
        self.log_adapter    = LogAdapter("ROUTER")
        self.io_adapter     = [IoAdapter(self.receive, "qdrouter"),
                               IoAdapter(self.receive, "qdhello")]
        self.max_routers    = max_routers
        self.id             = router_id
        self.area           = area
        self.instance       = long(time.time())
        self.log(LOG_INFO, "Router Engine Instantiated: area=%s id=%s instance=%d max_routers=%d" %
                 (self.area, self.id, self.instance, self.max_routers))
        self._config         = None # Not yet loaded
        ##
        ## Launch the sub-module engines
        ##
        self.node_tracker          = NodeTracker(self, self.max_routers)
        self.neighbor_engine       = NeighborEngine(self)
        self.link_state_engine     = LinkStateEngine(self)
        self.path_engine           = PathEngine(self)
        self.mobile_address_engine = MobileAddressEngine(self, self.node_tracker)
        self.routing_table_engine  = RoutingTableEngine(self, self.node_tracker)


    ##========================================================================================
    ## Adapter Entry Points - invoked from the adapter
    ##========================================================================================
    def getId(self):
        """
        Return the router's ID
        """
        return self.id

    @property
    def config(self):
        if not self._config:
            try: self._config = self.router_adapter.get_agent().find_entity_by_type('router')[0]
            except IndexError: raise ValueError("No router configuration found")
        return self._config

    def addressAdded(self, addr):
        """
        """
        try:
            if addr.find('Mtemp.') == 0:  ## This is a temporary measure until dynamic is added to Messenger
                return
            if addr.find('M') == 0:
                self.mobile_address_engine.add_local_address(addr[1:])
        except Exception, e:
            self.log(LOG_ERROR, "Exception in new-address processing: exception=%r" % e)
            exc_type, exc_value, exc_traceback = sys.exc_info()
            traceback.print_tb(exc_traceback)


    def addressRemoved(self, addr):
        """
        """
        try:
            if addr.find('Mtemp.') == 0:
                return
            if addr.find('M') == 0:
                self.mobile_address_engine.del_local_address(addr[1:])
        except Exception, e:
            self.log(LOG_ERROR, "Exception in del-address processing: exception=%r" % e)
            exc_type, exc_value, exc_traceback = sys.exc_info()
            traceback.print_tb(exc_traceback)


    def linkLost(self, link_id):
        """
        """
        self.log(LOG_INFO, "Router Link Lost - link_id=%d" % link_id)
        self.neighbor_engine.linkLost(link_id)


    def handleTimerTick(self):
        """
        """
        try:
            now = time.time()
            self.neighbor_engine.tick(now)
            self.link_state_engine.tick(now)
            self.path_engine.tick(now)
            self.mobile_address_engine.tick(now)
            self.routing_table_engine.tick(now)
            self.node_tracker.tick(now)
        except Exception, e:
            self.log(LOG_ERROR, "Exception in timer processing: exception=%r" % e)
            exc_type, exc_value, exc_traceback = sys.exc_info()
            traceback.print_tb(exc_traceback)


    def handleControlMessage(self, opcode, body, link_id):
        """
        """
        try:
            now = time.time()
            if   opcode == 'HELLO':
                msg = MessageHELLO(body)
                self.log(LOG_TRACE, "RCVD: %r" % msg)
                self.neighbor_engine.handle_hello(msg, now, link_id)

            elif opcode == 'RA':
                msg = MessageRA(body)
                self.log(LOG_TRACE, "RCVD: %r" % msg)
                self.link_state_engine.handle_ra(msg, now)
                self.mobile_address_engine.handle_ra(msg, now)

            elif opcode == 'LSU':
                msg = MessageLSU(body)
                self.log(LOG_DEBUG, "RCVD: %r" % msg)
                self.link_state_engine.handle_lsu(msg, now)

            elif opcode == 'LSR':
                msg = MessageLSR(body)
                self.log(LOG_DEBUG, "RCVD: %r" % msg)
                self.link_state_engine.handle_lsr(msg, now)

            elif opcode == 'MAU':
                msg = MessageMAU(body)
                self.log(LOG_DEBUG, "RCVD: %r" % msg)
                self.mobile_address_engine.handle_mau(msg, now)

            elif opcode == 'MAR':
                msg = MessageMAR(body)
                self.log(LOG_DEBUG, "RCVD: %r" % msg)
                self.mobile_address_engine.handle_mar(msg, now)

        except Exception, e:
            self.log(LOG_ERROR, "Exception in message processing: opcode=%s body=%r exception=%r" % (opcode, body, e))
            exc_type, exc_value, exc_traceback = sys.exc_info()
            traceback.print_tb(exc_traceback)


    def receive(self, message, link_id):
        """
        This is the IoAdapter message-receive handler
        """
        try:
            self.handleControlMessage(message.properties['opcode'], message.body, link_id)
        except Exception, e:
            self.log(LOG_ERROR, "Exception in raw message processing: properties=%r body=%r exception=%r" %
                     (message.properties, message.body, e))
            exc_type, exc_value, exc_traceback = sys.exc_info()
            traceback.print_tb(exc_traceback)


    def getRouterData(self, kind):
        """
        """
        if kind == 'help':
            return { 'help'           : "Get list of supported values for kind",
                     'link-state'     : "This router's link state",
                     'link-state-set' : "The set of link states from known routers",
                     'next-hops'      : "Next hops to each known router"
                     }
        if kind == 'link-state'     : return self.neighbor_engine.link_state.to_dict()
        if kind == 'next-hops'      : return self.routing_table_engine.next_hops
        if kind == 'link-state-set' :
            copy = {}
            for _id,_ls in self.link_state_engine.collection.items():
                copy[_id] = _ls.to_dict()
            return copy

        return {'notice':'Use kind="help" to get a list of possibilities'}


    ##========================================================================================
    ## Adapter Calls - outbound calls to Dispatch
    ##========================================================================================
    def log(self, level, text):
        """
        Emit a log message to the host's event log
        """
        info = traceback.extract_stack(limit=2)[0] # Caller frame info
        self.log_adapter.log(level, text, info[0], info[1])


    def send(self, dest, msg):
        """
        Send a control message to another router.
        """
        app_props = {'opcode' : msg.get_opcode() }
        self.io_adapter[0].send(Message(address=dest, properties=app_props, body=msg.to_dict()))
        self.log(LOG_TRACE, "SENT: %r dest=%s" % (msg, dest))


    def node_updated(self, addr, reachable, neighbor):
        """
        """
        self.router_adapter(addr, reachable, neighbor)


    ##========================================================================================
    ## Interconnect between the Sub-Modules
    ##========================================================================================
    def local_link_state_changed(self, link_state):
        self.log(LOG_DEBUG, "Event: local_link_state_changed: %r" % link_state)
        self.link_state_engine.new_local_link_state(link_state)

    def ls_collection_changed(self, collection):
        self.log(LOG_DEBUG, "Event: ls_collection_changed: %r" % collection)
        self.path_engine.ls_collection_changed(collection)

    def next_hops_changed(self, next_hop_table):
        self.log(LOG_DEBUG, "Event: next_hops_changed: %r" % next_hop_table)
        self.routing_table_engine.next_hops_changed(next_hop_table)

    def valid_origins_changed(self, valid_origins):
        self.log(LOG_DEBUG, "Event: valid_origins_changed: %r" % valid_origins)
        self.routing_table_engine.valid_origins_changed(valid_origins)

    def mobile_sequence_changed(self, mobile_seq):
        self.log(LOG_DEBUG, "Event: mobile_sequence_changed: %d" % mobile_seq)
        self.link_state_engine.set_mobile_sequence(mobile_seq)

    def get_next_hops(self):
        return self.routing_table_engine.get_next_hops()

    def new_neighbor(self, rid, link_id, instance):
        self.log(LOG_DEBUG, "Event: new_neighbor: id=%s link_id=%d inst=%r" % (rid, link_id, instance))
        self.node_tracker.new_neighbor(rid, link_id, instance)
        self.link_state_engine.new_neighbor(rid)

    def lost_neighbor(self, rid):
        self.log(LOG_DEBUG, "Event: lost_neighbor: id=%s" % rid)
        self.node_tracker.lost_neighbor(rid)

    def new_node(self, rid, instance):
        self.log(LOG_DEBUG, "Event: new_node: id=%s inst=%r" % (rid, instance))
        self.node_tracker.new_node(rid, instance)

    def lost_node(self, rid):
        self.log(LOG_DEBUG, "Event: lost_node: id=%s" % rid)
        self.node_tracker.lost_node(rid)

    def touch_node(self, rid, instance):
        return self.node_tracker.touch_node(rid, instance)

    def add_neighbor_router(self, address, router_bit, link_bit):
        self.log(LOG_DEBUG, "Event: add_neighbor_router: address=%s, router_bit=%d, link_bit=%d" % \
                     (address, router_bit, link_bit))
        self.router_adapter.add_neighbor_router(address, router_bit, link_bit)

    def del_neighbor_router(self, router_id, router_bit):
        self.log(LOG_DEBUG, "Event: del_neighbor_router: router_bit=%d" % router_bit)
        self.link_state_engine.purge_remote(router_id)
        self.mobile_address_engine.purge_remote(router_id)
        self.router_adapter.del_neighbor_router(router_bit)

    def add_remote_router(self, address, router_bit):
        self.log(LOG_DEBUG, "Event: add_remote_router: address=%s, router_bit=%d" % (address, router_bit))
        self.router_adapter.add_remote_router(address, router_bit)

    def del_remote_router(self, router_id, router_bit):
        self.log(LOG_DEBUG, "Event: del_remote_router: router_bit=%d" % router_bit)
        self.link_state_engine.purge_remote(router_id)
        self.mobile_address_engine.purge_remote(router_id)
        self.router_adapter.del_remote_router(router_bit)


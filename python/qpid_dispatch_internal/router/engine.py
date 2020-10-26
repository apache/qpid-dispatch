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

from .data import MessageHELLO, MessageRA, MessageLSU, MessageMAU, MessageMAR, MessageLSR, \
    isCompatibleVersion, getIdAndVersion
from .hello import HelloProtocol
from .link import LinkStateEngine
from .path import PathEngine
from .node import NodeTracker
from .message import Message

from traceback import format_exc, extract_stack
import time

##
## Import the Dispatch adapters from the environment.  If they are not found
## (i.e. we are in a test bench, etc.), load the stub versions.
##
from ..dispatch import IoAdapter, LogAdapter, LOG_TRACE, LOG_INFO, LOG_ERROR, LOG_WARNING, LOG_STACK_LIMIT
from ..dispatch import TREATMENT_MULTICAST_FLOOD, TREATMENT_MULTICAST_ONCE

class RouterEngine(object):
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
        self._config        = None # Not yet loaded
        self._log_hello     = LogAdapter("ROUTER_HELLO")
        self._log_ls        = LogAdapter("ROUTER_LS")
        self._log_general   = LogAdapter("ROUTER")
        self.io_adapter     = [IoAdapter(self.receive, "qdrouter", 'L', '0', TREATMENT_MULTICAST_FLOOD),
                               IoAdapter(self.receive, "qdrouter", 'T', '0', TREATMENT_MULTICAST_FLOOD),
                               IoAdapter(self.receive, "qdhello",  'L', '0', TREATMENT_MULTICAST_FLOOD)]
        self.max_routers    = max_routers
        self.id             = router_id
        self.instance       = int(time.time())
        self.area           = area
        self.incompatIds    = []
        self.log(LOG_INFO, "Router Engine Instantiated: id=%s instance=%d max_routers=%d" %
                 (self.id, self.instance, self.max_routers))

        ##
        ## Launch the sub-module engines
        ##
        self.node_tracker          = NodeTracker(self, self.max_routers)
        self.hello_protocol        = HelloProtocol(self, self.node_tracker)
        self.link_state_engine     = LinkStateEngine(self)
        self.path_engine           = PathEngine(self)


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
            try:
                self._config = self.router_adapter.get_agent().find_entity_by_type('router')[0]
            except IndexError:
                raise ValueError("No router configuration found")
        return self._config


    def setMobileSeq(self, router_maskbit, mobile_seq):
        """
        Another router's mobile sequence number has been changed and the Python router needs to store
        this number.
        """
        self.node_tracker.set_mobile_seq(router_maskbit, mobile_seq)

        
    def setMyMobileSeq(self, mobile_seq):
        """
        This router's mobile sequence number has been changed and the Python router needs to store
        this number and immediately send a router-advertisement message to reflect the change.
        """
        self.link_state_engine.set_mobile_seq(mobile_seq)
        self.link_state_engine.send_ra(time.time())

        
    def linkLost(self, link_id):
        """
        The control-link to a neighbor has been dropped.  We can cancel the neighbor from the
        link-state immediately instead of waiting for the hello-timeout to expire.
        """
        self.node_tracker.link_lost(link_id)


    def handleTimerTick(self):
        """
        """
        try:
            now = time.time()
            self.hello_protocol.tick(now)
            self.link_state_engine.tick(now)
            self.node_tracker.tick(now)
        except Exception:
            self.log(LOG_ERROR, "Exception in timer processing\n%s" % format_exc(LOG_STACK_LIMIT))


    def handleControlMessage(self, opcode, body, link_id, cost):
        """
        """
        if not isCompatibleVersion(body):
            rid, version = getIdAndVersion(body)
            if rid not in self.incompatIds:
                self.incompatIds.append(rid)
                self.log(LOG_WARNING, "Received %s at protocol version %d from %s.  Ignoring." % (opcode, version, rid))
            return

        try:
            now = time.time()
            if   opcode == 'HELLO':
                msg = MessageHELLO(body)
                self.log_hello(LOG_TRACE, "RCVD: %r" % msg)
                self.hello_protocol.handle_hello(msg, now, link_id, cost)

            elif opcode == 'RA':
                msg = MessageRA(body)
                self.log_ls(LOG_TRACE, "RCVD: %r" % msg)
                self.link_state_engine.handle_ra(msg, now)

            elif opcode == 'LSU':
                msg = MessageLSU(body)
                self.log_ls(LOG_TRACE, "RCVD: %r" % msg)
                self.link_state_engine.handle_lsu(msg, now)

            elif opcode == 'LSR':
                msg = MessageLSR(body)
                self.log_ls(LOG_TRACE, "RCVD: %r" % msg)
                self.link_state_engine.handle_lsr(msg, now)

        except Exception:
            self.log(LOG_ERROR, "Exception in control message processing\n%s" % format_exc(LOG_STACK_LIMIT))
            self.log(LOG_ERROR, "Control message error: opcode=%s body=%r" % (opcode, body))


    def receive(self, message, link_id, cost):
        """
        This is the IoAdapter message-receive handler
        """
        try:
            self.handleControlMessage(message.properties['opcode'], message.body, link_id, cost)
        except Exception:
            self.log(LOG_ERROR, "Exception in raw message processing\n%s" % format_exc(LOG_STACK_LIMIT))
            self.log(LOG_ERROR, "Exception in raw message processing: properties=%r body=%r" %
                     (message.properties, message.body))


    def getRouterData(self, kind):
        """
        """
        if kind == 'help':
            return { 'help'           : "Get list of supported values for kind",
                     'link-state'     : "This router's link state",
                     'link-state-set' : "The set of link states from known routers",
                     'next-hops'      : "Next hops to each known router"
                     }
        if kind == 'link-state'     :
            return self.neighbor_engine.link_state.to_dict()
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
        info = extract_stack(limit=2)[0] # Caller frame info
        self._log_general.log(level, text, info[0], info[1])


    def log_hello(self, level, text):
        """
        Emit a log message to the host's event log
        """
        info = extract_stack(limit=2)[0] # Caller frame info
        self._log_hello.log(level, text, info[0], info[1])


    def log_ls(self, level, text):
        """
        Emit a log message to the host's event log
        """
        info = extract_stack(limit=2)[0] # Caller frame info
        self._log_ls.log(level, text, info[0], info[1])


    def log_ma(self, level, text):
        """
        Emit a log message to the host's event log
        """
        info = extract_stack(limit=2)[0] # Caller frame info
        self._log_ma.log(level, text, info[0], info[1])


    def send(self, dest, msg):
        """
        Send a control message to another router.
        """
        app_props = {'opcode' : msg.get_opcode() }
        self.io_adapter[0].send(Message(address=dest, properties=app_props, body=msg.to_dict()), True, True)


    def node_updated(self, addr, reachable, neighbor):
        """
        """
        self.router_adapter(addr, reachable, neighbor)


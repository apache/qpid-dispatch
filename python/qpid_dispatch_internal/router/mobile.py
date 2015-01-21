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

from data import MessageMAR, MessageMAU
from dispatch import LOG_DEBUG, LOG_TRACE

MAX_KEPT_DELTAS = 10

class MobileAddressEngine(object):
    """
    This module is responsible for maintaining an up-to-date list of mobile addresses in the domain.
    It runs the Mobile-Address protocol and generates an un-optimized routing table for mobile addresses.
    Note that this routing table maps from the mobile address to the remote router where that address
    is directly bound.
    """
    def __init__(self, container, node_tracker):
        self.container     = container
        self.node_tracker  = node_tracker
        self.id            = self.container.id
        self.mobile_seq    = 0
        self.local_addrs   = []
        self.added_addrs   = []
        self.deleted_addrs = []
        self.sent_deltas   = {}


    def tick(self, now):
        ##
        ## If local addrs have changed, collect the changes and send a MAU with the diffs
        ## Note: it is important that the differential-MAU be sent before a RA is sent
        ##
        if len(self.added_addrs) > 0 or len(self.deleted_addrs) > 0:
            self.mobile_seq += 1
            msg = MessageMAU(None, self.id, self.mobile_seq, self.added_addrs, self.deleted_addrs)

            self.sent_deltas[self.mobile_seq] = msg
            if len(self.sent_deltas) > MAX_KEPT_DELTAS:
                self.sent_deltas.pop(self.mobile_seq - MAX_KEPT_DELTAS)

            self.container.send('amqp:/_topo/0/all/qdrouter.ma', msg)
            self.container.log_ma(LOG_TRACE, "SENT: %r" % msg)
            self.local_addrs.extend(self.added_addrs)
            for addr in self.deleted_addrs:
                self.local_addrs.remove(addr)
            self.added_addrs   = []
            self.deleted_addrs = []
        return self.mobile_seq


    def add_local_address(self, addr):
        """
        """
        if self.local_addrs.count(addr) == 0:
            if self.added_addrs.count(addr) == 0:
                self.added_addrs.append(addr)
        else:
            if self.deleted_addrs.count(addr) > 0:
                self.deleted_addrs.remove(addr)


    def del_local_address(self, addr):
        """
        """
        if self.local_addrs.count(addr) > 0:
            if self.deleted_addrs.count(addr) == 0:
                self.deleted_addrs.append(addr)
        else:
            if self.added_addrs.count(addr) > 0:
                self.added_addrs.remove(addr)


    def handle_mau(self, msg, now):
        ##
        ## If the MAU is differential, we can only use it if its sequence is exactly one greater
        ## than our stored sequence.  If not, we will ignore the content and schedule a MAR.
        ##
        ## If the MAU is absolute, we can use it in all cases.
        ##
        if msg.id == self.id:
            return
        node = self.node_tracker.router_node(msg.id)

        if msg.exist_list != None:
            ##
            ## Absolute MAU
            ##
            if msg.mobile_seq == node.mobile_address_sequence:
                return
            node.mobile_address_sequence = msg.mobile_seq
            node.overwrite_addresses(msg.exist_list)
        else:
            ##
            ## Differential MAU
            ##
            if node.mobile_address_sequence + 1 == msg.mobile_seq:
                ##
                ## This message represents the next expected sequence, incorporate the deltas
                ##
                node.mobile_address_sequence += 1
                for a in msg.add_list:
                    node.map_address(a)
                for a in msg.del_list:
                    node.unmap_address(a)

            elif node.mobile_address_sequence == msg.mobile_seq:
                ##
                ## Ignore duplicates
                ##
                return

            else:
                ##
                ## This is an out-of-sequence delta.  Don't use it.  Schedule a MAR to
                ## get back on track.
                ##
                node.mobile_address_request()


    def handle_mar(self, msg, now):
        if msg.id == self.id:
            return
        if msg.have_seq == self.mobile_seq:
            return
        if self.mobile_seq - (msg.have_seq + 1) < len(self.sent_deltas):
            ##
            ## We can catch the peer up with a series of stored differential updates
            ##
            for s in range(msg.have_seq + 1, self.mobile_seq + 1):
                self.container.send('amqp:/_topo/0/%s/qdrouter.ma' % msg.id, self.sent_deltas[s])
                self.container.log_ma(LOG_TRACE, "SENT: %r" % self.sent_deltas[s])
            return

        ##
        ## The peer needs to be sent an absolute update with the whole address list
        ##
        smsg = MessageMAU(None, self.id, self.mobile_seq, None, None, self.local_addrs)
        self.container.send('amqp:/_topo/0/%s/qdrouter.ma' % msg.id, smsg)
        self.container.log_ma(LOG_TRACE, "SENT: %r" % smsg)


    def send_mar(self, node_id, seq):
        msg = MessageMAR(None, self.id, seq)
        self.container.send('amqp:/_topo/0/%s/qdrouter.ma' % node_id, msg)
        self.container.log_ma(LOG_TRACE, "SENT: %r" % msg)



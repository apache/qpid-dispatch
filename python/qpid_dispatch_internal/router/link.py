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

from .data import MessageRA, MessageLSU, MessageLSR
from ..dispatch import LOG_TRACE


class LinkStateEngine:
    """This module is responsible for running the Link State protocol."""

    def __init__(self, container):
        self.container = container
        self.node_tracker = container.node_tracker
        self.id = self.container.id
        self.ra_interval_stable = self.container.config.raIntervalSeconds
        self.ra_interval_flux   = self.container.config.raIntervalFluxSeconds
        self.last_ra_time = 0
        self.mobile_seq   = 0

    def set_mobile_seq(self, mobile_seq):
        self.mobile_seq = mobile_seq

    def tick(self, now):
        interval = self.ra_interval_stable
        if self.node_tracker.in_flux_mode(now):
            interval = self.ra_interval_flux
        if now - self.last_ra_time >= interval:
            self.send_ra(now)

    def handle_ra(self, msg, now):
        if msg.id == self.id:
            return
        self.node_tracker.ra_received(msg.id, msg.version, msg.ls_seq, msg.mobile_seq, msg.instance, now)

    def handle_lsu(self, msg, now):
        if msg.id == self.id:
            return
        self.node_tracker.link_state_received(msg.id, msg.version, msg.ls, msg.instance, now)

    def handle_lsr(self, msg, now):
        if msg.id == self.id:
            return
        self.node_tracker.router_learned(msg.id, msg.version)
        my_ls = self.node_tracker.link_state
        smsg = MessageLSU(None, self.id, my_ls.ls_seq, my_ls, self.container.instance)
        self.container.send('amqp:/_topo/%s/%s/qdrouter' % (msg.area, msg.id), smsg)
        self.container.log_ls(LOG_TRACE, "SENT: %r" % smsg)

    def send_lsr(self, _id):
        msg = MessageLSR(None, self.id)
        self.container.send('amqp:/_topo/0/%s/qdrouter' % _id, msg)
        self.container.log_ls(LOG_TRACE, "SENT: %r to: %s" % (msg, _id))

    def send_ra(self, now):
        self.last_ra_time = now
        ls_seq = self.node_tracker.link_state.ls_seq
        msg = MessageRA(None, self.id, ls_seq, self.mobile_seq, self.container.instance)
        self.container.send('amqp:/_topo/0/all/qdrouter', msg)
        self.container.log_ls(LOG_TRACE, "SENT: %r" % msg)

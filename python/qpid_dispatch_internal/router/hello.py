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

from data import LinkState, MessageHELLO
from dispatch import LOG_INFO, LOG_TRACE

class HelloProtocol(object):
    """
    This module is responsible for running the HELLO protocol.
    """
    def __init__(self, container, node_tracker):
        self.container       = container
        self.node_tracker    = node_tracker
        self.id              = self.container.id
        self.last_hello_time = 0.0
        self.hello_interval  = container.config.helloInterval
        self.hello_max_age   = container.config.helloMaxAge
        self.hellos          = {}


    def tick(self, now):
        self._expire_hellos(now)
        if now - self.last_hello_time >= self.hello_interval:
            self.last_hello_time = now
            msg = MessageHELLO(None, self.id, self.hellos.keys(), self.container.instance)
            self.container.send('amqp:/_local/qdhello', msg)
            self.container.log_hello(LOG_TRACE, "SENT: %r" % msg)


    def handle_hello(self, msg, now, link_id):
        if msg.id == self.id:
            return
        self.hellos[msg.id] = now
        if msg.is_seen(self.id):
            self.node_tracker.neighbor_refresh(msg.id, msg.instance, link_id, now)


    def _expire_hellos(self, now):
        """
        Expire local records of received hellos.  This is not involved in the
        expiration of neighbor status for routers.
        """
        for key, last_seen in self.hellos.items():
            if now - last_seen > self.hello_max_age:
                self.hellos.pop(key)
                self.container.log_hello(LOG_INFO, "HELLO peer expired: %s" % key)


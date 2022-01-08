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

from ..dispatch import LOG_INFO, LOG_TRACE, LOG_DEBUG
from .data import LinkState, ProtocolVersion
from .address import Address


class NodeTracker:
    """
    This module is responsible for tracking the set of router nodes that are known to this
    router.  It tracks whether they are neighbor or remote and whether they are reachable.

    This module is also responsible for assigning a unique mask bit value to each router.
    The mask bit is used in the main router to represent sets of valid destinations for addresses.
    """

    def __init__(self, container, max_routers):
        self.container             = container
        self.my_id                 = container.id
        self.max_routers           = max_routers
        self.link_state            = LinkState(None, self.my_id, 0, {})
        self.link_state_changed    = False
        self.recompute_topology    = False
        self.last_topology_change  = 0
        self.flux_mode             = False
        self.nodes                 = {}  # id => RouterNode
        self.nodes_by_link_id      = {}  # link-id => node-id
        self.maskbits              = []
        self.next_maskbit          = 1   # Reserve bit '0' to represent this router
        for i in range(max_routers):
            self.maskbits.append(None)
        self.maskbits[0]      = True
        self.neighbor_max_age = self.container.config.helloMaxAgeSeconds
        self.ls_max_age       = self.container.config.remoteLsMaxAgeSeconds
        self.flux_interval    = self.container.config.raIntervalFluxSeconds * 2
        self.container.router_adapter.get_agent().add_implementation(self, "router.node")

    def refresh_entity(self, attributes):
        """Refresh management attributes"""
        attributes.update({
            "id": self.my_id,
            "index": 0,
            "protocolVersion": ProtocolVersion,
            "instance": self.container.instance,  # Boot number, integer
            "linkState": list(self.link_state.peers),  # List of neighbour nodes
            "nextHop": "(self)",
            "validOrigins": [],
            "address": Address.topological(self.my_id, area=self.container.area),
            "lastTopoChange" : self.last_topology_change
        })

    def _do_expirations(self, now):
        """Run through the list of routers and check for expired conditions"""
        for node_id, node in list(self.nodes.items()):
            ##
            # If the node is a neighbor, check the neighbor refresh time to see
            # if we've waited too long for a refresh.  If so, disconnect the link
            # and remove the node from the local link state.
            ##
            if node.is_neighbor():
                if now - node.neighbor_refresh_time > self.neighbor_max_age:
                    node.remove_link()
                    if self.link_state.del_peer(node_id):
                        self.link_state_changed = True

            ##
            # Check the age of the node's link state.  If it's too old, clear it out.
            ##
            if now - node.link_state.last_seen > self.ls_max_age:
                if node.link_state.has_peers():
                    node.link_state.del_all_peers()
                    self.recompute_topology = True

            ##
            # If the node has empty link state, check to see if it appears in any other
            # node's link state.  If it does not, then delete the node.
            ##
            if not node.link_state.has_peers() and not node.is_neighbor():
                delete_node = True
                for _id, _n in self.nodes.items():
                    if _id != node_id:
                        if _n.link_state.is_peer(node_id):
                            delete_node = False
                            break
                if delete_node:
                    ##
                    # The keep_alive_count is set to zero when a new node is first
                    # discovered.  Since we can learn about a node before we receive
                    # its link state, the keep_alive_count is used to prevent the
                    # node from being deleted before we can learn more about it.
                    ##
                    node.keep_alive_count += 1
                    if node.keep_alive_count > 2:
                        node.delete()
                        self.nodes.pop(node_id)

    def tick(self, now):
        send_ra = False

        ##
        # Expire neighbors and link state
        ##
        self._do_expirations(now)

        ##
        # Enter flux mode if things are changing
        ##
        if self.link_state_changed or self.recompute_topology:
            self.last_topology_change = int(round(now))
            if not self.flux_mode:
                self.flux_mode = True
                self.container.log(LOG_TRACE, "Entered Router Flux Mode")

        ##
        # Handle local link state changes
        ##
        if self.link_state_changed:
            self.link_state_changed = False
            self.link_state.bump_sequence()
            self.recompute_topology = True
            send_ra = True
            self.container.log_ls(LOG_TRACE, "Local Link State: %r" % self.link_state)

        ##
        # Recompute the topology
        ##
        if self.recompute_topology:
            self.recompute_topology = False
            collection = {self.my_id : self.link_state}
            for node_id, node in self.nodes.items():
                collection[node_id] = node.link_state
            next_hops, costs, valid_origins, radius = self.container.path_engine.calculate_routes(collection)
            self.container.log_ls(LOG_INFO, "Computed next hops: %r" % next_hops)
            self.container.log_ls(LOG_INFO, "Computed costs: %r" % costs)
            self.container.log_ls(LOG_INFO, "Computed valid origins: %r" % valid_origins)
            self.container.log_ls(LOG_INFO, "Computed radius: %d" % radius)

            ##
            # Update the topology radius
            ##
            self.container.router_adapter.set_radius(radius)

            ##
            # Update the next hops and valid origins for each node
            ##
            for node_id, next_hop_id in next_hops.items():
                node     = self.nodes[node_id]
                next_hop = self.nodes[next_hop_id]
                vo       = valid_origins[node_id]
                cost     = costs[node_id]
                node.set_next_hop(next_hop)
                node.set_valid_origins(vo)
                node.set_cost(cost)

        ##
        # Send link-state requests and mobile-address requests to the nodes
        # that have pending requests and are reachable
        ##
        for node_id, node in self.nodes.items():
            if node.link_state_requested():
                self.container.link_state_engine.send_lsr(node_id)
            if node.mobile_address_requested():
                self.container.router_adapter.mobile_seq_advanced(node.maskbit)

        ##
        # Send an immediate RA if our link state changed
        ##
        if send_ra:
            self.container.link_state_engine.send_ra(now)

    def neighbor_refresh(self, node_id, version, instance, link_id, cost, now):
        """
        Invoked when the hello protocol has received positive confirmation
        of continued bi-directional connectivity with a neighbor router.
        """

        ##
        # If the node id is not known, create a new RouterNode to track it.
        ##
        if node_id not in self.nodes:
            self.nodes[node_id] = RouterNode(self, node_id, version, instance)
        node = self.nodes[node_id]

        ##
        # Add the version if we haven't already done so.
        ##
        if node.version is None:
            node.version = version

        ##
        # Set the link_id to indicate this is a neighbor router.  If the link_id
        # changed, update the index and add the neighbor to the local link state.
        ##
        if node.set_link_id(link_id):
            self.nodes_by_link_id[link_id] = node
            node.request_link_state()
            if self.link_state.add_peer(node_id, cost):
                self.link_state_changed = True

        ##
        # Update the refresh time for later expiration checks
        ##
        node.neighbor_refresh_time = now

        ##
        # If the instance was updated (i.e. the neighbor restarted suddenly),
        # schedule a topology recompute and a link-state-request to that router.
        ##
        if node.update_instance(instance, version):
            self.recompute_topology = True
            node.request_link_state()

    def link_lost(self, link_id):
        """Invoked when an inter-router link is dropped."""
        self.container.log_ls(LOG_INFO, "Link to Neighbor Router Lost - link_tag=%d" % link_id)
        node_id = self.link_id_to_node_id(link_id)
        if node_id:
            self.nodes_by_link_id.pop(link_id)
            node = self.nodes[node_id]
            node.remove_link()
            if self.link_state.del_peer(node_id):
                self.link_state_changed = True

    def set_mobile_seq(self, router_maskbit, mobile_seq):
        """
        """
        for node in self.nodes.values():
            if node.maskbit == router_maskbit:
                node.mobile_address_sequence = mobile_seq
                return

    def in_flux_mode(self, now):
        result = (now - self.last_topology_change) <= self.flux_interval
        if not result and self.flux_mode:
            self.flux_mode = False
            self.container.log(LOG_TRACE, "Exited Router Flux Mode")
        return result

    def ra_received(self, node_id, version, ls_seq, mobile_seq, instance, now):
        """Invoked when a router advertisement is received from another router."""
        ##
        # If the node id is not known, create a new RouterNode to track it.
        ##
        if node_id not in self.nodes:
            self.nodes[node_id] = RouterNode(self, node_id, version, instance)
        node = self.nodes[node_id]

        ##
        # Add the version if we haven't already done so.
        ##
        if node.version is None:
            node.version = version

        ##
        # If the instance was updated (i.e. the router restarted suddenly),
        # schedule a topology recompute and a link-state-request to that router.
        ##
        if node.update_instance(instance, version):
            self.recompute_topology = True
            node.request_link_state()

        ##
        # Update the last seen time to now to control expiration of the link state.
        ##
        node.link_state.last_seen = now

        ##
        # Check the link state sequence.  Send a link state request if our records are
        # not up to date.
        ##
        if node.link_state.ls_seq < ls_seq:
            self.container.link_state_engine.send_lsr(node_id)

        ##
        # Check the mobile sequence.  Send a mobile-address-request if we are
        # behind the advertized sequence.
        ##
        if node.mobile_address_sequence < mobile_seq:
            node.mobile_address_request()

    def router_learned(self, node_id, version):
        """Invoked when we learn about another router by any means"""
        if node_id not in self.nodes and node_id != self.my_id:
            self.nodes[node_id] = RouterNode(self, node_id, version, None)

    def link_state_received(self, node_id, version, link_state, instance, now):
        """Invoked when a link state update is received from another router."""
        ##
        # If the node id is not known, create a new RouterNode to track it.
        ##
        if node_id not in self.nodes:
            self.nodes[node_id] = RouterNode(self, node_id, version, instance)
        node = self.nodes[node_id]

        ##
        # Add the version if we haven't already done so.
        ##
        if node.version is None:
            node.version = version

        ##
        # If the new link state is more up-to-date than the stored link state,
        # update it and schedule a topology recompute.
        ##
        if link_state.ls_seq > node.link_state.ls_seq:
            node.link_state = link_state
            node.link_state.last_seen = now
            self.recompute_topology = True

            ##
            # Look through the new link state for references to nodes that we don't
            # know about.  Schedule link state requests for those nodes to be sent
            # after we next recompute the topology.
            ##
            for peer in node.link_state.peers:
                if peer not in self.nodes:
                    self.router_learned(peer, None)

    def router_node(self, node_id):
        return self.nodes[node_id]

    def link_id_to_node_id(self, link_id):
        if link_id in self.nodes_by_link_id:
            return self.nodes_by_link_id[link_id].id
        return None

    def _allocate_maskbit(self):
        if self.next_maskbit is None:
            raise Exception("Exceeded Maximum Router Count")
        result = self.next_maskbit
        self.next_maskbit = None
        self.maskbits[result] = True
        for n in range(result + 1, self.max_routers):
            if self.maskbits[n] is None:
                self.next_maskbit = n
                break
        return result

    def _free_maskbit(self, i):
        self.maskbits[i] = None
        if self.next_maskbit is None or i < self.next_maskbit:
            self.next_maskbit = i


class RouterNode:
    """RouterNode is used to track remote routers in the router network."""

    def __init__(self, parent, node_id, version, instance):
        self.parent                  = parent
        self.adapter                 = parent.container.router_adapter
        self.log                     = parent.container.log
        self.id                      = node_id
        self.version                 = version
        self.instance                = instance
        self.maskbit                 = self.parent._allocate_maskbit()
        self.neighbor_refresh_time   = 0.0
        self.peer_link_id            = None
        self.link_state              = LinkState(None, self.id, 0, {})
        self.next_hop_router         = None
        self.cost                    = None
        self.valid_origins           = None
        self.mobile_address_sequence = 0
        self.need_ls_request         = True
        self.need_mobile_request     = False
        self.keep_alive_count        = 0
        self.adapter.add_router("amqp:/_topo/0/%s/qdrouter" % self.id, self.maskbit)
        self.log(LOG_TRACE, "Node %s created: maskbit=%d" % (self.id, self.maskbit))
        self.adapter.get_agent().add_implementation(self, "router.node")

    def refresh_entity(self, attributes):
        """Refresh management attributes"""
        attributes.update({
            "id": self.id,
            "index": self.maskbit,
            "protocolVersion": self.version,
            "instance": self.instance,  # Boot number, integer
            "linkState": list(self.link_state.peers),  # List of neighbour nodes
            "nextHop": self.next_hop_router and self.next_hop_router.id,
            "validOrigins": self.valid_origins,
            "address": Address.topological(self.id, area=self.parent.container.area),
            "routerLink": self.peer_link_id,
            "cost": self.cost
        })

    def _logify(self, addr):
        cls   = addr[0]
        phase = None
        if cls == 'M':
            phase = addr[1]
            return "%s;class=%c;phase=%c" % (addr[2:], cls, phase)
        return "%s;class=%c" % (addr[1:], cls)

    def set_link_id(self, link_id):
        if self.peer_link_id == link_id:
            return False
        self.peer_link_id = link_id
        self.next_hop_router = None
        self.adapter.set_link(self.maskbit, link_id)
        self.adapter.remove_next_hop(self.maskbit)
        self.log(LOG_TRACE, "Node %s link set: link_id=%r (removed next hop)" % (self.id, link_id))
        return True

    def remove_link(self):
        if self.peer_link_id is not None:
            self.peer_link_id = None
            self.adapter.remove_link(self.maskbit)
            self.log(LOG_TRACE, "Node %s link removed" % self.id)

    def delete(self):
        self.adapter.get_agent().remove_implementation(self)
        self.unmap_all_addresses()
        self.adapter.del_router(self.maskbit)
        self.parent._free_maskbit(self.maskbit)
        self.log(LOG_TRACE, "Node %s deleted" % self.id)

    def set_next_hop(self, next_hop):
        if self.id == next_hop.id:
            ##
            # If the next hop is self (destination is a neighbor) and there
            # was a next hop in place, explicitly remove the next hop (DISPATCH-873).
            ##
            self.remove_next_hop()
            return
        if self.next_hop_router and self.next_hop_router.id == next_hop.id:
            return
        self.next_hop_router = next_hop
        self.adapter.set_next_hop(self.maskbit, next_hop.maskbit)
        self.log(LOG_TRACE, "Node %s next hop set: %s" % (self.id, next_hop.id))

    def set_valid_origins(self, valid_origins):
        if self.valid_origins == valid_origins:
            return
        self.valid_origins = valid_origins
        vo_mb = [self.parent.nodes[N].maskbit for N in valid_origins]
        self.adapter.set_valid_origins(self.maskbit, vo_mb)
        self.log(LOG_TRACE, "Node %s valid origins: %r" % (self.id, valid_origins))

    def set_cost(self, cost):
        if self.cost == cost:
            return
        self.cost = cost
        self.adapter.set_cost(self.maskbit, cost)
        self.log(LOG_TRACE, "Node %s cost: %d" % (self.id, cost))

    def remove_next_hop(self):
        if self.next_hop_router:
            self.next_hop_router = None
            self.adapter.remove_next_hop(self.maskbit)
            self.log(LOG_TRACE, "Node %s next hop removed" % self.id)

    def is_neighbor(self):
        return self.peer_link_id is not None

    def request_link_state(self):
        """
        Set the link-state-requested flag so we can send this node a link-state
        request at the most opportune time.
        """
        self.need_ls_request = True

    def link_state_requested(self):
        """
        Return True iff we need to request this node's link state AND the node is
        reachable.  There's no point in sending it a request if we don't know how to
        reach it.
        """
        if self.need_ls_request and (self.peer_link_id is not None or
                                     self.next_hop_router is not None):
            self.need_ls_request = False
            return True
        return False

    def mobile_address_request(self):
        self.need_mobile_request = True

    def mobile_address_requested(self):
        if self.need_mobile_request and (self.peer_link_id is not None or
                                         self.next_hop_router is not None):
            self.need_mobile_request = False
            return True
        return False

    def unmap_all_addresses(self):
        self.mobile_address_sequence = 0
        self.adapter.flush_destinations(self.maskbit)
        self.log(LOG_DEBUG, "Remote destinations flushed from router %s" % (self.id))

    def update_instance(self, instance, version):
        if instance is None:
            return False
        if self.instance is None:
            self.instance = instance
            return False
        if self.instance == instance:
            return False

        self.instance = instance
        self.version  = version
        self.link_state.del_all_peers()
        self.unmap_all_addresses()
        self.log(LOG_INFO, "Detected Restart of Router Node %s" % self.id)
        return True

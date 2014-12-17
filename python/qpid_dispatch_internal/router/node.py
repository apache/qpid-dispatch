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

class NodeTracker(object):
    """
    This module is responsible for tracking the set of router nodes that are known to this
    router.  It tracks whether they are neighbor or remote and whether they are reachable.

    This module is also responsible for assigning a unique mask bit value to each router.
    The mask bit is used in the main router to represent sets of valid destinations for addresses.
    """
    def __init__(self, container, max_routers):
        self.container        = container
        self.max_routers      = max_routers
        self.nodes            = {}  # id => RemoteNode
        self.nodes_by_link_id = {}  # link-id => node-id
        self.maskbits         = []
        self.next_maskbit     = 1   # Reserve bit '0' to represent this router
        for i in range(max_routers):
            self.maskbits.append(None)
        self.maskbits[0] = True


    def tick(self, now):
        pass


    def new_neighbor(self, node_id, link_maskbit, instance):
        """
        A node, designated by node_id, has been discovered as a neighbor over a link with
        a maskbit of link_maskbit.
        """
        self.nodes_by_link_id[link_maskbit] = node_id
        if node_id in self.nodes:
            node = self.nodes[node_id]
            if node.neighbor:
                if node.update_instance(instance):
                    self.container.del_neighbor_router(node.id, node.maskbit)
                else:
                    return
            else:
                self.container.del_remote_router(node.id, node.maskbit)
                node.neighbor = True
                node.link_id  = link_maskbit
        else:
            node = RemoteNode(node_id, self._allocate_maskbit(), True, link_maskbit, instance)
            self.nodes[node_id] = node
        self.container.add_neighbor_router(self._address(node_id), node.maskbit, link_maskbit)


    def lost_neighbor(self, node_id):
        """
        We have lost contact with a neighboring node.
        """
        node = self.nodes[node_id]
        node.neighbor = False
        self.nodes_by_link_id.pop(node.link_id)
        node.link_id = None
        self.container.del_neighbor_router(node.id, node.maskbit)
        if node.remote:
            self.container.add_remote_router(self._address(node.id), node.maskbit)
        else:
            self._free_maskbit(node.maskbit)
            self.nodes.pop(node_id)


    def new_node(self, node_id, instance):
        """
        A node, designated by node_id, has been discovered through the an advertisement from a
        remote peer.
        """
        if node_id not in self.nodes:
            node = RemoteNode(node_id, self._allocate_maskbit(), False, None, instance)
            self.nodes[node_id] = node
            self.container.add_remote_router(self._address(node.id), node.maskbit)
        else:
            node = self.nodes[node_id]
            node.remote = True
            if node.update_instance(instance):
                self.container.del_remote_router(node.id, node.maskbit)
                self.container.add_remote_router(self._address(node.id), node.maskbit)


    def lost_node(self, node_id):
        """
        A remote node, node_id, has not been heard from for too long and is being deemed lost.
        """
        node = self.nodes[node_id]
        if node.remote:
            node.remote = False
            if not node.neighbor:
                self.container.del_remote_router(node.id, node.maskbit)
                self._free_maskbit(node.maskbit)
                self.nodes.pop(node_id)


    def touch_node(self, node_id, instance):
        """
        We've received an advertisement or hello from a node.  If the instance has changed,
        we need to treat the node as though it was lost and regained.
        """
        try:
            node = self.nodes[node_id]
            if node.update_instance(instance):
                if node.neighbor:
                    self.container.del_neighbor_router(node.id, node.maskbit)
                    self.container.add_neighbor_router(self._address(node_id), node.maskbit, node.link_id)
                elif node.remote:
                    self.container.del_remote_router(node.id, node.maskbit)
                    self.container.add_remote_router(self._address(node.id), node.maskbit)
                return True
        except:
            pass
        return False


    def maskbit_for_node(self, node_id):
        """
        """
        if node_id in self.nodes:
            node = self.nodes[node_id]
            return node.maskbit
        return None


    def add_addresses(self, node_id, addrs):
        node = self.nodes[node_id]
        for a in addrs:
            node.addrs[a] = 1


    def del_addresses(self, node_id, addrs):
        node = self.nodes[node_id]
        for a in addrs:
            node.addrs.pop(a)


    def overwrite_addresses(self, node_id, addrs):
        node    = self.nodes[node_id]
        added   = []
        deleted = []
        for a in addrs:
            if a not in node.addrs.keys():
                added.append(a)
        for a in node.addrs.keys():
            if a not in addrs:
                deleted.append(a)
        node.addrs = {}
        for a in addrs:
            node.addrs[a] = 1
        return (added, deleted)


    def link_id_to_node_id(self, link_id):
        if link_id in self.nodes_by_link_id:
            return self.nodes_by_link_id[link_id]
        return None


    def _allocate_maskbit(self):
        if self.next_maskbit == None:
            raise Exception("Exceeded Maximum Router Count")
        result = self.next_maskbit
        self.next_maskbit = None
        self.maskbits[result] = True
        for n in range(result + 1, self.max_routers):
            if self.maskbits[n] == None:
                self.next_maskbit = n
                break
        return result


    def _free_maskbit(self, i):
        self.maskbits[i] = None
        if self.next_maskbit == None or i < self.next_maskbit:
            self.next_maskbit = i


    def _address(self, node_id):
        return "amqp:/_topo/%s/%s" % (self.container.area, node_id)


class RemoteNode(object):

    def __init__(self, node_id, maskbit, neighbor, link_id, instance):
        self.id       = node_id
        self.maskbit  = maskbit
        self.neighbor = neighbor
        self.remote   = not neighbor
        self.link_id  = link_id
        self.instance = instance
        self.addrs    = {}  # Address => Count at Node (1 only for the present)


    def update_instance(self, instance):
        if instance == None:
            return False
        if self.instance == None:
            self.instance = instance
            return False
        if self.instance == instance:
            return False
        self.instance = instance
        return True

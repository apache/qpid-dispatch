#!/usr/bin/env python
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

import json
from proton import generate_uuid
import collections

class Schema(object):
    schema = {}

    @staticmethod
    def i(entity, attribute):
        return Schema.schema[entity]["attributeNames"].index(attribute)

    @staticmethod
    def type(entity):
        return Schema.schema[entity]["fullyQualifiedType"]

    @staticmethod
    def init():
        with open("topologies/schema.json") as fp:
            data = json.load(fp)
            for entity in data["entityTypes"]:
                Schema.schema[entity] = {"attributeNames": [],
                                         "fullyQualifiedType": data["entityTypes"][entity]["fullyQualifiedType"]}
                for attribute in data["entityTypes"][entity]["attributes"]:
                    Schema.schema[entity]["attributeNames"].append(attribute)
                Schema.schema[entity]["attributeNames"].append("type")

class SparseList(list):
    '''
    from http://stackoverflow.com/questions/1857780/sparse-assignment-list-in-python
    '''
    def __setitem__(self, index, value):
        missing = index - len(self) + 1
        if missing > 0:
            self.extend([None] * missing)
        list.__setitem__(self, index, value)
    def __getitem__(self, index):
        try: return list.__getitem__(self, index)
        except IndexError: return None

class Entity(object):
    def __init__(self, name):
        self.name = name
        self.value = SparseList()
        self.settype()

    def setval(self, attribute, value):
        self.value[Schema.i(self.name, attribute)] = value

    def settype(self):
        self.setval("type", Schema.type(self.name))

    def setZero(self, attributes):
        for attribute in attributes:
            self.setval(attribute, 0)

    def getval(self, attr):
        return self.value[Schema.i(self.name, attr)]

    def vals(self):
        return self.value

class Multiple(object):
    def __init__(self):
        self.results = []

    def vals(self):
        return self.results

class RouterNode(Entity):
    instance = 0
    def __init__(self, f, t, links, hopper):
        super(RouterNode, self).__init__("router.node")
        self.hopper = hopper
        self.init(f, t, links)

    def init(self, f, t, links):
        RouterNode.instance += 1
        self.setval("name", "router.node/" + t)
        self.setval("nextHop", "(self)" if f == t else self.hopper.get(f, t, links))
        self.setval("validOrigins", [])
        self.setval("linkState", [])
        self.setval("instance", RouterNode.instance)
        self.setval("cost", 1)
        self.setval("address", "amqp:/_topo/0/" + t)
        self.setval("id", t)
        self.setval("identity", self.value[Schema.i(self.name, "name")])

    def reset(self):
        RouterNode.nh = NextHop()

class Connector(Entity):
    def __init__(self, host, port):
        super(Connector, self).__init__("connector")
        self.init(host, port)

    def init(self, host, port):
        self.setval("verifyHostName", True)
        self.setval("cost", 1)
        self.setval("addr", "127.0.0.1")
        self.setval("maxSessions", 32768)
        self.setval("allowRedirect", True)
        self.setval("idleTimeoutSeconds", 16)
        self.setval("saslMechanisms", "AMONYMOUS")
        self.setval("maxFrameSize", 16384)
        self.setval("maxSessionFrames", 100)
        self.setval("host", host)
        self.setval("role", "inter-router")
        self.setval("stripAnnotations", "both")
        self.setval("port", port)
        self.setval("identity", "connector/" + host + ":" + port)
        self.setval("name", self.getval("identity"))

class Policy(Entity):
    def __init__(self):
        super(Policy, self).__init__("policy")
        self.init()

    def init(self):
        self.setval("connectionsProcessed", 2)
        self.setval("defaultVhost", "$default")
        self.setval("connectionsDenied", 0)
        self.setval("enableVhostPolicy", False)
        self.setval("maxConnections", 65535)
        self.setval("connectionsCurrent", self.getval("connectionsProcessed"))
        self.setval("identity", 1)
        self.setval("name", "policy/" + str(self.getval("identity")))

class Logs(Multiple):
    modules = ["AGENT", "CONTAINER", "DEFAULT", "ERROR", "MESSAGE", "POLICY", "ROUTER", "ROUTER_CORE", "ROUTER_HELLO",
               "ROUTER_LS", "ROUTER_MA", "SERVER"]

    def __init__(self):
        super(Logs, self).__init__()
        for module in Logs.modules:
            self.results.append(Log(module).vals())

class Log(Entity):
    def __init__(self, module):
        super(Log, self).__init__("log")
        self.init(module)

    def init(self, module):
        self.setval("name", "log/" + module)
        self.setval("identity", self.getval("name"))
        self.setval("module", module)

class Allocators(Multiple):
    names = [["qd_bitmask", 24], ["_buffer", 536], ["_composed_field", 64], ["_composite", 112], ["_connection", 232],
             ["_connector", 56], ["_deferred_call", 32], ["_field_iterator", 128], ["_hash_handle", 16],
             ["_hash_item", 32], ["_hash_segment", 24], ["_link", 48], ["_listener", 32], ["_log_entry", 2104],
             ["_management_context", 56], ["_message_context", 640], ["_message", 128], ["_node", 56],
             ["_parsed_field", 88], ["_timer", 56], ["_work_item", 24], ["pn_connector", 600], ["pn_listener", 48],
             ["r_action", 160], ["r_address_config", 56], ["r_address", 264], ["r_connection", 232],
             ["r_connection_work", 56], ["r_delivery_ref", 24], ["r_delivery", 144], ["r_field", 40],
             ["r_general_work", 64], ["r_link_ref", 24], ["r_link", 304], ["r_node", 64], ["r_query", 336],
             ["r_terminus", 64], ["tm_router", 16]]

    def __init__(self):
        super(Allocators, self).__init__()
        for name in Allocators.names:
            self.results.append(Allocator(name).vals())

class Allocator(Entity):
    def __init__(self, name):
        super(Allocator, self).__init__("allocator")
        self.init(name)

    def init(self, name):
        n = "qd" + name[0] + "_t"
        self.setZero(["heldByThreads", "transferBatchSize", "globalFreeListMax", "batchesRebalancedToGlobal", "batchesRebalancedToThreads",
                      "totalFreeToHeap", "totalAllocFromHeap", "localFreeListMax"])
        self.setval("name", "allocator/" + n)
        self.setval("identity", self.getval("name"))
        self.setval("typeName", n)
        self.setval("typeSize", name[1])

class RouterAddresses(Multiple):
    def __init__(self, node, nodes):
        super(RouterAddresses, self).__init__()

        addresses = {}
        others = []
        for n in nodes:
            if n['nodeType'] == 'inter-router':
                if n['name'] != node['name']:
                    self.results.append(RouterAddress("R"+n['name'], [n['name']], "closest", 0).vals())
                    others.append(n['name'])
            else:
                for normal in n['normals']:
                    nname = '.'.join(normal['name'].split('.')[:-1])
                    if "console_identifier" not in node['properties']:
                        maddr = "M0" + normal['addr']
                        if maddr not in addresses:
                            addresses[maddr] = []
                        if nname != node['name']:
                            if nname not in addresses[maddr]:
                                addresses[maddr].append(nname)

        for address in addresses:
            self.results.append(RouterAddress(address, addresses[address], "balanced", 0).vals())

        self.results.append(RouterAddress("L_$management_internal", [], "closest", 1).vals())
        self.results.append(RouterAddress("M0$management", [], "closest", 1).vals())
        self.results.append(RouterAddress("L$management", [], "closest", 1).vals())
        self.results.append(RouterAddress("L$qdhello", [], "flood", 1).vals())
        self.results.append(RouterAddress("L$qdrouter", [], "flood", 1).vals())
        self.results.append(RouterAddress("L$qdrouter.ma", [], "multicast", 1).vals())
        self.results.append(RouterAddress("Tqdrouter", others, "flood", 1).vals())
        self.results.append(RouterAddress("Tqdrouter.ma", others, "multicast", 1).vals())

class RouterAddress(Entity):
    def __init__(self, name, rhrList, distribution, inProcess):
        super(RouterAddress, self).__init__("router.address")
        self.init(name, rhrList, distribution, inProcess)

    def init(self, name, rhrList, distribution, inProcess):
        self.setZero(["subscriberCount", "deliveriesEgress", "deliveriesIngress",
                      "deliveriesFromContainer", "deliveriesTransit", "containerCount",
                      "trackedDeliveries", "deliveriesToContainer"])
        self.setval("name", name)
        self.setval("key", self.getval("name"))
        self.setval("distribution", distribution)
        self.setval("identity", self.getval("name"))
        self.setval("remoteHostRouters", rhrList)
        self.setval("remoteCount", len(rhrList))
        self.setval("inProcess", inProcess)

class Address(Entity):
    def __init__(self):
        super(Address, self).__init__("address")
        self.init()

    def init(self):
        self.setval("egressPhase", 0)
        self.setval("ingressPhase", 0)
        self.setval("prefix", "closest")
        self.setval("waypoint", False)
        self.setval("distribution", "closest")
        self.setval("identity", 1)
        self.setval("name", "address/" + str(self.getval("identity")))

class Router(Entity):
    def __init__(self, node):
        super(Router, self).__init__("router")
        self.init(node)

    def init(self, node):
        self.setval("mobileAddrMaxAge", 60)
        self.setval("raIntervalFlux", 4)
        self.setval("workerThreads", 4)
        self.setval("name", "router/" + node['name'])
        self.setval("helloInterval", 1)
        self.setval("area", 0)
        self.setval("helloMaxAge", 3)
        self.setval("remoteLsMaxAge", 60)
        self.setval("addrCount", 0)
        self.setval("raInterval", 30)
        self.setval("mode", "interior")
        self.setval("nodeCount", 0)
        self.setval("saslConfigName", "qdrouterd")
        self.setval("linkCount", 0)
        self.setval("id", node['name'])
        self.setval("identity", "router/" + node['name'])

class Listener(Entity):
    def __init__(self, port):
        super(Listener, self).__init__("listener")
        self.init(port)

    def init(self, port):
        self.setval("stripAnnotations", "both")
        self.setval("requireSsl", False)
        self.setval("idleTimeoutSeconds", 16)
        self.setval("cost", 1)
        self.setval("port", str(port))
        self.setval("addr", "0.0.0.0")
        self.setval("saslMechanisms", "ANONYMOUS")
        self.setval("requireEncryption", False)
        self.setval("linkCapacity", 4)
        self.setval("role", "normal")
        self.setval("authenticatePeer", False)
        self.setval("host", "::")
        self.setval("identity", "listener/:::" + str(port))
        self.setval("name", self.getval("identity"))
        self.setval("maxFrameSize", 16384)

class Connection(Entity):
    def __init__(self, node, id):
        super(Connection, self).__init__("connection")
        self.init(node, id)

    def init(self, node, id):
        if "container" not in node:
            self.setval("container", str(generate_uuid()))
        else:
            self.setval("container", node["container"])
        self.setval("opened", True)
        self.setval("name", "connection/0.0.0.0:" + str(id))
        self.setval("properties", node["properties"])
        self.setval("ssl", False)
        if "host" in node:
            self.setval("host", node["host"])
        else:
            self.setval("host", "0.0.0.0:20000")
        if "isEncrypted" not in node:
            self.setval("isEncrypted", False)
        else:
            self.setval("isEncrypted", node["isEncrypted"])
        if "user" not in node:
            self.setval("user", "anonymous")
        else:
            self.setval("user", node["user"])
        self.setval("role", node["nodeType"])
        self.setval("isAuthenticated", False)
        self.setval("identity", id)
        self.setval("dir", node["cdir"])

class RouterLink(Entity):
    def __init__(self, node, identity, ldir, owningAddr, linkType, connId):
        super(RouterLink, self).__init__("router.link")
        self.init(node, identity, ldir, owningAddr, linkType, connId)

    def init(self, node, identity, ldir, owningAddr, linkType, connId):
        linkUuid = str(generate_uuid())
        self.setval("name", linkUuid)
        self.setval("identity", identity)
        self.setval("linkName", linkUuid)
        self.setval("linkType", linkType)
        self.setval("linkDir", ldir)
        self.setval("owningAddr", owningAddr)
        self.setval("capacity", 250)
        self.setZero(["undeliveredCount", "unsettledCount", "deliveryCount", "presettledCount", "acceptedCount",
                      "rejectedCount", "releasedCount", "modifiedCount"])
        self.setval("connectionId", connId)
        self.setval("adminStatus", "enabled")
        self.setval("operStatus", "up")

Schema.init()
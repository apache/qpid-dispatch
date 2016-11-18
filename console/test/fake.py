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

import optparse
from proton import Endpoint, generate_uuid
from proton import Message
from proton.handlers import MessagingHandler
from proton.reactor import Container
import json
from pprint import pprint
import os
import string
import random
import shutil
from mock import Connection, RouterLink, Schema, Listener, RouterNode, Hopper, Router, Address, Policy, Connector, \
    RouterAddresses, Allocators, Logs
import pdb

def id_generator(size=6, chars=string.ascii_uppercase + string.digits):
    return ''.join(random.choice(chars) for _ in range(size))

class Manager(object):
    def __init__(self, topology, verbose):
        self.topology = topology
        self.verbose = verbose
        self.base = "topologies/"

    def operation(self, op, request):
        m = op.replace("-", "_")
        try:
            method = getattr(self, m)
        except AttributeError:
            print op + " is not implemented yet"
            return []
        if self.verbose:
            print "Got request " + op
        return method(request)

    def GET_LOG(self, request):
        return []

    def GET_SCHEMA(self, request):
        with open(self.base + "schema.json") as fp:
            data = json.load(fp)
            return data

    def GET_MGMT_NODES(self, request):
        onlyfiles = []
        if not os.path.exists(self.base + self.topology):
            os.makedirs(self.base + self.topology)

        for f in os.listdir(self.base + self.topology):
            if os.path.isfile(os.path.join(self.base + self.topology, f)):
                if os.path.splitext(f)[1] == ".json":
                    onlyfiles.append(unicode("amqp:/_topo_/0/" + os.path.splitext(f)[0] + "/$management"))
        '''
        onlyfiles = [
            unicode("amqp:/_topo/0/" + os.path.splitext(f)[0] + "/$management", "utf-8")
            for f in os.listdir(self.topology)
                if os.path.isfile(os.path.join(self.topology, f)) and os.path.splitext(f)[1] == ".json"
        ]
        '''
        if self.verbose:
            pprint (onlyfiles)
        return onlyfiles

    def QUERY(self, request):
        #pdb.set_trace()
        if not getattr(request, "address"):
            nodes = self.GET_MGMT_NODES(request)
            node = nodes[0]
        else:
            node = request.address
        print "node is"
        pprint (node)
        nid = node.split('/')[-2]
        fullentity = request.properties["entityType"]
        entity = fullentity[len("org.apache.qpid.dispatch"):]
        if self.verbose:
            pprint("nid is " + nid + " entity is " + entity)
        if not "arrtibuteNmaes" in request.body:
            requestedAttrs = []
        else:
            requestedAttrs = request.body["attributeNames"]
        if not os.path.isfile(self.base + self.topology + "/" + nid + ".json"):
            return {"results": [], "attributeNames": requestedAttrs}
        with open(self.base + self.topology + "/" + nid + ".json") as fp:
            data = json.load(fp)
            ent = data.get(entity, {'attributeNames': [], 'results': []})
            attributeNames = ent['attributeNames']
            allresults = ent['results']
            if len(requestedAttrs) == 0:
                requestedAttrs = attributeNames
            results = []
            for result in allresults:
                newresult = []
                for atr in requestedAttrs:
                    atrindex = attributeNames.index(atr)
                    if atrindex < 0 or atrindex >= len(result):
                        newresult.append('not found')
                    else:
                        newresult.append(result[atrindex])
                results.append(newresult)
            newdata = {"results": results, "attributeNames": requestedAttrs}

            if self.verbose:
                pprint(newdata)
            return newdata

    def LOAD(self, request):
        topology = request.properties["topology"]
        fname = self.base + topology + "/nodeslinks.dat"
        if not os.path.isfile(fname):
            if self.verbose:
                print "returning empty topology for " + topology
            return {"nodes": [], "links": [], "topology": topology}

        with open(fname) as fp:
            data = json.load(fp)
            return data

    def GET_TOPOLOGY(self, request):
        if self.verbose:
            pprint (self.topology)
        return unicode(self.topology)

    def GET_TOPOLOGY_LIST(self, request):
        return [unicode(f) for f in os.listdir(self.base) if os.path.splitext(f)[1] != ".json"]

    def SWITCH(self, request):
        self.topology = request.properties["topology"]
        return self.LOAD(request)

    def PUBLISH(self, request):
        nodes = request.properties["nodes"]
        links = request.properties["links"]
        topology = request.properties["topology"]
        if self.verbose:
            print("PUBLISHing to " + topology)
        shutil.rmtree(self.base + topology)

        if not os.path.exists(self.base + topology):
            os.makedirs(self.base + topology)

        with open(self.base + topology + "/nodeslinks.dat", "w+") as fp:
            fp.write(json.dumps({"nodes": nodes, "links": links, "topology": topology}, indent=2))

        port = 20001
        clients = {}
        connectionId = 1
        # cache any connections and links for clients first
        for node in nodes:
            if node['nodeType'] != 'inter-router':
                if not node['key'] in clients:
                    clients[node['key']] = {"connections": [], "links": [], "addresses": []}

                for normal in node["normals"]:
                    clients[node['key']]["connections"].append(Connection(node, connectionId).vals())
                    ldir = "in" if node['cdir'] == "in" else "out"
                    owningAddr = "M0" + normal['addr'] if "console_identifier" not in node['properties'] else ""
                    clients[node['key']]["links"].append(RouterLink(node, str(len(clients[node['key']]["links"])),
                                                                    ldir, owningAddr, "endpoint", connectionId).vals())
                    if node['cdir'] == "both":
                        otherAddr = "M0" + normal['addr'] if "console_identifier" not in node['properties'] \
                            else "Ltemp." + id_generator(15)
                        clients[node['key']]["links"].append(RouterLink(node,
                                                                        str(len(clients[node['key']]["links"])), "in",
                                                                        otherAddr, "endpoint", connectionId).vals())
                    connectionId += 1


        hopper = Hopper(self.verbose)
        for node in nodes:
            if node['nodeType'] == 'inter-router':
                nodeInfo = {}

                # this should be driven by the schema and not hard coded like this
                nname = node["name"]
                entities = ("connection", "router", "router.link", "router.node", "allocator",
                            "sslProfile", "autoLink", "linkRoute", "address", "policy", "log",
                            "vhost", "vhostStats", "listener", "router.address", "connector")
                for entity in entities:
                    savedAs = entity
                    if entity == "address":
                        savedAs = "router.config.address"
                    nodeInfo["."+savedAs] = {"results": [], "attributeNames": Schema.schema[entity]["attributeNames"]}

                # find all the other nodes that are linked to this node
                nodeCons = []
                nodeLinks = []
                # if the link source or target is this node's id
                for link in links:
                    # only process links to other routers
                    if link['cls'] != "small":
                        toNode = None
                        if link['source']['name'] == node['name']:
                            toNode = link['target']
                            toNode["cdir"] = "in"
                        if link['target']['name'] == node['name']:
                            toNode = link['source']
                            toNode["cdir"] = "out"
                        if toNode:
                            toNode["container"] = toNode["name"]
                            nodeCons.append(Connection(toNode, connectionId).vals())

                            nodeLinks.append(RouterLink(toNode, str(len(nodeLinks)+1), "in",
                                                        '', "router-control", connectionId).vals())
                            nodeLinks.append(RouterLink(toNode, str(len(nodeLinks)+1), "out",
                                                        '', "router-control", connectionId).vals())
                            nodeLinks.append(RouterLink(toNode, str(len(nodeLinks)+1), "in",
                                                        '', "inter-router", connectionId).vals())
                            nodeLinks.append(RouterLink(toNode, str(len(nodeLinks)+1), "out",
                                                        '', "inter-router", connectionId).vals())
                            connectionId += 1

                nodeInfo[".connection"]["results"] = nodeCons
                nodeInfo[".router.link"]["results"] = nodeLinks

                # add any connections and links for clients
                if node['key'] in clients:
                    nodeInfo[".connection"]["results"].extend(clients[node['key']]["connections"])
                    nodeInfo[".router.link"]["results"].extend(clients[node['key']]["links"])

                nodeInfo[".listener"]["results"].append(Listener(port).vals())
                port += 1

                nodeInfo[".router"]["results"].append(Router(node).vals())
                nodeInfo[".router.config.address"]["results"].append(Address().vals())
                nodeInfo[".policy"]["results"].append(Policy().vals())

                for connection in nodeInfo[".connection"]["results"]:
                    dir = connection[Schema.i("connection", "dir")]
                    if dir == "out":
                        hostIndex = Schema.i("connection", "host")
                        connhost, connport = connection[hostIndex].split(":")
                        nodeInfo[".connector"]["results"].append(Connector(connhost, connport).vals())

                for n in nodes:
                    if n['nodeType'] == 'inter-router':
                        nodeInfo[".router.node"]["results"].append(RouterNode(node['name'], n['name'], links, hopper).vals())

                nodeInfo[".router.address"]["results"] = RouterAddresses(node, nodes).vals()
                nodeInfo[".allocator"]["results"] = Allocators().vals()
                nodeInfo[".log"]["results"] = Logs().vals()

                with open(self.base + topology + "/" + nname + ".json", "w+") as fp:
                    fp.write(json.dumps(nodeInfo, indent=2, sort_keys=True))

        return "published"

class MockRouter(MessagingHandler):
    def __init__(self, url, topology, verbose):
        super(MockRouter, self).__init__()
        self.url = url
        self.manager = Manager(topology, verbose)
        self.senders = {}
        self.verbose = verbose

    def on_start(self, event):
        self.acceptor = event.container.listen(self.url)

    def on_link_opening(self, event):
        if event.link.is_sender:
            if event.link.remote_source.dynamic:
                if self.verbose:
                    print("opening dynamic sender")
                address = str(generate_uuid())
                event.link.source.address = address
            elif event.link.remote_source.address:
                if self.verbose:
                    print("opening remote_source address sender")
                event.link.source.address = event.link.remote_source.address
            else:
                print("received unknown sender link")
            self.senders[event.link.source.address] = event.link

        elif event.link.is_receiver:
            if self.verbose:
                print "got a receiver link"
            event.link.target.address = event.link.remote_target.address

    def on_message(self, event):
        ret = self.manager.operation(event.message.properties["operation"], event.message)
        m = Message(address=event.message.reply_to, body=ret,
            correlation_id=event.message.correlation_id,
            properties={"statusCode": 200} )
        self.senders[event.message.reply_to].send(m)

parser = optparse.OptionParser(usage="usage: %prog [options]")
parser.add_option("-a", "--address", default="localhost:5672",
                  help="address router listens on (default %default)")
parser.add_option("-t", "--topology", default="config-80",
                  help="which topology directory to return (default %default)")
parser.add_option("-v", "--verbose", default=False,
                  help="display requests and responses to stdout (default %default)")
opts, args = parser.parse_args()

try:
    Container(MockRouter(opts.address, opts.topology, opts.verbose)).run()
except KeyboardInterrupt: pass

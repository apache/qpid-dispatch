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

import argparse
from pprint import pprint
import os, sys, inspect
import string
import random
from glob import glob
from mock import *
import SimpleHTTPServer
import SocketServer
import json
import cStringIO

import pdb

def id_generator(size=6, chars=string.ascii_uppercase + string.digits):
    return ''.join(random.choice(chars) for _ in range(size))

get_class = lambda x: globals()[x]
sectionKeys = {"log": "module", "sslProfile": "name", "connector": "port", "listener": "port"}

# borrowed from qpid-dispatch/python/qpid_dispatch_internal/management/config.py
def _parse(lines):
    """Parse config file format into a section list"""
    begin = re.compile(r'([\w-]+)[ \t]*{') # WORD {
    end = re.compile(r'}')                 # }
    attr = re.compile(r'([\w-]+)[ \t]*:[ \t]*(.+)') # WORD1: VALUE
    pattern = re.compile(r'([\w-]+)[ \t]*:[ \t]*([\S]+).*')

    def sub(line):
        """Do substitutions to make line json-friendly"""
        line = line.strip()
        if line.startswith("#"):
            return ""
        # 'pattern:' is a special snowflake.  It allows '#' characters in
        # its value, so they cannot be treated as comment delimiters
        if line.split(':')[0].strip().lower() == "pattern":
            line = re.sub(pattern, r'"\1": "\2",', line)
        else:
            line = line.split('#')[0].strip()
            line = re.sub(begin, r'["\1", {', line)
            line = re.sub(end, r'}],', line)
            line = re.sub(attr, r'"\1": "\2",', line)
        return line

    js_text = "[%s]"%("\n".join([sub(l) for l in lines]))
    spare_comma = re.compile(r',\s*([]}])') # Strip spare commas
    js_text = re.sub(spare_comma, r'\1', js_text)
    # Convert dictionary keys to camelCase
    sections = json.loads(js_text)
    #Config.transform_sections(sections)
    return sections

class DirectoryConfigs(object):
    def __init__(self, path='./'):
        self.path = path
        self.configs = {}

        files = glob(path + '*.conf')
        for file in files:
            with open(file) as f:
                self.configs[file] = _parse(f)

    def asSection(self, s):
        cname = s[0][0].upper() + s[0][1:] + "Section"
        try:
            c = get_class(cname)
            return c(**s[1])
        except KeyError, e:
            return None

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
            return None
        if self.verbose:
            print "Got request " + op
        return method(request)

    def GET_LOG(self, request):
        return []

    def GET_SCHEMA(self, request):
        with open("schema.json") as fp:
            data = json.load(fp)
            return data

    def LOAD(self, request):
        topology = request["topology"]
        nodes = []
        links = []

        dc = DirectoryConfigs('./' + self.base + topology + '/')
        configs = dc.configs

        port_map = []
        for index, file in enumerate(configs):
            port_map.append({'connectors': [], 'listeners': []})
            node = {}
            for sect in configs[file]:
                section = dc.asSection(sect)
                if section:
                    if section.type == "router":
                        node["index"] = index
                        node["nodeType"] = unicode("inter-router")
                        node["name"] = section.entries["id"]
                        node["key"] = "amqp:/_topo/0/" + node["name"] + "/$management"
                        nodes.append(node)

                    elif section.type in sectionKeys:
                        # look for a host in a listener
                        if section.type == 'listener':
                            host = section.entries.get('host')
                            if host and 'host' not in node:
                                node['host'] = host

                        role = section.entries.get('role')
                        if role == 'inter-router':
                            # we are processing an inter-router listener or connector: so create a link
                            port = section.entries.get('port', 'amqp')
                            if section.type == 'listener':
                                port_map[index]['listeners'].append(port)
                            else:
                                port_map[index]['connectors'].append(port)
                        else:
                            if section.type+'s' not in node:
                                node[section.type+'s'] = {}
                            key = sectionKeys[section.type]
                            val = section.entries.get(key)
                            node[section.type+'s'][val] = section.entries

        for source, ports_for_this_routers in enumerate(port_map):
            for listener_port in ports_for_this_routers['listeners']:
                for target, ports_for_other_routers in enumerate(port_map):
                    if listener_port in ports_for_other_routers['connectors']:
                        links.append({'source': source, 'target': target, 'dir': unicode("in")})

        return {"nodes": nodes, "links": links, "topology": topology}

    def GET_TOPOLOGY(self, request):
        if self.verbose:
            pprint (self.topology)
        return unicode(self.topology)

    def GET_TOPOLOGY_LIST(self, request):
        return [unicode(f) for f in os.listdir(self.base) if os.path.isdir(self.base + f)]

    def SWITCH(self, request):
        self.topology = request["topology"]
        tdir = './' + self.base + self.topology + '/'
        if not os.path.exists(tdir):
            os.makedirs(tdir)
        return self.LOAD(request)

    def FIND_DIR(self, request):
        dir = request['relativeDir']
        files = request['fileList']
        # find a directory with this name that contains these files


    def SHOW_CONFIG(self, request):
        nodeIndex = request['nodeIndex']
        return self.PUBLISH(request, nodeIndex)

    def PUBLISH(self, request, nodeIndex=None):
        nodes = request["nodes"]
        links = request["links"]
        topology = request["topology"]
        settings = request["settings"]
        http_port = settings.get('http_port', 5675)
        listen_port = settings.get('internal_port', 2000)
        default_host = settings.get('default_host', '0.0.0.0')

        if nodeIndex and nodeIndex >= len(nodes):
            return "Node index out of range"

        if self.verbose:
            if nodeIndex is None:
                print("PUBLISHing to " + topology)
            else:
                print("Creating config for " + topology + " node " + nodes[nodeIndex]['name'])

        if nodeIndex is None:
            # remove all .conf files from the output dir. they will be recreated below possibly under new names
            for f in glob(self.base + topology + "/*.conf"):
                if self.verbose:
                    print "Removing", f
                os.remove(f)

        for link in links:
            s = nodes[link['source']]
            t = nodes[link['target']]
            # keep track of names so we can print them above the sections
            if 'listen_from' not in s:
                s['listen_from'] = []
            if 'conn_to' not in t:
                t['conn_to'] = []
            if 'conns' not in t:
                t['conns'] = []

            # make sure source node has a listener
            lport = listen_port
            lhost = s.get('host', default_host)
            s['listen_from'].append(t['name'])
            if 'listener' not in s:
                s['listener'] = listen_port
                listen_port += 1
            else:
                lport = s['listener']

            t['conns'].append({"port": lport, "host": lhost})
            t['conn_to'].append(s['name'])

        # now process all the routers
        for node in nodes:
            if node['nodeType'] == 'inter-router':
                if self.verbose:
                    print "------------- processing node", node["name"], "---------------"

                nname = node["name"]
                if nodeIndex is None:
                    config_fp = open(self.base + topology + "/" + nname + ".conf", "w+")
                else:
                    config_fp = cStringIO.StringIO()

                # add a router section in the config file
                r = RouterSection(**node)
                if not node.get('conns') and not node.get('listener'):
                    r.setEntry('mode', 'standalone')
                else:
                    r.setEntry('mode', 'interior')
                r.setEntry('id', node['name'])
                config_fp.write(str(r) + "\n")

                # write other sections
                for sectionKey in sectionKeys:
                    if sectionKey+'s' in node:
                        for k in node[sectionKey+'s']:
                            o = node[sectionKey+'s'][k]
                            cname = sectionKey[0].upper() + sectionKey[1:] + "Section"
                            c = get_class(cname)
                            if sectionKey == "listener" and o['port'] != 'amqp' and int(o['port']) == http_port:
                                config_fp.write("\n# Listener for a console\n")
                            config_fp.write(str(c(**o)) + "\n")

                if 'listener' in node:
                    lhost = node.get('host', default_host)
                    listenerSection = ListenerSection(node['listener'], **{'host': lhost, 'role': 'inter-router'})
                    if 'listen_from' in node and len(node['listen_from']) > 0:
                        config_fp.write("\n# listener for connectors from " + ', '.join(node['listen_from']) + "\n")
                    config_fp.write(str(listenerSection) + "\n")

                if 'conns' in node:
                    for idx, conns in enumerate(node['conns']):
                        conn_port = conns['port']
                        conn_host = conns['host']
                        connectorSection = ConnectorSection(conn_port, **{'host': conn_host, 'role': 'inter-router'})
                        if 'conn_to' in node and len(node['conn_to']) > idx:
                            config_fp.write("\n# connect to " + node['conn_to'][idx] + "\n")
                        config_fp.write(str(connectorSection) + "\n")

                # return requested config file as string
                if node.get('index', -1) == nodeIndex:
                    val = config_fp.getvalue()
                    config_fp.close()
                    return val

                config_fp.close()

        return "published"

class HttpHandler(SimpleHTTPServer.SimpleHTTPRequestHandler):
    # use GET requests to serve the web pages
    def do_GET(self):
        SimpleHTTPServer.SimpleHTTPRequestHandler.do_GET(self);

    # use PORT requests to send commands
    def do_POST(self):
        content_len = int(self.headers.getheader('content-length', 0))
        if content_len > 0:
            body = self.rfile.read(content_len)
            data = json.loads(body)
            response = self.server.manager.operation(data['operation'], data)
            if response is not None:
                self.send_response(200)
                self.send_header('Content-Type', 'application/json')
                self.end_headers()

                self.wfile.write(json.dumps(response));
                self.wfile.close();
        else:
            return SimpleHTTPServer.SimpleHTTPRequestHandler.do_POST(self)

    # only log if verbose was requested
    def log_request(self, code='-', size='-'):
        if self.server.verbose:
            self.log_message('"%s" %s %s', self.requestline, str(code), str(size))

class ConfigTCPServer(SocketServer.TCPServer):
    def __init__(self, port, manager, verbose):
        SocketServer.TCPServer.__init__(self, ("", port), HttpHandler)
        self.manager = manager
        self.verbose = verbose

Schema.init()
parser = argparse.ArgumentParser(description='Read/Write Qpid Dispatch Router config files.')
parser.add_argument('-p', "--port", type=int, default=8000, help='port to listen for requests from browser')
parser.add_argument('-v', "--verbose", action='store_true', help='verbose output')
parser.add_argument("-t", "--topology", default="config-2", help="which topology to load (default: %(default)s)")
args = parser.parse_args()

try:
    httpd = ConfigTCPServer(args.port, Manager(args.topology, args.verbose), args.verbose)
    print "serving at port", args.port
    httpd.serve_forever()
except KeyboardInterrupt:
    pass
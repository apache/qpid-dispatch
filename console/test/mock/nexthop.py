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

import collections

class TreeNode(object):
    def __init__(self, f, parent):
        self.name = f
        self.parent = parent
        self.children = []
        self.visited = False

    def procreate(self, links):
        if self.visited:
            return
        self.visited = True
        for link in links:
            if link['source']['nodeType'] == 'inter-router' and link['target']['nodeType'] == 'inter-router':
                if (link['source']['name'] == self.name or link['target']['name'] == self.name):
                    name = link['source']['name'] if link['target']['name'] == self.name else link['target']['name']
                    if not name in self.ancestors():
                        self.children.append(TreeNode(name, self))

    def ancestors(self):
        a = self.geneology(self.parent)
        a.reverse()
        return a

    def geneology(self, parent):
        if parent is None:
            return []
        ret = [parent.name]
        ret.extend(self.geneology(parent.parent))
        return ret

class Hopper(object):
    def __init__(self, verbose):
        self.tree = {}
        self.table = {}
        self.verbose = verbose

    def get(self, f, t, links):
        if self.verbose:
            print ("------- asked to get " + f + " to " + t)
        if f in self.table and t in self.table[f]:
            if self.verbose:
                print " ------- returning existing " + str(self.table[f][t])
            return self.table[f][t]

        self.tree = {}
        #treef = self.highest(f)
        #if treef is None:
        treef = self.root(f)

        q = collections.deque([treef])
        while len(q):
            node = q.popleft()
            self.process(f, node, treef.name, links)
            if f in self.table and t in self.table[f]:
                if self.verbose:
                    print " ------- returning " + str(self.table[f][t])
                ret = self.table[f][t]
                #self.table = {}
                return ret
            for n in node.children:
                q.append(n)
        if self.verbose:
            print (" ------- returning unfound nextHop of None")

    def process(self, f, node, r, links):
        node.procreate(links)
        for n in node.children:
            self.populateTable(f, n, r)

    def populateTable(self, f, node, r):
        n = node.name
        if not f in self.table:
            self.table[f] = {}
            self.table[f][f] = None
        if not n in self.table:
            self.table[n] = {}
            self.table[n][n] = None
        if not node.parent:
            return
        if node.parent.name == f:
            self.table[f][n] = None
            self.table[n][f] = None
        else:
            def setHop(n, a, p):
                if not a in self.table[n]:
                    self.table[n][a] = p

            def loop(ancestors):
                for i in range(len(ancestors)):
                    start = ancestors[i]
                    for j in range(i+1, len(ancestors)):
                        stop = ancestors[j]
                        if j-i == 1:
                            setHop(start, stop, None)
                        else:
                            setHop(start, stop, ancestors[i+1])


            ancestors = node.ancestors()
            while len(ancestors) > 0 and ancestors[0] != r:
                ancestors.pop(0)
            ancestors.append(n)
            loop(ancestors)
            ancestors.reverse()
            loop(ancestors)

    def root(self, f):
        if not self.tree:
            self.tree[f] = TreeNode(f, None)
        return self.tree[list(self.tree.keys())[0]]

    def highest(self, f):
        r = self.root(f)
        if r.name == f:
            return r
        q = collections.deque([r])
        while len(q):
            node = q.popleft()
            for n in node.children:
                if n.name == f:
                    return n
                q.append(n)
        return None

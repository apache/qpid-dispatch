/*
Licensed to the Apache Software Foundation (ASF) under one
or more contributor license agreements.  See the NOTICE file
distributed with this work for additional information
regarding copyright ownership.  The ASF licenses this file
to you under the Apache License, Version 2.0 (the
"License"); you may not use this file except in compliance
with the License.  You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing,
software distributed under the License is distributed on an
"AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
KIND, either express or implied.  See the License for the
specific language governing permissions and limitations
under the License.
*/

class Link {
  constructor(source, target, dir, cls, uid) {
    this.source = source;
    this.target = target;
    this.left = dir != 'out';
    this.right = (dir == 'out' || dir == 'both');
    this.cls = cls;
    this.uid = uid;
  }
}

export class Links {
  constructor(QDRService, logger) {
    this.links = [];
    this.QDRService = QDRService;
    this.logger = logger;
  }
  getLinkSource (nodesIndex) {
    for (let i=0; i<this.links.length; ++i) {
      if (this.links[i].target === nodesIndex)
        return i;
    }
    return -1;
  }
  getLink(_source, _target, dir, cls, uid) {
    for (let i = 0; i < this.links.length; i++) {
      let s = this.links[i].source,
        t = this.links[i].target;
      if (typeof this.links[i].source == 'object') {
        s = s.id;
        t = t.id;
      }
      if (s == _source && t == _target) {
        return i;
      }
      // same link, just reversed
      if (s == _target && t == _source) {
        return -i;
      }
    }
    //this.logger.debug("creating new link (" + (links.length) + ") between " + nodes[_source].name + " and " + nodes[_target].name);
    if (this.links.some( function (l) { return l.uid === uid;}))
      uid = uid + '.' + this.links.length;
    return this.links.push(new Link(_source, _target, dir, cls, uid)) - 1;
  }
  linkFor (source, target) {
    for (let i = 0; i < this.links.length; ++i) {
      if ((this.links[i].source == source) && (this.links[i].target == target))
        return this.links[i];
      if ((this.links[i].source == target) && (this.links[i].target == source))
        return this.links[i];
    }
    // the selected node was a client/broker
    return null;
  }


  initializeLinks (nodeInfo, nodes, unknowns, localStorage, height) {
    let animate = false;
    let source = 0;
    let client = 1.0;
    for (let id in nodeInfo) {
      let onode = nodeInfo[id];
      if (!onode['connection'])
        continue;
      let conns = onode['connection'].results;
      let attrs = onode['connection'].attributeNames;
      //QDRLog.debug("external client parent is " + parent);
      let normalsParent = {}; // 1st normal node for this parent

      for (let j = 0; j < conns.length; j++) {
        let connection = this.QDRService.utilities.flatten(attrs, conns[j]);
        let role = connection.role;
        let properties = connection.properties || {};
        let dir = connection.dir;
        if (role == 'inter-router') {
          let connId = connection.container;
          let target = getContainerIndex(connId, nodeInfo, this.QDRService);
          if (target >= 0) {
            this.getLink(source, target, dir, '', source + '-' + target);
          }
        } /* else if (role == "normal" || role == "on-demand" || role === "route-container")*/ {
          // not an connection between routers, but an external connection
          let name = this.QDRService.utilities.nameFromId(id) + '.' + connection.identity;

          // if we have any new clients, animate the force graph to position them
          let position = localStorage[name] ? JSON.parse(localStorage[name]) : undefined;
          if ((typeof position == 'undefined')) {
            animate = true;
            position = {
              x: Math.round(nodes.get(source).x + 40 * Math.sin(client / (Math.PI * 2.0))),
              y: Math.round(nodes.get(source).y + 40 * Math.cos(client / (Math.PI * 2.0))),
              fixed: false
            };
            //QDRLog.debug("new client pos (" + position.x + ", " + position.y + ")")
          }// else QDRLog.debug("using previous location")
          if (position.y > height) {
            position.y = Math.round(nodes.get(source).y + 40 + Math.cos(client / (Math.PI * 2.0)));
          }
          let existingNodeIndex = nodes.nodeExists(connection.container);
          let normalInfo = nodes.normalExists(connection.container);
          let node = nodes.getOrCreateNode(id, name, role, nodeInfo, nodes.getLength(), position.x, position.y, connection.container, j, position.fixed, properties);
          let nodeType = this.QDRService.utilities.isAConsole(properties, connection.identity, role, node.key) ? 'console' : 'client';
          let cdir = getLinkDir(id, connection, onode, this.QDRService);
          if (existingNodeIndex >= 0) {
            // make a link between the current router (source) and the existing node
            this.getLink(source, existingNodeIndex, dir, 'small', connection.name);
          } else if (normalInfo.nodesIndex) {
            // get node index of node that contained this connection in its normals array
            let normalSource = this.getLinkSource(normalInfo.nodesIndex);
            if (normalSource >= 0) {
              if (cdir === 'unknown')
                cdir = dir;
              node.cdir = cdir;
              nodes.add(node);
              // create link from original node to the new node
              this.getLink(this.links[normalSource].source, nodes.getLength()-1, cdir, 'small', connection.name);
              // create link from this router to the new node
              this.getLink(source, nodes.getLength()-1, cdir, 'small', connection.name);
              // remove the old node from the normals list
              nodes.get(normalInfo.nodesIndex).normals.splice(normalInfo.normalsIndex, 1);
            }
          } else if (role === 'normal') {
          // normal nodes can be collapsed into a single node if they are all the same dir
            if (cdir !== 'unknown') {
              node.user = connection.user;
              node.isEncrypted = connection.isEncrypted;
              node.host = connection.host;
              node.connectionId = connection.identity;
              node.cdir = cdir;
              // determine arrow direction by using the link directions
              if (!normalsParent[nodeType+cdir]) {
                normalsParent[nodeType+cdir] = node;
                nodes.add(node);
                node.normals = [node];
                // now add a link
                this.getLink(source, nodes.getLength() - 1, cdir, 'small', connection.name);
                client++;
              } else {
                normalsParent[nodeType+cdir].normals.push(node);
              }
            } else {
              node.id = nodes.getLength() - 1 + unknowns.length;
              unknowns.push(node);
            }
          } else {
            nodes.add(node);
            // now add a link
            this.getLink(source, nodes.getLength() - 1, dir, 'small', connection.name);
            client++;
          }
        }
      }
      source++;
    }
    return animate;
  }
  clearHighlighted () {
    for (let i = 0; i < this.links.length; ++i) {
      this.links[i].highlighted = false;
    }
  }
}

var getContainerIndex = function (_id, nodeInfo, QDRService) {
  let nodeIndex = 0;
  for (let id in nodeInfo) {
    if (QDRService.utilities.nameFromId(id) === _id)
      return nodeIndex;
    ++nodeIndex;
  }
  return -1;
};

var getLinkDir = function (id, connection, onode, QDRService) {
  let links = onode['router.link'];
  if (!links) {
    return 'unknown';
  }
  let inCount = 0, outCount = 0;
  links.results.forEach( function (linkResult) {
    let link = QDRService.utilities.flatten(links.attributeNames, linkResult);
    if (link.linkType === 'endpoint' && link.connectionId === connection.identity)
      if (link.linkDir === 'in')
        ++inCount;
      else
        ++outCount;
  });
  if (inCount > 0 && outCount > 0)
    return 'both';
  if (inCount > 0)
    return 'in';
  if (outCount > 0)
    return 'out';
  return 'unknown';
};


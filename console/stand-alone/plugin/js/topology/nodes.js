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

export class Node {
  constructor(id, name, nodeType, properties, routerId, x, y, nodeIndex, resultIndex, fixed, connectionContainer) {
    this.key = id;
    this.name = name;
    this.nodeType = nodeType;
    this.properties = properties;
    this.routerId = routerId;
    this.x = x;
    this.y = y;
    this.id = nodeIndex;
    this.resultIndex = resultIndex;
    this.fixed = !!+fixed;
    this.cls = '';
    this.container = connectionContainer;
  }
}

export class Nodes {
  constructor(QDRService, logger) {
    this.nodes = [];
    this.QDRService = QDRService;
    this.logger = logger;
  }
  getLength () {
    return this.nodes.length;
  }
  get (index) {
    if (index < this.getLength()) {
      return this.nodes[index];
    }
    this.logger.error(`Attempted to get node[${index}] but there were only ${this.getLength()} nodes`);
    return undefined;
  }
  setNodesFixed (name, b) {
    this.nodes.some(function (n) {
      if (n.name === name) {
        n.fixed = b;
        return true;
      }
    });
  }
  nodeFor (name) {
    for (let i = 0; i < this.nodes.length; ++i) {
      if (this.nodes[i].name == name)
        return this.nodes[i];
    }
    return null;
  }
  nodeExists (connectionContainer) {
    return this.nodes.findIndex( function (node) {
      return node.container === connectionContainer;
    });
  }
  normalExists (connectionContainer) {
    let normalInfo = {};
    for (let i=0; i<this.nodes.length; ++i) {
      if (this.nodes[i].normals) {
        if (this.nodes[i].normals.some(function (normal, j) {
          if (normal.container === connectionContainer && i !== j) {
            normalInfo = {nodesIndex: i, normalsIndex: j};
            return true;
          }
          return false;
        }))
          break;
      }
    }
    return normalInfo;
  }
  savePositions () {
    this.nodes.forEach( function (d) {
      localStorage[d.name] = JSON.stringify({
        x: Math.round(d.x),
        y: Math.round(d.y),
        fixed: (d.fixed & 1) ? 1 : 0,
      });
    });
  }
  find (connectionContainer, properties, name) {
    properties = properties || {};
    for (let i=0; i<this.nodes.length; ++i) {
      if (this.nodes[i].name === name || this.nodes[i].container === connectionContainer) {
        if (properties.product)
          this.nodes[i].properties = properties;
        return this.nodes[i];
      }
    }
    return undefined;
  }
  getOrCreateNode (id, name, nodeType, nodeInfo, nodeIndex, x, y, 
    connectionContainer, resultIndex, fixed, properties) {
    properties = properties || {};
    let gotNode = this.find(connectionContainer, properties, name);
    if (gotNode) {
      return gotNode;
    }
    let routerId = this.QDRService.utilities.nameFromId(id);
    return new Node(id, name, nodeType, properties, routerId, x, y, 
      nodeIndex, resultIndex, fixed, connectionContainer);
  }
  add (obj) {
    this.nodes.push(obj);
    return obj;
  }
  addUsing (id, name, nodeType, nodeInfo, nodeIndex, x, y, 
    connectContainer, resultIndex, fixed, properties) {
    let obj = this.getOrCreateNode(id, name, nodeType, nodeInfo, nodeIndex, x, y, 
      connectContainer, resultIndex, fixed, properties);
    this.nodes.push(obj);
    return obj;
  }
  clearHighlighted () {
    for (let i = 0; i<this.nodes.length; ++i) {
      this.nodes[i].highlighted = false;
    }
  }
  initialize (nodeInfo, localStorage, width, height) {
    let nodeCount = Object.keys(nodeInfo).length;
    let yInit = 50;
    let animate = false;
    for (let id in nodeInfo) {
      let name = this.QDRService.utilities.nameFromId(id);
      // if we have any new nodes, animate the force graph to position them
      let position = localStorage[name] ? JSON.parse(localStorage[name]) : undefined;
      if (!position) {
        animate = true;
        position = {
          x: Math.round(width / 4 + ((width / 2) / nodeCount) * this.nodes.length),
          y: Math.round(height / 2 + Math.sin(this.nodes.length / (Math.PI*2.0)) * height / 4),
          fixed: false,
        };
      }
      if (position.y > height) {
        position.y = 200 - yInit;
        yInit *= -1;
      }
      this.addUsing(id, name, 'inter-router', nodeInfo, this.nodes.length, position.x, position.y, name, undefined, position.fixed, {});
    }
    return animate;
  }

}


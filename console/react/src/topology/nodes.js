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

import { utils } from "../common/amqp/utilities.js";
import * as d3 from "d3";

export class Node {
  constructor(
    id,
    name,
    nodeType,
    properties,
    routerId,
    x,
    y,
    nodeIndex,
    resultIndex,
    fixed,
    connectionContainer
  ) {
    this.key = id; // the router uri for this node (or group of clients) like: amqp:/_topo/0/<router id>/$management
    this.name = name; // the router id portion of the key
    this.nodeType = nodeType; // router.role
    this.properties = properties;
    this.routerId = routerId; // the router uri of the router we are connected to (for groups)
    this.x = x;
    this.y = y;
    this.id = nodeIndex;
    this.resultIndex = resultIndex;
    this.fixed = fixed ? true : false;
    this.cls = "";
    this.container = connectionContainer;
    this.isConsole = utils.isConsole(this);
    this.isArtemis = utils.isArtemis(this);
  }
  title(hide) {
    let x = "";
    if (this.normals && this.normals.length > 1 && !hide) x = " x " + this.normals.length;
    if (this.isConsole) return "Dispatch console" + x;
    else if (this.isArtemis) return "Broker - Artemis" + x;
    else if (this.properties.product === "qpid-cpp") return "Broker - qpid-cpp" + x;
    else if (this.nodeType === "edge") return "Edge Router";
    else if (this.cdir === "in") return "Sender" + x;
    else if (this.cdir === "out") return "Receiver" + x;
    else if (this.cdir === "both") return "Sender/Receiver" + x;
    else if (this.nodeType === "normal") return "client" + x;
    else if (this.nodeType === "on-demand") return "broker";
    else if (this.properties.product) {
      return this.properties.product;
    } else {
      return "";
    }
  }
  toolTip(topology, verbose) {
    return new Promise(
      function(resolve) {
        if (this.nodeType === "normal" || this.nodeType === "edge") {
          resolve(this.clientTooltip());
        } else
          this.routerTooltip(topology, verbose).then(function(toolTip) {
            resolve(toolTip);
          });
      }.bind(this)
    );
  }

  clientTooltip() {
    let type = this.title(true);
    let title = "";
    title += `<table class="popupTable"><tr><td>Type</td><td>${type}</td></tr>`;
    if (!this.normals || this.normals.length < 2)
      title += `<tr><td>Host</td><td>${this.host}</td></tr>`;
    else {
      title += `<tr><td>Count</td><td>${this.normals.length}</td></tr>`;
    }
    if (!this.isConsole && !this.isArtemis)
      title +=
        '<tr><td colspan=2 class="more-info">Click circle for more info</td></tr></table>';
    return title;
  }

  routerTooltip(topology, verbose) {
    return new Promise(
      function(resolve) {
        topology.ensureEntities(
          this.key,
          [
            {
              entity: "listener",
              attrs: ["role", "port", "http"]
            },
            {
              entity: "router"
            }
          ],
          function(foo, nodes) {
            // update all the router title text
            let node = nodes[this.key];
            const err = `<table class="popupTable"><tr><td>Error</td><td>Unable to get router info for ${this.key}</td></tr></table>`;
            if (!node) {
              resolve(err);
              return;
            }
            let listeners = node["listener"];
            let router = node["router"];
            if (!listeners || !router) {
              resolve(err);
              return;
            }
            let r = utils.flatten(router.attributeNames, router.results[0]);
            let title = '<table class="popupTable">';
            title += "<tr><td>Router</td><td>" + r.name + "</td></tr>";
            if (r.hostName)
              title += "<tr><td>Host Name</td><td>" + r.hostHame + "</td></tr>";
            title += "<tr><td>Version</td><td>" + r.version + "</td></tr>";
            let ports = [];
            for (let l = 0; l < listeners.results.length; l++) {
              let listener = utils.flatten(
                listeners.attributeNames,
                listeners.results[l]
              );
              if (listener.role === "normal") {
                ports.push(listener.port + "");
              }
            }
            if (ports.length > 0) {
              title += "<tr><td>Ports</td><td>" + ports.join(", ") + "</td></tr>";
            }
            // add verbose rows
            if (verbose) {
              title += "<tr><td>Addresses</td><td>" + r.addrCount + "</td></tr>";
              title += "<tr><td>Connections</td><td>" + r.connectionCount + "</td></tr>";
              title += "<tr><td>Links</td><td>" + r.linkCount + "</td></tr>";
              title += "<tr><td>Auto links</td><td>" + r.autoLinkCount + "</td></tr>";
              title += "<tr><td>Link routes</td><td>" + r.linkRouteCount + "</td></tr>";
            }
            title += "</table>";
            resolve(title);
            return title;
          }.bind(this)
        );
      }.bind(this)
    );
  }
  radius() {
    return nodeProperties[this.nodeType].radius;
  }
  uid() {
    if (!this.uuid) this.uuid = `${this.container}`;
    return this.normals ? `${this.uuid}-${this.normals.length}` : this.uuid;
  }
  setFixed(fixed) {
    if (!fixed & 1) this.lat = this.lon = null;
    this.fixed = fixed & 1 ? true : false;
  }
  isFixed() {
    return this.fixed & 1 ? true : false;
  }
}
const nodeProperties = {
  // router types
  "inter-router": {
    radius: 28,
    refX: {
      end: 32,
      start: -19
    },
    linkDistance: [150, 70],
    charge: [-1800, -900]
  },
  edge: {
    radius: 30,
    refX: {
      end: 34,
      start: -21
    },
    linkDistance: [90, 55],
    charge: [-1350, -900]
  },
  _edge: {
    radius: 24,
    refX: {
      end: 24,
      start: -17
    },
    linkDistance: [90, 55],
    charge: [-1350, -900]
  },
  // generated nodes from connections. key is from connection.role
  normal: {
    radius: 15,
    refX: {
      end: 20,
      start: -7
    },
    linkDistance: [75, 40],
    charge: [-900, -900]
  }
};
// aliases
nodeProperties._topo = nodeProperties["inter-router"];
nodeProperties["on-demand"] = nodeProperties["normal"];
nodeProperties["route-container"] = nodeProperties["normal"];

export class Nodes {
  constructor(logger) {
    this.nodes = [];
  }
  static radius(type) {
    if (nodeProperties[type].radius) return nodeProperties[type].radius;
    return 15;
  }
  static textOffset(type, len) {
    let r = Nodes.radius(type);
    if (type === "inter-router" || type === "_topo" || type === "_edge") {
      if (len > 4) return -(r - 5);
    }
    return 0;
  }
  static maxRadius() {
    let max = 0;
    for (let key in nodeProperties) {
      max = Math.max(max, nodeProperties[key].radius);
    }
    return max;
  }
  static refX(end, r) {
    for (let key in nodeProperties) {
      if (nodeProperties[key].radius === parseInt(r))
        return nodeProperties[key].refX[end];
    }
    return 0;
  }
  // return all possible values of node radii
  static discrete() {
    let values = {};
    for (let key in nodeProperties) {
      values[nodeProperties[key].radius] = true;
    }
    return Object.keys(values);
  }
  // vary the following force graph attributes based on nodeCount
  static forceScale(nodeCount, minmax) {
    let count = Math.max(Math.min(nodeCount, 80), 6);
    let x = d3.scale
      .linear()
      .domain([6, 80])
      .range(minmax);
    return x(count);
  }
  linkDistance(d, nodeCount) {
    let range = nodeProperties[d.target.nodeType].linkDistance;
    return Nodes.forceScale(nodeCount, range);
  }
  charge(d, nodeCount) {
    let charge = nodeProperties[d.nodeType].charge;
    return Nodes.forceScale(nodeCount, charge);
  }
  gravity(d, nodeCount) {
    return Nodes.forceScale(nodeCount, [0.0001, 0.1]);
  }
  setFixed(d, fixed) {
    let n = this.find(d.container, d.properties, d.name);
    if (n) {
      n.fixed = fixed;
    }
    d.setFixed(fixed);
  }
  getLength() {
    return this.nodes.length;
  }
  get(index) {
    if (index < this.getLength()) {
      return this.nodes[index];
    }
    console.log(
      `Attempted to get node[${index}] but there were only ${this.getLength()} nodes`
    );
    return undefined;
  }
  nodeFor(name) {
    for (let i = 0; i < this.nodes.length; ++i) {
      if (this.nodes[i].name === name) return this.nodes[i];
    }
    return null;
  }
  nodeExists(connectionContainer) {
    return this.nodes.findIndex(function(node) {
      return node.container === connectionContainer;
    });
  }
  normalExists(connectionContainer) {
    let normalInfo = {};
    const exists = i =>
      this.nodes[i].normals.some((normal, j) => {
        if (normal.container === connectionContainer && i !== j) {
          normalInfo = {
            nodesIndex: i,
            normalsIndex: j
          };
          return true;
        }
        return false;
      });

    for (let i = 0; i < this.nodes.length; ++i) {
      if (this.nodes[i].normals) {
        if (exists(i)) break;
      }
    }
    return normalInfo;
  }
  savePositions(nodes) {
    if (!nodes) nodes = this.nodes;
    if (Object.prototype.toString.call(nodes) !== "[object Array]") {
      nodes = [nodes];
    }
    this.nodes.forEach(function(d) {
      localStorage[d.name] = JSON.stringify({
        x: Math.round(d.x),
        y: Math.round(d.y),
        fixed: d.fixed
      });
    });
  }
  // Convert node's x,y coordinates to longitude, lattitude
  saveLonLat(backgroundMap, nodes) {
    if (!backgroundMap || !backgroundMap.initialized) return;
    // didn't pass nodes, use all nodes
    if (!nodes) nodes = this.nodes;
    // passed a single node, wrap it in an array
    if (Object.prototype.toString.call(nodes) !== "[object Array]") {
      nodes = [nodes];
    }
    for (let i = 0; i < nodes.length; i++) {
      let n = nodes[i];
      if (n.fixed) {
        let lonlat = backgroundMap.getLonLat(n.x, n.y);
        if (lonlat) {
          n.lon = lonlat[0];
          n.lat = lonlat[1];
        }
      } else {
        n.lon = n.lat = null;
      }
    }
  }
  // convert all nodes' longitude,lattitude to x,y coordinates
  setXY(backgroundMap) {
    if (!backgroundMap) return;
    for (let i = 0; i < this.nodes.length; i++) {
      let n = this.nodes[i];
      if (n.lon && n.lat) {
        let xy = backgroundMap.getXY(n.lon, n.lat);
        if (xy) {
          n.x = n.px = xy[0];
          n.y = n.py = xy[1];
        }
      }
    }
  }

  find(connectionContainer, properties, name) {
    properties = properties || {};
    for (let i = 0; i < this.nodes.length; ++i) {
      if (
        this.nodes[i].name === name ||
        this.nodes[i].container === connectionContainer
      ) {
        if (properties.product) this.nodes[i].properties = properties;
        return this.nodes[i];
      }
    }
    return undefined;
  }
  getOrCreateNode = nodeObj => {
    const {
      id,
      name,
      nodeType,
      nodeIndex,
      x,
      y,
      connectionContainer,
      resultIndex,
      fixed,
      properties
    } = nodeObj;
    const props = properties || {};
    let gotNode = this.find(connectionContainer, props, name);
    if (gotNode) {
      return gotNode;
    }
    let routerId = utils.nameFromId(id);
    return new Node(
      id,
      name,
      nodeType,
      props,
      routerId,
      x,
      y,
      nodeIndex,
      resultIndex,
      fixed,
      connectionContainer
    );
  };
  add(obj) {
    this.nodes.push(obj);
    return obj;
  }

  addUsing = nodeInfo => {
    let obj = this.getOrCreateNode(nodeInfo);
    return this.add(obj);
  };

  clearHighlighted() {
    for (let i = 0; i < this.nodes.length; ++i) {
      this.nodes[i].highlighted = false;
    }
  }

  initialize(nodeInfo, width, height, localStorage) {
    this.nodes.length = 0;
    let nodeCount = Object.keys(nodeInfo).length;
    let yInit = 50;
    let animate = false;
    for (let id in nodeInfo) {
      let name = utils.nameFromId(id);
      // if we have any new nodes, animate the force graph to position them
      let position = localStorage[name] ? JSON.parse(localStorage[name]) : undefined;
      if (!position) {
        animate = true;
        position = {
          x: Math.round(width / 4 + (width / 2 / nodeCount) * this.nodes.length),
          y: Math.round(
            height / 2 + (Math.sin(this.nodes.length / (Math.PI * 2.0)) * height) / 4
          ),
          fixed: false
        };
      }
      if (position.y > height) {
        position.y = 200 - yInit;
        yInit *= -1;
      }
      position.fixed = position.fixed ? true : false;
      let parts = id.split("/");
      this.addUsing({
        id,
        name,
        nodeType: parts[1],
        nodeIndex: this.nodes.length,
        x: position.x,
        y: position.y,
        connectionContainer: name,
        resultIndex: undefined,
        fixed: position.fixed,
        properties: {}
      });
    }
    return animate;
  }
}

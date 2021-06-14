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

class Link {
  constructor(source, target, dir, cls, uid, suid, tuid) {
    this.source = source;
    this.target = target;
    this.suid = suid;
    this.tuid = tuid;
    this.left = dir === "in" || dir === "both";
    this.right = dir === "out" || dir === "both";
    this.cls = cls;
    this.uuid = uid;
  }

  markerId(end) {
    let selhigh = this.highlighted ? "highlighted" : this.selected ? "selected" : "";
    if (selhigh === "" && !this.left && !this.right) selhigh = "unknown";
    return `-${selhigh}-${end === "end" ? this.target.radius() : this.source.radius()}`;
  }

  uid() {
    return this.uuid;
  }
}

export class Links {
  constructor(logger) {
    this.links = [];
    this.logger = logger;
  }

  reset() {
    this.links.length = 0;
  }

  getLinkSource(nodesIndex) {
    for (let i = 0; i < this.links.length; ++i) {
      if (this.links[i].target === nodesIndex) return i;
    }
    return -1;
  }

  getLink(_source, _target, dir, cls, suid, tuid) {
    for (let i = 0; i < this.links.length; i++) {
      let s = this.links[i].source,
        t = this.links[i].target;
      if (typeof this.links[i].source === "object") {
        s = s.id;
        t = t.id;
      }
      if (s === _source && t === _target) {
        return i;
      }
      // same link, just reversed
      if (s === _target && t === _source) {
        return -i;
      }
    }
    let uid = `${suid}-${tuid}`;
    if (this.links.some(l => l.uid() === uid)) {
      uid = uid + "." + this.links.length;
    }
    return this.links.push(new Link(_source, _target, dir, cls, uid, suid, tuid)) - 1;
  }

  linkFor(source, target) {
    for (let i = 0; i < this.links.length; ++i) {
      if (this.links[i].source === source && this.links[i].target === target)
        return this.links[i];
      if (this.links[i].source === target && this.links[i].target === source)
        return this.links[i];
    }
    // the selected node was a client/broker
    return undefined;
  }

  getPosition(name, nodes, source, client, height, localStorage) {
    let position = localStorage[name] ? JSON.parse(localStorage[name]) : undefined;
    if (typeof position === "undefined") {
      position = {
        x: Math.round(nodes.get(source).x + 40 * Math.sin(client / (Math.PI * 2.0))),
        y: Math.round(nodes.get(source).y + 40 * Math.cos(client / (Math.PI * 2.0))),
        fixed: false,
        animate: true,
      };
    } else position.animate = false;
    if (position.y > height) {
      position.y = Math.round(
        nodes.get(source).y + 40 + Math.cos(client / (Math.PI * 2.0))
      );
    }
    if (position.x === null || position.y === null) {
      position.x = Math.round(
        nodes.get(source).x + 40 * Math.sin(client / (Math.PI * 2.0))
      );
      position.y = Math.round(
        nodes.get(source).y + 40 * Math.cos(client / (Math.PI * 2.0))
      );
    }
    position.fixed = position.fixed ? true : false;
    return position;
  }

  initialize(nodeInfo, nodes, separateContainers, unknowns, height, localStorage) {
    this.reset();
    const connectionsPerContainer = {};
    const nodeIds = Object.keys(nodeInfo);
    const nodeNames = new Set(nodeIds.map(n => utils.nameFromId(n)));
    // collect connection info for each router
    for (let source = 0; source < nodeIds.length; source++) {
      let onode = nodeInfo[nodeIds[source]];
      // skip any routers without connections
      if (
        !onode.connection ||
        !onode.connection.results ||
        onode.connection.results.length === 0
      ) {
        continue;
      }
      for (let c = 0; c < onode.connection.results.length; c++) {
        let connection = utils.flatten(
          onode.connection.attributeNames,
          onode.connection.results[c]
        );

        // ignore internal only connections
        if (connection.properties["qd.adaptor"]) {
          continue;
        }
        // we need a unique connection.container
        if (connection.container === "") {
          connection.container = connection.name.replace("/", "").replace(":", "-");
          //utils.uuidv4();
        }
        // this is a connection to another interior/edge router
        if (
          connection.role === "inter-router" ||
          (connection.role === "edge" && separateContainers.has(connection.container)) ||
          nodeNames.has(connection.container)
        ) {
          const suid = nodes.get(source).uid();
          const target = getContainerIndex(connection.container, nodeInfo);
          if (target >= 0) {
            const tuid = nodes.get(target).uid();
            this.getLink(source, target, connection.dir, "", suid, tuid);
          }
          continue;
        }
        if (!connectionsPerContainer[connection.container])
          connectionsPerContainer[connection.container] = [];
        let linksDir = getLinkDir(connection, onode);
        if (linksDir === "unknown") {
          unknowns.push(nodeIds[source]);
        }
        connectionsPerContainer[connection.container].push({
          source: source,
          linksDir: linksDir,
          connection: connection,
          resultsIndex: c,
        });
      }
    }
    let unique = {};
    // create map of type:id:dir to [containers]
    for (let container in connectionsPerContainer) {
      let key = getKey(connectionsPerContainer[container]);
      if (!unique[key])
        unique[key] = {
          c: [],
          nodes: [],
        };
      unique[key].c.push(container);
    }
    for (let key in unique) {
      let containers = unique[key].c;
      for (let i = 0; i < containers.length; i++) {
        let containerId = containers[i];
        let connections = connectionsPerContainer[containerId];
        let container = connections[0];
        let name =
          utils.nameFromId(nodeIds[container.source]) +
          "." +
          container.connection.identity;
        let position = this.getPosition(
          name,
          nodes,
          container.source,
          container.resultsIndex,
          height,
          localStorage
        );
        let node = nodes.getOrCreateNode({
          id: nodeIds[container.source],
          name,
          nodeType: container.connection.role,
          nodeIndex: nodes.getLength(),
          x: position.x,
          y: position.y,
          connectionContainer: container.connection.container,
          resultIndex: container.resultsIndex,
          fixed: position.fixed,
          properties: container.connection.properties,
        });
        node.host = container.connection.host;
        node.cdir = container.linksDir;
        node.user = container.connection.user;
        node.isEncrypted = container.connection.isEncrypted;
        node.connectionId = container.connection.identity;
        node.uuid = `${containerId}-${node.routerId}-${node.nodeType}-${node.cdir}`;
        // in case a created node (or group) is connected to multiple
        // routers, we need to remember all the routers for traffic animations
        for (let c = 1; c < connections.length; c++) {
          if (!node.alsoConnectsTo) node.alsoConnectsTo = [];
          node.alsoConnectsTo.push({
            key: nodeIds[connections[c].source],
            cdir: connections[c].linksDir,
            connectionId: connections[c].connection.identity,
          });
        }
        unique[key].nodes.push(node);
      }
    }
    for (let key in unique) {
      nodes.add(unique[key].nodes[0]);
      let target = nodes.nodes.length - 1;
      unique[key].nodes[0].normals = [unique[key].nodes[0]];
      for (let n = 1; n < unique[key].nodes.length; n++) {
        unique[key].nodes[0].normals.push(unique[key].nodes[n]);
      }
      let containerId = unique[key].c[0];
      let links = connectionsPerContainer[containerId];
      for (let l = 0; l < links.length; l++) {
        let source = links[l].source;
        const suid = nodes.get(source).uid();
        const tuid = nodes.get(target).uid();
        this.getLink(links[l].source, target, links[l].linksDir, "small", suid, tuid);
      }
    }
  }

  clearHighlighted() {
    for (let i = 0; i < this.links.length; ++i) {
      this.links[i].highlighted = false;
    }
  }
}

var getContainerIndex = function (_id, nodeInfo) {
  let nodeIndex = 0;
  for (let id in nodeInfo) {
    if (utils.nameFromId(id) === _id) return nodeIndex;
    ++nodeIndex;
  }
  return -1;
};

var getLinkDir = function (connection, onode) {
  let links = onode["router.link"];
  if (!links) {
    return "unknown";
  }
  let inCount = 0,
    outCount = 0;
  let typeIndex = links.attributeNames.indexOf("linkType");
  let connectionIdIndex = links.attributeNames.indexOf("connectionId");
  let dirIndex = links.attributeNames.indexOf("linkDir");
  links.results.forEach(function (linkResult) {
    if (
      linkResult[typeIndex] === "endpoint" &&
      linkResult[connectionIdIndex] === connection.identity
    )
      if (linkResult[dirIndex] === "in") ++inCount;
      else ++outCount;
  });
  if (inCount > 0 && outCount > 0) return "both";
  if (inCount > 0) return "in";
  if (outCount > 0) return "out";
  return "unknown";
};

var getKey = function (containers) {
  let parts = {};
  let connection = containers[0].connection;
  let d = {
    nodeType: connection.role,
    properties: connection.properties || {},
  };
  let connectionType = "client";
  if (utils.isConsole(connection)) connectionType = "console";
  else if (utils.isArtemis(d)) connectionType = "artemis";
  else if (utils.isQpid(d)) connectionType = "qpid";
  else if (connection.role === "edge") connectionType = "edge";
  for (let c = 0; c < containers.length; c++) {
    let container = containers[c];
    parts[`${container.source}-${container.linksDir}`] = true;
  }
  return `${connectionType}:${Object.keys(parts).join(":")}`;
};

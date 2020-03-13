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

import * as d3 from "d3";
import { ChordData } from "../chord/data.js";
import { MIN_CHORD_THRESHOLD } from "../chord/matrix.js";
import { nextHop } from "./topoUtils.js";
import { utils } from "../common/amqp/utilities.js";

const transitionDuration = 1000;

export class Traffic {
  // eslint-disable-line no-unused-vars
  constructor($scope, QDRService, converter, radius, topology, types, addressesChanged) {
    this.QDRService = QDRService;
    this.addressesChanged = addressesChanged;
    this.types = [];
    this.viss = [];
    this.topology = topology; // contains the list of router nodes
    this.$scope = $scope;
    // internal variables
    this.interval = null; // setInterval handle
    types.forEach(
      function(t) {
        this.addAnimationType(t, converter, radius);
      }.bind(this)
    );
    // called when mouse enters one of the address legends
    this.$scope.enterLegend = address => {
      // fade all flows that aren't for this address
      this.fadeOtherAddresses(address);
    };
    // called when the mouse leaves one of the address legends
    this.$scope.leaveLegend = () => {
      this.unFadeAll();
    };
  }
  fadeOtherAddresses = address => {
    d3.selectAll("circle.flow").classed("fade", function(d) {
      return d.address !== address;
    });
  };
  unFadeAll = () => {
    d3.selectAll("circle.flow").classed("fade", false);
  };

  // stop updating the traffic data
  stop() {
    if (this.interval) {
      clearInterval(this.interval);
      this.interval = null;
    }
  }
  // start updating the traffic data
  start() {
    this.stop();
    this.setup();
    this.doUpdate();
    this.interval = setInterval(this.doUpdate.bind(this), transitionDuration);
  }
  // remove animations that are in progress
  remove(type) {
    let all = !type;
    let i = this.viss.length - 1;
    while (i >= 0) {
      if (all || this.viss[i].type === type) {
        this.viss[i].remove();
        this.viss.splice(i, 1);
      }
      i--;
    }
    if (this.viss.length === 0) {
      this.stop();
    }
  }
  // called when one of the address checkboxes is toggled on
  addAnimationType(type, converter, radius) {
    if (!this.viss.some(v => v.type === type)) {
      this.viss.push(
        type === "dots" ? new Dots(this, converter, radius) : new Congestion(this)
      );
    }
    this.start();
  }
  // set the data needed for each animation
  setup() {
    this.viss.forEach(v => v.setup());
  }
  // called periodically to refresh the traffic flow
  doUpdate() {
    if (!this.interval) return;
    this.viss.forEach(v => v.doUpdate());
  }

  setTopology(topology) {
    this.topology = topology;
  }

  getAddressColors(service, converter) {
    return Dots.getAddressColors(service, converter);
  }

  updateAddressColors(address, checked) {
    if (addressColors[address]) {
      addressColors[address].checked = checked;
    }
    this.updateDots();
  }

  updateDots() {
    const dots = this.viss.find(v => v.type === "dots");
    if (dots) {
      const excludedAddresses = Object.keys(addressColors).filter(
        address => !addressColors[address].checked
      );
      dots.updateAddresses(excludedAddresses);
    }
  }

  addressColors() {
    return addressColors;
  }
}

/* Base class for congestion and dots visualizations */
class TrafficAnimation {
  constructor(traffic) {
    this.traffic = traffic;
  }
  nodeIndexFor(nodes, name) {
    for (let i = 0; i < nodes.length; i++) {
      let node = nodes[i];
      if (node.container === name) return i;
    }
    // not found. loop through normals
    for (let i = 0; i < nodes.length; i++) {
      let node = nodes[i];
      if (node.normals) {
        let normalIndex = node.normals.findIndex(function(normal) {
          return normal.container === name;
        });
        if (normalIndex >= 0) return i;
      }
    }
    return -1;
  }
}

const STEPS = 6;
/* Color the links between router to show how heavily used the links are. */
class Congestion extends TrafficAnimation {
  constructor(traffic) {
    super(traffic);
    this.type = "congestion";
    this.stopped = false;
  }

  setup() {
    this.traffic.QDRService.management.topology.addUpdateEntities([
      {
        entity: "router.link"
      },
      {
        entity: "connection"
      }
    ]);
  }

  findResult(node, entity, attribute, value) {
    let attrIndex = node[entity].attributeNames.indexOf(attribute);
    if (attrIndex >= 0) {
      for (let i = 0; i < node[entity].results.length; i++) {
        if (node[entity].results[i][attrIndex] === value) {
          return utils.flatten(node[entity].attributeNames, node[entity].results[i]);
        }
      }
    }
    return null;
  }

  doUpdate() {
    this.stopped = false;
    let self = this;
    // animation was stopped between the ensureAllEntities request and the response
    if (self.stopped) return;
    let links = {};
    let nodeInfo = self.traffic.QDRService.management.topology.nodeInfo();
    const nodes = self.traffic.topology.nodes.nodes;
    const srv = self.traffic.QDRService;
    // accumulate all the inter-router links in an object
    // keyed by the svgs path id
    for (let nodeId in nodeInfo) {
      let node = nodeInfo[nodeId];
      let nodeLinks = node["router.link"];
      if (!nodeLinks) continue;
      for (let n = 0; n < nodeLinks.results.length; n++) {
        let link = srv.utilities.flatten(nodeLinks.attributeNames, nodeLinks.results[n]);
        if (link.linkType !== "router-control") {
          let f = self.nodeIndexFor(nodes, srv.utilities.nameFromId(nodeId));
          let connection = self.findResult(
            node,
            "connection",
            "identity",
            link.connectionId
          );
          if (connection) {
            let t = self.nodeIndexFor(nodes, connection.container);
            let little = Math.min(f, t);
            let big = Math.max(f, t);
            if (little >= 0) {
              let key = ["#hitpath", nodes[little].uid(), nodes[big].uid()].join("-");
              if (!links[key]) links[key] = [];
              links[key].push(link);
            }
          }
        }
      }
    }
    // accumulate the colors/directions to be used
    for (let key in links) {
      let congestion = self.congestion(links[key]);
      let path;
      d3.selectAll("path.hittarget").each(function(l) {
        if (
          key === `#hitpath-${l.suid}-${l.tuid}` ||
          key === `#hitpath-${l.tuid}-${l.suid}`
        ) {
          path = d3.select(this);
        }
      });
      if (path && !path.empty()) {
        // start the path with transparent white
        if (!path.attr("style")) {
          path.style("stroke", "rgb(255, 255, 255)").style("opacity", 0);
        }
        // transition to next color
        path
          .transition()
          .duration(1000)
          .style("stroke", congestion)
          .style("opacity", 0.2)
          .each("end", function(d) {
            if (self.stopped) {
              // fade to transparent
              const p = d3.select(this);
              p.transition()
                .duration(100)
                .style("opacity", 0)
                .each("end", function() {
                  // remove the style
                  p.style("stroke", null).style("opacity", null);
                });
            }
          });
      }
    }
  }
  congestion(links) {
    const max = STEPS - 1;
    let v = 0;
    for (let l = 0; l < links.length; l++) {
      let link = links[l];
      v = Math.max(
        v,
        (max * (link.undeliveredCount + link.unsettledCount)) / link.capacity
      );
      v = Math.min(max, v);
    }
    return this.fillColor(v);
  }
  fillColor(v) {
    let color = d3.scale
      .linear()
      .domain([...Array(STEPS).keys()])
      .interpolate(d3.interpolateHcl)
      .range([
        d3.rgb("#999999"),
        d3.rgb("#00FF00"),
        d3.rgb("#66CC00"),
        d3.rgb("#FFA500"),
        d3.rgb("#FFCC00"),
        d3.rgb("#FF0000")
      ]);
    return color(Math.max(0, Math.min(STEPS - 1, v)));
  }
  remove() {
    this.stopped = true;
    d3.select("#SVG_ID")
      .selectAll("path.traffic")
      .classed("traffic", false);
    d3.select("#SVG_ID")
      .select("defs.custom-markers")
      .selectAll("marker")
      .remove();
  }
}

const colorGen = d3.scale.category10();
const addressColors = {};

/* Create animated dots moving along the links between routers
   to show message flow */
class Dots extends TrafficAnimation {
  constructor(traffic, converter, radius) {
    super(traffic);
    this.type = "dots";
    this.radius = radius; // the radius of a router circle
    this.lastFlows = {}; // the number of dots animated between routers
    this.stopped = false;
    this.chordData = new ChordData(this.traffic.QDRService, true, converter); // gets ingressHistogram data
    if (Object.keys(addressColors).length === 0) {
      // map address to {color, checked}
      Dots.getAddressColors(this.traffic.QDRService, converter);
    }
  }

  static getAddressColors(service, converter) {
    return new Promise(resolve => {
      const chordData = new ChordData(service, true, converter);
      chordData.getMatrix().then(() => {
        const addresses = chordData.getAddresses();
        for (let address in addresses) {
          addressColors[address] = { color: colorGen(address), checked: true };
        }
        resolve(addressColors);
      });
    });
  }

  setup() {
    this.traffic.QDRService.management.topology.addUpdateEntities([
      {
        entity: "router.node",
        attrs: ["id", "nextHop"]
      }
    ]);
  }

  remove() {
    d3.select("#SVG_ID")
      .selectAll("path.traffic")
      .classed("traffic", false);
    d3.select("#SVG_ID")
      .selectAll("circle.flow")
      .remove();
    this.lastFlows = {};
    this.stopped = true;
  }

  updateAddresses(excludedAddresses) {
    this.chordData.setFilter(excludedAddresses);
    // make sure no excludedAddress have an animation
    this.doUpdate();
  }

  fadeOtherAddresses(address) {
    d3.selectAll("circle.flow").classed("fade", function(d) {
      return d.address !== address;
    });
  }
  unFadeAll() {
    d3.selectAll("circle.flow").classed("fade", false);
  }

  doUpdate() {
    this.stopped = false;
    // get the ingressHistogram data for all routers
    this.chordData.getMatrix().then(this.render);
  }

  render = matrix => {
    if (this.stopped) return;
    const addresses = this.chordData.getAddresses();
    let changed = false;
    // make sure each address has a color
    for (let address in addresses) {
      if (!(address in addressColors)) {
        addressColors[address] = { color: this.fillColor(address), checked: true };
        changed = true;
      }
    }
    // remove any address that no longer have traffic
    for (let address in addressColors) {
      if (!(address in addresses)) {
        delete addressColors[address];
        changed = true;
      }
    }
    if (changed) {
      this.traffic.addressesChanged();
    }

    // get the rate of message flow between routers
    let hops = {}; // every hop between routers that is involved in message flow
    let matrixMessages = matrix.matrixMessages();
    // the fastest traffic rate gets more dots than the slowest
    let minmax = matrix.getMinMax();
    let flowScale = d3.scale
      .linear()
      .domain(minmax)
      .range([1, 1.5]);
    // row is ingress router, col is egress router. Value at [row][col] is the rate
    matrixMessages.forEach((row, r) => {
      row.forEach((val, c) => {
        if (val > MIN_CHORD_THRESHOLD) {
          // translate between matrix row/col and node index
          let f = this.nodeIndexFor(
            this.traffic.topology.nodes.nodes,
            matrix.rows[r].egress
          );
          let t = this.nodeIndexFor(
            this.traffic.topology.nodes.nodes,
            matrix.rows[r].ingress
          );
          let address = matrix.getAddress(r, c);
          if (r !== c) {
            // accumulate the hops between the ingress and egress routers
            nextHop(
              this.traffic.topology.nodes.nodes[f],
              this.traffic.topology.nodes.nodes[t],
              this.traffic.topology.nodes,
              this.traffic.topology.links,
              this.traffic.QDRService.management.topology.nodeInfo(),
              this.traffic.topology.nodes.nodes[f],
              function(link, fnode, tnode) {
                let key = "-" + link.uid();
                let back = fnode.index < tnode.index;
                if (!hops[key]) hops[key] = [];
                hops[key].push({
                  val: val,
                  back: back,
                  address: address
                });
              }
            );
          }
          // Find the senders connected to nodes[f] and the receivers connected to nodes[t]
          // and add their links to the animation
          this.addClients(hops, f, val, true, address);
          this.addClients(hops, t, val, false, address);

          const ftrue = this.addEdges(hops, f, val, true, address);
          const ffalse = this.addEdges(hops, f, val, false, address);
          const ttrue = this.addEdges(hops, t, val, true, address);
          const tfalse = this.addEdges(hops, t, val, false, address);
          [...ftrue, ...ffalse, ...ttrue, ...tfalse].forEach(eIndex => {
            this.addClients(hops, eIndex, val, true, address);
            this.addClients(hops, eIndex, val, false, address);
          });
        }
      });
    });
    // for each link between routers that has traffic, start an animation
    let keep = {};
    let offset = 0; // index of the address going in the same driection
    let total = 0; // number of addresses going in the same direction for this path

    for (let id in hops) {
      let hop = hops[id];
      const backCount = hop.filter(h => h.back).length;
      const foreCount = hop.length - backCount;
      let backIndex = 0; // index of address going the back direction
      let foreIndex = 0;
      for (let h = 0; h < hop.length; h++) {
        let ahop = hop[h];
        let pathId = CSS.escape(id); //id.replace(/\./g, "\\.").replace(/ /g, "\\ ");
        /*
        let flowId =
          id.replace(/\./g, "").replace(/ /g, "") +
          "-" +
          this.addressIndex(ahop.address) +
          (ahop.back ? "b" : "");
          */
        let flowId = CSS.escape(
          `${id}-${this.addressIndex(ahop.address)}${ahop.back ? "b" : ""}`
        );
        let path = d3.select("#path" + pathId);
        if (!path.empty()) {
          if (ahop.back) {
            offset = backIndex++;
            total = backCount;
          } else {
            offset = foreIndex++;
            total = foreCount;
          }
          // start the animation. If the animation is already running, this will have no effect
          this.animateDots(path, flowId, ahop, flowScale(ahop.val), offset, total);
        }
        keep[flowId] = true;
      }
    }
    // remove any existing animations that we don't have data for anymore
    for (let id in this.lastFlows) {
      if (this.lastFlows[id] && !keep[id]) {
        this.lastFlows[id] = 0;
        d3.select("#SVG_ID")
          .selectAll("circle.flow" + id)
          .remove();
      }
    }
  };

  // move the dots in the selection flow along the path
  animateFlow(flow, path, back, rate, offset, total) {
    let l = path.node().getTotalLength();
    const count = flow.size();
    const duration = (l * 10) / rate;
    flow
      .transition()
      .ease("easeLinear")
      .duration(duration)
      .attrTween("transform", this.translateDots(path, count, back, offset, total))
      .each("end", () => {
        if (this.stopped === false) {
          setTimeout(() => {
            this.animateFlow(flow, path, back, rate, offset, total);
          }, 1);
        }
      });
  }

  animateDots(path, id, hop, rate, offset, total) {
    let back = hop.back,
      address = hop.address;
    // the density of dots is determined by the rate of this traffic relative to the other traffic
    if (!path.node().getTotalLength) return;
    let len = Math.max(Math.floor(path.node().getTotalLength() / 50), 1);
    let dots = [];
    for (let i = 0, offset = this.addressIndex(address); i < len; ++i) {
      dots[i] = {
        i: i + 10 * offset,
        address
      };
    }
    // keep track of the number of dots for each link. If the length of the link is changed,
    // re-create the animation
    if (!this.lastFlows[id]) {
      this.lastFlows[id] = len;
    } else {
      if (this.lastFlows[id] !== len) {
        this.lastFlows[id] = len;
        d3.select("#SVG_ID")
          .selectAll("circle.flow" + id)
          .remove();
      }
    }
    let flow = d3
      .select("#SVG_ID")
      .selectAll("circle.flow" + id)
      .data(dots, function(d) {
        return d.i + d.address;
      });
    let circles = flow
      .enter()
      .append("circle")
      .attr("class", "flow flow" + id)
      .attr("data-testid", (d, i) => `flow${id}-${i}`)
      .attr("fill", this.fillColor(address))
      .attr("r", 4);

    // start the animation
    if (circles.size() > 0) {
      this.animateFlow(circles, path, back, rate, offset, total);
    }
    flow.exit().remove();
  }

  fillColor(address) {
    return colorGen(address);
  }

  // find the link that carries traffic for this address
  // going to nodes[f] if sender is true
  // coming from nodes[f] if sender if false.
  // Add the link's id to the hops array
  addClients(hops, f, val, sender, address) {
    const nodes = this.traffic.topology.nodes.nodes;
    if (!nodes[f]) return;
    const cdir = sender ? "out" : "in";
    const uuid = nodes[f].uid();
    const key = nodes[f].key;
    if (!this.traffic.QDRService.management.topology._nodeInfo[key]) return;
    const links = this.traffic.QDRService.management.topology._nodeInfo[key][
      "router.link"
    ];
    if (links) {
      const ilt = links.attributeNames.indexOf("linkType");
      const ioa = links.attributeNames.indexOf("owningAddr");
      const ici = links.attributeNames.indexOf("connectionId");
      const ild = links.attributeNames.indexOf("linkDir");
      let foundLinks = links.results.filter(function(l) {
        return (
          (l[ilt] === "endpoint" || l[ilt] === "edge-downlink") &&
          address === utils.addr_text(l[ioa]) &&
          l[ild] === cdir
        );
      }, this);
      // we now have the links involved in traffic for this address that
      // ingress/egress to/from this router (f).
      // Now find the created node that each link is associated with
      for (let linkIndex = 0; linkIndex < foundLinks.length; linkIndex++) {
        // use .some so the loop stops at the 1st match
        // eslint-disable-next-line no-loop-func
        nodes.some(node => {
          if (
            node.normals &&
            node.normals.some(function(normal) {
              return testNode(normal, key, cdir, foundLinks[linkIndex][ici]);
            })
          ) {
            // one of the normals for this node has the traffic
            const uuid2 = node.uid();
            const key = ["", uuid, uuid2].join("-");
            if (!hops[key]) hops[key] = [];
            hops[key].push({
              val: val,
              back: !sender,
              address
            });
            return true;
          }
          return false;
        });
      }
    }
  }

  addEdges(hops, f, val, sender, address) {
    const nodes = this.traffic.topology.nodes.nodes;
    if (!nodes[f]) return;
    const edges = [];
    const cdir = sender ? "out" : "in";
    const uuid = nodes[f].uid();
    const key = nodes[f].key;
    const links = this.traffic.QDRService.management.topology._nodeInfo[key][
      "router.link"
    ];
    if (links) {
      const ilt = links.attributeNames.indexOf("linkType");
      const ioa = links.attributeNames.indexOf("owningAddr");
      const ici = links.attributeNames.indexOf("connectionId");
      const ild = links.attributeNames.indexOf("linkDir");
      let foundLinks = links.results.filter(function(l) {
        return (
          (l[ilt] === "endpoint" || l[ilt] === "edge-downlink") &&
          address === utils.addr_text(l[ioa]) &&
          l[ild] === cdir
        );
      }, this);

      // we now have the links involved in traffic for this address that
      // ingress/egress to/from this router (f).
      // Now find the edge router for the links
      const connections = this.traffic.QDRService.management.topology._nodeInfo[key][
        "connection"
      ];
      for (let linkIndex = 0; linkIndex < foundLinks.length; linkIndex++) {
        const connectionId = foundLinks[linkIndex][ici];
        const iconnid = connections.attributeNames.indexOf("identity");
        const connection = connections.results.find(
          result => result[iconnid] === connectionId
        );
        if (connection) {
          const container = utils.valFor(
            connections.attributeNames,
            connection,
            "container"
          );
          const edge = nodes.find(
            node => node.nodeType === "_edge" && node.name === container
          );
          if (edge) {
            const uuid2 = edge.uid();
            const key = ["", uuid, uuid2].join("-");
            if (!hops[key]) hops[key] = [];
            hops[key].push({
              val: val,
              back: !sender,
              address
            });
            edges.push(edge.index);
          }
        }
      }
    }
    return edges;
  }

  addressIndex(address) {
    return Object.keys(addressColors).indexOf(address);
  }

  // calculate the translation for each dot along the path
  translateDots(path, count, back, offset, total) {
    let pnode = path.node();
    // will be called for each element in the flow selection (for each dot)
    return function(d) {
      const off = offset / total / count;
      // will be called with t going from 0 to 1 for each dot
      return function(t) {
        // start the points at different positions depending on their value (d)
        let tt = t * 1000; // total time
        let f = ((tt + (d.i * 1000) / count) % 1000) / 1000; // fraction
        f = (f + off) % 1;
        if (back) f = 1 - f;
        // l needs to be calculated each tick because the path's length might be changed during the animation
        let l = pnode.getTotalLength();
        let p = pnode.getPointAtLength(f * l);
        return "translate(" + p.x + "," + p.y + ")";
      };
    };
  }
}

// see if this node, or any of the nodes it also connects to
// match the key, dir, and connectionId
let testNode = function(node, key, dir, connectionId) {
  // does the node match
  if (
    node.key === key &&
    node.connectionId === connectionId &&
    (node.cdir === dir || node.cdir === "both")
  )
    return true;
  if (!node.alsoConnectsTo) return false;
  // do any of the alsoConnectsTo nodes match
  return node.alsoConnectsTo.some(function(ac2) {
    return testNode(ac2, key, dir, connectionId);
  });
};

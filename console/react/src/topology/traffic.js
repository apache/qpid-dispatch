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
import { utils } from "../amqp/utilities.js";

const transitionDuration = 1000;
//const CHORDFILTERKEY = "chordFilter";

export class Traffic {
  // eslint-disable-line no-unused-vars
  constructor($scope, QDRService, converter, radius, topology, types) {
    this.QDRService = QDRService;
    this.types = [];
    this.viss = [];
    this.topology = topology; // contains the list of router nodes
    this.$scope = $scope;
    this.addresses = $scope.state.legendOptions.traffic.addresses;
    this.addressColors = $scope.state.legendOptions.traffic.addressColors;
    // internal variables
    this.interval = null; // setInterval handle
    types.forEach(
      function(t) {
        this.addAnimationType(t, converter, radius);
      }.bind(this)
    );
  }
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
        type === "dots"
          ? new Dots(this, converter, radius)
          : new Congestion(this)
      );
    }
    this.start();
  }
  // called periodically to refresh the traffic flow
  doUpdate() {
    this.viss.forEach(v => v.doUpdate());
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

/* Color the links between router to show how heavily used the links are. */
class Congestion extends TrafficAnimation {
  constructor(traffic) {
    super(traffic);
    this.type = "congestion";
    this.stopped = false;
    this.init_markerDef();
  }
  init_markerDef() {
    this.custom_markers_def = d3
      .select("#SVG_ID")
      .select("defs.custom-markers");
    if (this.custom_markers_def.empty()) {
      this.custom_markers_def = d3
        .select("#SVG_ID")
        .append("svg:defs")
        .attr("class", "custom-markers");
    }
  }
  findResult(node, entity, attribute, value) {
    let attrIndex = node[entity].attributeNames.indexOf(attribute);
    if (attrIndex >= 0) {
      for (let i = 0; i < node[entity].results.length; i++) {
        if (node[entity].results[i][attrIndex] === value) {
          return utils.flatten(
            node[entity].attributeNames,
            node[entity].results[i]
          );
        }
      }
    }
    return null;
  }
  doUpdate() {
    this.stopped = false;
    let self = this;
    this.traffic.QDRService.management.topology.ensureAllEntities(
      [
        {
          entity: "router.link",
          force: true
        },
        {
          entity: "connection"
        }
      ],
      function() {
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
            let link = srv.utilities.flatten(
              nodeLinks.attributeNames,
              nodeLinks.results[n]
            );
            if (link.linkType !== "router-control") {
              let f = self.nodeIndexFor(
                nodes,
                srv.utilities.nameFromId(nodeId)
              );
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
                  let key = [
                    "#path",
                    nodes[little].uid(srv),
                    nodes[big].uid(srv)
                  ].join("-");
                  if (!links[key]) links[key] = [];
                  links[key].push(link);
                }
              }
            }
          }
        }
        // accumulate the colors/directions to be used
        let colors = {};
        for (let key in links) {
          let congestion = self.congestion(links[key]);
          let pathId = key.replace(/\./g, "\\.").replace(/ /g, "\\ ");
          let path = d3.select(pathId);
          if (path && !path.empty()) {
            let dir = path.attr("marker-end") === "" ? "start" : "end";
            let small = path.attr("class").indexOf("small") > -1;
            let id = dir + "-" + congestion.substr(1) + (small ? "-s" : "");
            colors[id] = {
              dir: dir,
              color: congestion,
              small: small
            };
            path
              .classed("traffic", true)
              .attr("marker-start", function() {
                return null;
              })
              .attr("marker-end", function() {
                return null;
              });
            path
              .transition()
              .duration(1000)
              .attr("stroke", congestion);
          }
        }
        // create the svg:def that holds the custom markers
        self.init_markerDef();
        let colorKeys = Object.keys(colors);
        let custom_markers = self.custom_markers_def
          .selectAll("marker")
          .data(colorKeys, function(d) {
            return d;
          });
        custom_markers
          .enter()
          .append("svg:marker")
          .attr("id", function(d) {
            return d;
          })
          .attr("viewBox", "0 -5 10 10")
          .attr("refX", function(d) {
            return colors[d].dir === "end" ? 24 : colors[d].small ? -24 : -14;
          })
          .attr("markerWidth", 14)
          .attr("markerHeight", 14)
          .attr("markerUnits", "userSpaceOnUse")
          .attr("orient", "auto")
          .style("fill", function(d) {
            return colors[d].color;
          })
          .append("svg:path")
          .attr("d", function(d) {
            return colors[d].dir === "end"
              ? "M 0 -5 L 10 0 L 0 5 z"
              : "M 10 -5 L 0 0 L 10 5 z";
          });
        custom_markers.exit().remove();
      }
    );
  }
  congestion(links) {
    let v = 0;
    for (let l = 0; l < links.length; l++) {
      let link = links[l];
      v = Math.max(
        v,
        (link.undeliveredCount + link.unsettledCount) / link.capacity
      );
      if (link.deliveriesDelayed1Sec) v += link.deliveriesDelayed1Sec * 0.5;
      if (link.deliveriesDelayed10Sec) v = 3;
      v = Math.min(3, v);
    }
    return this.fillColor(v);
  }
  fillColor(v) {
    let color = d3.scale
      .linear()
      .domain([0, 1, 2, 3])
      .interpolate(d3.interpolateHcl)
      .range([
        d3.rgb("#999999"),
        d3.rgb("#00FF00"),
        d3.rgb("#FFA500"),
        d3.rgb("#FF0000")
      ]);
    return color(Math.max(0, Math.min(3, v)));
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
    // colors
    this.colorGen = d3.scale.category10();
    for (let i = 0; i < 10; i++) {
      this.colorGen(i);
    }
    this.chordData.getMatrix().then(() => {
      const addresses = this.chordData.getAddresses();
      const addressColors = {};
      for (let address in addresses) {
        this.fillColor(address, addressColors);
      }
      this.traffic.$scope.handleUpdatedAddresses(addresses);
      this.traffic.$scope.handleUpdateAddressColors(addressColors);
      // set excludedAddresses
      this.updateAddresses();
    });
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
  updateAddresses() {
    this.excludedAddresses = [];
    for (const address in this.traffic.addresses) {
      if (!this.traffic.addresses[address])
        this.excludedAddresses.push(address);
    }
    if (this.chordData) {
      this.chordData.setFilter(this.excludedAddresses);
    }
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
    let self = this;
    this.stopped = false;
    // we need the nextHop data to show traffic between routers that are connected by intermediaries
    this.traffic.QDRService.management.topology.ensureAllEntities(
      [
        {
          entity: "router.node",
          attrs: ["id", "nextHop"]
        }
      ],
      function() {
        // if we were stopped between the request and response, just exit
        if (self.stopped) return;
        // get the ingressHistogram data for all routers
        self.chordData.getMatrix().then(self.render.bind(self), function(e) {
          console.log("Could not get message histogram" + e);
        });
      }
    );
  }
  render(matrix) {
    if (this.stopped === false) {
      const addresses = this.chordData.getAddresses();
      this.traffic.$scope.handleUpdatedAddresses(addresses);
      const addressColors = {};
      for (let address in addresses) {
        this.fillColor(address, addressColors);
      }
      this.traffic.$scope.handleUpdateAddressColors(addressColors);

      // get the rate of message flow between routers
      let hops = {}; // every hop between routers that is involved in message flow
      let matrixMessages = matrix.matrixMessages();
      // the fastest traffic rate gets 3 times as many dots as the slowest
      let minmax = matrix.getMinMax();
      let flowScale = d3.scale
        .linear()
        .domain(minmax)
        .range([1, 1.1]);
      // row is ingress router, col is egress router. Value at [row][col] is the rate
      matrixMessages.forEach(
        function(row, r) {
          row.forEach(
            function(val, c) {
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
                      let key = "-" + link.uid;
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
                this.addClients(
                  hops,
                  this.traffic.topology.nodes.nodes,
                  f,
                  val,
                  true,
                  address
                );
                this.addClients(
                  hops,
                  this.traffic.topology.nodes.nodes,
                  t,
                  val,
                  false,
                  address
                );
              }
            }.bind(this)
          );
        }.bind(this)
      );
      // for each link between routers that has traffic, start an animation
      let keep = {};
      for (let id in hops) {
        let hop = hops[id];
        for (let h = 0; h < hop.length; h++) {
          let ahop = hop[h];
          let pathId = id.replace(/\./g, "\\.").replace(/ /g, "\\ ");
          let flowId =
            id.replace(/\./g, "").replace(/ /g, "") +
            "-" +
            this.addressIndex(this, ahop.address) +
            (ahop.back ? "b" : "");
          let path = d3.select("#path" + pathId);
          // start the animation. If the animation is already running, this will have no effect
          this.startAnimation(path, flowId, ahop, flowScale(ahop.val));
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
    }
  }
  // animate the d3 selection (flow) along the given path
  animateFlow(flow, path, count, back, rate) {
    let l = path.node().getTotalLength();
    flow
      .transition()
      .ease("easeLinear")
      .duration((l * 10) / rate)
      .attrTween(
        "transform",
        this.translateDots(this.radius, path, count, back)
      )
      .each("end", () => {
        if (this.stopped === false) {
          this.animateFlow(flow, path, count, back, rate);
        }
      });
  }
  // create dots along the path between routers
  startAnimation(selection, id, hop, rate) {
    if (selection.empty()) return;
    this.animateDots(selection, id, hop, rate);
  }
  animateDots(path, id, hop, rate) {
    let back = hop.back,
      address = hop.address;
    // the density of dots is determined by the rate of this traffic relative to the other traffic
    let len = Math.max(Math.floor(path.node().getTotalLength() / 50), 1);
    let dots = [];
    for (let i = 0, offset = this.addressIndex(this, address); i < len; ++i) {
      dots[i] = {
        i: i + 10 * offset,
        address: address
      };
    }
    // keep track of the number of dots for each link. If the length of the link is changed,
    // re-create the animation
    if (!this.lastFlows[id]) this.lastFlows[id] = len;
    else {
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
      .attr("fill", this.fillColor(address, this.traffic.addressColors))
      .attr("r", 5);
    this.animateFlow(circles, path, dots.length, back, rate);
    flow.exit().remove();
  }
  fillColor(n, addressColors) {
    if (!(n in addressColors)) {
      let ci = Object.keys(addressColors).length;
      addressColors[n] = this.colorGen(ci);
    }
    return addressColors[n];
  }
  // find the link that carries traffic for this address
  // going to nodes[f] if sender is true
  // coming from nodes[f] if sender if false.
  // Add the link's id to the hops array
  addClients(hops, nodes, f, val, sender, address) {
    if (!nodes[f]) return;
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
              address: address
            });
            return true;
          }
          return false;
        });
      }
    }
  }
  addressIndex(vis, address) {
    return Object.keys(vis.traffic.addresses).indexOf(address);
  }
  // calculate the translation for each dot along the path
  translateDots(radius, path, count, back) {
    let pnode = path.node();
    // will be called for each element in the flow selection (for each dot)
    return function(d) {
      // will be called with t going from 0 to 1 for each dot
      return function(t) {
        // start the points at different positions depending on their value (d)
        let tt = t * 1000;
        let f = ((tt + (d.i * 1000) / count) % 1000) / 1000;
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

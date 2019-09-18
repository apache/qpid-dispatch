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

export const RouterStates = [
  "NEW",
  "READY TO DEPLOY",
  "CLUSTER HEARD FROM",
  "IN NETWORK"
];

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
    radius: 20,
    refX: {
      end: 24,
      start: -14
    },
    linkDistance: [110, 55],
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
nodeProperties._edge = nodeProperties["edge"];
nodeProperties["on-demand"] = nodeProperties["normal"];
nodeProperties["route-container"] = nodeProperties["normal"];

export class Nodes {
  constructor() {
    this.nodes = [];
  }
  static radius(type) {
    if (nodeProperties[type].radius) return nodeProperties[type].radius;
    return 15;
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
      if (nodeProperties[key].radius === parseInt(r)) {
        return nodeProperties[key].refX[end];
      }
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
  clearHighlighted() {
    for (let i = 0; i < this.nodes.length; ++i) {
      this.nodes[i].highlighted = false;
    }
  }
}

// Generate a marker for each combination of:
//  start|end, ''|selected highlighted, and each possible node radius
export function addDefs(svg) {
  let sten = ["start", "end"];
  let states = [""];
  let radii = Nodes.discrete();
  let defs = [];
  for (let isten = 0; isten < sten.length; isten++) {
    for (let istate = 0; istate < states.length; istate++) {
      for (let iradii = 0; iradii < radii.length; iradii++) {
        defs.push({
          sten: sten[isten],
          state: states[istate],
          r: radii[iradii]
        });
      }
    }
  }

  svg
    .insert("svg:defs", "svg g")
    .attr("class", "marker-defs")
    .selectAll("marker")
    .data(defs)
    .enter()
    .append("svg:marker")
    .attr("id", function(d) {
      return [d.sten, d.state, d.r].join("-");
    })
    .attr("viewBox", "0 -5 10 10")
    .attr("refX", function(d) {
      return -1;
      //return Nodes.refX(d.sten, d.r);
    })
    .attr("markerWidth", 14)
    .attr("markerHeight", 14)
    .attr("markerUnits", "userSpaceOnUse")
    .attr("orient", "auto")
    .append("svg:path")
    .attr("d", function(d) {
      return d.sten === "end"
        ? "M 0 -5 L 10 0 L 0 5 z"
        : "M 10 -5 L 0 0 L 10 5 z";
    })
    .attr("fill", "#000000");

  addStyles(
    sten,
    {
      selected: "#33F",
      highlighted: "#6F6",
      unknown: "#888"
    },
    radii
  );
}
export function addGradient(svg) {
  // gradient for sender/receiver client
  let grad = svg
    .append("svg:defs")
    .append("linearGradient")
    .attr("id", "half-circle")
    .attr("x1", "0%")
    .attr("x2", "0%")
    .attr("y1", "100%")
    .attr("y2", "0%");
  grad
    .append("stop")
    .attr("offset", "50%")
    .style("stop-color", "#C0F0C0");
  grad
    .append("stop")
    .attr("offset", "50%")
    .style("stop-color", "#F0F000");
}

function addStyles(stend, stateColor, radii) {
  // the <style>
  let element = document.querySelector("style");
  // Reference to the stylesheet
  let sheet = element.sheet;

  let states = Object.keys(stateColor);
  // create styles for each combo of 'stend-state-radii'
  for (let istend = 0; istend < stend.length; istend++) {
    for (let istate = 0; istate < states.length; istate++) {
      let selectors = [];
      for (let iradii = 0; iradii < radii.length; iradii++) {
        selectors.push(`#${stend[istend]}-${states[istate]}-${radii[iradii]}`);
      }
      let color = stateColor[states[istate]];
      let sels = `${selectors.join(",")} {fill: ${color}; stroke: ${color};}`;
      sheet.insertRule(sels, 0);
    }
  }
}

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

import { Nodes } from "./nodes.js";
import { appendCircle, appendContent, appendTitle } from "./svgUtils.js";
import * as d3 from "d3";

const PADDING = 5;
const lookFor = [
  {
    role: "_topo",
    title: "Router",
    text: "Router",
    cmp: n => n.nodeType === "_topo"
  },
  {
    role: "edge",
    title: "Edge Group",
    text: "Edge Group",
    cmp: n => n.nodeType === "edge"
  },
  {
    role: "_edge",
    title: "Edge Router",
    text: "Edge Router",
    cmp: n => n.nodeType === "_edge"
  },
  {
    role: "normal",
    title: "Console",
    text: "console",
    cmp: n => n.isConsole,
    props: { console_identifier: "Dispatch console" }
  },
  {
    role: "normal",
    title: "Sender",
    text: "Sender",
    cmp: n => n.nodeType === "normal" && n.cdir === "in",
    cdir: "in"
  },
  {
    role: "normal",
    title: "Receiver",
    text: "Receiver",
    cmp: n => n.nodeType === "normal" && n.cdir === "out",
    cdir: "out"
  },
  {
    role: "normal",
    title: "Sender/Receiver",
    text: "Sender/Receiver",
    cmp: n => n.nodeType === "normal" && n.cdir === "both" && !n.isConsole,
    cdir: "both"
  },
  {
    role: "route-container",
    title: "Artemis broker",
    text: "Artemis broker",
    cmp: n => n.isArtemis,
    props: { product: "apache-activemp-artemis" }
  },
  {
    role: "route-container",
    title: "Qpid broker",
    text: "Qpid broker",
    cmp: n => n.nodeType === "route-container" && n.properties.product === "qpid-cpp",
    props: { product: "qpid-cpp" }
  },
  {
    role: "route-container",
    title: "Service",
    text: "Service",
    cmp: n =>
      n.nodeType === "route-container" && n.properties.product === "External Service",
    props: { product: " External Service" }
  }
];

export class Legend {
  constructor(nodes, QDRLog) {
    this.nodes = nodes;
  }

  // create a new legend container svg
  init() {
    return d3
      .select("#topo_svg_legend")
      .append("svg")
      .attr("id", "svglegend")
      .attr("xmlns", "http://www.w3.org/2000/svg")
      .append("svg:g")
      .attr("transform", `translate(${Nodes.maxRadius()}, ${Nodes.maxRadius()})`)
      .selectAll("g");
  }

  // create or update the legend
  update() {
    let lsvg;
    d3.select("#svglegend").remove();
    lsvg = this.init();

    // add a node to legendNodes for each node type that is currently in the svg
    let legendNodes = new Nodes();
    lookFor.forEach((node, i) => {
      if (this.nodes.nodes.some(n => node.cmp(n))) {
        let newNode = legendNodes.addUsing({
          id: node.title,
          name: node.text,
          nodeType: node.role,
          nodeIndex: undefined,
          x: 0,
          y: 0,
          connectionContainer: i,
          resultIndex: 0,
          fixed: 0,
          properties: node.props ? node.props : {}
        });
        if (node.cdir) {
          newNode.cdir = node.cdir;
        }
      }
    });
    // cury is the y position to add the next legend node
    let cury = 2;

    // associate the legendNodes with lsvg
    lsvg = lsvg.data(legendNodes.nodes, function(d) {
      return d.key;
    });

    // add any new nodes
    let legendEnter = lsvg
      .enter()
      .append("svg:g")
      .attr("transform", d => {
        if (d.nodeType === "edge") cury += 4;
        let t = `translate(2, ${cury})`;
        cury = cury + Nodes.radius(d.nodeType) * 2 + PADDING;
        return t;
      });
    appendCircle(legendEnter, this.urlPrefix);
    appendContent(legendEnter, true);
    appendTitle(legendEnter);
    legendEnter
      //.filter(d => d.nodeType !== "_edge" && d.nodeType !== "_topo")
      .append("svg:text")
      .attr("x", 35)
      .attr("y", 6)
      .attr("class", "label")
      .text(function(d) {
        if (d.nodeType === "_topo") return "Router";
        else if (d.nodeType === "_edge") return "Edge Router";
        else return d.key;
      });

    let svgEl = document.getElementById("svglegend");
    if (svgEl) {
      svgEl.style.height = `${cury + 20}px`;
    }
  }
}

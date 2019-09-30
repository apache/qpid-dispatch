/*
 * Copyright 2018 Red Hat Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

import { Nodes } from "./nodes.js";
import { appendCircle, appendContent, appendTitle } from "./svgUtils.js";
import * as d3 from "d3";

const PADDING = 5;
const lookFor = [
  {
    role: "_topo",
    title: "Router",
    text: "",
    cmp: n => n.nodeType === "_topo"
  },
  {
    role: "edge",
    title: "Router",
    text: "Edge",
    cmp: n => n.nodeType === "edge"
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
    cmp: n =>
      n.nodeType === "route-container" && n.properties.product === "qpid-cpp",
    props: { product: "qpid-cpp" }
  },
  {
    role: "route-container",
    title: "Service",
    text: "Service",
    cmp: n =>
      n.nodeType === "route-container" &&
      n.properties.product === "External Service",
    props: { product: " External Service" }
  }
];

export class Legend {
  constructor(nodes, QDRLog) {
    this.nodes = nodes;
    this.log = QDRLog;
  }

  // create a new legend container svg
  init() {
    return d3
      .select("#topo_svg_legend")
      .append("svg")
      .attr("id", "svglegend")
      .attr("xmlns", "http://www.w3.org/2000/svg")
      .append("svg:g")
      .attr(
        "transform",
        `translate(${Nodes.maxRadius()}, ${Nodes.maxRadius()})`
      )
      .selectAll("g");
  }

  gap = d => {
    let g = Nodes.radius(d.nodeType) * 2 + PADDING;
    if (d.nodeType === "_topo") {
      g -= Nodes.radius(d.nodeType) / 2;
    }
    return g;
  };

  // create or update the legend
  update() {
    let lsvg;
    if (d3.select("#topo_svg_legend svg").empty()) {
      lsvg = this.init();
    } else {
      lsvg = d3.select("#topo_svg_legend svg g").selectAll("g");
    }
    // add a node to legendNodes for each node type that is currently in the svg
    let legendNodes = new Nodes(this.log);
    this.nodes.nodes.forEach((n, i) => {
      let node = lookFor.find(lf => lf.cmp(n));
      if (node) {
        if (!legendNodes.nodes.some(ln => ln.key === node.title)) {
          let newNode = legendNodes.addUsing(
            node.title,
            node.text,
            node.role,
            undefined,
            0,
            0,
            i,
            0,
            false,
            node.props ? node.props : {}
          );
          if (node.cdir) {
            newNode.cdir = node.cdir;
          }
        }
      }
    });

    // determine the y coordinate of the last existing node in the legend
    let cury = 2;
    lsvg.each((d, i) => {
      cury += this.gap(d);
    });

    // associate the legendNodes with lsvg
    lsvg = lsvg.data(legendNodes.nodes, function(d) {
      return d.key;
    });

    // add any new nodes
    let legendEnter = lsvg
      .enter()
      .append("svg:g")
      .attr("transform", d => {
        let t = `translate(2, ${cury})`;
        cury += this.gap(d);
        return t;
      });
    appendCircle(legendEnter, this.urlPrefix);
    appendContent(legendEnter);
    appendTitle(legendEnter);
    legendEnter
      .append("svg:text")
      .attr("x", 35)
      .attr("y", 6)
      .attr("class", "label")
      .text(function(d) {
        return d.key;
      });

    // remove any nodes that dropped out of legendNodes
    lsvg.exit().remove();

    let svgEl = document.getElementById("svglegend");
    if (svgEl) {
      svgEl.style.height = `${cury + 20}px`;
    }
    /*
    // position the legend based on it's size
    let svgEl = document.getElementById("svglegend");
    if (svgEl) {
      let bb;
      // firefox can throw an exception on getBBox on an svg element
      try {
        bb = svgEl.getBBox();
      } catch (e) {
        bb = {
          y: 0,
          height: 200,
          x: 0,
          width: 200
        };
      }
      svgEl.style.height = bb.y + bb.height + "px";
      svgEl.style.width = bb.x + bb.width + "px";
    }
    */
  }
}

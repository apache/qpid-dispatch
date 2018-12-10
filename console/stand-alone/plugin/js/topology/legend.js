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

/* global d3 */

import { Nodes } from "./nodes.js";
import { appendCircle, appendContent, appendTitle } from "./svgUtils.js";
// for testing, window will be undefined
var ddd = typeof window === 'undefined' ? require('d3') : d3;

const lookFor = [
  { role: "inter-router", title: "Router", text: "", cls: '' },
  { role: "edge", title: "Router", text: "Edge", cls: 'edge' },
  { role: "normal", title: "Console", text: "console", cls: 'console', props: { console_identifier: "Dispatch console" } },
  { role: "normal", title: "Sender", text: "Sender", cls: 'client.in', cdir: "in" },
  { role: "normal", title: "Receiver", text: "Receiver", cls: 'client.out', cdir: "out" },
  { role: "normal", title: "Sender/Receiver", text: "Sender/Receiver", cls: 'client.inout', cdir: "both" },
  { role: "route-container", title: "Qpid broker", text: "Qpid broker", cls: 'client.route-container', props: { product: "qpid-cpp" } },
  { role: "route-container", title: "Artemis broker", text: "Artemis broker", cls: 'route-container', props: { product: "apache-activemp-artemis" } },
  { role: "route-container", title: "Service", text: "Service", cls: 'route-container', props: { product: " External Service" } }
];

export class Legend {
  constructor(svg, QDRLog, urlPrefix) {
    this.svg = svg;
    this.log = QDRLog;
    this.urlPrefix = urlPrefix;
  }

  // create a new legend container svg
  init() {
    return ddd
      .select("#topo_svg_legend")
      .append("svg")
      .attr("id", "svglegend")
      .append("svg:g")
      .attr(
        "transform",
        `translate(${Nodes.maxRadius()}, ${Nodes.maxRadius()})`
      )
      .selectAll("g");
  }

  // create or update the legend
  update() {
    let lsvg;
    if (ddd.select("#topo_svg_legend svg").empty()) {
      lsvg = this.init();
    } else {
      lsvg = ddd.select("#topo_svg_legend svg g").selectAll("g");
    }
    // add a node to legendNodes for each node type that is currently in the svg
    let legendNodes = new Nodes(this.log);
    lookFor.forEach(function (node, i) {
      if (!node.cls || !this.svg.selectAll(`circle.${node.cls}`).empty()) {
        let lnode = legendNodes.addUsing(
          node.title,
          node.text,
          node.role,
          undefined,
          0, 0, i, 0,
          false,
          node.props ? node.props : {}
        );
        if (node.cdir)
          lnode.cdir = node.cdir;
      }
    }, this);

    // determine the y coordinate of the last existing node in the legend 
    let cury = 0;
    lsvg.each(function (d) {
      cury += Nodes.radius(d.nodeType) * 2 + 10;
    });

    // associate the legendNodes with lsvg
    lsvg = lsvg.data(legendNodes.nodes, function (d) {
      return d.uid();
    });

    // add any new nodes
    let legendEnter = lsvg
      .enter()
      .append("svg:g")
      .attr("transform", function (d) {
        let t = `translate(0, ${cury})`;
        cury += Nodes.radius(d.nodeType) * 2 + 10;
        return t;
      });
    appendCircle(legendEnter, this.urlPrefix);
    appendContent(legendEnter);
    appendTitle(legendEnter);
    legendEnter.append("svg:text")
      .attr("x", 35)
      .attr("y", 6)
      .attr("class", "label")
      .text(function (d) {
        return d.key;
      });

    // remove any nodes that dropped out of legendNodes
    lsvg.exit().remove();

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
  }
}



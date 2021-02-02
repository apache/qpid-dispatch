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
import { utils } from "../common/amqp/utilities.js";

// update the node's classes based on the node's data
export function updateState(circle) {
  circle
    .selectAll("circle")
    .classed("highlighted", function (d) {
      return d.highlighted;
    })
    .classed("selected", function (d) {
      return d.selected;
    })
    .classed("fixed", function (d) {
      return d.fixed ? d.fixed & 1 : false;
    })
    .classed("multiple", function (d) {
      return d.normals;
    })
    .classed("dropped", d => d.dropped);
}

export function appendCircle(g) {
  // add new circles and set their attr/class/behavior
  return (
    g
      .append("svg:circle")
      .attr("class", "node")
      // the following attrs and classes won't change after the node is created
      .attr("r", function (d) {
        return Nodes.radius(d.nodeType);
      })
      .attr("fill", function (d) {
        if (d.nodeType === "edge" && d.normals && d.normals.length > 1) {
          return "url(#diagonal-stripe-1) #fff;";
        }
        if (d.cdir === "both" && !utils.isConsole(d)) {
          return "url(#half-circle)";
        }
        return null;
      })
      .attr("data-testid", function (d) {
        return (d.nodeType !== "normal" ? "router" : "client") + "-" + d.index;
      })
      .classed("normal", function (d) {
        return d.nodeType === "normal" || utils.isConsole(d);
      })
      .classed("in", function (d) {
        return d.cdir === "in";
      })
      .classed("out", function (d) {
        return d.cdir === "out";
      })
      .classed("inout", function (d) {
        return d.cdir === "both";
      })
      .classed("inter-router", function (d) {
        return d.nodeType === "inter-router" || d.nodeType === "_topo";
      })
      .classed("on-demand", function (d) {
        return d.nodeType === "on-demand";
      })
      .classed("edge", function (d) {
        return d.nodeType === "edge" || d.nodeType === "_edge";
      })
      .classed("console", function (d) {
        return utils.isConsole(d);
      })
      .classed("artemis", function (d) {
        return utils.isArtemis(d);
      })
      .classed("qpid-cpp", function (d) {
        return utils.isQpid(d);
      })
      .classed("route-container", function (d) {
        return (
          !utils.isArtemis(d) && !utils.isQpid(d) && d.nodeType === "route-container"
        );
      })
      .classed("client", function (d) {
        return d.nodeType === "normal" && !d.properties.console_identifier;
      })
  );
}

export function appendContent(g, legend) {
  // show node IDs
  g.append("svg:text")
    .attr("x", d => Nodes.textOffset(d.nodeType, d.name.length))
    .attr("y", function (d) {
      let y = 7;
      if (utils.isArtemis(d)) y = 8;
      else if (utils.isQpid(d)) y = 9;
      else if (d.nodeType === "inter-router") y = 4;
      else if (d.nodeType === "route-container") y = 5;
      else if (d.nodeType === "edge" || d.nodeType === "_edge") y = 4;
      return y;
    })
    .attr("class", d =>
      d.nodeType === "_topo" || d.nodeType === "_edge" ? "label" : "id"
    )
    .classed("long", d => d.name.length > 4)
    .classed("console", function (d) {
      return utils.isConsole(d);
    })
    .classed("normal", function (d) {
      return d.nodeType === "normal";
    })
    .classed("on-demand", function (d) {
      return d.nodeType === "on-demand";
    })
    .classed("edge", function (d) {
      return d.nodeType === "edge";
    })
    .classed("edge", function (d) {
      return d.nodeType === "_edge";
    })
    .classed("artemis", function (d) {
      return utils.isArtemis(d);
    })
    .classed("qpid-cpp", function (d) {
      return utils.isQpid(d);
    })
    .text(function (d) {
      if (legend && (d.nodeType === "_edge" || d.nodeType === "_topo")) return null;
      if (utils.isConsole(d)) {
        return "\uf108"; // icon-desktop for a console
      } else if (utils.isArtemis(d)) {
        return "\ue900"; // custom font character
      } else if (utils.isQpid(d)) {
        return "\ue901"; // custom font character
      } else if (d.nodeType === "route-container") {
        return d.properties.product ? d.properties.product[0].toUpperCase() : "S";
      } else if (d.nodeType === "normal") {
        return "\uf109"; // icon-laptop for clients
      } else if (d.nodeType === "edge") {
        return "Edges";
      }
      return d.name.length > 15
        ? d.name.substr(0, 5) + "..." + d.name.substr(d.name.length - 5)
        : d.name;
    });
}

export function appendTitle(g) {
  g.append("svg:title").text(function (d) {
    return d.title();
  });
}

// Generate a marker for each combination of:
//  start|end, ''|selected highlighted, and each possible node radius
export function addDefs(svg) {
  let sten = ["start", "end"];
  let states = ["", "selected", "highlighted", "unknown"];
  let radii = Nodes.discrete();
  let defs = [];
  for (let isten = 0; isten < sten.length; isten++) {
    for (let istate = 0; istate < states.length; istate++) {
      for (let iradii = 0; iradii < radii.length; iradii++) {
        defs.push({
          sten: sten[isten],
          state: states[istate],
          r: radii[iradii],
        });
      }
    }
  }
  svg
    .append("svg:defs")
    .attr("class", "marker-defs")
    .selectAll("marker")
    .data(defs)
    .enter()
    .append("svg:marker")
    .attr("id", function (d) {
      return [d.sten, d.state, d.r].join("-");
    })
    .attr("viewBox", "0 -5 10 10")
    .attr("refX", function (d) {
      return Nodes.refX(d.sten, d.r);
    })
    .attr("markerWidth", 14)
    .attr("markerHeight", 14)
    .attr("markerUnits", "userSpaceOnUse")
    .attr("orient", "auto")
    .append("svg:path")
    .attr("d", function (d) {
      return d.sten === "end" ? "M 0 -5 L 10 0 L 0 5 z" : "M 10 -5 L 0 0 L 10 5 z";
    });

  // based on http://iros.github.io/patternfills/sample_svg.html
  svg
    .append("svg:defs")
    .append("pattern")
    .attr("id", "diagonal-stripe-1")
    .attr("patternUnits", "userSpaceOnUse")
    .attr("width", 10)
    .attr("height", 10)
    .append("image")
    .attr(
      "xlink:href",
      "data:image/svg+xml;base64,PHN2ZyB4bWxucz0naHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmcnIHdpZHRoPScxMCcgaGVpZ2h0PScxMCc+CiAgPHJlY3Qgd2lkdGg9JzEwJyBoZWlnaHQ9JzEwJyBmaWxsPSd3aGl0ZScvPgogIDxwYXRoIGQ9J00tMSwxIGwyLC0yCiAgICAgICAgICAgTTAsMTAgbDEwLC0xMAogICAgICAgICAgIE05LDExIGwyLC0yJyBzdHJva2U9J2JsYWNrJyBzdHJva2Utd2lkdGg9JzEnLz4KPC9zdmc+Cg=="
    )
    .attr("x", 0)
    .attr("y", 0)
    .attr("width", 10)
    .attr("height", 10);

  /*
    addStyles(
    sten,
    {
      selected: "#33F",
      highlighted: "#6F6",
      unknown: "#888",
    },
    radii
  );
  */
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
  grad.append("stop").attr("offset", "50%").style("stop-color", "#C0F0C0");
  grad.append("stop").attr("offset", "50%").style("stop-color", "#F0F000");
}

/*
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
*/

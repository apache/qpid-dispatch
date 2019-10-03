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

import React, { Component } from "react";
import * as d3 from "d3";
import QDRPopup from "../qdrPopup";
import { Traffic } from "./traffic.js";
import { separateAddresses } from "../chord/filters.js";
import { Nodes } from "./nodes.js";
import { Links } from "./links.js";
import { nextHop, connectionPopupHTML, getSizes } from "./topoUtils.js";
import { BackgroundMap } from "./map.js";
import { utils } from "../amqp/utilities.js";
import { Legend } from "./legend.js";
import LegendComponent from "./legendComponent";
import RouterInfoComponent from "./routerInfoComponent";
import ClientInfoComponent from "./clientInfoComponent";
import ContextMenuComponent from "../contextMenuComponent";
import {
  appendCircle,
  appendContent,
  addGradient,
  addDefs,
  updateState
} from "./svgUtils.js";
import { QDRLogger } from "../qdrGlobals";
const TOPOOPTIONSKEY = "topoLegendOptions";

class TopologyPage extends Component {
  constructor(props) {
    super(props);
    // restore the state of the legend sections
    let savedOptions = localStorage.getItem(TOPOOPTIONSKEY);
    savedOptions = savedOptions
      ? JSON.parse(savedOptions)
      : {
          traffic: {
            open: false,
            dots: false,
            congestion: false,
            addresses: [],
            addressColors: []
          },
          legend: {
            open: true
          },
          map: {
            open: false,
            show: false
          },
          arrows: {
            open: false,
            routerArrows: false,
            clientArrows: true
          }
        };
    // previous version read from storage didn't have show attribute
    if (typeof savedOptions.map.show === "undefined") {
      savedOptions.map.show = false;
    }
    this.state = {
      popupContent: "",
      showPopup: false,
      legendOptions: savedOptions,
      showRouterInfo: false,
      showClientInfo: false,
      showContextMenu: false
    };
    this.QDRLog = new QDRLogger(console, "Topology");
    this.popupCancelled = true;

    //  - nodes is an array of router/client info. these are the circles
    //  - links is an array of connections between the routers. these are the lines with arrows
    this.forceData = {
      nodes: new Nodes(this.QDRLog),
      links: new Links(this.QDRLog)
    };

    this.force = null;
    this.traffic = new Traffic(
      this,
      this.props.service,
      separateAddresses,
      Nodes.radius("inter-router"),
      this.forceData,
      ["dots", "congestion"].filter(t => this.state.legendOptions.traffic[t])
    );
    this.backgroundMap = new BackgroundMap(
      this,
      this.state.legendOptions.map,
      // notify: called each time a pan/zoom is performed
      () => {
        if (this.state.legendOptions.map.show) {
          // set all the nodes' x,y position based on their saved lon,lat
          this.forceData.nodes.setXY(this.backgroundMap);
          this.forceData.nodes.savePositions();
          // redraw the nodes in their x,y position and let non-fixed nodes bungie
          this.force.start();
          this.clearPopups();
        }
      }
    );
    this.state.mapOptions = this.backgroundMap.mapOptions;

    this.contextMenuItems = [
      {
        title: "Freeze in place",
        action: this.setFixed,
        enabled: data => !this.isFixed(data)
      },
      {
        title: "Unfreeze",
        action: this.setFixed,
        enabled: this.isFixed,
        endGroup: true
      },
      {
        title: "Unselect",
        action: this.setSelected,
        enabled: this.isSelected
      },
      {
        title: "Select",
        action: this.setSelected,
        enabled: data => !this.isSelected(data)
      }
    ];
  }

  // called only once when the component is initialized
  componentDidMount = () => {
    window.addEventListener("resize", this.resize);
    // we only need to update connections during steady-state
    this.props.service.management.topology.setUpdateEntities(["connection"]);
    // poll the routers for their latest entities (set to connection above)
    this.props.service.management.topology.startUpdating();

    // create the svg
    this.init();

    // get notified when a router is added/dropped and when
    // the number of connections for a router changes
    this.props.service.management.topology.addChangedAction("topology", () => {
      this.init();
    });
  };

  componentWillUnmount = () => {
    this.props.service.management.topology.setUpdateEntities([]);
    this.props.service.management.topology.stopUpdating();
    this.props.service.management.topology.delChangedAction("topology");
    this.traffic.remove();
    this.forceData.nodes.savePositions();
    window.removeEventListener("resize", this.resize);
    d3.select(".pf-c-page__main").style("background-color", "white");
  };

  resize = () => {
    if (!this.svg) return;
    let sizes = getSizes(this.topologyRef, this.QDRLog);
    this.width = sizes[0];
    this.height = sizes[1];
    if (this.width > 0) {
      // set attrs and 'resume' force
      this.svg.attr("width", this.width);
      this.svg.attr("height", this.height);
      this.force.size(sizes).resume();
    }
    this.updateLegend();
  };

  setFixed = (item, data) => {
    data.setFixed(item.title !== "Unfreeze");
  };

  isFixed = data => {
    return data.isFixed();
  };

  setSelected = (item, data) => {
    // remove the selected attr from each node
    this.circle.each(function(d) {
      d.selected = false;
    });
    // set the selected attr for this node
    data.selected = item.title === "Select";
    this.selected_node = data.selected ? data : null;
    this.restart();
  };
  isSelected = data => {
    return data.selected ? true : false;
  };
  updateLegend = () => {
    this.legend.update();
  };
  clearPopups = () => {};

  // initialize the nodes and links array from the QDRService.topology._nodeInfo object
  init = () => {
    let sizes = getSizes(this.topologyRef, this.QDRLog);
    this.width = sizes[0];
    this.height = sizes[1];
    if (this.width < 768) {
      const legendOptions = this.state.legendOptions;
      legendOptions.map.open = false;
      legendOptions.map.show = false;
      this.setState({ legendOptions });
    }
    let nodeInfo = this.props.service.management.topology.nodeInfo();
    let nodeCount = Object.keys(nodeInfo).length;

    this.mouseover_node = null;
    this.selected_node = null;

    d3.select("#SVG_ID .links").remove();
    d3.select("#SVG_ID .nodes").remove();
    d3.select("#SVG_ID circle.flow").remove();
    if (d3.select("#SVG_ID").empty()) {
      this.svg = d3
        .select("#topology")
        .append("svg")
        .attr("id", "SVG_ID")
        .attr("width", this.width)
        .attr("height", this.height)
        .on("click", this.clearPopups);
      // read the map data from the data file and build the map layer
      this.backgroundMap
        .init(this, this.svg, this.width, this.height)
        .then(() => {
          this.forceData.nodes.saveLonLat(this.backgroundMap);
          this.backgroundMap.setMapOpacity(this.state.legendOptions.map.show);
        });
      addDefs(this.svg);
      addGradient(this.svg);
    }
    // handles to link and node element groups
    this.path = this.svg
      .append("svg:g")
      .attr("class", "links")
      .selectAll("g");
    this.circle = this.svg
      .append("svg:g")
      .attr("class", "nodes")
      .selectAll("g");

    this.traffic.remove();
    if (this.state.legendOptions.traffic.dots)
      this.traffic.addAnimationType(
        "dots",
        separateAddresses,
        Nodes.radius("inter-router")
      );
    if (this.state.legendOptions.traffic.congestion)
      this.traffic.addAnimationType(
        "congestion",
        separateAddresses,
        Nodes.radius("inter-router")
      );

    // mouse event vars
    this.mousedown_node = null;

    // initialize the list of nodes
    this.forceData.nodes.initialize(
      nodeInfo,
      this.width,
      this.height,
      localStorage
    );
    this.forceData.nodes.savePositions();

    // initialize the list of links
    let unknowns = [];
    this.forceData.links.initialize(
      nodeInfo,
      this.forceData.nodes,
      unknowns,
      this.height,
      localStorage
    );
    // init D3 force layout
    this.force = d3.layout
      .force()
      .nodes(this.forceData.nodes.nodes)
      .links(this.forceData.links.links)
      .size([this.width, this.height])
      .linkDistance(d => {
        return this.forceData.nodes.linkDistance(d, nodeCount);
      })
      .charge(d => {
        return this.forceData.nodes.charge(d, nodeCount);
      })
      .friction(0.1)
      .gravity(d => {
        return this.forceData.nodes.gravity(d, nodeCount);
      })
      .on("tick", this.tick)
      .on("end", () => {
        this.forceData.nodes.savePositions();
        this.forceData.nodes.saveLonLat(this.backgroundMap);
      })
      .start();
    for (let i = 0; i < this.forceData.nodes.nodes.length; i++) {
      this.forceData.nodes.nodes[i].sx = this.forceData.nodes.nodes[i].x;
      this.forceData.nodes.nodes[i].sy = this.forceData.nodes.nodes[i].y;
    }

    // app starts here

    if (unknowns.length === 0) this.restart();
    // the legend
    this.legend = new Legend(this.forceData.nodes, this.QDRLog);
    this.updateLegend();

    if (this.oldSelectedNode) {
      d3.selectAll("circle.inter-router").classed("selected", function(d) {
        if (d.key === this.oldSelectedNode.key) {
          this.selected_node = d;
          return true;
        }
        return false;
      });
    }
    if (this.oldMouseoverNode && this.selected_node) {
      d3.selectAll("circle.inter-router").each(function(d) {
        if (d.key === this.oldMouseoverNode.key) {
          this.mouseover_node = d;
          this.props.service.management.topology.ensureAllEntities(
            [
              {
                entity: "router.node",
                attrs: ["id", "nextHop"]
              }
            ],
            function() {
              this.nextHopHighlight(this.selected_node, d);
              this.restart();
            }
          );
        }
      });
    }
    // if any clients don't yet have link directions, get the links for those nodes and restart the graph
    if (unknowns.length > 0)
      setTimeout(this.resolveUnknowns, 10, nodeInfo, unknowns);

    var continueForce = function(extra) {
      if (extra > 0) {
        --extra;
        this.force.start();
        setTimeout(continueForce, 100, extra);
      }
    };
    continueForce(Nodes.forceScale(nodeCount, [0, 200])); // give large graphs time to settle down
  };
  // To start up quickly, we only get the connection info for each router.
  // That means we don't have the router.link info when links.initialize() is first called
  // and the initial graph is drawn.
  // The router.link info is needed to determine which direction the arrows between routers
  // and client should point. (Direction between interior routers is determined by connection.dir)
  // So, the first time through links.initialize() we keep track of the nodes for which we
  // need router.link info and fill in that info here.
  resolveUnknowns = (nodeInfo, unknowns) => {
    let unknownNodes = {};
    // collapse the unknown nodes using an object
    for (let i = 0; i < unknowns.length; ++i) {
      unknownNodes[unknowns[i]] = 1;
    }
    unknownNodes = Object.keys(unknownNodes);
    this.props.service.management.topology.ensureEntities(
      unknownNodes,
      [
        {
          entity: "router.link",
          attrs: ["linkType", "connectionId", "linkDir", "owningAddr"],
          force: true
        }
      ],
      () => {
        let nodeInfo = this.props.service.management.topology.nodeInfo();
        this.forceData.nodes.initialize(
          nodeInfo,
          this.width,
          this.height,
          localStorage
        );
        let edgeUnknowns = [];
        this.forceData.links.initialize(
          nodeInfo,
          this.forceData.nodes,
          edgeUnknowns,
          this.height,
          localStorage
        );
        this.force
          .nodes(this.forceData.nodes.nodes)
          .links(this.forceData.links.links)
          .start();
        this.forceData.nodes.saveLonLat(this.backgroundMap);
        this.restart();
        this.updateLegend();
      }
    );
  };

  resetMouseVars = () => {
    this.mousedown_node = null;
    this.mouseover_node = null;
    this.mouseup_node = null;
  };

  handleMouseOutPath = d => {
    // mouse out of a path
    this.popupCancelled = true;
    this.props.service.management.topology.delUpdatedAction(
      "connectionPopupHTML"
    );
    this.setState({ showPopup: false });
    d.selected = false;
    connectionPopupHTML();
  };

  showMarker = d => {
    if (d.source.nodeType === "normal" || d.target.nodeType === "normal") {
      // link between router and client
      return this.state.legendOptions.arrows.clientArrows;
    } else {
      // link between routers or edge routers
      return this.state.legendOptions.arrows.routerArrows;
    }
  };

  // Takes the forceData.nodes and forceData.links array and creates svg elements
  // Also updates any existing svg elements based on the updated values in forceData.nodes
  // and forceData.links
  restart = () => {
    this.circle.call(this.force.drag);

    // path is a selection of all g elements under the g.links svg:group
    // here we associate the links.links array with the {g.links g} selection
    // based on the link.uid
    this.path = this.path.data(this.forceData.links.links, function(d) {
      return d.uid;
    });

    // update each existing {g.links g.link} element
    this.path
      .select(".link")
      .classed("selected", function(d) {
        return d.selected;
      })
      .classed("highlighted", function(d) {
        return d.highlighted;
      })
      .classed("unknown", function(d) {
        return !d.right && !d.left;
      });

    // reset the markers based on current highlighted/selected
    if (
      !this.state.legendOptions.traffic.open ||
      !this.state.legendOptions.traffic.congestion
    ) {
      this.path
        .select(".link")
        .attr("marker-end", d => {
          if (!this.showMarker(d)) return null;
          return d.right ? `url(#end${d.markerId("end")})` : null;
        })
        .attr("marker-start", d => {
          if (!this.showMarker(d)) return null;
          return d.left || (!d.left && !d.right)
            ? `url(#start${d.markerId("start")})`
            : null;
        });
    }
    // add new links. if a link with a new uid is found in the data, add a new path
    let enterpath = this.path
      .enter()
      .append("g")
      .on("mouseover", d => {
        // mouse over a path
        let event = d3.event;
        d.selected = true;
        this.popupCancelled = false;
        let updateTooltip = () => {
          if (d.selected) {
            this.setState({
              popupContent: connectionPopupHTML(
                d,
                this.props.service.management.topology._nodeInfo
              )
            });
            this.displayTooltip(event);
          } else {
            this.handleMouseOutPath(d);
          }
        };
        // update the contents of the popup tooltip each time the data is polled
        this.props.service.management.topology.addUpdatedAction(
          "connectionPopupHTML",
          updateTooltip
        );
        // request the data and update the tooltip as soon as it arrives
        this.props.service.management.topology.ensureAllEntities(
          [
            {
              entity: "router.link",
              force: true
            },
            {
              entity: "connection"
            }
          ],
          updateTooltip
        );
        // just show the tooltip with whatever data we have
        updateTooltip();

        this.restart();
      })
      .on("mouseout", d => {
        this.handleMouseOutPath(d);
        this.restart();
      })
      // left click a path
      .on("click", () => {
        d3.event.stopPropagation();
        this.clearPopups();
      });

    enterpath
      .append("path")
      .attr("class", "link")
      .attr("marker-end", d => {
        if (!this.showMarker(d)) return null;
        return d.right ? `url(#end${d.markerId("end")})` : null;
      })
      .attr("marker-start", d => {
        if (!this.showMarker(d)) return null;
        return d.left || (!d.left && !d.right)
          ? `url(#start${d.markerId("start")})`
          : null;
      })
      .attr("id", function(d) {
        const si = d.source.uid();
        const ti = d.target.uid();
        return ["path", si, ti].join("-");
      })
      .classed("unknown", function(d) {
        return !d.right && !d.left;
      });

    enterpath
      .append("path")
      .attr("class", "hittarget")
      .attr("id", d => `hitpath-${d.source.uid()}-${d.target.uid()}`);

    // remove old links
    this.path.exit().remove();

    // circle (node) group
    this.circle = d3
      .select("g.nodes")
      .selectAll("g")
      .data(this.forceData.nodes.nodes, function(d) {
        return d.uid();
      });

    // update existing nodes visual states
    updateState(this.circle);

    // add new circle nodes
    let enterCircle = this.circle
      .enter()
      .append("g")
      .classed("multiple", function(d) {
        return d.normals && d.normals.length > 1;
      })
      .attr("id", function(d) {
        return (d.nodeType !== "normal" ? "router" : "client") + "-" + d.index;
      });

    let self = this;
    appendCircle(enterCircle)
      .on("mouseover", function(d) {
        // mouseover a circle
        self.current_node = d;
        self.props.service.management.topology.delUpdatedAction(
          "connectionPopupHTML"
        );
        let e = d3.event;
        self.popupCancelled = false;
        d.toolTip(self.props.service.management.topology).then(function(
          toolTip
        ) {
          self.showToolTip(toolTip, e);
        });
        if (d === self.mousedown_node) return;
        // enlarge target node
        d3.select(this).attr("transform", "scale(1.1)");
        if (!self.selected_node) {
          return;
        }
        // highlight the next-hop route from the selected node to this node
        self.clearAllHighlights();
        // we need .router.node info to highlight hops
        self.props.service.management.topology.ensureAllEntities(
          [
            {
              entity: "router.node",
              attrs: ["id", "nextHop"]
            }
          ],
          function() {
            self.mouseover_node = d; // save this node in case the topology changes so we can restore the highlights
            self.nextHopHighlight(self.selected_node, d);
            self.restart();
          }
        );
      })
      .on("mouseout", function() {
        // mouse out for a circle
        self.current_node = null;
        // unenlarge target node
        d3.select(this).attr("transform", "");
        self.setState({ showPopup: false });
        self.popupCancelled = true;
        self.clearAllHighlights();
        self.mouseover_node = null;
        self.restart();
      })
      .on("mousedown", d => {
        // mouse down for circle
        this.backgroundMap.cancelZoom();
        this.current_node = d;
        if (d3.event.button !== 0) {
          // ignore all but left button
          return;
        }
        this.mousedown_node = d;
        // mouse position relative to svg
        this.initial_mouse_down_position = d3
          .mouse(this.topologyRef.parentNode.parentNode.parentNode)
          .slice();
      })
      .on("mouseup", function(d) {
        // mouse up for circle
        self.backgroundMap.restartZoom();
        if (!self.mousedown_node) return;

        // unenlarge target node
        d3.select(this).attr("transform", "");

        // check for drag
        self.mouseup_node = d;

        // if we dragged the node, make it fixed
        let cur_mouse = d3.mouse(self.svg.node());
        if (
          cur_mouse[0] !== self.initial_mouse_down_position[0] ||
          cur_mouse[1] !== self.initial_mouse_down_position[1]
        ) {
          self.forceData.nodes.setFixed(d, true);
          self.forceData.nodes.savePositions();
          self.forceData.nodes.saveLonLat(this.backgroundMap);
          self.resetMouseVars();
          self.restart();
          return;
        }

        self.clearAllHighlights();
        self.mousedown_node = null;
        // handle clicking on nodes that represent multiple sub-nodes
        if (d.normals && !d.isArtemis && !d.isQpid) {
          self.doDialog(d, "client");
        } else if (d.nodeType === "_topo") {
          self.doDialog(d, "router");
        }
        // apply any data changes to the interface
        self.restart();
      })
      .on("dblclick", d => {
        // circle
        d3.event.preventDefault();
        if (d.fixed) {
          this.forceData.nodes.setFixed(d, false);
          this.restart(); // redraw the node without a dashed line
          this.force.start(); // let the nodes move to a new position
        }
      })
      .on("contextmenu", d => {
        // circle
        d3.event.preventDefault();
        this.contextEventPosition = [d3.event.pageX, d3.event.pageY];
        this.contextEventData = d;
        this.setState({ showContextMenu: true });
        return false;
      })
      .on("click", d => {
        // circle
        if (!this.mouseup_node) return;
        // clicked on a circle
        this.clearPopups();
        // circle was a broker
        if (utils.isArtemis(d)) {
          const host = d.container === "0.0.0.0" ? "localhost" : d.container;
          const artemis = `${window.location.protocol()}://${host}:8161/console`;
          window.open(
            artemis,
            "artemis",
            "fullscreen=yes, toolbar=yes,location = yes, directories = yes, status = yes, menubar = yes, scrollbars = yes, copyhistory = yes, resizable = yes"
          );
          return;
        }
        d3.event.stopPropagation();
      });

    appendContent(enterCircle);

    // remove old nodes
    this.circle.exit().remove();

    // add text to client circles if there are any that represent multiple clients
    this.svg.selectAll(".subtext").remove();
    let multiples = this.svg.selectAll(".multiple");
    multiples.each(function(d) {
      let g = d3.select(this);
      let r = Nodes.radius(d.nodeType);
      g.append("svg:text")
        .attr("x", r + 4)
        .attr("y", Math.floor(r / 2 - 4))
        .attr("class", "subtext")
        .text("* " + d.normals.length);
    });

    if (!this.mousedown_node || !this.selected_node) return;

    // set the graph in motion
    this.force.start();
  };

  // update force layout (called automatically each iteration)
  tick = () => {
    // move the circles
    this.circle.attr("transform", d => {
      if (isNaN(d.x) || isNaN(d.px)) {
        d.x = d.px = d.sx;
        d.y = d.py = d.sy;
      }
      // don't let the edges of the circle go beyond the edges of the svg
      let r = Nodes.radius(d.nodeType);
      d.x = Math.max(Math.min(d.x, this.width - r), r);
      d.y = Math.max(Math.min(d.y, this.height - r), r);
      return `translate(${d.x},${d.y})`;
    });

    // draw lines from node centers
    this.path.selectAll("path").attr("d", function(d) {
      return `M${d.source.x},${d.source.y}L${d.target.x},${d.target.y}`;
    });
  };

  nextHopHighlight = (selected_node, d) => {
    selected_node.highlighted = true;
    d.highlighted = true;
    // if the selected node isn't a router,
    // find the router to which it is connected
    if (selected_node.nodeType !== "_topo") {
      let connected_node = this.forceData.nodes.find(
        selected_node.routerId,
        {},
        selected_node.routerId
      );
      // push the link between the selected_node and the router
      let link = this.forceData.links.linkFor(selected_node, connected_node);
      if (link) {
        link.highlighted = true;
        d3.select(`path[id='hitpath-${link.uid}']`).classed(
          "highlighted",
          true
        );
      }
      // start at the router
      selected_node = connected_node;
    }
    if (d.nodeType !== "_topo") {
      let connected_node = this.forceData.nodes.find(
        d.routerId,
        {},
        d.routerId
      );
      // push the link between the target_node and its router
      let link = this.forceData.links.linkFor(d, connected_node);
      if (link) {
        link.highlighted = true;
        d3.select(`path[id='hitpath-${link.uid}']`).classed(
          "highlighted",
          true
        );
      }
      // end at the router
      d = connected_node;
    }
    nextHop(
      selected_node,
      d,
      this.forceData.nodes,
      this.forceData.links,
      this.props.service.management.topology.nodeInfo(),
      selected_node,
      (link, fnode, tnode) => {
        link.highlighted = true;
        d3.select(`path[id='hitpath-${link.uid}']`).classed(
          "highlighted",
          true
        );
        fnode.highlighted = true;
        tnode.highlighted = true;
      }
    );
    let hnode = this.forceData.nodes.nodeFor(d.name);
    hnode.highlighted = true;
  };

  // show the details dialog for a client or group of clients
  doDialog = (d, type) => {
    this.d = d;
    if (type === "router") {
      this.setState({ showRouterInfo: true });
    } else if (type === "client") {
      this.setState({ showClientInfo: true });
    }
  };
  handleCloseRouterInfo = type => {
    this.setState({ showRouterInfo: false });
  };
  handleCloseClientInfo = () => {
    this.setState({ showClientInfo: false });
  };

  showToolTip = (title, event) => {
    // show the tooltip
    this.setState({ popupContent: title });
    this.displayTooltip(event);
  };

  displayTooltip = event => {
    if (this.popupCancelled) {
      this.setState({ showPopup: false });
      return;
    }
    let width = this.topologyRef.offsetWidth;
    let top = this.topologyRef.offsetTop - 5;

    // position the popup
    d3.select("#popover-div")
      .style("left", event.pageX + 5 + "px")
      .style("top", event.pageY - top + "px");
    // show popup
    let pwidth = this.popupRef.offsetWidth;
    this.setState({ showPopup: true }, () =>
      d3
        .select("#popover-div")
        .style("left", Math.min(width - pwidth, event.pageX + 5) + "px")
        .on("mouseout", () => {
          this.setState({ showPopup: false });
        })
    );
  };

  clearAllHighlights = () => {
    this.forceData.links.clearHighlighted();
    this.forceData.nodes.clearHighlighted();
    d3.selectAll(".hittarget").classed("highlighted", false);
  };

  saveLegendOptions = legendOptions => {
    localStorage.setItem(TOPOOPTIONSKEY, JSON.stringify(legendOptions));
  };
  handleLegendOptionsChange = (legendOptions, callback) => {
    this.saveLegendOptions(legendOptions);
    this.setState({ legendOptions }, () => {
      if (callback) {
        callback();
      }
      this.restart();
    });
  };

  handleOpenChange = (section, open) => {
    const { legendOptions } = this.state;
    legendOptions[section].open = open;
    if (section === "legend" && open) {
      this.legend.update();
    }
    this.handleLegendOptionsChange(this.state.legendOptions);
  };
  handleChangeArrows = (checked, event) => {
    const { legendOptions } = this.state;
    legendOptions.arrows[event.target.name] = checked;
    this.handleLegendOptionsChange(legendOptions);
  };

  // checking and unchecking of which traffic animation to show
  handleChangeTrafficAnimation = (checked, event) => {
    const { legendOptions } = this.state;
    const name = event.target.name;
    legendOptions.traffic[name] = checked;
    if (!checked) {
      this.traffic.remove(name);
    } else {
      this.traffic.addAnimationType(
        name,
        separateAddresses,
        Nodes.radius("inter-router")
      );
    }
    this.handleLegendOptionsChange(legendOptions);
  };

  handleChangeTrafficFlowAddress = (address, checked) => {
    const { legendOptions } = this.state;
    legendOptions.traffic.addresses[address] = checked;
    this.handleLegendOptionsChange(legendOptions, this.addressFilterChanged);
  };

  // called from traffic
  // the list of addresses has changed. set new addresses to true
  handleUpdatedAddresses = addresses => {
    const { legendOptions } = this.state;
    let changed = false;
    // set any new keys to the passed in value
    Object.keys(addresses).forEach(address => {
      if (typeof legendOptions.traffic.addresses[address] === "undefined") {
        legendOptions.traffic.addresses[address] = addresses[address];
        changed = true;
      }
    });
    // remove any old keys that were not passed in
    Object.keys(legendOptions.traffic.addresses).forEach(address => {
      if (typeof addresses[address] === "undefined") {
        delete legendOptions.traffic.addresses[address];
        changed = true;
      }
    });
    if (changed) {
      this.handleLegendOptionsChange(legendOptions, this.addressFilterChanged);
    }
  };

  handleUpdateAddressColors = addressColors => {
    const { legendOptions } = this.state;
    let changed = false;
    // set any new keys to the passed in value
    Object.keys(addressColors).forEach(address => {
      if (typeof legendOptions.traffic.addressColors[address] === "undefined") {
        legendOptions.traffic.addressColors[address] = addressColors[address];
        changed = true;
      }
    });
    // remove any old keys that were not passed in
    Object.keys(legendOptions.traffic.addressColors).forEach(address => {
      if (typeof addressColors[address] === "undefined") {
        delete legendOptions.traffic.addressColors[address];
        changed = true;
      }
    });
    if (changed) {
      this.handleLegendOptionsChange(legendOptions);
    }
  };
  handleUpdateMapColor = (which, color) => {
    let mapOptions = this.backgroundMap.updateMapColor(which, color);
    this.setState({ mapOptions });
  };

  // the mouse was hovered over one of the addresses in the legend
  handleHoverAddress = (address, over) => {
    // this.enterLegend and this.leaveLegend are defined in traffic.js
    if (over) {
      this.enterLegend(address);
    } else {
      this.leaveLegend();
    }
  };

  handleUpdateMapShown = checked => {
    const { legendOptions } = this.state;
    legendOptions.map.show = checked;
    this.setState({ legendOptions }, () => {
      this.backgroundMap.setMapOpacity(checked);
      this.backgroundMap.setBackgroundColor();
      this.saveLegendOptions(legendOptions);
    });
  };

  handleContextHide = () => {
    this.setState({ showContextMenu: false });
  };

  render() {
    return (
      <div className="qdrTopology">
        <LegendComponent
          addresses={this.state.legendOptions.traffic.addresses}
          addressColors={this.state.legendOptions.traffic.addressColors}
          trafficOpen={this.state.legendOptions.traffic.open}
          legendOpen={this.state.legendOptions.legend.open}
          mapOpen={this.state.legendOptions.map.open}
          mapShown={this.state.legendOptions.map.show}
          arrowsOpen={this.state.legendOptions.arrows.open}
          dots={this.state.legendOptions.traffic.dots}
          congestion={this.state.legendOptions.traffic.congestion}
          routerArrows={this.state.legendOptions.arrows.routerArrows}
          clientArrows={this.state.legendOptions.arrows.clientArrows}
          areaColor={this.state.mapOptions.areaColor}
          oceanColor={this.state.mapOptions.oceanColor}
          handleOpenChange={this.handleOpenChange}
          handleChangeArrows={this.handleChangeArrows}
          handleChangeTrafficAnimation={this.handleChangeTrafficAnimation}
          handleChangeTrafficFlowAddress={this.handleChangeTrafficFlowAddress}
          handleUpdateMapColor={this.handleUpdateMapColor}
          handleUpdateMapShown={this.handleUpdateMapShown}
        />
        <div className="diagram">
          {this.state.showContextMenu ? (
            <ContextMenuComponent
              ref={el => (this.contextRef = el)}
              contextEventPosition={this.contextEventPosition}
              contextEventData={this.contextEventData}
              handleContextHide={this.handleContextHide}
              menuItems={this.contextMenuItems}
            />
          ) : (
            <React.Fragment />
          )}
          <div ref={el => (this.topologyRef = el)} id="topology"></div>
          <div
            id="popover-div"
            className={this.state.showPopup ? "qdrPopup" : "qdrPopup hidden"}
            ref={el => (this.popupRef = el)}
          >
            <QDRPopup content={this.state.popupContent}></QDRPopup>
          </div>
        </div>
        {this.state.showRouterInfo ? (
          <RouterInfoComponent
            d={this.d}
            topology={this.props.service.management.topology}
            handleCloseRouterInfo={this.handleCloseRouterInfo}
          />
        ) : (
          <div />
        )}
        {this.state.showClientInfo ? (
          <ClientInfoComponent
            d={this.d}
            topology={this.props.service.management.topology}
            handleCloseClientInfo={this.handleCloseClientInfo}
          />
        ) : (
          <div />
        )}
      </div>
    );
  }
}

export default TopologyPage;

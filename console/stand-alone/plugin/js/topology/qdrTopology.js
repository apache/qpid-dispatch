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

/* global angular d3 */
/**
 * @module QDR
 */
import { QDRLogger, QDRRedirectWhenConnected, QDRTemplatePath } from "../qdrGlobals.js";
import { Traffic } from "./traffic.js";
import { separateAddresses } from "../chord/filters.js";
import { Nodes } from "./nodes.js";
import { Links } from "./links.js";
import { nextHop, connectionPopupHTML, getSizes } from "./topoUtils.js";
import { BackgroundMap } from "./map.js";
import { utils } from "../amqp/utilities.js";
import { Legend } from "./legend.js";
import { appendCircle, appendContent, addGradient, addDefs, updateState } from "./svgUtils.js";
/**
 * @module QDR
 */
export class TopologyController {
  constructor(
    QDRService,
    $scope,
    $log,
    $rootScope,
    $location,
    $timeout,
    $uibModal,
    $sce
  ) {
    this.controllerName = "QDR.TopologyController";

    let QDRLog = new QDRLogger($log, "TopologyController");
    const TOPOOPTIONSKEY = "topoLegendOptions";

    //  - nodes is an array of router/client info. these are the circles
    //  - links is an array of connections between the routers. these are the lines with arrows
    let forceData = {
      nodes: new Nodes(QDRLog),
      links: new Links(QDRLog)
    };

    // restore the state of the legend sections
    $scope.legendOptions = angular.fromJson(localStorage[TOPOOPTIONSKEY]) || {
      traffic: {
        open: false,
        dots: false,
        congestion: false
      },
      legend: {
        open: true
      },
      map: {
        open: false
      }
    };
    let backgroundMap = new BackgroundMap(
      $scope,
      // notify: called each time a pan/zoom is performed
      function () {
        if ($scope.legendOptions.map.open) {
          // set all the nodes' x,y position based on their saved lon,lat
          forceData.nodes.setXY(backgroundMap);
          forceData.nodes.savePositions();
          // redraw the nodes in their x,y position and let non-fixed nodes bungie
          force.start();
          clearPopups();
        }
      }
    );
    // urlPrefix is used when referring to svg:defs
    let urlPrefix = $location.absUrl();
    urlPrefix = urlPrefix.split("#")[0];

    let traffic = new Traffic(
      $scope,
      $timeout,
      QDRService,
      separateAddresses,
      Nodes.radius("inter-router"),
      forceData,
      ["dots", "congestion"].filter(t => $scope.legendOptions.traffic[t]),
      urlPrefix
    );

    let changeTraffic = function (checked, type) {
      localStorage[TOPOOPTIONSKEY] = JSON.stringify($scope.legendOptions);
      if ($scope.legendOptions.traffic.open) {
        if (checked) {
          traffic.addAnimationType(type, separateAddresses, Nodes.radius("inter-router"));
        } else {
          traffic.remove(type);
        }
      }
      restart();
    };
    // the dots animation was checked/unchecked
    $scope.$watch("legendOptions.traffic.dots", function (newValue) {
      changeTraffic(newValue, "dots");
    });
    // the congestion animation was checked/unchecked
    $scope.$watch("legendOptions.traffic.congestion", function (newValue) {
      changeTraffic(newValue, "congestion");
    });
    // the traffic section was opened/closed
    $scope.$watch("legendOptions.traffic.open", function () {
      localStorage[TOPOOPTIONSKEY] = JSON.stringify($scope.legendOptions);
      if ($scope.legendOptions.traffic.open) {
        // opened the traffic area
        changeTraffic($scope.legendOptions.traffic.dots, "dots");
        changeTraffic($scope.legendOptions.traffic.congestion, "congestion");
      } else {
        traffic.remove();
      }
      restart();
    });
    // the background map was shown or hidden
    $scope.$watch("legendOptions.map.open", function (newvalue, oldvalue) {
      localStorage[TOPOOPTIONSKEY] = JSON.stringify($scope.legendOptions);
      // map was shown
      if ($scope.legendOptions.map.open && backgroundMap.initialized) {
        // respond to pan/zoom events
        backgroundMap.restartZoom();
        // set the main_container div's background color to the ocean color
        backgroundMap.updateOceanColor();
        d3.select("g.geo").style("opacity", 1);
      } else {
        if (newvalue !== oldvalue) backgroundMap.cancelZoom();
        // hide the map and reset the background color
        d3.select("g.geo").style("opacity", 0);
        d3.select("#main_container").style("background-color", "#FFF");
      }
    });

    // mouse event vars
    let selected_node = null,
      mouseover_node = null,
      mouseup_node = null,
      initial_mouse_down_position = null;

    $scope.schema = "Not connected";
    $scope.current_node = null;
    $scope.mousedown_node = null;
    $scope.contextNode = null; // node that is associated with the current context menu
    $scope.isRight = function (mode) {
      return mode.right;
    };

    // show the details dialog for a client or group of clients
    function doDialog(d) {
      $uibModal
        .open({
          backdrop: true,
          keyboard: true,
          backdropClick: true,
          templateUrl: QDRTemplatePath + "tmplClientDetail.html",
          controller: "QDR.DetailDialogController",
          resolve: {
            d: function () {
              return d;
            }
          }
        })
        .result.then(function () { });
    }

    // called from the html page's popup menu
    $scope.setFixed = function (b) {
      if ($scope.contextNode) {
        forceData.nodes.setFixed($scope.contextNode, b);
        forceData.nodes.savePositions();
        forceData.nodes.saveLonLat(backgroundMap, $scope.contextNode);
      }
      // redraw the circles/links
      restart();
    };
    $scope.isFixed = function () {
      if (!$scope.contextNode) return false;
      return $scope.contextNode.fixed;
    };
    $scope.addressStyle = function (address) {
      return {
        "background-color": $scope.addressColors[address]
      };
    };

    let mouseX, mouseY;
    var relativeMouse = function () {
      let offset = $("#main_container").offset();
      return {
        left: mouseX + $(document).scrollLeft() - 1,
        top: mouseY + $(document).scrollTop() - 1,
        offset: offset
      };
    };
    // event handlers for popup context menu
    $(document).mousemove(e => {
      mouseX = e.clientX;
      mouseY = e.clientY;
    });
    $(document).mousemove();
    $(document).click(function () {
      $scope.contextNode = null;
      $(".contextMenu").fadeOut(200);
    });

    let svg; // main svg
    let force;
    let path, circle;   // the d3 selections for links and nodes respectively
    let savedKeys = {}; // so we can redraw the svg if the topology changes
    let width = 0;
    let height = 0;

    var resize = function () {
      if (!svg) return;
      let sizes = getSizes(QDRLog);
      width = sizes[0];
      height = sizes[1];
      if (width > 0) {
        // set attrs and 'resume' force
        svg.attr("width", width);
        svg.attr("height", height);
        force.size(sizes).resume();
      }
      $timeout(updateLegend);
    };

    // the window is narrow and the page menu icon was clicked.
    // Re-create the legend
    $scope.$on("pageMenuClicked", function () {
      $timeout(updateLegend);
    });

    window.addEventListener("resize", resize);
    let sizes = getSizes(QDRLog);
    width = sizes[0];
    height = sizes[1];
    if (width <= 0 || height <= 0) return;

    // initialize the nodes and links array from the QDRService.topology._nodeInfo object
    var initForceGraph = function () {
      if (width < 768) {
        $scope.legendOptions.map.open = false;
      }
      let nodeInfo = QDRService.management.topology.nodeInfo();
      let nodeCount = Object.keys(nodeInfo).length;

      let oldSelectedNode = selected_node;
      let oldMouseoverNode = mouseover_node;
      mouseover_node = null;
      selected_node = null;

      d3.select("#SVG_ID").remove();
      if (d3.select("#SVG_ID").empty()) {
        svg = d3
          .select("#topology")
          .append("svg")
          .attr("id", "SVG_ID")
          .attr("width", width)
          .attr("height", height)
          .on("click", function () {
            clearPopups();
          });
        // read the map data from the data file and build the map layer
        backgroundMap.init($scope, svg, width, height).then(function () {
          forceData.nodes.saveLonLat(backgroundMap);
          backgroundMap.setMapOpacity($scope.legendOptions.map.open);
        });
        addDefs(svg);
        addGradient(svg);
        // handles to link and node element groups
        path = svg.append("svg:g").attr("class", "links").selectAll("g");
        circle = svg.append("svg:g").attr("class", "nodes").selectAll("g");
      }
      // mouse event vars
      $scope.mousedown_node = null;
      mouseup_node = null;

      // initialize the list of nodes
      forceData.nodes.initialize(nodeInfo, width, height, localStorage);
      forceData.nodes.savePositions();

      // initialize the list of links
      let unknowns = [];
      forceData.links.initialize(nodeInfo,
        forceData.nodes,
        unknowns,
        height,
        localStorage);
      $scope.schema = QDRService.management.schema();
      // init D3 force layout
      force = d3.layout
        .force()
        .nodes(forceData.nodes.nodes)
        .links(forceData.links.links)
        .size([width, height])
        .linkDistance(function (d) {
          return forceData.nodes.linkDistance(d, nodeCount);
        })
        .charge(function (d) {
          return forceData.nodes.charge(d, nodeCount);
        })
        .friction(0.1)
        .gravity(function (d) {
          return forceData.nodes.gravity(d, nodeCount);
        })
        .on("tick", tick)
        .on("end", function () {
          forceData.nodes.savePositions();
          forceData.nodes.saveLonLat(backgroundMap);
        })
        .start();

      // app starts here
      if (unknowns.length === 0)
        restart();
      // the legend
      // call updateLegend in timeout because:
      // If we create the legend right away, then it will be destroyed when the accordian
      // gets initialized as the page loads.
      $timeout(updateLegend);

      if (oldSelectedNode) {
        d3.selectAll("circle.inter-router").classed("selected", function (d) {
          if (d.key === oldSelectedNode.key) {
            selected_node = d;
            return true;
          }
          return false;
        });
      }
      if (oldMouseoverNode && selected_node) {
        d3.selectAll("circle.inter-router").each(function (d) {
          if (d.key === oldMouseoverNode.key) {
            mouseover_node = d;
            QDRService.management.topology.ensureAllEntities(
              [{
                entity: "router.node",
                attrs: ["id", "nextHop"]
              }],
              function () {
                nextHopHighlight(selected_node, d);
                restart();
              }
            );
          }
        });
      }

      // if any clients don't yet have link directions, get the links for those nodes and restart the graph
      if (unknowns.length > 0)
        setTimeout(resolveUnknowns, 10, nodeInfo, unknowns);

      var continueForce = function (extra) {
        if (extra > 0) {
          --extra;
          force.start();
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
    var resolveUnknowns = function (nodeInfo, unknowns) {
      let unknownNodes = {};
      // collapse the unknown nodes using an object
      for (let i = 0; i < unknowns.length; ++i) {
        unknownNodes[unknowns[i]] = 1;
      }
      unknownNodes = Object.keys(unknownNodes);
      QDRService.management.topology.ensureEntities(
        unknownNodes,
        [{
          entity: "router.link",
          attrs: ["linkType", "connectionId", "linkDir", "owningAddr"],
          force: true
        }],
        function () {
          let nodeInfo = QDRService.management.topology.nodeInfo();
          forceData.nodes.initialize(nodeInfo, width, height, localStorage);
          let edgeUnknowns = [];
          forceData.links.initialize(nodeInfo, forceData.nodes, edgeUnknowns, height, localStorage);
          force
            .nodes(forceData.nodes.nodes)
            .links(forceData.links.links)
            .start();
          forceData.nodes.saveLonLat(backgroundMap);
          restart();
          updateLegend();
        }
      );
    };

    function resetMouseVars() {
      $scope.mousedown_node = null;
      mouseover_node = null;
      mouseup_node = null;
    }

    // update force layout (called automatically each iteration)
    function tick() {
      // move the circles
      circle.attr("transform", function (d) {
        // don't let the edges of the circle go beyond the edges of the svg
        let r = Nodes.radius(d.nodeType);
        d.x = Math.max(Math.min(d.x, width - r), r);
        d.y = Math.max(Math.min(d.y, height - r), r);
        return `translate(${d.x},${d.y})`;
      });

      // draw lines from node centers
      path.selectAll("path").attr("d", function (d) {
        return `M${d.source.x},${d.source.y}L${d.target.x},${d.target.y}`;
      });
    }

    function nextHopHighlight(selected_node, d) {
      nextHop(
        selected_node,
        d,
        forceData.nodes,
        forceData.links,
        QDRService.management.topology.nodeInfo(),
        selected_node,
        function (hlLink, hnode) {
          hlLink.highlighted = true;
          hnode.highlighted = true;
        }
      );
      let hnode = forceData.nodes.nodeFor(d.name);
      hnode.highlighted = true;
    }

    function clearPopups() {
      d3.select("#crosssection").style("display", "none");
      $(".hastip").empty();
      d3.select("#multiple_details").style("display", "none");
      d3.select("#link_details").style("display", "none");
      d3.select("#node_context_menu").style("display", "none");
      d3.select("#popover-div").style("display", "none");
    }

    function clearAllHighlights() {
      forceData.links.clearHighlighted();
      forceData.nodes.clearHighlighted();
    }

    let popupCancelled = true;
    function handleMouseOutPath(d) {
      // mouse out of a path
      popupCancelled = true;
      QDRService.management.topology.delUpdatedAction(
        "connectionPopupHTML"
      );
      d3.select("#popover-div").style("display", "none");
      d.selected = false;
      connectionPopupHTML();
    }

    // Takes the forceData.nodes and forceData.links array and creates svg elements
    // Also updates any existing svg elements based on the updated values in forceData.nodes
    // and forceData.links
    function restart() {
      if (!circle) return;
      circle.call(force.drag);

      // path is a selection of all g elements under the g.links svg:group
      // here we associate the links.links array with the {g.links g} selection
      // based on the link.uid
      path = path.data(forceData.links.links, function (d) {
        return d.uid;
      });

      // update each existing {g.links g.link} element
      path
        .select(".link")
        .classed("selected", function (d) {
          return d.selected;
        })
        .classed("highlighted", function (d) {
          return d.highlighted;
        })
        .classed("unknown", function (d) {
          return !d.right && !d.left;
        });

      // reset the markers based on current highlighted/selected
      if (
        !$scope.legendOptions.traffic.open ||
        !$scope.legendOptions.traffic.congestion
      ) {
        path
          .select(".link")
          .attr("marker-end", function (d) {
            return d.right ? `url(${urlPrefix}#end${d.markerId("end")})` : null;
          })
          .attr("marker-start", function (d) {
            return d.left || (!d.left && !d.right) ?
              `url(${urlPrefix}#start${d.markerId("start")})` :
              null;
          });
      }
      // add new links. if a link with a new uid is found in the data, add a new path
      let enterpath = path
        .enter()
        .append("g")
        .on("mouseover", function (d) {
          // mouse over a path
          let event = d3.event;
          d.selected = true;
          popupCancelled = false;
          let updateTooltip = function () {
            $timeout(function () {
              if (d.selected) {
                $scope.trustedpopoverContent = $sce.trustAsHtml(
                  connectionPopupHTML(
                    d,
                    QDRService.management.topology.nodeInfo()
                  )
                );
                displayTooltip(event);
              } else {
                handleMouseOutPath(d);
              }
            });
          };

          // update the contents of the popup tooltip each time the data is polled
          QDRService.management.topology.addUpdatedAction(
            "connectionPopupHTML",
            updateTooltip
          );
          // request the data and update the tooltip as soon as it arrives
          QDRService.management.topology.ensureAllEntities(
            [{
              entity: "router.link",
              force: true
            }, {
              entity: "connection"
            }], updateTooltip
          );
          // just show the tooltip with whatever data we have
          updateTooltip();
          restart();
        })
        .on("mouseout", function (d) {
          handleMouseOutPath(d);
          restart();
        })
        // left click a path
        .on("click", function () {
          d3.event.stopPropagation();
          clearPopups();
        });

      enterpath
        .append("path")
        .attr("class", "link")
        .attr("marker-end", function (d) {
          return d.right ? `url(${urlPrefix}#end${d.markerId("end")})` : null;
        })
        .attr("marker-start", function (d) {
          return d.left || (!d.left && !d.right) ?
            `url(${urlPrefix}#start${d.markerId("start")})` :
            null;
        })
        .attr("id", function (d) {
          const si = d.source.uid();
          const ti = d.target.uid();
          return ["path", si, ti].join("-");
        })
        .classed("unknown", function (d) {
          return !d.right && !d.left;
        });

      enterpath.append("path").attr("class", "hittarget");

      // remove old links
      path.exit().remove();

      // circle (node) group
      circle = d3.select("g.nodes").selectAll("g")
        .data(forceData.nodes.nodes, function (d) {
          return d.uid();
        });

      // update existing nodes visual states
      updateState(circle, selected_node);

      // add new circle nodes
      let enterCircle = circle
        .enter()
        .append("g")
        .classed("multiple", function (d) {
          return d.normals && d.normals.length > 1;
        })
        .attr("id", function (d) {
          return (
            (d.nodeType !== "normal" ? "router" : "client") + "-" + d.index
          );
        });

      appendCircle(enterCircle, urlPrefix)
        .on("mouseover", function (d) {
          // mouseover a circle
          $scope.current_node = d;
          QDRService.management.topology.delUpdatedAction(
            "connectionPopupHTML"
          );
          let e = d3.event;
          popupCancelled = false;
          d.toolTip(QDRService.management.topology).then(function (toolTip) {
            showToolTip(toolTip, e);
          });
          if (d === $scope.mousedown_node) return;
          // enlarge target node
          d3.select(this).attr("transform", "scale(1.1)");
          if (!selected_node) {
            return;
          }
          // highlight the next-hop route from the selected node to this node
          clearAllHighlights();
          // we need .router.node info to highlight hops
          QDRService.management.topology.ensureAllEntities(
            [{
              entity: "router.node",
              attrs: ["id", "nextHop"]
            }],
            function () {
              mouseover_node = d; // save this node in case the topology changes so we can restore the highlights
              nextHopHighlight(selected_node, d);
              restart();
            }
          );
        })
        .on("mouseout", function () {
          // mouse out for a circle
          $scope.current_node = null;
          // unenlarge target node
          d3.select("#popover-div").style("display", "none");
          popupCancelled = true;
          d3.select(this).attr("transform", "");
          clearAllHighlights();
          mouseover_node = null;
          restart();
        })
        .on("mousedown", function (d) {
          // mouse down for circle
          backgroundMap.cancelZoom();
          $scope.current_node = d;
          if (d3.event.button !== 0) {
            // ignore all but left button
            return;
          }
          $scope.mousedown_node = d;
          // mouse position relative to svg
          initial_mouse_down_position = d3
            .mouse(this.parentNode.parentNode.parentNode)
            .slice();
        })
        .on("mouseup", function (d) {
          // mouse up for circle
          backgroundMap.restartZoom();
          if (!$scope.mousedown_node) return;

          // unenlarge target node
          d3.select(this).attr("transform", "");

          // check for drag
          mouseup_node = d;

          // if we dragged the node, make it fixed
          let cur_mouse = d3.mouse(svg.node());
          if (
            cur_mouse[0] != initial_mouse_down_position[0] ||
            cur_mouse[1] != initial_mouse_down_position[1]
          ) {
            forceData.nodes.setFixed(d, true);
            forceData.nodes.savePositions();
            forceData.nodes.saveLonLat(backgroundMap);
            resetMouseVars();
            restart();
            return;
          }

          // if this node was selected, unselect it
          if ($scope.mousedown_node === selected_node) {
            selected_node = null;
          } else {
            if (
              d.nodeType !== "normal" &&
              d.nodeType !== "on-demand" &&
              d.nodeType !== "edge" &&
              d.nodeTYpe !== "_edge"
            )
              selected_node = $scope.mousedown_node;
          }
          clearAllHighlights();
          $scope.mousedown_node = null;
          if (!$scope.$$phase) $scope.$apply();
          // handle clicking on nodes that represent multiple sub-nodes
          if (d.normals && !d.isArtemis && !d.isQpid) {
            doDialog(d);
          }
          // apply any data changes to the interface
          restart();
        })
        .on("dblclick", function (d) {
          // circle
          d3.event.preventDefault();
          if (d.fixed) {
            forceData.nodes.setFixed(d, false);
            restart(); // redraw the node without a dashed line
            force.start(); // let the nodes move to a new position
          }
        })
        .on("contextmenu", function (d) {
          // circle
          $(document).click();
          d3.event.preventDefault();
          let rm = relativeMouse();
          d3.select("#node_context_menu").style({
            display: "block",
            left: rm.left + "px",
            top: rm.top - rm.offset.top + "px"
          });
          $timeout(function () {
            $scope.contextNode = d;
          });
        })
        .on("click", function (d) {
          // circle
          if (!mouseup_node) return;
          // clicked on a circle
          clearPopups();
          if (!d.normals) {
            // circle was a router or a broker
            if (utils.isArtemis(d)) {
              const artemisPath = "/jmx/attributes?tab=artemis&con=Artemis";
              window.location =
                $location.protocol() + "://localhost:8161/hawtio" + artemisPath;
            }
            return;
          }
          d3.event.stopPropagation();
        });

      appendContent(enterCircle);

      // remove old nodes
      circle.exit().remove();

      // add text to client circles if there are any that represent multiple clients
      svg.selectAll(".subtext").remove();
      let multiples = svg.selectAll(".multiple");
      multiples.each(function (d) {
        let g = d3.select(this);
        let r = Nodes.radius(d.nodeType);
        g.append("svg:text")
          .attr("x", r + 4)
          .attr("y", Math.floor(r / 2 - 4))
          .attr("class", "subtext")
          .text("* " + d.normals.length);
      });

      if (!$scope.mousedown_node || !selected_node)
        return;

      // set the graph in motion
      force.start();
    }

    function updateLegend() {
      // dynamically create/update the legend based on which node types are present
      let lsvg = new Legend(svg, QDRLog, urlPrefix);
      lsvg.update(svg);
    }

    function showToolTip(title, event) {
      // show the tooltip
      $timeout(function () {
        $scope.trustedpopoverContent = $sce.trustAsHtml(title);
        displayTooltip(event);
      });
    }

    function displayTooltip(event) {
      $timeout(function () {
        if (popupCancelled) {
          d3.select("#popover-div").style("display", "none");
          return;
        }
        let top = $("#topology").offset().top - 5;
        let width = $("#topology").width();
        // hide popup while getting its width
        d3.select("#popover-div")
          .style("visibility", "hidden")
          .style("display", "block")
          .style("left", event.pageX + 5 + "px")
          .style("top", event.pageY - top + "px");
        let pwidth = $("#popover-div").width();
        // show popup
        d3.select("#popover-div")
          .style("visibility", "visible")
          .style("left", Math.min(width - pwidth, event.pageX + 5) + "px")
          .on("mouseout", function () {
            d3.select(this).style("display", "none");
          });
      });
    }

    function hasChanged() {
      // Don't update the underlying topology diagram if we are adding a new node.
      // Once adding is completed, the topology will update automatically if it has changed
      let nodeInfo = QDRService.management.topology.nodeInfo();
      // don't count the nodes without connection info
      let cnodes = Object.keys(nodeInfo).filter(function (node) {
        return nodeInfo[node]["connection"];
      });
      let routers = forceData.nodes.nodes.filter(function (node) {
        return node.nodeType === "_topo";
      });
      if (routers.length > cnodes.length) {
        return -1;
      }
      if (cnodes.length != Object.keys(savedKeys).length) {
        return cnodes.length > Object.keys(savedKeys).length ? 1 : -1;
      }
      // we may have dropped a node and added a different node in the same update cycle
      for (let i = 0; i < cnodes.length; i++) {
        let key = cnodes[i];
        // if this node isn't in the saved node list
        if (!savedKeys.hasOwnProperty(key)) return 1;
        // if the number of connections for this node chaanged
        if (nodeInfo[key]["connection"].results.length !== savedKeys[key])
          return nodeInfo[key]["connection"].results.length - savedKeys[key];
      }
      return 0;
    }

    function saveChanged() {
      savedKeys = {};
      let nodeInfo = QDRService.management.topology.nodeInfo();
      // save the number of connections per node
      for (let key in nodeInfo) {
        if (nodeInfo[key]["connection"])
          savedKeys[key] = nodeInfo[key]["connection"].results.length;
      }
    }

    function destroy() {
      forceData.nodes.savePositions();
      QDRService.management.topology.setUpdateEntities([]);
      QDRService.management.topology.stopUpdating();
      QDRService.management.topology.delUpdatedAction("normalsStats");
      QDRService.management.topology.delUpdatedAction("topology");
      QDRService.management.topology.delUpdatedAction("connectionPopupHTML");

      d3.select("#SVG_ID").remove();
      window.removeEventListener("resize", resize);
      traffic.stop();
      d3.select("#main_container").style("background-color", "white");
    }
    // When the DOM element is removed from the page,
    // AngularJS will trigger the $destroy event on
    // the scope
    $scope.$on("$destroy", function () {
      destroy();
    });
    // we are about to leave the page, save the node positions
    $rootScope.$on("$locationChangeStart", function () {
      destroy();
    });

    function handleInitialUpdate() {
      // we only need to update connections during steady-state
      QDRService.management.topology.setUpdateEntities(["connection"]);
      // we currently have all entities available on all routers
      initForceGraph();
      saveChanged();
      // after the graph is displayed fetch all .router.node info. This is done so highlighting between nodes
      // doesn't incur a delay
      QDRService.management.topology.addUpdateEntities([{
        entity: "router.node",
        attrs: ["id", "nextHop"]
      }]);
      // call this function every time a background update is done
      QDRService.management.topology.addUpdatedAction("topology", function () {
        let changed = hasChanged();
        // there is a new node, we need to get all of it's entities before drawing the graph
        if (changed > 0) {
          QDRService.management.topology.delUpdatedAction("topology");
          setupInitialUpdate();
        } else if (changed === -1) {
          // we lost a node (or a client), we can draw the new svg immediately
          QDRService.management.topology.purge();
          initForceGraph();
          saveChanged();
        } else {
          //QDRLog.debug("topology didn't change")
        }
      });
    }

    function setupInitialUpdate() {
      // make sure all router nodes have .connection info. if not then fetch any missing info
      QDRService.management.topology.ensureAllEntities(
        [{
          entity: "connection"
        }],
        handleInitialUpdate
      );
    }
    if (!QDRService.management.connection.is_connected()) {
      // we are not connected. we probably got here from a bookmark or manual page reload
      QDRRedirectWhenConnected($location, "topology");
      return;
    }

    setupInitialUpdate();
    QDRService.management.topology.startUpdating(true);
  }
}
TopologyController.$inject = [
  "QDRService",
  "$scope",
  "$log",
  "$rootScope",
  "$location",
  "$timeout",
  "$uibModal",
  "$sce"
];

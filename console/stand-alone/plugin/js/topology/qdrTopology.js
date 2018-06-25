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
import { QDRLogger, QDRRedirectWhenConnected } from '../qdrGlobals.js';
import { Traffic } from './traffic.js';
import { separateAddresses } from '../chord/filters.js';
import { Nodes } from './nodes.js';
import { Links } from './links.js';
import { nextHop, connectionPopupHTML } from './topoUtils.js';
/**
 * @module QDR
 */
export class TopologyController {
  constructor(QDRService, $scope, $log, $rootScope, $location, $timeout, $uibModal, $sce) {
    this.controllerName = 'QDR.TopologyController';

    let QDRLog = new QDRLogger($log, 'TopologyController');
    const TOPOOPTIONSKEY = 'topoOptions';
    const radius = 25;
    const radiusNormal = 15;

    //  - nodes is an array of router/client info. these are the circles
    //  - links is an array of connections between the routers. these are the lines with arrows
    let nodes = new Nodes(QDRService, QDRLog);
    let links = new Links(QDRService, QDRLog);
    let forceData = {nodes: nodes, links: links};
    // urlPrefix is used when referring to svg:defs
    let urlPrefix = $location.absUrl();
    urlPrefix = urlPrefix.split('#')[0];

    $scope.legendOptions = angular.fromJson(localStorage[TOPOOPTIONSKEY]) || {showTraffic: false, trafficType: 'dots'};
    if (!$scope.legendOptions.trafficType)
      $scope.legendOptions.trafficType = 'dots';
    $scope.legend = {status: {legendOpen: true, optionsOpen: true}};
    $scope.legend.status.optionsOpen = $scope.legendOptions.showTraffic;
    let traffic = new Traffic($scope, $timeout, QDRService, separateAddresses, 
      radius, forceData, $scope.legendOptions.trafficType, urlPrefix);

    // the showTraaffic checkbox was just toggled (or initialized)
    $scope.$watch('legend.status.optionsOpen', function () {
      $scope.legendOptions.showTraffic = $scope.legend.status.optionsOpen;
      localStorage[TOPOOPTIONSKEY] = JSON.stringify($scope.legendOptions);
      if ($scope.legend.status.optionsOpen) {
        traffic.start();
      } else {
        traffic.stop();
        traffic.remove();
        restart();
      }
    });
    $scope.$watch('legendOptions.trafficType', function () {
      localStorage[TOPOOPTIONSKEY] = JSON.stringify($scope.legendOptions);
      if ($scope.legendOptions.showTraffic) {
        restart();
        traffic.setAnimationType($scope.legendOptions.trafficType, separateAddresses, radius);
        traffic.start();
      }
    });

    // mouse event vars
    let selected_node = null,
      selected_link = null,
      mousedown_link = null,
      mousedown_node = null,
      mouseover_node = null,
      mouseup_node = null,
      initial_mouse_down_position = null;

    $scope.schema = 'Not connected';

    $scope.contextNode = null; // node that is associated with the current context menu
    $scope.isRight = function(mode) {
      return mode.right;
    };

    $scope.setFixed = function(b) {
      if ($scope.contextNode) {
        $scope.contextNode.fixed = b;
        nodes.setNodesFixed($scope.contextNode.name, b);
        nodes.savePositions();
      }
      restart();
    };
    $scope.isFixed = function() {
      if (!$scope.contextNode)
        return false;
      return ($scope.contextNode.fixed & 1);
    };

    let mouseX, mouseY;
    var relativeMouse = function () {
      let offset = $('#main_container').offset();
      return {left: (mouseX + $(document).scrollLeft()) - 1,
        top: (mouseY  + $(document).scrollTop()) - 1,
        offset: offset
      };
    };
    // event handlers for popup context menu
    $(document).mousemove(e => {
      mouseX = e.clientX;
      mouseY = e.clientY;
    });
    $(document).mousemove();
    $(document).click(function() {
      $scope.contextNode = null;
      $('.contextMenu').fadeOut(200);
    });

    const radii = {
      'inter-router': 25,
      'normal': 15,
      'on-demand': 15,
      'route-container': 15,
    };
    let svg, lsvg;  // main svg and legend svg
    let force;
    let animate = false; // should the force graph organize itself when it is displayed
    let path, circle;
    let savedKeys = {};
    let width = 0;
    let height = 0;

    var getSizes = function() {
      const gap = 5;
      let legendWidth = 194;
      let topoWidth = $('#topology').width();
      if (topoWidth < 768)
        legendWidth = 0;
      let width = $('#topology').width() - gap - legendWidth;
      let top = $('#topology').offset().top;
      let height = window.innerHeight - top - gap;
      if (width < 10) {
        QDRLog.info(`page width and height are abnormal w: ${width} h: ${height}`);
        return [0, 0];
      }
      return [width, height];
    };
    var resize = function() {
      if (!svg)
        return;
      let sizes = getSizes();
      width = sizes[0];
      height = sizes[1];
      if (width > 0) {
        // set attrs and 'resume' force
        svg.attr('width', width);
        svg.attr('height', height);
        force.size(sizes).resume();
      }
      $timeout(createLegend);
    };

    // the window is narrow and the page menu icon was clicked.
    // Re-create the legend
    $scope.$on('pageMenuClicked', function () {
      $timeout(createLegend);
    });

    window.addEventListener('resize', resize);
    let sizes = getSizes();
    width = sizes[0];
    height = sizes[1];
    if (width <= 0 || height <= 0)
      return;

    // vary the following force graph attributes based on nodeCount
    // <= 6 routers returns min, >= 80 routers returns max, interpolate linearly
    var forceScale = function(nodeCount, min, max) {
      let count = Math.max(Math.min(nodeCount, 80), 6);
      let x = d3.scale.linear()
        .domain([6,80])
        .range([min, max]);
      //QDRLog.debug("forceScale(" + nodeCount + ", " + min + ", " + max + "  returns " + x(count) + " " + x(nodeCount))
      return x(count);
    };
    var linkDistance = function (d, nodeCount) {
      if (d.target.nodeType === 'inter-router')
        return forceScale(nodeCount, 150, 70);
      return forceScale(nodeCount, 75, 40);
    };
    var charge = function (d, nodeCount) {
      if (d.nodeType === 'inter-router')
        return forceScale(nodeCount, -1800, -900);
      return -900;
    };
    var gravity = function (d, nodeCount) {
      return forceScale(nodeCount, 0.0001, 0.1);
    };
    // initialize the nodes and links array from the QDRService.topology._nodeInfo object
    var initForceGraph = function() {
      forceData.nodes = nodes = new Nodes(QDRService, QDRLog);
      forceData.links = links = new Links(QDRService, QDRLog);
      let nodeInfo = QDRService.management.topology.nodeInfo();
      let nodeCount = Object.keys(nodeInfo).length;

      let oldSelectedNode = selected_node;
      let oldMouseoverNode = mouseover_node;
      mouseover_node = null;
      selected_node = null;
      selected_link = null;

      nodes.savePositions();
      d3.select('#SVG_ID').remove();
      svg = d3.select('#topology')
        .append('svg')
        .attr('id', 'SVG_ID')
        .attr('width', width)
        .attr('height', height);

      // the legend
      d3.select('#topo_svg_legend svg').remove();
      lsvg = d3.select('#topo_svg_legend')
        .append('svg')
        .attr('id', 'svglegend');
      lsvg = lsvg.append('svg:g')
        .attr('transform', `translate( ${(radii['inter-router'] + 2)},${(radii['inter-router'] + 2)})`)
        .selectAll('g');

      // mouse event vars
      mousedown_link = null;
      mousedown_node = null;
      mouseup_node = null;

      // initialize the list of nodes
      forceData.nodes = nodes = new Nodes(QDRService, QDRLog);
      animate = nodes.initialize(nodeInfo, localStorage, width, height);
      nodes.savePositions();

      // initialize the list of links
      let unknowns = [];
      forceData.links = links = new Links(QDRService, QDRLog);
      if (links.initializeLinks(nodeInfo, nodes, unknowns, localStorage, height)) {
        animate = true;
      }
      $scope.schema = QDRService.management.schema();
      // init D3 force layout
      force = d3.layout.force()
        .nodes(nodes.nodes)
        .links(links.links)
        .size([width, height])
        .linkDistance(function(d) { return linkDistance(d, nodeCount); })
        .charge(function(d) { return charge(d, nodeCount); })
        .friction(.10)
        .gravity(function(d) { return gravity(d, nodeCount); })
        .on('tick', tick)
        .on('end', function () {nodes.savePositions();})
        .start();

      // This section adds in the arrows
      svg.append('svg:defs').attr('class', 'marker-defs').selectAll('marker')
        .data(['end-arrow', 'end-arrow-selected', 'end-arrow-small', 'end-arrow-highlighted', 
          'start-arrow', 'start-arrow-selected', 'start-arrow-small', 'start-arrow-highlighted'])
        .enter().append('svg:marker') 
        .attr('id', function (d) { return d; })
        .attr('viewBox', '0 -5 10 10')
        .attr('refX', function (d) { 
          if (d.substr(0, 3) === 'end') {
            return 24;
          }
          return d !== 'start-arrow-small' ? -14 : -24;})
        .attr('markerWidth', 4)
        .attr('markerHeight', 4)
        .attr('orient', 'auto')
        .classed('small', function (d) {return d.indexOf('small') > -1;})
        .append('svg:path')
        .attr('d', function (d) {
          return d.substr(0, 3) === 'end' ? 'M 0 -5 L 10 0 L 0 5 z' : 'M 10 -5 L 0 0 L 10 5 z';
        });

      // gradient for sender/receiver client
      let grad = svg.append('svg:defs').append('linearGradient')
        .attr('id', 'half-circle')
        .attr('x1', '0%')
        .attr('x2', '0%')
        .attr('y1', '100%')
        .attr('y2', '0%');
      grad.append('stop').attr('offset', '50%').style('stop-color', '#C0F0C0');
      grad.append('stop').attr('offset', '50%').style('stop-color', '#F0F000');

      // handles to link and node element groups
      path = svg.append('svg:g').selectAll('path'),
      circle = svg.append('svg:g').selectAll('g');

      // app starts here
      restart(false);
      force.start();
      if (oldSelectedNode) {
        d3.selectAll('circle.inter-router').classed('selected', function (d) {
          if (d.key === oldSelectedNode.key) {
            selected_node = d;
            return true;
          }
          return false;
        });
      }
      if (oldMouseoverNode && selected_node) {
        d3.selectAll('circle.inter-router').each(function (d) {
          if (d.key === oldMouseoverNode.key) {
            mouseover_node = d;
            QDRService.management.topology.ensureAllEntities([{entity: 'router.node', attrs: ['id','nextHop']}], function () {
              nextHopHighlight(selected_node, d);
              restart();
            });
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
      continueForce(forceScale(nodeCount, 0, 200));  // give large graphs time to settle down
    };

    // To start up quickly, we only get the connection info for each router.
    // That means we don't have the router.link info when links.initialize() is first called.
    // The router.link info is needed to determine which direction the arrows between routers should point.
    // So, the first time through links.initialize() we keep track of the nodes for which we 
    // need router.link info and fill in that info here.
    var resolveUnknowns = function (nodeInfo, unknowns) {
      let unknownNodes = {};
      // collapse the unknown node.keys using an object
      for (let i=0; i<unknowns.length; ++i) {
        unknownNodes[unknowns[i].key] = 1;
      }
      unknownNodes = Object.keys(unknownNodes);
      //QDRLog.info("-- resolveUnknowns: ensuring .connection and .router.link are present for each node")
      QDRService.management.topology.ensureEntities(unknownNodes, [{entity: 'connection', force: true}, 
        {entity: 'router.link', attrs: ['linkType','connectionId','linkDir'], force: true}], function () {
        nodeInfo = QDRService.management.topology.nodeInfo();
        forceData.links = links = new Links(QDRService, QDRLog);
        links.initializeLinks(nodeInfo, nodes, [], localStorage, height);
        animate = true;
        force.nodes(nodes.nodes).links(links.links).start();
        restart(false);
      });
    };

    function resetMouseVars() {
      mousedown_node = null;
      mouseover_node = null;
      mouseup_node = null;
      mousedown_link = null;
    }

    // update force layout (called automatically each iteration)
    function tick() {
      circle.attr('transform', function(d) {
        let cradius;
        if (d.nodeType == 'inter-router') {
          cradius = d.left ? radius + 8 : radius;
        } else {
          cradius = d.left ? radiusNormal + 18 : radiusNormal;
        }
        d.x = Math.max(d.x, radiusNormal * 2);
        d.y = Math.max(d.y, radiusNormal * 2);
        d.x = Math.max(0, Math.min(width - cradius, d.x));
        d.y = Math.max(0, Math.min(height - cradius, d.y));
        return `translate(${d.x},${d.y})`;
      });

      // draw directed edges with proper padding from node centers
      path.attr('d', function(d) {
        let sourcePadding, targetPadding, r;

        r = d.target.nodeType === 'inter-router' ? radius : radiusNormal - 18;
        sourcePadding = targetPadding = 0;
        let dtx = Math.max(targetPadding, Math.min(width - r, d.target.x)),
          dty = Math.max(targetPadding, Math.min(height - r, d.target.y)),
          dsx = Math.max(sourcePadding, Math.min(width - r, d.source.x)),
          dsy = Math.max(sourcePadding, Math.min(height - r, d.source.y));

        let deltaX = dtx - dsx,
          deltaY = dty - dsy,
          dist = Math.sqrt(deltaX * deltaX + deltaY * deltaY);
        if (dist == 0)
          dist = 0.001;
        let normX = deltaX / dist,
          normY = deltaY / dist;
        let sourceX = dsx + (sourcePadding * normX),
          sourceY = dsy + (sourcePadding * normY),
          targetX = dtx - (targetPadding * normX),
          targetY = dty - (targetPadding * normY);
        sourceX = Math.max(0, sourceX);
        sourceY = Math.max(0, sourceY);
        targetX = Math.max(0, targetX);
        targetY = Math.max(0, targetY);

        return `M${sourceX},${sourceY}L${targetX},${targetY}`;
      })
        .attr('id', function (d) {
          return ['path', d.source.index, d.target.index].join('-');
        });

      if (!animate) {
        animate = true;
        force.stop();
      }
    }

    function nextHopHighlight(selected_node, d) {
      nextHop(selected_node, d, nodes, links, QDRService, selected_node, function (hlLink, hnode) {
        hlLink.highlighted = true;
        hnode.highlighted = true;
      });
      let hnode = nodes.nodeFor(d.name);
      hnode.highlighted = true;
    }

    function clearPopups() {
      d3.select('#crosssection').style('display', 'none');
      $('.hastip').empty();
      d3.select('#multiple_details').style('display', 'none');
      d3.select('#link_details').style('display', 'none');
      d3.select('#node_context_menu').style('display', 'none');

    }

    function clearAllHighlights() {
      links.clearHighlighted();
      nodes.clearHighlighted();
    }
    // takes the nodes and links array of objects and adds svg elements for everything that hasn't already
    // been added
    function restart(start) {
      if (!circle)
        return;
      circle.call(force.drag);

      // path (link) group
      path = path.data(links.links, function(d) {return d.uid;});

      // update existing links
      path.classed('selected', function(d) {
        return d === selected_link;
      })
        .classed('highlighted', function(d) {
          return d.highlighted;
        });
      if (!$scope.legend.status.optionsOpen || $scope.legendOptions.trafficType === 'dots') {
        path
          .attr('marker-start', function(d) {
            let sel = d === selected_link ? '-selected' : (d.cls === 'small' ? '-small' : '');
            if (d.highlighted)
              sel = '-highlighted';
            return d.left ? `url(${urlPrefix}#start-arrow${sel})` : '';
          })
          .attr('marker-end', function(d) {
            let sel = d === selected_link ? '-selected' : (d.cls === 'small' ? '-small' : '');
            if (d.highlighted)
              sel = '-highlighted';
            return d.right ? `url(${urlPrefix}#end-arrow${sel})` : '';
          });
      }
      // add new links. if a link with a new uid is found in the data, add a new path
      path.enter().append('svg:path')
        .attr('class', 'link')
        .attr('marker-start', function(d) {
          let sel = d === selected_link ? '-selected' : (d.cls === 'small' ? '-small' : '');
          return d.left ? `url(${urlPrefix}#start-arrow${sel})` : '';
        })
        .attr('marker-end', function(d) {
          let sel = d === selected_link ? '-selected' : (d.cls === 'small' ? '-small' : '');
          return d.right ? `url(${urlPrefix}#end-arrow${sel})` : '';
        })
        .classed('small', function(d) {
          return d.cls == 'small';
        })
        .on('mouseover', function(d) { // mouse over a path
          let event = d3.event;
          mousedown_link = d;
          selected_link = mousedown_link;
          let updateTooltip = function () {
            $timeout(function () {
              $scope.trustedpopoverContent = $sce.trustAsHtml(connectionPopupHTML(d, QDRService));
              if (selected_link)
                displayTooltip(event);
            });
          };
          // update the contents of the popup tooltip each time the data is polled
          QDRService.management.topology.addUpdatedAction('connectionPopupHTML', updateTooltip);
          QDRService.management.topology.ensureAllEntities(
            [{ entity: 'router.link', force: true},{entity: 'connection'}], function () {
              updateTooltip();
            });
          // show the tooltip
          updateTooltip();
          restart();

        })
        .on('mouseout', function() { // mouse out of a path
          QDRService.management.topology.delUpdatedAction('connectionPopupHTML');
          d3.select('#popover-div')
            .style('display', 'none');
          selected_link = null;
          restart();
        })
        // left click a path
        .on('click', function () {
          d3.event.stopPropagation();
          clearPopups();
        });
      // remove old links
      path.exit().remove();


      // circle (node) group
      // nodes are known by id
      circle = circle.data(nodes.nodes, function(d) {
        return d.name;
      });

      // update existing nodes visual states
      circle.selectAll('circle')
        .classed('highlighted', function(d) {
          return d.highlighted;
        })
        .classed('selected', function(d) {
          return (d === selected_node);
        })
        .classed('fixed', function(d) {
          return d.fixed & 1;
        });

      // add new circle nodes
      let g = circle.enter().append('svg:g')
        .classed('multiple', function(d) {
          return (d.normals && d.normals.length > 1);
        })
        .attr('id', function (d) { return (d.nodeType !== 'normal' ? 'router' : 'client') + '-' + d.index; });

      appendCircle(g)
        .on('mouseover', function(d) {  // mouseover a circle
          QDRService.management.topology.delUpdatedAction('connectionPopupHTML');
          if (d.nodeType === 'normal') {
            showClientTooltip(d, d3.event);
          } else
            showRouterTooltip(d, d3.event);
          if (d === mousedown_node)
            return;
          // enlarge target node
          d3.select(this).attr('transform', 'scale(1.1)');
          if (!selected_node) {
            return;
          }
          // highlight the next-hop route from the selected node to this node
          clearAllHighlights();
          // we need .router.node info to highlight hops
          QDRService.management.topology.ensureAllEntities([{entity: 'router.node', attrs: ['id','nextHop']}], function () {
            mouseover_node = d;  // save this node in case the topology changes so we can restore the highlights
            nextHopHighlight(selected_node, d);
            restart();
          });
        })
        .on('mouseout', function() { // mouse out for a circle
          // unenlarge target node
          d3.select('#popover-div')
            .style('display', 'none');
          d3.select(this).attr('transform', '');
          clearAllHighlights();
          mouseover_node = null;
          restart();
        })
        .on('mousedown', function(d) { // mouse down for circle
          if (d3.event.button !== 0) { // ignore all but left button
            return;
          }
          mousedown_node = d;
          // mouse position relative to svg
          initial_mouse_down_position = d3.mouse(this.parentNode.parentNode.parentNode).slice();
        })
        .on('mouseup', function(d) {  // mouse up for circle
          if (!mousedown_node)
            return;

          selected_link = null;
          // unenlarge target node
          d3.select(this).attr('transform', '');

          // check for drag
          mouseup_node = d;

          let mySvg = this.parentNode.parentNode.parentNode;
          // if we dragged the node, make it fixed
          let cur_mouse = d3.mouse(mySvg);
          if (cur_mouse[0] != initial_mouse_down_position[0] ||
            cur_mouse[1] != initial_mouse_down_position[1]) {
            d.fixed = true;
            nodes.setNodesFixed(d.name, true);
            resetMouseVars();
            restart();
            return;
          }

          // if this node was selected, unselect it
          if (mousedown_node === selected_node) {
            selected_node = null;
          } else {
            if (d.nodeType !== 'normal' && d.nodeType !== 'on-demand')
              selected_node = mousedown_node;
          }
          clearAllHighlights();
          mousedown_node = null;
          if (!$scope.$$phase) $scope.$apply();
          restart(false);

        })
        .on('dblclick', function(d) { // circle
          if (d.fixed) {
            d.fixed = false;
            nodes.setNodesFixed(d.name, false);
            restart(); // redraw the node without a dashed line
            force.start(); // let the nodes move to a new position
          }
        })
        .on('contextmenu', function(d) {  // circle
          $(document).click();
          d3.event.preventDefault();
          let rm = relativeMouse();
          d3.select('#node_context_menu')
            .style({
              display: 'block',
              left: rm.left + 'px',
              top: (rm.top - rm.offset.top) + 'px'
            });
          $timeout( function () {
            $scope.contextNode = d;
          });
        })
        .on('click', function(d) {  // circle
          if (!mouseup_node)
            return;
          // clicked on a circle
          clearPopups();
          if (!d.normals) {
            // circle was a router or a broker
            if (QDRService.utilities.isArtemis(d)) {
              const artemisPath = '/jmx/attributes?tab=artemis&con=Artemis';
              window.location = $location.protocol() + '://localhost:8161/hawtio' + artemisPath;
            }
            return;
          }
          d3.event.stopPropagation();
        });

      appendContent(g);
      //appendTitle(g);

      // remove old nodes
      circle.exit().remove();

      // add text to client circles if there are any that represent multiple clients
      svg.selectAll('.subtext').remove();
      let multiples = svg.selectAll('.multiple');
      multiples.each(function(d) {
        let g = d3.select(this);
        g.append('svg:text')
          .attr('x', radiusNormal + 3)
          .attr('y', Math.floor(radiusNormal / 2))
          .attr('class', 'subtext')
          .text('x ' + d.normals.length);
      });
      // call createLegend in timeout because:
      // If we create the legend right away, then it will be destroyed when the accordian
      // gets initialized as the page loads.
      $timeout(createLegend);

      if (!mousedown_node || !selected_node)
        return;

      if (!start)
        return;
      // set the graph in motion
      //QDRLog.debug("mousedown_node is " + mousedown_node);
      force.start();

    }
    let createLegend = function () {
      // dynamically create the legend based on which node types are present
      // the legend
      d3.select('#topo_svg_legend svg').remove();
      lsvg = d3.select('#topo_svg_legend')
        .append('svg')
        .attr('id', 'svglegend');
      lsvg = lsvg.append('svg:g')
        .attr('transform', `translate(${(radii['inter-router'] + 2)},${(radii['inter-router'] + 2)})`)
        .selectAll('g');
      let legendNodes = new Nodes(QDRService, QDRLog);
      legendNodes.addUsing('Router', '', 'inter-router', '', undefined, 0, 0, 0, 0, false, {});

      if (!svg.selectAll('circle.console').empty()) {
        legendNodes.addUsing('Console', 'Console', 'normal', '', undefined, 0, 0, 1, 0, false, {
          console_identifier: 'Dispatch console'
        });
      }
      if (!svg.selectAll('circle.client.in').empty()) {
        legendNodes.addUsing('Sender', 'Sender', 'normal', '', undefined, 0, 0, 2, 0, false, {}).cdir = 'in';
      }
      if (!svg.selectAll('circle.client.out').empty()) {
        legendNodes.addUsing('Receiver', 'Receiver', 'normal', '', undefined, 0, 0, 3, 0, false, {}).cdir = 'out';
      }
      if (!svg.selectAll('circle.client.inout').empty()) {
        legendNodes.addUsing('Sender/Receiver', 'Sender/Receiver', 'normal', '', undefined, 0, 0, 4, 0, false, {}).cdir = 'both';
      }
      if (!svg.selectAll('circle.qpid-cpp').empty()) {
        legendNodes.addUsing('Qpid broker', 'Qpid broker', 'route-container', '', undefined, 0, 0, 5, 0, false, {
          product: 'qpid-cpp'
        });
      }
      if (!svg.selectAll('circle.artemis').empty()) {
        legendNodes.addUsing('Artemis broker', 'Artemis broker', 'route-container', '', undefined, 0, 0, 6, 0, false,
          {product: 'apache-activemq-artemis'});
      }
      if (!svg.selectAll('circle.route-container').empty()) {
        legendNodes.addUsing('Service', 'Service', 'route-container', 'external-service', undefined, 0, 0, 7, 0, false,
          {product: ' External Service'});
      }
      lsvg = lsvg.data(legendNodes.nodes, function(d) {
        return d.key;
      });
      let lg = lsvg.enter().append('svg:g')
        .attr('transform', function(d, i) {
          // 45px between lines and add 10px space after 1st line
          return 'translate(0, ' + (45 * i + (i > 0 ? 10 : 0)) + ')';
        });

      appendCircle(lg);
      appendContent(lg);
      appendTitle(lg);
      lg.append('svg:text')
        .attr('x', 35)
        .attr('y', 6)
        .attr('class', 'label')
        .text(function(d) {
          return d.key;
        });
      lsvg.exit().remove();
      let svgEl = document.getElementById('svglegend');
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
        svgEl.style.height = (bb.y + bb.height) + 'px';
        svgEl.style.width = (bb.x + bb.width) + 'px';
      }
    };
    let appendCircle = function(g) {
      // add new circles and set their attr/class/behavior
      return g.append('svg:circle')
        .attr('class', 'node')
        .attr('r', function(d) {
          return radii[d.nodeType];
        })
        .attr('fill', function (d) {
          if (d.cdir === 'both' && !QDRService.utilities.isConsole(d)) {
            return 'url(' + urlPrefix + '#half-circle)';
          }
          return null;
        })
        .classed('fixed', function(d) {
          return d.fixed & 1;
        })
        .classed('normal', function(d) {
          return d.nodeType == 'normal' || QDRService.utilities.isConsole(d);
        })
        .classed('in', function(d) {
          return d.cdir == 'in';
        })
        .classed('out', function(d) {
          return d.cdir == 'out';
        })
        .classed('inout', function(d) {
          return d.cdir == 'both';
        })
        .classed('inter-router', function(d) {
          return d.nodeType == 'inter-router';
        })
        .classed('on-demand', function(d) {
          return d.nodeType == 'on-demand';
        })
        .classed('console', function(d) {
          return QDRService.utilities.isConsole(d);
        })
        .classed('artemis', function(d) {
          return QDRService.utilities.isArtemis(d);
        })
        .classed('qpid-cpp', function(d) {
          return QDRService.utilities.isQpid(d);
        })
        .classed('route-container', function (d) {
          return (!QDRService.utilities.isArtemis(d) && !QDRService.utilities.isQpid(d) && d.nodeType === 'route-container');
        })
        .classed('client', function(d) {
          return d.nodeType === 'normal' && !d.properties.console_identifier;
        });
    };
    let appendContent = function(g) {
      // show node IDs
      g.append('svg:text')
        .attr('x', 0)
        .attr('y', function(d) {
          let y = 7;
          if (QDRService.utilities.isArtemis(d))
            y = 8;
          else if (QDRService.utilities.isQpid(d))
            y = 9;
          else if (d.nodeType === 'inter-router')
            y = 4;
          else if (d.nodeType === 'route-container')
            y = 5;
          return y;
        })
        .attr('class', 'id')
        .classed('console', function(d) {
          return QDRService.utilities.isConsole(d);
        })
        .classed('normal', function(d) {
          return d.nodeType === 'normal';
        })
        .classed('on-demand', function(d) {
          return d.nodeType === 'on-demand';
        })
        .classed('artemis', function(d) {
          return QDRService.utilities.isArtemis(d);
        })
        .classed('qpid-cpp', function(d) {
          return QDRService.utilities.isQpid(d);
        })
        .text(function(d) {
          if (QDRService.utilities.isConsole(d)) {
            return '\uf108'; // icon-desktop for this console
          } else if (QDRService.utilities.isArtemis(d)) {
            return '\ue900';
          } else if (QDRService.utilities.isQpid(d)) {
            return '\ue901';
          } else if (d.nodeType === 'route-container') {
            return d.properties.product ? d.properties.product[0].toUpperCase() : 'S';
          } else if (d.nodeType === 'normal')
            return '\uf109'; // icon-laptop for clients
          return d.name.length > 7 ? d.name.substr(0, 6) + '...' : d.name;
        });
    };
    let appendTitle = function(g) {
      g.append('svg:title').text(function(d) {
        return generateTitle(d);
      });
    };

    let generateTitle = function (d) {
      let x = '';
      if (d.normals && d.normals.length > 1)
        x = ' x ' + d.normals.length;
      if (QDRService.utilities.isConsole(d))
        return 'Dispatch console' + x;
      else if (QDRService.utilities.isArtemis(d))
        return 'Broker - Artemis' + x;
      else if (d.properties.product == 'qpid-cpp')
        return 'Broker - qpid-cpp' + x;
      else if (d.cdir === 'in')
        return 'Sender' + x;
      else if (d.cdir === 'out')
        return 'Receiver' + x;
      else if (d.cdir === 'both')
        return 'Sender/Receiver' + x;
      else if (d.nodeType === 'normal')
        return 'client' + x;
      else if (d.nodeType === 'on-demand')
        return 'broker';
      else if (d.properties.product) {
        return d.properties.product;
      }
      else {
        return '';
      }
    };

    let showClientTooltip = function (d, event) {
      let type = generateTitle(d);
      let title = `<table class="popupTable"><tr><td>Type</td><td>${type}</td></tr>`;
      if (!d.normals || d.normals.length < 2)
        title += ('<tr><td>Host</td><td>' + d.host + '</td></tr>');
      title += '</table>';
      showToolTip(title, event);
    };

    let showRouterTooltip = function (d, event) {
      QDRService.management.topology.ensureEntities(d.key, [
        {entity: 'listener', attrs: ['role', 'port', 'http']},
        {entity: 'router', attrs: ['name', 'version', 'hostName']}
      ], function () {
        // update all the router title text
        let nodes = QDRService.management.topology.nodeInfo();
        let node = nodes[d.key];
        let listeners = node['listener'];
        let router = node['router'];
        let r = QDRService.utilities.flatten(router.attributeNames, router.results[0]);
        let title = '<table class="popupTable">';
        title += ('<tr><td>Router</td><td>' + r.name + '</td></tr>');
        if (r.hostName)
          title += ('<tr><td>Host Name</td><td>' + r.hostHame + '</td></tr>');
        title += ('<tr><td>Version</td><td>' + r.version + '</td></tr>');
        let ports = [];
        for (let l=0; l<listeners.results.length; l++) {
          let listener = QDRService.utilities.flatten(listeners.attributeNames, listeners.results[l]);
          if (listener.role === 'normal') {
            ports.push(listener.port+'');
          }
        }
        if (ports.length > 0) {
          title += ('<tr><td>Ports</td><td>' + ports.join(', ') + '</td></tr>');
        }
        title += '</table>';
        showToolTip(title, event);
      });
    };
    let showToolTip = function (title, event) {
      // show the tooltip
      $timeout ( function () {
        $scope.trustedpopoverContent = $sce.trustAsHtml(title);
        displayTooltip(event);
      });
    };

    let displayTooltip = function (event) {
      $timeout( function () {
        let top = $('#topology').offset().top - 5;
        let width = $('#topology').width();
        d3.select('#popover-div')
          .style('visibility', 'hidden')
          .style('display', 'block')
          .style('left', (event.pageX+5)+'px')
          .style('top', (event.pageY-top)+'px');
        let pwidth = $('#popover-div').width();
        d3.select('#popover-div')
          .style('visibility', 'visible')
          .style('left',(Math.min(width-pwidth, event.pageX+5) + 'px'));
      });
    };

    function hasChanged() {
      // Don't update the underlying topology diagram if we are adding a new node.
      // Once adding is completed, the topology will update automatically if it has changed
      let nodeInfo = QDRService.management.topology.nodeInfo();
      // don't count the nodes without connection info
      let cnodes = Object.keys(nodeInfo).filter ( function (node) {
        return (nodeInfo[node]['connection']);
      });
      let routers = nodes.nodes.filter( function (node) {
        return node.nodeType === 'inter-router';
      });
      if (routers.length > cnodes.length) {
        return -1;
      }


      if (cnodes.length != Object.keys(savedKeys).length) {
        return cnodes.length > Object.keys(savedKeys).length ? 1 : -1;
      }
      // we may have dropped a node and added a different node in the same update cycle
      for (let i=0; i<cnodes.length; i++) {
        let key = cnodes[i];
        // if this node isn't in the saved node list
        if (!savedKeys.hasOwnProperty(key))
          return 1;
        // if the number of connections for this node chaanged
        if (!nodeInfo[key]['connection'])
          return -1;
        if (nodeInfo[key]['connection'].results.length != savedKeys[key]) {
          return -1;
        }
      }
      return 0;
    }

    function saveChanged() {
      savedKeys = {};
      let nodeInfo = QDRService.management.topology.nodeInfo();
      // save the number of connections per node
      for (let key in nodeInfo) {
        if (nodeInfo[key]['connection'])
          savedKeys[key] = nodeInfo[key]['connection'].results.length;
      }
    }
    // we are about to leave the page, save the node positions
    $rootScope.$on('$locationChangeStart', function() {
      //QDRLog.debug("locationChangeStart");
      nodes.savePositions();
    });
    // When the DOM element is removed from the page,
    // AngularJS will trigger the $destroy event on
    // the scope
    $scope.$on('$destroy', function() {
      //QDRLog.debug("scope on destroy");
      nodes.savePositions();
      QDRService.management.topology.setUpdateEntities([]);
      QDRService.management.topology.stopUpdating();
      QDRService.management.topology.delUpdatedAction('normalsStats');
      QDRService.management.topology.delUpdatedAction('topology');
      QDRService.management.topology.delUpdatedAction('connectionPopupHTML');

      d3.select('#SVG_ID').remove();
      window.removeEventListener('resize', resize);
      traffic.stop();
    });

    function handleInitialUpdate() {
      // we only need to update connections during steady-state
      QDRService.management.topology.setUpdateEntities(['connection']);
      // we currently have all entities available on all routers
      saveChanged();
      initForceGraph();
      // after the graph is displayed fetch all .router.node info. This is done so highlighting between nodes
      // doesn't incur a delay
      QDRService.management.topology.addUpdateEntities({entity: 'router.node', attrs: ['id','nextHop']});
      // call this function every time a background update is done
      QDRService.management.topology.addUpdatedAction('topology', function() {
        let changed = hasChanged();
        // there is a new node, we need to get all of it's entities before drawing the graph
        if (changed > 0) {
          QDRService.management.topology.delUpdatedAction('topology');
          animate = true;
          setupInitialUpdate();
        } else if (changed === -1) {
          // we lost a node (or a client), we can draw the new svg immediately
          animate = false;
          saveChanged();
          let nodeInfo = QDRService.management.topology.nodeInfo();
          forceData.nodes = nodes = new Nodes(QDRService, QDRLog);
          animate = nodes.initialize(nodeInfo, localStorage, width, height);

          let unknowns = [];
          forceData.links = links = new Links(QDRService, QDRLog);
          if (links.initializeLinks(nodeInfo, nodes, unknowns, localStorage, height)) {
            animate = true;
          }
          if (unknowns.length > 0) {
            resolveUnknowns(nodeInfo, unknowns);
          }
          else {
            force.nodes(nodes.nodes).links(links.links).start();
            restart();
          }
          //initForceGraph();
        } else {
          //QDRLog.debug("topology didn't change")
        }

      });
    }
    function setupInitialUpdate() {
      // make sure all router nodes have .connection info. if not then fetch any missing info
      QDRService.management.topology.ensureAllEntities(
        [{entity: 'connection'}],
        handleInitialUpdate);
    }
    if (!QDRService.management.connection.is_connected()) {
      // we are not connected. we probably got here from a bookmark or manual page reload
      QDRRedirectWhenConnected($location, 'topology');
      return;
    }



    animate = true;
    setupInitialUpdate();
    QDRService.management.topology.startUpdating(false);

  }
}
TopologyController.$inject = ['QDRService', '$scope', '$log', '$rootScope', '$location', '$timeout', '$uibModal', '$sce'];

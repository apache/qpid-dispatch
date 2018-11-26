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
import { QDRLogger, QDRRedirectWhenConnected, QDRTemplatePath } from '../qdrGlobals.js';
import { Traffic } from './traffic.js';
import { separateAddresses } from '../chord/filters.js';
import { Nodes } from './nodes.js';
import { Links } from './links.js';
import { nextHop, connectionPopupHTML, addStyles } from './topoUtils.js';
import { BackgroundMap } from './map.js';
/**
 * @module QDR
 */
export class TopologyController {
  constructor(QDRService, $scope, $log, $rootScope, $location, $timeout, $uibModal, $sce) {
    this.controllerName = 'QDR.TopologyController';

    let QDRLog = new QDRLogger($log, 'TopologyController');
    const TOPOOPTIONSKEY = 'topoOptions';

    //  - nodes is an array of router/client info. these are the circles
    //  - links is an array of connections between the routers. these are the lines with arrows
    let nodes = new Nodes(QDRLog);
    let links = new Links(QDRLog);
    let forceData = {nodes: nodes, links: links};

    $scope.legendOptions = angular.fromJson(localStorage[TOPOOPTIONSKEY]) || {showTraffic: false, trafficType: 'dots', mapOpen: false, legendOpen: true};
    if (typeof $scope.legendOptions.mapOpen == 'undefined')
      $scope.legendOptions.mapOpen = false;
    if (typeof $scope.legendOptions.legendOpen == 'undefined')
      $scope.legendOptions.legendOpen = false;
    let backgroundMap = new BackgroundMap($scope, 
      // notify: called each time a pan/zoom is performed
      function () {
        if ($scope.legend.status.mapOpen) {
          // set all the nodes' x,y position based on their saved lon,lat
          nodes.setXY(backgroundMap);
          nodes.savePositions();
          // redraw the nodes in their x,y position and let non-fixed nodes bungie
          force.start();
          clearPopups();
        }
      });
    // urlPrefix is used when referring to svg:defs
    let urlPrefix = $location.absUrl();
    urlPrefix = urlPrefix.split('#')[0];

    if (!$scope.legendOptions.trafficType)
      $scope.legendOptions.trafficType = 'dots';
    $scope.legend = {status: {legendOpen: true, optionsOpen: true, mapOpen: false}};
    $scope.legend.status.optionsOpen = $scope.legendOptions.showTraffic;
    $scope.legend.status.mapOpen = $scope.legendOptions.mapOpen;
    let traffic = new Traffic($scope, $timeout, QDRService, separateAddresses, 
      Nodes.radius('inter-router'), forceData, $scope.legendOptions.trafficType, urlPrefix);

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
        traffic.setAnimationType($scope.legendOptions.trafficType, separateAddresses, Nodes.radius('inter-router'));
        traffic.start();
      }
    });
    $scope.$watch('legend.status.mapOpen', function (newvalue, oldvalue) {
      $scope.legendOptions.mapOpen = $scope.legend.status.mapOpen;
      localStorage[TOPOOPTIONSKEY] = JSON.stringify($scope.legendOptions);
      // map was shown
      if ($scope.legend.status.mapOpen && backgroundMap.initialized) {
        // respond to pan/zoom events
        backgroundMap.restartZoom();
        // set the main_container div's background color to the ocean color
        backgroundMap.updateOceanColor();
        d3.select('g.geo')
          .style('opacity', 1);
      } else {
        if (newvalue !== oldvalue)
          backgroundMap.cancelZoom();
        // hide the map and reset the background color
        d3.select('g.geo')
          .style('opacity', 0);
        d3.select('#main_container')
          .style('background-color', '#FFF');
      }
    });

    // mouse event vars
    let selected_node = null,
      mouseover_node = null,
      mouseup_node = null,
      initial_mouse_down_position = null;

    $scope.schema = 'Not connected';
    $scope.current_node = null,
    $scope.mousedown_node = null,

    $scope.contextNode = null; // node that is associated with the current context menu
    $scope.isRight = function(mode) {
      return mode.right;
    };

    function doDialog(d) {
      $uibModal.open({
        backdrop: true,
        keyboard: true,
        backdropClick: true,
        templateUrl: QDRTemplatePath + 'tmplClientDetail.html',
        controller: 'QDR.DetailDialogController',
        resolve: {
          d: function() {
            return d;
          }
        }
      }).result.then(function() {
      });
    }

    $scope.setFixed = function(b) {
      if ($scope.contextNode) {
        $scope.contextNode.setFixed(b);
        nodes.savePositions();
        nodes.saveLonLat(backgroundMap, $scope.contextNode);
      }
      if (!b)
        animate = true;
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

    // initialize the nodes and links array from the QDRService.topology._nodeInfo object
    var initForceGraph = function() {
      forceData.nodes = nodes = new Nodes(QDRLog);
      forceData.links = links = new Links(QDRLog);
      let nodeInfo = QDRService.management.topology.nodeInfo();
      let nodeCount = Object.keys(nodeInfo).length;

      let oldSelectedNode = selected_node;
      let oldMouseoverNode = mouseover_node;
      mouseover_node = null;
      selected_node = null;

      d3.select('#SVG_ID').remove();
      svg = d3.select('#topology')
        .append('svg')
        .attr('id', 'SVG_ID')
        .attr('width', width)
        .attr('height', height)
        .on('click', function () {
          clearPopups();
        });

      // the legend
      d3.select('#topo_svg_legend svg').remove();
      lsvg = d3.select('#topo_svg_legend')
        .append('svg')
        .attr('id', 'svglegend');
      lsvg = lsvg.append('svg:g')
        .attr('transform', `translate(${Nodes.maxRadius()}, ${Nodes.maxRadius()})`)
        .selectAll('g');

      // mouse event vars
      $scope.mousedown_node = null;
      mouseup_node = null;

      // initialize the list of nodes
      animate = nodes.initialize(nodeInfo, localStorage, width, height);
      nodes.savePositions();
      // read the map data from the data file and build the map layer
      backgroundMap.init($scope, svg, width, height)
        .then( function () {
          nodes.saveLonLat(backgroundMap);
          backgroundMap.setMapOpacity($scope.legend.status.mapOpen);
        });

      // initialize the list of links
      let unknowns = [];
      if (links.initialize(nodeInfo, nodes, unknowns, localStorage, height)) {
        animate = true;
      }
      $scope.schema = QDRService.management.schema();
      // init D3 force layout
      force = d3.layout.force()
        .nodes(nodes.nodes)
        .links(links.links)
        .size([width, height])
        .linkDistance(function(d) { return nodes.linkDistance(d, nodeCount); })
        .charge(function(d) { return nodes.charge(d, nodeCount); })
        .friction(.10)
        .gravity(function(d) { return nodes.gravity(d, nodeCount); })
        .on('tick', tick)
        .on('end', function () {nodes.savePositions(); nodes.saveLonLat(backgroundMap);})
        .start();

      // This section adds in the arrows
      // Generate a marker for each combination of:
      //  start|end, ''|selected highlighted, and each possible node radius
      {
        let sten = ['start', 'end'];
        let states = ['', 'selected', 'highlighted', 'unknown'];
        let radii = Nodes.discrete();
        let defs = [];
        for (let isten=0; isten<sten.length; isten++) {
          for (let istate=0; istate<states.length; istate++) {
            for (let iradii=0; iradii<radii.length; iradii++) {
              defs.push({sten: sten[isten], state: states[istate], r: radii[iradii]});
            }
          }
        }
        svg.append('svg:defs').attr('class', 'marker-defs').selectAll('marker')
          .data(defs)
          .enter().append('svg:marker')
          .attr('id', function (d) { return [d.sten, d.state, d.r].join('-'); })
          .attr('viewBox', '0 -5 10 10')
          .attr('refX', function (d) { return Nodes.refX(d.sten, d.r); })
          .attr('markerWidth', 14)
          .attr('markerHeight', 14)
          .attr('markerUnits', 'userSpaceOnUse')
          .attr('orient', 'auto')
          .append('svg:path')
          .attr('d', function (d) {
            return d.sten === 'end' ? 'M 0 -5 L 10 0 L 0 5 z' : 'M 10 -5 L 0 0 L 10 5 z';
          });
        addStyles (sten, {selected: '#33F', highlighted: '#6F6', unknown: '#888'}, radii);
      }
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
      path = svg.append('svg:g').attr('class', 'links').selectAll('g'),
      circle = svg.append('svg:g').attr('class', 'nodes').selectAll('g');

      // app starts here
      if (unknowns.length === 0)
        restart();
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
      continueForce(Nodes.forceScale(nodeCount, [0, 200]));  // give large graphs time to settle down
    };

    // To start up quickly, we only get the connection info for each router.
    // That means we don't have the router.link info when links.initialize() is first called.
    // The router.link info is needed to determine which direction the arrows between routers
    // and client should point. (Direction between interior routers is determined by connection.dir)
    // So, the first time through links.initialize() we keep track of the nodes for which we 
    // need router.link info and fill in that info here.
    var resolveUnknowns = function (nodeInfo, unknowns) {
      let unknownNodes = {};
      // collapse the unknown nodes using an object
      for (let i=0; i<unknowns.length; ++i) {
        unknownNodes[unknowns[i]] = 1;
      }
      unknownNodes = Object.keys(unknownNodes);
      QDRService.management.topology.ensureEntities(unknownNodes, 
        [{entity: 'router.link', 
          attrs: ['linkType','connectionId','linkDir','owningAddr'], 
          force: true}], 
        function () {
          let nodeInfo = QDRService.management.topology.nodeInfo();
          forceData.nodes = nodes = new Nodes(QDRLog);
          nodes.initialize(nodeInfo, localStorage, width, height);
          forceData.links = links = new Links(QDRLog);
          links.initialize(nodeInfo, nodes, [], localStorage, height);
          animate = true;
          force.nodes(nodes.nodes).links(links.links).start();
          nodes.saveLonLat(backgroundMap);
          restart();
        });
    };

    function resetMouseVars() {
      $scope.mousedown_node = null;
      mouseover_node = null;
      mouseup_node = null;
    }

    // update force layout (called automatically each iteration)
    function tick() {
      // move the circles
      circle.attr('transform', function(d) {
        // don't let the edges of the circle go beyond the edges of the svg
        let r = Nodes.radius(d.nodeType);
        d.x = Math.max(Math.min(d.x, width - r), r);
        d.y = Math.max(Math.min(d.y, height - r), r);
        return `translate(${d.x},${d.y})`;
      });

      // draw lines from node centers
      path.selectAll('path')
        .attr('d', function(d) {
          return `M${d.source.x},${d.source.y}L${d.target.x},${d.target.y}`;
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
      d3.select('#popover-div').style('display', 'none');
    }

    function clearAllHighlights() {
      links.clearHighlighted();
      nodes.clearHighlighted();
    }
    // Takes the forceData.nodes and forceData.links array and creates svg elements
    // Also updates any existing svg elements based on the updated values in forceData.nodes
    // and forceData.links
    function restart() {
      if (!circle)
        return;
      circle.call(force.drag);

      // path is a selection of all g elements under the g.links svg:group
      // here we associate the links.links array with the {g.links g} selection
      // based on the link.uid
      path = path.data(links.links, function(d) {return d.uid;});

      // update each existing {g.links g.link} element
      path.select('.link')
        .classed('selected', function(d) {
          return d.selected;
        })
        .classed('highlighted', function(d) {
          return d.highlighted;
        })
        .classed('unknown', function (d) {
          return !d.right && !d.left;
        });

      // reset the markers based on current highlighted/selected
      if (!$scope.legend.status.optionsOpen || $scope.legendOptions.trafficType === 'dots') {
        path.select('.link')
          .attr('marker-end', function(d) {
            return d.right ? `url(${urlPrefix}#end${d.markerId('end')})` : null;
          })
          .attr('marker-start', function(d) {
            return (d.left || (!d.left && !d.right)) ? `url(${urlPrefix}#start${d.markerId('start')})` : null;
          });
      }
      // add new links. if a link with a new uid is found in the data, add a new path
      let enterpath = path.enter().append('g')
        .on('mouseover', function(d) { // mouse over a path
          let event = d3.event;
          d.selected = true;
          let updateTooltip = function () {
            $timeout(function () {
              if (d.selected) {
                $scope.trustedpopoverContent = $sce.trustAsHtml(connectionPopupHTML(d, QDRService));
                displayTooltip(event);
              }
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
        .on('mouseout', function(d) { // mouse out of a path
          QDRService.management.topology.delUpdatedAction('connectionPopupHTML');
          d3.select('#popover-div')
            .style('display', 'none');
          d.selected = false;
          connectionPopupHTML();
          restart();
        })
        // left click a path
        .on('click', function () {
          d3.event.stopPropagation();
          clearPopups();
        });

      enterpath.append('path')
        .attr('class', 'link')
        .attr('marker-end', function(d) {
          return d.right ? `url(${urlPrefix}#end${d.markerId('end')})` : null;
        })
        .attr('marker-start', function(d) {
          return (d.left || (!d.left && !d.right)) ? `url(${urlPrefix}#start${d.markerId('start')})` : null;
        })
        .attr('id', function (d) {
          const si = d.source.uid(QDRService);
          const ti = d.target.uid(QDRService);
          return ['path', si, ti].join('-');
        })
        .classed('unknown', function (d) {
          return !d.right && !d.left;
        });

      enterpath.append('path')
        .attr('class', 'hittarget');

      // remove old links
      path.exit().remove();


      // circle (node) group
      // nodes are known by router id, or for groups, by the router id + 1st connectionId
      circle = circle.data(nodes.nodes, function(d) {
        return d.uid(QDRService);
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
      circle
        .classed('multiple', function (d) {
          return (d.normals && d.normals.length > 1);
        });

      // add new circle nodes
      let g = circle.enter().append('svg:g')
        .classed('multiple', function(d) {
          return (d.normals && d.normals.length > 1);
        })
        .attr('id', function (d) { return (d.nodeType !== 'normal' ? 'router' : 'client') + '-' + d.index; });

      appendCircle(g)
        .on('mouseover', function(d) {  // mouseover a circle
          $scope.current_node = d;
          QDRService.management.topology.delUpdatedAction('connectionPopupHTML');
          let e = d3.event;
          d.toolTip(QDRService.management.topology)
            .then( function (toolTip) {
              showToolTip(toolTip, e);
            });
          if (d === $scope.mousedown_node)
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
          $scope.current_node = null;
          // unenlarge target node
          d3.select('#popover-div')
            .style('display', 'none');
          d3.select(this).attr('transform', '');
          clearAllHighlights();
          mouseover_node = null;
          restart();
        })
        .on('mousedown', function(d) { // mouse down for circle
          backgroundMap.cancelZoom();
          $scope.current_node = d;
          if (d3.event.button !== 0) { // ignore all but left button
            return;
          }
          $scope.mousedown_node = d;
          // mouse position relative to svg
          initial_mouse_down_position = d3.mouse(this.parentNode.parentNode.parentNode).slice();
        })
        .on('mouseup', function(d) {  // mouse up for circle
          backgroundMap.restartZoom();
          if (!$scope.mousedown_node)
            return;

          // unenlarge target node
          d3.select(this).attr('transform', '');

          // check for drag
          mouseup_node = d;

          let mySvg = d3.select('#SVG_ID').node();
          // if we dragged the node, make it fixed
          let cur_mouse = d3.mouse(mySvg);
          if (cur_mouse[0] != initial_mouse_down_position[0] ||
            cur_mouse[1] != initial_mouse_down_position[1]) {
            d.setFixed(true);
            nodes.savePositions(d);
            nodes.saveLonLat(backgroundMap, d);
            console.log('savedLonLat for fixed node');
            resetMouseVars();
            restart();
            return;
          }

          // if this node was selected, unselect it
          if ($scope.mousedown_node === selected_node) {
            selected_node = null;
          } else {
            if (d.nodeType !== 'normal' && 
                d.nodeType !== 'on-demand' && 
                d.nodeType !== 'edge' &&
                d.nodeTYpe !== '_edge')
              selected_node = $scope.mousedown_node;
          }
          clearAllHighlights();
          $scope.mousedown_node = null;
          if (!$scope.$$phase) $scope.$apply();
          // handle clicking on nodes that represent multiple sub-nodes
          if (d.normals && !d.isConsole && !d.isArtemis) {
            doDialog(d);
          }
          restart();

        })
        .on('dblclick', function(d) { // circle
          d3.event.preventDefault();
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
        let r = Nodes.radius(d.nodeType);
        g.append('svg:text')
          .attr('x', r + 4)
          .attr('y', Math.floor((r / 2) - 4))
          .attr('class', 'subtext')
          .text('* ' + d.normals.length);
      });
      // call createLegend in timeout because:
      // If we create the legend right away, then it will be destroyed when the accordian
      // gets initialized as the page loads.
      $timeout(createLegend);

      if (!$scope.mousedown_node || !selected_node)
        return;

      // set the graph in motion
      //QDRLog.debug("mousedown_node is " + mousedown_node);
      force.start();

    }
    let createLegend = function () {
      // dynamically create the legend based on which node types are present
      d3.select('#topo_svg_legend svg').remove();
      lsvg = d3.select('#topo_svg_legend')
        .append('svg')
        .attr('id', 'svglegend');
      lsvg = lsvg.append('svg:g')
        .attr('transform', `translate(${Nodes.maxRadius()}, ${Nodes.maxRadius()})`)
        .selectAll('g');
      let legendNodes = new Nodes(QDRLog);
      legendNodes.addUsing('Router', '', 'inter-router', undefined, 0, 0, 0, 0, false, {});
      if (!svg.selectAll('circle.edge').empty() || !svg.selectAll('circle._edge').empty()) {
        legendNodes.addUsing('Router', 'Edge', 'edge', undefined, 0, 0, 1, 0, false, {});
      }
      if (!svg.selectAll('circle.console').empty()) {
        legendNodes.addUsing('Console', 'Console', 'normal', undefined, 0, 0, 2, 0, false, {
          console_identifier: 'Dispatch console'
        });
      }
      if (!svg.selectAll('circle.client.in').empty()) {
        legendNodes.addUsing('Sender', 'Sender', 'normal', undefined, 0, 0, 3, 0, false, {}).cdir = 'in';
      }
      if (!svg.selectAll('circle.client.out').empty()) {
        legendNodes.addUsing('Receiver', 'Receiver', 'normal', undefined, 0, 0, 4, 0, false, {}).cdir = 'out';
      }
      if (!svg.selectAll('circle.client.inout').empty()) {
        legendNodes.addUsing('Sender/Receiver', 'Sender/Receiver', 'normal', undefined, 0, 0, 5, 0, false, {}).cdir = 'both';
      }
      if (!svg.selectAll('circle.qpid-cpp').empty()) {
        legendNodes.addUsing('Qpid broker', 'Qpid broker', 'route-container', undefined, 0, 0, 6, 0, false, {
          product: 'qpid-cpp'
        });
      }
      if (!svg.selectAll('circle.artemis').empty()) {
        legendNodes.addUsing('Artemis broker', 'Artemis broker', 'route-container', undefined, 0, 0, 7, 0, false,
          {product: 'apache-activemq-artemis'});
      }
      if (!svg.selectAll('circle.route-container').empty()) {
        legendNodes.addUsing('Service', 'Service', 'route-container', undefined, 0, 0, 8, 0, false,
          {product: ' External Service'});
      }
      lsvg = lsvg.data(legendNodes.nodes, function(d) {
        return d.key + d.name;
      });
      let cury = 0;
      let lg = lsvg.enter().append('svg:g')
        .attr('transform', function(d) {
          let t = `translate(0, ${cury})`;
          cury += (Nodes.radius(d.nodeType) * 2 + 10);
          return t;
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
          return Nodes.radius(d.nodeType);
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
          return d.nodeType == 'inter-router' || d.nodeType === '_topo';
        })
        .classed('on-demand', function(d) {
          return d.nodeType == 'on-demand';
        })
        .classed('edge', function(d) {
          return d.nodeType === 'edge' || d.nodeType === '_edge';
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
          else if (d.nodeType === 'edge' || d.nodeType === '_edge')
            y = 4;
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
        .classed('edge', function(d) {
          return d.nodeType === 'edge';
        })
        .classed('edge', function(d) {
          return d.nodeType === '_edge';
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
          } else if (d.nodeType === 'normal') {
            return '\uf109'; // icon-laptop for clients
          } else if (d.nodeType === 'edge' || d.nodeType === '_edge') {
            return 'Edge';
          }
          return d.name.length > 7 ?
            d.name.substr(0, 3) + '...' + d.name.substr(d.name.length-3, 3) :
            d.name;
        });
    };
    let appendTitle = function(g) {
      g.append('svg:title').text(function(d) {
        return d.title();
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
          .style('left',(Math.min(width-pwidth, event.pageX+5) + 'px'))
          .on('mouseout', function () {
            d3.select(this)
              .style('display', 'none');
          });
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
        return node.nodeType === '_topo';
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
    function destroy () {
      nodes.savePositions();
      QDRService.management.topology.setUpdateEntities([]);
      QDRService.management.topology.stopUpdating();
      QDRService.management.topology.delUpdatedAction('normalsStats');
      QDRService.management.topology.delUpdatedAction('topology');
      QDRService.management.topology.delUpdatedAction('connectionPopupHTML');

      d3.select('#SVG_ID').remove();
      window.removeEventListener('resize', resize);
      traffic.stop();
      d3.select('#main_container')
        .style('background-color', 'white');
    }
    // When the DOM element is removed from the page,
    // AngularJS will trigger the $destroy event on
    // the scope
    $scope.$on('$destroy', function() {
      destroy();
    });
    // we are about to leave the page, save the node positions
    $rootScope.$on('$locationChangeStart', function() {
      destroy();
    });

    function handleInitialUpdate() {
      // we only need to update connections during steady-state
      QDRService.management.topology.setUpdateEntities(['connection']);
      // we currently have all entities available on all routers
      saveChanged();
      initForceGraph();
      // after the graph is displayed fetch all .router.node info. This is done so highlighting between nodes
      // doesn't incur a delay
      QDRService.management.topology.addUpdateEntities([
        {entity: 'router.node', attrs: ['id','nextHop']}
      ]);
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
          forceData.nodes = nodes = new Nodes(QDRLog);
          animate = nodes.initialize(nodeInfo, localStorage, width, height);

          let unknowns = [];
          forceData.links = links = new Links(QDRLog);
          if (links.initialize(nodeInfo, nodes, unknowns, localStorage, height)) {
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

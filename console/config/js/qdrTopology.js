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
/**
 * @module QDR
 */
var QDR = (function(QDR) {

  /**
   * @method TopologyController
   *
   * Controller that handles the QDR topology page
   */
  QDR.module.controller("QDR.TopologyController", ['$scope', '$rootScope', 'QDRService', '$location', '$timeout', '$uibModal',
    function($scope, $rootScope, QDRService, $location, $timeout, $uibModal) {

      var settings = {baseName: "A", http_port: 5673, normal_port: 22000, internal_port: 2000, default_host: "0.0.0.0", artemis_port: 61616, qpid_port: 5672}
      var sections = ['log', 'connector', 'sslProfile', 'listener', 'address']
      // mouse event vars
      var selected_link = null,
        mousedown_link = null,
        mousedown_node = null,
        mouseover_node = null,
        mouseup_node = null,
        initial_mouse_down_position = null;

      $scope.selected_node = null
      $scope.selected_nodes = {}
      $scope.mockTopologies = []
      $scope.mockTopologyDir = ""
      $scope.ansible = false

      $scope.Publish = function () {
        doOperation("PUBLISH", function (response) {
          Core.notification('info', $scope.mockTopologyDir + " published");
          QDR.log.info("published " + $scope.mockTopologyDir)
        })
      }
      var startDeploy = function (hosts) {
        var extra = {}
        for (var i=0; i<hosts.length; i++) {
          if (hosts[i].pass !== '')
            extra['ansible_become_pass_' + hosts[i].name] = hosts[i].pass
        }
        // send the deploy command
        doOperation("DEPLOY", function (response) {
          QDR.log.info("deployment " + $scope.mockTopologyDir + " started")
        }, extra)
      }
      $scope.Deploy = function () {
        // show the deploy dialog
        doDeployDialog(startDeploy)
      }

      $scope.showConfig = function (node) {
        doOperation("SHOW-CONFIG", function (response) {
          doShowConfigDialog(response)
        }, {nodeIndex: node.index})
      }
      var doOperation = function (operation, callback, extraProps) {
        var l = []
        links.forEach( function (link) {
          if (link.source.nodeType === 'inter-router' && link.target.nodeType === 'inter-router')
            l.push({source: link.source.index,
                    target: link.target.index,
                    cls: link.cls})
        })
        var props = {nodes: nodes, links: l, topology: $scope.mockTopologyDir, settings: settings}
        if (extraProps)
          Object.assign(props, props, extraProps)
        QDRService.sendMethod(operation, props, function (response) {
          if (callback)
            callback(response)
        })
      }

      $scope.canDeploy = function () {
        return nodes.length > 0
      }
      $scope.$watch('mockTopologyDir', function(newVal, oldVal) {
        if (oldVal != newVal) {
          switchTopology(newVal)
        }
      })
      var switchTopology = function (topology) {
        var props = {topology: topology}
        QDRService.sendMethod("SWITCH", props, function (response) {
          if ($scope.mockTopologies.indexOf(topology) == -1) {
            $timeout(function () {$scope.mockTopologies.push(topology)})
          }
          nodes = []
          links = []
          var savedPositions = localStorage[topology] ? angular.fromJson(localStorage[topology]) : undefined
          for (var i=0; i<response.nodes.length; ++i) {
            var node = response.nodes[i]
            var x = 100+i*90
            var y = 200+(i % 2 ? -100 : 100)
            if (savedPositions && node.name in savedPositions) {
              x = savedPositions[node.name].x
              y = savedPositions[node.name].y
            }
            var anode = aNode(node.key, node.name, node.nodeType, 'router', nodes.length, x, y, undefined, false)
            if (node['host'])
              anode['host'] = node['host']
            sections.forEach( function (section) {
              if (node[section+'s']) {
                anode[section+'s'] = node[section+'s']
              }
            })
            nodes.push(anode)
          }
          for (var i=0; i<response.links.length; ++i) {
            var link = response.links[i]
            getLink(link.source, link.target, link.dir, "", link.source + '.' + link.target);
          }

          for (var i=0; i<nodes.length; ++i) {
            var node = nodes[i]
            sections.forEach( function (section) {
              if (node[section+'s']) {
                for (var key in node[section+'s']) {
                  var type = section
                  if (section === 'listener' && key == settings.http_port)
                    type = 'console'
                  if (section === 'connector' && key == settings.artemis_port && node['connectors'][settings.artemis_port+'']['role'] === 'route-container')
                    type = 'artemis'
                  if (section === 'connector' && key == settings.qpid_port && node['connectors'][settings.qpid_port+'']['role'] === 'route-container')
                    type = 'qpid'
                  var sub = genNodeToAdd(node, type, key)
                  nodes.push(sub)
                  var source = node.id
                  var target = nodes.length-1
                  getLink(source, target, sub.cdir, "small", source + '.' + target);
                }
              }
            })
          }

          animate = true
          QDR.log.info("switched to " + topology)
          initForceGraph()
          $timeout( function () {
            Core.notification('info', "switched to " + props.topology);
          })
        })
      }

      $scope.Clear = function () {
        nodes = []
        links = []
        $scope.selected_nodes = {}
        $scope.selected_node = null
        resetMouseVars()
        force.nodes(nodes).links(links).start();
        $timeout( function () {
          restart();
        })
      }

      $scope.deleteNode = function (multi) {
        if (multi) {
          var del = true
          while (del) {
            del = false
            for (var i=0; i<nodes.length; i++) {
              if (isSelectedNode(nodes[i])) {
                delNode(nodes[i])   // this changes nodes. we can't continue looping
                del = true
                break;
              }
            }
          }
        } else {
          delNode($scope.selected_node)
        }
      }

      var delNode = function (node, skipinit) {
        // loop through all the nodes
        if (node.nodeType !== 'inter-router') {
          var n = findParentNode(node)
          if (n)
            delete n[node.entity+'s'][node.entityKey]
        } else {
          var sub_nodes = []
          for (var x=0; x<sections.length; x++) {
            var section = sections[x]
            if (node[section+'s']) {
              for (var sectionKey in node[section+'s']) {
                var n = findChildNode(section, sectionKey, node.name)
                if (n)
                  sub_nodes.push(n)
              }
            }
          }
          for (var x=0; x<sub_nodes.length; x++) {
            delNode(sub_nodes[x], true)
          }
          removeSelectedNode(node)
        }
        // find the index of the node
        var index = nodes.findIndex( function (n) {return n.name === node.name})
        nodes.splice(index, 1)

        nodes.forEach( function (n, i) {
          n.id = i
          n.index = i
        })

        var i = links.length
        while (i--) {
          if (links[i].source.name === node.name || links[i].target.name === node.name) {
            links.splice(i, 1)
          }
        }
        links.forEach( function (l, i) {
          l.uid = l.source.id + "." + l.target.id
        })

        $scope.selected_node = null

        if (!skipinit) {
          animate = true
          initGraph()
          initForce()
          restart();
        }
      }

      $scope.showMultiActions = function (e) {
        $(document).click();
        e.stopPropagation()
        var position = $('#multiple_action_button').position()
        position.top += $('#multiple_action_button').height() + 8
        position['display'] = "block"
        $('#multiple_action_menu').css(position)
      }
      $scope.showActions = function (e) {
        $(document).click();
        e.stopPropagation()
        var position = $('#action_button').position()
        position.top += $('#action_button').height() + 8
        position['display'] = "block"
        $('#action_menu').css(position)
      }

      var genNodeToAdd = function (contextNode, type, entityKey) {
        var id = contextNode.key
        var clients = nodes.filter ( function (node) {
           return node.nodeType !== 'inter-router' && node.routerId === contextNode.name
        })
        var clientLen = 0
        clients.forEach (function (client) {
          clientLen += client.normals.length
        })
        var dir = "out"
        if (type === 'listener')
          dir = "in"
        else if (type === 'console' || type === 'sslProfile')
          dir = "both"
        var name = contextNode.name + "." + (clientLen + 1)
        nodeType = "normal"
        var properties = type === 'console' ? {console_identifier: "Dispatch console"} : {}
        if (type === 'artemis') {
          properties = {product: 'apache-activemq-artemis'}
          nodeType = "route-container"
        }
        if (type === 'qpid') {
          properties = {product: 'qpid-cpp'}
          nodeType = "route-container"
        }
        var node = aNode(id, name, nodeType, type, nodes.length, contextNode.x, contextNode.y - radius - radiusNormal,
                             contextNode.id, false, properties)
        var entity = type === 'console' ? 'listener' : type
        entity = (type === 'artemis' || type === 'qpid') ? 'connector' : entity
        node.user = "anonymous"
        node.isEncrypted = false
        node.connectionId = node.id
        node.cdir = dir
        node.entity = entity
        node.entityKey = entityKey
        node.normals = [{name: node.name}]

        return node
      }

      var addToNode = function (contextNode, type, key) {
        var node = genNodeToAdd(contextNode, type, key)
        nodes.push(node)

        var uid = "connection/" + node.host + ":" + node.connectionId
        getLink(contextNode.id, nodes.length-1, node.cdir, "small", uid);
        force.nodes(nodes).links(links).start();
        restart();
      }

      $scope.addingNode = {
        step: 0,
        hasLink: false,
        trigger: ''
      };

      $scope.cancel = function() {
        $scope.addingNode.step = 0;
      }

      $scope.hasConsoleListener = function (node) {
        if (!node) {
          for (var i=0; i<nodes.length; i++) {
            var n = nodes[i]
            var found = n.listeners ? Object.keys(n.listeners).some(function (key) {return key == settings.http_port}) : false
            if (found)
              return true
          }
          return false
        }
        return node.listeners ? Object.keys(node.listeners).some(function (key) {return key == settings.http_port}) : false
      }
      // return a list of keys in a node's extra entities maps
      // called from the template to construct the context menu for nodes
      $scope.getSectionList = function (node, section) {
        if (node && node[section+'s']) {
          if (section === 'listener')
            return node && node.listeners ? Object.keys(node.listeners).filter( function (key) {
              return !node.listeners[key].http
            }) : []
          else
            return Object.keys(node[section+'s'])
        }
        return []
      }

      $scope.addConsoleListener = function (node) {
        if (!node.listeners)
          node.listeners = {}
        var host = node.host || settings.default_host
        node.listeners[settings.http_port] = {name: 'Console Listener', http: true, port: settings.http_port, host: host, saslMechanisms: 'ANONYMOUS'}
        addToNode(node, "console", settings.http_port)
      }
      $scope.delConsoleListener = function (node) {
        // find the actual console listener
        var n = findChildNode('listener', settings.http_port, node.name)
        if (n)
          delNode(n)
      }

      var yoffset = 1; // toggles between 1 and -1. used to position new nodes
      $scope.addAnotherNode = function (calc) {
        resetMouseVars();
        // add a new node
        var x = radiusNormal * 4;
        var y = x;;
        var offset = $('#topology').offset();
        if (calc) {
          var w = $('#topology').width()
          var h = $('#topology').height()
          var x = (w + offset.left) / 4
          var y = (h + offset.top) / 4 + yoffset * radiusNormal
          yoffset *= -1
          var overlap = true
          while (overlap) {
            overlap = false
            for (var i=0; i<nodes.length; i++) {
              if ((Math.abs(nodes[i].x - x) < radiusNormal * 2) &&
                  (Math.abs(nodes[i].y - y) < radiusNormal * 2)) {
                overlap = true
                x += radiusNormal
                if (x + radiusNormal/2 >= offset.left + w) {
                  x = offset.left + radiusNormal/2
                  y += radiusNormal
                  if (y + radiusNormal/2 >= offset.top + h) {
                    x = offset.left + radiusNormal
                    y = offset.top + radiusNormal
                  }
                }
                break;
              }
            }
          }
        } else {
          x = mouseX - offset.left + $(document).scrollLeft();
          y = mouseY - offset.top + $(document).scrollTop();;
        }
        var name = genNewName()
        var nextId = nodes.length //maxNodeIndex() + 1
        var id = "amqp:/_topo/0/" + name + "/$management";
        var node = aNode(id, name, "inter-router", 'router', nextId, x, y, undefined, false)
        node.host = settings.default_host
        nodes.push(node);
        $scope.selected_node = node
        force.nodes(nodes).links(links).start();
        restart(false);
      }

      var maxNodeIndex = function () {
        var maxIndex = -1
        nodes.forEach( function (node) {
          if (node.nodeType === "inter-router") {
            if (node.id > maxIndex)
              maxIndex = node.id
          }
        })
        return maxIndex;
      }

      // generate unique name for router and containerName
      var genNewName = function() {
        var re = /./g;
        for (var i=0, newName = '', found = true; found; ++i) {
          newName = i.toString(26).replace(re, function (m) {
            var ccode = m.charCodeAt(0)
            if (ccode >= 48 && ccode <= 57)
              return String.fromCharCode(ccode+17)
            return String.fromCharCode(ccode-22)
          })
          found = nodes.some( function (n) {return n.name === newName})
        }
        return newName
      }

      $scope.doSettings = function () {
        doSettingsDialog(settings);
      };
      $scope.showNewDlg = function () {
        doNewDialog();
      }
      $scope.reverseLink = function() {
        if (!mousedown_link)
          return;
        var d = mousedown_link;
        for (var i=0; i<links.length; i++) {
          if (links[i].source.index === d.source.index && links[i].target.index === d.target.index ) {
            var tmp = links[i].source
            links[i].source = links[i].target
            links[i].target = tmp
          }
        }
        restart(false);
        tick();
      }
      $scope.removeLink = function() {
        if (!mousedown_link)
          return;
        var d = mousedown_link;
        links.every(function(l, i) {
          if (l.source.id == d.source.id && l.target.id == d.target.id) {
            links.splice(i, 1);
            force.links(links).start();
            return false; // exit the 'every' loop
          }
          return true;
        });
        restart(false);
        tick();
      }

      var findChildNode = function (entity, entityKey, name) {
        // find the node that has entity and entityKey
        for (var i=0; i<nodes.length; i++) {
          if (nodes[i].entity === entity && nodes[i].entityKey == entityKey && nodes[i].routerId === name)
            return nodes[i]
        }
      }
      var findParentNode = function (node) {
        // find the node that contains the entity[entityKey] of this node
        for (var i=0; i<nodes.length; i++) {
          if (nodes[i][node.entity+'s'] && node.entityKey in nodes[i][node.entity+'s'])
            return nodes[i]
        }
      }
      // menu item of router to set host of all listeners
      $scope.setRouterHost = function (multi) {
        if (!$scope.selected_node)
          $scope.selected_node = firstSelectedNode()
        doSetRouterHostDialog($scope.selected_node, multi)
      }
      // menu item of router to edit one of its sub-entities
      $scope.editSection = function (multi, type, section) {
        if (!$scope.selected_node)
          $scope.selected_node = firstSelectedNode()
        doEditDialog($scope.selected_node, type, section, multi)
      }
      // menu item of sub-entity to edit itself
      $scope.editThisSection = function (node) {
        var n = findParentNode(node)
        if (n)
          doEditDialog(n, node.entity, node.entityKey, false)
      }

      var mouseX, mouseY;
      var relativeMouse = function () {
        var offset = $('#main-container').offset();
        return {left: (mouseX + $(document).scrollLeft()) - 1,
                top: (mouseY  + $(document).scrollTop()) - 1,
                offset: offset
                }
      }
      // event handlers for popup context menu
      $(document).mousemove(function(e) {
        mouseX = e.clientX;
        mouseY = e.clientY;
      });
      $(document).mousemove();
      $(document).click(function(e) {
        $("#svg_context_menu").fadeOut(200);
        $("#multiple_action_menu").fadeOut(200);
        $("#action_menu").fadeOut(200);
        $("#link_context_menu").fadeOut(200);
        $("#client_context_menu").fadeOut(200);
      });
      function clearPopups() {
        d3.select("#crosssection").style("display", "none");
        $('.hastip').empty();
        d3.select("#multiple_details").style("display", "none")
        d3.select("#link_details").style("display", "none")
        d3.select('#multiple_action_menu').style('display', 'none');
        d3.select('#action_menu').style('display', 'none');
        d3.select('#svg_context_menu').style('display', 'none');
        d3.select('#link_context_menu').style('display', 'none');
        d3.select('#client_context_menu').style('display', 'none');
      }

      var radii = {
        'inter-router': 25,
        'normal': 15,
        'on-demand': 15,
        'route-container': 15,
        'host': 20
      };
      var radius = 25;
      var radiusNormal = 15;
      var svg, lsvg;
      var force;
      var animate = false; // should the force graph organize itself when it is displayed
      var path, circle;
      var savedKeys = {};
      var dblckickPos = [0, 0];
      var width = 0;
      var height = 0;

      var getSizes = function() {
        var legendWidth = 143;
        var gap = 5;
        var width = $('#topology').width() - gap - legendWidth;
        var top = $('#topology').offset().top
        var height = Math.max(window.innerHeight, top) - top - gap;
        if (width < 10) {
          QDR.log.info("page width and height are abnormal w:" + width + " height:" + height)
          return [0, 0];
        }
        return [width, height]
      }
      var resize = function() {
        if (!svg)
          return;
        var sizes = getSizes();
        width = sizes[0]
        height = sizes[1]
        if (width > 0) {
          // set attrs and 'resume' force
          svg.attr('width', width);
          svg.attr('height', height);
          force.size(sizes).resume();
        }
      }
      window.addEventListener('resize', resize);
      var sizes = getSizes()
      width = sizes[0]
      height = sizes[1]
      if (width <= 0 || height <= 0)
        return

      // set up initial nodes and links
      //  - nodes are known by 'id', not by index in array.
      //  - selected edges are indicated on the node (as a bold red circle).
      //  - links are always source < target; edge directions are set by 'left' and 'right'.
      var nodes = [];
      var links = [];

      var aNode = function(id, name, nodeType, nodeInfo, nodeIndex, x, y, resultIndex, fixed, properties) {
        for (var i=0; i<nodes.length; ++i) {
          if (nodes[i].name === name)
            return nodes[i]
        }
        properties = properties || {};
        var routerId = QDRService.nameFromId(id)
        return {
          key: id,
          name: name,
          nodeType: nodeType,
          properties: properties,
          routerId: routerId,
          x: x,
          y: y,
          id: nodeIndex,
          host: '0.0.0.0',
          resultIndex: resultIndex,
          cls: nodeInfo
        };
      };


      var initForm = function(attributes, results, entityType, formFields) {

        while (formFields.length > 0) {
          // remove all existing attributes
          formFields.pop();
        }

        for (var i = 0; i < attributes.length; ++i) {
          var name = attributes[i];
          var val = results[i];
          var desc = "";
          if (entityType.attributes[name])
            if (entityType.attributes[name].description)
              desc = entityType.attributes[name].description;

          formFields.push({
            'attributeName': name,
            'attributeValue': val,
            'description': desc
          });
        }
      }

      var savePositions = function () {
        var positions = {}
        nodes.forEach( function (d) {
          positions[d.name] = {
            x: Math.round(d.x),
            y: Math.round(d.y)
          };
        })
        localStorage[$scope.mockTopologyDir] = angular.toJson(positions)
      }

      // vary the following force graph attributes based on nodeCount
      // <= 6 routers returns min, >= 80 routers returns max, interpolate linearly
      var forceScale = function(nodeCount, min, max) {
        var count = nodeCount
        if (nodeCount < 6) count = 6
        if (nodeCount > 200) count = 200
        var x = d3.scale.linear()
          .domain([6,200])
          .range([min, max]);
//QDR.log.debug("forceScale(" + nodeCount + ", " + min + ", " + max + "  returns " + x(count) + " " + x(nodeCount))
        return x(count)
      }
      var linkDistance = function (d, nodeCount) {
        if (d.target.nodeType === 'inter-router')
          return forceScale(nodeCount, 150, 20)
        return forceScale(nodeCount, 75, 10)
      }
      var charge = function (d, nodeCount) {
        if (d.nodeType === 'inter-router')
          return forceScale(nodeCount, -1800, -200)
        return -900
      }
      var gravity = function (d, nodeCount) {
        return forceScale(nodeCount, 0.0001, 0.1)
      }

      var initGraph = function () {
        d3.select("#SVG_ID").remove();
        svg = d3.select('#topology')
          .append('svg')
          .attr("id", "SVG_ID")
          .attr('width', width)
          .attr('height', height)
          .on("contextmenu", function(d) {
            if (d3.event.defaultPrevented)
              return;
            d3.event.preventDefault();

            //if ($scope.addingNode.step != 0)
            //  return;
            if (d3.select('#svg_context_menu').style('display') !== 'block')
              $(document).click();
            var rm = relativeMouse()
            d3.select('#svg_context_menu')
              .style('left', (rm.left - 16) + "px")
              .style('top', (rm.top - rm.offset.top) + "px")
              .style('display', 'block');
          })

        svg.append("svg:defs").selectAll('marker')
          .data(["end-arrow", "end-arrow-selected", "end-arrow-small", "end-arrow-highlighted"]) // Different link/path types can be defined here
          .enter().append("svg:marker") // This section adds in the arrows
          .attr("id", String)
          .attr("viewBox", "0 -5 10 10")
          .attr("markerWidth", 4)
          .attr("markerHeight", 4)
          .attr("orient", "auto")
          .classed("small", function (d) {return d.indexOf('small') > -1})
          .append("svg:path")
            .attr('d', 'M 0 -5 L 10 0 L 0 5 z')

        svg.append("svg:defs").selectAll('marker')
          .data(["start-arrow", "start-arrow-selected", "start-arrow-small", "start-arrow-highlighted"]) // Different link/path types can be defined here
          .enter().append("svg:marker") // This section adds in the arrows
          .attr("id", String)
          .attr("viewBox", "0 -5 10 10")
          .attr("refX", 5)
          .attr("markerWidth", 4)
          .attr("markerHeight", 4)
          .attr("orient", "auto")
          .append("svg:path")
            .attr('d', 'M 10 -5 L 0 0 L 10 5 z');

        var hostColors = d3.scale.category10();
        var colors = []
        for (var i=0; i<10; i++) {
          colors.push(hostColors(i))
        }
        svg.append("svg:defs").selectAll('pattern')
          .data(colors)
          .enter().append("pattern")
          .attr({ id:"host"+i, width:"8", height:"8", patternUnits:"userSpaceOnUse", patternTransform:"rotate(-45)"})
          .attr('id', function (d, i) {return 'host_'+i})
          .attr({width:"4", height:"8", patternUnits:"userSpaceOnUse", patternTransform:"rotate(-45)"})
          .append("rect")
          .attr('fill', function (d) {return d})
          .attr({ width:"2", height:"8", transform:"translate(0,0)"});

        // handles to link and node element groups
        path = svg.append('svg:g').selectAll('path')
        circle = svg.append('svg:g').selectAll('g')

      }

      var initLegend = function () {
        // the legend
        d3.select("#svg_legend svg").remove();
        lsvg = d3.select("#svg_legend")
          .append('svg')
          .attr('id', 'svglegend')
        lsvg = lsvg.append('svg:g')
          .attr('transform', 'translate(' + (radii['inter-router'] + 2) + ',' + (radii['inter-router'] + 2) + ')')
          .selectAll('g');
      }

      var initForce = function () {
        // convert link source/target into node index numbers
        links.forEach( function (link, i) {
          if (link.source.id) {
            link.source = link.source.id
            link.target = link.target.id
          }
        })
        var routerCount = nodes.filter(function (n) {
          return n.nodeType === 'inter-router'
        }).length

        force = d3.layout.force()
          .nodes(nodes)
          .links(links)
          .size([width, height])
          .linkDistance(function(d) { return linkDistance(d, routerCount) })
          .charge(function(d) { return charge(d, routerCount) })
          .friction(.10)
          .gravity(function(d) { return gravity(d, routerCount) })
          .on('tick', tick)
          .on('end', function () {savePositions()})
          .start()
      }
      // initialize the nodes and links array from the QDRService.topology._nodeInfo object
      var initForceGraph = function() {

        mouseover_node = null;
        $scope.selected_nodes = {};
        $scope.selected_node = null
        selected_link = null;

        initGraph()
        initLegend()

        // mouse event vars
        mousedown_link = null;
        mousedown_node = null;
        mouseup_node = null;

        savePositions()
        // init D3 force layout
        initForce()

        // app starts here
        restart(false);
        force.start();
        tick();
      }

      function getContainerIndex(_id, nodeInfo) {
        var nodeIndex = 0;
        for (var id in nodeInfo) {
          if (QDRService.nameFromId(id) === _id)
            return nodeIndex;
          ++nodeIndex;
        }
        return -1;
      }

      function getLink(_source, _target, dir, cls, uid) {
        for (var i = 0; i < links.length; i++) {
          var s = links[i].source,
              t = links[i].target;
          if (typeof links[i].source == "object") {
            s = s.id;
            t = t.id;
          }
          if (s == _source && t == _target) {
            return i;
          }
          // same link, just reversed
          if (s == _target && t == _source) {
            return -i;
          }
        }

        var link = {
          source: _source,
          target: _target,
          left: dir != "out",
          right: (dir == "out" || dir == "both"),
          cls: cls,
          uid: uid,
        };
        return links.push(link) - 1;
      }


      function resetMouseVars() {
        mousedown_node = null;
        mouseover_node = null;
        mouseup_node = null;
        mousedown_link = null;
      }

      // update force layout (called automatically each iteration)
      function tick() {
        circle.attr('transform', function(d) {
          var cradius;
          if (d.nodeType == "inter-router") {
            cradius = d.left ? radius + 8 : radius;
          } else {
            cradius = d.left ? radiusNormal + 18 : radiusNormal;
          }
          d.x = Math.max(d.x, radiusNormal * 2);
          d.y = Math.max(d.y, radiusNormal * 2);
          d.x = Math.max(0, Math.min(width - cradius, d.x))
          d.y = Math.max(0, Math.min(height - cradius, d.y))
          return 'translate(' + d.x + ',' + d.y + ')';
        });

        // draw directed edges with proper padding from node centers
        path.attr('d', function(d) {
          var sourcePadding, targetPadding, r;

          if (d.target.nodeType == "inter-router") {
            r = radius;
            //                       right arrow  left line start
            sourcePadding = d.left ? radius + 8 : radius;
            //                      left arrow      right line start
            targetPadding = d.right ? radius + 16 : radius;
          } else {
            r = radiusNormal - 18;
            sourcePadding = d.left ? radiusNormal + 18 : radiusNormal + 10;
            targetPadding = d.right ? radiusNormal + 6 : radiusNormal;
          }
          var dtx = Math.max(targetPadding, Math.min(width - r, d.target.x)),
            dty = Math.max(targetPadding, Math.min(height - r, d.target.y)),
            dsx = Math.max(sourcePadding, Math.min(width - r, d.source.x)),
            dsy = Math.max(sourcePadding, Math.min(height - r, d.source.y));

          var deltaX = dtx - dsx,
            deltaY = dty - dsy,
            dist = Math.sqrt(deltaX * deltaX + deltaY * deltaY),
            normX = deltaX / dist,
            normY = deltaY / dist;
          var sourceX = dsx + (sourcePadding * normX),
            sourceY = dsy + (sourcePadding * normY),
            targetX = dtx - (targetPadding * normX),
            targetY = dty - (targetPadding * normY);
          sourceX = Math.max(0, Math.min(width, sourceX))
          sourceY = Math.max(0, Math.min(width, sourceY))
          targetX = Math.max(0, Math.min(width, targetX))
          targetY = Math.max(0, Math.min(width, targetY))

          return 'M' + sourceX + ',' + sourceY + 'L' + targetX + ',' + targetY;
        });

        if (!animate) {
          animate = true;
          force.stop();
        }
      }

      function nodeFor(name) {
        for (var i = 0; i < nodes.length; ++i) {
          if (nodes[i].name == name)
            return nodes[i];
        }
        return null;
      }

      function genLinkName(d1, d2) {
        return d1.id + "." + d2.id
      }
      function linkFor(source, target) {
        for (var i = 0; i < links.length; ++i) {
          if ((links[i].source == source) && (links[i].target == target))
            return links[i];
          if ((links[i].source == target) && (links[i].target == source))
            return links[i];
        }
        return null;
      }

      function hideLinkDetails() {
        d3.select("#link_details").transition()
          .duration(500)
          .style("opacity", 0)
          .each("end", function(d) {
            d3.select("#link_details").style("display", "none")
          })
      }

      var appendTitle = function(g) {
        g.append("svg:title").text(function(d) {
          if (QDRService.isConsole(d)) {
            return 'Dispatch console'
          }
          if (d.properties.product == 'qpid-cpp') {
            return 'Broker - Qpid'
          }
          if (QDRService.isArtemis(d)) {
            return 'Broker - Artemis'
          }
          if (d.cls === 'log') {
            return 'Log' + (d.entityKey ? (': ' + d.entityKey) : '')
          }
          if (d.cls === 'address') {
            return 'Address' + (d.entityKey ? (': ' + d.entityKey) : '')
          }
          if (d.cdir === 'in')
            return 'Listener on port ' + d.entityKey
          if (d.cdir === 'out')
            return 'Connector to ' + d.host + ':' + d.entityKey
          if (d.cdir === 'both')
            return 'sslProfile'
          if (d.cls === 'host')
            return 'Host ' + d.name
          return d.nodeType == 'normal' ? 'client' : (d.nodeType == 'route-container' ? 'broker' : 'Router ' + d.name)
        })
      }

      var appendCircle = function(g) {
        // add new circles and set their attr/class/behavior
        return g.append('svg:circle')
          .attr('class', 'node')
          .attr('r', function(d) {
            return radii[d.nodeType]
          })
          .classed('normal', function(d) {
            return d.nodeType == 'normal' || QDRService.isConsole(d)
          })
          .classed('in', function(d) {
            return d.cdir == 'in'
          })
          .classed('out', function(d) {
            return d.cdir == 'out'
          })
          .classed('connector', function(d) {
            return d.cls == 'connector'
          })
          .classed('address', function(d) {
            return d.cls == 'address'
          })
          .classed('listener', function(d) {
            return d.cls == 'listener'
          })
          .classed('selected', function (d) {
            return $scope.selected_node === d
          })
          .classed('inout', function(d) {
            return d.cdir == 'both'
          })
          .classed('inter-router', function(d) {
            return d.nodeType == 'inter-router'
          })
          .classed('on-demand', function(d) {
            return d.nodeType == 'route-container'
          })
          .classed('log', function(d) {
            return d.cls === 'log'
          })
          .classed('console', function(d) {
            return QDRService.isConsole(d)
          })
          .classed('artemis', function(d) {
            return QDRService.isArtemis(d)
          })
          .classed('qpid-cpp', function(d) {
            return QDRService.isQpid(d)
          })
          .classed('route-container', function (d) {
            return (!QDRService.isArtemis(d) && !QDRService.isQpid(d) && d.nodeType === 'route-container')
          })
          .classed('client', function(d) {
            return d.nodeType === 'normal' && !d.properties.console_identifier
          })
      }

      var appendContent = function(g) {
        // show node IDs
        g.append('svg:text')
          .attr('x', 0)
          .attr('y', function(d) {
            var y = 7;
            if (QDRService.isArtemis(d))
              y = 8;
            else if (QDRService.isQpid(d))
              y = 9;
            else if (d.nodeType === 'inter-router')
              y = 4;
            return y;
          })
          .attr('class', 'id')
          .classed('log', function(d) {
            return d.cls === 'log'
          })
          .classed('address', function(d) {
            return d.cls === 'address'
          })
          .classed('console', function(d) {
            return QDRService.isConsole(d)
          })
          .classed('normal', function(d) {
            return d.nodeType === 'normal'
          })
          .classed('on-demand', function(d) {
            return d.nodeType === 'on-demand'
          })
          .classed('artemis', function(d) {
            return QDRService.isArtemis(d)
          })
          .classed('qpid-cpp', function(d) {
            return QDRService.isQpid(d)
          })
          .text(function(d) {
            if (QDRService.isConsole(d)) {
              return '\uf108'; // icon-desktop for this console
            } else if (QDRService.isArtemis(d)) {
              return '\ue900'
            } else if (QDRService.isQpid(d)) {
              return '\ue901';
            } else if (d.cls === 'log') {
              return '\uf036';
            } else if (d.cls === 'address') {
              return '\uf2bc';
            } else if (d.nodeType === 'route-container') {
              return d.properties.product ? d.properties.product[0].toUpperCase() : 'S'
            } else if (d.nodeType === 'normal' && d.cdir === "in") // listener
                return '\uf2a0'; // phone top
              else if (d.nodeType === 'normal' && d.cdir === "out") // connector
                return '\uf2a0'; // phone top (will be rotated)
              else if (d.nodeType === 'normal' && d.cdir === "both") // not used
                return '\uf023'; // icon-laptop for clients
              else if (d.nodeType === 'host')
                return ''

            return d.name.length > 7 ? d.name.substr(0, 6) + '...' : d.name;
          })
          // rotatie the listener icon 180 degrees to use as the connector icon
         .attr("transform", function (d) {
            var nAngle = 0
            if (d.nodeType === 'normal' && d.cdir === "out" && d.cls === 'connector')
              nAngle = 180
            return "rotate("+nAngle+")"
          });
      }

      // takes the nodes and links array of objects and adds svg elements for everything that hasn't already
      // been added
      function restart(start) {
        circle.call(force.drag);

        // path (link) group
        path = path.data(links, function(d) {return d.uid});

        // update existing links
        path.classed('selected', function(d) {
            return d === selected_link;
          })
          .classed('highlighted', function(d) {
            return d.highlighted;
          })
          .attr('marker-start', function(d) {
            var sel = d === selected_link ? '-selected' : (d.cls === 'small' ? '-small' : '');
            if (d.highlighted)
              sel = "-highlighted"
            return d.left ? 'url(#start-arrow' + sel + ')' : '';
          })
          .attr('marker-end', function(d) {
            var sel = d === selected_link ? '-selected' : (d.cls === 'small' ? '-small' : '');
            if (d.highlighted)
              sel = "-highlighted"
            return d.right ? 'url(#end-arrow' + sel + ')' : '';
          })


        // add new links. if links[] is longer than the existing paths, add a new path for each new element
        path.enter().append('svg:path')
          .attr('class', 'link')
          .attr('marker-start', function(d) {
            var sel = d === selected_link ? '-selected' : (d.cls === 'small' ? '-small' : '');
            return d.left ? 'url(#start-arrow' + sel + ')' : '';
          })
          .attr('marker-end', function(d) {
            var sel = d === selected_link ? '-selected' : (d.cls === 'small' ? '-small' : '');
            return d.right ? 'url(#end-arrow' + sel + ')' : '';
          })
          .classed('small', function(d) {
            return d.cls == 'small';
          })
          .on('mouseover', function(d) { // mouse over a path
            if ($scope.addingNode.step > 0) {
              if (d.cls == 'temp') {
                d3.select(this).classed('over', true);
              }
              return;
            }

            mousedown_link = d;
            selected_link = mousedown_link;
            restart();
          })
          .on('mouseout', function(d) { // mouse out of a path
            if ($scope.addingNode.step > 0) {
              if (d.cls == 'temp') {
                d3.select(this).classed('over', false);
              }
              return;
            }
            selected_link = null;
            restart();
          })
          .on("contextmenu", function(d) {  // right click a path
            $(document).click();
            d3.event.preventDefault();

            mousedown_link = d;
            var rm = relativeMouse()
            d3.select('#link_context_menu')
              .style('left', rm.left + "px")
              .style('top', (rm.top - rm.offset.top) + "px")
              .style('display', 'block');
          })
          // left click a path
          .on("click", function (d) {
            var clickPos = d3.mouse(this);
            d3.event.stopPropagation();
            clearPopups();
          })
        // remove old links
        path.exit().remove();

        // circle (node) group
        // nodes are known by id
        circle = circle.data(nodes, function(d) { return d.name + d.host });

        // update existing nodes visual states
        circle.selectAll('circle')
          .classed('highlighted', function(d) {
            return d.highlighted;
          })
          .classed('selected', function(d) {
            return (d === $scope.selected_node)
          })
        // add 'multiple' class to existing <g> elements as needed
          .each(function (d) {
            if (d.normals && d.normals.length > 1) {
              // add the "multiple" class to the parent <g>
              var d3g = d3.select(this.parentElement)
              d3g.attr('class', 'multiple')
              d3g.select('title').remove()
              appendTitle(d3g)
            }
          })

        // add new circle nodes. if nodes[] is longer than the existing paths, add a new path for each new element
        var g = circle.enter().append('svg:g')
          .classed('multiple', function(d) {
            return (d.normals && d.normals.length > 1)
          })

        appendCircle(g)
          .on('mouseover', function(d) {  // mouseover a circle
            if ($scope.addingNode.step > 0) {
              d3.select(this).attr('transform', 'scale(1.1)');
              return;
            }

            if (d === mousedown_node)
              return;
            // enlarge target node
            d3.select(this).attr('transform', 'scale(1.1)');
            mousedown_node = null;
          })
          .on('mouseout', function(d) { // mouse out for a circle
            // unenlarge target node
            d3.select(this).attr('transform', '');
            mouseover_node = null;
            restart();
          })
          .on('mousedown', function(d) { // mouse down for circle
            if (d3.event.button !== 0) { // ignore all but left button
              return;
            }
            mousedown_node = d;
            // mouse position relative to svg
            initial_mouse_down_position = d3.mouse(this.parentElement.parentElement.parentElement).slice();
          })
          .on('mouseup', function(d) {  // mouse up for circle
            if (!mousedown_node)
              return;

            selected_link = null;
            // unenlarge target node
            d3.select(this).attr('transform', '');

            // check for drag
            mouseup_node = d;
            var mySvg = this.parentElement.parentElement.parentElement;
            // if we dragged the node, don't do anything
            var cur_mouse = d3.mouse(mySvg);
            if (Math.abs(cur_mouse[0] - initial_mouse_down_position[0]) > 4 ||
              Math.abs(cur_mouse[1] - initial_mouse_down_position[1]) > 4) {
              return
            }
            if (d3.event.ctrlKey && d.cls === 'router') {
              return
            }
            // we want a link between the selected_node and this node
            if ($scope.selected_node && d !== $scope.selected_node) {
              if (d.nodeType !== 'inter-router')
                return;

              // add a link from the clicked node to the selected node
              var source = nodes.findIndex( function (n) {
                return (n.key === d.key && n.nodeType === 'inter-router')
              })
              var target = nodes.findIndex( function (n) {
                return (n.key === $scope.selected_node.key && n.nodeType === 'inter-router')
              })
              var curLinkCount = links.length
              var newIndex = getLink(source, target, "in", "", genLinkName(d, $scope.selected_node));
              // there was already a link from selected to clicked node
              if (newIndex != curLinkCount) {
                $scope.selected_node = d
                restart();
                return;
              }
                // add new elements to the svg
              force.links(links).start();
              restart();
              return;

            }

            // if this node was selected, unselect it
            if (mousedown_node === $scope.selected_node) {
              $scope.selected_node = null;
            } else {
              if (d.nodeType !== 'normal' && d.nodeType !== 'on-demand')
                $scope.selected_node = mousedown_node;
            }
            mousedown_node = null;
            if (!$scope.$$phase) $scope.$apply()
            restart(false);

          })
          .on("contextmenu", function(d) {  // circle
            clearPopups();

            d3.event.preventDefault();
            $scope.selected_node = d;
            if (!$scope.$$phase) $scope.$apply() // we just changed a scope variable during an async event
            var rm = relativeMouse()
            var menu = d.nodeType === 'inter-router' ? 'action_menu' : 'client_context_menu'
            if (isSelectedNode(d))
              menu = 'multiple_action_menu'
            d3.select('#'+menu)
              .style('left', rm.left + "px")
              .style('top', (rm.top - rm.offset.top) + "px")
              .style('display', 'block');
          })
          .on("click", function(d) {  // circle
            if (!mouseup_node)
              return;
            if (d3.event.ctrlKey && d.cls === 'router') {
              $timeout( function () {
                toggleSelectedNode(d)
              })
            }
            // clicked on a circle
            clearPopups();
            clickPos = d3.mouse(this);
            d3.event.stopPropagation();
          })
        //.attr("transform", function (d) {return "scale(" + (d.nodeType === 'normal' ? .5 : 1) + ")"})
        //.transition().duration(function (d) {return d.nodeType === 'normal' ? 3000 : 0}).ease("elastic").attr("transform", "scale(1)")
        resetCircleHosts()
        appendContent(g)
        appendTitle(g);

        // remove old nodes
        circle.exit().remove();

        // add subcircles
        svg.selectAll('.more').remove();
        svg.selectAll('.multiple')
          .append('svg:path')
            .attr('d', "M1.5,-1 V4 M-1,1.5 H4")
            .attr('class', 'more')
            .attr('transform', "translate(18, -3) scale(2)")

        // dynamically create the legend based on which node types are present
        // the legend
        updateLegend()

        if (!mousedown_node || !$scope.selected_node)
          return;

        if (!start)
          return;
        // set the graph in motion
        force.start();

      }

      function updateLegend() {
        initLegend()
        var legendNodes = [];
        //legendNodes.push(aNode("Router", "", "inter-router", 'router', 0, 0, 0, 0, false, {}))

        if (!svg.selectAll('circle.console').empty()) {
          legendNodes.push(aNode("Console", "", "normal", 'console', 1, 0, 0, 0, false, {
            console_identifier: 'Dispatch console'
          }))
        }
        if (!svg.selectAll('circle.listener').empty()) {
          var node = aNode("Listener", "", "normal", 'listener', 2, 0, 0, 0, false, {})
          node.cdir = "in"
          legendNodes.push(node)
        }
        if (!svg.selectAll('circle.connector').empty()) {
          var node = aNode("Connector", "", "normal", 'connector', 3, 0, 0, 0, false, {})
          node.cdir = "out"
          legendNodes.push(node)
        }
        if (!svg.selectAll('circle.address').empty()) {
          var node = aNode("Address", "", "normal", 'address', 4, 0, 0, 0, false, {})
          node.cdir = "out"
          legendNodes.push(node)
        }
        if (!svg.selectAll('circle.sslProfile').empty()) {
          var node = aNode("sslProfile", "", "normal", 'sslProfile', 5, 0, 0, 0, false, {})
          node.cdir = "both"
          legendNodes.push(node)
        }
        if (!svg.selectAll('circle.log').empty()) {
          legendNodes.push(aNode("Logs", "", "normal", 'log', 6, 0, 0, 0, false, {}))
        }
        if (!svg.selectAll('circle.qpid-cpp').empty()) {
          legendNodes.push(genNodeToAdd({key:'Qpid broker', name:'legend', x:0, y:0, id:'legend'}, 'qpid', ''))
        }
        if (!svg.selectAll('circle.artemis').empty()) {
          legendNodes.push(genNodeToAdd({key:'Artemis broker', name:'legend', x:0, y:0, id:'legend'}, 'artemis', ''))
        }
        if (!svg.selectAll('circle.route-container').empty()) {
          legendNodes.push(aNode("Service", "", "route-container", 'service', 9, 0, 0, 0, false,
          {product: ' External Service'}))
        }
        // add a circle for each unique host in nodes
        var hosts = getHosts()
        for (var i=0; i<hosts.length; i++) {
          var host = hosts[i]
          legendNodes.push({key: host, name: host, nodeType: 'host', id: getHostIndex(host), properties: {}, host: host, cls: 'host'})
        }

        lsvg = lsvg.data(legendNodes, function(d) {
          return d.key;
        });
        var lg = lsvg.enter().append('svg:g')
          .attr('transform', function(d, i) {
            // 45px between lines and add 10px space after 1st line
            return "translate(0, " + (45 * i) + ")"
          })

        appendCircle(lg)
        resetCircleHosts()
        appendContent(lg)
        appendTitle(lg)
        lg.append('svg:text')
          .attr('x', 35)
          .attr('y', 6)
          .attr('class', "label")
          .text(function(d) {
            return d.key
          })
        lsvg.exit().remove();
        var svgEl = document.getElementById('svglegend')
        if (svgEl) {
          var bb;
          // firefox can throw an exception on getBBox on an svg element
          try {
            bb = svgEl.getBBox();
          } catch (e) {
            bb = {
              y: 0,
              height: 200,
              x: 0,
              width: 200
            }
          }
          svgEl.style.height = (bb.y + bb.height) + 'px';
          svgEl.style.width = (bb.x + bb.width) + 'px';
        }

      }

      // Ensure a host name will always return the same number (for this session)
      // This is to make sure the colors associated with host names don't change
      // In other words, if you have a node with host==localhost and then delete it, all the
      // other host colors won't change and if you re-add localhost it would get the original color
      var knownHosts = {}
      function initHosts() {
        var hosts = getHosts()
        var hostColors = d3.scale.category10();
        for (var i=0; i<hosts.length; i++) {
          knownHosts[hosts[i]] = i
        }

        var style = document.createElement("style");
        style.appendChild(document.createTextNode(""));
        document.head.appendChild(style);
        var sheet = style.sheet ? style.sheet : style.styleSheet;
        for (var i=0; i<10; i++) {
          if (sheet.insertRule) {
            sheet.insertRule("button {color:red}", 0);
            sheet.insertRule("circle.node.host"+i+" {fill: " + hostColors(i) + ";}", 0);
            sheet.insertRule("circle.node.host"+i+".multi-selected {fill: url(#host_"+i+");}", 1);
            sheet.insertRule("div.node.host"+i+" {background-color: " + hostColors(i) + ";}", 2);
          } else if (sheet.addRule) { // for IE < 9
            sheet.addRule("circle.node.host"+i, "fill: " + hostColors(i) + ";", 0);
            sheet.addRule("circle.node.host"+i+".multi-selected", "fill: url(#host_"+i+")", 1);
            sheet.addRule("div.node.host"+i, "background-color: " + hostColors(i) + ";", 2);
          }
        }

      }
      function getHosts() {
        var hosts = {}
        for (var i=0; i<nodes.length; i++) {
          var node = nodes[i]
          if (node.host && node.cls === 'router')
            hosts[node.host] = 1
        }
        return Object.keys(hosts)
      }
      initHosts()
      function getHostIndex(h) {
        if (h in knownHosts)
          return knownHosts[h]
        knownHosts[h] = Object.keys(knownHosts).length
        return knownHosts[h]
      }
      function mousedown() {
        // prevent I-bar on drag
        //d3.event.preventDefault();

        // because :active only works in WebKit?
        svg.classed('active', true);
      }

      // we are about to leave the page, save the node positions
      $rootScope.$on('$locationChangeStart', function(event, newUrl, oldUrl) {
        savePositions()
      });
      // When the DOM element is removed from the page,
      // AngularJS will trigger the $destroy event on
      // the scope
      $scope.$on("$destroy", function(event) {
        savePositions();
        d3.select("#SVG_ID").remove();
        window.removeEventListener('resize', resize);
      });

      QDRService.sendMethod("ANSIBLE-INSTALLED", {}, function (response) {
        $scope.ansible = (response !== "")
      })
      QDRService.sendMethod("GET-TOPOLOGY-LIST", {}, function (response) {
        $scope.mockTopologies = response.sort()
        QDRService.sendMethod("GET-TOPOLOGY", {}, function (response) {
          // this will trigger the watch on this variable which will get the topology
          $timeout(function () {
            $scope.mockTopologyDir = response
          })
        })
      })

      function doShowConfigDialog(config) {
        var d = $uibModal.open({
          dialogClass: "modal dlg-large",
          backdrop: true,
          keyboard: true,
          backdropClick: true,
          controller: 'QDR.ShowConfigDialogController',
          templateUrl: 'show-config-template.html',
          resolve: {
            config: function() {
              return config;
            }
          }
        });
      }
      // set the host# class for all inter-router circles
      function resetCircleHosts() {
        var sel = d3.selectAll('circle.node')
        for (var i=0; i<10; i++) {
          sel.classed('host'+i, function (d) {
            return i === getHostIndex(d.host) && (d.cls === 'router' || d.cls === 'host')
          })
        }
      }

      function firstSelectedNode() {
        return $scope.selected_nodes[Object.keys($scope.selected_nodes)[0]]
      }
      function isSelectedNode(d) {
        return d.name in $scope.selected_nodes
      }
      $scope.anySelectedNodes = function() {
        return (Object.keys($scope.selected_nodes).length > 0)
      }
      function updateSelectedNodes() {
        d3.selectAll('circle.node.inter-router')
          .classed('multi-selected', function (d) {
            return d.name in $scope.selected_nodes
          })
          .attr('fill', function (d) {
            var hostIndex = getHostIndex(d.host)
            return "url(#host_" + hostIndex+")"
          })
        restart();
      }
      function removeSelectedNode(d) {
        if (d.name in $scope.selected_nodes) {
          delete $scope.selected_nodes[d.name]
          updateSelectedNodes()
        }
      }
      function toggleSelectedNode(d) {
        if (d.name in $scope.selected_nodes)
          delete $scope.selected_nodes[d.name]
        else
          $scope.selected_nodes[d.name] = d
        updateSelectedNodes()
      }
      function addSelectedNode(d) {
        $scope.selected_nodes[d.name] = d
        updateSelectedNodes()
      }

      function doSetRouterHostDialog(node, multi) {
        var d = $uibModal.open({
          dialogClass: "modal dlg-large",
          backdrop: true,
          keyboard: true,
          backdropClick: true,
          controller: 'QDR.SetRouterHostDialogController',
          templateUrl: 'set-router-host.html',
          resolve: {
            host: function() {
              var host = node.host || '0.0.0.0'
              return host;
            }
          }
        });
        $timeout(function () {
          d.result.then(function(result) {
            if (result) {
              function setAHost(n) {
                n.host = result.host
                // loop through all listeners and set the host
                if (node.listeners) {
                  for (var listener in n.listeners) {
                    n.listeners[listener].host = result.host
                  }
                }
              }
              if (multi) {
                for (var i=0; i<nodes.length; i++) {
                  if (isSelectedNode(nodes[i]))
                    setAHost(nodes[i])
                }
              } else
                setAHost(node)
              resetCircleHosts()
              restart()
            }
          });
        })
      }

      function doNewDialog() {
        var d = $uibModal.open({
          dialogClass: "modal dlg-large",
          backdrop: true,
          keyboard: true,
          backdropClick: true,
          controller: 'QDR.NewDialogController',
          templateUrl: 'new-config-template.html',
          resolve: {
            list: function() {
              return $scope.mockTopologies;
            }
          }
        });
        $timeout(function () {
          d.result.then(function(result) {
            if (result) {
              // append the new topology to the list of configs and switch to the new one
              if ($scope.mockTopologies.indexOf(result.newTopology) < 0) {
                $scope.mockTopologies.push(result.newTopology)
                $scope.mockTopologies.sort()
              }
              $scope.mockTopologyDir = result.newTopology
            }
          });
        })
      };
      function doDeployDialog(startFn) {
        var host = undefined
        var port = undefined
        for (var i=0; i<nodes.length; i++) {
          var node = nodes[i]
          if (node.listeners) {
            for (var l in node.listeners) {
              var listener = node.listeners[l]
              if (listener.http) {
                host = node.host
                port = listener.port
              }
            }
          }
        }
        var d = $uibModal.open({
          dialogClass: "modal dlg-large",
          backdrop: true,
          keyboard: true,
          backdropClick: true,
          controller: 'QDR.DeployDialogController',
          templateUrl: 'deploy-template.html',
          resolve: {
            dir: function () {
              return $scope.mockTopologyDir
            },
            http_host: function () {
              return host
            },
            http_port: function () {
              return port
            },
            start_fn: function () {
              return startFn
            },
            hosts: function () {
              var hosts = []
              var hostNames = getHosts()
              for (var i=0; i<hostNames.length; i++) {
                var hostName = hostNames[i]
                var hostIndex = getHostIndex(hostName)
                var hostNodes = []
                for (var j=0; j<nodes.length; j++) {
                  if (nodes[j].host === hostName && nodes[j].cls === 'router') {
                    hostNodes.push(nodes[j].name)
                  }
                }
                hosts.push({name: hostName, nodes: hostNodes, color: 'host'+hostIndex, pass: ''})
              }
              return hosts
            }
          }
        });
        $timeout(function () {
          d.result.then(function(result) {
            if (result) {
            }
          });
        })
      }

      function doSettingsDialog(opts) {
        var d = $uibModal.open({
          dialogClass: "modal dlg-large",
          backdrop: true,
          keyboard: true,
          backdropClick: true,
          controller: 'QDR.SettingsDialogController',
          templateUrl: 'settings-template.html',
          resolve: {
            settings: function() {
              return opts
            }
          }
        });
        $timeout(function () {
          d.result.then(function(result) {
            if (result) {
              Object.assign(settings, result)
            }
          });
        })
      };
      function valFromMapArray(ar, key, val) {
        for (var i=0; i<ar.length; i++) {
          if (ar[i][key] && ar[i][key] === val)
            return ar[i]
        }
        return undefined
      }

      function doEditDialog(node, entity, context, multi) {
        var entity2key = {router: 'name', log: 'module', sslProfile: 'name', connector: 'port', listener: 'port', address: 'pattern|prefix'}
        var d = $uibModal.open({
          dialogClass: "modal dlg-large",
          backdrop: true,
          keyboard: true,
          backdropClick: true,
          controller: 'QDR.NodeDialogController',
          templateUrl: 'node-config-template.html',
          resolve: {
            node: function() {
              return node;
            },
            entityType: function() {
              return entity
            },
            context: function () {
              return context
            },
            entityKey: function () {
              return entity2key[entity]
            },
            hasLinks: function () {
              return links.some(function (l) {
                return l.source.key === node.key || l.target.key === node.key
              })
            },
            maxPort: function () {
              var maxPort = settings.normal_port
              nodes.forEach(function (node) {
                if (node.entity === 'listener' || node.entity === 'connector') {
                  if (node.entityKey !== 'amqp' && node.entityKey != settings.artemis_port && node.entityKey != settings.qpid_port)
                    if (parseInt(node.entityKey) > maxPort)
                      maxPort = parseInt(node.entityKey)
                }
              })
              return maxPort
            }
          }
        });
          d.result.then(function(result) {
            if (result) {
              if (entity === 'router') {
                var router = valFromMapArray(result.entities, "actualName", "router")
                if (router) {
                    var r = new FormValues(router)
                    r.name(router)
                    Object.assign(node, r.node)
                    initGraph()
                    initForce()
                    restart();
                }
              }
              else {
                if ('del' in result) {
                  // find the 'normal' node that is associated with this entry
                  var n = findChildNode(entity, context, node.name)
                  if (n)
                    delNode(n)
                } else {
                  var rVals = valFromMapArray(result.entities, "actualName", entity)
                  if (rVals) {
                    var nodeObj = node[entity+'s']
                    var key = entity2key[entity]
                    var o = new FormValues(rVals)
                    oNodeKey = o.node[key]
                    // address can have either a prefix or pattern
                    if (entity === 'address') {
                      oNodeKey = o.node['prefix'] ? o.node['prefix'] : o.node['pattern']
                    }
                    if (!angular.isDefined(nodeObj)) {
                      node[entity+'s'] = {}
                      nodeObj = node[entity+'s']
                    }
                    // we were editing an existing section and the key for that section was changed
                    else if (oNodeKey !== context && context !== 'new' && context !== 'artemis' && context != 'qpid') {
                      delete nodeObj[context]
                    }
                    if (entity === 'log' || entity === 'address') {
                      if (multi) {
                        // apply this entity's key to all selected routers
                        for (var i=0; i<nodes.length; i++) {
                          if (isSelectedNode(nodes[i])) {
                            var ents = nodes[i][entity+'s']
                            if (!ents)
                              nodes[i][entity+'s'] = {}
                            if (!nodes[i][entity+'s'][oNodeKey])
                              addToNode(nodes[i], entity, oNodeKey)
                            nodes[i][entity+'s'][oNodeKey] = o.node
                          }
                        }
                        return
                      }
                    }
                    nodeObj[oNodeKey] = o.node
                    if (entity === 'sslProfile')
                      return
                    if (context === 'new' || context === 'artemis' || context === 'qpid') {
                      if (context !== 'new')
                        entity = context
                      addToNode(node, entity, oNodeKey)
                    }
                  }
                }
              }
            }
          });
      };

      var FormValues = function (entity) {
          this.node = {};
          for (var i=0; i<entity.attributes.length; i++) {
            var attr = entity.attributes[i]
            if (typeof attr.rawtype === 'object' && attr['selected'])
              attr['value'] = attr['selected']
            this.node[attr['name']] = attr['value']
          }
      };

      FormValues.prototype.name = function (entity) {
          var name = valFromMapArray(entity.attributes, "name", "name")
          if (name) {
            name = name.value
            this.node['name'] = name
            this.node['routerId'] = name
            this.node['key'] = "amqp:/_topo/0/" + name + "/$management"
          }
      };


    }
  ]);

  QDR.module.controller("QDR.SetRouterHostDialogController", function ($scope, $uibModalInstance, host) {
    $scope.host = host
    $scope.setSettings = function () {
      $uibModalInstance.close({host: $scope.host});
    }

    $scope.cancel = function () {
      $uibModalInstance.close()
    }
  })

  QDR.module.controller("QDR.ShowConfigDialogController", function ($scope, $uibModalInstance, config) {
    $scope.config = config
    $scope.ok = function () {
      $uibModalInstance.close()
    }
  })

  QDR.module.controller("QDR.NewDialogController", function($scope, $uibModalInstance, list) {
    $scope.newTopology = ""
    $scope.exclude = list
    $scope.inList = function () {
      return $scope.exclude.indexOf($scope.newTopology) >= 0
    }

    $scope.setSettings = function () {
      $uibModalInstance.close({
        newTopology: $scope.newTopology
      });
    }
    $scope.cancel = function () {
      $uibModalInstance.close()
    }
  })

  QDR.module.controller("QDR.SettingsDialogController", function($scope, $uibModalInstance, settings) {
    var local_settings = {}
    Object.assign(local_settings, settings)
    $scope.entity = {description: "Settings",
                    attributes: [
                      {name: "baseName", humanName: "Starting router name", input: "input", type: "text", value: local_settings.baseName, required: true},
                      {name: "http", humanName: "Port for console listeners", input: "input", type: "text", value: local_settings.http_port, required: true},
                      {name: "normal_port", humanName: "Starting port for normal listeners/connectors", input: "input", type: "text", value: local_settings.normal_port, required: true},
                      {name: "internal_port", humanName: "Starting port for inter-router listeners/connectors", input: "input", type: "text", value: local_settings.internal_port, required: true}
                    ]}

    $scope.setSettings = function () {
      var newSettings = {}
      $scope.entity.attributes.forEach( function (attr) {
        newSettings[attr.name] = attr.value
      })
      $uibModalInstance.close(newSettings);
    }

    $scope.cancel = function () {
      $uibModalInstance.close()
    }

  })

  QDR.module.directive('focusWhen', function($timeout) {
    return {
      restrict : 'A',
      link : function($scope, $element, $attr) {
        $scope.$watch($attr.focusWhen, function(val) {
          $timeout(function() {
            if (val)
              $element[0].focus()
          });
        });
      }
    }
  })

  QDR.module.controller("QDR.DeployDialogController", function($scope, $uibModalInstance, QDRService, $timeout, $sce, dir, http_host, http_port, hosts, start_fn) {
    // setup polling to get deployment status
    $scope.polling = true
    $scope.state = "Deploying " + dir
    $scope.deploy_state = ""
    $scope.close_button_class = "btn-warning"
    var success_state = "Deploy Completed"
    $scope.address = ""
    var pollTimer = null
    function doPoll() {
      QDRService.sendMethod("DEPLOY-STATUS", {config: dir}, function (response) {
        if (response[1] !== 'DEPLOYING') {
          $scope.polling = false
          if (response[1] === "DONE") {
            $scope.state = success_state
            $scope.close_button_class = "btn-success"
            Core.notification('info', dir + " deployed");
            if (http_host && http_port) {
              $scope.address = $sce.trustAsHtml("http://" + http_host + ":" + http_port + "/#!/topology")
            }
          } else {
            $scope.state = "Deploy Failed"
            $scope.close_button_class = "btn-danger"
            Core.notification('error', dir + " deployment failed");
          }
        }
        $timeout(function () {
          $scope.status = response[0]
          scrollToEnd()
          if ($scope.polling) (
            pollTimer = setTimeout(doPoll, 1000)
          )
        })
      })
    }
    $scope.deploy = function () {
      $scope.deploy_state = "deploying"
      start_fn(hosts)
      doPoll()
    }
    $scope.done = function () {
      return !$scope.polling && $scope.state === success_state
    }
    $scope.hasConsole = function () {
      return http_host && http_port && $scope.state === success_state
    }

    $scope.showing_pass = undefined
    $scope.show_pass = function (host) {
      $scope.showing_pass = (host) ? host.name : ''
    }
    $scope.hosts = hosts
    $scope.pass = function () {
      console.log('set password')
    }

    function scrollTopTween(scrollTop) {
      return function() {
        var i = d3.interpolateNumber(this.scrollTop, scrollTop);
        return function(t) { this.scrollTop = i(t); };
      }
    }
    var scrollToEnd = function () {
      var scrollheight = d3.select("#deploy_output").property("scrollHeight");

      d3.select('#deploy_output')
        .transition().duration(1000)
        .tween("uniquetweenname", scrollTopTween(scrollheight));
    }
    $scope.cancel = function () {
      polling = false
      clearTimeout(pollTimer)
      $uibModalInstance.close()
    }

  })

  return QDR;
}(QDR || {}));

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

  QDR.module.controller('QDR.TopologyFormController', function($scope, $rootScope, $timeout, QDRService) {

    $scope.attributes = []
    var nameTemplate = '<div title="{{row.entity.description}}" class="ngCellText {{row.entity.cls}}"><span>{{row.entity.attributeName}}</span></div>';
    var valueTemplate = '<div title="{{row.entity.attributeValue}}" class="ngCellText {{row.entity.cls}}"><span>{{row.entity.attributeValue}}</span></div>';
    $scope.topoGridOptions = {
      data: 'attributes',
      enableColumnResize: false,
      multiSelect: false,
      jqueryUIDraggable: true,
      columnDefs: [{
        field: 'attributeName',
        displayName: 'Attribute',
        cellTemplate: nameTemplate
      }, {
        field: 'attributeValue',
        displayName: 'Value',
        cellTemplate: valueTemplate
      }]
    };
    $scope.form = 'router'
    $scope.$on('showEntityForm', function(event, args) {
      var attributes = args.attributes;
      var entityTypes = QDRService.schema.entityTypes[args.entity].attributes;
      attributes.forEach(function(attr) {
        attr.cls = ''
        if (attr.attributeName === 'Listening on')
          attr.cls = 'listening-on'
        if (entityTypes[attr.attributeName] && entityTypes[attr.attributeName].description) {
          attr.description = entityTypes[attr.attributeName].description
        }
        //QDR.log.debug("attr.description " + attr.description)
      })
      $scope.attributes = attributes;
      $scope.form = args.entity;
    })
  })
  /**
   * @method TopologyController
   *
   * Controller that handles the QDR topology page
   */
  QDR.module.controller("QDR.TopologyController", ['$scope', '$rootScope', 'QDRService', '$location', '$timeout', '$uibModal',
    function($scope, $rootScope, QDRService, $location, $timeout, $uibModal) {

      $scope.multiData = []
      $scope.selectedClient = [];
      $scope.quiesceState = {}
      var dontHide = false;

      $scope.panelVisible = true  // show/hide the panel on the left
      $scope.hideLeftPane = function () {
        d3.select(".qdr-topology.pane.left")
          .transition().duration(300).ease("sin-in")
          .style("left" , "-380px")

        d3.select(".panel-adjacent")
          .transition().duration(300).ease("sin-in")
          .style("margin-left", "30px")
          .each("end", function () {
            resize()
            $timeout(function () {$scope.panelVisible = false})
          })
      }
      $scope.showLeftPane = function () {
        d3.select(".qdr-topology.pane.left")
          .transition().duration(300).ease("sin-out")
          .style("left" , "0px")

        d3.select(".panel-adjacent")
          .transition().duration(300).ease("sin-out")
          .style("margin-left", "430px")
          .each("end", function () {
            resize()
            $timeout(function () {$scope.panelVisible = true})
          })
      }

      $scope.quiesceConnection = function(row) {
        var entity = row.entity;
        var state = $scope.quiesceState[entity.connectionId].state;
        if (state === 'enabled') {
          // start quiescing all links
          $scope.quiesceState[entity.connectionId].state = 'quiescing';
        } else if (state === 'quiesced') {
          // start reviving all links
          $scope.quiesceState[entity.connectionId].state = 'reviving';
        }
        $scope.multiDetails.updateState(entity);
        dontHide = true;
        $scope.multiDetails.selectRow(row.rowIndex, true);
        $scope.multiDetails.showLinksList(row)
      }
      $scope.quiesceDisabled = function(row) {
        return $scope.quiesceState[row.entity.connectionId].buttonDisabled;
      }
      $scope.quiesceText = function(row) {
        return $scope.quiesceState[row.entity.connectionId].buttonText;
      }
      $scope.quiesceClass = function(row) {
        var stateClassMap = {
          enabled: 'btn-primary',
          quiescing: 'btn-warning',
          reviving: 'btn-warning',
          quiesced: 'btn-danger'
        }
        return stateClassMap[$scope.quiesceState[row.entity.connectionId].state];
      }

      $scope.multiData = []
      $scope.multiDetails = {
        data: 'multiData',
        selectedItems: $scope.selectedClient,
        multiSelect: false,
        jqueryUIDraggable: true,
        afterSelectionChange: function(obj) {
          if (obj.selected && obj.orig) {
            var detailsDiv = d3.select('#link_details')
            var isVis = detailsDiv.style('display') === 'block';
            if (!dontHide && isVis && $scope.connectionId === obj.entity.connectionId) {
              hideLinkDetails();
              return;
            }
            dontHide = false;
            $scope.multiDetails.showLinksList(obj)
          }
        },
        showLinksList: function(obj) {
          $scope.linkData = obj.entity.linkData;
          $scope.connectionId = obj.entity.connectionId;
          var visibleLen = Math.min(obj.entity.linkData.length, 10)
          //QDR.log.debug("visibleLen is " + visibleLen)
          var left = parseInt(d3.select('#multiple_details').style("left"))
          var offset = jQuery('#topology').offset();
          var detailsDiv = d3.select('#link_details')
          detailsDiv
            .style({
              display: 'block',
              opacity: 1,
              left: (left + 20) + "px",
              top: (mouseY - offset.top + 20 + $(document).scrollTop()) + "px",
              height: ((visibleLen + 1) * 30) + 40 + "px", // +1 for the header row
              'overflow-y': obj.entity.linkData > 10 ? 'scroll' : 'hidden'
            })
        },
        updateState: function(entity) {
          var state = $scope.quiesceState[entity.connectionId].state

          // count enabled and disabled links for this connection
          var enabled = 0,
            disabled = 0;
          entity.linkData.forEach(function(link) {
            if (link.adminStatus === 'enabled')
              ++enabled;
            if (link.adminStatus === 'disabled')
              ++disabled;
          })

          var linkCount = entity.linkData.length;
          // if state is quiescing and any links are enabled, button should say 'Quiescing' and be disabled
          if (state === 'quiescing' && (enabled > 0)) {
            $scope.quiesceState[entity.connectionId].buttonText = 'Quiescing';
            $scope.quiesceState[entity.connectionId].buttonDisabled = true;
          } else
          // if state is enabled and all links are disabled, button should say Revive and be enabled. set state to quisced
          // if state is quiescing and all links are disabled, button should say 'Revive' and be enabled. set state to quiesced
          if ((state === 'quiescing' || state === 'enabled') && (disabled === linkCount)) {
            $scope.quiesceState[entity.connectionId].buttonText = 'Revive';
            $scope.quiesceState[entity.connectionId].buttonDisabled = false;
            $scope.quiesceState[entity.connectionId].state = 'quiesced'
          } else
          // if state is reviving and any links are disabled, button should say 'Reviving' and be disabled
          if (state === 'reviving' && (disabled > 0)) {
            $scope.quiesceState[entity.connectionId].buttonText = 'Reviving';
            $scope.quiesceState[entity.connectionId].buttonDisabled = true;
          } else
          // if state is reviving or quiesced and all links are enabled, button should say 'Quiesce' and be enabled. set state to enabled
          if ((state === 'reviving' || state === 'quiesced') && (enabled === linkCount)) {
            $scope.quiesceState[entity.connectionId].buttonText = 'Quiesce';
            $scope.quiesceState[entity.connectionId].buttonDisabled = false;
            $scope.quiesceState[entity.connectionId].state = 'enabled'
          }
        },
        columnDefs: [{
            field: 'host',
            cellTemplate: "titleCellTemplate.html",
            headerCellTemplate: 'titleHeaderCellTemplate.html',
            displayName: 'Connection host'
          }, {
            field: 'user',
            cellTemplate: "titleCellTemplate.html",
            headerCellTemplate: 'titleHeaderCellTemplate.html',
            displayName: 'User'
          }, {
            field: 'properties',
            cellTemplate: "titleCellTemplate.html",
            headerCellTemplate: 'titleHeaderCellTemplate.html',
            displayName: 'Properties'
          }
          /*,
                {
                  cellClass: 'gridCellButton',
                  cellTemplate: '<button title="{{quiesceText(row)}} the links" type="button" ng-class="quiesceClass(row)" class="btn" ng-click="$event.stopPropagation();quiesceConnection(row)" ng-disabled="quiesceDisabled(row)">{{quiesceText(row)}}</button>'
                }*/
        ]
      };
      $scope.quiesceLinkClass = function(row) {
        var stateClassMap = {
          enabled: 'btn-primary',
          disabled: 'btn-danger'
        }
        return stateClassMap[row.entity.adminStatus]
      }
      $scope.quiesceLink = function(row) {
        QDRService.quiesceLink(row.entity.nodeId, row.entity.name);
      }
      $scope.quiesceLinkDisabled = function(row) {
        return (row.entity.operStatus !== 'up' && row.entity.operStatus !== 'down')
      }
      $scope.quiesceLinkText = function(row) {
        return row.entity.operStatus === 'down' ? "Revive" : "Quiesce";
      }
      $scope.linkData = [];
      $scope.linkDetails = {
        data: 'linkData',
        jqueryUIDraggable: true,
        columnDefs: [{
            field: 'adminStatus',
            cellTemplate: "titleCellTemplate.html",
            headerCellTemplate: 'titleHeaderCellTemplate.html',
            displayName: 'Admin state'
          }, {
            field: 'operStatus',
            cellTemplate: "titleCellTemplate.html",
            headerCellTemplate: 'titleHeaderCellTemplate.html',
            displayName: 'Oper state'
          }, {
            field: 'dir',
            cellTemplate: "titleCellTemplate.html",
            headerCellTemplate: 'titleHeaderCellTemplate.html',
            displayName: 'dir'
          }, {
            field: 'owningAddr',
            cellTemplate: "titleCellTemplate.html",
            headerCellTemplate: 'titleHeaderCellTemplate.html',
            displayName: 'Address'
          }, {
            field: 'deliveryCount',
            displayName: 'Delivered',
            headerCellTemplate: 'titleHeaderCellTemplate.html',
            cellClass: 'grid-values'

          }, {
            field: 'uncounts',
            displayName: 'Outstanding',
            headerCellTemplate: 'titleHeaderCellTemplate.html',
            cellClass: 'grid-values'
          }
          /*,
                {
                  cellClass: 'gridCellButton',
                  cellTemplate: '<button title="{{quiesceLinkText(row)}} this link" type="button" ng-class="quiesceLinkClass(row)" class="btn" ng-click="quiesceLink(row)" ng-disabled="quiesceLinkDisabled(row)">{{quiesceLinkText(row)}}</button>'
                }*/
        ]
      }

      if (!QDRService.connected) {
        // we are not connected. we probably got here from a bookmark or manual page reload
        QDRService.redirectWhenConnected("topology");
        return;
      }
      // we are currently connected. setup a handler to get notified if we are ever disconnected
      var onDisconnect = function () {
        //QDR.log.info("we were just disconnected while on the topology page. Setting org to redirect back once we are connected again")
        $timeout(function () {
          QDRService.redirectWhenConnected("topology")
        })
      }
      QDRService.addDisconnectAction( onDisconnect )

      var urlPrefix = $location.absUrl();
      urlPrefix = urlPrefix.split("#")[0]
      QDR.log.debug("started QDR.TopologyController with urlPrefix: " + urlPrefix);

      // mouse event vars
      var selected_node = null,
        selected_link = null,
        mousedown_link = null,
        mousedown_node = null,
        mouseover_node = null,
        mouseup_node = null,
        initial_mouse_down_position = null;

      $scope.schema = "Not connected";

      $scope.contextNode = null; // node that is associated with the current context menu
      $scope.isRight = function(mode) {
        return mode.right;
      }

      var setNodesFixed = function (name, b) {
        nodes.some(function (n) {
          if (n.name === name) {
            n.fixed = b;
            return true;
          }
        })
      }
      $scope.setFixed = function(b) {
        if ($scope.contextNode) {
          $scope.contextNode.fixed = b;
          setNodesFixed($scope.contextNode.name, b)
          savePositions()
        }
        restart();
      }
      $scope.isFixed = function() {
        if (!$scope.contextNode)
          return false;
        return ($scope.contextNode.fixed & 1);
      }

      var mouseX, mouseY;
      var relativeMouse = function () {
        var offset = jQuery('#topology').offset();
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
        $scope.contextNode = null;
        $(".contextMenu").fadeOut(200);
      });

      var radii = {
        'inter-router': 25,
        'normal': 15,
        'on-demand': 15,
        'route-container': 15,
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
        var tpformHeight = $('#topologyForm').height()
        var height = Math.max(window.innerHeight, tpformHeight + top) - top - gap;
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

      $scope.$on('panel-resized', function () {
        resize()
      })
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

      var nodeExists = function (connectionContainer) {
        for (var i=0; i<nodes.length; ++i) {
          if (nodes[i].container === connectionContainer) {
            return i
          }
        }
        return -1
      }
      var normalExists = function (connectionContainer) {
        var normalInfo = {}
        for (var i=0; i<nodes.length; ++i) {
          if (nodes[i].normals) {
            if (nodes[i].normals.some(function (normal, j) {
              if (normal.container === connectionContainer && i !== j) {
                normalInfo = {nodesIndex: i, normalsIndex: j}
                return true
              }
              return false
            }))
              break;
          }
        }
        return normalInfo
      }
      var getLinkSource = function (nodesIndex) {
        for (var i=0; i<links.length; ++i) {
          if (links[i].target === nodesIndex)
            return i
        }
        return -1
      }
      var aNode = function(id, name, nodeType, nodeInfo, nodeIndex, x, y, connectionContainer, resultIndex, fixed, properties) {
        properties = properties || {};
        for (var i=0; i<nodes.length; ++i) {
          if (nodes[i].name === name || nodes[i].container === connectionContainer) {
            if (properties.product)
              nodes[i].properties = properties
            return nodes[i]
          }
        }
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
          resultIndex: resultIndex,
          fixed: !!+fixed,
          cls: '',
          container: connectionContainer
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

      var getLinkDir = function (id, connection, onode) {
        var links = onode[".router.link"]
        if (!links) {
          return "unknown"
        }
        var inCount = 0, outCount = 0
        links.results.forEach( function (linkResult) {
          var link = QDRService.flatten(links.attributeNames, linkResult)
          if (link.linkType === "endpoint" && link.connectionId === connection.identity)
            if (link.linkDir === "in")
              ++inCount
            else
              ++outCount
        })
        if (inCount > 0 && outCount > 0)
          return "both"
        if (inCount > 0)
          return "in"
        if (outCount > 0)
          return "out"
        return "unknown"
      }

      var savePositions = function () {
        nodes.forEach( function (d) {
          localStorage[d.name] = angular.toJson({
            x: Math.round(d.x),
            y: Math.round(d.y),
            fixed: (d.fixed & 1) ? 1 : 0,
          });
        })
      }

      var initializeNodes = function (nodeInfo) {
        var nodeCount = Object.keys(nodeInfo).length
        var yInit = 50;
        nodes = []
        for (var id in nodeInfo) {
          var name = QDRService.nameFromId(id);
          // if we have any new nodes, animate the force graph to position them
          var position = angular.fromJson(localStorage[name]);
          if (!angular.isDefined(position)) {
            animate = true;
            position = {
              x: Math.round(width / 4 + ((width / 2) / nodeCount) * nodes.length),
              y: Math.round(height / 2 + Math.sin(nodes.length / (Math.PI*2.0)) * height / 4),
              fixed: false,
            };
            //QDR.log.debug("new node pos (" + position.x + ", " + position.y + ")")
          }
          if (position.y > height) {
            position.y = 200 - yInit;
            yInit *= -1
          }
          nodes.push(aNode(id, name, "inter-router", nodeInfo, nodes.length, position.x, position.y, name, undefined, position.fixed));
        }
      }

      var initializeLinks = function (nodeInfo, unknowns) {
        links = [];
        var source = 0;
        var client = 1.0;
        for (var id in nodeInfo) {
          var onode = nodeInfo[id];
          if (!onode['.connection'])
            continue
          var conns = onode['.connection'].results;
          var attrs = onode['.connection'].attributeNames;
          //QDR.log.debug("external client parent is " + parent);
          var normalsParent = {}; // 1st normal node for this parent

          for (var j = 0; j < conns.length; j++) {
            var connection = QDRService.flatten(attrs, conns[j])
            var role = connection.role
            var properties = connection.properties || {};
            var dir = connection.dir
            if (role == "inter-router") {
              var connId = connection.container
              var target = getContainerIndex(connId, nodeInfo);
              if (target >= 0) {
                getLink(source, target, dir, "", source + "-" + target);
              }
            } else if (role == "normal" || role == "on-demand" || role === "route-container") {
              // not an connection between routers, but an external connection
              var name = QDRService.nameFromId(id) + "." + connection.identity;

              // if we have any new clients, animate the force graph to position them
              var position = angular.fromJson(localStorage[name]);
              if (!angular.isDefined(position)) {
                animate = true;
                position = {
                  x: Math.round(nodes[source].x + 40 * Math.sin(client / (Math.PI * 2.0))),
                  y: Math.round(nodes[source].y + 40 * Math.cos(client / (Math.PI * 2.0))),
                  fixed: false
                };
                //QDR.log.debug("new client pos (" + position.x + ", " + position.y + ")")
              }// else QDR.log.debug("using previous location")
              if (position.y > height) {
                position.y = Math.round(nodes[source].y + 40 + Math.cos(client / (Math.PI * 2.0)))
              }
              var existingNodeIndex = nodeExists(connection.container)
              var normalInfo = normalExists(connection.container)
              var node = aNode(id, name, role, nodeInfo, nodes.length, position.x, position.y, connection.container, j, position.fixed, properties)
              var nodeType = QDRService.isAConsole(properties, connection.identity, role, node.key) ? "console" : "client"
              var cdir = getLinkDir(id, connection, onode)
              if (existingNodeIndex >= 0) {
                // make a link between the current router (source) and the existing node
                getLink(source, existingNodeIndex, dir, "small", connection.name);
              } else if (normalInfo.nodesIndex) {
                  // get node index of node that contained this connection in its normals array
                  var normalSource = getLinkSource(normalInfo.nodesIndex)
                  if (normalSource >= 0) {
                    if (cdir === 'unknown')
                      cdir = dir
                    node.cdir = cdir
                    nodes.push(node)
                    // create link from original node to the new node
                    getLink(links[normalSource].source, nodes.length-1, cdir, "small", connection.name)
                    // create link from this router to the new node
                    getLink(source, nodes.length-1, cdir, "small", connection.name)
                    // remove the old node from the normals list
                    nodes[normalInfo.nodesIndex].normals.splice(normalInfo.normalsIndex, 1)
                  }
              } else if (role === 'normal') {
              // normal nodes can be collapsed into a single node if they are all the same dir
                if (cdir !== 'unknown') {
                  node.user = connection.user
                  node.isEncrypted = connection.isEncrypted
                  node.host = connection.host
                  node.connectionId = connection.identity
                  node.cdir = cdir
                  // determine arrow direction by using the link directions
                  if (!normalsParent[nodeType+cdir]) {
                    normalsParent[nodeType+cdir] = node;
                    nodes.push(node);
                    node.normals = [node];
                    // now add a link
                    getLink(source, nodes.length - 1, cdir, "small", connection.name);
                    client++;
                  } else {
                    normalsParent[nodeType+cdir].normals.push(node)
                  }
                } else {
                  node.id = nodes.length - 1 + unknowns.length
                  unknowns.push(node)
                }
              } else {
                nodes.push(node)
                  // now add a link
                getLink(source, nodes.length - 1, dir, "small", connection.name);
                client++;
              }
            }
          }
          source++;
        }
      }

      // vary the following force graph attributes based on nodeCount
      // <= 6 routers returns min, >= 80 routers returns max, interpolate linearly
      var forceScale = function(nodeCount, min, max) {
        var count = nodeCount
        if (nodeCount < 6) count = 6
        if (nodeCount > 80) count = 80
        var x = d3.scale.linear()
          .domain([6,80])
          .range([min, max]);
//QDR.log.debug("forceScale(" + nodeCount + ", " + min + ", " + max + "  returns " + x(count) + " " + x(nodeCount))
        return x(count)
      }
      var linkDistance = function (d, nodeCount) {
        if (d.target.nodeType === 'inter-router')
          return forceScale(nodeCount, 150, 70)
        return forceScale(nodeCount, 75, 40)
      }
      var charge = function (d, nodeCount) {
        if (d.nodeType === 'inter-router')
          return forceScale(nodeCount, -1800, -900)
        return -900
      }
      var gravity = function (d, nodeCount) {
        return forceScale(nodeCount, 0.0001, 0.1)
      }
      // initialize the nodes and links array from the QDRService.topology._nodeInfo object
      var initForceGraph = function() {
        nodes = [];
        links = [];
        var nodeInfo = QDRService.topology.nodeInfo();
        var nodeCount = Object.keys(nodeInfo).length

        var oldSelectedNode = selected_node
        var oldMouseoverNode = mouseover_node
        mouseover_node = null;
        selected_node = null;
        selected_link = null;

        savePositions();
        d3.select("#SVG_ID").remove();
        svg = d3.select('#topology')
          .append('svg')
          .attr("id", "SVG_ID")
          .attr('width', width)
          .attr('height', height)
          .on('click', function(d) {
            removeCrosssection()
          });

        $(document).keyup(function(e) {
          if (e.keyCode === 27) {
            removeCrosssection()
          }
        });

        // the legend
        d3.select("#svg_legend svg").remove();
        lsvg = d3.select("#svg_legend")
          .append('svg')
          .attr('id', 'svglegend')
        lsvg = lsvg.append('svg:g')
          .attr('transform', 'translate(' + (radii['inter-router'] + 2) + ',' + (radii['inter-router'] + 2) + ')')
          .selectAll('g');

        // mouse event vars
        mousedown_link = null;
        mousedown_node = null;
        mouseup_node = null;

        // initialize the list of nodes
        initializeNodes(nodeInfo)
        savePositions()

        // initialize the list of links
        var unknowns = []
        initializeLinks(nodeInfo, unknowns)
        $scope.schema = QDRService.schema;
        // init D3 force layout
        force = d3.layout.force()
          .nodes(nodes)
          .links(links)
          .size([width, height])
          .linkDistance(function(d) { return linkDistance(d, nodeCount) })
          .charge(function(d) { return charge(d, nodeCount) })
          .friction(.10)
          .gravity(function(d) { return gravity(d, nodeCount) })
          .on('tick', tick)
          .on('end', function () {savePositions()})
          .start()

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

        var grad = svg.append("svg:defs").append("linearGradient")
          .attr("id", "half-circle")
          .attr("x1", "0%")
          .attr("x2", "0%")
          .attr("y1", "100%")
          .attr("y2", "0%");
        grad.append("stop").attr("offset", "50%").style("stop-color", "#C0F0C0");
        grad.append("stop").attr("offset", "50%").style("stop-color", "#F0F000");

        // handles to link and node element groups
        path = svg.append('svg:g').selectAll('path'),
          circle = svg.append('svg:g').selectAll('g');

        // app starts here
        restart(false);
        force.start();
        if (oldSelectedNode) {
          d3.selectAll('circle.inter-router').classed("selected", function (d) {
            if (d.key === oldSelectedNode.key) {
              selected_node = d;
              return true
            }
            return false
          })
        }
        if (oldMouseoverNode && selected_node) {
          d3.selectAll('circle.inter-router').each(function (d) {
            if (d.key === oldMouseoverNode.key) {
              mouseover_node = d
              QDRService.ensureAllEntities([{entity: ".router.node", attrs: ["id","nextHop"]}], function () {
                nextHop(selected_node, d);
                restart();
              })
            }
          })
        }
        setTimeout(function () {
          updateForm(Object.keys(QDRService.topology.nodeInfo())[0], 'router', 0);
        })

        // if any clients don't yet have link directions, get the links for those nodes and restart the graph
        if (unknowns.length > 0)
          setTimeout(resolveUnknowns, 10, nodeInfo, unknowns)

        var continueForce = function (extra) {
          if (extra > 0) {
            --extra
            force.start()
            setTimeout(continueForce, 100, extra)
          }
        }
        continueForce(forceScale(nodeCount, 0, 200))  // give graph time to settle down
      }

      var resolveUnknowns = function (nodeInfo, unknowns) {
        var unknownNodes = {}
        // collapse the unknown node.keys using an object
        for (var i=0; i<unknowns.length; ++i) {
          unknownNodes[unknowns[i].key] = 1
        }
        unknownNodes = Object.keys(unknownNodes)
        //QDR.log.info("-- resolveUnknowns: ensuring .connection and .router.link are present for each node")
        QDRService.ensureEntities(unknownNodes, [{entity: ".connection", force: true}, {entity: ".router.link", attrs: ["linkType","connectionId","linkDir"], force: true}], function () {
          initializeLinks(nodeInfo, [])
          // collapse any router-container nodes that are duplicates
          animate = true;
          force.nodes(nodes).links(links).start();
          restart(false);
        })
      }

      function updateForm(key, entity, resultIndex) {
        var nodeInfo = QDRService.topology.nodeInfo();
        if (key in nodeInfo) {
          QDRService.ensureEntities(key, [
            {entity: '.'+entity},
            {entity: '.listener', attrs: ["role", "port"]}], function () {
            var onode = nodeInfo[key]
            var nodeResults = onode['.' + entity].results[resultIndex]
            var nodeAttributes = onode['.' + entity].attributeNames
            var attributes = nodeResults.map(function(row, i) {
                return {
                  attributeName: nodeAttributes[i],
                  attributeValue: row
                }
              })
              // sort by attributeName
            attributes.sort(function(a, b) {
              return a.attributeName.localeCompare(b.attributeName)
            })

            // move the Name first
            var nameIndex = attributes.findIndex(function(attr) {
              return attr.attributeName === 'name'
            })
            if (nameIndex >= 0)
              attributes.splice(0, 0, attributes.splice(nameIndex, 1)[0]);

            // get the list of ports this router is listening on
            if (entity === 'router') {
              var listeners = onode['.listener'].results;
              var listenerAttributes = onode['.listener'].attributeNames;
              var normals = listeners.filter(function(listener) {
                return QDRService.valFor(listenerAttributes, listener, 'role') === 'normal';
              })
              var ports = []
              normals.forEach(function(normalListener) {
                  ports.push(QDRService.valFor(listenerAttributes, normalListener, 'port'))
                })
                // add as 2nd row
              if (ports.length) {
                attributes.splice(1, 0, {
                  attributeName: 'Listening on',
                  attributeValue: ports,
                  description: 'The port on which this router is listening for connections'
                });
              }
            }
            $rootScope.$broadcast('showEntityForm', {
              entity: entity,
              attributes: attributes
            })
            if (!$scope.$$phase) $scope.$apply()
          })
        }
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
        //QDR.log.debug("creating new link (" + (links.length) + ") between " + nodes[_source].name + " and " + nodes[_target].name);
        if (links.some( function (l) { return l.uid === uid}))
          uid = uid + '.' + links.length
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
          //QDR.log.debug("in tick for d");
          //console.dump(d);
          var sourcePadding, targetPadding, r;

          if (d.target.nodeType == "inter-router") {
            r = radius;
            //                       right arrow  left line start
            sourcePadding = d.left ? radius + 8 : radius;
            //                      left arrow      right line start
            targetPadding = d.right ? radius + 16 : radius;
          } else {
            r = radiusNormal - 18;
            sourcePadding = d.left ? radiusNormal + 18 : radiusNormal;
            targetPadding = d.right ? radiusNormal + 16 : radiusNormal;
          }
          var dtx = Math.max(targetPadding, Math.min(width - r, d.target.x)),
            dty = Math.max(targetPadding, Math.min(height - r, d.target.y)),
            dsx = Math.max(sourcePadding, Math.min(width - r, d.source.x)),
            dsy = Math.max(sourcePadding, Math.min(height - r, d.source.y));

          var deltaX = dtx - dsx,
            deltaY = dty - dsy,
            dist = Math.sqrt(deltaX * deltaX + deltaY * deltaY);
          if (dist == 0)
            dist = 0.001;
          var normX = deltaX / dist,
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

      // highlight the paths between the selected node and the hovered node
      function findNextHopNode(from, d) {
        // d is the node that the mouse is over
        // from is the selected_node ....
        if (!from)
          return null;

        if (from == d)
          return selected_node;

        //QDR.log.debug("finding nextHop from: " + from.name + " to " + d.name);
        var sInfo = QDRService.topology.nodeInfo()[from.key];

        if (!sInfo) {
          QDR.log.warn("unable to find topology node info for " + from.key);
          return null;
        }

        // find the hovered name in the selected name's .router.node results
        if (!sInfo['.router.node'])
          return null;
        var aAr = sInfo['.router.node'].attributeNames;
        var vAr = sInfo['.router.node'].results;
        for (var hIdx = 0; hIdx < vAr.length; ++hIdx) {
          var addrT = QDRService.valFor(aAr, vAr[hIdx], "id");
          if (addrT == d.name) {
            //QDR.log.debug("found " + d.name + " at " + hIdx);
            var nextHop = QDRService.valFor(aAr, vAr[hIdx], "nextHop");
            //QDR.log.debug("nextHop was " + nextHop);
            return (nextHop == null) ? nodeFor(addrT) : nodeFor(nextHop);
          }
        }
        return null;
      }

      function nodeFor(name) {
        for (var i = 0; i < nodes.length; ++i) {
          if (nodes[i].name == name)
            return nodes[i];
        }
        return null;
      }

      function linkFor(source, target) {
        for (var i = 0; i < links.length; ++i) {
          if ((links[i].source == source) && (links[i].target == target))
            return links[i];
          if ((links[i].source == target) && (links[i].target == source))
            return links[i];
        }
        // the selected node was a client/broker
        //QDR.log.debug("failed to find a link between ");
        //console.dump(source);
        //QDR.log.debug(" and ");
        //console.dump(target);
        return null;
      }

      function clearPopups() {
        d3.select("#crosssection").style("display", "none");
        $('.hastip').empty();
        d3.select("#multiple_details").style("display", "none")
        d3.select("#link_details").style("display", "none")
        d3.select('#node_context_menu').style('display', 'none');

      }

      function removeCrosssection() {
        setTimeout(function() {
          d3.select("[id^=tooltipsy]").remove()
          $('.hastip').empty();
        }, 1010);
        d3.select("#crosssection svg g").transition()
          .duration(1000)
          .attr("transform", "scale(0)")
            .style("opacity", 0)
            .each("end", function (d) {
                d3.select("#crosssection svg").remove();
                d3.select("#crosssection").style("display","none");
            });
        d3.select("#multiple_details").transition()
          .duration(500)
          .style("opacity", 0)
          .each("end", function(d) {
            d3.select("#multiple_details").style("display", "none")
            stopUpdateConnectionsGrid();
          })
        hideLinkDetails();
      }

      function hideLinkDetails() {
        d3.select("#link_details").transition()
          .duration(500)
          .style("opacity", 0)
          .each("end", function(d) {
            d3.select("#link_details").style("display", "none")
          })
      }

      function clerAllHighlights() {
        for (var i = 0; i < links.length; ++i) {
          links[i]['highlighted'] = false;
        }
        for (var i=0; i<nodes.length; ++i) {
          nodes[i]['highlighted'] = false;
        }
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
            return d.left ? 'url(' + urlPrefix + '#start-arrow' + sel + ')' : '';
          })
          .attr('marker-end', function(d) {
            var sel = d === selected_link ? '-selected' : (d.cls === 'small' ? '-small' : '');
            if (d.highlighted)
              sel = "-highlighted"
            return d.right ? 'url(' + urlPrefix + '#end-arrow' + sel + ')' : '';
          })


        // add new links. if links[] is longer than the existing paths, add a new path for each new element
        path.enter().append('svg:path')
          .attr('class', 'link')
          .attr('marker-start', function(d) {
            var sel = d === selected_link ? '-selected' : (d.cls === 'small' ? '-small' : '');
            return d.left ? 'url(' + urlPrefix + '#start-arrow' + sel + ')' : '';
          })
          .attr('marker-end', function(d) {
            var sel = d === selected_link ? '-selected' : (d.cls === 'small' ? '-small' : '');
            return d.right ? 'url(' + urlPrefix + '#end-arrow' + sel + ')' : '';
          })
          .classed('small', function(d) {
            return d.cls == 'small';
          })
          .on('mouseover', function(d) { // mouse over a path
            //QDR.log.debug("showing connections form");
            var resultIndex = 0; // the connection to use
            var left = d.left ? d.target : d.source;
            // right is the node that the arrow points to, left is the other node
            var right = d.left ? d.source : d.target;
            var onode = QDRService.topology.nodeInfo()[left.key];
            // loop through all the connections for left, and find the one for right
            if (!onode || !onode['.connection'])
              return;
            // update the info dialog for the link the mouse is over
            if (!selected_node && !selected_link) {
              for (resultIndex = 0; resultIndex < onode['.connection'].results.length; ++resultIndex) {
                var conn = onode['.connection'].results[resultIndex];
                /// find the connection whose container is the right's name
                var name = QDRService.valFor(onode['.connection'].attributeNames, conn, "container");
                if (name == right.routerId) {
                  break;
                }
              }
              // did not find connection. this is a connection to a non-interrouter node
              if (resultIndex === onode['.connection'].results.length) {
                // use the non-interrouter node's connection info
                left = d.target;
                resultIndex = left.resultIndex;
              }
              if (resultIndex)
                updateForm(left.key, 'connection', resultIndex);
            }

            mousedown_link = d;
            selected_link = mousedown_link;
            restart();
          })
          .on('mouseout', function(d) { // mouse out of a path
            //QDR.log.debug("showing connections form");
            selected_link = null;
            restart();
          })
          // left click a path
          .on("click", function (d) {
            var clickPos = d3.mouse(this);
            d3.event.stopPropagation();
            clearPopups();
            var showCrossSection = function() {
              var diameter = 400;
              var format = d3.format(",d");
              var pack = d3.layout.pack()
                  .size([diameter - 4, diameter - 4])
                  .padding(-10)
                  .value(function(d) { return d.size; });

              d3.select("#crosssection svg").remove();
              var svg = d3.select("#crosssection").append("svg")
                  .attr("width", diameter)
                  .attr("height", diameter)
              var svgg = svg.append("g")
                  .attr("transform", "translate(2,2)");

              var root = {
                name: " Links between " + d.source.name + " and " + d.target.name,
                children: []
              }
              var nodeInfo = QDRService.topology.nodeInfo();
              var connections = nodeInfo[d.source.key]['.connection'];
              var containerIndex = connections.attributeNames.indexOf('container');
              connections.results.some ( function (connection) {
                if (connection[containerIndex] == d.target.routerId) {
                  root.attributeNames = connections.attributeNames;
                  root.obj = connection;
                  root.desc = "Connection";
                  return true;    // stop looping after 1 match
                }
                return false;
              })

              // find router.links where link.remoteContainer is d.source.name
              var links = nodeInfo[d.source.key]['.router.link'];
              var identityIndex = connections.attributeNames.indexOf('identity')
              var roleIndex = connections.attributeNames.indexOf('role')
              var connectionIdIndex = links.attributeNames.indexOf('connectionId');
              var linkTypeIndex = links.attributeNames.indexOf('linkType');
              var nameIndex = links.attributeNames.indexOf('name');
              var linkDirIndex = links.attributeNames.indexOf('linkDir');

              if (roleIndex < 0 || identityIndex < 0 || connectionIdIndex < 0
                || linkTypeIndex < 0 || nameIndex < 0 || linkDirIndex < 0)
                return;
              links.results.forEach ( function (link) {
                if (root.obj && link[connectionIdIndex] == root.obj[identityIndex] && link[linkTypeIndex] == root.obj[roleIndex])
                  root.children.push (
                    { name: " " + link[linkDirIndex] + " ",
                    size: 100,
                    obj: link,
                    desc: "Link",
                    attributeNames: links.attributeNames
                  })
              })
              if (root.children.length == 0)
                return;
              var node = svgg.datum(root).selectAll(".node")
                .data(pack.nodes)
                .enter().append("g")
                .attr("class", function(d) { return d.children ? "parent node hastip" : "leaf node hastip"; })
                .attr("transform", function(d) { return "translate(" + d.x + "," + d.y + ")" + (!d.children ? "scale(0.9)" : ""); })
                .attr("title", function (d) {
                  var title = "<h4>" + d.desc + "</h4><table class='tiptable'><tbody>";
                  if (d.attributeNames)
                    d.attributeNames.forEach( function (n, i) {
                      title += "<tr><td>" + n + "</td><td>";
                      title += d.obj[i] != null ? d.obj[i] : '';
                      title += '</td></tr>';
                    })
                  title += "</tbody></table>"
                  return title
                })
              node.append("circle")
                .attr("r", function(d) { return d.r; });

  //          node.filter(function(d) { return !d.children; }).append("text")
              node.append("text")
                .attr("dy", function (d) { return d.children ? "-10em" : ".5em"})
                .style("text-anchor", "middle")
                .text(function(d) {
                    return d.name.substring(0, d.r / 3);
                });
              $('.hastip').tooltipsy({ alignTo: 'cursor'});
              svgg.attr("transform", "translate(2,2) scale(0.01)")

              var bounds = $("#topology").position()
              d3.select("#crosssection")
                .style("display", "block")
                .style("left", (clickPos[0] + bounds.left) + "px")
                .style("top", (clickPos[1] + bounds.top) + "px")

              svgg.transition()
                .attr("transform", "translate(2,2) scale(1)")
                .each("end", function ()  {
                  d3.selectAll("#crosssection g.leaf text").attr("dy", ".3em")
                })
            }
            QDRService.ensureEntities(d.source.key, {entity: '.router.link', force: true}, showCrossSection)
          })
        // remove old links
        path.exit().remove();


        // circle (node) group
        // nodes are known by id
        circle = circle.data(nodes, function(d) {
          return d.name;
        });

        // update existing nodes visual states
        circle.selectAll('circle')
          .classed('highlighted', function(d) {
            return d.highlighted;
          })
          .classed('selected', function(d) {
            return (d === selected_node)
          })
          .classed('fixed', function(d) {
            return d.fixed & 1
          })

        // add new circle nodes. if nodes[] is longer than the existing paths, add a new path for each new element
        var g = circle.enter().append('svg:g')
          .classed('multiple', function(d) {
            return (d.normals && d.normals.length > 1)
          })

        var appendCircle = function(g) {
          // add new circles and set their attr/class/behavior
          return g.append('svg:circle')
            .attr('class', 'node')
            .attr('r', function(d) {
              return radii[d.nodeType]
            })
            .attr('fill', function (d) {
              if (d.cdir === 'both' && !QDRService.isConsole(d)) {
                return 'url(' + urlPrefix + '#half-circle)'
              }
              return null;
            })
            .classed('fixed', function(d) {
              return d.fixed & 1
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
            .classed('inout', function(d) {
              return d.cdir == 'both'
            })
            .classed('inter-router', function(d) {
              return d.nodeType == 'inter-router'
            })
            .classed('on-demand', function(d) {
              return d.nodeType == 'on-demand'
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
        appendCircle(g)
          .on('mouseover', function(d) {  // mouseover a circle
            if (!selected_node && !mousedown_node) {
              if (d.nodeType === 'inter-router') {
                //QDR.log.debug("showing general form");
                updateForm(d.key, 'router', 0);
              } else if (d.nodeType === 'normal' || d.nodeType === 'on-demand' || d.nodeType === 'route-container') {
                //QDR.log.debug("showing connections form");
                updateForm(d.key, 'connection', d.resultIndex);
              }
            }

            if (d === mousedown_node)
              return;
            //if (d === selected_node)
            //    return;
            // enlarge target node
            d3.select(this).attr('transform', 'scale(1.1)');
            // highlight the next-hop route from the selected node to this node
            //mousedown_node = null;

            if (!selected_node) {
              return;
            }
            clerAllHighlights()
            // we need .router.node info to highlight hops
            QDRService.ensureAllEntities([{entity: ".router.node", attrs: ["id","nextHop"]}], function () {
              mouseover_node = d  // save this node in case the topology changes so we can restore the highlights
              nextHop(selected_node, d);
              restart();
            })
          })
          .on('mouseout', function(d) { // mouse out for a circle
            // unenlarge target node
            d3.select(this).attr('transform', '');
            clerAllHighlights()
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

            var mySvg = this.parentNode.parentNode.parentNode;
            // if we dragged the node, make it fixed
            var cur_mouse = d3.mouse(mySvg);
            if (cur_mouse[0] != initial_mouse_down_position[0] ||
              cur_mouse[1] != initial_mouse_down_position[1]) {
              d.fixed = true;
              setNodesFixed(d.name, true)
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
            clerAllHighlights()
            mousedown_node = null;
            if (!$scope.$$phase) $scope.$apply()
            restart(false);

          })
          .on("dblclick", function(d) { // circle
            if (d.fixed) {
              d.fixed = false
              setNodesFixed(d.name, false)
              restart() // redraw the node without a dashed line
              force.start(); // let the nodes move to a new position
            }
            if (QDRService.nameFromId(d.key) == '__internal__') {
              editNode();
              if (!$scope.$$phase) $scope.$apply()
            }
          })
          .on("contextmenu", function(d) {  // circle
            $(document).click();
            d3.event.preventDefault();
            $scope.contextNode = d
            var rm = relativeMouse()
            d3.select('#node_context_menu')
              .style('left', rm.left + "px")
              .style('top', (rm.top - rm.offset.top) + "px")
              .style('display', 'block');
            if (!$scope.$$phase) $scope.$apply()
          })
          .on("click", function(d) {  // circle
            if (!mouseup_node)
              return;
            // clicked on a circle
            clearPopups();
            if (!d.normals) {
              // circle was a router or a broker
              if (QDRService.isArtemis(d)) {
                var artemisPath = '/jmx/attributes?tab=artemis&con=Artemis'
                if (QDR.isStandalone)
                  window.location = $location.protocol() + '://localhost:8161/hawtio' + artemisPath
                else
                  $location.path(artemisPath)
              }
              return;
            }
            clickPos = d3.mouse(this);
            d3.event.stopPropagation();
            startUpdateConnectionsGrid(d);
          })
        //.attr("transform", function (d) {return "scale(" + (d.nodeType === 'normal' ? .5 : 1) + ")"})
        //.transition().duration(function (d) {return d.nodeType === 'normal' ? 3000 : 0}).ease("elastic").attr("transform", "scale(1)")

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
              else if (d.nodeType === 'route-container')
                y = 5;
              return y;
            })
            .attr('class', 'id')
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
              } else if (d.nodeType === 'route-container') {
                return d.properties.product ? d.properties.product[0].toUpperCase() : 'S'
              } else if (d.nodeType === 'normal')
                return '\uf109'; // icon-laptop for clients
              return d.name.length > 7 ? d.name.substr(0, 6) + '...' : d.name;
            });
        }

        appendContent(g)

        var appendTitle = function(g) {
          g.append("svg:title").text(function(d) {
            var x = '';
            if (d.normals && d.normals.length > 1)
              x = " x " + d.normals.length;
            if (QDRService.isConsole(d)) {
              return 'Dispatch console' + x
            } else if (QDRService.isArtemis(d)) {
              return 'Broker - Artemis' + x
            } else if (d.properties.product == 'qpid-cpp') {
              return 'Broker - qpid-cpp' + x
            } else if (d.properties.product) {
              return d.properties.product
            } else if (d.cdir === 'in')
              return 'Sender' + x
            else if (d.cdir === 'out')
              return 'Receiver' + x
            else if (d.cdir === 'both')
              return 'Sender/Receiver' + x
            return d.nodeType == 'normal' ? 'client' + x : (d.nodeType == 'on-demand' ? 'broker' : 'Router ' + d.name)
          })
        }
        appendTitle(g);

        // remove old nodes
        circle.exit().remove();

        // add subcircles
        svg.selectAll('.subcircle').remove();
        var multiples = svg.selectAll('.multiple')
        multiples.each(function(d) {
          d.normals.forEach(function(n, i) {
            if (i < d.normals.length - 1 && i < 3) // only show a few shadow circles
              this.insert('svg:circle', ":first-child")
              .attr('class', 'subcircle node')
              .attr('r', 15 - i)
              .attr('transform', "translate(" + 4 * (i + 1) + ", 0)")
          }, d3.select(this))
        })

        // dynamically create the legend based on which node types are present
        // the legend
        d3.select("#svg_legend svg").remove();
        lsvg = d3.select("#svg_legend")
          .append('svg')
          .attr('id', 'svglegend')
        lsvg = lsvg.append('svg:g')
          .attr('transform', 'translate(' + (radii['inter-router'] + 2) + ',' + (radii['inter-router'] + 2) + ')')
          .selectAll('g');
        var legendNodes = [];
        legendNodes.push(aNode("Router", "", "inter-router", '', undefined, 0, 0, 0, 0, false, {}))

        if (!svg.selectAll('circle.console').empty()) {
          legendNodes.push(aNode("Console", "", "normal", '', undefined, 1, 0, 0, 0, false, {
            console_identifier: 'Dispatch console'
          }))
        }
        if (!svg.selectAll('circle.client.in').empty()) {
          var node = aNode("Sender", "", "normal", '', undefined, 2, 0, 0, 0, false, {})
          node.cdir = "in"
          legendNodes.push(node)
        }
        if (!svg.selectAll('circle.client.out').empty()) {
          var node = aNode("Receiver", "", "normal", '', undefined, 3, 0, 0, 0, false, {})
          node.cdir = "out"
          legendNodes.push(node)
        }
        if (!svg.selectAll('circle.client.inout').empty()) {
          var node = aNode("Sender/Receiver", "", "normal", '', undefined, 4, 0, 0, 0, false, {})
          node.cdir = "both"
          legendNodes.push(node)
        }
        if (!svg.selectAll('circle.qpid-cpp').empty()) {
          legendNodes.push(aNode("Qpid broker", "", "route-container", '', undefined, 5, 0, 0, 0, false, {
            product: 'qpid-cpp'
          }))
        }
        if (!svg.selectAll('circle.artemis').empty()) {
          legendNodes.push(aNode("Artemis broker", "", "route-container", '', undefined, 6, 0, 0, 0, false,
          {product: 'apache-activemq-artemis'}))
        }
        if (!svg.selectAll('circle.route-container').empty()) {
          legendNodes.push(aNode("Service", "", "route-container", 'external-service', undefined, 7, 0, 0, 0, false,
          {product: ' External Service'}))
        }
        lsvg = lsvg.data(legendNodes, function(d) {
          return d.key;
        });
        var lg = lsvg.enter().append('svg:g')
          .attr('transform', function(d, i) {
            // 45px between lines and add 10px space after 1st line
            return "translate(0, " + (45 * i + (i > 0 ? 10 : 0)) + ")"
          })

        appendCircle(lg)
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

        if (!mousedown_node || !selected_node)
          return;

        if (!start)
          return;
        // set the graph in motion
        //QDR.log.debug("mousedown_node is " + mousedown_node);
        force.start();

      }

      var startUpdateConnectionsGrid = function(d) {
        // called after each topology update
        var extendConnections = function() {
          // force a fetch of the links for this node
          QDRService.ensureEntities(d.key, {entity: ".router.link", force: true}, function () {
            // the links for this node are now available
            $scope.multiData = []
            var normals = d.normals;
            // find updated normals for d
            d3.selectAll('.normal')
              .each(function(newd) {
                if (newd.id == d.id && newd.name == d.name) {
                  normals = newd.normals;
                }
              });
            if (normals) {
              normals.forEach(function(n) {
                var nodeInfo = QDRService.topology.nodeInfo();
                var links = nodeInfo[n.key]['.router.link'];
                var linkTypeIndex = links.attributeNames.indexOf('linkType');
                var connectionIdIndex = links.attributeNames.indexOf('connectionId');
                n.linkData = [];
                links.results.forEach(function(link) {
                  if (link[linkTypeIndex] === 'endpoint' && link[connectionIdIndex] === n.connectionId) {
                    var l = {};
                    var ll = QDRService.flatten(links.attributeNames, link)
                    l.owningAddr = ll.owningAddr
                    l.dir = ll.linkDir
                    if (l.owningAddr && l.owningAddr.length > 2)
                      if (l.owningAddr[0] === 'M')
                        l.owningAddr = l.owningAddr.substr(2)
                      else
                        l.owningAddr = l.owningAddr.substr(1)

                    l.deliveryCount = ll.deliveryCount
                    l.uncounts = QDRService.pretty(ll.undeliveredCount + ll.unsettledCount)
                    l.adminStatus = ll.adminStatus;
                    l.operStatus = ll.operStatus;
                    l.identity = ll.identity
                    l.connectionId = ll.connectionId
                    l.nodeId = n.key
                    l.type = ll.type
                    l.name = ll.name

                    // TODO: remove this fake quiescing/reviving logic when the routers do the work
                    initConnState(n.connectionId)
                    if ($scope.quiesceState[n.connectionId].linkStates[l.identity])
                      l.adminStatus = $scope.quiesceState[n.connectionId].linkStates[l.identity];
                    if ($scope.quiesceState[n.connectionId].state == 'quiescing') {
                      if (l.adminStatus === 'enabled') {
                        // 25% chance of switching
                        var chance = Math.floor(Math.random() * 2);
                        if (chance == 1) {
                          l.adminStatus = 'disabled';
                          $scope.quiesceState[n.connectionId].linkStates[l.identity] = 'disabled';
                        }
                      }
                    }
                    if ($scope.quiesceState[n.connectionId].state == 'reviving') {
                      if (l.adminStatus === 'disabled') {
                        // 25% chance of switching
                        var chance = Math.floor(Math.random() * 2);
                        if (chance == 1) {
                          l.adminStatus = 'enabled';
                          $scope.quiesceState[n.connectionId].linkStates[l.identity] = 'enabled';
                        }
                      }
                    }
                    QDR.log.debug("pushing link state for " + l.owningAddr + " status: " + l.adminStatus)

                    n.linkData.push(l)
                  }
                })
                $scope.multiData.push(n)
                if (n.connectionId == $scope.connectionId)
                  $scope.linkData = n.linkData;
                initConnState(n.connectionId)
                $scope.multiDetails.updateState(n)
              })
            }
            $scope.$apply();

            d3.select('#multiple_details')
              .style({
                height: ((normals.length + 1) * 30) + 40 + "px",
                'overflow-y': normals.length > 10 ? 'scroll' : 'hidden'
              })
          })
        }
        // register a notification function for when the topology is updated
        QDRService.addUpdatedAction("normalsStats", extendConnections)
        // call the function that gets the links right now
        extendConnections();
        clearPopups();
        var display = 'block'
        var left = mouseX + $(document).scrollLeft()
        if (d.normals.length === 1) {
          display = 'none'
          left = left - 30;
          mouseY = mouseY - 20
        }
        var rm = relativeMouse()
        d3.select('#multiple_details')
          .style({
            display: display,
            opacity: 1,
            left: rm.left + "px",
            top: rm.top + "px"
          })
        if (d.normals.length === 1) {
          // simulate a click on the connection to popup the link details
          QDRService.ensureEntities(d.key, {entity: ".router.link", force: true}, function () {
            $scope.multiDetails.showLinksList({
              entity: d
            })
          })
        }
      }
      var stopUpdateConnectionsGrid = function() {
        QDRService.delUpdatedAction("normalsStats");
      }

      var initConnState = function(id) {
        if (!angular.isDefined($scope.quiesceState[id])) {
          $scope.quiesceState[id] = {
            state: 'enabled',
            buttonText: 'Quiesce',
            buttonDisabled: false,
            linkStates: {}
          }
        }
      }

      function nextHop(thisNode, d) {
        if ((thisNode) && (thisNode != d)) {
          var target = findNextHopNode(thisNode, d);
          //QDR.log.debug("highlight link from node ");
          //console.dump(nodeFor(selected_node.name));
          //console.dump(target);
          if (target) {
            var hnode = nodeFor(thisNode.name)
            var hlLink = linkFor(hnode, target);
            //QDR.log.debug("need to highlight");
            //console.dump(hlLink);
            if (hlLink) {
              hlLink['highlighted'] = true;
              hnode['highlighted'] = true
            }
            else
              target = null;
          }
          nextHop(target, d);
        }
        if (thisNode == d) {
          var hnode = nodeFor(thisNode.name)
          hnode['highlighted'] = true
        }
      }


      function mousedown() {
        // prevent I-bar on drag
        //d3.event.preventDefault();

        // because :active only works in WebKit?
        svg.classed('active', true);
      }

      function hasChanged() {
        // Don't update the underlying topology diagram if we are adding a new node.
        // Once adding is completed, the topology will update automatically if it has changed
        var nodeInfo = QDRService.topology.nodeInfo();
        if (Object.keys(nodeInfo).length != Object.keys(savedKeys).length)
          return Object.keys(nodeInfo).length > Object.keys(savedKeys).length ? 1 : -1;
        // we may have dropped a node and added a different node in the same update cycle
        for (var key in nodeInfo) {
          // if this node isn't in the saved node list
          if (!savedKeys.hasOwnProperty(key))
            return 1;
          // if the number of connections for this node chaanged
          if (nodeInfo[key]['.connection'].results.length != savedKeys[key]) {
            return -1;
          }
        }
        return 0;
      };

      function saveChanged() {
        savedKeys = {};
        var nodeInfo = QDRService.topology.nodeInfo();
        // save the number of connections per node
        for (var key in nodeInfo) {
          if (nodeInfo[key]['.connection'])
            savedKeys[key] = nodeInfo[key]['.connection'].results.length;
        }
        //QDR.log.debug("saving current keys");
        //console.dump(savedKeys);
      };
      // we are about to leave the page, save the node positions
      $rootScope.$on('$locationChangeStart', function(event, newUrl, oldUrl) {
        //QDR.log.debug("locationChangeStart");
        savePositions()
      });
      // When the DOM element is removed from the page,
      // AngularJS will trigger the $destroy event on
      // the scope
      $scope.$on("$destroy", function(event) {
        //QDR.log.debug("scope on destroy");
        savePositions();
        QDRService.setUpdateEntities([])
        QDRService.stopUpdating();
        QDRService.delUpdatedAction("normalsStats");
        QDRService.delUpdatedAction("topology");
        QDRService.delDisconnectAction( onDisconnect )

        d3.select("#SVG_ID").remove();
        window.removeEventListener('resize', resize);
      });

      function handleInitialUpdate() {
        // we only need to update connections during steady-state
        QDRService.setUpdateEntities([".connection"])
        // we currently have all entities available on all routers
        saveChanged();
        animate = true;
        initForceGraph();
        // after the graph is displayed fetch all .router.node info. This is done so highlighting between nodes
        // doesn't incur a delay
        QDRService.ensureAllEntities([{entity: ".router.node", attrs: ["id","nextHop"]}], function () {})
        // call this function every time a background update is done
        QDRService.addUpdatedAction("topology", function() {
          var changed = hasChanged()
          // there is a new node, we need to get all of it's entities before drawing the graph
          if (changed > 0) {
            QDRService.delUpdatedAction("topology")
            setupInitialUpdate()
          } else if (changed === -1) {
            // we lost a node (or a client), we can draw the new svg immediately
            saveChanged();
            var nodeInfo = QDRService.topology.nodeInfo();
            initializeNodes(nodeInfo)

            var unknowns = []
            initializeLinks(nodeInfo, unknowns)
            if (unknowns.length > 0) {
              resolveUnknowns(nodeInfo, unknowns)
            }
            else {
              force.nodes(nodes).links(links).start();
              animate = true;
              restart();
            }

            //initForceGraph();
          } else {
            //QDR.log.debug("topology didn't change")
          }

        })
      }

      function setupInitialUpdate() {
        // make sure all router nodes have .connection info. if not then fetch any missing info
        QDRService.ensureAllEntities(
//          [{entity: ".connection"}, {entity: ".router.lin.router.link", attrs: ["linkType","connectionId","linkDir"]}],
          [{entity: ".connection"}],
          //[{entity: ".connection"}],
            handleInitialUpdate)
      }
      setupInitialUpdate();
      QDRService.startUpdating();

    }
  ]);

  return QDR;

}(QDR || {}));

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
var QDR = (function (QDR) {
  'use strict';

  angular
    .module('horizon.dashboard.dispatch.topology')
    .controller('horizon.dashboard.dispatch.topology.TopologyController', TopologyController);

  TopologyController.$inject = [
    '$scope',
    '$rootScope',
    'horizon.dashboard.dispatch.comService',
    '$location',
    '$timeout',
    '$modal',
  ]

  var mouseX, mouseY;
  var dontHide = false;
  function hideLinkDetails() {
    d3.select("#link_details").transition()
      .duration(500)
      .style("opacity", 0)
      .each("end", function (d) {
          d3.select("#link_details").style("visibility", "hidden")
      })
  }

  function TopologyController(
    $scope,
    $rootScope,
    QDRService,
    $location,
    $timeout,
    $modal) {

    var ctrl = this;
    QDRService.addConnectAction( function () {
      Topology($scope, $rootScope, QDRService, $location, $timeout, $modal)
    })
    QDRService.loadConnectOptions(QDRService.connect);

    $scope.multiData = []
    $scope.multiDetails = {
      data: 'multiData',
      enableRowSelection: true,
      enableRowHeaderSelection: false,
      multiSelect: false,
      enableColumnResize: true,
      enableColumnReordering: true,
      enableVerticalScrollbar: 0,
      enableHorizontalScrollbar: 0,
      onRegisterApi: function(gridApi){
        gridApi.selection.on.rowSelectionChanged($scope, function(row){
          var detailsDiv = d3.select('#link_details')
          var isVis = detailsDiv.style('visibility') === 'visible';
          if (!dontHide && isVis && $scope.connectionId === row.entity.connectionId) {
            hideLinkDetails();
            return;
          }
          dontHide = false;
          $scope.multiDetails.showLinksList(row)
        });
      },
      showLinksList: function (obj) {
        $scope.linkData = obj.entity.linkData;
        $scope.connectionId = obj.entity.connectionId;
        var visibleLen = Math.min(obj.entity.linkData.length, 10)
        var left = parseInt(d3.select('#multiple_details').style("left"))
        var bounds = $("#topology").position()
        var detailsDiv = d3.select('#link_details')
        detailsDiv
          .style({
            visibility: 'visible',
            opacity: 1,
            left: (left + 20) + "px",
            top:  (mouseY + 40 - bounds.top + $(document).scrollTop()) + "px",
            height: ((visibleLen + 1) * 30) + 40 + "px", // +1 for the header row
            'overflow-y': obj.entity.linkData > 10 ? 'scroll' : 'hidden'})
      },
      columnDefs: [
      {
        field: 'host',
        displayName: 'Connection host'
      },
      {
        field: 'user',
        displayName: 'User'
      },
      {
        field: 'properties',
        displayName: 'Properties'
      },
/*
      {
        cellClass: 'gridCellButton',
        cellTemplate: '<button title="{{quiesceText(row)}} the links" type="button" ng-class="quiesceClass(row)" class="btn" ng-click="$event.stopPropagation();quiesceConnection(row)" ng-disabled="quiesceDisabled(row)">{{quiesceText(row)}}</button>'
      },
*/
      ]
    };
    $scope.linkData = [];
    $scope.linkDetails = {
      data: 'linkData',
      enableRowSelection: true,
      enableRowHeaderSelection: false,
      multiSelect: false,
      enableColumnResize: true,
      enableColumnReordering: true,
      enableVerticalScrollbar: 0,
      enableHorizontalScrollbar: 0,
      columnDefs: [
      {
        field: 'adminStatus',
        displayName: 'Admin state'
      },
      {
        field: 'operStatus',
        displayName: 'Oper state'
      },
      {
        field: 'dir',
        displayName: 'dir'
      },
      {
        field: 'owningAddr',
        displayName: 'Address'
      },
      {
        field: 'deliveryCount',
        displayName: 'Delivered',
        cellClass: 'grid-values'

      },
      {
        field: 'uncounts',
        displayName: 'Outstanding',
        cellClass: 'grid-values'
      }/*,
      {
        cellClass: 'gridCellButton',
        cellTemplate: '<button title="{{quiesceLinkText(row)}} this link" type="button" ng-class="quiesceLinkClass(row)" class="btn" ng-click="quiesceLink(row)" ng-disabled="quiesceLinkDisabled(row)">{{quiesceLinkText(row)}}</button>'
      }*/
      ]
    }
  }

  function Topology(
    $scope,
    $rootScope,
    QDRService,
    $location,
    $timeout,
    $modal) {

    $scope.quiesceState = {}
    $scope.quiesceConnection = function (row) {
      // call method to set adminStatus
    }
    $scope.quiesceDisabled = function (row) {
      return false;
    }
    $scope.quiesceText = function (row) {
      return 'Quiesce'
    }
    $scope.quiesceClass = function (row) {
      var stateClassMap = {
        enabled: 'btn-primary',
        quiescing: 'btn-warning',
        reviving: 'btn-warning',
        quiesced: 'btn-danger'
      }
      return 'btn-primary'
    }
    $scope.quiesceLinkClass = function (row) {
      var stateClassMap = {
        enabled: 'btn-primary',
        disabled: 'btn-danger'
      }
      return stateClassMap[row.entity.adminStatus]
    }
    $scope.quiesceLink = function (row) {
      QDRService.quiesceLink(row.entity.nodeId, row.entity.name);
    }
    $scope.quiesceLinkDisabled = function (row) {
      return (row.entity.operStatus !== 'up' && row.entity.operStatus !== 'down')
    }
    $scope.quiesceLinkText = function (row) {
      return row.entity.operStatus === 'down' ? "Revive" : "Quiesce";
    }

    // we are currently connected. setup a handler to get notified if we are ever disconnected
    QDRService.addDisconnectAction( function () {
      QDR.log.debug("disconnected from router. show a toast message");
    })

    var urlPrefix = $location.absUrl();
    urlPrefix = urlPrefix.split("#")[0]
    QDR.log.debug("started QDR.TopologyController with urlPrefix: " + urlPrefix);

    $scope.addingNode = {
      step: 0,
      hasLink: false,
      trigger: ''
    };

    $scope.cancel = function () {
      $scope.addingNode.step = 0;
    }
    $scope.editNewRouter = function () {
      $scope.addingNode.trigger = 'editNode';
    }

    var NewRouterName = "__NEW__";
      // mouse event vars
    var selected_node = null,
      selected_link = null,
      mousedown_link = null,
      mousedown_node = null,
      mouseup_node = null,
      initial_mouse_down_position = null;

    $scope.schema = "Not connected";

    $scope.modes = [
      {title: 'Topology view', name: 'Diagram', right: false},
      /* {title: 'Add a new router node', name: 'Add Router', right: true} */
    ];
    $scope.mode = "Diagram";
    $scope.contextNode = null; // node that is associated with the current context menu

    $scope.isModeActive = function (name) {
      if ((name == 'Add Router' || name == 'Diagram') && $scope.addingNode.step > 0)
        return true;
      return ($scope.mode == name);
    }
    $scope.selectMode = function (name) {
      if (name == "Add Router") {
        name = 'Diagram';
        if ($scope.addingNode.step > 0) {
          $scope.addingNode.step = 0;
        } else {
          // start adding node mode
          $scope.addingNode.step = 1;
        }
      } else {
        $scope.addingNode.step = 0;
      }

      $scope.mode = name;
    }
    $scope.$watch(function () {return $scope.addingNode.step}, function (newValue, oldValue) {
      if (newValue == 0 && oldValue != 0) {
        // we are cancelling the add

        // find the New node
        nodes.every(function (n, i) {
          // for the placeholder node, the key will be __internal__
          if (QDRService.nameFromId(n.key) == '__internal__') {
            var newLinks = links.filter(function (e, i) {
              return e.source.id == n.id || e.target.id == n.id;
            })
            // newLinks is an array of links to remove
            newLinks.map(function (e) {
              links.splice(links.indexOf(e), 1);
            })
            // i is the index of the node to remove
            nodes.splice(i, 1);
            force.nodes(nodes).links(links).start();
                    restart(false);
            return false; // stop looping
          }
          return true;
        })
        updateForm(Object.keys(QDRService.topology.nodeInfo())[0], 'router', 0);

      } else if (newValue > 0) {
        // we are starting the add mode
        $scope.$broadcast('showAddForm')

        resetMouseVars();
        selected_node = null;
        selected_link = null;
        // add a new node
        var id = "amqp:/_topo/0/__internal__/$management";
        var x = radiusNormal * 4;
        var y = x;;
        if (newValue > 1) {   // add at current mouse position
          var offset = jQuery('#topology').offset();
          x = mouseX - offset.left + $(document).scrollLeft();
          y = mouseY - offset.top + $(document).scrollTop();;
        }
        NewRouterName = genNewName();
        nodes.push( aNode(id, NewRouterName, "inter-router", undefined, nodes.length, x, y, undefined, true) );
        force.nodes(nodes).links(links).start();
        restart(false);
      }
    })

    $scope.isRight = function (mode) {
      return mode.right;
    }

    // for ng-grid that shows details for multiple consoles/clients
    // generate unique name for router and containerName
    var genNewName = function () {
      var nodeInfo = QDRService.topology.nodeInfo();
      var nameIndex = 1;
      var newName = "R." + nameIndex;

      var names = [];
      for (var key in nodeInfo) {
        var node = nodeInfo[key];
        var router = node['.router'];
        var attrNames = router.attributeNames;
        var name = QDRService.valFor(attrNames, router.results[0], 'routerId')
        if (!name)
          name = QDRService.valFor(attrNames, router.results[0], 'name')
        names.push(name);
      }

      while (names.indexOf(newName) >= 0) {
        newName = "R." + nameIndex++;
      }
      return newName;
    }

    $scope.$watch(function () {return $scope.addingNode.trigger}, function (newValue, oldValue) {
      if (newValue == 'editNode') {
        $scope.addingNode.trigger = "";
        editNode();
      }
    })

    function editNode() {
      doAddDialog(NewRouterName);
    };
    $scope.reverseLink = function () {
      if (!mousedown_link)
        return;
      var d = mousedown_link;
      var tmp = d.left;
      d.left = d.right;;
      d.right = tmp;
        restart(false);
        tick();
    }
    $scope.removeLink = function () {
      if (!mousedown_link)
        return;
      var d = mousedown_link;
       links.every( function (l, i) {
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
    $scope.setFixed = function (b) {
      if ($scope.contextNode) {
        $scope.contextNode.fixed = b;
      }
      restart();
    }
    $scope.isFixed = function () {
      if (!$scope.contextNode)
        return false;
      return ($scope.contextNode.fixed & 0b1);
    }

    // event handlers for popup context menu
    $(document).mousemove(function (e) {
        mouseX = e.clientX;
        mouseY = e.clientY;
        //console.log("("+mouseX+"," + mouseY+")")
    });
    $(document).mousemove();
    $(document).click(function (e) {
      $scope.contextNode = null;
      $(".contextMenu").fadeOut(200);
    });

    // set up SVG for D3
    var colors = {'inter-router': "#EAEAEA", 'normal': "#F0F000", 'on-demand': '#00F000'};
    var radii = {'inter-router': 25, 'normal': 15, 'on-demand': 15};
    var radius = 25;
    var radiusNormal = 15;
    var svg, lsvg;
    var force;
    var animate = false; // should the force graph organize itself when it is displayed
    var path, circle;
    var savedKeys = {};
    var width = 0;
    var height = 0;

    var getSizes = function () {
      var legendWidth = 196;
      var gap = 5;
      var width = $('.qdrTopology').width() - gap - legendWidth;
      var top = $('#topology').offset().top
      var tpformHeight = $('#topologyForm').height()
      var height = window.innerHeight - tpformHeight - top - gap;
      if (height < 400)
        height = 400;
/*
      QDR.log.debug("window.innerHeight:" + window.innerHeight +
        " tpformHeight:" + tpformHeight +
        " top:" + top +
        " gap:" + gap +
        " width:" + width +
        " height:" + height)
*/
      if (width < 10 || height < 30) {
        QDR.log.info("page width and height are abnormal w:" + width + " height:" + height)
        return [0,0];
      }
      return [width, height]
    }
    var resize = function () {
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
    height = 300
    if (width <= 0 || height <= 0)
      return

      // set up initial nodes and links
      //  - nodes are known by 'id', not by index in array.
      //  - selected edges are indicated on the node (as a bold red circle).
      //  - links are always source < target; edge directions are set by 'left' and 'right'.
    var nodes = [];
    var links = [];

    var aNode = function (id, name, nodeType, nodeInfo, nodeIndex, x, y, resultIndex, fixed, properties) {
      properties = properties || {};
      var routerId;
      if (nodeInfo) {
        var node = nodeInfo[id];
        if (node) {
          var router = node['.router'];
          routerId = QDRService.valFor(router.attributeNames, router.results[0], 'id')
          if (!routerId)
            routerId = QDRService.valFor(router.attributeNames, router.results[0], 'routerId')
        }
      }
      return {   key: id,
        name: name,
        nodeType: nodeType,
        properties: properties,
        routerId: routerId,
        x: x,
        y: y,
        id: nodeIndex,
        resultIndex: resultIndex,
        fixed: fixed,
        cls: name == NewRouterName ? 'temp' : ''
      };
    };


        var initForm = function (attributes, results, entityType, formFields) {

            while(formFields.length > 0) {
                // remove all existing attributes
                    formFields.pop();
            }

            for (var i=0; i<attributes.length; ++i) {
                var name = attributes[i];
                var val = results[i];
                var desc = "";
                if (entityType.attributes[name])
                    if (entityType.attributes[name].description)
                        desc = entityType.attributes[name].description;

                formFields.push({'attributeName': name, 'attributeValue': val, 'description': desc});
            }
        }

    // initialize the nodes and links array from the QDRService.topology._nodeInfo object
    var initForceGraph = function () {
      nodes = [];
      links = [];

      svg = d3.select('#topology')
        .append('svg')
        .attr("id", "SVG_ID")
        .attr('width', width)
        .attr('height', height)
        .on("contextmenu", function(d) {
          if (QDR.isHorizon)
            return;
          if (d3.event.defaultPrevented)
            return;
          d3.event.preventDefault();
          if ($scope.addingNode.step != 0)
            return;
          if (d3.select('#svg_context_menu').style('display') !== 'block')
            $(document).click();
          d3.select('#svg_context_menu')
            .style('left', (mouseX + $(document).scrollLeft()) + "px")
            .style('top', (mouseY + $(document).scrollTop()) + "px")
            .style('display', 'block');
        })
        .on('click', function (d) {
          removeCrosssection()
        });

      $(document).keyup(function(e) {
        if (e.keyCode === 27) {
          removeCrosssection()
        }
      });

      // the legend
      lsvg = d3.select("#svg_legend")
         .append('svg')
        .attr('id', 'svglegend')
      lsvg = lsvg.append('svg:g')
        .attr('transform', 'translate('+(radii['inter-router']+2)+','+(radii['inter-router']+2)+')')
        .selectAll('g');

      // mouse event vars
      selected_node = null;
      selected_link = null;
      mousedown_link = null;
      mousedown_node = null;
      mouseup_node = null;

      // initialize the list of nodes
      var yInit = 10;
      var nodeInfo = QDRService.topology.nodeInfo();
      var nodeCount = Object.keys(nodeInfo).length;
      for (var id in nodeInfo) {
        var name = QDRService.nameFromId(id);
                // if we have any new nodes, animate the force graph to position them
        var position = angular.fromJson(localStorage[name]);
        if (!angular.isDefined(position)) {
            animate = true;
            position = {x: width / 4 + ((width / 2)/nodeCount) * nodes.length,
                        y: 200 + yInit,
                        fixed: false};
        }
        if (position.y > height)
          position.y = 200 - yInit;
        nodes.push( aNode(id, name, "inter-router", nodeInfo, nodes.length, position.x, position.y, undefined, position.fixed) );
        yInit *= -1;
        //QDR.log.debug("adding node " + nodes.length-1);
      }

      // initialize the list of links
      var source = 0;
      var client = 1;
      for (var id in nodeInfo) {
        var onode = nodeInfo[id];
        var conns = onode['.connection'].results;
        var attrs = onode['.connection'].attributeNames;
        var parent = getNodeIndex(QDRService.nameFromId(id));
        //QDR.log.debug("external client parent is " + parent);
        var normalsParent = {console: undefined, client: undefined}; // 1st normal node for this parent

        for (var j = 0; j < conns.length; j++) {
                    var role = QDRService.valFor(attrs, conns[j], "role");
                    var properties = QDRService.valFor(attrs, conns[j], "properties") || {};
                    var dir = QDRService.valFor(attrs, conns[j], "dir");
          if (role == "inter-router") {
            var connId = QDRService.valFor(attrs, conns[j], "container");
            var target = getContainerIndex(connId);
            if (target >= 0)
              getLink(source, target, dir);
          } else if (role == "normal" || role == "on-demand") {
            // not a router, but an external client
            //QDR.log.debug("found an external client for " + id);
            var name = QDRService.nameFromId(id) + "." + client;
            //QDR.log.debug("external client name is  " + name + " and the role is " + role);

                        // if we have any new clients, animate the force graph to position them
                        var position = angular.fromJson(localStorage[name]);
                        if (!angular.isDefined(position)) {
                            animate = true;
                            position = {x: nodes[parent].x + 40 + Math.sin(Math.PI/2 * client),
                                        y: nodes[parent].y + 40 + Math.cos(Math.PI/2 * client),
                                        fixed: false};
                        }
            if (position.y > height)
              position.y = nodes[parent].y + 40 + Math.cos(Math.PI/2 * client)
            var node = aNode(id, name, role, nodeInfo, nodes.length, position.x, position.y, j, position.fixed, properties)
            var nodeType = QDRService.isAConsole(properties, QDRService.valFor(attrs, conns[j], "identity"), role, node.key)

            if (role === 'normal') {
              node.user = QDRService.valFor(attrs, conns[j], "user")
              node.isEncrypted = QDRService.valFor(attrs, conns[j], "isEncrypted")
              node.host = QDRService.valFor(attrs, conns[j], "host")
              node.connectionId = QDRService.valFor(attrs, conns[j], "identity")

              if (!normalsParent[nodeType]) {
                normalsParent[nodeType] = node;
                nodes.push(  node );
                node.normals = [node];
                // now add a link
                getLink(parent, nodes.length-1, dir);
                client++;
              } else {
                normalsParent[nodeType].normals.push(node)
              }
            } else {
              nodes.push( node)
              // now add a link
              getLink(parent, nodes.length-1, dir);
              client++;
            }
          }
        }
        source++;
      }

            $scope.schema = QDRService.schema;
      // init D3 force layout
      force = d3.layout.force()
        .nodes(nodes)
        .links(links)
        .size([width, height])
        .linkDistance(function(d) { return d.target.nodeType === 'inter-router' ? 150 : 65 })
        .charge(-1800)
        .friction(.10)
        .gravity(0.0001)
        .on('tick', tick)
        .start()

      svg.append("svg:defs").selectAll('marker')
        .data(["end-arrow", "end-arrow-selected"])      // Different link/path types can be defined here
        .enter().append("svg:marker")    // This section adds in the arrows
        .attr("id", String)
        .attr("viewBox", "0 -5 10 10")
        //.attr("refX", 25)
        .attr("markerWidth", 4)
        .attr("markerHeight", 4)
        .attr("orient", "auto")
        .append("svg:path")
        .attr('d', 'M 0 -5 L 10 0 L 0 5 z')

      svg.append("svg:defs").selectAll('marker')
        .data(["start-arrow", "start-arrow-selected"])      // Different link/path types can be defined here
        .enter().append("svg:marker")    // This section adds in the arrows
        .attr("id", String)
        .attr("viewBox", "0 -5 10 10")
        .attr("refX", 5)
        .attr("markerWidth", 4)
        .attr("markerHeight", 4)
        .attr("orient", "auto")
        .append("svg:path")
        .attr('d', 'M 10 -5 L 0 0 L 10 5 z');

      // handles to link and node element groups
      path = svg.append('svg:g').selectAll('path'),
      circle = svg.append('svg:g').selectAll('g');

      force.on('end', function() {
        //QDR.log.debug("force end called");
        circle
          .attr('cx', function(d) {
            localStorage[d.name] = angular.toJson({x: d.x, y: d.y, fixed: d.fixed});
            return d.x; });
      });

      // app starts here
      restart(false);
          force.start();
      setTimeout(function () {
            updateForm(Object.keys(QDRService.topology.nodeInfo())[0], 'router', 0);
      }, 10)

    }

    function updateForm (key, entity, resultIndex) {
      var nodeInfo = QDRService.topology.nodeInfo();
      var onode = nodeInfo[key]
      if (onode) {
        var nodeResults = onode['.' + entity].results[resultIndex]
        var nodeAttributes = onode['.' + entity].attributeNames
        var attributes = nodeResults.map( function (row, i) {
          return {
            attributeName: nodeAttributes[i],
            attributeValue: row
          }
        })
        // sort by attributeName
        attributes.sort( function (a, b) { return a.attributeName.localeCompare(b.attributeName) })

        // move the Name first
        var nameIndex = attributes.findIndex ( function (attr) {
          return attr.attributeName === 'name'
        })
        if (nameIndex >= 0)
          attributes.splice(0, 0, attributes.splice(nameIndex, 1)[0]);
        // get the list of ports this router is listening on
        if (entity === 'router') {
          var listeners = onode['.listener'].results;
          var listenerAttributes = onode['.listener'].attributeNames;
          var normals = listeners.filter ( function (listener) {
            return QDRService.valFor( listenerAttributes, listener, 'role') === 'normal';
          })
          var ports = []
          normals.forEach (function (normalListener) {
            ports.push(QDRService.valFor( listenerAttributes, normalListener, 'port'))
          })
          // add as 2nd row
          if (ports.length)
            attributes.splice(1, 0, {attributeName: 'Listening on', attributeValue: ports, description: 'The port on which this router is listening for connections'});
        }

        $scope.$broadcast('showEntityForm', {entity: entity, attributes: attributes})
      }
      if (!$scope.$$phase) $scope.$apply()
    }

        function getContainerIndex(_id) {
            var nodeIndex = 0;
            var nodeInfo = QDRService.topology.nodeInfo();
            for (var id in nodeInfo) {
                var node = nodeInfo[id]['.router'];
                // there should be only one router entity for each node, so using results[0] should be fine
                if (QDRService.valFor( node.attributeNames, node.results[0], "id") === _id)
                    return nodeIndex;
                if (QDRService.valFor( node.attributeNames, node.results[0], "routerId") === _id)
                    return nodeIndex;
                nodeIndex++
            }
      // there was no router.id that matched, check deprecated router.routerId
            nodeIndex = 0;
            for (var id in nodeInfo) {
                var node = nodeInfo[id]['.container'];
        if (node) {
          if (QDRService.valFor ( node.attributeNames, node.results[0], "containerName") === _id)
            return nodeIndex;
        }
        nodeIndex++
      }
            //QDR.log.warn("unable to find containerIndex for " + _id);
            return -1;
        }

        function getNodeIndex (_id) {
            var nodeIndex = 0;
            var nodeInfo = QDRService.topology.nodeInfo();
            for (var id in nodeInfo) {
                if (QDRService.nameFromId(id) == _id) return nodeIndex;
                nodeIndex++
            }
            QDR.log.warn("unable to find nodeIndex for " + _id);
            return -1;
        }

        function getLink (_source, _target, dir, cls) {
            for (var i=0; i < links.length; i++) {
                var s = links[i].source, t = links[i].target;
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
            var link = {
                source: _source,
                target: _target,
                left: dir != "out",
                right: dir == "out",
                cls: cls
            };
            return links.push(link) - 1;
        }


      function resetMouseVars() {
          mousedown_node = null;
          mouseup_node = null;
          mousedown_link = null;
      }

      // update force layout (called automatically each iteration)
      function tick() {
          circle.attr('transform', function (d) {
                var cradius;
                if (d.nodeType == "inter-router") {
          cradius = d.left ? radius + 8  : radius;
                } else {
          cradius = d.left ? radiusNormal + 18  : radiusNormal;
                }
              d.x = Math.max(d.x, radiusNormal * 2);
              d.y = Math.max(d.y, radiusNormal * 2);
        d.x = Math.max(0, Math.min(width-cradius, d.x))
        d.y = Math.max(0, Math.min(height-cradius, d.y))
              return 'translate(' + d.x + ',' + d.y + ')';
          });

          // draw directed edges with proper padding from node centers
          path.attr('d', function (d) {
        //QDR.log.debug("in tick for d");
        //console.dump(d);
                var sourcePadding, targetPadding, r;

                if (d.target.nodeType == "inter-router") {
          r = radius;
          //                       right arrow  left line start
          sourcePadding = d.left ? radius + 8  : radius;
          //                      left arrow      right line start
          targetPadding = d.right ? radius + 16 : radius;
                } else {
          r = radiusNormal - 18;
          sourcePadding = d.left ? radiusNormal + 18  : radiusNormal;
          targetPadding = d.right ? radiusNormal + 16 : radiusNormal;
                }
        var dtx = Math.max(targetPadding, Math.min(width-r, d.target.x)),
            dty = Math.max(targetPadding, Math.min(height-r, d.target.y)),
            dsx = Math.max(sourcePadding, Math.min(width-r, d.source.x)),
          dsy = Math.max(sourcePadding, Math.min(height-r, d.source.y));

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
            for (var hIdx=0; hIdx<vAr.length; ++hIdx) {
                var addrT = QDRService.valFor(aAr, vAr[hIdx], "id" );
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
            for (var i=0; i<nodes.length; ++i) {
                if (nodes[i].name == name)
                    return nodes[i];
            }
            return null;
        }

        function linkFor(source, target) {
            for (var i=0; i<links.length; ++i) {
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
      d3.select("#multiple_details").style("visibility", "hidden")
      d3.select("#link_details").style("visibility", "hidden")
      d3.select('#node_context_menu').style('display', 'none');
    }
    function removeCrosssection() {
      setTimeout(function () {
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
        .each("end", function (d) {
            d3.select("#multiple_details").style("visibility", "hidden")
            stopUpdateConnectionsGrid();
        })
      hideLinkDetails();
    }

    // takes the nodes and links array of objects and adds svg elements for everything that hasn't already
    // been added
    function restart(start) {
      circle.call(force.drag);

      // path (link) group
      path = path.data(links);

      // update existing links
      path.classed('selected', function(d) { return d === selected_link; })
        .classed('highlighted', function(d) { return d.highlighted; } )
        .classed('temp', function(d) { return d.cls == 'temp'; } )
          .attr('marker-start', function(d) {
            var sel = d===selected_link ? '-selected' : '';
            return d.left ? 'url('+urlPrefix+'#start-arrow' + sel + ')' : ''; })
          .attr('marker-end', function(d) {
            var sel = d===selected_link ? '-selected' : '';
            return d.right ? 'url('+urlPrefix+'#end-arrow' + sel +')' : ''; })


      // add new links. if links[] is longer than the existing paths, add a new path for each new element
      path.enter().append('svg:path')
        .attr('class', 'link')
                .attr('marker-start', function(d) {
                        var sel = d===selected_link ? '-selected' : '';
            return d.left ? 'url('+urlPrefix+'#start-arrow' + sel + ')' : ''; })
                .attr('marker-end', function(d) {
          var sel = d===selected_link ? '-selected' : '';
                    return d.right ? 'url('+urlPrefix+'#end-arrow' + sel + ')' : ''; })
            .classed('temp', function(d) { return d.cls == 'temp'; } )
        // mouseover a line
        .on('mouseover', function (d) {
          if($scope.addingNode.step > 0) {
            if (d.cls == 'temp') {
                d3.select(this).classed('over', true);
            }
            return;
          }
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
                        for (resultIndex=0; resultIndex < onode['.connection'].results.length; ++resultIndex) {
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
        // mouseout a line
        .on('mouseout', function (d) {
          if($scope.addingNode.step > 0) {
            if (d.cls == 'temp') {
                d3.select(this).classed('over', false);
            }
            return;
          }
              //QDR.log.debug("showing connections form");
          selected_link = null;
          restart();
        })
        // contextmenu for a line
        .on("contextmenu", function(d) {
          $(document).click();
          d3.event.preventDefault();
          if (d.cls !== "temp")
              return;

          mousedown_link = d;
          d3.select('#link_context_menu')
            .style('left', (mouseX + $(document).scrollLeft()) + "px")
            .style('top', (mouseY + $(document).scrollTop()) + "px")
            .style('display', 'block');
        })
        // clicked on a line
        .on("click", function (d) {
          var clickPos = d3.mouse(this);
          d3.event.stopPropagation();
          clearPopups();
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
        })

          // remove old links
          path.exit().remove();


          // circle (node) group
          // nodes are known by id
          circle = circle.data(nodes, function (d) {
              return d.id;
          });

          // update existing nodes visual states
          circle.selectAll('circle')
              .classed('selected', function (d) { return (d === selected_node) })
              .classed('fixed', function (d) { return (d.fixed & 0b1) })

      // add new circle nodes. if nodes[] is longer than the existing paths, add a new path for each new element
          var g = circle.enter().append('svg:g')
            .classed('multiple', function(d) { return (d.normals && d.normals.length > 1)  } )

      var appendCircle = function (g) {
        // add new circles and set their attr/class/behavior
            return g.append('svg:circle')
                .attr('class', 'node')
                .attr('r', function (d) { return radii[d.nodeType] } )
                .classed('fixed', function (d) {return d.fixed})
                  .classed('temp', function(d) { return QDRService.nameFromId(d.key) == '__internal__'; } )
                  .classed('normal', function(d) { return d.nodeType == 'normal' } )
                  .classed('inter-router', function(d) { return d.nodeType == 'inter-router' } )
                  .classed('on-demand', function(d) { return d.nodeType == 'on-demand' } )
                  .classed('console', function(d) { return QDRService.isConsole(d) } )
                  .classed('artemis', function(d) { return QDRService.isArtemis(d) } )
                  .classed('qpid-cpp', function(d) { return QDRService.isQpid(d) } )
                  .classed('client', function(d) { return d.nodeType === 'normal' && !d.properties.console_identifier } )
      }
      appendCircle(g)
        .on('mouseover', function (d) { // mouseover a circle
          if ($scope.addingNode.step > 0) {
            d3.select(this).attr('transform', 'scale(1.1)');
            return;
          }
          if (!selected_node) {
            if (d.nodeType === 'inter-router') {
              //QDR.log.debug("showing general form");
              updateForm(d.key, 'router', 0);
            } else if (d.nodeType === 'normal' || d.nodeType === 'on-demand') {
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
          mousedown_node = null;
          if (!selected_node) {
              return;
          }
          setTimeout(nextHop, 1, selected_node, d);
        })
        .on('mouseout', function (d) { // mouseout a circle
          // unenlarge target node
          d3.select(this).attr('transform', '');
          for (var i=0; i<links.length; ++i) {
            links[i]['highlighted'] = false;
          }
          restart();
        })
        .on('mousedown', function (d) { // mousedown a circle
          if (d3.event.button !== 0) {   // ignore all but left button
            return;
          }
          mousedown_node = d;
          // mouse position relative to svg
          initial_mouse_down_position = d3.mouse(this.parentElement.parentElement.parentElement).slice();
        })
        .on('mouseup', function (d) {  // mouseup a circle
          if (!mousedown_node)
            return;

          selected_link = null;
          // unenlarge target node
          d3.select(this).attr('transform', '');

          // check for drag
          mouseup_node = d;
          var mySvg = this.parentElement.parentElement.parentElement;
          // if we dragged the node, make it fixed
          var cur_mouse = d3.mouse(mySvg);
          if (cur_mouse[0] != initial_mouse_down_position[0] ||
            cur_mouse[1] != initial_mouse_down_position[1]) {
              console.log("mouse pos changed. making this node fixed")
              d3.select(this).classed("fixed", d.fixed = true);
              resetMouseVars();
              return;
          }

          // we didn't drag, we just clicked on the node
          if ($scope.addingNode.step > 0) {
            if (d.nodeType !== 'inter-router')
              return;
            if (QDRService.nameFromId(d.key) == '__internal__')
              return;

            // add a link from the clicked node to the new node
            getLink(d.id, nodes.length-1, "in", "temp");
            $scope.addingNode.hasLink = true;
            if (!$scope.$$phase) $scope.$apply()
            // add new elements to the svg
            force.links(links).start();
            restart();
            return;
          }

          // if this node was selected, unselect it
          if (mousedown_node === selected_node) {
            selected_node = null;
          }
          else {
            if (d.nodeType !== 'normal' && d.nodeType !== 'on-demand')
              selected_node = mousedown_node;
          }
          for (var i=0; i<links.length; ++i) {
            links[i]['highlighted'] = false;
          }
          mousedown_node = null;
          if (!$scope.$$phase) $scope.$apply()
            restart(false);
        })
        .on("dblclick", function (d) {  // dblclick a circle
          if (d.fixed) {
            d3.select(this).classed("fixed", d.fixed = false);
            force.start();  // let the nodes move to a new position
          }
          if (QDRService.nameFromId(d.key) == '__internal__') {
            editNode();
            if (!$scope.$$phase) $scope.$apply()
          }
        })
        .on("contextmenu", function(d) { // rightclick a circle
          $(document).click();
          d3.event.preventDefault();
          $scope.contextNode = d;
          if (!$scope.$$phase) $scope.$apply()     // we just changed a scope valiable during an async event
          var bounds = $(QDR.offsetParent).offset()
          d3.select('#node_context_menu')
            .style('left', (mouseX - bounds.left + $(document).scrollLeft()) + "px")
            .style('top', (mouseY - bounds.top + $(document).scrollTop()) + "px")
            .style('display', 'block');
        })
        .on("click", function (d) {  // leftclick a circle
          var clickPos = d3.mouse(this);
          clearPopups();
          if (!d.normals) {
            // circle was a router or a broker
            if ( QDRService.isArtemis(d) && Core.ConnectionName === 'Artemis' ) {
              $location.path('/jmx/attributes?tab=artemis&con=Artemis')
            }
            return;
          }
          // circle was a client or console
          d3.event.stopPropagation();
          startUpdateConnectionsGrid(d, clickPos);
        })

      var appendContent = function (g) {
        // show node IDs
        g.append('svg:text')
          .attr('x', 0)
          .attr('y', function (d) {
            var y = 6;
            if (QDRService.isArtemis(d))
              y = 8;
            else if (QDRService.isQpid(d))
              y = 9;
            else if (d.nodeType === 'inter-router')
              y = 4;
            return y;})
          .attr('class', 'id')
          .classed('console', function(d) { return QDRService.isConsole(d) } )
          .classed('normal', function(d) { return d.nodeType === 'normal' } )
          .classed('on-demand', function(d) { return d.nodeType === 'on-demand' } )
          .classed('artemis', function(d) { return QDRService.isArtemis(d) } )
          .classed('qpid-cpp', function(d) { return QDRService.isQpid(d) } )
          .text(function (d) {
            if (QDRService.isConsole(d)) {
              return '\uf108'; // icon-desktop for this console
            }
            if (QDRService.isArtemis(d)) {
              return '\ue900'
            }
            if (QDRService.isQpid(d)) {
              return '\ue901';
            }
            if (d.nodeType === 'normal')
              return '\uf109'; // icon-laptop for clients
                    return d.name.length>7 ? d.name.substr(0,6)+'...' : d.name;
          });
      }
      appendContent(g)

      var appendTitle = function (g) {
        g.append("svg:title").text(function (d) {
          var x = '';
          if (d.normals && d.normals.length > 1)
            x = " x " + d.normals.length;
          if (QDRService.isConsole(d)) {
            return 'Dispatch console' + x
          }
          if (d.properties.product == 'qpid-cpp') {
            return 'Broker - qpid-cpp' + x
          }
          if ( QDRService.isArtemis(d) ) {
            return 'Broker - Artemis' + x
          }
          return d.nodeType == 'normal' ? 'client' + x : (d.nodeType == 'on-demand' ? 'broker' : 'Router ' + d.name)
        })
      }
      appendTitle(g);

      // remove old nodes
      circle.exit().remove();

      // add subcircles
      svg.selectAll('.subcircle').remove();
      var multiples = svg.selectAll('.multiple')
      multiples.each( function (d) {
        d.normals.forEach( function (n, i) {
          if (i<d.normals.length-1 && i<3) // only show a few shadow circles
            this.insert('svg:circle', ":first-child")
            .attr('class', 'subcircle node')
            .attr('r', 15 - i)
            .attr('transform', "translate("+ 4 * (i+1) +", 0)")
        }, d3.select(this))
      })

      // dynamically create the legend based on which node types are present
      var legendNodes = [];
      legendNodes.push(aNode("Router", "", "inter-router", undefined, 0, 0, 0, 0, false, {}))

      if (!svg.selectAll('circle.console').empty()) {
        legendNodes.push(aNode("Dispatch console", "", "normal", undefined, 1, 0, 0, 0, false, {console_identifier: 'Dispatch console'}))
      }
      if (!svg.selectAll('circle.client').empty()) {
        legendNodes.push(aNode("Client", "", "normal", undefined, 2, 0, 0, 0, false, {}))
      }
      if (!svg.selectAll('circle.qpid-cpp').empty()) {
        legendNodes.push(aNode("Qpid cpp broker", "", "on-demand", undefined, 3, 0, 0, 0, false, {product: 'qpid-cpp'}))
      }
      if (!svg.selectAll('circle.artemis').empty()) {
        legendNodes.push(aNode("Artemis broker", "", "on-demand", undefined, 4, 0, 0, 0, false, {}))
      }
      lsvg = lsvg.data(legendNodes, function (d) {
        return d.id;
      });
      var lg = lsvg.enter().append('svg:g')
        .attr('transform', function (d, i) {
          // 45px between lines and add 10px space after 1st line
          return "translate(0, "+(45*i+(i>0?10:0))+")"
        })

      appendCircle(lg)
      appendContent(lg)
      appendTitle(lg)
      lg.append('svg:text')
        .attr('x', 35)
        .attr('y', 6)
        .attr('class', "label")
        .text(function (d) {return d.key })
      lsvg.exit().remove();
      var svgEl = document.getElementById('svglegend')
      if (svgEl) {
        var bb;
        // firefox can throw an exception on getBBox on an svg element
        try {
          bb = svgEl.getBBox();
        } catch (e) {
          bb = {y: 0, height: 200, x: 0, width: 200}
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

    // show the links popup and update it periodically
    var startUpdateConnectionsGrid = function (d, clickPos) {
      // called every update tick
      var extendConnections = function () {
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
          normals.forEach( function (n) {
            var nodeInfo = QDRService.topology.nodeInfo();
            var links = nodeInfo[n.key]['.router.link'];
            var linkTypeIndex = links.attributeNames.indexOf('linkType');
            var connectionIdIndex = links.attributeNames.indexOf('connectionId');
            n.linkData = [];
            links.results.forEach( function (linkArray) {
              var link = QDRService.flatten(links.attributeNames, linkArray)
              if (link.linkType === 'endpoint' && link.connectionId === n.connectionId) {
                var l = {};
                l.owningAddr = link.owningAddr;
                l.dir = link.linkDir;
                if (l.owningAddr && l.owningAddr.length > 2)
                  if (l.owningAddr[0] === 'M')
                    l.owningAddr = l.owningAddr.substr(2)
                  else
                    l.owningAddr = l.owningAddr.substr(1)

                l.deliveryCount = QDRService.pretty(link.deliveryCount);
                l.uncounts = QDRService.pretty(link.undeliveredCount + link.unsettledCount)
                l.adminStatus = link.adminStatus;
                l.operStatus = link.operStatus;
                l.identity = link.identity
                l.connectionId = link.connectionId
                l.nodeId = n.key
                l.type = link.type
                l.name = link.name

                //QDR.log.debug("pushing link state for " + l.owningAddr + " status: "+ l.adminStatus)
                n.linkData.push(l)
              }
            })
            $scope.multiData.push(n)
            if (n.connectionId == $scope.connectionId)
              $scope.linkData = n.linkData;
          })
        }
        $scope.$apply();

        d3.select('#multiple_details')
          .style({
            height: ((normals.length + 1) * 30) + 40 + "px",
            'overflow-y': normals.length > 10 ? 'scroll' : 'hidden'
          })

      }

      // call extendConnections whenever the background data is updated
      QDRService.addUpdatedAction("normalsStats", extendConnections)
      extendConnections();
      clearPopups();
      var visibility = 'visible'
      var left = mouseX + $(document).scrollLeft()
      var bounds = $("#topology").position()
      if (d.normals.length === 1) {
        visibility = 'hidden'
        left = left - 30;
        mouseY = mouseY - 20
      }
      d3.select('#multiple_details')
        .style({
          visibility: visibility,
          opacity: 1,
          left: (clickPos[0] + bounds.left) + "px",
          top:  (clickPos[1] + bounds.top) + "px"})
      if (d.normals.length === 1) {
        // simulate a click on the connection to popup the link details
        $scope.multiDetails.showLinksList( {entity: d} )
      }
    }

    var stopUpdateConnectionsGrid = function () {
      QDRService.delUpdatedAction("normalsStats");
    }

    function nextHop(thisNode, d) {
      if ((thisNode) && (thisNode != d)) {
        var target = findNextHopNode(thisNode, d);
        //QDR.log.debug("highlight link from node ");
         //console.dump(nodeFor(selected_node.name));
         //console.dump(target);
        if (target) {
          var hlLink = linkFor(nodeFor(thisNode.name), target);
          //QDR.log.debug("need to highlight");
          //console.dump(hlLink);
          if (hlLink)
            hlLink['highlighted'] = true;
          else
            target = null;
        }
        setTimeout(nextHop, 1, target, d);
      }
      restart();
    }


    function mousedown() {
      // prevent I-bar on drag
      //d3.event.preventDefault();

      // because :active only works in WebKit?
      svg.classed('active', true);
    }

    QDRService.addUpdatedAction("topology", function() {
      //QDR.log.debug("Topology controller was notified that the model was updated");
      if (hasChanged()) {
        QDR.log.info("svg graph changed")
        saveChanged();
        // TODO: update graph nodes instead of rebuilding entire graph
        d3.select("#SVG_ID").remove();
        d3.select("#svg_legend svg").remove();
        animate = true;
        initForceGraph();
        //if ($location.path().startsWith("/topology"))
        //    Core.notification('info', "Qpid dispatch router topology changed");

      } else {
        //QDR.log.debug("no changes")
      }
    });

    function hasChanged () {
      // Don't update the underlying topology diagram if we are adding a new node.
      // Once adding is completed, the topology will update automatically if it has changed
      if ($scope.addingNode.step > 0)
        return false;
      var nodeInfo = QDRService.topology.nodeInfo();
      if (Object.keys(nodeInfo).length != Object.keys(savedKeys).length)
        return true;
      for (var key in nodeInfo) {
                // if this node isn't in the saved node list
                if (!savedKeys.hasOwnProperty(key))
                    return true;
                // if the number of connections for this node chaanged
                if (nodeInfo[key]['.connection'].results.length != savedKeys[key]) {
          /*
          QDR.log.debug("number of connections changed for " + key);
          QDR.log.debug("QDRService.topology._nodeInfo[key]['.connection'].results.length");
          console.dump(QDRService.topology._nodeInfo[key]['.connection'].results.length);
          QDR.log.debug("savedKeys[key]");
          console.dump(savedKeys[key]);
          */
                    return true;
                }
      }
      return false;
    };
    function saveChanged () {
            savedKeys = {};
            var nodeInfo = QDRService.topology.nodeInfo();
            // save the number of connections per node
        for (var key in nodeInfo) {
            savedKeys[key] = nodeInfo[key]['.connection'].results.length;
        }
      //QDR.log.debug("saving current keys");
      //console.dump(savedKeys);
    };
    // we are about to leave the page, save the node positions
    $rootScope.$on('$locationChangeStart', function(event, newUrl, oldUrl) {
      //QDR.log.debug("locationChangeStart");
      nodes.forEach( function (d) {
             localStorage[d.name] = angular.toJson({x: d.x, y: d.y, fixed: d.fixed});
      });
            $scope.addingNode.step = 0;

    });
    // When the DOM element is removed from the page,
    // AngularJS will trigger the $destroy event on
    // the scope
    $scope.$on("$destroy", function( event ) {
      //QDR.log.debug("scope on destroy");
      QDRService.stopUpdating();
      QDRService.delUpdatedAction("topology");
      d3.select("#SVG_ID").remove();
      window.removeEventListener('resize', resize);
    });

    initForceGraph();
    saveChanged();
    QDRService.startUpdating();

    function doAddDialog(NewRouterName) {
      var d = $modal.dialog({
      dialogClass: "modal dlg-large",
      backdrop: true,
      keyboard: true,
      backdropClick: true,
          controller: 'QDR.NodeDialogController',
          templateUrl: 'node-config-template.html',
          resolve: {
              newname: function () {
                  return NewRouterName;
              }
          }
      });
      d.open().then(function (result) {
      if (result)
        doDownloadDialog(result);
      });
    };

    function doDownloadDialog(result) {
      d = modal.dialog({
      backdrop: true,
      keyboard: true,
      backdropClick: true,
      controller: 'QDR.DownloadDialogController',
          templateUrl: 'download-dialog-template.html',
          resolve: {
              results: function () {
                  return result;
              }
          }
      });
      d.open().then(function (result) {
      //QDR.log.debug("download dialog done")
      })
      if (!$scope.$$phase) $scope.$apply()
    };
  };

  return QDR;
}(QDR || {}));

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


	QDR.module.controller('QDR.TopologyFormController', function ($scope, QDRService) {

		$scope.attributes = []
        var generalCellTemplate = '<div class="ngCellText"><span title="{{row.entity.description}}">{{row.entity.attributeName}}</span></div>';
        $scope.topoGridOptions = {
            data: 'attributes',
			enableColumnResize: true,
			multiSelect: false,
            columnDefs: [
            {
                field: 'attributeName',
                displayName: 'Attribute',
                cellTemplate: generalCellTemplate
            },
            {
                field: 'attributeValue',
                displayName: 'Value'
            }
            ]
        };
		$scope.form = ''
		$scope.$on('showEntityForm', function (event, args) {
			var attributes = args.attributes;
			var entityTypes = QDRService.schema.entityTypes[args.entity].attributes;
			Object.keys(attributes).forEach( function (attr) {
				if (entityTypes[attr])
					attributes[attr].description = entityTypes[attr]
			})
			$scope.attributes = attributes;
			$scope.form = args.entity;
		})
		$scope.$on('showAddForm', function (event) {
			$scope.form = 'add';
		})
	})


  /**
   * @method TopologyController
   *
   * Controller that handles the QDR topology page
   */
    QDR.module.controller("QDR.TopologyController", ['$scope', '$rootScope', 'QDRService', '$location', '$timeout', '$dialog',
    function($scope, $rootScope, QDRService, $location, $timeout, $dialog) {

		$scope.multiData = [{name: ''}, {name: ''}, {name: ''}]
        $scope.multiDetails = {
            data: 'multiData',
            columnDefs: [
            {
                field: 'host',
                displayName: 'Host'
            },
            {
                field: 'user',
                displayName: 'User'
            },
			{
				field: 'properties',
				displayName: 'Properties'
			},
			{
				field: 'isEncrypted',
				displayName: 'Encrypted'
			}
            ]
        };

		if (!QDRService.connected) {
			// we are not connected. we probably got here from a bookmark or manual page reload
			$location.path("/dispatch_plugin/connect")
			$location.search('org', "topology");
			return;
		}

		QDR.log.debug("started QDR.TopologyController with urlPrefix: " + $location.absUrl());
		var urlPrefix = $location.absUrl();

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
			for (key in nodeInfo) {
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

		var mouseX, mouseY;
		// event handlers for popup context menu
		$(document).mousemove(function (e) {
		    mouseX = e.clientX;
		    mouseY = e.clientY;
		});
		$(document).mousemove();
		$(document).click(function (e) {
			$scope.contextNode = null;
            $(".contextMenu").fadeOut(200);
        });


		// set up SVG for D3
	    var width, height;
	    var tpdiv = $('#topology');
	    var colors = {'inter-router': "#EAEAEA", 'normal': "#F0F000", 'on-demand': '#00F000'};
	    var gap = 5;
	    var radii = {'inter-router': 25, 'normal': 15, 'on-demand': 15};
	    var radius = 25;
	    var radiusNormal = 15;
	    width = tpdiv.width() - gap;
	    height = $('#main').height() - $('#topology').position().top - gap;

	    var svg, lsvg;
		var force;
		var animate = false; // should the force graph organize itself when it is displayed
		var path, circle;
		var savedKeys = {};
		var dblckickPos = [0,0];

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
            //QDR.log.debug("initForceGraph called");
			nodes = [];
			links = [];

			svg = d3.select('#topology')
				.append('svg')
				.attr("id", "SVG_ID")
				.attr('width', width)
				.attr('height', height)
	            .on("contextmenu", function(d) {
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
						var nodeType = role === 'normal' ? (properties.console_identifier == 'Dispatch console' ? 'console' : 'client') : 'broker';
						if (role === 'normal') {
							node.user = QDRService.valFor(attrs, conns[j], "user")
							node.isEncrypted = QDRService.valFor(attrs, conns[j], "isEncrypted")
							node.host = QDRService.valFor(attrs, conns[j], "host")

							if (!normalsParent[nodeType]) {
								normalsParent[nodeType] = node;
								nodes.push(	node );
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

			//drag = force.drag()
            //    .on("dragstart", dragstart);

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
						attributes.splice(1, 0, {attributeName: 'Listening on', attributeValue: ports});
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
            QDR.log.warn("unable to find containerIndex for " + _id);
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
	        // draw directed edges with proper padding from node centers
	        path.attr('d', function (d) {
				//QDR.log.debug("in tick for d");
				//console.dump(d);

	            var deltaX = d.target.x - d.source.x,
	                deltaY = d.target.y - d.source.y,
	                dist = Math.sqrt(deltaX * deltaX + deltaY * deltaY),
	                normX = deltaX / dist,
	                normY = deltaY / dist;
	                var sourcePadding, targetPadding;
	                if (d.target.nodeType == "inter-router") {
						//                       right arrow  left line start
						sourcePadding = d.left ? radius + 8  : radius;
						//                      left arrow      right line start
						targetPadding = d.right ? radius + 16 : radius;
	                } else {
						sourcePadding = d.left ? radiusNormal + 18  : radiusNormal;
						targetPadding = d.right ? radiusNormal + 16 : radiusNormal;
	                }
	                var sourceX = d.source.x + (sourcePadding * normX),
	                sourceY = d.source.y + (sourcePadding * normY),
	                targetX = d.target.x - (targetPadding * normX),
	                targetY = d.target.y - (targetPadding * normY);
	            return 'M' + sourceX + ',' + sourceY + 'L' + targetX + ',' + targetY;
	        });

	        circle.attr('transform', function (d) {
	            d.x = Math.max(d.x, radiusNormal * 2);
	            d.y = Math.max(d.y, radiusNormal * 2);
	            return 'translate(' + d.x + ',' + d.y + ')';
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
                var addrT = QDRService.valFor(aAr, vAr[hIdx], "routerId" );
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

		function removeCrosssection() {
			setTimeout(function () {
				d3.select("[id^=tooltipsy]").remove()
				$('.hastip').empty();
			}, 1010);
			d3.select("#crosssection svg g").transition()
                .duration(1000)
				.attr("transform", "translate("+(dblckickPos[0]-140) + "," + (dblckickPos[1]-100) + ") scale(0)")
                .style("opacity", 0)
                .each("end", function (d) {
                    d3.select("#crosssection svg").remove();
                    d3.select("#crosssection").style("display","none");
                });
            d3.select("#multiple_details").transition()
                .duration(500)
                .style("opacity", 0)
                .each("end", function (d) {
                    d3.select("#multiple_details").style("display", "none")
                })
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
                .on("click", function (d) {
                    dblckickPos = d3.mouse(this);
                    d3.event.stopPropagation();
                    var diameter = 400;
                    var format = d3.format(",d");
                    var pack = d3.layout.pack()
                        .size([diameter - 4, diameter - 4])
                        .padding(-10)
                        .value(function(d) { return d.size; });

                    var svg = d3.select("#crosssection").append("svg")
                        .attr("width", diameter)
                        .attr("height", diameter)
                    var svgg = svg.append("g")
                        .attr("transform", "translate(2,2)");

					svg.on('click', function (d) {
						removeCrosssection();
					})

					var root = {
						name: "links between " + d.source.name + " and " + d.target.name,
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
								{ name: "(" + link[linkDirIndex] + ") " + link[nameIndex],
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

//	                node.filter(function(d) { return !d.children; }).append("text")
	                node.append("text")
	                      .attr("dy", function (d) { return d.children ? "-10em" : ".3em"})
	                      .style("text-anchor", "middle")
	                      .text(function(d) {
	                          return d.name.substring(0, d.r / 3);
	                      });

					$('.hastip').tooltipsy({ alignTo: 'cursor'});
					svgg.attr("transform", "translate("+(dblckickPos[0]-140) + "," + (dblckickPos[1]-100) + ") scale(0.01)")
	                d3.select("#crosssection").style("display","block");

					svgg.transition().attr("transform", "translate(2,2) scale(1)")
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
  			    //.classed('multiple', function(d) { return (d.normals && d.normals.length > 1)  } )

			// add new circle nodes. if nodes[] is longer than the existing paths, add a new path for each new element
	        var g = circle.enter().append('svg:g')
  			    .classed('multiple', function(d) { return (d.normals && d.normals.length > 1)  } )

			var appendCircle = function (g) {
				// add new circles and set their attr/class/behavior
		        return g.append('svg:circle')
		            .attr('class', 'node')
		            .attr('r', function (d) {
		                return radii[d.nodeType];
		            })
		            .classed('fixed', function (d) {return d.fixed})
	                .classed('temp', function(d) { return QDRService.nameFromId(d.key) == '__internal__'; } )
	                .classed('normal', function(d) { return d.nodeType == 'normal' } )
	                .classed('inter-router', function(d) { return d.nodeType == 'inter-router' } )
	                .classed('on-demand', function(d) { return d.nodeType == 'on-demand' } )
	                .classed('console', function(d) { return d.properties.console_identifier == 'Dispatch console' } )
	                .classed('artemis', function(d) { return QDRService.isArtemis(d) } )
	                .classed('qpid-cpp', function(d) { return QDRService.isQpid(d) } )
	                .classed('client', function(d) { return d.nodeType === 'normal' && !d.properties.console_identifier } )
			}
			appendCircle(g).on('mouseover', function (d) {
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
	            .on('mouseout', function (d) {
	                // unenlarge target node
	                d3.select(this).attr('transform', '');
                    for (var i=0; i<links.length; ++i) {
                        links[i]['highlighted'] = false;
                    }
                    restart();
	            })
	            .on('mousedown', function (d) {
	                if (d3.event.button !== 0) {   // ignore all but left button
	                    return;
	                }
	                mousedown_node = d;
	                // mouse position relative to svg
	                initial_mouse_down_position = d3.mouse(this.parentElement.parentElement.parentElement).slice();
	            })
	            .on('mouseup', function (d) {
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
						// don't select nodes that represent multiple clients/consoles
                        if (!d.normals || d.normals.length < 2)
                            selected_node = mousedown_node;
                    }
                    for (var i=0; i<links.length; ++i) {
                        links[i]['highlighted'] = false;
                    }
	                mousedown_node = null;
					if (!$scope.$$phase) $scope.$apply()
                    restart(false);

	            })
	            .on("dblclick", function (d) {
	                if (d.fixed) {
						d3.select(this).classed("fixed", d.fixed = false);
						force.start();  // let the nodes move to a new position
	                }
	                if (QDRService.nameFromId(d.key) == '__internal__') {
	                    editNode();
						if (!$scope.$$phase) $scope.$apply()
	                }
	            })
	            .on("contextmenu", function(d) {
	                $(document).click();
                    d3.event.preventDefault();
	                $scope.contextNode = d;
					if (!$scope.$$phase) $scope.$apply()     // we just changed a scope valiable during an async event
                    d3.select('#node_context_menu')
                      .style('left', (mouseX + $(document).scrollLeft()) + "px")
                      .style('top', (mouseY + $(document).scrollTop()) + "px")
                      .style('display', 'block');

                })
                .on("click", function (d) {
					if (!d.normals || d.normals.length < 2) {
			            if ( QDRService.isArtemis(d) && Core.ConnectionName === 'Artemis' ) {
							$location.path('/jmx/attributes?tab=artemis&con=Artemis')
						}
						return;
					}
                    clickPos = d3.mouse(this);
                    d3.event.stopPropagation();
                    $scope.multiData = []
                    d.normals.forEach( function (n) {
                        $scope.multiData.push(n)
                    })
                    $scope.$apply();
                    d3.select('#multiple_details')
                        .style({
                            display: 'block',
                            opacity: 1,
                            height: (d.normals.length + 1) * 30 + "px",
                            'overflow-y': d.normals.length > 10 ? 'scroll' : 'hidden',
		                    left: (mouseX + $(document).scrollLeft()) + "px",
                            top:  (mouseY + $(document).scrollTop()) + "px"})
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
	                .classed('console', function(d) { return d.properties.console_identifier == 'Dispatch console' } )
	                .classed('normal', function(d) { return d.nodeType === 'normal' } )
	                .classed('on-demand', function(d) { return d.nodeType === 'on-demand' } )
	                .classed('artemis', function(d) { return QDRService.isArtemis(d) } )
	                .classed('qpid-cpp', function(d) { return QDRService.isQpid(d) } )
		            .text(function (d) {
		                if (d.properties.console_identifier == 'Dispatch console') {
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
		            if (d.properties.console_identifier == 'Dispatch console') {
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

			svg.selectAll('.multiple')
				.insert('svg:circle', '.normal')
					.attr('class', 'subcircle')
					.attr('r', 18)


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
			var svgEl = document.getElementById("svglegend"),
				bb = svgEl.getBBox();
			svgEl.style.height = bb.y + bb.height;
			svgEl.style.width = bb.x + bb.width;

	        if (!mousedown_node || !selected_node)
	            return;

            if (!start)
                return;
	        // set the graph in motion
	        //QDR.log.debug("mousedown_node is " + mousedown_node);
	        force.start();

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
			console.dump(savedKeys);
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
        });

		initForceGraph();
		saveChanged();
        QDRService.startUpdating();

	    function doAddDialog(NewRouterName) {
		    var d = $dialog.dialog({
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
		    d = $dialog.dialog({
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
  }]);

  QDR.module.controller("QDR.NodeDialogController", function($scope, QDRService, dialog, newname) {
   		var schema = QDRService.schema;
   		var myEntities = ['container', 'router', 'log', 'listener' ];
   		var typeMap = {integer: 'number', string: 'text', path: 'text', boolean: 'boolean'};
		var newLinks = $('path.temp').toArray();    // jquery array of new links for the added router
		var nodeInfo = QDRService.topology.nodeInfo();
		var separatedEntities = []; // additional entities required if a link is reversed
		var myPort = 0, myAddr = '0.0.0.0'; // port and address for new router
   		$scope.entities = [];

		// find max port number that is used in all the listeners
		var getMaxPort = function (nodeInfo) {
			var maxPort = 5674;
			for (var key in nodeInfo) {
				var node = nodeInfo[key];
				var listeners = node['.listener'];
				var attrs = listeners.attributeNames;
				for (var i=0; i<listeners.results.length; ++i) {
					var res = listeners.results[i];
					var port = QDRService.valFor(attrs, res, 'port');
					if (parseInt(port, 10) > maxPort)
						maxPort = parseInt(port, 10);
				}
			}
			return maxPort;
		}
		var maxPort = getMaxPort(nodeInfo);

		// construct an object that contains all the info needed for a single tab's fields
		var entity = function (actualName, tabName, humanName, ent, icon, link) {
			var nameIndex = -1; // the index into attributes that the name field was placed
			var index = 0;
			var info = {
			    actualName: actualName,
				tabName:    tabName,
				humanName:  humanName,
				description:ent.description,
				icon:       angular.isDefined(icon) ? icon : '',
				references: ent.references,
				link:       link,

   		        attributes: $.map(ent.attributes, function (value, key) {
					// skip identity and depricated fields
   		            if (key == 'identity' || value.description.startsWith('Deprecated'))
   		                return null;
					var val = value['default'];
					if (key == 'name')
						nameIndex = index;
					index++;
					return {    name:       key,
								humanName:  QDRService.humanify(key),
                                description:value.description,
                                type:       typeMap[value.type],
                                rawtype:    value.type,
                                input:      typeof value.type == 'string' ? value.type == 'boolean' ? 'boolean' : 'input'
                                                                          : 'select',
                                selected:   val ? val : undefined,
                                'default':  value['default'],
                                value:      val,
                                required:   value.required,
                                unique:     value.unique
                    };
                })
			}
			// move the 'name' attribute to the 1st position
			if (nameIndex > -1) {
				var tmp = info.attributes[0];
				info.attributes[0] = info.attributes[nameIndex];
				info.attributes[nameIndex] = tmp;
			}
			return info;
		}

		// remove the annotation fields
		var stripAnnotations = function (entityName, ent, annotations) {
			if (ent.references) {
				var newEnt = {attributes: {}};
				ent.references.forEach( function (annoKey) {
					if (!annotations[annoKey])
						annotations[annoKey] = {};
					annotations[annoKey][entityName] = true;    // create the key/consolidate duplicates
					var keys = Object.keys(schema.annotations[annoKey].attributes);
					for (var attrib in ent.attributes) {
						if (keys.indexOf(attrib) == -1) {
							newEnt.attributes[attrib] = ent.attributes[attrib];
						}
					}
					// add a field for the reference name
					newEnt.attributes[annoKey] = {type: 'string',
							description: 'Name of the ' + annoKey + ' section.',
							'default': annoKey, required: true};
				})
				newEnt.references = ent.references;
				newEnt.description = ent.description;
				return newEnt;
			}
			return ent;
		}

		var annotations = {};
   		myEntities.forEach(function (entityName) {
   		    var ent = schema.entityTypes[entityName];
   		    var hName = QDRService.humanify(entityName);
   		    if (entityName == 'listener')
   		        hName = "Listener for clients";
   		    var noAnnotations = stripAnnotations(entityName, ent, annotations);
			var ediv = entity(entityName, entityName, hName, noAnnotations, undefined);
			if (ediv.actualName == 'router') {
				ediv.attributes.filter(function (attr) { return attr.name == 'name'})[0].value = newname;
				// if we have any new links (connectors), then the router's mode should be interior
				if (newLinks.length) {
					var roleAttr = ediv.attributes.filter(function (attr) { return attr.name == 'mode'})[0];
					roleAttr.value = roleAttr.selected = "interior";
				}
			}
			if (ediv.actualName == 'container') {
				ediv.attributes.filter(function (attr) { return attr.name == 'containerName'})[0].value = newname + "-container";
			}
			if (ediv.actualName == 'listener') {
				// find max port number that is used in all the listeners
				ediv.attributes.filter(function (attr) { return attr.name == 'port'})[0].value = ++maxPort;
			}
			// special case for required log.module since it doesn't have a default
			if (ediv.actualName == 'log') {
				var moduleAttr = ediv.attributes.filter(function (attr) { return attr.name == 'module'})[0];
				moduleAttr.value = moduleAttr.selected = "DEFAULT";
			}
			$scope.entities.push( ediv );
   		})

		// add a tab for each annotation that was found
		var annotationEnts = [];
		for (var key in annotations) {
			ent = angular.copy(schema.annotations[key]);
			ent.attributes.name = {type: "string", unique: true, description: "Unique name that is used to refer to this set of attributes."}
			var ediv = entity(key, key+'tab', QDRService.humanify(key), ent, undefined);
			ediv.attributes.filter(function (attr) { return attr.name == 'name'})[0].value = key;
			$scope.entities.push( ediv );
			annotationEnts.push( ediv );
		}

		// add an additional listener tab if any links are reversed
		ent = schema.entityTypes['listener'];
		newLinks.some(function (link) {
			if (link.__data__.right) {
	   		    var noAnnotations = stripAnnotations('listener', ent, annotations);
				var ediv = entity("listener", "listener0", "Listener (internal)", noAnnotations, undefined);
				ediv.attributes.filter(function (attr) { return attr.name == 'port'})[0].value = ++maxPort;
				// connectors from other routers need to connect to this addr:port
				myPort = maxPort;
				myAddr = ediv.attributes.filter(function (attr) { return attr.name == 'addr'})[0].value

				// override the role. 'normal' is the default, but we want inter-router
				ediv.attributes.filter(function( attr ) { return attr.name == 'role'})[0].selected = 'inter-router';
				separatedEntities.push( ediv );
				return true; // stop looping
			}
			return false;   // continue looping
		})

		// Add connector tabs for each new link on the topology graph
		ent = schema.entityTypes['connector'];
		newLinks.forEach(function (link, i) {
   		    var noAnnotations = stripAnnotations('connector', ent, annotations);
			var ediv = entity('connector', 'connector' + i, " " + link.__data__.source.name, noAnnotations, link.__data__.right, link)

			// override the connector role. 'normal' is the default, but we want inter-router
			ediv.attributes.filter(function( attr ) { return attr.name == 'role'})[0].selected = 'inter-router';

			// find the addr:port of the inter-router listener to use
			var listener = nodeInfo[link.__data__.source.key]['.listener'];
			var attrs = listener.attributeNames;
			for (var i=0; i<listener.results.length; ++i) {
				var res = listener.results[i];
				var role = QDRService.valFor(attrs, res, 'role');
				if (role == 'inter-router') {
					ediv.attributes.filter(function( attr ) { return attr.name == 'addr'})[0].value =
						QDRService.valFor(attrs, res, 'addr')
					ediv.attributes.filter(function( attr ) { return attr.name == 'port'})[0].value =
						QDRService.valFor(attrs, res, 'port')
					break;
				}
			}
			if (link.__data__.right) {
				// connectors from other nodes need to connect to the new router's listener addr:port
   				ediv.attributes.filter(function (attr) { return attr.name == 'port'})[0].value = myPort;
   				ediv.attributes.filter(function (attr) { return attr.name == 'addr'})[0].value = myAddr;

				separatedEntities.push(ediv)
			}
			else
				$scope.entities.push( ediv );
		})
		Array.prototype.push.apply($scope.entities, separatedEntities);

		// update the description on all the annotation tabs
		annotationEnts.forEach ( function (ent) {
			var shared = Object.keys(annotations[ent.actualName]);
			ent.description += " These fields are shared by " + shared.join(" and ") + ".";

		})

		$scope.testPattern = function (attr) {
			if (attr.rawtype == 'path')
				return /^(\/)?([^/\0]+(\/)?)+$/;
				//return /^(.*\/)([^/]*)$/;
			return /(.*?)/;
		}

		$scope.attributeDescription = '';
		$scope.attributeType = '';
		$scope.attributeRequired = '';
		$scope.attributeUnique = '';
		$scope.active = 'container'
		$scope.fieldsetDivs = "/fieldsetDivs.html"
		$scope.setActive = function (tabName) {
			$scope.active = tabName
		}
		$scope.isActive = function (tabName) {
			return $scope.active === tabName
		}
		$scope.showDescription = function (attr, e) {
			$scope.attributeDescription = attr.description;
			var offset = jQuery(e.currentTarget).offset()
			jQuery('.attr-description').offset({top: offset.top})

			$scope.attributeType = "Type: " + JSON.stringify(attr.rawtype);
			$scope.attributeRequired = attr.required ? 'required' : '';
			$scope.attributeUnique = attr.unique ? 'Must be unique' : '';
		}
        // handle the download button click
        // copy the dialog's values to the original node
        $scope.download = function () {
	        dialog.close({entities: $scope.entities, annotations: annotations});
        }
        $scope.cancel = function () {
            dialog.close()
        };

		$scope.selectAnnotationTab = function (tabName) {
            var tabs = $( "#tabs" ).tabs();
            tabs.tabs("select", tabName);
		}

        var initTabs = function () {
            var div = angular.element("#tabs");
            if (!div.width()) {
                setTimeout(initTabs, 100);
                return;
            }
            $( "#tabs" )
                .tabs()
                .addClass('ui-tabs-vertical ui-helper-clearfix');
        }
        // start the update loop
        initTabs();

  });

QDR.module.controller("QDR.DownloadDialogController", function($scope, QDRService, $templateCache, $window, dialog, results) {
		var result = results.entities;
		var annotations = results.annotations;
		var annotationKeys = Object.keys(annotations);
		var annotationSections = {};

		// use the router's name as the file name if present
		$scope.newRouterName = 'router';
		result.forEach( function (e) {
			if (e.actualName == 'router') {
				e.attributes.forEach( function (a) {
					if (a.name == 'name') {
						$scope.newRouterName = a.value;
					}
				})
			}
		})
		$scope.newRouterName = $scope.newRouterName + ".conf";

		var template = $templateCache.get('config-file-header.html');
		$scope.verbose = true;
		$scope.$watch('verbose', function (newVal) {
			if (newVal !== undefined) {
				// recreate output using current verbose setting
				getOutput();
			}
		})

		var getOutput = function () {
			$scope.output = template + '\n';
			$scope.parts = [];
			var commentChar = '#'
			result.forEach(function (entity) {
				// don't output a section for annotations, they get flattened into the entities
				var section = "";
				if (entity.icon) {
					section += "##\n## Add to " + entity.link.__data__.source.name + "'s configuration file\n##\n";
				}
				section += "##\n## " + QDRService.humanify(entity.actualName) + " - " + entity.description + "\n##\n";
				section += entity.actualName + " {\n";
				entity.attributes.forEach(function (attribute) {
					if (attribute.input == 'select')
						attribute.value = attribute.selected;

					// treat values with all spaces and empty strings as undefined
					attribute.value = String(attribute.value).trim();
					if (attribute.value === 'undefined' || attribute.value === '')
						attribute.value = undefined;

					if ($scope.verbose) {
						commentChar = attribute.required || attribute.value != attribute['default'] ? ' ' : '#';
						if (!attribute.value) {
							commentChar = '#';
							attribute.value = '';
						}
						section += commentChar + "    "
							+ attribute.name + ":" + Array(Math.max(20 - attribute.name.length, 1)).join(" ")
							+ attribute.value
						    + Array(Math.max(20 - ((attribute.value)+"").length, 1)).join(" ")
							+ '# ' + attribute.description
						    + "\n";
					} else {
						if (attribute.value) {
							if (attribute.value != attribute['default'] || attribute.required)
								section += "    "
									+ attribute.name + ":" + Array(20 - attribute.name.length).join(" ")
									+ attribute.value + "\n";

						}
					}
				})
				section += "}\n\n";
				// if entity.icon is true, this is a connector intended for another router
				if (entity.icon)
					$scope.parts.push({output: section,
								link: entity.link,
								name: entity.link.__data__.source.name,
								references: entity.references});
				else
					$scope.output += section;

				// if this section is actually an annotation
				if (annotationKeys.indexOf(entity.actualName) > -1) {
					annotationSections[entity.actualName] = section;
				}
			})
			// go back and add annotation sections to the parts
			$scope.parts.forEach (function (part) {
				for (var section in annotationSections) {
					if (part.references.indexOf(section) > -1) {
						part.output += annotationSections[section];
					}
				}
			})
			QDR.log.debug($scope.output);
		}

        // handle the download button click
        $scope.download = function () {
			var blob = new Blob([$scope.output], { type: 'text/plain' });
	        var downloadLink = angular.element('<a></a>');
	        downloadLink.attr('href', ($window.URL || $window.webkitURL).createObjectURL(blob));
	        downloadLink.attr('download', $scope.newRouterName);
	        downloadLink[0].click();
        }

		$scope.downloadPart = function (part) {
			var linkName = part.link.__data__.source.name + 'additional.conf';
			var blob = new Blob([part.output], { type: 'text/plain' });
	        var downloadLink = angular.element('<a></a>');
	        downloadLink.attr('href', ($window.URL || $window.webkitURL).createObjectURL(blob));
	        downloadLink.attr('download', linkName);
	        downloadLink[0].click();

			QDR.log.debug(part);
		}

		$scope.done = function () {
	        dialog.close();
		}
});

  return QDR;
}(QDR || {}));

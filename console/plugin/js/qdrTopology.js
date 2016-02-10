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

  /**
   * @method SettingsController
   * @param $scope
   * @param QDRServer
   *
   * Controller that handles the QDR settings page
   */

  /**
   * @function NavBarController
   *
   * @param $scope
   * @param workspace
   *
   * The controller for this plugin's navigation bar
   *
   */
   
    QDR.module.controller("QDR.TopologyController", ['$scope', '$rootScope', 'uiGridConstants', 'QDRService', '$uibModal', '$location', '$timeout',
    function($scope, $rootScope, uiGridConstants, QDRService, $uibModal, $location, $timeout) {

		QDR.log.debug("started QDR.TopologyController with location.url: " + $location.url());
		var urlPrefix = window.location.pathname;

		$scope.attributes = [];
        $scope.connAttributes = [];
        $scope.topoForm = "general";
        $scope.topoFormSelected = "";
		$scope.addingNode = {
			step: 0,
			hasLink: false,
			trigger: ''
		}; // shared object about the node that is be	    $scope.topoForm = "general";

        var generalCellTemplate = '<div class="ngCellText"><span title="{{row.entity.description}}">{{row.entity.attributeName}}</span></div>';

		$scope.isGeneral = function () {
    	    //QDR.log.debug("$scope.topoForm=" + $scope.topoForm)
    	    return $scope.topoForm === 'general';
		};
		$scope.isConnections = function () {
    	    //QDR.log.debug("$scope.topoForm=" + $scope.topoForm)
    	    return $scope.topoForm === 'connections';
		};
		$scope.isAddNode = function () {
    	    //QDR.log.debug("$scope.topoForm=" + $scope.topoForm)
			return $scope.topoForm === 'addNode';
		}

		$scope.getTableHeight = function (rows) {
	        return {height: (rows.length * 30) + "px"};
		}
        $scope.isSelected = function () {
            return ($scope.topoFormSelected != "");
        }

        $scope.cancel = function () {
            $scope.addingNode.step = 0;
        }
		$scope.editNewRouter = function () {
			$scope.addingNode.trigger = 'editNode';
		}

        $scope.topoGridOptions = {
            data: 'attributes',
			enableColumnResize: true,
			enableHorizontalScrollbar: uiGridConstants.scrollbars.NEVER,
            enableVerticalScrollbar: uiGridConstants.scrollbars.NEVER,
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
        $scope.topoConnOptions = angular.copy($scope.topoGridOptions);
        $scope.topoConnOptions.data = 'connAttributes';
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
	    	/* {title: '3D Globe view', name: 'Globe', right: false}, */
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
					$scope.topoForm = 'general'
					$scope.topoFormSelected = '';
					$scope.addingNode.step = 0;
				} else {
					// start adding node mode
					$scope.addingNode.step = 1;
				}
			} else {
				$scope.topoForm = 'general'
				$scope.topoFormSelected = '';
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
				$scope.topoForm = 'general'
				$scope.topoFormSelected = '';
			} else if (newValue > 0) {
				// we are starting the add mode
				$scope.topoForm = 'addNode';
                $scope.topoFormSelected = 'addNode';

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
	    height = $(document).height() - gap;

	    var svg;
		var force;
		var animate = false; // should the force graph organize itself when it is displayed
		var path, circle;
		var savedKeys = {};

	    // set up initial nodes and links
	    //  - nodes are known by 'id', not by index in array.
	    //  - selected edges are indicated on the node (as a bold red circle).
	    //  - links are always source < target; edge directions are set by 'left' and 'right'.
		var nodes = [];
		var links = [];

		var aNode = function (id, name, nodeType, nodeInfo, nodeIndex, x, y, resultIndex, fixed) {
			var containerName;
			if (nodeInfo) {
				var node = nodeInfo[id];
				if (node) {
					containerName = node['.container'].results[0][0];
				}
			}
			return {   key: id,
				name: name,
				nodeType: nodeType,
				containerName: containerName,
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

		var cities = ["Raleigh"];
		var possibleCities = ["Boston","Tel Aviv-Yafo", "Brno", "Toronto", "Beijing", , "Ashburn", "Raleigh"]
		//var drag;
		// create an bare svg element and
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
					d3.select("#crosssection").style("display","none");
					d3.select("#crosssection svg").remove();

                });

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
                				y: height / 2 + yInit,
                				fixed: false};
				}
				nodes.push( aNode(id, name, "inter-router", nodeInfo, nodes.length, position.x, position.y, undefined, position.fixed) );
				yInit *= -1;
				//QDR.log.debug("adding node " + nodes.length-1);
			}

			// initialize the list of links
			var source = 0;
			var client = 1;
			cities = ["Raleigh"];
			for (var id in nodeInfo) {
				var onode = nodeInfo[id];
				var conns = onode['.connection'].results;
				var attrs = onode['.connection'].attributeNames;

				for (var j = 0; j < conns.length; j++) {
                    var role = QDRService.valFor(attrs, conns[j], "role");
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
						var parent = getNodeIndex(QDRService.nameFromId(id));
						//QDR.log.debug("external client parent is " + parent);

                        // if we have any new clients, animate the force graph to position them
                        var position = angular.fromJson(localStorage[name]);
                        if (!angular.isDefined(position)) {
                            animate = true;
                            position = {x: nodes[parent].x + 40 + Math.sin(Math.PI/2 * client),
                                        y: nodes[parent].y + 40 + Math.cos(Math.PI/2 * client),
                                        fixed: false};
                        }
						//QDR.log.debug("adding node " + nodeIndex);
						nodes.push(	aNode(id, name, role, nodeInfo, nodes.length, position.x, position.y, j, position.fixed) );
						// now add a link
						getLink(parent, nodes.length-1, dir);
						client++;

/*
	                    var container = QDRService.valFor(attrs, conns[j], "container");
	                    var parts = container.split('.')
	                    if (parts.length) {
	                        var city = parts[parts.length-1]
	                        if (city === 'TelAvivYafo')
	                            city = 'Tel Aviv-Yafo'
	                        if (possibleCities.indexOf(city) > -1) {
	                            if (cities.indexOf(city) == -1) {
	                                cities.push(city);
	                            }
	                        }
	                    } else {
	                        // there was no city
		                    var user = QDRService.valFor(attrs, conns[j], "user");
	                        city = 'Boston'
                            if (cities.indexOf(city) == -1 && role == 'normal' && user != "anonymous") {
                                cities.push(city);
                            }

	                    }
*/
					}
				}
				source++;
			}

            $scope.schema = QDRService.schema;
			// add a row for each attribute in .router attributeNames array
			for (var id in nodeInfo) {
				var onode = nodeInfo[id];

                initForm(onode['.connection'].attributeNames, onode['.connection'].results[0], QDRService.schema.entityTypes.connection, $scope.connAttributes);
                initForm(onode['.router'].attributeNames, onode['.router'].results[0], QDRService.schema.entityTypes.router, $scope.attributes);
                
				break;
			}
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
		}
/*
		function dragstart(d) {
		  d3.select(this).classed("fixed", d.fixed = true);
		}

		function dblclick(d) {
		  d3.select(this).classed("fixed", d.fixed = false);
		}
*/
        var initGlobe = function (clients) {
			d3.select(window)
				.on("mousemove", mousemove)
				.on("mouseup", mouseup);

			var width = 960,
				height = 500;

			var proj = d3.geo.orthographic()
				.scale(220)
				.translate([width / 2, height / 2])
				.clipAngle(90);

			var path = d3.geo.path().projection(proj).pointRadius(1.5);

			var links = [],
				arcLines = [];

			var graticule = d3.geo.graticule();
			d3.select("#geology svg").remove();
			var svg = d3.select("#geology").append("svg")
				.attr("width", width)
				.attr("height", height)
				.on("mousedown", mousedown);

			queue()
				.defer(d3.json, "plugin/data/world-110m.json")
				.defer(d3.json, "plugin/data/places1.json")
				.await(ready);

			function ready(error, world, places) {
			  var ocean_fill = svg.append("defs").append("radialGradient")
					.attr("id", "ocean_fill")
					.attr("cx", "75%")
					.attr("cy", "25%");
				  ocean_fill.append("stop").attr("offset", "5%").attr("stop-color", "#fff");
				  ocean_fill.append("stop").attr("offset", "100%").attr("stop-color", "#eef");

			  var globe_highlight = svg.append("defs").append("radialGradient")
					.attr("id", "globe_highlight")
					.attr("cx", "75%")
					.attr("cy", "25%");
				  globe_highlight.append("stop")
					.attr("offset", "5%").attr("stop-color", "#ffd")
					.attr("stop-opacity","0.6");
				  globe_highlight.append("stop")
					.attr("offset", "100%").attr("stop-color", "#ba9")
					.attr("stop-opacity","0.1");

			  var globe_shading = svg.append("defs").append("radialGradient")
					.attr("id", "globe_shading")
					.attr("cx", "55%")
					.attr("cy", "45%");
				  globe_shading.append("stop")
					.attr("offset","30%").attr("stop-color", "#fff")
					.attr("stop-opacity","0")
				  globe_shading.append("stop")
					.attr("offset","100%").attr("stop-color", "#505962")
					.attr("stop-opacity","0.2")

			  var drop_shadow = svg.append("defs").append("radialGradient")
					.attr("id", "drop_shadow")
					.attr("cx", "50%")
					.attr("cy", "50%");
				  drop_shadow.append("stop")
					.attr("offset","20%").attr("stop-color", "#000")
					.attr("stop-opacity",".5")
				  drop_shadow.append("stop")
					.attr("offset","100%").attr("stop-color", "#000")
					.attr("stop-opacity","0")

			  svg.append("ellipse")
				.attr("cx", 440).attr("cy", 450)
				.attr("rx", proj.scale()*.90)
				.attr("ry", proj.scale()*.25)
				.attr("class", "noclicks")
				.style("fill", "url("+urlPrefix+"#drop_shadow)");

			  svg.append("circle")
				.attr("cx", width / 2).attr("cy", height / 2)
				.attr("r", proj.scale())
				.attr("class", "noclicks")
				.style("fill", "url("+urlPrefix+"#ocean_fill)");

			  svg.append("path")
				.datum(topojson.object(world, world.objects.land))
				.attr("class", "land noclicks")
				.attr("d", path);

			  svg.append("path")
				.datum(graticule)
				.attr("class", "graticule noclicks")
				.attr("d", path);

			  svg.append("circle")
				.attr("cx", width / 2).attr("cy", height / 2)
				.attr("r", proj.scale())
				.attr("class","noclicks")
				.style("fill", "url("+urlPrefix+"#globe_highlight)");

			  svg.append("circle")
				.attr("cx", width / 2).attr("cy", height / 2)
				.attr("r", proj.scale())
				.attr("class","noclicks")
				.style("fill", "url("+urlPrefix+"#globe_shading)");

			  var filtered = places.features.filter( function (feature) {
			    return clients.indexOf(feature.properties.NAME) > -1
			  })
			  svg.append("g").attr("class","points")
				  .selectAll("text").data(filtered)
				.enter().append("path")
				  .attr("class", "point")
				  .attr("d", path);

				svg.append("g").attr("class","labels")
				  .selectAll("text").data(filtered)
				  .enter().append("text")
				  .attr("class", "label")
				  .text(function(d) { return d.properties.NAME })

				position_labels();

			  // spawn links between cities as source/target coord pairs
			  places.features.forEach(function(a, i) {
				if (clients.indexOf(a.properties.NAME) > -1) {
					places.features.forEach(function(b, j) {
					  if (b.properties.NAME === 'Raleigh') {
					  if (j > i) {	// avoid duplicates
						links.push({
						  source: a.geometry.coordinates,
						  target: b.geometry.coordinates
						});
					  }
					  }
					});
				}
			  });

			  // build geoJSON features from links array
			  links.forEach(function(e,i,a) {
				var feature =   { "type": "Feature", "geometry": { "type": "LineString", "coordinates": [e.source,e.target] }}
				arcLines.push(feature)
			  })

			  svg.append("g").attr("class","arcs")
				.selectAll("path").data(arcLines)
				.enter().append("path")
				  .attr("class","arc")
				  .attr("d",path)
			  refresh();
			}

			function position_labels() {
			  var centerPos = proj.invert([width/2,height/2]);

			  var arc = d3.geo.greatArc();

			  svg.selectAll(".label")
				.attr("transform", function(d) {
				  var loc = proj(d.geometry.coordinates),
					x = loc[0],
					y = loc[1];
				  var offset = x < width/2 ? -5 : 5;
				  return "translate(" + (x+offset) + "," + (y-2) + ")"
				})
				.style("display",function(d) {
				  var d = arc.distance({source: d.geometry.coordinates, target: centerPos});
				  return (d > 1.57) ? 'none' : 'inline';
				})
			}

			function refresh() {
			  svg.selectAll(".land").attr("d", path);
			  svg.selectAll(".point").attr("d", path);
			  svg.selectAll(".graticule").attr("d", path);
			  svg.selectAll(".arc").attr("d", path);
			  position_labels();
			}

			// modified from http://bl.ocks.org/1392560
			var m0, o0;
			o0 = angular.fromJson(localStorage["QDR.rotate"]);
			if (o0)
				proj.rotate(o0);

			function mousedown() {
			  m0 = [d3.event.pageX, d3.event.pageY];
			  o0 = proj.rotate();
			  d3.event.preventDefault();
			}
			function mousemove() {
			  if (m0) {
				var m1 = [d3.event.pageX, d3.event.pageY]
				  , o1 = [o0[0] + (m1[0] - m0[0]) / 6, o0[1] + (m0[1] - m1[1]) / 6];
				o1[1] = o1[1] > 30  ? 30  :
						o1[1] < -30 ? -30 :
						o1[1];
				proj.rotate(o1);
				refresh();
			  }
			}
			function mouseup() {
			  if (m0) {
				mousemove();
				m0 = null;
				localStorage["QDR.rotate"] = angular.toJson(proj.rotate());
			  }
			}
        }

        // called when we mouseover a node
        // we need to update the table
		function updateNodeForm (d) {
			//QDR.log.debug("update form info for ");
			//console.dump(d);
			var nodeInfo = QDRService.topology.nodeInfo();
			var onode = nodeInfo[d.key];
			if (onode) {
				var nodeResults = onode['.router'].results[0];
				var nodeAttributes = onode['.router'].attributeNames;

                for (var i=0; i<$scope.attributes.length; ++i) {
                    var idx = nodeAttributes.indexOf($scope.attributes[i].attributeName);
                    if (idx > -1) {
                        if ($scope.attributes[i].attributeValue != nodeResults[idx]) {
                            // highlight the changed data
                            $scope.attributes[i].attributeValue = nodeResults[idx];

                        }
                    }
                }
			}
            $scope.topoForm = "general";
            $scope.$apply();
		}

		function updateConnForm (d, resultIndex) {
			var nodeInfo = QDRService.topology.nodeInfo();
			var onode = nodeInfo[d.key];
			if (onode && onode['.connection']) {
				var nodeResults = onode['.connection'].results[resultIndex];
				var nodeAttributes = onode['.connection'].attributeNames;

                for (var i=0; i<$scope.connAttributes.length; ++i) {
                    var idx = nodeAttributes.indexOf($scope.connAttributes[i].attributeName);
                    if (idx > -1) {
                    	try {
                        if ($scope.connAttributes[i].attributeValue != nodeResults[idx]) {
                            // highlight the changed data
                            $scope.connAttributes[i].attributeValue = nodeResults[idx];

                        }
                        } catch (err) {
							QDR.log.error("error updating form" + err)
                        }
                    }
                }
			}
            $scope.topoForm = "connections";
            $scope.$apply();
		}


        function getContainerIndex(_id) {
            var nodeIndex = 0;
            var nodeInfo = QDRService.topology.nodeInfo();
            for (var id in nodeInfo) {
                var node = nodeInfo[id];
                if (node['.container'].results[0][0] == _id)
                    return nodeIndex;
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

	    // takes the nodes and links array of objects and adds svg elements for everything that hasn't already
	    // been added
	    function restart(start) {
	        circle.call(force.drag);
	        //svg.classed('ctrl', true);

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
                            if (name == right.name) {
                                break;
                            }
                        }
                        // did not find connection. this is a connection to a non-interrouter node
                        if (resultIndex === onode['.connection'].results.length) {
                            // use the non-interrouter node's connection info
                            left = d.target;
                            resultIndex = left.resultIndex;
                        }
                        updateConnForm(left, resultIndex);
                    }

					// select link
					mousedown_link = d;
					selected_link = mousedown_link;
					//selected_node = null;
					//mousedown_node = null;
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
					// select link
					selected_link = null;
					//selected_node = null;
					//mousedown_node = null;
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
                .on("dblclick", function (d) {
                    var pos = d3.mouse(this);
                    var diameter = 400;
                    var format = d3.format(",d");
                    var pack = d3.layout.pack()
                        .size([diameter - 4, diameter - 4])
                        .padding(3)
                        .value(function(d) { return d.size; });

                    var svg = d3.select("#crosssection").append("svg")
                        .attr("width", diameter)
                        .attr("height", diameter);
                    var svgg = svg.append("g")
                        .attr("transform", "translate(2,2)");

					svg.on('click', function (d) {
		                d3.select("#crosssection").style("display","none");
					})

					var root = {
						name: "links between " + d.source.name + " and " + d.target.name,
						children: []
					}
					var nodeInfo = QDRService.topology.nodeInfo();
					var connections = nodeInfo[d.source.key]['.connection'];
					var containerIndex = connections.attributeNames.indexOf('container');
					connections.results.some ( function (connection) {
                        if (connection[containerIndex] == d.target.containerName) {
                            root.attributeNames = connections.attributeNames;
                            root.obj = connection;
                            root.desc = "Connection";
                            return true;    // stop looping after 1 match
                        }
                        return false;
                    })

					// find router.links where link.remoteContainer is d.source.name
					var links = nodeInfo[d.source.key]['.router.link'];
					containerIndex = links.attributeNames.indexOf('remoteContainer');
					var nameIndex = links.attributeNames.indexOf('name');
					var linkDirIndex = links.attributeNames.indexOf('linkDir');
					links.results.forEach ( function (link) {
						if (link[containerIndex] == d.target.containerName)
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
	                      .attr("transform", function(d) { return "translate(" + d.x + "," + d.y + ")"; })
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
	                d3.select("#crosssection").style("display","block");
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
	        var g = circle.enter().append('svg:g');

			// add new circles and set their attr/class/behavior
	        g.append('svg:circle')
	            .attr('class', 'node')
	            .attr('r', function (d) {
	            	return radii[d.nodeType];
	            })
	            .classed('fixed', function (d) {return d.fixed})
  			    .classed('temp', function(d) { return QDRService.nameFromId(d.key) == '__internal__'; } )
  			    .classed('normal', function(d) { return d.nodeType == 'normal' } )
  			    .classed('inter-router', function(d) { return d.nodeType == 'inter-router' } )
  			    .classed('on-demand', function(d) { return d.nodeType == 'on-demand' } )

/*
	            .style('fill', function (d) {
	                var sColor = colors[d.nodeType];
	                return (d === selected_node) ? d3.rgb(sColor).brighter().toString() : d3.rgb(sColor);
	            })
	            .style('stroke', function (d) {
	                var sColor = colors[d.nodeType];
	                return d3.rgb(sColor).darker().toString();
	            })
*/
	            .on('mouseover', function (d) {
	                if ($scope.addingNode.step > 0) {
		                d3.select(this).attr('transform', 'scale(1.1)');
						return;
	                }
					if (!selected_node) {
                        if (d.nodeType === 'inter-router') {
                            //QDR.log.debug("showing general form");
                            updateNodeForm(d);
                        } else if (d.nodeType === 'normal' || d.nodeType === 'on-demand') {
                            //QDR.log.debug("showing connections form");
                            updateConnForm(d, d.resultIndex);
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
						$scope.$apply();
						// add new elements to the svg
						force.links(links).start();
						restart();
						return;

	                }

					// if this node was selected, unselect it
                    if (mousedown_node === selected_node) {
                        selected_node = null;
                        $scope.topoFormSelected = "";
                    }
                    else {
                        selected_node = mousedown_node;
                        if (d.nodeType === 'inter-router') {
                            //QDR.log.debug("showing general form");
                            updateNodeForm(d);
                            $scope.topoFormSelected = "general";
                        } else if (d.nodeType === 'normal' || d.nodeType === 'on-demand') {
                            //QDR.log.debug("showing connections form");
                            updateConnForm(d, d.resultIndex);
                            $scope.topoFormSelected = "connections";
                        }
                    }
                    for (var i=0; i<links.length; ++i) {
                        links[i]['highlighted'] = false;
                    }
	                mousedown_node = null;
                    $scope.$apply();
                    restart(false);

	            })
	            .on("dblclick", function (d) {
	                if (d.fixed) {
						d3.select(this).classed("fixed", d.fixed = false);
						force.start();  // let the nodes move to a new position
	                }
	                if (QDRService.nameFromId(d.key) == '__internal__') {
	                    editNode();
	                    $scope.$apply();
	                }
	            })
	            .on("contextmenu", function(d) {
	                $(document).click();
                    d3.event.preventDefault();
	                $scope.contextNode = d;
	                $scope.$apply();    // we just changed a scope valiable during an async event
                    d3.select('#node_context_menu')
                      .style('left', (mouseX + $(document).scrollLeft()) + "px")
                      .style('top', (mouseY + $(document).scrollTop()) + "px")
                      .style('display', 'block');

                });

	        // show node IDs
	        g.append('svg:text')
	            .attr('x', 0)
	            .attr('y', 4)
	            .attr('class', 'id')
	            .text(function (d) {
	                return (d.nodeType === 'normal' || d.nodeType == 'on-demand') ? d.name.slice(-1) :
	                    d.name.length>7 ? d.name.substr(0,6)+'...' : d.name;
	        });

	        // remove old nodes
	        circle.exit().remove();

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
                animate = true;
                initForceGraph();
                initGlobe(cities);
                //if ($location.path().startsWith("/topology"))
                //    Core.notification('info', "Qpid dispatch router topology changed");

            } else {
                //QDR.log.debug("no changes")
            }
        });

		function hasChanged () {
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

		initGlobe(cities)//, "Boston", "Tel Aviv-Yafo"]);

	    function doAddDialog(NewRouterName) {
		    var modalInstance = $uibModal.open({
		        animation: true,
		        controller: 'QDR.NodeDialogController',
		        templateUrl: 'node-config-template.html',
		        size: 'lg',
		        resolve: {
		            newname: function () {
		                return NewRouterName;
		            }
		        }
		    });
		    modalInstance.result.then(function (result) {
				if (result)
					setTimeout(doDownloadDialog, 100, result);
		    });
        };

	    function doDownloadDialog(result) {
		    var modalInstance = $uibModal.open({
		        animation: true,
				controller: 'QDR.DownloadDialogController',
		        templateUrl: 'download-dialog-template.html',
		        resolve: {
		            results: function () {
		                return result;
		            }
		        }
		    });
        };
  }]);

  QDR.module.controller("QDR.NodeDialogController", function($scope, QDRService, $uibModalInstance, newname) {
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

        $scope.cancel = function () {
            $uibModalInstance.close()
        };
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
	        $uibModalInstance.close({entities: $scope.entities, annotations: annotations});
        }

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

QDR.module.controller("QDR.DownloadDialogController", function($scope, QDRService, $templateCache, $window, $uibModalInstance, results) {

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
	        $uibModalInstance.close();
		}
});

  return QDR;
}(QDR || {}));

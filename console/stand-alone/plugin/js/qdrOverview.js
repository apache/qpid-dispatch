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
/**
 * @module QDR
 */
var QDR = (function (QDR) {

  /**
   * @method OverviewController
   * @param $scope
   * @param QDRService
   * @param QDRChartServer
   * dialogServer
   * $location
   *
   * Controller that handles the QDR overview page
   */
	QDR.module.controller("QDR.OverviewController", ['$scope', 'QDRService', '$location', '$timeout', function($scope, QDRService, $location, $timeout) {

		console.log("QDR.OverviewControll started with location of " + $location.path() + " and connection of  " + QDRService.connected);
		var columnStateKey = 'QDRColumnKey.';
		var OverviewExpandedKey = "QDROverviewExpanded"
		var OverviewActivatedKey = "QDROverviewActivated"
		var treeRoot;   // the dynatree root node. initialized once log data is received

		// we want attributes to be listed first, so add it at index 0
		$scope.subLevelTabs = [{
		    content: '<i class="icon-list"></i> Attributes',
		    title: "View the attribute values on your selection",
		    isValid: function (workspace) { return true; },
		    href: function () { return "#/" + QDR.pluginName + "/attributes"; },
		    index: 0
		},
		{
		    content: '<i class="icon-leaf"></i> Operations',
		    title: "Execute operations on your selection",
		    isValid: function (workspace) { return true; },
		    href: function () { return "#/" + QDR.pluginName + "/operations"; },
		    index: 1
		}]
		$scope.activeTab = $scope.subLevelTabs[0];
		$scope.setActive = function (nav) {
			$scope.activeTab = nav;
		}
		$scope.isValid = function (nav) {
			return nav.isValid()
		}
		$scope.isActive = function (nav) {
			return nav == $scope.activeTab;
		}
		$scope.linkFields = []
		$scope.link = null;
		var refreshInterval = 5000
	    $scope.modes = [
	    	{title: 'Overview', name: 'Overview', right: false}
	    	];

		$scope.tmplOverviewTree = QDR.templatePath + 'tmplOverviewTree.html';
		$scope.templates =
		[     { name: 'Routers', url: 'routers.html'},
		      { name: 'Router', url: 'router.html'},
              { name: 'Addresses', url: 'addresses.html'},
		      { name: 'Address', url: 'address.html'},
              { name: 'Links', url: 'links.html'},
		      { name: 'Link', url: 'link.html'},
              { name: 'Connections', url: 'connections.html'},
		      { name: 'Connection', url: 'connection.html'},
              { name: 'Logs', url: 'logs.html'},
              { name: 'Log', url: 'log.html'}
        ];
		var topLevelChildren = [];

		$scope.allRouterSelected = function (row ) {
			console.log("row selected" + row)
		}
		function afterSelectionChange(rowItem, checkAll) {
			var nodeId = rowItem.entity.nodeId;
			$("#overtree").dynatree("getTree").activateKey(nodeId);
        }

		$scope.allRouterFields = [];
		$scope.allRouterSelections = [];
		$scope.allRouters = {
		    saveKey: 'allRouters',
			data: 'allRouterFields',
			columnDefs: [
                 {
                     field: 'routerId',
                     saveKey: 'allRouters',
                     displayName: 'Router'
                 },
                 {
                     field: 'area',
                     displayName: 'Area'
                 },
                 {
                     field: 'mode',
                     displayName: 'Mode'
                 },
                 {
                     field: 'connections',
                     displayName: 'External connections'
                 },
                 {
                     field: 'addrCount',
                     displayName: 'Address count'
                 },
                 {
                     field: 'linkCount',
                     displayName: 'Link count'
                 }
            ],
			enableColumnResize: true,
			multiSelect: false,
			selectedItems: $scope.allRouterSelections,
			afterSelectionChange: function(data) {
				if (data.selected) {
					var selItem = $scope.allRouterSelections[0]
					var nodeId = selItem.nodeId
					// activate Routers->nodeId in the tree
					$("#overtree").dynatree("getTree").activateKey(nodeId);

				}
            }
		};

		// get info for all routers
		var allRouterInfo = function () {
			var nodeIds = QDRService.nodeIdList()
			var expected = Object.keys(nodeIds).length
			var received = 0;
			var allRouterFields = [];
			var gotNodeInfo = function (nodeName, entity, response) {
				var results = response.results;
				var name = QDRService.nameFromId(nodeName)
				var connections = 0;
				results.forEach( function (result) {
					var role = QDRService.valFor(response.attributeNames, result, "role")
					if (role != 'inter-router') {
						++connections
					}
				})
				allRouterFields.push({routerId: name, connections: connections, nodeId: nodeName})
				++received
				if (expected == received) {
					allRouterFields.sort ( function (a,b) { return a.routerId < b.routerId ? -1 : a.routerId > b.routerId ? 1 : 0})
					// now get each router's node info
					QDRService.getMultipleNodeInfo(nodeIds, "router", [], function (nodeIds, entity, responses) {
						for(var r in responses) {
							var result = responses[r]
							var routerId = QDRService.valFor(result.attributeNames, result.results[0], "routerId")
							allRouterFields.some( function (connField) {
								if (routerId === connField.routerId) {
									result.attributeNames.forEach ( function (attrName) {
										connField[attrName] = QDRService.valFor(result.attributeNames, result.results[0], attrName)
									})
									return true
								}
								return false
							})
						}
						$scope.allRouterFields = allRouterFields
					}, nodeIds[0], false)
				}
			}
			nodeIds.forEach ( function (nodeId, i) {
				QDRService.getNodeInfo(nodeId, ".connection", ["role"], gotNodeInfo)
			})
			loadColState($scope.allRouters)
			updateRouterTree(nodeIds)
		}

		$scope.routerFields = []
        $scope.routerGrid = {
            saveKey: 'routerGrid',
            data: 'routerFields',
            columnDefs: [
                 {
                     field: 'attribute',
                     displayName: 'Attribute',
                     saveKey: 'routerGrid',
                     width: '40%'
                 },
                 {
                     field: 'value',
                     displayName: 'Value',
                     width: '40%'
                 }
            ],
            enableColumnResize: true,
            multiSelect: false
        }

		// get info for a single router
		var routerInfo = function (node) {
			$scope.router = node

			$scope.routerFields = [];
			$scope.allRouterFields.some( function (field) {
				if (field.routerId === node.data.title) {
					Object.keys(field).forEach ( function (key) {
						if (key !== '$$hashKey')
							$scope.routerFields.push({attribute: key, value: field[key]})
					})
					return true
				}
			})
	        loadColState($scope.routerGrid);
		}

		$scope.addressesData = []
		$scope.selectedAddresses = []
		$scope.addressesGrid = {
			saveKey: 'addressesGrid',
			data: 'addressesData',
			columnDefs: [
                 {
                     field: 'address',
                    saveKey: 'addressesGrid',
                     displayName: 'address'
                 },
                 {
                     field: 'class',
                     displayName: 'class'
                 },
                 {
                     field: 'phase',
                     displayName: 'phase',
                     cellClass: 'grid-align-value'
                 },
                 {
                     field: 'inproc',
                     displayName: 'in-proc'
                 },
                 {
                     field: 'local',
                     displayName: 'local',
                     cellClass: 'grid-align-value'
                 },
                 {
                     field: 'remote',
                     displayName: 'remote',
                     cellClass: 'grid-align-value'
                 },
                 {
                     field: 'in',
                     displayName: 'in',
                     cellClass: 'grid-align-value'
                 },
                 {
                     field: 'out',
                     displayName: 'out',
                     cellClass: 'grid-align-value'
                 }
            ],
			enableColumnResize: true,
			multiSelect: false,
			selectedItems: $scope.selectedAddresses,
			afterSelectionChange: function(data) {
				if (data.selected) {
					var selItem = data.entity;
					var nodeId = selItem.uid
					$("#overtree").dynatree("getTree").activateKey(nodeId);
				}
            }
		};

		// get info for all addresses
		var allAddressInfo = function () {
			var gotAllAddressFields = function ( addressFields ) {
				// update the grid's data
				addressFields.sort ( function (a,b) { return a.address < b.address ? -1 : a.address > b.address ? 1 : 0})
				addressFields[0].title = addressFields[0].address
				for (var i=1; i<addressFields.length; ++i) {
					if (addressFields[i].address === addressFields[i-1].address) {
						addressFields[i-1].title = addressFields[i-1].address + " (" + addressFields[i-1]['class'] + ")"
						addressFields[i].title = addressFields[i].address + " (" + addressFields[i]['class'] + ")"
					} else
						addressFields[i].title = addressFields[i].address
				}
				$scope.addressesData = addressFields

				// repopulate the tree's child nodes
				updateAddressTree(addressFields)
			}
			getAllAddressFields(gotAllAddressFields)
	        loadColState($scope.addressesGrid);
		}

		var getAllAddressFields = function (callback) {
			var nodeIds = QDRService.nodeIdList()
			var addr_class = function (addr) {
				if (!addr) return "-"
		        if (addr[0] == 'M')  return "mobile"
		        if (addr[0] == 'R')  return "router"
		        if (addr[0] == 'A')  return "area"
		        if (addr[0] == 'L')  return "local"
		        if (addr[0] == 'C')  return "link-incoming"
		        if (addr[0] == 'D')  return "link-outgoing"
		        return "unknown: " + addr[0]
			}

			var addr_text = function (addr) {
		        if (!addr)
		            return "-"
		        if (addr[0] == 'M')
		            return addr.substring(2)
		        else
		            return addr.substring(1)
			}

			var addr_phase = function (addr) {
		        if (!addr)
		            return "-"
		        if (addr[0] == 'M')
		            return addr[1]
		        return ''
			}

			var identity_clean = function (identity) {
		        if (!identity)
		            return "-"
		        var pos = identity.indexOf('/')
		        if (pos >= 0)
		            return identity.substring(pos + 1)
		        return identity
			}

			var addressFields = []
			QDRService.getMultipleNodeInfo(nodeIds, "router.address", [], function (nodeIds, entity, response) {
				response.aggregates.forEach( function (result) {
					var prettySum = function (field) {
						var fieldIndex = response.attributeNames.indexOf(field)
						if (fieldIndex < 0) {
							return "-"
						}
						var val = result[fieldIndex].sum
						return QDRService.pretty(val)
					}

					var uid = QDRService.valFor(response.attributeNames, result, "identity").sum
					var identity = identity_clean(uid)

					addressFields.push({
						address: addr_text(identity),
						'class': addr_class(identity),
						phase:   addr_phase(identity),
						inproc:  prettySum("inProcess"),
						local:   prettySum("subscriberCount"),
						remote:  prettySum("remoteCount"),
						'in':    prettySum("deliveriesIngress"),
						out:     prettySum("deliveriesEgress"),
						thru:    prettySum("deliveriesTransit"),
						toproc:  prettySum("deliveriesToContainer"),
						fromproc:prettySum("deliveriesFromContainer"),
						uid:     uid
					})
				})
				callback(addressFields)
			}, nodeIds[0])
		}

		var updateLinkGrid = function ( linkFields ) {
			$scope.linkFields = linkFields
			// if we have a selected link
			if ($scope.link) {
				// find the selected link in the array of all links
				var links = $scope.linkFields.filter(function (link) {
					return link.name === $scope.link.data.fields.name;
				})
				if (links.length > 0) {
					// linkInfo() is the function that is called by dynatree when a link is selected
					// It is passed a dynatree node. We need to simulate that node type to update the link grid
					linkInfo({data: {title: links[0].title, fields: links[0]}})
				}
			}
		}

		// get info for a all links
		$scope.selectedLinks = []
		$scope.linksGrid = {
			saveKey: 'linksGrid',
			data: 'linkFields',
			columnDefs: [
                 {
                     field: 'link',
                     displayName: 'Link',
                     groupable:	false,
                     saveKey: 'linksGrid',
                     width: '12%'
                 },
                 {
                     field: 'linkType',
                     displayName: 'Link type',
                     groupable:	false,
                     width: '10%'
                 },
                 {
                     field: 'linkDir',
                     displayName: 'Link dir',
                     groupable:	false,
                     width: '8%'
                 },
                 {
                     field: 'adminStatus',
                     displayName: 'Admin status',
                     groupable:	false,
                     width: '10%'
                 },
                 {
                     field: 'operStatus',
                     displayName: 'Oper status',
                     groupable:	false,
                     width: '10%'
                 },
                 {
                     field: 'deliveryCount',
                     displayName: 'Delivery Count',
                     groupable:	false,
                     cellClass: 'grid-align-value',
                     width: '12%'
                 },
                 {
                     field: 'rate',
                     displayName: 'Rate',
                     groupable:	false,
                     cellClass: 'grid-align-value',
                     width: '8%'
                 },
                 {
                     field: 'uncounts',
                     displayName: 'Outstanding',
                     groupable:	false,
                     cellClass: 'grid-align-value',
                     width: '10%'
                 },
                 {
                     field: 'owningAddr',
                     displayName: 'Address',
                     groupable:	false,
                     width: '20%'
                 }
            ],
			enableColumnResize: true,
			enableColumnReordering: true,
			showColumnMenu: true,
			rowTemplate: 'linkRowTemplate.html',
			// aggregateTemplate: "linkAggTemplate.html",
	        multiSelect: false,
			selectedItems: $scope.selectedLinks,
			afterSelectionChange: function(data) {
				if (data.selected) {
					var selItem = data.entity;
					var nodeId = selItem.uid
					$("#overtree").dynatree("getTree").activateKey(nodeId);
				}
            }
		};


		$scope.$on('ngGridEventColumns', function (e, columns) {
			var saveInfo = columns.map( function (col) {
				return [col.width, col.visible]
			})
			var saveKey = columns[0].colDef.saveKey
			if (saveKey)
                localStorage.setItem(columnStateKey+saveKey, JSON.stringify(saveInfo));
        })

        var loadColState = function (grid) {
            if (!grid)
                return;
            var columns = localStorage.getItem(columnStateKey+grid.saveKey);
            if (columns) {
                var cols = JSON.parse(columns);
                cols.forEach( function (col, index) {
                    grid.columnDefs[index].width = col[0];
                    grid.columnDefs[index].visible = col[1]
                })
            }
        }
		var allLinkInfo = function () {
			getAllLinkFields([updateLinkGrid, updateLinkTree])
	        loadColState($scope.linksGrid);
		}

		var getAllLinkFields = function (callbacks) {
			var nodeIds = QDRService.nodeIdList()
			var linkFields = []
			var now = Date.now()
			var rate = function (response, result) {
				var name = QDRService.valFor(response.attributeNames, result, "linkName").sum
				var oldname = $scope.linkFields.filter(function (link) {
					return link.linkName === name
				})
				if (oldname.length > 0) {
					var elapsed = (now - oldname[0].timestamp) / 1000;
					var delivered = QDRService.valFor(response.attributeNames, result, "deliveryCount").sum - oldname[0].rawDeliveryCount
					//QDR.log.debug("elapsed " + elapsed + " delivered " + delivered)
					return elapsed > 0 ? parseFloat(Math.round((delivered/elapsed) * 100) / 100).toFixed(2) : 0;
				} else {
					//QDR.log.debug("unable to find old linkName")
					return 0
				}
			}
			QDRService.getMultipleNodeInfo(nodeIds, "router.link", [], function (nodeIds, entity, response) {
				response.aggregates.forEach( function (result) {
					var prettySum = function (field) {
						var fieldIndex = response.attributeNames.indexOf(field)
						if (fieldIndex < 0) {
							return "-"
						}
						var val = result[fieldIndex].sum
						return QDRService.pretty(val)
					}
					var uncounts = function () {
						var und = QDRService.valFor(response.attributeNames, result, "undeliveredCount").sum
						var uns = QDRService.valFor(response.attributeNames, result, "unsettledCount").sum
						return und + uns
					}
					var nameIndex = response.attributeNames.indexOf('name')
					var linkName = function () {
						var details = result[nameIndex].detail
						var names = []
						details.forEach( function (detail) {
							if (detail.node.endsWith(':'))
								names.push(detail.node.slice(0, -1))
							else
								names.push(detail.node)
						})
						//var namestr = names.join('-')
						var namestr = names.length > 0 ? names[0] : ""
						return namestr + ':' + QDRService.valFor(response.attributeNames, result, "identity").sum
					}
					var fixAddress = function () {
						var owningAddr = QDRService.valFor(response.attributeNames, result, "owningAddr").sum || ""
						/*
						     - "L*"  =>  "* (local)"
						     - "M0*" =>  "* (direct)"
						     - "M1*" =>  "* (dequeue)"
						     - "MX*" =>  "* (phase X)"
						*/
						var address = undefined;
						var starts = {'L': '(local)', 'M0': '(direct)', 'M1': '(dequeue)'}
						for (var start in starts) {
							if (owningAddr.startsWith(start)) {
								var ends = owningAddr.substr(start.length)
								address = ends + " " + starts[start]
								break;
							}
						}
						if (!address) {
							// check for MX*
							if (owningAddr.length > 3) {
								if (owningAddr[0] === 'M') {
									var phase = parseInt(owningAddress.substr(1))
									if (phase && !isNaN(phase)) {
										var phaseStr = phase + "";
										var star = owningAddress.substr(2 + phaseStr.length)
										address = star + " " + "(phase " + phaseStr + ")"
									}
								}
							}
						}
						return address || owningAddr;
					}

					var adminStatus = QDRService.valFor(response.attributeNames, result, "adminStatus").sum
					var operStatus = QDRService.valFor(response.attributeNames, result, "operStatus").sum
					var linkName = linkName()
					var linkType = QDRService.valFor(response.attributeNames, result, "linkType").sum
					if ($scope.currentLinkFilter === "" || $scope.currentLinkFilter === linkType) {
						linkFields.push({
							link:       linkName,
							title:      linkName,
							uncounts:   uncounts(),
							operStatus: operStatus,
							adminStatus:adminStatus,
							owningAddr: fixAddress(),
							deliveryCount:prettySum("deliveryCount") + " ",
							rawDeliveryCount: QDRService.valFor(response.attributeNames, result, "deliveryCount").sum,
							name: QDRService.valFor(response.attributeNames, result, "name").sum,
							linkName: QDRService.valFor(response.attributeNames, result, "linkName").sum,
							capacity: QDRService.valFor(response.attributeNames, result, "capacity").sum,
							connectionId: QDRService.valFor(response.attributeNames, result, "connectionId").sum,
							linkDir: QDRService.valFor(response.attributeNames, result, "linkDir").sum,
							linkType: linkType,
							peer: QDRService.valFor(response.attributeNames, result, "peer").sum,
							type: QDRService.valFor(response.attributeNames, result, "type").sum,
							undeliveredCount: QDRService.valFor(response.attributeNames, result, "undeliveredCount").sum,
							unsettledCount: QDRService.valFor(response.attributeNames, result, "unsettledCount").sum,
							uid:     linkName,
							timestamp: now,
							rate: rate(response, result)
						})
					}
				})
				callbacks.forEach( function (cb) {
					cb(linkFields)
				})
			}, nodeIds[0])
		}

		$scope.allConnectionFields = []
		$scope.allConnectionSelections = [];
		$scope.allConnectionGrid = {
			saveKey: 'allConnGrid',
            data: 'allConnectionFields',
            columnDefs: [
                 {
                     field: 'host',
                    saveKey: 'allConnGrid',
                     displayName: 'host'
                 },
                 {
                     field: 'container',
                     displayName: 'container'
                 },
                 {
                     field: 'role',
                     displayName: 'role'
                 },
                 {
                     field: 'dir',
                     displayName: 'dir'
                 },
                 {
                     field: 'security',
                     displayName: 'security'
                 },
                 {
                     field: 'authentication',
                     displayName: 'authentication'
                 }
            ],
            enableColumnResize: true,
            multiSelect: false,
            selectedItems: $scope.allConnectionSelections,
            afterSelectionChange: function(data) {
                if (data.selected) {
                    var selItem = $scope.allConnectionSelections[0]
                    var nodeId = selItem.host
                    // activate Routers->nodeId in the tree
                    $("#overtree").dynatree("getTree").activateKey(nodeId);
                }
            }
        };
		// get info for a all connections
		var allConnectionInfo = function () {
			var nodeIds = QDRService.nodeIdList()
			updateConnectionTree(nodeIds)
			$scope.allConnectionFields = [];;
            var connectionNodes = $("#overtree").dynatree("getTree").getNodeByKey('Connections').getChildren()
			connectionNodes.forEach( function (connection) {
				$scope.allConnectionFields.push(connection.data.fields)
			})
	        loadColState($scope.allConnectionGrid);
		}

		$scope.addressFields = []
		$scope.addressGrid = {
			saveKey: 'addGrid',
			data: 'addressFields',
			columnDefs: [
                 {
                     field: 'attribute',
                     displayName: 'Attribute',
                     saveKey: 'addGrid',
                     width: '40%'
                 },
                 {
                     field: 'value',
                     displayName: 'Value',
                     width: '40%'
                 }
            ],
			enableColumnResize: true,
			multiSelect: false
		}

		// get info for a single address
		var addressInfo = function (address) {
			if (!address)
				return;
			$scope.address = address
			$scope.addressFields = [];

			var fields = Object.keys(address.data.fields)
			fields.forEach( function (field) {
				if (field != "title" && field != "uid")
					$scope.addressFields.push({attribute: field, value: address.data.fields[field]})
			})
	        loadColState($scope.addressGrid);
		}

		$scope.singleLinkFields = []
		$scope.linkGrid = {
		    saveKey: 'linkGrid',
			data: 'singleLinkFields',
			columnDefs: [
                 {
                    field: 'attribute',
                    displayName: 'Attribute',
                     width: '40%'
                 },
                 {
                    field: 'value',
                    displayName: 'Value',
                    width: '40%'
                 }
            ],
			enableColumnResize: true,
			multiSelect: false
		}

		// display the grid detail info for a single link
		var linkInfo = function (link) {
			if (!link)
				return;
			$scope.link = link

			$scope.singleLinkFields = [];
			var fields = Object.keys(link.data.fields)
			var excludeFields = ["title", "uid", "uncounts", "rawDeliveryCount", "timestamp"]
			fields.forEach( function (field) {
				if (excludeFields.indexOf(field) == -1)
					$scope.singleLinkFields.push({attribute: field, value: link.data.fields[field]})
			})
	        loadColState($scope.linkGrid);
		}

		// get info for a single connection
		$scope.connectionModes = [{
	        content: '<a><i class="icon-list"></i> Attriutes</a>',
			id: 'attributes',
			title: "View connection attributes",
	        isValid: function () { return true; }
	    },
	    {
	        content: '<a><i class="icon-link"></i> Links</a>',
	        id: 'links',
	        title: "Show links",
	        isValid: function () { return true }
	    }
	    ];
        $scope.currentMode = $scope.connectionModes[0];
		$scope.isModeSelected = function (mode) {
			return mode === $scope.currentMode;
		}
		$scope.connectionLinks = [];
		var updateConnectionLinks = function () {
			var n = $scope.connection.data.fields
			var key = n.routerId
			var nodeInfo = QDRService.topology.nodeInfo();
			var links = nodeInfo[key]['.router.link'];
			var linkTypeIndex = links.attributeNames.indexOf('linkType');
			var connectionIdIndex = links.attributeNames.indexOf('connectionId');
			$scope.connectionLinks = [];
			links.results.forEach( function (link) {
				if (link[linkTypeIndex] === 'endpoint' && link[connectionIdIndex] === n.identity) {
					var l = {};
					l.owningAddr = QDRService.valFor(links.attributeNames, link, 'owningAddr');
					l.dir = QDRService.valFor(links.attributeNames, link, 'linkDir');
					if (l.owningAddr && l.owningAddr.length > 2)
						if (l.owningAddr[0] === 'M')
							l.owningAddr = l.owningAddr.substr(2)
						else
							l.owningAddr = l.owningAddr.substr(1)

					l.adminStatus = QDRService.valFor(links.attributeNames, link, 'adminStatus');
					l.operStatus = QDRService.valFor(links.attributeNames, link, 'operStatus');
					l.identity = QDRService.valFor(links.attributeNames, link, 'identity')
					l.connectionId = QDRService.valFor(links.attributeNames, link, 'connectionId')
/*
						l.deliveryCount = QDRService.pretty(QDRService.valFor(links.attributeNames, link, 'deliveryCount'));
						l.undeliveredCount = QDRService.pretty(QDRService.valFor(links.attributeNames, link, 'undeliveredCount'));
						l.unsettledCount = QDRService.pretty(QDRService.valFor(links.attributeNames, link, 'unsettledCount'));
*/
					// ---------------------------------------------
					// TODO: remove this fake quiescing/reviving logic when the routers do the work
					if ($scope.quiesceState.linkStates[l.identity])
						l.adminStatus = $scope.quiesceState.linkStates[l.identity];
					if ($scope.quiesceState.operStates[l.identity])
						l.operStatus = $scope.quiesceState.operStates[l.identity];
					if ($scope.quiesceState.state == 'quiescing') {
						if (l.adminStatus === 'enabled') {
							var chance = Math.floor(Math.random() * 2);
							if (chance == 1) {
								l.adminStatus = 'disabled';
								$scope.quiesceState.linkStates[l.identity] = 'disabled';
								$scope.quiesceState.operStates[l.identity] = 'idle';
							}
						}
					}
					if ($scope.quiesceState.state == 'reviving') {
						if (l.adminStatus === 'disabled') {
							var chance = Math.floor(Math.random() * 2);
							if (chance == 1) {
								l.adminStatus = 'enabled';
								$scope.quiesceState.linkStates[l.identity] = 'enabled';
								$scope.quiesceState.operStates[l.identity] = 'up';
							}
						}
					}
					if ($scope.quiesceState.linkStates[l.identity] === 'disabled') {
						l.unsettledCount = 0;
						l.undeliveredCount = 0;
						l.operState = 'idle'
						$scope.quiesceState.operStates[l.identity] = l.operState
					} else {
						l.deliveryCount = QDRService.pretty(QDRService.valFor(links.attributeNames, link, 'deliveryCount'));
						l.undeliveredCount = QDRService.pretty(QDRService.valFor(links.attributeNames, link, 'undeliveredCount'));
						l.unsettledCount = QDRService.pretty(QDRService.valFor(links.attributeNames, link, 'unsettledCount'));
						l.operState = 'up'
						$scope.quiesceState.operStates[l.identity] = l.operState
					}
					// ---------------------------------------------

					$scope.connectionLinks.push(l)
				}
			})
			$scope.connectionLinksGrid.updateState()
		}

		$scope.selectMode = function (mode) {
			$scope.currentMode = mode;
			if (mode.id === 'links') {
		        QDRService.addUpdatedAction("connectionLinks", updateConnectionLinks)
				updateConnectionLinks();
			} else {
				QDRService.delUpdatedAction("connectionLinks");
			}
		}
		$scope.isValid = function (mode) {
			return mode.isValid()
		}
		$scope.quiesceState = {
			state: 'enabled',
			buttonText: 'Quiesce',
			buttonDisabled: false,
			linkStates: {},
			operStates: {}
		}
		$scope.quiesceAllClicked = function () {
			var state = $scope.quiesceState.state;
			if (state === 'enabled') {
				// start quiescing all links
				$scope.quiesceState.state = 'quiescing';
			} else if (state === 'quiesced') {
				// start reviving all links
				$scope.quiesceState.state = 'reviving';
			}
			$scope.connectionLinksGrid.updateState();
		}
		$scope.quiesceClass = function (row) {
			var stateClassMap = {
				enabled: 'btn-primary',
				quiescing: 'btn-warning',
				reviving: 'btn-warning',
				quiesced: 'btn-danger'
			}
			return stateClassMap[$scope.quiesceState.state];
		}
		$scope.quiesceLinkClass = function (row) {
			var stateClassMap = {
				enabled: 'btn-primary',
				disabled: 'btn-danger'
			}
			return stateClassMap[row.entity.adminStatus]
		}
		$scope.quiesceLink = function (row) {
			var state = row.entity.adminStatus === 'enabled' ? 'disabled' : 'enabled';
			var operState = state === 'enabled' ? 'up' : 'idle';
			$scope.quiesceState.linkStates[row.entity.identity] = state;
			$scope.quiesceState.operStates[row.entity.identity] = operState;
		}
		$scope.quiesceLinkDisabled = function (row) {
			return false;
		}
		$scope.quiesceLinkText = function (row) {
			return row.entity.adminStatus === 'disabled' ? "Revive" : "Quiesce";
		}
		$scope.quiesceHide = function () {
			return $scope.connectionLinks.length == 0;
		}
		$scope.connectionLinksGrid = {
		    saveKey: 'connLinksGrid',

			data: 'connectionLinks',
			updateState: function () {
				var state = $scope.quiesceState.state;

				// count enabled and disabled links for this connection
				var enabled = 0, disabled = 0;
				$scope.connectionLinks.forEach ( function (link) {
					if (link.adminStatus === 'enabled')
						++enabled;
					if (link.adminStatus === 'disabled')
						++disabled;
				})

				var linkCount = $scope.connectionLinks.length;
				if (linkCount == 0) {
					$scope.quiesceState.buttonText = 'Quiesce';
					$scope.quiesceState.buttonDisabled = false;
					$scope.quiesceState.state = 'enabled'
					return;
				}
				// if state is quiescing and any links are enabled, button should say 'Quiescing' and be disabled
				if (state === 'quiescing' && (enabled > 0)) {
					$scope.quiesceState.buttonText = 'Quiescing';
					$scope.quiesceState.buttonDisabled = true;
				} else
				// if state is enabled and all links are disabled, button should say Revive and be enabled. set state to quisced
				// if state is quiescing and all links are disabled, button should say 'Revive' and be enabled. set state to quiesced
				if ((state === 'quiescing' || state === 'enabled') && (disabled === linkCount)) {
					$scope.quiesceState.buttonText = 'Revive';
					$scope.quiesceState.buttonDisabled = false;
					$scope.quiesceState.state = 'quiesced'
				} else
				// if state is reviving and any links are disabled, button should say 'Reviving' and be disabled
				if (state === 'reviving' && (disabled > 0)) {
					$scope.quiesceState.buttonText = 'Reviving';
					$scope.quiesceState.buttonDisabled = true;
				} else
				// if state is reviving or quiesced and all links are enabled, button should say 'Quiesce' and be enabled. set state to enabled
				if ((state === 'reviving' || state === 'quiesced') && (enabled === linkCount)) {
					$scope.quiesceState.buttonText = 'Quiesce';
					$scope.quiesceState.buttonDisabled = false;
					$scope.quiesceState.state = 'enabled'
				}

				if ($scope.quiesceState.state === 'quiesced') {
					d3.selectAll('.external.connection.dynatree-active')
						.classed('quiesced', true)
				} else {
					d3.selectAll('.external.connection.dynatree-active.quiesced')
						.classed('quiesced', false)
				}
			},
            columnDefs: [
			{
				field: 'adminStatus',
                cellTemplate: "titleCellTemplate.html",
				headerCellTemplate: 'titleHeaderCellTemplate.html',
			    saveKey: 'connLinksGrid',
				displayName: 'Admin state'
			},
			{
				field: 'operStatus',
                cellTemplate: "titleCellTemplate.html",
				headerCellTemplate: 'titleHeaderCellTemplate.html',
				displayName: 'Oper state'
			},
			{
				field: 'dir',
                cellTemplate: "titleCellTemplate.html",
				headerCellTemplate: 'titleHeaderCellTemplate.html',
				displayName: 'dir'
			},
			{
				field: 'owningAddr',
                cellTemplate: "titleCellTemplate.html",
				headerCellTemplate: 'titleHeaderCellTemplate.html',
				displayName: 'Address'
			},
			{
				field: 'deliveryCount',
				displayName: 'Delivered',
				headerCellTemplate: 'titleHeaderCellTemplate.html',
				cellClass: 'grid-values'

			},
			{
				field: 'undeliveredCount',
				displayName: 'Undelivered',
				headerCellTemplate: 'titleHeaderCellTemplate.html',
				cellClass: 'grid-values'
			},
			{
				field: 'unsettledCount',
				displayName: 'Unsettled',
				headerCellTemplate: 'titleHeaderCellTemplate.html',
				cellClass: 'grid-values'
			}/*,
			{
				cellClass: 'gridCellButton',
				cellTemplate: '<button title="{{quiesceLinkText(row)}} this link" type="button" ng-class="quiesceLinkClass(row)" class="btn" ng-click="quiesceLink(row)" ng-disabled="quiesceLinkDisabled(row)">{{quiesceLinkText(row)}}</button>'
			}*/
			]
		}

		$scope.connectionFields = []
		$scope.connectionGrid = {
		    saveKey: 'connGrid',
			data: 'connectionFields',
			columnDefs: [
                 {
                     field: 'attribute',
                     displayName: 'Attribute',
                    saveKey: 'connGrid',
                     width: '40%'
                 },
                 {
                     field: 'value',
                     displayName: 'Value',
                     width: '40%'
                 }
            ],
			enableColumnResize: true,
			multiSelect: false
		}

		var connectionInfo = function (connection) {
			$scope.connection = connection

			$scope.connectionFields = [];
			var fields = Object.keys(connection.data.fields)
			fields.forEach( function (field) {
				$scope.connectionFields.push({attribute: field, value: connection.data.fields[field]})
			})
			$scope.selectMode($scope.currentMode)
	        loadColState($scope.connectionGrid);
		}

		// get info for a all logs
		var allLogInfo = function () {
		}

		// get info for a single log
		var logInfo = function (node) {
			$scope.log = node
		}

		var getExpandedList = function () {
			if (!treeRoot)
				return;
			var list = [];
			if (treeRoot.visit) {
				treeRoot.visit(function(node){
	                if (node.isExpanded()) {
	                    list.push(node.data.parent)
	                }
	            });
			}
			return list;
		}

		var updateExpanded = function () {
			if (!treeRoot)
				return;
			if (treeRoot.visit) {
				treeRoot.visit(function(node){
	                if (node.isExpanded() || node.isActive()) {
	                    node.data.info(node)
	                }
	            });
			}
		}

		// loads the tree node name that was last selected
		var loadActivatedNode = function () {
			return localStorage[OverviewActivatedKey] || 'Routers'
		}
		// saved the tree node name that is currently selected
		var saveActivated = function (key) {
			localStorage[OverviewActivatedKey] = key;
			lastKey = key;
		}
		// loads list that was saved
		var loadExpandedNodeList = function () {
			return angular.fromJson(localStorage[OverviewExpandedKey]) || [];
		}
		// called when a node is expanded
		// here we save the expanded node so it can be restored when the page reloads
		var saveExpanded = function () {
			var list = getExpandedList();
			localStorage[OverviewExpandedKey] = JSON.stringify(list)
		}

		// activated is called each time a tree node is clicked
		// based on which node is clicked, load the correct data grid template and start getting the data
		var activated = function (node) {
			//QDR.log.debug("node activated: " + node.data.title)
			var type = node.data.type;
			saveExpanded()
			saveActivated(node.data.key)

			var template = $scope.templates.filter( function (tpl) {
				return tpl.name == type;
			})
			$scope.template = template[0];
			// the nodes info function will fetch the grids data
			if (node.data.info) {
				$timeout(function () {node.data.info(node)})
			}

		}
        $scope.template = {url: ''};

		if (!QDRService.connected) {
			QDRService.redirectWhenConnected("overview")
			return;
		}

		// we are currently connected. setup a handler to get notified if we are ever disconnected
		QDRService.addDisconnectAction( function () {
			QDRService.redirectWhenConnected("overview")
			$scope.$apply();
		})

	/* --------------------------------------------------
	 *
     * setup the tree on the left
     *
     * -------------------------------------------------
     */
		// utility function called by each top level tree node when it needs to populate its child nodes
		var updateLeaves = function (leaves, parentKey, parentFolder, worker) {
			var scrollTree = $('.qdr-overview.pane.left .pane-viewport')
			var scrollTop = scrollTree.scrollTop();
			var tree;
			try {
				tree = $("#overtree").dynatree("getTree")
			} catch (e) {
				// tree is not initialized yet
				tree = {}
			}
			var parentNode = tree.count ? tree.getNodeByKey(parentKey) : null;
			// if we were called after the tree is created, replace the existing nodes
			var activeNode;
			if (parentNode) {
				activeNode = tree.getActiveNode();
				parentNode.removeChildren();
			}
			leaves.forEach( function (leaf) {
				var childFolder = worker(leaf)
				if (parentNode)
					parentNode.addChild(childFolder)
				else
					parentFolder.children.push(childFolder)
			})
			scrollTree.scrollTop(scrollTop)
			if (activeNode) {
				var newNode = tree.getNodeByKey(activeNode.data.key)
				newNode.activateSilently()
			}
		}

		// get saved tree state
        var lastKey = loadActivatedNode();
		var expandedNodeList = loadExpandedNodeList();

		// create a routers tree branch
		var routers = new Folder("Routers")
		routers.type = "Routers"
		routers.info = allRouterInfo
		routers.activate = lastKey === 'Routers'
		routers.expand = (expandedNodeList.indexOf("Routers") > -1)
		routers.clickFolderMode = 1
		routers.key = "Routers"
		routers.parent = "Routers"
	    routers.addClass = "routers"
		topLevelChildren.push(routers)
		// called when the list of routers changes
		var updateRouterTree = function (nodes) {
			var worker = function (node) {
				var name = QDRService.nameFromId(node)
				var router = new Folder(name)
				router.type = "Router"
				router.info = routerInfo
				router.nodeId = node
				router.key = node
				router.addClass = "router"
				router.parent = "Routers"
				router.activate = lastKey === node
				return router;
			}
			updateLeaves(nodes, "Routers", routers, worker)
		}
		updateRouterTree(QDRService.nodeIdList());

		// create an addresses tree branch
		var addresses = new Folder("Addresses")
		addresses.type = "Addresses"
		addresses.info = allAddressInfo
		addresses.activate = lastKey === 'Addresses'
		addresses.expand = (expandedNodeList.indexOf("Addresses") > -1)
		addresses.clickFolderMode = 1
		addresses.key = "Addresses"
		addresses.parent = "Addresses"
	    addresses.addClass = "addresses"
		topLevelChildren.push(addresses)
		var updateAddressTree = function (addressFields) {
			var worker = function (address) {
				var a = new Folder(address.title)
				a.info = addressInfo
				a.key = address.uid
				a.fields = address
				a.type = "Address"
				a.addClass = "address"
				a.activate = lastKey === address.uid
				a.parent = "Addresses"
				return a;
			}
			updateLeaves(addressFields, "Addresses", addresses, worker)
		}
		allAddressInfo();

		$scope.setLinkFilter = function (cur) {
			// filter out non-matching links from the tree and the grid
			getAllLinkFields([updateLinkGrid, updateLinkTree])
		}
		$scope.filterClose = function () {
			var filter = $('#linkFilter')
			filter.hide();
		}
		$scope.currentLinkFilter = "endpoint"
		var filterHtml = "<button type='button' class='btn btn-secondary btn-filter'><span class='filter-icon'><i class='icon-filter'> Filter</span></button>";
		var links = new Folder("Links " + filterHtml)
		links.type = "Links"
		links.info = allLinkInfo
		links.activate = lastKey === 'Links'
		links.expand = (expandedNodeList.indexOf("Links") > -1)
		links.clickFolderMode = 1
		links.key = "Links"
		links.parent = "Links"
	    links.addClass = "links"
		topLevelChildren.push(links)

		// called both before the tree is created and whenever a background update is done
		var updateLinkTree = function (linkFields) {
			var worker = function (link) {
				var l = new Folder(link.title)
				l.info = linkInfo
				l.key = link.uid
				l.fields = link
				l.type = "Link"
				l.parent = "Links"
				l.activate = lastKey === link.uid
				l.addClass = "link"
				return l;
			}
			linkFields.sort ( function (a,b) { return a.link < b.link ? -1 : a.link > b.link ? 1 : 0})
			updateLeaves(linkFields, "Links", links, worker)
		}
		allLinkInfo();

		var connections = new Folder("Connections")
		connections.type = "Connections"
		connections.info = allConnectionInfo
		connections.activate = lastKey === 'Connections'
		connections.expand = (expandedNodeList.indexOf("Connections") > -1)
		connections.clickFolderMode = 1
		connections.key = "Connections"
		connections.parent = "Connections"
	    connections.addClass = "connections"
		topLevelChildren.push(connections)

		updateConnectionTree = function (nodes) {
			var connectionsObj = {}
			var expected = nodes.length;
			var connreceived = 0;
			nodes.forEach( function (nodeId) {
				QDRService.getNodeInfo(nodeId, ".connection", [], function (nodeName, entity, response) {
					response.results.forEach( function (result) {

						var auth = "no_auth"
						var sasl = QDRService.valFor(response.attributeNames, result, "sasl")
						if (QDRService.valFor(response.attributeNames, result, "isAuthenticated")) {
							auth = sasl
							if (sasl === "ANONYMOUS")
								auth = "anonymous-user"
							else {
								if (sasl === "GSSAPI")
									sasl = "Kerberos"
								if (sasl === "EXTERNAL")
									sasl = "x.509"
								auth = QDRService.valFor(response.attributeNames, result, "user") + "(" +
										QDRService.valFor(response.attributeNames, result, "sslCipher") + ")"
								}
						}

						var sec = "no-security"
						if (QDRService.valFor(response.attributeNames, result, "isEncrypted")) {
							if (sasl === "GSSAPI")
								sec = "Kerberos"
							else
								sec = QDRService.valFor(response.attributeNames, result, "sslProto") + "(" +
										QDRService.valFor(response.attributeNames, result, "sslCipher") + ")"
						}

						var host = QDRService.valFor(response.attributeNames, result, "host")
						connectionsObj[host] = {}
						response.attributeNames.forEach( function (attribute, i) {
							connectionsObj[host][attribute] = result[i]
						})
						connectionsObj[host].security = sec
						connectionsObj[host].authentication = auth
						connectionsObj[host].routerId = nodeName

					})
					++connreceived;
					if (connreceived == expected) {
						var worker = function (connection) {
							var c = new Folder(connection)
							c.type = "Connection"
							c.info = connectionInfo
							c.key = connection
							c.fields = connectionsObj[connection]
							c.tooltip = connectionsObj[connection].role === "inter-router" ? "inter-router connection" : "external connection"
							c.addClass = c.tooltip
							c.parent = "Connections"
							c.activate = lastKey === connection
							return c
						}
						var allConnections = Object.keys(connectionsObj).sort()
						updateLeaves(allConnections, "Connections", connections, worker)
					}
				})
			})
		}
		updateConnectionTree(QDRService.nodeIdList())

		var logsreceived = 0;
		var logObj = {}
		var logs = new Folder("Logs")
		logs.type = "Logs"
		logs.info = allLogInfo
		logs.activate = lastKey === 'Logs'
		logs.expand = (expandedNodeList.indexOf("Logs") > -1)
		logs.clickFolderMode = 1
		logs.key = "Logs"
		logs.parent = "Logs"
		//topLevelChildren.push(logs)
		var nodeIds = QDRService.nodeIdList()
		var expected = nodeIds.length;
		nodeIds.forEach( function (nodeId) {
			QDRService.getNodeInfo(nodeId, ".log", ["name"], function (nodeName, entity, response) {
				response.results.forEach( function (result) {
					logObj[result[0]] = 1    // use object to collapse duplicates
				})
				++logsreceived;
				if (logsreceived == expected) {
					var allLogs = Object.keys(logObj).sort()
					allLogs.forEach(function (log) {
						var l = new Folder(log)
						l.type = "Log"
						l.info = logInfo
						l.key = log
						l.parent = "Logs"
						l.activate = lastKey === log
						logs.children.push(l)
					})
					$('#overtree').dynatree({
						onActivate: activated,
						onExpand: saveExpanded,
						onClick: function (n, e) {
							if (e.target.className.indexOf('-filter') > -1) {
								//QDR.log.debug("overtree on click called")
								e.preventDefault();
								e.stopPropagation()
								var filter = $('#linkFilter')
								var treeLink = $('span.links')
								filter.css({
                                              top: treeLink.offset().top + treeLink.height(),
                                              left: treeLink.offset().left,
                                              zIndex:5000
                                            });
								filter.toggle()
								$("#overtree").dynatree("getTree").getNodeByKey('Links').activate()
								return false;
							}
						},
						selectMode: 1,
						debugLevel: 0,
						activeVisible: false,
						children: topLevelChildren
					})
					treeRoot = $("#overtree").dynatree("getRoot");

					// simulate a click on the previous active node
					var active = $("#overtree").dynatree("getActiveNode");
					if (!active) {
						active = $("#overtree").dynatree("getTree").getNodeByKey("Routers")
					}
					activated(active);

					// populate the data for each expanded node
					updateExpanded();
					QDRService.addUpdatedAction( "overview", function () {
						$timeout(updateExpanded);
					})
					// update the node list
					QDRService.startUpdating()

			        loadColState($scope.allRouters);
			        loadColState($scope.routerGrid);
			        loadColState($scope.addressesGrid);
			        loadColState($scope.addressGrid);
			        loadColState($scope.linksGrid);
			        loadColState($scope.linkGrid);
			        loadColState($scope.allConnectionGrid);
			        loadColState($scope.connectionGrid);

				}
			})
		})

        $scope.$on("$destroy", function( event ) {
			QDRService.stopUpdating()
			QDRService.delUpdatedAction("overview")
        });

    }]);

  return QDR;

}(QDR || {}));


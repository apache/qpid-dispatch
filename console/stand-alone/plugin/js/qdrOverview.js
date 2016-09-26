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
		var COLUMNSTATEKEY = 'QDRColumnKey.';
		var OVERVIEWEXPANDEDKEY = "QDROverviewExpanded"
		var OVERVIEWACTIVATEDKEY = "QDROverviewActivated"
		var FILTERKEY = "QDROverviewFilters"
		var OVERVIEWMODEIDS = "QDROverviewModeIds"
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
							var routerId = QDRService.valFor(result.attributeNames, result.results[0], "id")
							allRouterFields.some( function (connField) {
								if (routerId === connField.routerId) {
									result.attributeNames.forEach ( function (attrName) {
										connField[attrName] = QDRService.valFor(result.attributeNames, result.results[0], attrName)
									})
									connField['routerId'] = connField.id
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
						if (key !== '$$hashKey') {
							var attr = (key === 'connections') ? 'External connections' : key
							$scope.routerFields.push({attribute: attr, value: field[key]})
						}
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
				if (!addressFields || addressFields.length === 0)
					return;
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
		        if (addr[0] == 'T')  return "topo"
		        return "unknown: " + addr[0]
			}

			var addr_phase = function (addr) {
		        if (!addr)
		            return "-"
		        if (addr[0] == 'M')
		            return addr[1]
		        return ''
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
					var identity = QDRService.identity_clean(uid)

					addressFields.push({
						address: QDRService.addr_text(identity),
						'class': QDRService.addr_class(identity),
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
			// apply the filter
			var filteredLinks = linkFields.filter( function (link) {
				var include = true;
				if ($scope.filter.endpointsOnly === "true") {
					if (link.linkType !== 'endpoint')
						include = false;
				}
				if ($scope.filter.hideConsoles) {
					if (QDRService.isConsoleLink(link))
						include = false;
				}
				return include;
			})
			$scope.linkFields = filteredLinks;
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
					width: '11%'
				},
				{
					field: 'linkType',
					displayName: 'Link type',
					groupable:	false,
					width: '9%'
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
					width: '9%'
				},
				{
					field: 'operStatus',
					displayName: 'Oper status',
					groupable:	false,
					width: '9%'
				},
				{
					field: 'deliveryCount',
					displayName: 'Delivery Count',
					groupable:	false,
					cellClass: 'grid-align-value',
					width: '11%'
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
					width: '9%'
				},
				{
					field: 'owningAddr',
					displayName: 'Address',
					groupable:	false,
					width: '15%'
				}/*,
				{
					displayName: 'Quiesce',
                    cellClass: 'gridCellButton',
                    cellTemplate: '<button title="{{quiesceLinkText(row)}} this link" type="button" ng-class="quiesceLinkClass(row)" class="btn" ng-click="quiesceLink(row, $event)" ng-disabled="quiesceLinkDisabled(row)">{{quiesceLinkText(row)}}</button>',
					width: '10%'
                }*/
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
                localStorage.setItem(COLUMNSTATEKEY+saveKey, JSON.stringify(saveInfo));
        })

        var loadColState = function (grid) {
            if (!grid)
                return;
            var columns = localStorage.getItem(COLUMNSTATEKEY+grid.saveKey);
            if (columns) {
                var cols = JSON.parse(columns);
                cols.forEach( function (col, index) {
					if (grid.columnDefs[index]) {
	                    grid.columnDefs[index].width = col[0];
	                    grid.columnDefs[index].visible = col[1]
					}
                })
            }
        }
		var allLinkInfo = function () {
			getAllLinkFields([updateLinkGrid, updateLinkTree])
	        loadColState($scope.linksGrid);
		}

		var getAllLinkFields = function (completionCallbacks, selectionCallback) {
			if (!$scope.linkFields) {
				QDR.log.info("$scope.linkFields was not defined")
				return;
			}
			var nodeIds = QDRService.nodeIdList()
			var linkFields = []
			var now = Date.now()
			var rate = function (linkName, response, result) {
				if (!$scope.linkFields)
					return 0;
				var oldname = $scope.linkFields.filter(function (link) {
					return link.link === linkName
				})
				if (oldname.length === 1) {
					var elapsed = (now - oldname[0].timestamp) / 1000;
					if (elapsed < 0)
						return 0
					var delivered = QDRService.valFor(response.attributeNames, result, "deliveryCount") - oldname[0].rawDeliveryCount
					//QDR.log.debug("elapsed " + elapsed + " delivered " + delivered)
					return elapsed > 0 ? parseFloat(Math.round((delivered/elapsed) * 100) / 100).toFixed(2) : 0;
				} else {
					//QDR.log.debug("unable to find old linkName")
					return 0
				}
			}
			var expected = nodeIds.length;
			var received = 0;
			var gotLinkInfo = function (nodeName, entity, response) {
				response.results.forEach( function (result) {
					var nameIndex = response.attributeNames.indexOf('name')
					var prettyVal = function (field) {
						var fieldIndex = response.attributeNames.indexOf(field)
						if (fieldIndex < 0) {
							return "-"
						}
						var val = result[fieldIndex]
						return QDRService.pretty(val)
					}
					var uncounts = function () {
						var und = QDRService.valFor(response.attributeNames, result, "undeliveredCount")
						var uns = QDRService.valFor(response.attributeNames, result, "unsettledCount")
						return QDRService.pretty(und + uns)
					}
					var linkName = function () {
						var namestr = QDRService.nameFromId(nodeName)
						return namestr + ':' + QDRService.valFor(response.attributeNames, result, "identity")
					}
					var fixAddress = function () {
						var addresses = []
						var owningAddr = QDRService.valFor(response.attributeNames, result, "owningAddr") || ""
						var rawAddress = owningAddr
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
								rawAddress = ends
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
						addresses[0] = address || owningAddr
						addresses[1] = rawAddress
						return addresses
					}
					if (!selectionCallback || selectionCallback(response, result)) {
						var adminStatus = QDRService.valFor(response.attributeNames, result, "adminStatus")
						var operStatus = QDRService.valFor(response.attributeNames, result, "operStatus")
						var linkName = linkName()
						var linkType = QDRService.valFor(response.attributeNames, result, "linkType")
						var addresses = fixAddress();
						linkFields.push({
							link:       linkName,
							title:      linkName,
							uncounts:   uncounts(),
							operStatus: operStatus,
							adminStatus:adminStatus,
							owningAddr: addresses[0],

							acceptedCount: prettyVal("acceptedCount"),
							modifiedCount: prettyVal("modifiedCount"),
							presettledCount: prettyVal("presettledCount"),
							rejectedCount: prettyVal("rejectedCount"),
							releasedCount: prettyVal("releasedCount"),
							deliveryCount:prettyVal("deliveryCount") + " ",

							rate: QDRService.pretty(rate(linkName, response, result)),
							capacity: QDRService.valFor(response.attributeNames, result, "capacity"),
							undeliveredCount: QDRService.valFor(response.attributeNames, result, "undeliveredCount"),
							unsettledCount: QDRService.valFor(response.attributeNames, result, "unsettledCount"),

							rawAddress: addresses[1],
							rawDeliveryCount: QDRService.valFor(response.attributeNames, result, "deliveryCount"),
							name: QDRService.valFor(response.attributeNames, result, "name"),
							linkName: QDRService.valFor(response.attributeNames, result, "linkName"),
							connectionId: QDRService.valFor(response.attributeNames, result, "connectionId"),
							linkDir: QDRService.valFor(response.attributeNames, result, "linkDir"),
							linkType: linkType,
							peer: QDRService.valFor(response.attributeNames, result, "peer"),
							type: QDRService.valFor(response.attributeNames, result, "type"),

							uid:     linkName,
							timestamp: now,
							nodeId: nodeName,
							identity: QDRService.valFor(response.attributeNames, result, "identity")
						})
					}
				})
				if (expected === ++received) {
					linkFields.sort ( function (a,b) { return a.link < b.link ? -1 : a.link > b.link ? 1 : 0})
					completionCallbacks.forEach( function (cb) {
						cb(linkFields)
					})
				}
			}
			nodeIds.forEach( function (nodeId) {
				QDRService.getNodeInfo(nodeId, "router.link", [], gotLinkInfo);
			})
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
                    var nodeId = selItem.uid
                    // activate Routers->nodeId in the tree
                    $("#overtree").dynatree("getTree").activateKey(nodeId);
                }
            }
        };
		// get info for a all connections
		var allConnectionInfo = function () {
			getAllConnectionFields([updateConnectionGrid, updateConnectionTree])
	        loadColState($scope.allConnectionGrid);
		}
		// called after conection data is available
		var updateConnectionGrid = function (connectionFields) {
			$scope.allConnectionFields = connectionFields;
		}

		// get the connection data for all nodes
		// called periodically
		// creates a connectionFields array and calls the callbacks (updateTree and updateGrid)
		var getAllConnectionFields = function (callbacks) {
			var nodeIds = QDRService.nodeIdList()
			var connectionFields = [];
			var expected = nodeIds.length;
			var received = 0;
			var gotConnectionInfo = function (nodeName, entity, response) {
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
					var connField = {
						host: host,
						security: sec,
						authentication: auth,
						routerId: nodeName,
						uid: host + QDRService.valFor(response.attributeNames, result, "identity")
					}
					response.attributeNames.forEach( function (attribute, i) {
						connField[attribute] = result[i]
					})
					connectionFields.push(connField)
				})
				if (expected === ++received) {
					connectionFields.sort ( function (a,b) { return a.host < b.host ? -1 : a.host > b.host ? 1 : 0})
					callbacks.forEach( function (cb) {
						cb(connectionFields)
					})
				}
			}
			nodeIds.forEach( function (nodeId) {
				QDRService.getNodeInfo(nodeId, ".connection", [], gotConnectionInfo)
			})
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
			var currentEntity = getCurrentLinksEntity();
			// we are viewing the addressLinks page
			if (currentEntity === 'Address' && entityModes[currentEntity].currentModeId === 'links') {
				updateModeLinks()
				return;
			}

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
			var excludeFields = ["title", "uid", "uncounts", "rawDeliveryCount", "timestamp", "rawAddress"]
			fields.forEach( function (field) {
				if (excludeFields.indexOf(field) == -1)
					$scope.singleLinkFields.push({attribute: field, value: link.data.fields[field]})
			})
	        loadColState($scope.linkGrid);
		}

		// get info for a single connection
		$scope.gridModes = [{
	        content: '<a><i class="icon-list"></i> Attriutes</a>',
			id: 'attributes',
			title: "View attributes"
	    },
	    {
	        content: '<a><i class="icon-link"></i> Links</a>',
	        id: 'links',
	        title: "Show links"
	    }
	    ];
		var saveModeIds = function () {
			var modeIds = {Address: entityModes.Address.currentModeId, Connection: entityModes.Connection.currentModeId}
			localStorage[OVERVIEWMODEIDS] = JSON.stringify(modeIds)
		}
		var loadModeIds = function () {
			return angular.fromJson(localStorage[OVERVIEWMODEIDS]) ||
				{Address: 'attributes', Connection: 'attributes'}
		}
		var savedModeIds = loadModeIds()
	    var entityModes = {
	        Address: {
	            currentModeId: savedModeIds.Address,
	            filter: function (response, result) {
					var owningAddr = QDRService.valFor(response.attributeNames, result, "owningAddr")
					var id = $scope.address.data.fields.uid
					return (owningAddr === $scope.address.data.fields.uid)
	            }
	        },
	        Connection: {
	            currentModeId: savedModeIds.Connection,
	            filter: function (response, result) {
					var connectionId = QDRService.valFor(response.attributeNames, result, "connectionId")
					return (connectionId === $scope.connection.data.fields.identity)
	            }
	        }
	    }
		$scope.selectMode = function (mode, entity) {
			if (!mode || !entity)
				return;
			entityModes[entity].currentModeId = mode.id;
			saveModeIds();
			if (mode.id === 'links') {
				$scope.linkFields = [];
				updateModeLinks();
			}
		}
		$scope.isModeSelected = function (mode, entity) {
			return mode.id === entityModes[entity].currentModeId
		}
		$scope.isModeVisible = function (entity, id) {
			return entityModes[entity].currentModeId === id
		}

		var updateEntityLinkGrid = function (linkFields) {
			$timeout(function () {$scope.linkFields = linkFields})
		}
		// based on which entity is selected, get and filter the links
		var updateModeLinks = function () {
			var currentEntity = getCurrentLinksEntity()
			if (!currentEntity)
				return;
			var selectionCallback = entityModes[currentEntity].filter;
			getAllLinkFields([updateEntityLinkGrid], selectionCallback)
		}
		var getCurrentLinksEntity = function () {
			var currentEntity;
			var active = $("#overtree").dynatree("getActiveNode");
			if (active) {
				currentEntity = active.data.type;
			}
			return currentEntity;
		}

		$scope.quiesceLinkClass = function (row) {
			var stateClassMap = {
				enabled: 'btn-primary',
				disabled: 'btn-danger'
			}
			return stateClassMap[row.entity.adminStatus]
		}

		$scope.quiesceLink = function (row, $event) {
			QDRService.quiesceLink(row.entity.nodeId, row.entity.name);
			$event.stopPropagation()
		}

		$scope.quiesceLinkDisabled = function (row) {
			return (row.entity.operStatus !== 'up' && row.entity.operStatus !== 'down')
		}
		$scope.quiesceLinkText = function (row) {
			return row.entity.adminStatus === 'disabled' ? "Revive" : "Quiesce";
		}

		$scope.expandAll = function () {
			$("#overtree").dynatree("getRoot").visit(function(node){
                node.expand(true);
            });
		}
		$scope.contractAll = function () {
			$("#overtree").dynatree("getRoot").visit(function(node){
                node.expand(false);
            })
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
			if (!connection)
				return;
			$scope.connection = connection

			var currentEntity = getCurrentLinksEntity();
			// we are viewing the connectionLinks page
			if (currentEntity === 'Connection' && entityModes[currentEntity].currentModeId === 'links') {
				updateModeLinks()
				return;
			}

			$scope.connectionFields = [];
			var fields = Object.keys(connection.data.fields)
			fields.forEach( function (field) {
				if (field != "title" && field != "uid")
					$scope.connectionFields.push({attribute: field, value: connection.data.fields[field]})
			})
			$scope.selectMode($scope.currentMode)
	        loadColState($scope.connectionGrid);
		}

		$scope.allLogFields = []
		$scope.allLogSelections = [];
		$scope.allLogGrid = {
			saveKey: 'allLogGrid',
            data: 'allLogFields',
            columnDefs: [
                 {
                     field: 'module',
                    saveKey: 'allLogGrid',
                     displayName: 'Module'
                 },
                 {
                     field: 'enable',
                     displayName: 'Enable'
                 },
                 {
                     field: 'count',
                     displayName: 'Count'
                 }
            ],
            enableColumnResize: true,
            multiSelect: false,
            selectedItems: $scope.allLogSelections,
            afterSelectionChange: function(data) {
                if (data.selected) {
                    var selItem = $scope.allLogSelections[0]
                    var nodeId = selItem.module
                    // activate in the tree
                    $("#overtree").dynatree("getTree").activateKey(nodeId);
                }
            }
        };

		// get info for a all logs
		var allLogEntries = []
		var allLogInfo = function () {
			var nodeIds = QDRService.nodeIdList()
			var expected = nodeIds.length;
			var received = 0;
			var logResults = []
			var gotLogInfo = function (nodeId, entity, response, context) {
				var statusCode = context.message.application_properties.statusCode;
				if (statusCode < 200 || statusCode >= 300) {
					Core.notification('error', context.message.application_properties.statusDescription);
					//QDR.log.debug(context.message.application_properties.statusDescription)
					return;
				}
				var logFields = response.map( function (result) {
					return {
						nodeId: QDRService.nameFromId(nodeId),
						name: result[0],
						type: result[1],
						message: result[2],
						source: result[3],
						line: result[4],
						time: Date(result[5]).toString()
					}
				})
				logResults.push.apply(logResults, logFields) // append new array to existing
				if (expected == ++received) {
					logResults.sort( function (a, b) {
						return b.name - a.name
					})

					$scope.allLogFields = [];
					var logsRoot = $("#overtree").dynatree("getTree").getNodeByKey('Logs')
					logsRoot.visit( function (logModule) {
						$scope.allLogFields.push({module: logModule.data.key,
							enable: logModule.data.enable,
							count: logResults.filter( function (entry) {
								return entry.name === logModule.data.key
							}).length
						})
					})
					allLogEntries = logResults
				}
			}
			nodeIds.forEach( function (node) {
				QDRService.sendMethod(node, undefined, {}, "GET-LOG", {}, gotLogInfo)
			})

		}

		// get info for a single log
		var logInfo = function (node) {
			$scope.log = node
			$scope.logFields = allLogEntries.filter( function (log) {
				return node.data.key === log.name
			})
			$scope.$apply();
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
			return localStorage[OVERVIEWACTIVATEDKEY] || 'Routers'
		}
		// saved the tree node name that is currently selected
		var saveActivated = function (key) {
			localStorage[OVERVIEWACTIVATEDKEY] = key;
			lastKey = key;
		}
		// loads list that was saved
		var loadExpandedNodeList = function () {
			return angular.fromJson(localStorage[OVERVIEWEXPANDEDKEY]) || [];
		}
		// called when a node is expanded
		// here we save the expanded node so it can be restored when the page reloads
		var saveExpanded = function () {
			var list = getExpandedList();
			localStorage[OVERVIEWEXPANDEDKEY] = JSON.stringify(list)
			expandedNodeList = list
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
				a.tooltip = address['class'] + " address"
				if (address.address === '$management')
					a.tooltip = "internal " + a.tooltip
				a.addClass = a.tooltip
				a.activate = lastKey === address.uid
				a.parent = "Addresses"
				return a;
			}
			updateLeaves(addressFields, "Addresses", addresses, worker)
		}
		allAddressInfo();

		$scope.$watch("filter", function (newValue, oldValue) {
			if (newValue !== oldValue) {
				$timeout(allLinkInfo);
				localStorage[FILTERKEY] = JSON.stringify($scope.filter)
			}
		}, true)

		$scope.filterToggle = function () {
			var filter = $('#linkFilter')
			filter.toggle();
		}

		$scope.filter = angular.fromJson(localStorage[FILTERKEY]) || {endpointsOnly: "true", hideConsoles: true};
		var links = new Folder("Links")
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
				var isConsole = QDRService.isConsoleLink(link)
				l.info = linkInfo
				l.key = link.uid
				l.fields = link
				l.type = "Link"
				l.parent = "Links"
				l.activate = lastKey === link.uid
				if (isConsole)
					l.tooltip = "console link"
				else
					l.tooltip = link.linkType  + " link"
				l.addClass = l.tooltip
				return l;
			}
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

		updateConnectionTree = function (connectionFields) {
			var worker = function (connection) {
				var c = new Folder(connection.host)
				var isConsole = QDRService.isAConsole (connection.properties, connection.identity, connection.role, connection.routerId)
				c.type = "Connection"
				c.info = connectionInfo
				c.key = connection.uid
				c.fields = connection
				if (isConsole)
					c.tooltip = "console connection"
				else
					c.tooltip = connection.role === "inter-router" ? "inter-router connection" : "external connection"
				c.addClass = c.tooltip
				c.parent = "Connections"
				c.activate = lastKey === connection.uid
				return c
			}
			updateLeaves(connectionFields, "Connections", connections, worker)
		}
 		allConnectionInfo()

		var htmlReady = false;
		var dataReady = false;
		var logs = new Folder("Logs")
		logs.type = "Logs"
		logs.info = allLogInfo
		logs.activate = lastKey === 'Logs'
		logs.expand = (expandedNodeList.indexOf("Logs") > -1)
		logs.clickFolderMode = 1
		logs.key = "Logs"
		logs.parent = "Logs"
		topLevelChildren.push(logs)
		var nodeIds = QDRService.nodeIdList()
		QDRService.getNodeInfo(nodeIds[0], "log", ["module", "enable"], function (nodeName, entity, response) {
			var moduleIndex = response.attributeNames.indexOf('module')
			response.results.sort( function (a,b) {return a[moduleIndex] < b[moduleIndex] ? -1 : a[moduleIndex] > b[moduleIndex] ? 1 : 0})
			response.results.forEach( function (result) {
				var entry = QDRService.flatten(response.attributeNames, result)
				var l = new Folder(entry.module)
				l.type = "Log"
				l.info = logInfo
				l.key = entry.module
				l.parent = "Logs"
				l.activate = lastKey === l.key
				l.enable = entry.enable
				l.addClass = "log"
				logs.children.push(l)
			})
			dataReady = true;
			initTreeAndGrid();
		})
		var initTreeAndGrid = function () {
			if (!htmlReady || !dataReady)
				return;
            var div = angular.element("#overtree");
            if (!div.width()) {
                setTimeout(initTreeAndGrid, 100);
                return;
            }
			$('#overtree').dynatree({
				onActivate: activated,
				onExpand: saveExpanded,
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
			$timeout(updateExpanded);
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

		$scope.overviewLoaded = function () {
			htmlReady = true;
			initTreeAndGrid();
		}
        $scope.$on("$destroy", function( event ) {
			QDRService.stopUpdating()
			QDRService.delUpdatedAction("overview")
        });

    }]);

  return QDR;

}(QDR || {}));


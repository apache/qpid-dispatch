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
	QDR.module.controller("QDR.OverviewController", ['$scope', 'uiGridConstants', 'QDRService', function($scope, uiGridConstants, QDRService) {


		if (!angular.isDefined(QDRService.schema))
		    return;

		var nodeIds = QDRService.nodeIdList();
		var currentTimer;
		var refreshInterval = 5000
		var Folder = (function () {
            function Folder(title) {
                this.title = title;
				this.children = [];
				this.folder = true;
            }
            return Folder;
        })();
		var Leaf = (function () {
            function Leaf(title) {
                this.title = title;
            }
            return Leaf;
        })();
	    $scope.modes = [
	    	{title: 'Overview', name: 'Overview', right: false}
	    	];

		$scope.templates =
		    [ { name: 'Routers', url: 'routers.html'},
		      { name: 'Router', url: 'router.html'},
              { name: 'Addresses', url: 'addresses.html'},
		      { name: 'Address', url: 'address.html'},
              { name: 'Connections', url: 'connections.html'},
		      { name: 'Connection', url: 'connection.html'},
              { name: 'Logs', url: 'logs.html'},
              { name: 'Log', url: 'log.html'} ];

		$scope.getGridHeight = function (data) {
	       // add 1 for the header row
	       return {height: (($scope[data.data].length + 1) * 30) + "px"};
		}
		$scope.overview = new Folder("Overview");

		$scope.allRouterFields = [];
		var allRouterCols = [
			 {
				 field: 'routerId',
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
		];
		$scope.allRouters = {
			data: 'allRouterFields',
			columnDefs: allRouterCols,
			enableHorizontalScrollbar: uiGridConstants.scrollbars.NEVER,
            enableVerticalScrollbar: uiGridConstants.scrollbars.NEVER,
			enableColumnResize: true,
			multiSelect: false,
			enableRowHeaderSelection: false,
			noUnselect: true,
			enableSelectAll: false,
			enableRowSelection: true,
			onRegisterApi: function (gridApi) {
				gridApi.selection.on.rowSelectionChanged($scope, function(row) {
					if (row.isSelected) {
						var nodeId = row.entity.nodeId;
						$("#overtree").fancytree("getTree").activateKey(nodeId);
					}
				});
		    }
		};
		// get info for all routers
		var allRouterInfo = function () {
			nodeIds = QDRService.nodeIdList()
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
					QDRService.getMultipleNodeInfo(nodeIds, "router", [], function (nodeIds, entity, response) {
						var results = response.aggregates
						results.forEach ( function (result) {

							var routerId = QDRService.valFor(response.attributeNames, result, "routerId").sum
							allRouterFields.some( function (connField) {
								if (routerId === connField.routerId) {
									response.attributeNames.forEach ( function (attrName) {
										connField[attrName] = QDRService.valFor(response.attributeNames, result, attrName).sum
									})
									return true
								}
								return false
							})
						})
						$scope.allRouterFields = allRouterFields
						$scope.$apply()
						if (currentTimer) {
							clearTimeout(currentTimer)
						}
						currentTimer = setTimeout(allRouterInfo, refreshInterval);
					}, nodeIds[0])
				}
			}
			nodeIds.forEach ( function (nodeId) {
				QDRService.getNodeInfo(nodeId, ".connection", ["role"], gotNodeInfo)
			})

		}

		// get info for a single router
		var routerInfo = function (node) {
			$scope.router = node
			$scope.routerFields = []
			var cols = [
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
			]
			$scope.routerGrid = {
				data: 'routerFields',
				columnDefs: cols,
				enableHorizontalScrollbar: uiGridConstants.scrollbars.NEVER,
	            enableVerticalScrollbar: uiGridConstants.scrollbars.NEVER,
				enableColumnResize: true,
				multiSelect: false
			}

			$scope.allRouterFields.some( function (field) {
				if (field.routerId === node.title) {
					Object.keys(field).forEach ( function (key) {
						if (key !== '$$hashKey')
							$scope.routerFields.push({attribute: key, value: field[key]})
					})
					return true
				}
			})

			$scope.$apply()
			if (currentTimer) {
				clearTimeout(currentTimer)
				currentTimer = null
			}
		}

		// get info for a all addresses
		var allAddressInfo = function () {
			$scope.addressFields = []
			var addressCols = [
				 {
					 field: 'address',
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
			]
			$scope.selectedAddresses = []
			$scope.addressGrid = {
				data: 'addressFields',
				columnDefs: addressCols,
				enableColumnResize: true,
				enableHorizontalScrollbar: uiGridConstants.scrollbars.NEVER,
	            enableVerticalScrollbar: uiGridConstants.scrollbars.NEVER,
				multiSelect: false,
				enableRowHeaderSelection: false,
				noUnselect: true,
				enableSelectAll: false,
				enableRowSelection: true,
				onRegisterApi: function (gridApi) {
					gridApi.selection.on.rowSelectionChanged($scope, function(row) {
						if (row.isSelected) {
							var key = row.entity.uid;
							$("#overtree").fancytree("getTree").activateKey(key);
						}
					});
			    }
			}

			var gotAllAddressFields = function ( addressFields ) {
				$scope.addressFields = 	addressFields
				$scope.$apply()
				if (currentTimer) {
					clearTimeout(currentTimer)
				}
				currentTimer = setTimeout(allAddressInfo, refreshInterval);
			}
			getAllAddressFields(gotAllAddressFields)
		}

		var getAllAddressFields = function (callback) {
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
			nodeIds = QDRService.nodeIdList()
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


		// get info for a all connections
		var allConnectionInfo = function () {
			$scope.allConnectionFields = []
			var allConnectionCols = [
				 {
					 field: 'host',
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
			]
			$scope.allConnectionGrid = {
				data: 'allConnectionFields',
				columnDefs: allConnectionCols,
				enableHorizontalScrollbar: uiGridConstants.scrollbars.NEVER,
	            enableVerticalScrollbar: uiGridConstants.scrollbars.NEVER,
				enableColumnResize: true,
				multiSelect: false,
				enableRowHeaderSelection: false,
				noUnselect: true,
				enableSelectAll: false,
				enableRowSelection: true,
				onRegisterApi: function (gridApi) {
					gridApi.selection.on.rowSelectionChanged($scope, function(row) {
						if (row.isSelected) {
							var host = row.entity.host;
							$("#overtree").fancytree("getTree").activateKey(host);
						}
					});
			    }
			}
			connections.children.forEach( function (connection) {
				$scope.allConnectionFields.push(connection.fields)
			})
			$scope.$apply()
			if (currentTimer) {
				clearTimeout(currentTimer)
				currentTimer = null
			}
		}

		// get info for a single address
		var addressInfo = function (address) {
			$scope.address = address
			$scope.addressFields = []
			var cols = [
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
			]
			$scope.addressGrid = {
				data: 'addressFields',
				columnDefs: cols,
				enableColumnResize: true,
				enableHorizontalScrollbar: uiGridConstants.scrollbars.NEVER,
	            enableVerticalScrollbar: uiGridConstants.scrollbars.NEVER,
				multiSelect: false
			}

			var fields = Object.keys(address.data.fields)
			fields.forEach( function (field) {
				if (field != "title" && field != "uid")
					$scope.addressFields.push({attribute: field, value: address.data.fields[field]})
			})

			$scope.$apply()
			if (currentTimer) {
				clearTimeout(currentTimer)
				currentTimer = null
			}
		}

		// get info for a single connection
		var connectionInfo = function (connection) {
			$scope.connection = connection
			$scope.connectionFields = []
			var cols = [
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
			]
			$scope.connectionGrid = {
				data: 'connectionFields',
				columnDefs: cols,
				enableColumnResize: true,
				enableHorizontalScrollbar: uiGridConstants.scrollbars.NEVER,
	            enableVerticalScrollbar: uiGridConstants.scrollbars.NEVER,
				multiSelect: false
			}

			var fields = Object.keys(connection.data.fields)
			fields.forEach( function (field) {
				$scope.connectionFields.push({attribute: field, value: connection.data.fields[field]})
			})

			$scope.$apply()
			if (currentTimer) {
				clearTimeout(currentTimer)
				currentTimer = null
			}
		}

		// get info for a all logs
		var allLogInfo = function () {
		}

		// get info for a single log
		var logInfo = function (node) {
			$scope.log = node
		}

		var activated = function (event, node) {
			//QDR.log.debug("node activated: " + node.data.title)
			node = node.node;
			var type = node.data.type;
			var template = $scope.templates.filter( function (tpl) {
				return tpl.name == type;
			})
			$scope.template = template[0];
			// Call the function associated with this type passing in the node that was selected
			// In dynatree I could save the function to call in the node, but not in FancyTree
			if (treeTypeMap[type])
				treeTypeMap[type](node);

			/*if (node.data.info)
				node.data.info(node)
			*/
			$scope.$apply();
		}
        $scope.template = $scope.templates[0];

		var routers = new Folder("Routers")
		routers.type = "Routers"
		//routers.info = allRouterInfo
		routers.focus = true
		routers.expanded = true
		routers.key = "Routers"
		$scope.overview.children.push(routers)
		nodeIds.forEach( function (node) {
			var name = QDRService.nameFromId(node)
			var router = new Leaf(name)
			router.type = "Router"
			//router.info = routerInfo
			router.nodeId = node
			router.key = node
			routers.children.push(router)
		})

		var expected = nodeIds.length;
		var addresses = new Folder("Addresses")
		addresses.type = "Addresses"
		//addresses.info = allAddressInfo
		addresses.key = "Addresses"
		$scope.overview.children.push(addresses)

		var gotAddressFields = function (addressFields) {
			addressFields.sort ( function (a,b) { return a.address < b.address ? -1 : a.address > b.address ? 1 : 0})
			addressFields[0].title = addressFields[0].address
			for (var i=1; i<addressFields.length; ++i) {
				if (addressFields[i].address === addressFields[i-1].address) {
					addressFields[i-1].title = addressFields[i-1].address + " (" + addressFields[i-1]['class'] + ")"
					addressFields[i].title = addressFields[i].address + " (" + addressFields[i]['class'] + ")"
				} else
					addressFields[i].title = addressFields[i].address
			}
			addressFields.forEach( function (address) {
				var a = new Leaf(address.title)
				//a.info = addressInfo
				a.key = address.uid
				a.fields = address
				a.type = "Address"
				addresses.children.push(a)
			} )
		}
		getAllAddressFields(gotAddressFields)


		var connreceived = 0;
		var connectionsObj = {}
		var connections = new Folder("Connections")
		connections.type = "Connections"
		//connections.info = allConnectionInfo
		connections.key = "Connections"
		$scope.overview.children.push(connections)
		nodeIds.forEach( function (nodeId) {

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
				})
				++connreceived;
				if (connreceived == expected) {
					var allConnections = Object.keys(connectionsObj).sort()
					allConnections.forEach(function (connection) {
						var c = new Leaf(connection)
						c.type = "Connection"
						c.icon = "ui-icon "
						c.icon += connectionsObj[connection].role === "inter-router" ? "ui-icon-refresh" : "ui-icon-transfer-e-w"
						//c.info = connectionInfo
						c.key = connection
						c.fields = connectionsObj[connection]
						c.tooltip = connectionsObj[connection].role === "inter-router" ? "inter-router connection" : "external connection"
						connections.children.push(c)
					})
				}
			})
		})

		var logsreceived = 0;
		var logObj = {}
		var logs = new Folder("Logs")
		logs.type = "Logs"
		//logs.info = allLogInfo
		logs.key = "Logs"
		//$scope.overview.children.push(logs)
		nodeIds.forEach( function (nodeId) {
			QDRService.getNodeInfo(nodeId, ".log", ["name"], function (nodeName, entity, response) {
				response.results.forEach( function (result) {
					logObj[result[0]] = 1    // use object to collapse duplicates
				})
				++logsreceived;
				if (logsreceived == expected) {
					var allLogs = Object.keys(logObj).sort()
					allLogs.forEach(function (log) {
						var l = new Leaf(log)
						l.type = "Log"
						//l.info = logInfo
						l.key = log
						logs.children.push(l)
					})
					//console.log("---------------")
					//console.dump($scope.overview.children)
					//console.log("---------------")
					$("#overtree").fancytree({
						activate: activated,
						clickFolderMode: 1,
						source: $scope.overview.children
						})
					allRouterInfo();
				}
			})
		})

        $scope.$on("$destroy", function( event ) {
			if (currentTimer) {
				clearTimeout(currentTimer)
				currentTimer = null;
			}
        });
		var treeTypeMap = {
			Routers:     allRouterInfo,
			Router:      routerInfo,
			Addresses:   allAddressInfo,
			Address:     addressInfo,
			Connections: allConnectionInfo,
			Connection:  connectionInfo
		}

    }]);

  return QDR;

}(QDR || {}));


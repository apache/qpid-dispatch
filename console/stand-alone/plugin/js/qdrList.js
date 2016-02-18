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
   * @method ListController
   * @param $scope
   * @param QDRService
   *
   * Controller for the main interface
   */
	QDR.module.controller("QDR.ListController", ['$scope', '$location', 'uiGridConstants', 'QDRService', 'QDRChartService', '$uibModal',
		function ($scope, $location, uiGridConstants, QDRService, QDRChartService, $uibModal) {

		//QDR.log.debug("started List controller");
		if (!angular.isDefined(QDRService.schema))
			return;
		$scope.selectedAction = localStorage['QDRSelectedAction'];
		$scope.selectedNode = localStorage['QDRSelectedNode'];
		$scope.selectedNodeId = localStorage['QDRSelectedNodeId'];
		$scope.selectedRecordName = localStorage['QDRSelectedRecordName'];

		var excludedEntities = ["management", "org.amqp.management", "operationalEntity", "entity", "configurationEntity", "dummy", "console"];
		var aggregateEntities = ["router.address"];

		$scope.entities = [];
		for (var entity in QDRService.schema.entityTypes) {
			if (excludedEntities.indexOf(entity) == -1) {
				$scope.entities.push( {
					title: angular.isDefined(QDRService.schema.entityTypes[entity].description) ? QDRService.schema.entityTypes[entity].description : '',
					humanName: QDRService.humanify(entity),
					name: entity}
				 );
			}
		}
		if (!angular.isDefined($scope.selectedAction)) {
			$scope.selectedAction = $scope.entities[0].name;
			//QDR.log.debug("defaulted selectedAction to " + $scope.selectedAction);
		}

		$scope.nodes = QDRService.nodeList().sort(function (a, b) { return a.name.toLowerCase() > b.name.toLowerCase()});
		if (!angular.isDefined($scope.selectedNode)) {
			//QDR.log.debug("selectedNode was " + $scope.selectedNode);
			if ($scope.nodes.length > 0) {
				$scope.selectedNode = $scope.nodes[0].name;
				$scope.selectedNodeId = $scope.nodes[0].id;
				//QDR.log.debug("forcing selectedNode to " + $scope.selectedNode);
			}
		}

		$scope.isActionActive = function(name) {
			//QDR.log.debug("isActionActive(" + name + ")  selectedAction is " + $scope.selectedAction);
			return $scope.selectedAction === name;
		};
		$scope.isNodeSelected = function (id) {
			return $scope.selectedNodeId === id;
		};

		$scope.selectNode = function(node) {
			//QDR.log.debug("setting selectedNode to " + node.name);
			$scope.selectedNode = node.name;
			$scope.selectedNodeId = node.id;
			//QDR.log.debug("location is " + $location.url());
			$location.search('n', node.name);
		};
		$scope.selectAction = function(action) {
			$scope.selectedAction = action;
			$location.search('a', action);
			//QDR.log.debug("selectAction called with " + action + "  location is now " + $location.url());
		};

		$scope.$watch('selectedAction', function(newValue, oldValue) {
			if (newValue !== oldValue) {
				localStorage['QDRSelectedAction'] = $scope.selectedAction;
				//QDR.log.debug("saving selectedAction as " + $scope.selectedAction + " newValue is " + newValue);
			}
		})
		$scope.$watch('selectedNode', function(newValue, oldValue) {
		    if (newValue !== oldValue) {
				localStorage['QDRSelectedNode'] = $scope.selectedNode;
				localStorage['QDRSelectedNodeId'] = $scope.selectedNodeId;
				//QDR.log.debug("saving selectedNode as " + $scope.selectedNode + " newValue is " + newValue);
			}
		})
		$scope.$watch('selectedRecordName', function(newValue, oldValue) {
			if (newValue != oldValue) {
				localStorage['QDRSelectedRecordName'] = $scope.selectedRecordName;
				//QDR.log.debug("saving selectedRecordName as " + $scope.selectedRecordName);
			}
		})

		$scope.tableRows = [];
		var selectedRowIndex = 0;
		var updateTableData = function (entity) {
			var gotNodeInfo = function (nodeName, dotentity, response) {
				//QDR.log.debug("got results for  " + nodeName);
				//console.dump(response);

				var records = response.results;
				var aggregates = response.aggregates;
				var attributeNames = response.attributeNames;
				var nameIndex = attributeNames.indexOf("name");
				var ent = QDRService.schema.entityTypes[entity];
				var tableRows = [];
				for (var i=0; i<records.length; ++i) {
					var record = records[i];
					var aggregate = aggregates ? aggregates[i] : undefined;
					var row = {};
					var rowName;
					if (nameIndex > -1) {
						rowName = record[nameIndex];
					} else {
						QDR.log.error("response attributeNames did not contain a name field");
						console.dump(response.attributeNames);
						return;
					}
					if (rowName == $scope.selectedRecordName)
						selectedRowIndex = i;
					for (var j=0; j<attributeNames.length; ++j) {
						var col = attributeNames[j];
						row[col] = {value: record[j], type: undefined, graph: false, title: '', aggregate: '', aggregateTip: ''};
						if (ent) {
							var att = ent.attributes[col];
							if (att) {
								row[col].type = att.type;
								row[col].graph = att.graph;
								row[col].title = att.description;

								if (aggregate) {
									if (att.graph) {
										row[col].aggregate = att.graph ? aggregate[j].sum : '';
										var tip = [];
										aggregate[j].detail.forEach( function (line) {
											tip.push(line);
										})
										row[col].aggregateTip = angular.toJson(tip);
									}
								}
							}
						}
					}
					tableRows.push(row);
				}
				setTimeout(selectRow, 0, tableRows);
			}

			// if this entity should show an aggregate column, send the request to get the info for this entity from all the nedes
			if (aggregateEntities.indexOf(entity) > -1) {
				var nodeInfo = QDRService.topology.nodeInfo();
				QDRService.getMultipleNodeInfo(Object.keys(nodeInfo), entity, [], gotNodeInfo, $scope.selectedNodeId);
			} else {
				QDRService.getNodeInfo($scope.selectedNodeId, '.' + entity, [], gotNodeInfo);
			}
		};

		// tableRows are the records that were returned, this populates the left hand table on the page
		var selectRow = function (tableRows) {
			$scope.tableRows = tableRows;
			// must apply scope here to update the tableRows before selecting the row
			$scope.$apply();
            $scope.gridApi.selection.selectRow($scope.tableRows[selectedRowIndex]);
			fixTooltips();
		}

		$scope.getTableHeight = function() {
	       return {height: ($scope.tableRows.length * 30) + "px"};
	    };
	    var titleFromAlt = function (alt) {
			if (alt && alt.length) {
				var data = angular.fromJson(alt);
				var table = "<table class='tiptable'><tbody>";
				data.forEach (function (row) {
					table += "<tr>";
					table += "<td>" + row.node + "</td><td align='right'>" + QDRService.pretty(row.val) + "</td>";
					table += "</tr>"
				})
				table += "</tbody></table>"
				return table;
			}
			return '';
	    }
		var fixTooltips = function () {
			if ($('.hastip').length == 0) {
				setTimeout(fixTooltips, 100);
				return;
			}
			$('.hastip').each( function (i, tip) {
				var tipset = tip.getAttribute('tipset')
				if (!tipset) {
					$(tip).tipsy({html: true, className: 'subTip', opacity: 1, title: function () {
						return titleFromAlt(this.getAttribute('alt'))
					} });
					tip.setAttribute('tipset', true)
				} else {
					var title = titleFromAlt(tip.getAttribute('alt'))
					tip.setAttribute('original-title', title)
				}
/*
				$(tip).tipsy({html: true, className: 'subTip', opacity: 1, title: function () {
					var alt = this.getAttribute('alt');
					if (alt && alt.length) {
						var data = angular.fromJson(alt);
						var table = "<table class='tiptable'><tbody>";
						data.forEach (function (row) {
							table += "<tr>";
							table += "<td>" + row.node + "</td><td align='right'>" + QDRService.pretty(row.val) + "</td>";
							table += "</tr>"
						})
						table += "</tbody></table>"
						return table;
					}
					return '';
				} });
				*/
			})
		}
		$scope.selectedEntity = undefined;
		for (var i=0; i<$scope.entities.length; ++i) {
			if ($scope.selectedAction === $scope.entities[i].name) {
				$scope.selectedEntity = $scope.entities[i].name;
				break;
			}
		}
		if (!angular.isDefined($scope.selectedEntity)) {
			$scope.selectedAction = $scope.entities[0].name;
			$scope.selectedEntity = $scope.entities[0].name;
		}
		var savedCharts = angular.fromJson(localStorage['QDRListCharts']);
		var getCurrentSavedCharts = function () {
			if (angular.isDefined(savedCharts)) {
				if (angular.isDefined(savedCharts[$scope.selectedEntity])) {
					//graphFields = savedCharts[$scope.selectedEntity];
				}
			} else {
				savedCharts = {};
			}
		}
		getCurrentSavedCharts();

		//QDR.log.debug("using entity of " + $scope.selectedEntity);
		var stop = undefined;

		// The left-hand table that lists the names
		var gridCols = [
			{ field: 'name',
			  displayName: '',
			  cellTemplate: '<div class="ngCellText"><span ng-cell-text>{{row.entity.name.value}}</span></div>'
			}
		];
		// the table on the left of the page contains the name field for each record that was returned
		$scope.gridDef = {
			data: 'tableRows',
			hideHeader: true,
			showHeader: false,
			enableHorizontalScrollbar: uiGridConstants.scrollbars.NEVER,
			enableVerticalScrollbar: uiGridConstants.scrollbars.NEVER,
			columnDefs: gridCols,
			enableColumnResize: true,
			multiSelect: false,
			enableRowHeaderSelection: false,
			noUnselect: true,
			enableSelectAll: false,
			enableRowSelection: true,
			onRegisterApi: function (gridApi) {
				$scope.gridApi = gridApi;
				gridApi.selection.on.rowSelectionChanged($scope, function(row) {
					if (row.isSelected)  {
						selectedRowIndex = row.rowIndex;
						$scope.selectedRecordName = row.entity.name.value;
						var details = [];
						// for each field in the new row, add a row in the details grid
						for (var name in row.entity) {
							details.push( { attributeName: QDRService.humanify(name),
											attributeValue: QDRService.pretty(row.entity[name].value),
											type: row.entity[name].type,
											name: name,
											rawValue: row.entity[name].value,
											graph: row.entity[name].graph,
											title: row.entity[name].title,
											aggregateValue: QDRService.pretty(row.entity[name].aggregate),
											aggregateTip: row.entity[name].aggregateTip})
						}
						setTimeout(updateDetails, 10, details);
					}
				});
		    }
		};

		$scope.detailFields = [];
		updateDetails = function (details) {
			$scope.detailFields = details;
			$scope.$apply();
		}

		$scope.isFieldGraphed = function(rowEntity, aggregate) {
			var dot = !aggregate ? '.' : '';
			return QDRChartService.isAttrCharted($scope.selectedNodeId, dot + $scope.selectedEntity, $scope.selectedRecordName, rowEntity.name);
		}

		$scope.addToGraph = function(rowEntity) {
			var chart = QDRChartService.registerChart($scope.selectedNodeId, "." + $scope.selectedEntity, $scope.selectedRecordName, rowEntity.name, 1000);
			doDialog("template-from-script.html", chart);
			reset();
		}

		$scope.addAllToGraph = function(rowEntity) {
			var chart = QDRChartService.registerChart($scope.selectedNodeId,
						$scope.selectedEntity,
						$scope.selectedRecordName,
						rowEntity.name,
						1000,
						false,
						true);
			doDialog("template-from-script.html", chart);
			reset();
		}

		var detailCols = [
			 {
				 field: 'attributeName',
				 cellTemplate: '<div title="{{row.entity.title}}" class="listAttrName">{{row.entity.name}}<i ng-if="row.entity.graph" ng-click="grid.appScope.addToGraph(row.entity)" ng-class="{\'active\': grid.appScope.isFieldGraphed(row.entity, false), \'icon-bar-chart\': row.entity.graph == true }"></i></div>',
				 displayName: 'Attribute'
			 },
			 {
				 field: 'attributeValue',
				 displayName: 'Value'
			 }
		];
		if (aggregateEntities.indexOf($scope.selectedEntity) > -1) {
			detailCols.push(
			 {
				 width: '10%',
				 field: 'aggregateValue',
				 displayName: 'Aggregate',
				 cellTemplate: '<div class="hastip" alt="{{row.entity.aggregateTip}}">{{row.entity.aggregateValue}}<i ng-if="row.entity.graph" ng-click="grid.appScope.addAllToGraph(row.entity)" ng-class="{\'active\': grid.appScope.isFieldGraphed(row.entity, true), \'icon-bar-chart\': row.entity.graph == true }"></i></div>',
				 cellClass: 'aggregate'
			 }
			)
		}

		// the table on the right of the page contains a row for each field in the selected record in the table on the left
		$scope.details = {
			data: 'detailFields',
			columnDefs: detailCols,
			enableHorizontalScrollbar: uiGridConstants.scrollbars.NEVER,
			enableVerticalScrollbar: uiGridConstants.scrollbars.NEVER,
			enableColumnResize: true,
			multiSelect: false,
			enableRowHeaderSelection: false,
			noUnselect: true,
			enableSelectAll: false,
			enableRowSelection: true
		};

		updateTableData($scope.selectedEntity);
		stop = setInterval(updateTableData, 5000, $scope.selectedEntity);
		$scope.getDetailsTableHeight = function() {
	       return {height: ($scope.detailFields.length * 30) + "px"};
	    };

		$scope.$on("$destroy", function( event ) {
			//QDR.log.debug("scope destroyed for qdrList");
			reset();
			if (angular.isDefined(stop)) {
				clearInterval(stop);
				stop = undefined;
			};
		});

		var reset = function () {
			if ($scope.context) {
				$scope.context.stop();
				$scope.context = null;
			}
		};

		function doDialog(template, chart) {

		    var modalInstance = $uibModal.open({
		      animation: true,
		      templateUrl: template,
		      controller: 'QDR.ListChartController',
		      resolve: {
		        chart: function () {
		          return chart;
		        }
		      }
		    });
		};

	}]);


	QDR.module.controller('QDR.ListChartController', function ($scope, $uibModalInstance, $location, QDRChartService, chart) {

	    $scope.chart = chart;
		$scope.dialogSvgChart = null;
		$scope.updateTimer = null;
		$scope.svgDivId = "dialogChart";    // the div id for the svg chart

		$scope.showChartsPage = function () {
			cleanup();
		    $uibModalInstance.close();
			$location.path("/charts");
		};

		$scope.addChartsPage = function () {
			QDRChartService.addDashboard($scope.chart);
		};

		$scope.delChartsPage = function () {
			QDRChartService.delDashboard($scope.chart);
		};

		$scope.isOnChartsPage = function () {
			return $scope.chart.dashboard;
		}

		var showChart = function () {
			// the chart divs are generated by angular and aren't available immediately
			var div = angular.element("#" + $scope.svgDivId);
			if (!div.width()) {
				setTimeout(showChart, 100);
				return;
			}
			dialogSvgChart = new QDRChartService.AreaChart($scope.chart, $location.$$path);
			$scope.dialogSvgChart = dialogSvgChart;
			updateDialogChart();
		}
		showChart();

		var updateDialogChart = function () {
			if ($scope.dialogSvgChart)
				$scope.dialogSvgChart.tick($scope.svgDivId);
			$scope.updateTimer = setTimeout(updateDialogChart, 1000);
		}

		var cleanup = function () {
			if ($scope.updateTimer) {
				clearTimeout($scope.updateTimer);
				$scope.updateTimer = null;
			}
			if (!$scope.chart.dashboard)
				QDRChartService.unRegisterChart(chart);     // remove the chart

		}
		$scope.ok = function () {
			cleanup();
	        $uibModalInstance.close();
	    };

	});
    return QDR;

} (QDR || {}));

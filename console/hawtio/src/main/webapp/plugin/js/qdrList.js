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
	QDR.module.controller("QDR.ListController", ['$scope', '$location', '$dialog', 'localStorage', 'QDRService', 'QDRChartService',
		function ($scope, $location, $dialog, localStorage, QDRService, QDRChartService) {

		if (!QDRService.connected) {
			// we are not connected. we probably got here from a bookmark or manual page reload
			$location.path("/dispatch_plugin/connect")
			$location.search('org', "list");
			return;
		}

		$scope.selectedEntity = localStorage['QDRSelectedEntity'];
		$scope.selectedNode = localStorage['QDRSelectedNode'];
		$scope.selectedNodeId = localStorage['QDRSelectedNodeId'];
		$scope.selectedRecordName = localStorage['QDRSelectedRecordName'];

		$scope.nodes = QDRService.nodeList().sort(function (a, b) { return a.name.toLowerCase() > b.name.toLowerCase()});
		if (!angular.isDefined($scope.selectedNode)) {
			//QDR.log.debug("selectedNode was " + $scope.selectedNode);
			if ($scope.nodes.length > 0) {
				$scope.selectedNode = $scope.nodes[0].name;
				$scope.selectedNodeId = $scope.nodes[0].id;
				//QDR.log.debug("forcing selectedNode to " + $scope.selectedNode);
			}
		}
		$scope.nodes.some( function (node, i) {
			if (node.name === $scope.selectedNode) {
				$scope.currentNode = $scope.nodes[i]
				return true;
			}
		})

		var excludedEntities = ["management", "org.amqp.management", "operationalEntity", "entity", "configurationEntity", "dummy", "console"];
		var aggregateEntities = ["router.address"];
		var classOverrides = {
			"connection": function (row) {
				return row.role.value;
			},
			"router.link": function (row) {
				return row.linkType.value;
			}
		}
		$scope.modes = [
		    {
		        content: '<a><i class="icon-list"></i> Attriutes</a>',
				id: 'attributes',
				title: "View router attributes",
		        isValid: function () { return true; }
		    },
		    {
		        content: '<a><i class="icon-leaf"></i> Operations</a>',
		        id: 'operations',
		        title: "Execute operations",
		        isValid: function () { return true; }
		    }
        ];
        $scope.currentMode = $scope.modes[0];
		$scope.isModeSelected = function (mode) {
			return mode === $scope.currentMode;
		}
		$scope.selectMode = function (mode) {
			$scope.currentMode = mode;
		}

        var entityTreeChildren = [];
		for (var entity in QDRService.schema.entityTypes) {
			if (excludedEntities.indexOf(entity) == -1) {
				if (!angular.isDefined($scope.selectedEntity)) {
					$scope.selectedEntity = entity;
				}
				var current = entity === $scope.selectedEntity;
				var e = new Folder(entity)
				e.typeName = "entity"
                e.key = entity
				var placeHolder = new Folder("loading...")
				placeHolder.addClass = "loading"
			    e.children = [placeHolder]
				entityTreeChildren.push(e)
			}
		}
		$scope.treeReady = function () {
			$('#entityTree').dynatree({
				onActivate: onTreeSelected,
				onExpand: onTreeNodeExpanded,
				autoCollapse: true,
				selectMode: 1,
				activeVisible: false,
				children: entityTreeChildren
			})
		};
		var onTreeNodeExpanded = function (expanded, node) {
			if (expanded)
				onTreeSelected(node);
		}
		// a tree node was selected
		var onTreeSelected = function (selectedNode) {
			if (selectedNode.data.typeName === "entity") {
				$scope.selectedEntity = selectedNode.data.key;
			} else if (selectedNode.data.typeName === 'attribute') {
				$scope.selectedEntity = selectedNode.parent.data.key;
				$scope.selectedRecordName = selectedNode.data.key;
				updateDetails(selectedNode.data.details);
				$("#entityTree").dynatree("getRoot").visit(function(node){
				   node.select(false);
				});
				selectedNode.select();
			}
			$scope.$apply();
		}
		// the data for the selected entity is available, populate the tree
		var updateEntityChildren = function (tableRows, expand) {
			var tree = $("#entityTree").dynatree("getTree");
			var node = tree.getNodeByKey($scope.selectedEntity)
			var updatedDetails = false;
			node.removeChildren();
			if (tableRows.length == 0) {
			    node.addChild({
					addClass:   "no-data",
			        typeName:   "none",
			        title:      "no data"
			    })
			    $scope.selectedRecordName = "";
			    updateDetails({});
			} else {
				tableRows.forEach( function (row) {
					var addClass = $scope.selectedEntity;
					if (classOverrides[$scope.selectedEntity]) {
						addClass += " " + classOverrides[$scope.selectedEntity](row);
					}
					var child = {
                        typeName:   "attribute",
                        addClass:   addClass,
                        key:        row.name.value,
                        title:      row.name.value,
                        details:    row
                    }
					if (row.name.value === $scope.selectedRecordName) {
						updateDetails(row);
						child.select = true;
						updatedDetails = true;
					}
					node.addChild(child)
				})
			}
			if (expand) {
				node.expand(true);
			}
			// if the selectedRecordName was not found, select the 1st one
			if (!updatedDetails && tableRows.length > 0) {
				var row = tableRows[0];
				$scope.selectedRecordName = row.name.value;
				var node = tree.getNodeByKey($scope.selectedRecordName);
				node.select(true);
				updateDetails(row)
			}
		}

		var updateDetails = function (row) {
			var details = [];
			for (var attr in row) {
				var changed = $scope.detailFields.filter(function (old) {
					return (old.name === attr) ? old.graph && old.rawValue != row[attr].value : false;
				})
				details.push( {
					attributeName:  QDRService.humanify(attr),
					attributeValue: QDRService.pretty(row[attr].value),
					name:           attr,
					changed:        changed.length,
					rawValue:       row[attr].value,
					graph:          row[attr].graph,
					title:          row[attr].title,
					aggregateValue: QDRService.pretty(row[attr].aggregate),
					aggregateTip:   row[attr].aggregateTip
				})
			}
			$scope.detailFields = details;
			aggregateColumn();
			$scope.$apply();
		}

		var restartUpdate = function () {
			if (stop) {
				clearInterval(stop)
			}
			updateTableData($scope.selectedEntity, true);
			stop = setInterval(updateTableData, 5000, $scope.selectedEntity);
		}

		$scope.selectNode = function(node) {
			$scope.selectedNode = node.name;
			$scope.selectedNodeId = node.id;
			restartUpdate();
		};
		$scope.$watch('selectedEntity', function(newValue, oldValue) {
			if (newValue !== oldValue) {
				localStorage['QDRSelectedEntity'] = $scope.selectedEntity;
				restartUpdate();
			}
		})
		$scope.$watch('selectedNode', function(newValue, oldValue) {
		    if (newValue !== oldValue) {
				localStorage['QDRSelectedNode'] = $scope.selectedNode;
				localStorage['QDRSelectedNodeId'] = $scope.selectedNodeId;
			}
		})
		$scope.$watch('selectedRecordName', function(newValue, oldValue) {
			if (newValue != oldValue) {
				localStorage['QDRSelectedRecordName'] = $scope.selectedRecordName;
			}
		})

		$scope.tableRows = [];
		var selectedRowIndex = 0;
		var updateTableData = function (entity, expand) {
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
				setTimeout(selectRow, 0, {rows: tableRows, expand: expand});
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
		var selectRow = function (info) {
			$scope.tableRows = info.rows;
			updateEntityChildren(info.rows, info.expand);
			fixTooltips();
/*
			// must apply scope here to update the tableRows before selecting the row
			$scope.$apply();
			if ($scope.gridDef.selectRow)
				$scope.gridDef.selectRow(selectedRowIndex, true);
			fixTooltips();
*/
		}

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
			})
		}
		//QDR.log.debug("using entity of " + $scope.selectedEntity);
		var stop = undefined;

		$scope.detailFields = [];

		$scope.addToGraph = function(rowEntity) {
			var chart = QDRChartService.registerChart(
				{nodeId: $scope.selectedNodeId,
				 entity: "." + $scope.selectedEntity,
				 name:   $scope.selectedRecordName,
				 attr:    rowEntity.name,
				 forceCreate: true});
			doDialog("template-from-script.html", chart);
		}

		$scope.addAllToGraph = function(rowEntity) {
			var chart = QDRChartService.registerChart({
				nodeId:     $scope.selectedNodeId,
				entity:     $scope.selectedEntity,
				name:       $scope.selectedRecordName,
				attr:       rowEntity.name,
				type:       "rate",
				rateWindow: 5000,
				visibleDuration: 1,
				forceCreate: true,
				aggregate:   true});
			doDialog("template-from-script.html", chart);
		}

		$scope.detailCols = [];
		var aggregateColumn = function () {
			if ((aggregateEntities.indexOf($scope.selectedEntity) > -1 && $scope.detailCols.length != 3) ||
				(aggregateEntities.indexOf($scope.selectedEntity) == -1 && $scope.detailCols.length != 2)) {
				// column defs have to be reassigned and not spliced, so no push/pop
				 $scope.detailCols = [
				 {
					 field: 'attributeName',
					 displayName: 'Attribute',
					 cellTemplate: '<div title="{{row.entity.title}}" class="listAttrName">{{row.entity[col.field]}}<i ng-if="row.entity.graph" ng-click="addToGraph(row.entity)" ng-class="{\'icon-bar-chart\': row.entity.graph == true }"></i></div>'
				 },
				 {
					 field: 'attributeValue',
					 displayName: 'Value',
					 cellTemplate: '<div class="ngCellText" ng-class="{\'changed\': row.entity.changed == 1}"><span>{{row.getProperty(col.field)}}</span></div>'
				 }
				 ]
				if (aggregateEntities.indexOf($scope.selectedEntity) > -1) {
					$scope.detailCols.push(
					 {
						 width: '10%',
						 field: 'aggregateValue',
						 displayName: 'Aggregate',
						 cellTemplate: '<div class="hastip" alt="{{row.entity.aggregateTip}}"><span ng-class="{\'changed\': row.entity.changed == 1}">{{row.entity[col.field]}}</span><i ng-if="row.entity.graph" ng-click="addAllToGraph(row.entity)" ng-class="{\'icon-bar-chart\': row.entity.graph == true }"></i></div>',
						 cellClass: 'aggregate'
					 }
					)
				}
			}
			if ($scope.selectedRecordName === "")
				$scope.detailCols = [];
		}

		// the table on the right of the page contains a row for each field in the selected record in the table on the left
		$scope.details = {
			data: 'detailFields',
			columnDefs: "detailCols",
			enableColumnResize: true,
			multiSelect: false,
			beforeSelectionChange: function() {
				  return false;
			}
		};

		updateTableData($scope.selectedEntity, true);
		stop = setInterval(updateTableData, 5000, $scope.selectedEntity);

		$scope.$on("$destroy", function( event ) {
			//QDR.log.debug("scope destroyed for qdrList");
			if (angular.isDefined(stop)) {
				clearInterval(stop);
				stop = undefined;
			};
		});

		function doDialog(template, chart) {
		    var d = $dialog.dialog({
		      backdrop: true,
		      keyboard: true,
		      backdropClick: true,
		      templateUrl: template,
		      controller: "QDR.ListChartController",
		      resolve: {
                 chart: function() {
                   return chart
                 },
                 nodeName: function () {
                    return $scope.selectedNode
                 }
              }
		    });

		    d.open().then(function(result) { console.log("d.open().then"); });

		};
	}]);

	QDR.module.controller('QDR.ListChartController', function ($scope, dialog, $dialog, $location, localStorage, QDRChartService, chart, nodeName) {
	    $scope.chart = chart;
		$scope.dialogSvgChart = null;
		var updateTimer = null;
		$scope.svgDivId = "dialogChart";    // the div id for the svg chart

		$scope.showChartsPage = function () {
			cleanup();
			dialog.close(true);
			$location.path("/dispatch_plugin/charts");
		};

		$scope.addHChart = function () {
	        QDRChartService.addHDash($scope.chart);
			cleanup();
	        dialog.close(true);
		}

		$scope.addToDashboardLink = function () {
			var href = "#/dispatch_plugin/charts";
			var size = angular.toJson({
	                size_x: 2,
	                size_y: 2
	              });

			var params = angular.toJson({chid: $scope.chart.id()});
	        var title = "Dispatch - " + nodeName;
		    return "/hawtio/#/dashboard/add?tab=dashboard" +
		          "&href=" + encodeURIComponent(href) +
		          "&routeParams=" + encodeURIComponent(params) +
		          "&title=" + encodeURIComponent(title) +
		          "&size=" + encodeURIComponent(size);
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
			dialogSvgChart = new QDRChartService.AreaChart($scope.chart);
			$scope.dialogSvgChart = dialogSvgChart;
			updateDialogChart();
		}
		showChart();

		var updateDialogChart = function () {
			if ($scope.dialogSvgChart)
				$scope.dialogSvgChart.tick($scope.svgDivId);
			if (updateTimer)
				clearTimeout(updateTimer)
			updateTimer = setTimeout(updateDialogChart, 1000);
		}

		var cleanup = function () {
			if (updateTimer) {
				clearTimeout(updateTimer);
				updateTimer = null;
			}
			if (!$scope.chart.hdash)
				QDRChartService.unRegisterChart($scope.chart);     // remove the chart

		}
		$scope.ok = function () {
			cleanup();
			dialog.close(true);
			//dialogService.cancel("myDialog");
	    };

	});
    return QDR;

} (QDR || {}));

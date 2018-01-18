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
   * Controller for the main interface
   */
  QDR.module.controller("QDR.ListController", ['$scope', '$location', '$uibModal', '$filter', '$timeout', 'QDRService', 'QDRChartService', 'uiGridConstants', '$sce',
    function ($scope, $location, $uibModal, $filter, $timeout, QDRService, QDRChartService, uiGridConstants, $sce) {

    QDR.log.debug("QDR.ListControll started with location of " + $location.path() + " and connection of  " + QDRService.management.connection.is_connected());
    var updateIntervalHandle = undefined;
    var updateInterval = 5000;
    var ListExpandedKey = "QDRListExpanded";
    var SelectedEntityKey = "QDRSelectedEntity"
    var ActivatedKey = "QDRActivatedKey"
    $scope.details = {};

    $scope.tmplListTree = QDR.templatePath + 'tmplListTree.html';
    $scope.selectedEntity = localStorage[SelectedEntityKey] || "address";
    $scope.ActivatedKey = localStorage[ActivatedKey] || null;
    if ($scope.selectedEntity == "undefined")
      $scope.selectedEntity = undefined
    $scope.selectedNode = localStorage['QDRSelectedNode'];
    $scope.selectedNodeId = localStorage['QDRSelectedNodeId'];
    $scope.selectedRecordName = localStorage['QDRSelectedRecordName'];
    $scope.nodes = []
    $scope.currentNode = undefined;
    $scope.modes = [
      {
        content: '<a><i class="icon-list"></i> Attributes</a>',
        id: 'attributes',
        op: 'READ',
        title: "View router attributes",
        isValid: function () { return true; }
      },
      {
        content: '<a><i class="icon-edit"></i> Update</a>',
        id: 'operations',
        op: 'UPDATE',
        title: "Update this attribute",
        isValid: function () {
          return $scope.operations.indexOf(this.op) > -1
        }
      },
      {
        content: '<a><i class="icon-plus"></i> Create</a>',
        id: 'operations',
        op: 'CREATE',
        title: "Create a new attribute",
        isValid: function () { return $scope.operations.indexOf(this.op) > -1 }
      },
      {
        content: '<a><i class="icon-remove"></i> Delete</a>',
        id: 'delete',
        op: 'DELETE',
        title: "Delete",
        isValid: function () { return $scope.operations.indexOf(this.op) > -1 }
      },
      {
        content: '<a><i class="icon-eye-open"></i> Fetch</a>',
        id: 'log',
        op: 'GET-LOG',
        title: "Fetch recent log entries",
        isValid: function () { return ($scope.selectedEntity === 'log') }
      }
    ];
    $scope.operations = []
    $scope.currentMode = $scope.modes[0];
    $scope.isModeSelected = function (mode) {
      return mode === $scope.currentMode;
    }
    $scope.fetchingLog = false;
    $scope.selectMode = function (mode) {
      $scope.currentMode = mode;
      if (mode.id === 'log') {
        $scope.logResults = [];
        $scope.fetchingLog = true;
        var entity; // undefined since it is not supported in the GET-LOG call
        QDRService.management.connection.sendMethod($scope.currentNode.id, entity, {}, $scope.currentMode.op)
          .then( function (response) {
          var statusCode = response.context.message.application_properties.statusCode;
          if (statusCode < 200 || statusCode >= 300) {
            Core.notification('error', response.context.message.statusDescription);
            QDR.log.error('Error ' + response.context.message.statusDescription)
            return;
          }
          $timeout( function () {
            $scope.fetchingLog = false;
            $scope.logResults = response.response.filter( function (entry) {
              return entry[0] === $scope.detailsObject.module
            }).sort( function (a, b) {
              return b[5] - a[5]
            }).map( function (entry) {
              return {
                type: entry[1],
                message: entry[2],
                source: entry[3],
                line: entry[4],
                time: Date(entry[5]).toString()
              }
            })
          })
        })
      }
    }
    $scope.isValid = function (mode) {
      return mode.isValid()
    }

    $scope.expandAll = function () {
      $("#entityTree").fancytree("getTree").visit(function(node){
        node.setExpanded(true);
      });
    }
    $scope.contractAll = function () {
      $("#entityTree").fancytree("getTree").visit(function(node){
        node.setExpanded(false);
      });
    }

    if (!QDRService.management.connection.is_connected()) {
      // we are not connected. we probably got here from a bookmark or manual page reload
      QDR.redirectWhenConnected($location, "list")
      return;
    }

    $scope.nodes = []
    var excludedEntities = ["management", "org.amqp.management", "operationalEntity", "entity", "configurationEntity", "dummy", "console"];
    var aggregateEntities = ["router.address"];

    var classOverrides = {
      "connection": function (row, nodeId) {
        var isConsole = QDRService.utilities.isAConsole (row.properties.value, row.identity.value, row.role.value, nodeId)
        return isConsole ? "console" : row.role.value === "inter-router" ? "inter-router" : "external";
      },
      "router.link": function (row, nodeId) {
        var link = {nodeId: nodeId, connectionId: row.connectionId.value}

        var isConsole = QDRService.utilities.isConsole(QDRService.management.topology.getConnForLink(link))
        return isConsole ? "console" : row.linkType.value;
      },
      "router.address": function (row) {
        var identity = QDRService.utilities.identity_clean(row.identity.value)
        var address = QDRService.utilities.addr_text(identity)
        var cls = QDRService.utilities.addr_class(identity)
        if (address === "$management")
          cls = "internal " + cls
        return cls
      }
    }

    var lookupOperations = function () {
      var ops = QDRService.management.schema().entityTypes[$scope.selectedEntity].operations.filter( function (op) { return op !== 'READ'});
      $scope.operation = ops.length ? ops[0] : "";
      return ops;
    }
    var entityTreeChildren = [];
    var expandedList = angular.fromJson(localStorage[ListExpandedKey]) || [];
    var saveExpanded = function () {
      // save the list of entities that are expanded
      var tree = $("#entityTree").fancytree("getTree");
      var list = []
      tree.visit( function (tnode) {
        if (tnode.isExpanded()) {
          list.push(tnode.key)
        }
      })
      localStorage[ListExpandedKey] = JSON.stringify(list)
    }

    var onTreeNodeBeforeActivate = function (event, data) {
      // if node is toplevel entity
      if (data.node.data.typeName === "entity") {
        // if the current active node is not this one and not one of its children
        var active = data.tree.getActiveNode()
        if (active && !data.node.isActive() && data.node.isExpanded()) {  // there is an active node and it's not this one
          var any = false
          var children = data.node.getChildren()
          if (children) {
            any = children.some( function (child) {
              return child.key === active.key
            })
          }
          if (!any) // none of the clicked on node's children was active
            return false  // don't activate, just collapse this top level node
        }
      }
      return true
    }
    var onTreeNodeExpanded = function (event, data) {
      saveExpanded()
      updateExpandedEntities()
    }
    // a tree node was selected
    var onTreeNodeActivated = function (event, data) {
      $scope.ActivatedKey = data.node.key
      var selectedNode = data.node
      $scope.selectedTreeNode = data.node
      $timeout( function () {
        if ($scope.currentMode.id === 'operations')
          $scope.currentMode = $scope.modes[0];
        else if ($scope.currentMode.id === 'log')
          $scope.selectMode($scope.currentMode)
        else if ($scope.currentMode.id === 'delete') {
          // clicked on a tree node while on the delete screen -> switch to attribute screen
          $scope.currentMode = $scope.modes[0];
        }
        if (selectedNode.data.typeName === "entity") {
          $scope.selectedEntity = selectedNode.key;
          $scope.operations = lookupOperations()
          updateExpandedEntities()
        } else if (selectedNode.data.typeName === 'attribute') {
          $scope.selectedEntity = selectedNode.parent.key;
          $scope.operations = lookupOperations()
          $scope.selectedRecordName = selectedNode.key;
          updateDetails(selectedNode.data.details);   // update the table on the right
        } else if (selectedNode.data.typeName === 'none') {
          $scope.selectedEntity = selectedNode.parent.key;
          $scope.selectedRecordName = $scope.selectedEntity
          updateDetails(fromSchema($scope.selectedEntity));

        }
      })
    }
    var getExpanded = function (tree) {
      var list = []
      tree.visit( function (tnode) {
        if (tnode.isExpanded()) {
          list.push(tnode)
        }
      })
      return list
    }
    // fill in an empty results recoord based on the entities schema
    var fromSchema = function (entityName) {
      var row = {}
      var schemaEntity = QDRService.management.schema().entityTypes[entityName]
      for (attr in schemaEntity.attributes) {
        var entity = schemaEntity.attributes[attr]
        var value = ""
        if (angular.isDefined(entity['default'])) {
          if (entity['type'] === 'integer')
            value = parseInt(entity['default']) // some default values that are marked as integer are passed as string
          else
            value = entity['default']
        }
        row[attr] = {
          value: value,
          type: entity.type,
          graph: false,
          title: entity.description,
          aggregate: false,
          aggregateTip: '',
          'default': entity['default']
        }
      }
      return row;
    }
    $scope.hasCreate = function () {
      var schemaEntity = QDRService.management.schema().entityTypes[$scope.selectedEntity]
      return (schemaEntity.operations.indexOf("CREATE") > -1)
    }

    var getActiveChild = function (node) {
      var active = node.children.filter(function (child) {
        return child.isActive()
      })
      if (active.length > 0)
        return active[0].key
      return null
    }
    // the data for the selected entity is available, populate the tree on the left
    var updateTreeChildren = function (entity, tableRows, expand) {
      var tree = $("#entityTree").fancytree("getTree"), node;
      if (tree) {
        node = tree.getNodeByKey(entity)
      }
      if (!tree || !node) {
        return
      }
      var wasActive = node.isActive()
      var wasExpanded = node.isExpanded()
      var activeChildKey = getActiveChild(node)
      node.removeChildren()
      var updatedDetails = false;
      if (tableRows.length == 0) {
        var newNode = {
          extraClasses:   "no-data",
          typeName:   "none",
          title:      "no data",
          key:        node.key + ".1"
        }
        node.addNode(newNode)
        if (expand) {
          updateDetails(fromSchema(entity));
          $scope.selectedRecordName = entity;
        }
      } else {
        var children = tableRows.map( function (row) {
          var addClass = entity;
          if (classOverrides[entity]) {
            addClass += " " + classOverrides[entity](row, $scope.currentNode.id);
          }
          var child = {
            typeName:   "attribute",
            extraClasses:   addClass,
            tooltip:    addClass,
            key:        row.name.value,
            title:      row.name.value,
            details:    row
          }
          return child
        })
        node.addNode(children)
      }
      // top level node was expanded
      if (wasExpanded)
        node.setExpanded(true, {noAnimation: true, noEvents: true})
      // if the parent node was active, but none of the children were active, active the 1st child
      if (wasActive) {
        if (!activeChildKey) {
          activeChildKey = node.children[0].key
        }
      }
      if (!tree.getActiveNode())
         activeChildKey = $scope.ActivatedKey
      // re-active the previously active child node
      if (activeChildKey) {
        var newNode = tree.getNodeByKey(activeChildKey)
        // the node may not be there after the update
        if (newNode)
          newNode.setActive(true) // fires the onTreeNodeActivated event for this node
      }
      resizer()
    }


    var resizer = function () {
      // this forces the tree and the grid to be the size of the browser window.
      // the effect is that the tree and the grid will have vertical scroll bars if needed.
      // the alternative is to let the tree and grid determine the size of the page and have
      // the scroll bar on the window
      var viewport = $('#list-controller .pane-viewport')
      viewport.height( window.innerHeight - viewport.offset().top)
      // don't allow HTML in the tree titles
      $('.fancytree-title').each( function (idx) {
        var unsafe = $(this).html()
        $(this).html(unsafe.replace(/</g, "&lt;").replace(/>/g, "&gt;"))
      })
    }
    $(window).resize(resizer);

    var schemaProps = function (entityName, key, currentNode) {
         var typeMap = {integer: 'number', string: 'text', path: 'text', boolean: 'boolean', map: 'textarea'};

      var entity = QDRService.management.schema().entityTypes[entityName]
      var value = entity.attributes[key]
      // skip identity and depricated fields
      if (!value)
        return {input: 'input', type: 'disabled', required: false, selected: "", rawtype: 'string', disabled: true, 'default': ''}
      var description = value.description || ""
      var val = value['default'];
      var disabled = (key == 'identity' || description.startsWith('Deprecated'))
      // special cases
      if (entityName == 'log' && key == 'module') {
        return {input: 'input', type: 'disabled', required: false, selected: "", rawtype: 'string', disabled: true, 'default': ''}
      }
      if (entityName === 'linkRoutePattern' && key === 'connector') {
        // turn input into a select. the values will be populated later
        value.type = []
        // find all the connector names and populate the select
        QDRService.management.topology.fetchEntity(currentNode.id, 'connector', ['name'], function (nodeName, dotentity, response) {
          $scope.detailFields.some( function (field) {
            if (field.name === 'connector') {
              field.rawtype = response.results.map (function (result) {return result[0]})
              return true;
            }
          })
        });
      }
      return {    name:       key,
            humanName:  QDRService.utilities.humanify(key),
                        description:value.description,
                        type:       disabled ? 'disabled' : typeMap[value.type],
                        rawtype:    value.type,
                        input:      typeof value.type == 'string' ? value.type == 'boolean' ? 'boolean' : 'input'
                                                                  : 'select',
                        selected:   val ? val : undefined,
                        'default':  value['default'],
                        value:      val,
                        required:   value.required,
                        unique:     value.unique,
                        disabled:   disabled
            };
    }
    $scope.getAttributeValue = function (attribute) {
      var value = attribute.attributeValue;
      if ($scope.currentMode.op === "CREATE" && attribute.name === 'identity')
        value = "<assigned by system>"
      return value;
    }

    // update the table on the right
    var updateDetails = function (row) {
      var details = [];
      $scope.detailsObject = {};
      var attrs = Object.keys(row).sort();
      attrs.forEach( function (attr) {
        var changed = $scope.detailFields.filter(function (old) {
          return (old.name === attr) ? old.graph && old.rawValue != row[attr].value : false;
        })
        var schemaEntity = schemaProps($scope.selectedEntity, attr, $scope.currentNode)
        details.push( {
          attributeName:  QDRService.utilities.humanify(attr),
          attributeValue: attr === 'port' ? row[attr].value : QDRService.utilities.pretty(row[attr].value),
          name:           attr,
          changed:        changed.length,
          rawValue:       row[attr].value,
          graph:          row[attr].graph,
          title:          row[attr].title,
          chartExists:    (QDRChartService.isAttrCharted($scope.currentNode.id, $scope.selectedEntity, row.name.value, attr)),
          aggchartExists: (QDRChartService.isAttrCharted($scope.currentNode.id, $scope.selectedEntity, row.name.value, attr, true)),
          aggregateValue: QDRService.utilities.pretty(row[attr].aggregate),
          aggregateTip:   row[attr].aggregateTip,

          input:          schemaEntity.input,
          type:           schemaEntity.type,
          required:       schemaEntity.required,
          unique:         schemaEntity.unique,
          selected:       schemaEntity.selected,
          rawtype:        schemaEntity.rawtype,
          disabled:       schemaEntity.disabled,
          'default':      schemaEntity['default']
        })
        $scope.detailsObject[attr] = row[attr].value;
      })
      $scope.detailFields = details;
      aggregateColumn();
    }

    // called from html ng-style="getTableHeight()"
    $scope.getTableHeight = function () {
      return {
        height: ($scope.detailFields.length * 30 + 40) + 'px'
      }
    }

    var updateExpandedEntities = function () {
      clearTimeout(updateIntervalHandle)
      var tree = $("#entityTree").fancytree("getTree");
      if (tree) {
        var q = d3.queue(10)
        var expanded = getExpanded(tree)
        expanded.forEach( function (node) {
          q.defer(q_updateTableData, node.key, node.key === $scope.selectedEntity)
        })

        q.await(function (error) {
          if (error)
            QDR.log.error(error.message)

          if (!tree.getActiveNode()) {
            if ($scope.ActivatedKey) {
              var node = tree.getNodeByKey($scope.ActivatedKey)
              if (node) {
                node.setActive(true, {noEvents: true})
              }
            }
            if (!tree.getActiveNode()) {
              var first = tree.getFirstChild()
              if (first) {
                var child = first.getFirstChild()
                if (child)
                  first = child
              }
              first.setActive(true)
            }
          }

          d3.selectAll('.ui-effects-placeholder').style('height', '0px')

          // once all expanded tree nodes have been update, schedule another update
          updateIntervalHandle = setTimeout(updateExpandedEntities, updateInterval)
        })
      }
    }

    $scope.selectNode = function(node) {
      $scope.selectedNode = node.name;
      $scope.selectedNodeId = node.id;
      $timeout(setCurrentNode);
    };
    $scope.$watch('ActivatedKey', function(newValue, oldValue) {
      if (newValue !== oldValue) {
        localStorage[ActivatedKey] = $scope.ActivatedKey;
      }
    })
    $scope.$watch('selectedEntity', function(newValue, oldValue) {
      if (newValue !== oldValue) {
        localStorage['QDRSelectedEntity'] = $scope.selectedEntity;
        $scope.operations = lookupOperations()
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

    /* Called periodically to refresh the data on the page */
    var q_updateTableData = function (entity, expand, callback) {
      // don't update the data when on the operations tabs
      if ($scope.currentMode.id !== 'attributes') {
        callback(null)
        return;
      }

      var gotNodeInfo = function (nodeName, dotentity, response) {
        var tableRows = [];
        var records = response.results;
        var aggregates = response.aggregates;
        var attributeNames = response.attributeNames;
        // If !attributeNmes then  there was an error getting the records for this entity
        if (attributeNames) {
          var nameIndex = attributeNames.indexOf("name");
          var identityIndex = attributeNames.indexOf("identity");
          var ent = QDRService.management.schema().entityTypes[entity];
          for (var i=0; i<records.length; ++i) {
            var record = records[i];
            var aggregate = aggregates ? aggregates[i] : undefined;
            var row = {};
            var rowName;
            if (nameIndex > -1) {
              rowName = record[nameIndex];
              if (!rowName && identityIndex > -1) {
                rowName = record[nameIndex] = (dotentity + '/' + record[identityIndex])
              }
            }
            if (!rowName) {
              var msg = "response attributeNames did not contain a name field"
              QDR.log.error(msg);
              console.dump(response.attributeNames);
              callback(Error(msg))
              return;
            }
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
          tableRows.sort( function (a, b) { return a.name.value.localeCompare(b.name.value) })
          selectRow({entity: dotentity, rows: tableRows, expand: expand});
        }
        callback(null)  // let queue handler know we are done
      }
      // if this entity should show an aggregate column, send the request to get the info for this entity from all the nedes
      if (aggregateEntities.indexOf(entity) > -1) {
        var nodeIdList = QDRService.management.topology.nodeIdList();
        QDRService.management.topology.getMultipleNodeInfo(nodeIdList, entity, [], gotNodeInfo, $scope.selectedNodeId);
      } else {
        QDRService.management.topology.fetchEntity($scope.selectedNodeId, entity, [], gotNodeInfo);
      }
    };

    // tableRows are the records that were returned, this populates the left hand tree on the page
    var selectRow = function (info) {
      updateTreeChildren(info.entity, info.rows, info.expand);
      //fixTooltips();
    }
    $scope.detailFields = [];

    $scope.addToGraph = function(rowEntity) {
      var chart = QDRChartService.registerChart(
        {nodeId: $scope.selectedNodeId,
         entity: $scope.selectedEntity,
         name:   $scope.selectedRecordName,
         attr:    rowEntity.name,
         forceCreate: true});

      doDialog('tmplChartConfig.html', chart);
    }

    $scope.addAllToGraph = function(rowEntity) {
      var chart = QDRChartService.registerChart({
        nodeId:     $scope.selectedNodeId,
        entity:     $scope.selectedEntity,
        name:       $scope.selectedRecordName,
        attr:       rowEntity.name,
        type:       "rate",
        rateWindow: updateInterval,
        visibleDuration: 0.25,
        forceCreate: true,
        aggregate:   true});
      doDialog('tmplChartConfig.html', chart);
    }

    // The ui-popover dynamic html
    $scope.aggregateTip = ''
    // disable popover tips for non-integer cells
    $scope.aggregateTipEnabled = function (row) {
      var tip = row.entity.aggregateTip
      return (tip && tip.length) ? "true" : "false"
    }
    // convert the aggregate data into a table for the popover tip
    $scope.genAggregateTip = function (row) {
      var tip = row.entity.aggregateTip
      if (tip && tip.length) {
        var data = angular.fromJson(tip);
        var table = "<table class='tiptable'><tbody>";
        data.forEach (function (row) {
          table += "<tr>";
          table += "<td>" + row.node + "</td><td align='right'>" + QDRService.utilities.pretty(row.val) + "</td>";
          table += "</tr>"
        })
        table += "</tbody></table>"
        $scope.aggregateTip = $sce.trustAsHtml(table)
      }
    }
    var aggregateColumn = function () {
      if ((aggregateEntities.indexOf($scope.selectedEntity) > -1 && $scope.detailCols.length != 3) ||
        (aggregateEntities.indexOf($scope.selectedEntity) == -1 && $scope.detailCols.length != 2)) {
        // column defs have to be reassigned and not spliced, so no push/pop
        $scope.detailCols = [
        {
          field: 'attributeName',
          displayName: 'Attribute',
          cellTemplate: '<div title="{{row.entity.title}}" class="listAttrName ui-grid-cell-contents">{{COL_FIELD CUSTOM_FILTERS | pretty}}<button ng-if="row.entity.graph" title="Click to view/add a graph" ng-click="grid.appScope.addToGraph(row.entity)" ng-class="{\'btn-success\': row.entity.chartExists}" class="btn"><i ng-class="{\'icon-bar-chart\': row.entity.graph == true }"></i></button></div>'
        },
        {
          field: 'attributeValue',
          displayName: 'Value',
          cellTemplate: '<div class="ui-grid-cell-contents" ng-class="{\'changed\': row.entity.changed == 1}">{{COL_FIELD CUSTOM_FILTERS | pretty}}</div>'
        }
        ]
        if (aggregateEntities.indexOf($scope.selectedEntity) > -1) {
          $scope.detailCols.push(
            {
              width: '10%',
              field: 'aggregateValue',
              displayName: 'Aggregate',
              cellTemplate: '<div popover-enable="{{grid.appScope.aggregateTipEnabled(row)}}" uib-popover-html="grid.appScope.aggregateTip" popover-append-to-body="true" ng-mouseover="grid.appScope.genAggregateTip(row)" popover-trigger="\'mouseenter\'" class="listAggrValue ui-grid-cell-contents" ng-class="{\'changed\': row.entity.changed == 1}">{{COL_FIELD CUSTOM_FILTERS}} <button title="Click to view/add a graph" ng-if="row.entity.graph" ng-click="grid.appScope.addAllToGraph(row.entity)" ng-class="{\'btn-success\': row.entity.aggchartExists}" class="btn"><i ng-class="{\'icon-bar-chart\': row.entity.graph == true }"></i></button></div>',
              cellClass: 'aggregate'
            }
          )
        }
      }
      if ($scope.selectedRecordName === "")
        $scope.detailCols = [];

      $scope.details.columnDefs = $scope.detailCols
      if ($scope.gridApi)
        $scope.gridApi.core.notifyDataChange( uiGridConstants.dataChange.COLUMN );
    }

    $scope.gridApi = undefined;
    // the table on the right of the page contains a row for each field in the selected record in the table on the left
    $scope.desiredTableHeight = 340;
    $scope.detailCols = [];
    $scope.details = {
      data: 'detailFields',
      columnDefs: [
        {
          field: 'attributeName',
          displayName: 'Attribute',
          cellTemplate: '<div title="{{row.entity.title}}" class="listAttrName ui-grid-cell-contents">{{COL_FIELD CUSTOM_FILTERS | pretty}}<button ng-if="row.entity.graph" title="Click to view/add a graph" ng-click="grid.appScope.addToGraph(row.entity)" ng-class="{\'btn-success\': row.entity.chartExists}" class="btn"><i ng-class="{\'icon-bar-chart\': row.entity.graph == true }"></i></button></div>'
        },
        {
          field: 'attributeValue',
          displayName: 'Value',
          cellTemplate: '<div class="ui-grid-cell-contents" ng-class="{\'changed\': row.entity.changed == 1}">{{COL_FIELD CUSTOM_FILTERS | pretty}}</div>'
        }
      ],
      enableColumnResize: true,
      multiSelect: false,
      jqueryUIDraggable: true,
      onRegisterApi: function(gridApi) {
        $scope.gridApi = gridApi;
      }
    };

    $scope.$on("$destroy", function( event ) {
      clearTimeout(updateIntervalHandle)
      $(window).off("resize", resizer);
    });

    function gotMethodResponse (entity, context) {
      var statusCode = context.message.application_properties.statusCode;
      if (statusCode < 200 || statusCode >= 300) {
        var note = "Failed to " + $filter('Pascalcase')($scope.currentMode.op) + " " + entity + ": " + context.message.application_properties.statusDescription
        Core.notification('error', note);
        QDR.log.error('Error ' + note)
      } else {
        var note = entity + " " + $filter('Pascalcase')($scope.currentMode.op) + "d"
        Core.notification('success', note);
        QDR.log.debug('Success ' + note)
        $scope.selectMode($scope.modes[0]);
      }
    }
    $scope.ok = function () {
      var attributes = {}
      $scope.detailFields.forEach( function (field) {
        var value = field.rawValue;
        if (field.input === 'input') {
          if (field.type === 'text' || field.type === 'disabled')
            value = field.attributeValue;
        } else if (field.input === 'select') {
          value = field.selected;
        } else if (field.input === 'boolean') {
          value = field.rawValue
        }
        if (value === "")
          value = undefined;

        if ((value && value != field['default']) || field.required || (field.name === 'role')) {
          if (field.name !== 'identity')
            attributes[field.name] = value
        }
      })
      QDRService.management.connection.sendMethod($scope.currentNode.id, $scope.selectedEntity, attributes, $scope.currentMode.op)
       .then(function (response) {gotMethodResponse($scope.selectedEntity, response.context)})
    }
    $scope.remove = function () {
      var identity = $scope.selectedTreeNode.data.details.identity.value
      var attributes = {type: $scope.selectedEntity, identity: identity}
      QDRService.management.connection.sendMethod($scope.currentNode.id, $scope.selectedEntity, attributes, $scope.currentMode.op)
       .then(function (response) {gotMethodResponse($scope.selectedEntity, response.context)})
    }

    function doDialog(template, chart) {

      var d = $uibModal.open({
      backdrop: true,
      keyboard: true,
      backdropClick: true,
      templateUrl: QDR.templatePath + template,
      controller: "QDR.ChartDialogController",
      resolve: {
        chart: function() {
          return chart;
        },
        updateTick: function () {
          return function () {};
        },
        dashboard: function () {
          return $scope;
        },
        adding: function () {
          return true
        }
      }
      }).result.then(function(result) {
        QDRChartService.unRegisterChart(chart)
      });
    };
    var setCurrentNode = function () {
      $scope.nodes.some( function (node, i) {
        if (node.name === $scope.selectedNode) {
          $scope.currentNode = $scope.nodes[i]
          return true;
        }
      })
    }

    var treeReady = false;
    var serviceReady = false;
    $scope.largeNetwork = QDRService.management.topology.isLargeNetwork()
    if ($scope.largeNetwork)
      aggregateEntities = [];

    // called after we know for sure the schema is fetched and the routers are all ready
    QDRService.management.topology.addUpdatedAction("initList", function () {
      QDRService.management.topology.stopUpdating();
      QDRService.management.topology.delUpdatedAction("initList")

      $scope.nodes = QDRService.management.topology.nodeList().sort(function (a, b) { return a.name.toLowerCase() > b.name.toLowerCase()});
      // unable to get node list? Bail.
      if ($scope.nodes.length == 0) {
        $location.path("/" + QDR.pluginName + "/connect")
        $location.search('org', "list");
      }
      if (!angular.isDefined($scope.selectedNode)) {
        //QDR.log.debug("selectedNode was " + $scope.selectedNode);
        if ($scope.nodes.length > 0) {
          $scope.selectedNode = $scope.nodes[0].name;
          $scope.selectedNodeId = $scope.nodes[0].id;
          //QDR.log.debug("forcing selectedNode to " + $scope.selectedNode);
        }
      }
      setCurrentNode();
      if ($scope.currentNode == undefined) {
        if ($scope.nodes.length > 0) {
          $scope.selectedNode = $scope.nodes[0].name;
          $scope.selectedNodeId = $scope.nodes[0].id;
          $scope.currentNode = $scope.nodes[0];
        }
      }
      var sortedEntities = Object.keys(QDRService.management.schema().entityTypes).sort();
      sortedEntities.forEach( function (entity) {
        if (excludedEntities.indexOf(entity) == -1) {
          if (!angular.isDefined($scope.selectedEntity))
            $scope.selectedEntity = entity;
          $scope.operations = lookupOperations()
          var e = new Folder(entity)
          e.typeName = "entity"
          e.key = entity
          e.expanded = (expandedList.indexOf(entity) > -1)
          var placeHolder = new Leaf("loading...")
          placeHolder.addClass = "loading"
          e.children = [placeHolder]
          entityTreeChildren.push(e)
        }
      })
      serviceReady = true;
      initTree();
    })
    // called by ng-init="treeReady()" in tmplListTree.html
    $scope.treeReady = function () {
      treeReady = true;
      initTree();
    }

    // this gets called once tree is initialized
    var onTreeInitialized = function (event, data) {
      updateExpandedEntities();
    }

    var initTree = function () {
      if (!treeReady || !serviceReady)
        return;
      $('#entityTree').fancytree({
        activate: onTreeNodeActivated,
        expand: onTreeNodeExpanded,
        beforeActivate: onTreeNodeBeforeActivate,
        init: onTreeInitialized,
        selectMode: 1,
        autoCollapse: $scope.largeNetwork,
        activeVisible: !$scope.largeNetwork,
        clickFolderMode: 3,
        debugLevel: 0,
        extraClasses: {
          expander: 'fa-angle',
          connector: 'fancytree-no-connector'
          },
        source: entityTreeChildren
      })
    };
    QDRService.management.topology.ensureAllEntities({entity: "connection"}, function () {
      QDRService.management.topology.setUpdateEntities(["connection"])
      QDRService.management.topology.startUpdating(true);
    })
  }]);

    return QDR;

} (QDR || {}));

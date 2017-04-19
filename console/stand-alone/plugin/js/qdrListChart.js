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
  QDR.module.controller("QDR.ListController", ['$scope', '$location', '$uibModal', '$filter', '$timeout', 'QDRService', 'QDRChartService',
    function ($scope, $location, $uibModal, $filter, $timeout, QDRService, QDRChartService) {

    var updateIntervalHandle = undefined;
    var updateInterval = 5000;
    var ListExpandedKey = "QDRListExpanded";
    $scope.details = {};

    $scope.tmplListTree = QDR.templatePath + 'tmplListTree.html';
    $scope.selectedEntity = localStorage['QDRSelectedEntity'] || "address";
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
          //QDR.log.debug("isValid UPDAATE? " + this.op)
          //console.dump($scope.operations)
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
        QDRService.sendMethod($scope.currentNode.id, entity, {}, $scope.currentMode.op, {}, function (nodeName, entity, response, context) {
          $scope.fetchingLog = false;
          var statusCode = context.message.application_properties.statusCode;
          if (statusCode < 200 || statusCode >= 300) {
            Core.notification('error', context.message.application_properties.statusDescription);
            //QDR.log.debug(context.message.application_properties.statusDescription)
            return;
          }
          $scope.logResults = response.filter( function (entry) {
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
          $scope.$apply();
        })
      }
    }
    $scope.isValid = function (mode) {
      return mode.isValid()
    }

    $scope.expandAll = function () {
      $("#entityTree").dynatree("getRoot").visit(function(node){
                node.expand(true);
            });
    }
    $scope.contractAll = function () {
      $("#entityTree").dynatree("getRoot").visit(function(node){
                node.expand(false);
            });
    }

    if (!QDRService.connected) {
      // we are not connected. we probably got here from a bookmark or manual page reload
      QDRService.redirectWhenConnected("list");
      return;
    }
    // we are currently connected. setup a handler to get notified if we are ever disconnected
    QDRService.addDisconnectAction( function () {
      QDRService.redirectWhenConnected("list")
      $scope.$apply();
    })

    $scope.nodes = []
    var excludedEntities = ["management", "org.amqp.management", "operationalEntity", "entity", "configurationEntity", "dummy", "console"];
    var aggregateEntities = ["router.address"];
    var classOverrides = {
      "connection": function (row, nodeId) {
        var isConsole = QDRService.isAConsole (row.properties.value, row.identity.value, row.role.value, nodeId)
        return isConsole ? "console" : row.role.value === "inter-router" ? "inter-router" : "external";
      },
      "router.link": function (row, nodeId) {
        var link = {nodeId: nodeId, connectionId: row.connectionId.value}
        var isConsole = QDRService.isConsoleLink(link)
        return isConsole ? "console" : row.linkType.value;
      },
      "router.address": function (row) {
        var identity = QDRService.identity_clean(row.identity.value)
        var address = QDRService.addr_text(identity)
        var cls = QDRService.addr_class(identity)
        if (address === "$management")
          cls = "internal " + cls
        return cls
      }
    }

    var lookupOperations = function () {
      var ops = QDRService.schema.entityTypes[$scope.selectedEntity].operations.filter( function (op) { return op !== 'READ'});
      $scope.operation = ops.length ? ops[0] : "";
      return ops;
    }

    var entityTreeChildren = [];
    var expandedList = angular.fromJson(localStorage[ListExpandedKey]) || [];
    var onTreeNodeExpanded = function (expanded, node) {
      // save the list of entities that are expanded
      var tree = $("#entityTree").dynatree("getTree");
      var list = [];
      tree.visit( function (tnode) {
        if (tnode.isExpanded()) {
          list.push(tnode.data.key)
        }
      })
      localStorage[ListExpandedKey] = JSON.stringify(list)

      if (expanded)
        onTreeSelected(node);
    }
    // a tree node was selected
    var onTreeSelected = function (selectedNode) {
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
          $scope.selectedEntity = selectedNode.data.key;
          $scope.operations = lookupOperations()
        } else if (selectedNode.data.typeName === 'attribute') {
          $scope.selectedEntity = selectedNode.parent.data.key;
          $scope.operations = lookupOperations()
          $scope.selectedRecordName = selectedNode.data.key;
          updateDetails(selectedNode.data.details);   // update the table on the right
          $("#entityTree").dynatree("getRoot").visit(function(node){
             node.select(false);
          });
          selectedNode.select();
        }
      })
    }

    // fill in an empty results recoord based on the entities schema
    var fromSchema = function (entityName) {
      var row = {}
      var schemaEntity = QDRService.schema.entityTypes[entityName]
      for (attr in schemaEntity.attributes) {
        var entity = schemaEntity.attributes[attr]
        var value = ""
        if (angular.isDefined(entity['default']))
          value = entity['default']
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
      var schemaEntity = QDRService.schema.entityTypes[$scope.selectedEntity]
      return (schemaEntity.operations.indexOf("CREATE") > -1)
    }

    var stopUpdating = function () {
      if (angular.isDefined(updateIntervalHandle)) {
        clearInterval(updateIntervalHandle);
      }
      updateIntervalHandle = undefined;
    }

    // the data for the selected entity is available, populate the tree
    var updateEntityChildren = function (entity, tableRows, expand) {
      var tree = $("#entityTree").dynatree("getTree");
      if (!tree.getNodeByKey) {
        return stopUpdating()
      }
      var node = tree.getNodeByKey(entity)
      var updatedDetails = false;
      var scrollTreeDiv = $('.qdr-attributes.pane.left .pane-viewport')
      var scrollTop = scrollTreeDiv.scrollTop();
      node.removeChildren();
      if (tableRows.length == 0) {
          node.addChild({
          addClass:   "no-data",
              typeName:   "none",
              title:      "no data",
          key:        node.data.key + ".1"
          })
          if (expand) {
              updateDetails(fromSchema(entity));
                 $scope.selectedRecordName = entity;
        }
      } else {
        tableRows.forEach( function (row) {
          var addClass = entity;
          if (classOverrides[entity]) {
            addClass += " " + classOverrides[entity](row, $scope.currentNode.id);
          }
          var child = {
                        typeName:   "attribute",
                        addClass:   addClass,
                        tooltip:    addClass,
                        key:        row.name.value,
                        title:      row.name.value,
                        details:    row
                    }
          if (row.name.value === $scope.selectedRecordName) {
            if (expand)
              updateDetails(row); // update the table on the right
            child.select = true;
            updatedDetails = true;
          }
          node.addChild(child)
        })
      }
      // if the selectedRecordName was not found, select the 1st one
      if (expand && !updatedDetails && tableRows.length > 0) {
        var row = tableRows[0];
        $scope.selectedRecordName = row.name.value;
        var node = tree.getNodeByKey($scope.selectedRecordName);
        node.select(true);
        updateDetails(row)  // update the table on the right
      }
      scrollTreeDiv.scrollTop(scrollTop)
    }

    var schemaProps = function (entityName, key, currentNode) {
         var typeMap = {integer: 'number', string: 'text', path: 'text', boolean: 'boolean', map: 'textarea'};

      var entity = QDRService.schema.entityTypes[entityName]
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
        QDRService.fetchEntity(currentNode.id, '.connector', ['name'], function (nodeName, dotentity, response) {
          $scope.detailFields.some( function (field) {
            if (field.name === 'connector') {
              field.rawtype = response.results.map (function (result) {return result[0]})
              return true;
            }
          })
        });
      }
      return {    name:       key,
            humanName:  QDRService.humanify(key),
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
          attributeName:  QDRService.humanify(attr),
          attributeValue: attr === 'port' ? row[attr].value : QDRService.pretty(row[attr].value),
          name:           attr,
          changed:        changed.length,
          rawValue:       row[attr].value,
          graph:          row[attr].graph,
          title:          row[attr].title,
          aggregateValue: QDRService.pretty(row[attr].aggregate),
          aggregateTip:   row[attr].aggregateTip,

          input:          schemaEntity.input,
          type:           schemaEntity.type,
          required:       schemaEntity.required,
          selected:       schemaEntity.selected,
          rawtype:        schemaEntity.rawtype,
          disabled:       schemaEntity.disabled,
          'default':      schemaEntity['default']
        })
        $scope.detailsObject[attr] = row[attr].value;
      })
      setTimeout(applyDetails, 1, details)
    }

    var applyDetails = function (details) {
      $scope.detailFields = details;
      aggregateColumn();
      $scope.$apply();
      // ng-grid bug? the entire table doesn't always draw unless a reflow is triggered;
      $(window).trigger('resize');
    }

    var restartUpdate = function () {
      stopUpdating();
      updateTableData($scope.selectedEntity, true);
      updateIntervalHandle = setInterval(updateExpandedEntities, updateInterval);
    }
    var updateExpandedEntities = function () {
      var tree = $("#entityTree").dynatree("getTree");
      if (tree.visit) {
        tree.visit( function (node) {
          if (node.isExpanded()) {
            updateTableData(node.data.key, node.data.key === $scope.selectedEntity)
          }
        })
      } else {
        stopUpdating();
      }
    }

    $scope.selectNode = function(node) {
      $scope.selectedNode = node.name;
      $scope.selectedNodeId = node.id;
      setCurrentNode();
      restartUpdate();
    };
    $scope.$watch('selectedEntity', function(newValue, oldValue) {
      if (newValue !== oldValue) {
        localStorage['QDRSelectedEntity'] = $scope.selectedEntity;
        restartUpdate();
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
    var updateTableData = function (entity, expand) {
      if (!QDRService.connected) {
        // we are no longer connected. bail back to the connect page
        $location.path("/" + QDR.pluginName + "/connect")
        $location.search('org', "list");
        return;
      }
      // don't update the data when on the operations tab
      if ($scope.currentMode.id === 'operations') {
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
          var ent = QDRService.schema.entityTypes[entity];
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
              QDR.log.error("response attributeNames did not contain a name field");
              console.dump(response.attributeNames);
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
        }

        tableRows.sort( function (a, b) { return a.name.value.localeCompare(b.name.value) })
        setTimeout(selectRow, 0, {entity: dotentity, rows: tableRows, expand: expand});
      }
      // if this entity should show an aggregate column, send the request to get the info for this entity from all the nedes
      if (aggregateEntities.indexOf(entity) > -1) {
        var nodeInfo = QDRService.topology.nodeInfo();
        QDRService.getMultipleNodeInfo(Object.keys(nodeInfo), entity, [], gotNodeInfo, $scope.selectedNodeId);
      } else {
        QDRService.fetchEntity($scope.selectedNodeId, entity, [], gotNodeInfo);
      }
    };

    // tableRows are the records that were returned, this populates the left hand table on the page
    var selectRow = function (info) {
      updateEntityChildren(info.entity, info.rows, info.expand);
      fixTooltips();
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

    $scope.detailFields = [];

    $scope.addToGraph = function(rowEntity) {
      var chart = QDRChartService.registerChart(
        {nodeId: $scope.selectedNodeId,
         entity: "." + $scope.selectedEntity,
         name:   $scope.selectedRecordName,
         attr:    rowEntity.name,
         forceCreate: true});
      doDialog('tmplListChart.html', chart);
    }

    $scope.addAllToGraph = function(rowEntity) {
      var chart = QDRChartService.registerChart({
        nodeId:     $scope.selectedNodeId,
        entity:     $scope.selectedEntity,
        name:       $scope.selectedRecordName,
        attr:       rowEntity.name,
        type:       "rate",
        rateWindow: updateInterval,
        visibleDuration: 1,
        forceCreate: true,
        aggregate:   true});
      doDialog('tmplListChart.html', chart);
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
    $scope.$on("$destroy", function( event ) {
      //QDR.log.debug("scope destroyed for qdrList");
      stopUpdating();
    });

    function gotMethodResponse (nodeName, entity, response, context) {
      var statusCode = context.message.application_properties.statusCode;
      if (statusCode < 200 || statusCode >= 300) {
        Core.notification('error', context.message.application_properties.statusDescription);
        //QDR.log.debug(context.message.application_properties.statusDescription)
      } else {
        var note = entity + " " + $filter('Pascalcase')($scope.currentMode.op) + "d"
        Core.notification('success', note);
        $scope.selectMode($scope.modes[0]);
        restartUpdate();
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
      QDRService.sendMethod($scope.currentNode.id, $scope.selectedEntity, attributes, $scope.currentMode.op, undefined, gotMethodResponse)
    }
    $scope.remove = function () {
      var attributes = {type: $scope.selectedEntity, name: $scope.selectedRecordName}
      QDRService.sendMethod($scope.currentNode.id, $scope.selectedEntity, attributes, $scope.currentMode.op, undefined, gotMethodResponse)
    }

    function doDialog(tmpl, chart) {
        var d = $uibModal.open({
          backdrop: true,
          keyboard: true,
          backdropClick: true,
          templateUrl: QDR.templatePath + tmpl,
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

        d.result.then(function(result) { console.log("d.open().then"); });

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
    $scope.largeNetwork = QDRService.isLargeNetwork()
    // called after we know for sure the schema is fetched and the routers are all ready
    QDRService.addUpdatedAction("initList", function () {
      QDRService.stopUpdating();
      QDRService.delUpdatedAction("initList")

      $scope.nodes = QDRService.nodeList().sort(function (a, b) { return a.name.toLowerCase() > b.name.toLowerCase()});
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
      var sortedEntities = Object.keys(QDRService.schema.entityTypes).sort();
      sortedEntities.forEach( function (entity) {
        if (excludedEntities.indexOf(entity) == -1) {
          if (!angular.isDefined($scope.selectedEntity)) {
            $scope.selectedEntity = entity;
            $scope.operations = lookupOperations()
          }
          var e = new Folder(entity)
          e.typeName = "entity"
          e.key = entity
          e.isFolder = true
          e.expand = (expandedList.indexOf(entity) > -1)
          var placeHolder = new Folder("loading...")
          placeHolder.addClass = "loading"
          e.children = [placeHolder]
          entityTreeChildren.push(e)
        }
      })
      serviceReady = true;
      initTree();
    })
    $scope.treeReady = function () {
      treeReady = true;
      initTree();
    }

    var initTree = function () {
      if (!treeReady || !serviceReady)
        return;
      $('#entityTree').dynatree({
        onActivate: onTreeSelected,
        onExpand: onTreeNodeExpanded,
        selectMode: 1,
        autoCollapse: $scope.largeNetwork,
        activeVisible: !$scope.largeNetwork,
        debugLevel: 0,
        classNames: {
          expander: 'fa-angle',
          connector: 'dynatree-no-connector'
          },
        children: entityTreeChildren
      })
      restartUpdate()
      updateExpandedEntities();
    };
    QDRService.ensureAllEntities({entity: ".connection"}, function () {
      QDRService.setUpdateEntities([".connection"])
      QDRService.startUpdating();
    })


  }]);

    return QDR;

} (QDR || {}));

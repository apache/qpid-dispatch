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
/* global angular d3 */

import { QDRFolder, QDRLeaf, QDRCore, QDRLogger, QDRTemplatePath, QDRRedirectWhenConnected} from './qdrGlobals.js';

export class ListController {
  constructor(QDRService, QDRChartService, $scope, $log, $location, $uibModal, $filter, $timeout, uiGridConstants, $sce) {
    this.controllerName = 'QDR.ListController';

    let QDRLog = new QDRLogger($log, 'ListController');

    QDRLog.debug('QDR.ListControll started with location of ' + $location.path() + ' and connection of  ' + QDRService.management.connection.is_connected());
    let updateIntervalHandle = undefined,
      updateInterval = 5000,
      last_updated = 0,
      updateNow = false,
      ListExpandedKey = 'QDRListExpanded',
      SelectedEntityKey = 'QDRSelectedEntity',
      ActivatedKey = 'QDRActivatedKey';
    $scope.details = {};

    $scope.tmplListTree = QDRTemplatePath + 'tmplListTree.html';
    $scope.selectedEntity = localStorage[SelectedEntityKey] || 'address';
    $scope.ActivatedKey = localStorage[ActivatedKey] || null;
    if ($scope.selectedEntity == 'undefined')
      $scope.selectedEntity = undefined;
    $scope.selectedNode = localStorage['QDRSelectedNode'];
    $scope.selectedNodeId = localStorage['QDRSelectedNodeId'];
    $scope.selectedRecordName = localStorage['QDRSelectedRecordName'];
    $scope.nodes = [];
    $scope.currentNode = undefined;
    $scope.modes = [
      {
        content: '<a><i class="icon-list"></i> Attributes</a>',
        id: 'attributes',
        op: 'READ',
        title: 'View router attributes',
        isValid: function () { return true; }
      },
      {
        content: '<a><i class="icon-edit"></i> Update</a>',
        id: 'operations',
        op: 'UPDATE',
        title: 'Update this attribute',
        isValid: function () {
          return $scope.operations.indexOf(this.op) > -1;
        }
      },
      {
        content: '<a><i class="icon-plus"></i> Create</a>',
        id: 'operations',
        op: 'CREATE',
        title: 'Create a new attribute',
        isValid: function () { return $scope.operations.indexOf(this.op) > -1; }
      },
      {
        content: '<a><i class="icon-remove"></i> Delete</a>',
        id: 'delete',
        op: 'DELETE',
        title: 'Delete',
        isValid: function () { return $scope.operations.indexOf(this.op) > -1; }
      },
      {
        content: '<a><i class="icon-eye-open"></i> Fetch</a>',
        id: 'log',
        op: 'GET-LOG',
        title: 'Fetch recent log entries',
        isValid: function () { return ($scope.selectedEntity === 'log'); }
      }
    ];
    $scope.operations = [];
    $scope.currentMode = $scope.modes[0];
    $scope.isModeSelected = function (mode) {
      return mode === $scope.currentMode;
    };
    $scope.fetchingLog = false;
    $scope.selectMode = function (mode) {
      $scope.currentMode = mode;
      if (mode.id === 'log') {
        $scope.logResults = [];
        $scope.fetchingLog = true;
        let entity; // undefined since it is not supported in the GET-LOG call
        QDRService.management.connection.sendMethod($scope.currentNode.id, entity, {}, $scope.currentMode.op)
          .then( function (response) {
            let statusCode = response.context.message.application_properties.statusCode;
            if (statusCode < 200 || statusCode >= 300) {
              QDRCore.notification('error', response.context.message.statusDescription);
              QDRLog.error('Error ' + response.context.message.statusDescription);
              return;
            }
            $timeout( function () {
              $scope.fetchingLog = false;
              $scope.logResults = response.response.filter( function (entry) {
                return entry[0] === $scope.detailsObject.module;
              }).sort( function (a, b) {
                return b[5] - a[5];
              }).map( function (entry) {
                return {
                  type: entry[1],
                  message: entry[2],
                  source: entry[3],
                  line: entry[4],
                  time: Date(entry[5]).toString()
                };
              });
            });
          });
      }
    };
    $scope.isValid = function (mode) {
      return mode.isValid();
    };

    $scope.expandAll = function () {
      $('#entityTree').fancytree('getTree').visit(function(node){
        node.setExpanded(true);
      });
    };
    $scope.contractAll = function () {
      $('#entityTree').fancytree('getTree').visit(function(node){
        node.setExpanded(false);
      });
    };

    if (!QDRService.management.connection.is_connected()) {
    // we are not connected. we probably got here from a bookmark or manual page reload
      QDRRedirectWhenConnected($location, 'list');
      return;
    }

    let excludedEntities = ['management', 'org.amqp.management', 'operationalEntity', 'entity', 'configurationEntity', 'dummy', 'console'];
    let aggregateEntities = ['router.address'];

    let classOverrides = {
      'connection': function (row, nodeId) {
        let isConsole = QDRService.utilities.isAConsole (row.properties.value, row.identity.value, row.role.value, nodeId);
        return isConsole ? 'console' : row.role.value === 'inter-router' ? 'inter-router' : 'external';
      },
      'router.link': function (row, nodeId) {
        let link = {nodeId: nodeId, connectionId: row.connectionId.value};

        let isConsole = QDRService.utilities.isConsole(QDRService.management.topology.getConnForLink(link));
        return isConsole ? 'console' : row.linkType.value;
      },
      'router.address': function (row) {
        let identity = QDRService.utilities.identity_clean(row.identity.value);
        let address = QDRService.utilities.addr_text(identity);
        let cls = QDRService.utilities.addr_class(identity);
        if (address === '$management')
          cls = 'internal ' + cls;
        return cls;
      }
    };

    var lookupOperations = function () {
      let ops = QDRService.management.schema().entityTypes[$scope.selectedEntity].operations.filter( function (op) { return op !== 'READ';});
      $scope.operation = ops.length ? ops[0] : '';
      return ops;
    };
    let entityTreeChildren = [];
    let expandedList = angular.fromJson(localStorage[ListExpandedKey]) || [];
    let saveExpanded = function () {
    // save the list of entities that are expanded
      let tree = $('#entityTree').fancytree('getTree');
      let list = [];
      tree.visit( function (tnode) {
        if (tnode.isExpanded()) {
          list.push(tnode.key);
        }
      });
      console.log('saving expanded list');
      console.log(list);
      localStorage[ListExpandedKey] = JSON.stringify(list);
    };

    var onTreeNodeBeforeActivate = function (event, data) {
    // if node is toplevel entity
      if (data.node.data.typeName === 'entity') {
        return false;
        /*
        // if the current active node is not this one and not one of its children
        let active = data.tree.getActiveNode();
        if (active && !data.node.isActive() && data.node.isExpanded()) {  // there is an active node and it's not this one
          let any = false;
          let children = data.node.getChildren();
          if (children) {
            any = children.some( function (child) {
              return child.key === active.key;
            });
          }
          if (!any) // none of the clicked on node's children was active
            return false;  // don't activate, just collapse this top level node
        }
        */
      }
      return true;
    };
    var onTreeNodeExpanded = function () {
      saveExpanded();
      updateExpandedEntities();
    };
    var onTreeNodeCollapsed = function () {
      saveExpanded();
    };
    // a tree node was selected
    var onTreeNodeActivated = function (event, data) {
      $scope.ActivatedKey = data.node.key;
      let selectedNode = data.node;
      $scope.selectedTreeNode = data.node;
      if ($scope.currentMode.id === 'operations')
        $scope.currentMode = $scope.modes[0];
      else if ($scope.currentMode.id === 'log')
        $scope.selectMode($scope.currentMode);
      else if ($scope.currentMode.id === 'delete') {
        // clicked on a tree node while on the delete screen -> switch to attribute screen
        $scope.currentMode = $scope.modes[0];
      }
      if (selectedNode.data.typeName === 'entity') {
        $scope.selectedEntity = selectedNode.key;
        $scope.operations = lookupOperations();
        updateNow = true;
      } else if (selectedNode.data.typeName === 'attribute') {
        if (!selectedNode.parent)
          return;
        let sameEntity = $scope.selectedEntity === selectedNode.parent.key;
        $scope.selectedEntity = selectedNode.parent.key;
        $scope.operations = lookupOperations();
        $scope.selectedRecordName = selectedNode.key;
        updateDetails(selectedNode.data.details);   // update the table on the right
        if (!sameEntity) {
          updateNow = true;
        }
      } else if (selectedNode.data.typeName === 'none') {
        $scope.selectedEntity = selectedNode.parent.key;
        $scope.selectedRecordName = $scope.selectedEntity;
        updateDetails(fromSchema($scope.selectedEntity));
      }
    };
    var getExpanded = function (tree) {
      let list = [];
      tree.visit( function (tnode) {
        if (tnode.isExpanded()) {
          list.push(tnode);
        }
      });
      return list;
    };
    // fill in an empty results recoord based on the entities schema
    var fromSchema = function (entityName) {
      let row = {};
      let schemaEntity = QDRService.management.schema().entityTypes[entityName];
      for (let attr in schemaEntity.attributes) {
        let entity = schemaEntity.attributes[attr];
        let value = '';
        if (angular.isDefined(entity['default'])) {
          if (entity['type'] === 'integer')
            value = parseInt(entity['default']); // some default values that are marked as integer are passed as string
          else
            value = entity['default'];
        }
        row[attr] = {
          value: value,
          type: entity.type,
          graph: false,
          title: entity.description,
          aggregate: false,
          aggregateTip: '',
          'default': entity['default']
        };
      }
      return row;
    };
    $scope.hasCreate = function () {
      let schemaEntity = QDRService.management.schema().entityTypes[$scope.selectedEntity];
      return (schemaEntity.operations.indexOf('CREATE') > -1);
    };

    var getActiveChild = function (node) {
      let active = node.children.filter(function (child) {
        return child.isActive();
      });
      if (active.length > 0)
        return active[0].key;
      return null;
    };
    // the data for the selected entity is available, populate the tree on the left
    var updateTreeChildren = function (entity, tableRows, expand) {
      let tree = $('#entityTree').fancytree('getTree'), node, newNode;
      if (tree && tree.getNodeByKey) {
        node = tree.getNodeByKey(entity);
      }
      if (!tree || !node) {
        return;
      }
      let wasActive = node.isActive();
      let wasExpanded = node.isExpanded();
      let activeChildKey = getActiveChild(node);
      node.removeChildren();
      if (tableRows.length == 0) {
        newNode = {
          extraClasses:   'no-data',
          typeName:   'none',
          title:      'no data',
          key:        node.key + '.1'
        };
        node.addNode(newNode);
        if (expand) {
          updateDetails(fromSchema(entity));
          $scope.selectedRecordName = entity;
        }
      } else {
        let children = tableRows.map( function (row) {
          let addClass = entity;
          if (classOverrides[entity]) {
            addClass += ' ' + classOverrides[entity](row, $scope.currentNode.id);
          }
          let child = {
            typeName:   'attribute',
            extraClasses:   addClass,
            tooltip:    addClass,
            key:        row.name.value,
            title:      row.name.value,
            details:    row
          };
          return child;
        });
        node.addNode(children);
      }
      // top level node was expanded
      if (wasExpanded)
        node.setExpanded(true, {noAnimation: true, noEvents: true, noFocus: true});
      // if the parent node was active, but none of the children were active, active the 1st child
      if (wasActive) {
        if (!activeChildKey) {
          activeChildKey = node.children[0].key;
        }
      }
      if (!tree.getActiveNode())
        activeChildKey = $scope.ActivatedKey;
      // re-active the previously active child node
      if (activeChildKey) {
        newNode = tree.getNodeByKey(activeChildKey);
        // the node may not be there after the update
        if (newNode)
          newNode.setActive(true, {noFocus: true}); // fires the onTreeNodeActivated event for this node
      }
      //resizer();
    };


    var resizer = function () {
    // this forces the tree and the grid to be the size of the browser window.
    // the effect is that the tree and the grid will have vertical scroll bars if needed.
    // the alternative is to let the tree and grid determine the size of the page and have
    // the scroll bar on the window
      // don't allow HTML in the tree titles
      $('.fancytree-title').each( function () {
        let unsafe = $(this).html();
        $(this).html(unsafe.replace(/</g, '&lt;').replace(/>/g, '&gt;'));
      });
      let h = $scope.detailFields.length * 30 + 46;
      $('.ui-grid-viewport').height(h);
      $scope.details.excessRows = $scope.detailFields.length;
      $scope.gridApi.grid.handleWindowResize();
      $scope.gridApi.core.refresh();
      
    };
    $(window).resize(resizer);

    var schemaProps = function (entityName, key, currentNode) {
      let typeMap = {integer: 'number', string: 'text', path: 'text', boolean: 'boolean', map: 'textarea'};

      let entity = QDRService.management.schema().entityTypes[entityName];
      let value = entity.attributes[key];
      // skip identity and depricated fields
      if (!value)
        return {input: 'input', type: 'disabled', required: false, selected: '', rawtype: 'string', disabled: true, 'default': ''};
      let description = value.description || '';
      let val = value['default'];
      let disabled = (key == 'identity' || description.startsWith('Deprecated'));
      // special cases
      if (entityName == 'log' && key == 'module') {
        return {input: 'input', type: 'disabled', required: false, selected: '', rawtype: 'string', disabled: true, 'default': ''};
      }
      if (entityName === 'linkRoutePattern' && key === 'connector') {
      // turn input into a select. the values will be populated later
        value.type = [];
        // find all the connector names and populate the select
        QDRService.management.topology.fetchEntity(currentNode.id, 'connector', ['name'], function (nodeName, dotentity, response) {
          $scope.detailFields.some( function (field) {
            if (field.name === 'connector') {
              field.rawtype = response.results.map (function (result) {return result[0];});
              return true;
            }
          });
        });
      }
      return {
        name:       key,
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
    };
    $scope.getAttributeValue = function (attribute) {
      let value = attribute.attributeValue;
      if ($scope.currentMode.op === 'CREATE' && attribute.name === 'identity')
        value = '<assigned by system>';
      return value;
    };

    // update the table on the right
    var updateDetails = function (row) {
      let details = [];
      $scope.detailsObject = {};
      let attrs = Object.keys(row).sort();
      attrs.forEach( function (attr) {
        let changed = $scope.detailFields.filter(function (old) {
          return (old.name === attr) ? old.graph && old.rawValue != row[attr].value : false;
        });
        let schemaEntity = schemaProps($scope.selectedEntity, attr, $scope.currentNode);
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
        });
        $scope.detailsObject[attr] = row[attr].value;
      });
      $scope.detailFields = details;
      aggregateColumn();
      resizer();
    };

    // called from html ng-style="getTableHeight()"
    $scope.getTableHeight = function () {
      return {
        height: (Math.max($scope.detailFields.length, 15) * 30 + 46) + 'px'
      };
    };
    var updateExpandedEntities = function () {
      let tree = $('#entityTree').fancytree('getTree');
      if (tree) {
        let q = d3.queue(10);
        let expanded = getExpanded(tree);
        expanded.forEach( function (node) {
          q.defer(q_updateTableData, node.key, node.key === $scope.selectedEntity);
        });

        q.await(function (error) {
          if (error)
            QDRLog.error(error.message);

          if (!tree.getActiveNode()) {
            if ($scope.ActivatedKey) {
              let node = tree.getNodeByKey($scope.ActivatedKey);
              if (node) {
                node.setActive(true, {noEvents: true});
              }
            }
            if (!tree.getActiveNode()) {
              let first = tree.getFirstChild();
              if (first) {
                let child = first.getFirstChild();
                if (child)
                  first = child;
              }
              first.setActive(true);
            }
          }

          d3.selectAll('.ui-effects-placeholder').style('height', '0px');
          resizer();

          last_updated = Date.now();
        });
      }
    };

    // The selection dropdown (list of routers) was changed.
    $scope.selectNode = function(node) {
      $scope.selectedNode = node.name;
      $scope.selectedNodeId = node.id;
      $timeout( function () {
        setCurrentNode();
        updateNow = true;
      });
    };

    $scope.$watch('ActivatedKey', function(newValue, oldValue) {
      if (newValue !== oldValue) {
        localStorage[ActivatedKey] = $scope.ActivatedKey;
      }
    });
    $scope.$watch('selectedEntity', function(newValue, oldValue) {
      if (newValue !== oldValue) {
        localStorage['QDRSelectedEntity'] = $scope.selectedEntity;
        $scope.operations = lookupOperations();
      }
    });
    $scope.$watch('selectedNode', function(newValue, oldValue) {
      if (newValue !== oldValue) {
        localStorage['QDRSelectedNode'] = $scope.selectedNode;
        localStorage['QDRSelectedNodeId'] = $scope.selectedNodeId;
      }
    });
    $scope.$watch('selectedRecordName', function(newValue, oldValue) {
      if (newValue != oldValue) {
        localStorage['QDRSelectedRecordName'] = $scope.selectedRecordName;
      }
    });

    /* Called periodically to refresh the data on the page */
    var q_updateTableData = function (entity, expand, callback) {
    // don't update the data when on the operations tabs
      if ($scope.currentMode.id !== 'attributes') {
        callback(null);
        return;
      }
      var gotNodeInfo = function (nodeName, dotentity, response) {
        let tableRows = [];
        let records = response.results;
        let aggregates = response.aggregates;
        let attributeNames = response.attributeNames;
        // If !attributeNmes then  there was an error getting the records for this entity
        if (attributeNames) {
          let nameIndex = attributeNames.indexOf('name');
          let identityIndex = attributeNames.indexOf('identity');
          let ent = QDRService.management.schema().entityTypes[entity];
          for (let i=0; i<records.length; ++i) {
            let record = records[i];
            let aggregate = aggregates ? aggregates[i] : undefined;
            let row = {};
            let rowName;
            if (nameIndex > -1) {
              rowName = record[nameIndex];
              if (!rowName && identityIndex > -1) {
                rowName = record[nameIndex] = (dotentity + '/' + record[identityIndex]);
              }
            }
            if (!rowName) {
              let msg = 'response attributeNames did not contain a name field';
              QDRLog.error(msg);
              callback(Error(msg));
              return;
            }
            for (let j=0; j<attributeNames.length; ++j) {
              let col = attributeNames[j];
              row[col] = {value: record[j], type: undefined, graph: false, title: '', aggregate: '', aggregateTip: ''};
              if (ent) {
                let att = ent.attributes[col];
                if (att) {
                  row[col].type = att.type;
                  row[col].graph = att.graph;
                  row[col].title = att.description;

                  if (aggregate) {
                    if (att.graph) {
                      row[col].aggregate = att.graph ? aggregate[j].sum : '';
                      let tip = [];
                      aggregate[j].detail.forEach( function (line) {
                        tip.push(line);
                      });
                      row[col].aggregateTip = angular.toJson(tip);
                    }
                  }
                }
              }
            }
            tableRows.push(row);
          }
          tableRows.sort( function (a, b) { return a.name.value.localeCompare(b.name.value); });
          selectRow({entity: dotentity, rows: tableRows, expand: expand});
        }
        callback(null);  // let queue handler know we are done
      };
      // if this entity should show an aggregate column, send the request to get the info for this entity from all the nedes
      if (aggregateEntities.indexOf(entity) > -1) {
        let nodeIdList = QDRService.management.topology.nodeIdList();
        QDRService.management.topology.getMultipleNodeInfo(nodeIdList, entity, [], gotNodeInfo, $scope.selectedNodeId);
      } else {
        QDRService.management.topology.fetchEntity($scope.selectedNodeId, entity, [], gotNodeInfo);
      }
    };

    // tableRows are the records that were returned, this populates the left hand tree on the page
    var selectRow = function (info) {
      updateTreeChildren(info.entity, info.rows, info.expand);
    };
    $scope.detailFields = [];

    $scope.addToGraph = function(rowEntity) {
      let chart = QDRChartService.registerChart(
        {nodeId: $scope.selectedNodeId,
          entity: $scope.selectedEntity,
          name:   $scope.selectedRecordName,
          attr:    rowEntity.name,
          forceCreate: true});

      doDialog('tmplChartConfig.html', chart);
    };

    $scope.addAllToGraph = function(rowEntity) {
      let chart = QDRChartService.registerChart({
        nodeId:     $scope.selectedNodeId,
        entity:     $scope.selectedEntity,
        name:       $scope.selectedRecordName,
        attr:       rowEntity.name,
        type:       'rate',
        rateWindow: updateInterval,
        visibleDuration: 0.25,
        forceCreate: true,
        aggregate:   true});
      doDialog('tmplChartConfig.html', chart);
    };

    // The ui-popover dynamic html
    $scope.aggregateTip = '';
    // disable popover tips for non-integer cells
    $scope.aggregateTipEnabled = function (row) {
      let tip = row.entity.aggregateTip;
      return (tip && tip.length) ? 'true' : 'false';
    };
    // convert the aggregate data into a table for the popover tip
    $scope.genAggregateTip = function (row) {
      let tip = row.entity.aggregateTip;
      if (tip && tip.length) {
        let data = angular.fromJson(tip);
        let table = '<table class=\'tiptable\'><tbody>';
        data.forEach (function (row) {
          table += '<tr>';
          table += '<td>' + row.node + '</td><td align=\'right\'>' + QDRService.utilities.pretty(row.val) + '</td>';
          table += '</tr>';
        });
        table += '</tbody></table>';
        $scope.aggregateTip = $sce.trustAsHtml(table);
      }
    };
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
        ];
        if (aggregateEntities.indexOf($scope.selectedEntity) > -1) {
          $scope.detailCols.push(
            {
              width: '10%',
              field: 'aggregateValue',
              displayName: 'Aggregate',
              cellTemplate: '<div popover-enable="{{grid.appScope.aggregateTipEnabled(row)}}" uib-popover-html="grid.appScope.aggregateTip" popover-append-to-body="true" ng-mouseover="grid.appScope.genAggregateTip(row)" popover-trigger="\'mouseenter\'" class="listAggrValue ui-grid-cell-contents" ng-class="{\'changed\': row.entity.changed == 1}">{{COL_FIELD CUSTOM_FILTERS}} <button title="Click to view/add a graph" ng-if="row.entity.graph" ng-click="grid.appScope.addAllToGraph(row.entity)" ng-class="{\'btn-success\': row.entity.aggchartExists}" class="btn"><i ng-class="{\'icon-bar-chart\': row.entity.graph == true }"></i></button></div>',
              cellClass: 'aggregate'
            }
          );
        }
      }
      if ($scope.selectedRecordName === '')
        $scope.detailCols = [];

      $scope.details.columnDefs = $scope.detailCols;
      if ($scope.gridApi)
        $scope.gridApi.core.notifyDataChange( uiGridConstants.dataChange.COLUMN );
    };

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
      enableHorizontalScrollbar: 0,
      enableVerticalScrollbar: 0,
      multiSelect: false,
      jqueryUIDraggable: true,
      excessRows: 20,
      onRegisterApi: function(gridApi) {
        $scope.gridApi = gridApi;
      }
    };

    $scope.$on('$destroy', function() {
      clearTimeout(updateIntervalHandle);
      $(window).off('resize', resizer);
    });

    function gotMethodResponse (entity, context) {
      let statusCode = context.message.application_properties.statusCode, note;
      if (statusCode < 200 || statusCode >= 300) {
        note = 'Failed to ' + $filter('Pascalcase')($scope.currentMode.op) + ' ' + entity + ': ' + context.message.application_properties.statusDescription;
        QDRCore.notification('error', note);
        QDRLog.error('Error ' + note);
      } else {
        note = entity + ' ' + $filter('Pascalcase')($scope.currentMode.op) + 'd';
        QDRCore.notification('success', note);
        QDRLog.debug('Success ' + note);
        $scope.selectMode($scope.modes[0]);
      }
    }
    $scope.ok = function () {
      let attributes = {};
      $scope.detailFields.forEach( function (field) {
        let value = field.rawValue;
        if (field.input === 'input') {
          if (field.type === 'text' || field.type === 'disabled')
            value = field.attributeValue;
        } else if (field.input === 'select') {
          value = field.selected;
        } else if (field.input === 'boolean') {
          value = field.rawValue;
        }
        if (value === '')
          value = undefined;

        if ((value && value != field['default']) || field.required || (field.name === 'role')) {
          if (field.name !== 'identity')
            attributes[field.name] = value;
        }
      });
      QDRService.management.connection.sendMethod($scope.currentNode.id, $scope.selectedEntity, attributes, $scope.currentMode.op)
        .then(function (response) {gotMethodResponse($scope.selectedEntity, response.context);});
    };
    $scope.remove = function () {
      let identity = $scope.selectedTreeNode.data.details.identity.value;
      let attributes = {type: $scope.selectedEntity, identity: identity};
      QDRService.management.connection.sendMethod($scope.currentNode.id, $scope.selectedEntity, attributes, $scope.currentMode.op)
        .then(function (response) {gotMethodResponse($scope.selectedEntity, response.context);});
    };

    function doDialog(template, chart) {

      $uibModal.open({
        backdrop: true,
        keyboard: true,
        backdropClick: true,
        templateUrl: QDRTemplatePath + template,
        controller: 'QDR.ChartDialogController',
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
            return true;
          }
        }
      }).result.then(function() {
        QDRChartService.unRegisterChart(chart);
      });
    }
    var setCurrentNode = function () {
      let currentNode;
      $scope.nodes.some( function (node, i) {
        if (node.name === $scope.selectedNode) {
          currentNode = $scope.nodes[i];
          return true;
        }
      });
      if ($scope.currentNode !== currentNode)
        $scope.currentNode = currentNode;
    };

    let treeReady = false;
    let serviceReady = false;
    $scope.largeNetwork = QDRService.management.topology.isLargeNetwork();
    if ($scope.largeNetwork)
      aggregateEntities = [];

    // called after we know for sure the schema is fetched and the routers are all ready
    QDRService.management.topology.addUpdatedAction('initList', function () {
      QDRService.management.topology.stopUpdating();
      QDRService.management.topology.delUpdatedAction('initList');

      $scope.nodes = QDRService.management.topology.nodeList().sort(function (a, b) { return a.name.toLowerCase() > b.name.toLowerCase();});
      // unable to get node list? Bail.
      if ($scope.nodes.length == 0) {
        $location.path('/connect');
        $location.search('org', 'list');
      }
      if (!angular.isDefined($scope.selectedNode)) {
      //QDRLog.debug("selectedNode was " + $scope.selectedNode);
        if ($scope.nodes.length > 0) {
          $scope.selectedNode = $scope.nodes[0].name;
          $scope.selectedNodeId = $scope.nodes[0].id;
        //QDRLog.debug("forcing selectedNode to " + $scope.selectedNode);
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
      let sortedEntities = Object.keys(QDRService.management.schema().entityTypes).sort();
      sortedEntities.forEach( function (entity) {
        if (excludedEntities.indexOf(entity) == -1) {
          if (!angular.isDefined($scope.selectedEntity))
            $scope.selectedEntity = entity;
          $scope.operations = lookupOperations();
          let e = new QDRFolder(entity);
          e.typeName = 'entity';
          e.key = entity;
          e.expanded = (expandedList.indexOf(entity) > -1);
          let placeHolder = new QDRLeaf('loading...');
          placeHolder.addClass = 'loading';
          e.children = [placeHolder];
          entityTreeChildren.push(e);
        }
      });
      serviceReady = true;
      initTree();
    });
    // called by ng-init="treeReady()" in tmplListTree.html
    $scope.treeReady = function () {
      treeReady = true;
      initTree();
    };

    // this gets called once tree is initialized
    var onTreeInitialized = function () {
      updateExpandedEntities();
    };

    var initTree = function () {
      if (!treeReady || !serviceReady)
        return;
      $('#entityTree').fancytree({
        activate: onTreeNodeActivated,
        expand: onTreeNodeExpanded,
        collapse: onTreeNodeCollapsed,
        beforeActivate: onTreeNodeBeforeActivate,
        beforeSelect: function(event, data){
          // A node is about to be selected: prevent this for folders:
          if( data.node.isFolder() ){
            return false;
          }
        },
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
      });
    };
    QDRService.management.topology.ensureAllEntities({entity: 'connection'}, function () {
      QDRService.management.topology.setUpdateEntities(['connection']);
      // keep the list of routers up to date
      QDRService.management.topology.startUpdating(true);
    });

    updateIntervalHandle = setInterval(function () {
      if (!treeReady || !serviceReady)
        return;

      let now = Date.now();
      if (((now - last_updated) >= updateInterval) || updateNow) {
        updateNow = false;
        $timeout( function () {
          updateExpandedEntities();
          resizer();
        });
      }
    }, 100);

  }
}
ListController.$inject = ['QDRService', 'QDRChartService', '$scope', '$log', '$location', '$uibModal', '$filter', '$timeout', 'uiGridConstants', '$sce'];

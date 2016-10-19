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
  'use strict';

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
  angular
    .module('horizon.dashboard.dispatch.overv')
    .controller('horizon.dashboard.dispatch.overv.OverviewController', OverviewController);

  OverviewController.$inject = [
    '$scope',
    'horizon.dashboard.dispatch.comService',
    'horizon.dashboard.dispatch.chartService',
    '$location',
    '$timeout',
    'horizon.dashboard.dispatch.overv.basePath',
    'uiGridConstants',
  ];

  var FILTERKEY = "QDROverviewFilters"
  function OverviewController(
    $scope,
    QDRService,
    QDRChartService,
    $location,
    $timeout,
    basePath,
    uiGridConstants) {

    var ctrl = this;

    QDR.log.debug("QDR.OverviewController started");

    QDRService.addConnectAction( function () {
      Overview(
        $scope,
        QDRService,
        QDRChartService,
        $location,
        $timeout,
        basePath,
        uiGridConstants);
    })
    QDRService.loadConnectOptions(QDRService.connect);

    $scope.filter = angular.fromJson(localStorage[FILTERKEY]) || {endpointsOnly: true, hideConsoles: true};
    var showConsoleLinksTitle = function () {
      return ($scope.filter.hideConsoles ? "Show" : "Hide") + " Console Links"
    }
    var showHideConsoleMenuItem = {
      title: showConsoleLinksTitle(),
      action: function($event) {
        $scope.filter.hideConsoles = !$scope.filter.hideConsoles
        // assumes this is always the 1st custom menu item added
        this.context.col.colDef.menuItems[0].title = showConsoleLinksTitle()
        $timeout($scope.allLinkInfo)
      },
    }
    var endpointLinksTitle = function () {
      return "Show" + ($scope.filter.endpointsOnly ? " all link types" : " endpoints only")
    }

    $scope.allRouterFields = [];
    $scope.allRouters = {
      saveKey: 'allRouters',
      data: 'allRouterFields',
      enableHorizontalScrollbar: uiGridConstants.scrollbars.NEVER,
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
      enableRowSelection: true,
      enableRowHeaderSelection: false,
      multiSelect: false,
      enableColumnResize: true,
      enableColumnReordering: true,
      onRegisterApi: function(gridApi){
        gridApi.selection.on.rowSelectionChanged($scope,function(row){
          $scope.setActivated('router' , row.entity.nodeId, 'nodeId')
        });
      }
    };
    $scope.routerFields = []
    $scope.routerGrid = {
      saveKey: 'routerGrid',
      data: 'routerFields',
      enableHorizontalScrollbar: uiGridConstants.scrollbars.NEVER,
      columnDefs: [
        {
          field: 'attribute',
          displayName: 'Attribute',
          saveKey: 'routerGrid',
        },
        {
          field: 'value',
           displayName: 'Value',
        }
      ],
      enableColumnResize: true,
      multiSelect: false
    }
    $scope.addressesData = []
    $scope.addressesGrid = {
      saveKey: 'addressesGrid',
      data: 'addressesData',
      enableHorizontalScrollbar: uiGridConstants.scrollbars.NEVER,
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
          width: 80,
          cellClass: 'grid-align-value'
        },
        {
          field: 'inproc',
          width: 80,
          displayName: 'in-proc'
        },
        {
          field: 'local',
          displayName: 'local',
          width: 80,
          cellClass: 'grid-align-value'
        },
        {
          field: 'remote',
          displayName: 'remote',
          width: 80,
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

      enableRowSelection: true,
      enableRowHeaderSelection: false,
      multiSelect: false,
      enableColumnResize: true,
      enableColumnReordering: true,
      onRegisterApi: function(gridApi){
        gridApi.selection.on.rowSelectionChanged($scope,function(row){
          $scope.setActivated('address', row.entity.uid, 'uid')
        });
      }
    };
    // get info for a all links
    $scope.linkFields = []
    $scope.linksGrid = {
      saveKey: 'linksGrid',
      data: 'linkFields',
      enableFiltering: true,
      enableHorizontalScrollbar: uiGridConstants.scrollbars.NEVER,
      columnDefs: [
        {
          saveKey: 'linksGrid',
          field: 'link',
          displayName: 'Link',
          filter: {placeholder: 'filter link name...'},
          menuItems: [showHideConsoleMenuItem],
        },
        {
          field: 'linkType',
          displayName: 'Link type',
          filter: {
            term: $scope.filter.endpointsOnly ? 'endpoint' : '',
            placeholder: 'filter link type...',
          },
          menuItems: [showHideConsoleMenuItem,
          {
            title: endpointLinksTitle(),
            action: function($event) {
              $scope.filter.endpointsOnly = !$scope.filter.endpointsOnly
              // assumes this is the 2nd custom menu item added
              this.context.col.colDef.menuItems[1].title = endpointLinksTitle()
              this.context.col.filters[0].term = $scope.filter.endpointsOnly ? 'endpoint' : ''
              $timeout($scope.allLinkInfo)
            },
          }],
        },
        {
          field: 'linkDir',
          displayName: 'Link dir',
          filter: {placeholder: 'filter link dir...'},
          menuItems: [showHideConsoleMenuItem],
        },
        {
          field: 'adminStatus',
          displayName: 'Admin status',
          filter: {placeholder: 'filter admin status...'},
          menuItems: [showHideConsoleMenuItem],
        },
        {
          field: 'operStatus',
          displayName: 'Oper status',
          filter: {placeholder: 'filter oper status...'},
          menuItems: [showHideConsoleMenuItem],
        },
        {
          field: 'deliveryCount',
          displayName: 'Delivery Count',
          enableFiltering: false,
          menuItems: [showHideConsoleMenuItem],
        },
        {
          field: 'rate',
          displayName: 'Rate',
          enableFiltering: false,
          menuItems: [showHideConsoleMenuItem],
        },
        {
          field: 'uncounts',
          displayName: 'Outstanding',
          enableFiltering: false,
          menuItems: [showHideConsoleMenuItem],
        },
        {
          field: 'owningAddr',
          displayName: 'Address',
          filter: {placeholder: 'filter address...'},
          menuItems: [showHideConsoleMenuItem],
        }/*,
        {
          displayName: 'Quiesce',
                    cellClass: 'gridCellButton',
                    cellTemplate: '<button title="{$quiesceLinkText(row)$} this link" type="button" ng-class="quiesceLinkClass(row)" class="btn" ng-click="quiesceLink(row, $event)" ng-disabled="quiesceLinkDisabled(row)">{$quiesceLinkText(row)$}</button>',
          width: '10%'
                }*/
            ],
      enableRowSelection: true,
      enableRowHeaderSelection: false,
      multiSelect: false,
      enableColumnReordering: true,
      showColumnMenu: true,
      rowTemplate: 'dispatch/tplLinkRow.html',
      onRegisterApi: function(gridApi){
        gridApi.selection.on.rowSelectionChanged($scope,function(row){
          $scope.setActivated('link', row.entity.uid, 'uid')
        });
        gridApi.core.on.filterChanged( $scope, function() {
          var column = this.grid.columns[1];
          $scope.filter.endpointsOnly = (column.filters[0].term === 'endpoint' )
          column.colDef.menuItems[1].title = endpointLinksTitle()
        });
      }
    };
    $scope.allConnectionFields = []
    $scope.allConnectionGrid = {
      saveKey: 'allConnGrid',
      data: 'allConnectionFields',
      enableHorizontalScrollbar: uiGridConstants.scrollbars.NEVER,
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
      enableRowSelection: true,
      enableRowHeaderSelection: false,
      multiSelect: false,
      enableColumnResize: true,
      enableColumnReordering: true,
      onRegisterApi: function(gridApi){
        gridApi.selection.on.rowSelectionChanged($scope,function(row){
          $scope.setActivated('connection', row.entity.uid, 'uid')
        });
      }
    };
    $scope.addressFields = []
    $scope.addressGrid = {
      saveKey: 'addGrid',
      data: 'addressFields',
      enableHorizontalScrollbar: uiGridConstants.scrollbars.NEVER,
      columnDefs: [
        {
          field: 'attribute',
          displayName: 'Attribute',
          saveKey: 'addGrid',
        },
        {
          field: 'value',
          displayName: 'Value',
        }
      ],
      enableColumnResize: true,
      multiSelect: false
    }
    $scope.singleLinkFields = []
    $scope.linkGrid = {
      saveKey: 'linkGrid',
      data: 'singleLinkFields',
      enableHorizontalScrollbar: uiGridConstants.scrollbars.NEVER,
      columnDefs: [
        {
          field: 'attribute',
          displayName: 'Attribute',
        },
        {
          field: 'value',
          displayName: 'Value',
        }
      ],
      enableColumnResize: true,
      multiSelect: false
    }
    $scope.connectionFields = []
    $scope.connectionGrid = {
        saveKey: 'connGrid',
      data: 'connectionFields',
      enableHorizontalScrollbar: uiGridConstants.scrollbars.NEVER,
      columnDefs: [
                 {
                     field: 'attribute',
                     displayName: 'Attribute',
                    saveKey: 'connGrid',
                 },
                 {
                     field: 'value',
                     displayName: 'Value',
                 }
            ],
      enableColumnResize: true,
      multiSelect: false
    }
    $scope.allLogFields = []
    $scope.allLogGrid = {
      saveKey: 'allLogGrid',
      data: 'allLogFields',
      enableHorizontalScrollbar: uiGridConstants.scrollbars.NEVER,
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
      enableRowSelection: true,
      enableRowHeaderSelection: false,
      multiSelect: false,
      enableColumnResize: true,
      enableColumnReordering: true,
      onRegisterApi: function(gridApi){
        gridApi.selection.on.rowSelectionChanged($scope,function(row){
          $scope.setActivated('log', row.entity.module, 'module')
        });
      }
    };
  }

  function Overview (
    $scope,
    QDRService,
    QDRChartService,
    $location,
    $timeout,
    basePath,
    uiGridConstants) {

    var COLUMNSTATEKEY = 'QDRColumnKey.';
    var OVERVIEWEXPANDEDKEY = "QDROverviewExpanded"
    var OVERVIEWACTIVATEDKEY = "QDROverviewActivated"
    var OVERVIEWMODEIDS = "QDROverviewModeIds"

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
    };
    $scope.isValid = function (nav) {
      return nav.isValid()
    }
    $scope.isActive = function (nav) {
      return nav == $scope.activeTab;
    }
    var refreshInterval = 5000
    $scope.modes = [
      {title: 'Overview', name: 'Overview', right: false}
     ];

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
                  connField['routerId'] = connField.id; // like QDR.A
                  return true
                }
                return false
              })
            }
            $scope.allRouterFields = allRouterFields
            updateRouterTree(allRouterFields)
          }, nodeIds[0], false)
        }
      }
      nodeIds.forEach ( function (nodeId, i) {
        QDRService.getNodeInfo(nodeId, ".connection", ["role"], gotNodeInfo)
      })
      loadColState($scope.allRouters)
    }

    // get info for a single router
    var routerInfo = function (node) {
      $scope.router = node

      $scope.routerFields = [];
      var fields = Object.keys(node.fields)
      fields.forEach( function (field) {
        var attr = (field === 'connections') ? 'External connections' : field
        $scope.routerFields.push({attribute: attr, value: node.fields[field]})
      })
      loadColState($scope.routerGrid);
    }

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
/*
        if ($scope.filter.endpointsOnly === true) {
          if (link.linkType !== 'endpoint')
            include = false;
        }
*/
        if ($scope.filter.hideConsoles) {
          if (QDRService.isConsoleLink(link))
            include = false;
        }
        return include;
      })
      $scope.linkFields = filteredLinks;
    }

    $scope.$watch("filter", function (newValue, oldValue) {
      if (newValue !== oldValue) {
        $timeout($scope.allLinkInfo);
        localStorage[FILTERKEY] = JSON.stringify($scope.filter)
      }
    }, true)

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

    $scope.allLinkInfo = function () {
      getAllLinkFields([updateLinkGrid, updateLinkTree])
//      loadColState($scope.linksGrid);
    }

    var getAllLinkFields = function (completionCallbacks, selectionCallback) {
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
            var rawRate = rate(linkName, response, result)
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

              rate: QDRService.pretty(rawRate),
              rawRate: rawRate,
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
      var fields = Object.keys(address.fields)
      fields.forEach( function (field) {
        if (field != "title" && field != "uid")
          $scope.addressFields.push({attribute: field, value: address.fields[field]})
      })
      loadColState($scope.addressGrid);
    }

    // display the grid detail info for a single link
    var linkInfo = function (link) {
      if (!link)
        return;

      $scope.link = link;
      $scope.singleLinkFields = [];
      var fields = Object.keys(link.fields)
      var excludeFields = ["title", "uid", "uncounts", "rawDeliveryCount", "timestamp", "rawAddress"]
      fields.forEach( function (field) {
        if (excludeFields.indexOf(field) == -1)
          $scope.singleLinkFields.push({attribute: field, value: link.fields[field]})
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
      var fields = Object.keys(connection.fields)
      fields.forEach( function (field) {
        if (field != "title" && field != "uid")
          $scope.connectionFields.push({attribute: field, value: connection.fields[field]})
      })
      $scope.selectMode($scope.currentMode)
      loadColState($scope.connectionGrid);
    }

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
          var options = $scope.data.log.options;
          options.forEach( function (option) {
            if (option.id != 'all') {
              $scope.allLogFields.push(
                {module: option.id,
                enable: option.fields.enable,
                count: logResults.filter( function (entry) {
                  return entry.name === option.fields.module
                }).length
              })
            }
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
        return node.id === log.name
      })
      $scope.$apply();
    }

    var updateExpanded = function () {
      if (!$scope.selectedObject)
        return;
      // find the parent of the selectedObject and call it's info function
      if ($scope.selectedObject.pinfo)
        $scope.selectedObject.pinfo()
      $scope.selectedObject.info($scope.selectedObject)
    }

    var sendChartRequest = function (svgCharts) {
      var gotChartData = function (linkFields) {
        var now = new Date();
        svgCharts.forEach( function (svgChart) {
          var cutOff = new Date(now.getTime() - svgChart.chart.duration() * 60 * 1000);
          var name = svgChart.chart.name()
          var attr = svgChart.chart.attr()
          var data = svgChart.chart.data(name, attr) // get a reference to the data array
          var val = svgChart.chart.getVal(linkFields)
          data.push([now, val])
          // expire the old data
          while (data[0][0] < cutOff) {
            data.shift();
          }
        })
      }
      getAllLinkFields([gotChartData])
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

    $scope.setActivated = function (dropdown, uid, suid) {
      $("#sel" + dropdown).val(uid)

      var dd = $scope.data[dropdown];
      var newOption;
      if (uid == 'all') {
        newOption = dd.options[0];
      } else {
        dd.options.some( function (option) {
          if (option.fields && option.fields[suid] === uid) {
            newOption = option
            return true;
          } else
            return false;
        })
      }
      $scope.activated(newOption)
    }

    $scope.selectedObject = null;
    $scope.templateUrl = null;
    // activated is called each time a dropdown is changed
    // based on which node is clicked, load the correct data grid template and start getting the data
    $scope.activated = function (node) {
      if (!node)
        return;
      $scope.selectedObject = node;
      if (node.id !== "all") {
        $scope.data[node.type].sel = node
      }
      //saveExpanded()
      //saveActivated(node.data.key)

      $scope.templateUrl = 'dispatch/' + node.type + ".html";
      // the node's info function will fetch the grids data
      if (node.info) {
        $timeout(function () {node.info(node)})
      }
    }

    /* --------------------------------------------------
    *
    * setup the dropdowns
    *
    * -------------------------------------------------
    */
    var initDropDown = function (dd, info, type) {
      $scope.data[dd] = {}
      $scope.data[dd].options = getAllOption(dd, info, type)
      $scope.data[dd].sel = $scope.data[dd].options[0];
    }
    var getAllOption = function (dd, info, type) {
      return [{id: 'all',
        name: 'All ' + type,
        info: info,
        type: type}]
    }
    var updateDropdown = function (dropdown, allFields, idKey, nameKey, allInfo, allType, info) {
      var currentId = $scope.data[dropdown].sel.id;
      $scope.data[dropdown].options = getAllOption(dropdown, allInfo, allType)
      allFields.forEach( function (fields) {
        var option = {id: fields[idKey],
                     name: fields[nameKey],
                     info: info,
                     type: dropdown,
                     fields: fields,
                     pinfo: allInfo}
        $scope.data[dropdown].options.push(option);

        if (currentId === option.id) {
          $scope.data[dropdown].sel = option
        }
      })
      if ($scope.selectedObject && $scope.selectedObject.type === dropdown) {
        $scope.selectedObject = $scope.data[dropdown].sel;
//QDR.log.debug("updated " + dropdown + " to ")
//console.dump($scope.selectedObject)
      }
    }

    // get saved tree state
    var lastKey = loadActivatedNode();
    // called when the list of routers changes
    var updateRouterTree = function (routerFields) {
      updateDropdown('router', routerFields, 'nodeId', 'routerId', allRouterInfo, 'routers', routerInfo)
    }
    var updateAddressTree = function (addressFields) {
      updateDropdown('address', addressFields, 'uid', 'title', allAddressInfo, 'addresss', addressInfo)
    }
    // called whenever a background update is done and an option in the link dropdown is selected
    var updateLinkTree = function (linkFields) {
      updateDropdown('link', linkFields, 'uid', 'title', $scope.allLinkInfo, 'links', linkInfo)
    }
    var updateConnectionTree = function (connectionFields) {
      updateDropdown('connection', connectionFields, 'uid', 'host', allConnectionInfo, 'connections', connectionInfo)
    }

    $scope.data = {}
    initDropDown('router', allRouterInfo, 'routers')
    initDropDown('address', allAddressInfo, 'addresss')
    initDropDown('link', $scope.allLinkInfo, 'links', true)
    initDropDown('connection', allConnectionInfo, 'connections')
    initDropDown('log', allLogInfo, 'logs')
    // called after we are connected to initialize data
    var initTreeState = function () {
      allRouterInfo();
      allAddressInfo();
      $scope.allLinkInfo();
      allConnectionInfo()

      var nodeIds = QDRService.nodeIdList()
      QDRService.getNodeInfo(nodeIds[0], "log", ["module", "enable"], function (nodeName, entity, response) {
        var moduleIndex = response.attributeNames.indexOf('module')
        response.results.sort( function (a,b) {return a[moduleIndex] < b[moduleIndex] ? -1 : a[moduleIndex] > b[moduleIndex] ? 1 : 0})
        response.results.forEach( function (result) {
          var entry = QDRService.flatten(response.attributeNames, result)
          $scope.data.log.options.push({id: entry.module, name: entry.module, info: logInfo, type: 'log', fields: entry, pinfo: allLogInfo});
        })
        initTreeAndGrid();
      })
    }

    $scope.svgCharts = [];
    var updateTimer;
    var initCharts = function () {
      var charts = [];
      charts.push(QDRChartService.createRequestAndChart(
        {
         attr: 'Outstanding deliveries',
         nodeId: '',
         name: 'for all endpoints',
         entity: 'router.link',
         visibleDuration: 1,
         duration: 1,
        }))
        charts[charts.length-1].getVal = function (linkFields) {
          var uncountTotal = 0;
          linkFields.forEach( function (row) {
            if (row.linkType == 'endpoint' && !QDRService.isConsoleLink(row))
              uncountTotal += row.undeliveredCount + row.unsettledCount
          })
          return uncountTotal;
        }

      charts.push(QDRChartService.createRequestAndChart(
        {
         //type: "rate",
         attr: 'Outgoing deliveries per second',
         nodeId: '',
         name: 'for all endpoints',
         entity: 'router.link',
         visibleDuration: 1,
         duration: 1,
        }))
        charts[charts.length-1].getVal = function (linkFields) {
          var countTotal = 0.0;
          linkFields.forEach( function (row) {
            if (row.linkType == 'endpoint' && !QDRService.isConsoleLink(row) && row.linkDir == "out") {
              countTotal += parseFloat(row.rawRate + "")
            }
          })
          return countTotal;
        }

      charts.push(QDRChartService.createRequestAndChart(
        {
         //type: "rate",
         attr: 'Incoming deliveries per second',
         nodeId: '',
         name: 'for all endpoints',
         entity: 'router.link',
         visibleDuration: 1,
         duration: 1,
        }))
        charts[charts.length-1].getVal = function (linkFields) {
          var countTotal = 0.0;
          linkFields.forEach( function (row) {
            if (row.linkType == 'endpoint' && !QDRService.isConsoleLink(row) && row.linkDir == "in")
              countTotal += parseFloat(row.rawRate + "")
          })
          return countTotal;
        }
        charts[charts.length-1].areaColor = "#fcd6d6"
        charts[charts.length-1].lineColor = "#c70404"

      charts.forEach( function (chart) {
        $scope.svgCharts.push(new QDRChartService.AreaChart(chart));
      })
    }
    initCharts();

    var initTreeAndGrid = function () {

      // show the All routers page
      $scope.setActivated('link', 'all')

      // populate the data for each expanded node
      $timeout(updateExpanded);
      QDRService.addUpdatedAction( "overview", function () {
        $timeout(updateExpanded);
        sendChartRequest($scope.svgCharts)
      })
      // update the node list
      QDRService.startUpdating()

      var showChart = function () {
        // the chart divs are generated by angular and aren't available immediately
        var div = angular.element("#" + $scope.svgCharts[0].chart.id());
        if (!div.width()) {
          setTimeout(showChart, 100);
          return;
        }
        updateDialogChart();
      }

      var updateDialogChart = function () {
        $scope.svgCharts.forEach( function ( svgChart) {
          svgChart.tick(svgChart.chart.id())
        })
        if (updateTimer)
          clearTimeout(updateTimer)
        updateTimer = setTimeout(updateDialogChart, 1000);
      }
      showChart();

      loadColState($scope.allRouters);
      loadColState($scope.routerGrid);
      loadColState($scope.addressesGrid);
      loadColState($scope.addressGrid);
      loadColState($scope.linksGrid);
      loadColState($scope.linkGrid);
      loadColState($scope.allConnectionGrid);
      loadColState($scope.connectionGrid);
    } // end of initTreeAndGrid

    $scope.$on("$destroy", function( event ) {
      QDRService.stopUpdating()
      QDRService.delUpdatedAction("overview")
      if (updateTimer)
        clearTimeout(updateTimer)
    });
    initTreeState();
    QDRService.addDisconnectAction( function () {
      QDR.log.debug("disconnected from router. show a toast message");
      if (updateTimer)
        clearTimeout(updateTimer)
    })
  };

  return QDR;

}(QDR || {}));
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
   *
   * Controller that handles the QDR overview page
   */
  QDR.module.controller("QDR.OverviewController", ['$scope', 'QDRService', '$location', '$timeout', '$uibModal', 'uiGridConstants', function($scope, QDRService, $location, $timeout, $uibModal, uiGridConstants) {

    QDR.log.debug("QDR.OverviewControll started with location of " + $location.path() + " and connection of  " + QDRService.management.connection.is_connected());
    var updateIntervalHandle = undefined;
    var updateInterval = 5000;

    var COLUMNSTATEKEY = 'QDRColumnKey.';
    var OVERVIEWEXPANDEDKEY = "QDROverviewExpanded"
    var OVERVIEWACTIVATEDKEY = "QDROverviewActivated"
    var FILTERKEY = "QDROverviewFilters"
    var OVERVIEWMODEIDS = "QDROverviewModeIds"
    var treeRoot;   // the fancytree root node. initialized once log data is received

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
    $scope.templates = [
      { name: 'Routers', url: 'routers.html'},
      { name: 'Router', url: 'router.html'},
      { name: 'Addresses', url: 'addresses.html'},
      { name: 'Address', url: 'address.html'},
      { name: 'Links', url: 'links.html'},
      { name: 'Link', url: 'link.html'},
      { name: 'Connections', url: 'connections.html'},
      { name: 'Connection', url: 'connection.html'},
      { name: 'Logs', url: 'logs.html'},
      { name: 'Log', url: 'logModule.html'}
    ];
    var topLevelChildren = [];

    function afterSelectionChange(rowItem, checkAll) {
      var nodeId = rowItem.entity.nodeId;
      $("#overtree").fancytree("getTree").activateKey(nodeId);
    }

    var selectRow = function (gridApi) {
      if (!gridApi.selection)
        return
      gridApi.selection.on.rowSelectionChanged($scope,function(row){
        var treeKey = row.grid.options.treeKey
        if (treeKey && row.entity[treeKey]) {
          var key = row.entity[treeKey]
          $("#overtree").fancytree("getTree").activateKey(key);
        }
      })
    }
    $scope.routerPagingOptions = {
      pageSizes: [50, 100, 500],
      pageSize: 50,
      currentPage: 1
    };
    var getPagedData = function (pageSize, page) {
      $scope.totalRouters = $scope.allRouterFields.length
      $scope.allRouters.showFooter = $scope.totalRouters > 50
      $scope.pagedRouterFields = $scope.allRouterFields.slice((page - 1) * pageSize, page * pageSize);
    }
    $scope.$watch('pagingOptions', function (newVal, oldVal) {
      if (newVal !== oldVal && newVal.currentPage !== oldVal.currentPage) {
        getPagedData($scope.routerPagingOptions.pageSize, $scope.routerPagingOptions.currentPage);
      }
    }, true);

    $scope.totalRouters = 0;
    $scope.allRouterFields = [];
    $scope.pagedRouterFields = [];
    $scope.allRouters = {
      saveKey: 'allRouters',
      treeKey: 'nodeId',
      data: 'pagedRouterFields',
      columnDefs: [
        {
          field: 'id',
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
      jqueryUIDraggable: true,
      enablePaging: true,
      showFooter: $scope.totalRouters > 50,
      totalServerItems: 'totalRouters',
      pagingOptions: $scope.routerPagingOptions,
      multiSelect: false,
      enableSelectAll: false,
      onRegisterApi: selectRow,
      enableSelectionBatchEvent: false,
      enableRowHeaderSelection: false,
      minRowsToShow: Math.min(QDRService.management.topology.nodeIdList().length, 50),
      noUnselect: true,
    };

    // get info for all routers
    var allRouterInfo = function (node, callback) {
      var nodes = {}
      // gets called each node/entity response
      var gotNode = function (nodeName, entity, response) {
        if (!nodes[nodeName])
          nodes[angular.copy(nodeName)] = {}
        nodes[nodeName][entity] = response;
      }
      // send the requests for all connection and router info for all routers
      QDRService.management.topology.fetchAllEntities([
        {entity: "connection", attrs: ["role"]},
        {entity: "router"}], function () {
        // we have all the data now in the nodes object
        var allRouterFields = []
        for (var node in nodes) {
          var connections = 0
          for (var i=0; i<nodes[node]["connection"].results.length; ++i) {
            // we only requested "role" so it will be at [0]
            if (nodes[node]["connection"].results[i][0] === 'inter-router')
              ++connections
          }
          var routerRow = {connections: connections, nodeId: node, id: QDRService.management.topology.nameFromId(node)}
          nodes[node]["router"].attributeNames.forEach( function (routerAttr, i) {
            if (routerAttr !== "routerId" && routerAttr !== "id")
              routerRow[routerAttr] = nodes[node]["router"].results[0][i]
          })
          allRouterFields.push(routerRow)
        }
        $scope.allRouterFields = allRouterFields
        expandGridToContent("Routers", $scope.allRouterFields.length)
        getPagedData($scope.routerPagingOptions.pageSize, $scope.routerPagingOptions.currentPage);
        updateRouterTree(nodeIds)
        callback(null)
      }, gotNode)
      loadColState($scope.allRouters)
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
        },
        {
          field: 'value',
          displayName: 'Value',
        }
      ],
      jqueryUIDraggable: true,
      enableColumnResize: true,
      minRowsToShow: 25,
      multiSelect: false
    }

    $scope.router = null;
    // get info for a single router
    var routerInfo = function (node, callback) {
      $scope.router = node

      var routerFields = [];
      $scope.allRouterFields.some( function (field) {
        if (field.id === node.title) {
          Object.keys(field).forEach ( function (key) {
            var attr = (key === 'connections') ? 'External connections' : key
            routerFields.push({attribute: attr, value: field[key]})
          })
          return true
        }
      })
      $scope.routerFields = routerFields
      expandGridToContent("Router", $scope.routerFields.length)
      loadColState($scope.routerGrid);
      callback(null)
    }

    $scope.addressPagingOptions = {
      pageSizes: [50, 100, 500],
      pageSize: 50,
      currentPage: 1
    };
    var getAddressPagedData = function (pageSize, page) {
      $scope.totalAddresses = $scope.addressesData.length
      $scope.addressesGrid.showFooter = $scope.totalAddresses > 50
      $scope.pagedAddressesData = $scope.addressesData.slice((page - 1) * pageSize, page * pageSize);
    }
    $scope.$watch('addressPagingOptions', function (newVal, oldVal) {
      if (newVal !== oldVal && newVal.currentPage !== oldVal.currentPage) {
        getAddressPagedData($scope.addressPagingOptions.pageSize, $scope.addressPagingOptions.currentPage);
      }
    }, true);

    $scope.totalAddresses = 0;
    $scope.pagedAddressesData = []
    $scope.addressesData = []
    $scope.selectedAddresses = []
    $scope.addressesGrid = {
      saveKey: 'addressesGrid',
      treeKey: 'uid',
      data: 'pagedAddressesData',
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
      enablePaging: true,
      showFooter: $scope.totalAddresses > 50,
      totalServerItems: 'totalAddresses',
      pagingOptions: $scope.addressPagingOptions,
      enableColumnResize: true,
      multiSelect: false,
      enableSelectAll: false,
      onRegisterApi: selectRow,
      enableSelectionBatchEvent: false,
      enableRowHeaderSelection: false,
      noUnselect: true
    };

    // get info for all addresses
    var allAddressInfo = function (address, callback) {
      var nodes = {}
      // gets called each node/entity response
      var gotNode = function (nodeName, entity, response) {
        if (!nodes[nodeName])
          nodes[nodeName] = {}
        nodes[nodeName][entity] = angular.copy(response);
      }
      var addr_class = function (addr) {
        if (!addr) return "-"
            if (addr[0] == 'M')  return "mobile"
            if (addr[0] == 'R')  return "router"
            if (addr[0] == 'A')  return "area"
            if (addr[0] == 'L')  return "local"
            if (addr[0] == 'C')  return "link-incoming"
            if (addr[0] == 'E')  return "link-incoming"
            if (addr[0] == 'D')  return "link-outgoing"
            if (addr[0] == 'F')  return "link-outgoing"
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
      var prettyVal = function (val) {
        return QDRService.utilities.pretty(val || "-")
      }
      var addressFields = []
      var addressObjs = {}
      // send the requests for all connection and router info for all routers
      QDRService.management.topology.fetchAllEntities({entity: "router.address"}, function () {
        for (var node in nodes) {
          var response = nodes[node]["router.address"]
          response.results.forEach( function (result) {
            var address = QDRService.utilities.flatten(response.attributeNames, result)

            var addNull = function (oldVal, newVal) {
              if (oldVal != null && newVal != null)
                return oldVal + newVal
              if (oldVal != null)
                return oldVal
              return newVal
            }

            var uid = address.identity
            var identity = QDRService.utilities.identity_clean(uid)

            if (!addressObjs[QDRService.utilities.addr_text(identity)+QDRService.utilities.addr_class(identity)])
              addressObjs[QDRService.utilities.addr_text(identity)+QDRService.utilities.addr_class(identity)] = {
                address: QDRService.utilities.addr_text(identity),
                'class': QDRService.utilities.addr_class(identity),
                phase:   addr_phase(identity),
                inproc:  address.inProcess,
                local:   address.subscriberCount,
                remote:  address.remoteCount,
                'in':    address.deliveriesIngress,
                out:     address.deliveriesEgress,
                thru:    address.deliveriesTransit,
                toproc:  address.deliveriesToContainer,
                fromproc:address.deliveriesFromContainer,
                uid:     uid
              }
            else {
              var sumObj = addressObjs[QDRService.utilities.addr_text(identity)+QDRService.utilities.addr_class(identity)]
              sumObj.inproc = addNull(sumObj.inproc, address.inproc)
              sumObj.local = addNull(sumObj.local, address.local)
              sumObj.remote = addNull(sumObj.remote, address.remote)
              sumObj['in'] = addNull(sumObj['in'], address['in'])
              sumObj.out = addNull(sumObj.out, address.out)
              sumObj.thru = addNull(sumObj.thru, address.thru)
              sumObj.toproc = addNull(sumObj.toproc, address.toproc)
              sumObj.fromproc = addNull(sumObj.fromproc, address.fromproc)
            }
          })
        }
        for (var obj in addressObjs) {
          addressObjs[obj].inproc = prettyVal(addressObjs[obj].inproc)
          addressObjs[obj].local = prettyVal(addressObjs[obj].local)
          addressObjs[obj].remote = prettyVal(addressObjs[obj].remote)
          addressObjs[obj]['in'] = prettyVal(addressObjs[obj]['in'])
          addressObjs[obj].out = prettyVal(addressObjs[obj].out)
          addressObjs[obj].thru = prettyVal(addressObjs[obj].thru)
          addressObjs[obj].toproc = prettyVal(addressObjs[obj].toproc)
          addressObjs[obj].fromproc = prettyVal(addressObjs[obj].fromproc)
          addressFields.push(addressObjs[obj])
        }
        if (addressFields.length === 0)
          return;
        // update the grid's data
        addressFields.sort ( function (a,b) {
          return a.address + a['class'] < b.address + b['class'] ? -1 : a.address + a['class'] > b.address + b['class'] ? 1 : 0}
        )
        // callapse all records with same addres+class
        for (var i=1; i<addressFields.length; ++i) {

        }
        addressFields[0].title = addressFields[0].address
        for (var i=1; i<addressFields.length; ++i) {
          // if this address is the same as the previous address, add a class to the display titles
          if (addressFields[i].address === addressFields[i-1].address) {
            addressFields[i-1].title = addressFields[i-1].address + " (" + addressFields[i-1]['class'] + ")"
            addressFields[i].title = addressFields[i].address + " (" + addressFields[i]['class'] + ")"
          } else
            addressFields[i].title = addressFields[i].address
        }
        $scope.addressesData = addressFields
        expandGridToContent("Addresses", $scope.addressesData.length)
        getAddressPagedData($scope.addressPagingOptions.pageSize, $scope.addressPagingOptions.currentPage);
        // repopulate the tree's child nodes
        updateAddressTree(addressFields)
        loadColState($scope.addressesGrid);
        callback(null)
      }, gotNode)
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
          if (QDRService.utilities.isConsole(QDRService.management.topology.getConnForLink(link)))
            include = false;
        }
        return include;
      })
      QDR.log.debug("setting linkFields in updateLinkGrid")
      $scope.linkFields = filteredLinks;
      expandGridToContent("Links", $scope.linkFields.length)
      getLinkPagedData($scope.linkPagingOptions.pageSize, $scope.linkPagingOptions.currentPage);
      // if we have a selected link
      if ($scope.link) {
        // find the selected link in the array of all links
        var links = $scope.linkFields.filter(function (link) {
          return link.name === $scope.link.data.fields.name;
        })
        if (links.length > 0) {
          // linkInfo() is the function that is called by fancytree when a link is selected
          // It is passed a fancytree node. We need to simulate that node type to update the link grid
          linkInfo({data: {title: links[0].title, fields: links[0]}}, function () {$timeout(function (){})})
        }
      }
    }

    // get info for a all links
    $scope.linkPagingOptions = {
      pageSizes: [50, 100, 500],
      pageSize: 50,
      currentPage: 1
    };
    var getLinkPagedData = function (pageSize, page) {
      $scope.totalLinks = $scope.linkFields.length
      $scope.linksGrid.showFooter = $scope.totalLinks > 50
      $scope.pagedLinkData = $scope.linkFields.slice((page - 1) * pageSize, page * pageSize);
    }
    $scope.$watch('linkPagingOptions', function (newVal, oldVal) {
      if (newVal !== oldVal && newVal.currentPage !== oldVal.currentPage) {
        getLinkPagedData($scope.linkPagingOptions.pageSize, $scope.linkPagingOptions.currentPage);
      }
    }, true);

    $scope.totalLinks = 0;
    $scope.pagedLinkData = []
    $scope.selectedLinks = []

    $scope.linksGrid = {
      saveKey: 'linksGrid',
      treeKey: 'uid',
      data: 'pagedLinkData',
      columnDefs: [
        {
          field: 'link',
          displayName: 'Link',
          groupable:  false,
          saveKey: 'linksGrid',
          width: '11%'
        },
        {
          field: 'linkType',
          displayName: 'Link type',
          groupable:  false,
          width: '9%'
        },
        {
          field: 'linkDir',
          displayName: 'Link dir',
          groupable:  false,
          width: '8%'
        },
        {
          field: 'adminStatus',
          displayName: 'Admin status',
          groupable:  false,
          width: '9%'
        },
        {
          field: 'operStatus',
          displayName: 'Oper status',
          groupable:  false,
          width: '9%'
        },
        {
          field: 'deliveryCount',
          displayName: 'Delivery Count',
          groupable:  false,
          cellClass: 'grid-align-value',
          width: '11%'
        },
        {
          field: 'rate',
          displayName: 'Rate',
          groupable:  false,
          cellClass: 'grid-align-value',
          width: '8%'
        },
        {
          field: 'uncounts',
          displayName: 'Outstanding',
          groupable:  false,
          cellClass: 'grid-align-value',
          width: '9%'
        },
        {
          field: 'owningAddr',
          displayName: 'Address',
          groupable:  false,
          width: '15%'
        }/*,
        {
          displayName: 'Quiesce',
                    cellClass: 'gridCellButton',
                    cellTemplate: '<button title="{{quiesceLinkText(row)}} this link" type="button" ng-class="quiesceLinkClass(row)" class="btn" ng-click="quiesceLink(row, $event)" ng-disabled="quiesceLinkDisabled(row)">{{quiesceLinkText(row)}}</button>',
          width: '10%'
                }*/
            ],
      enablePaging: true,
      showFooter: $scope.totalLinks > 50,
      totalServerItems: 'totalLinks',
      pagingOptions: $scope.linkPagingOptions,
      enableColumnResize: true,
      enableColumnReordering: true,
      showColumnMenu: true,
      rowTemplate: 'linkRowTemplate.html',
      minRowsToShow: 15,
      multiSelect: false,
      enableSelectAll: false,
      onRegisterApi: selectRow,
      enableSelectionBatchEvent: false,
      enableRowHeaderSelection: false,
      noUnselect: true
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
return;
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
    var allLinkInfo = function (node, callback) {
      var gridCallback = function (linkFields) {
        QDRService.management.topology.ensureAllEntities({entity: "connection", force: true}, function () {
          // only update the grid with these fields if the List tree node is selected
          // this is becuase we are using the link grid in other places and we don't want to overwrite it
          if ($scope.template.name === "Links")
            updateLinkGrid(linkFields)
          updateLinkTree(linkFields)
          callback(null)
        })
      }
      getAllLinkFields([gridCallback])
      loadColState($scope.linksGrid);
    }

    var getAllLinkFields = function (completionCallbacks, selectionCallback) {
      if (!$scope.linkFields) {
        QDR.log.debug("$scope.linkFields was not defined!")
        return;
      }
      var nodeIds = QDRService.management.topology.nodeIdList()
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
          var delivered = QDRService.utilities.valFor(response.attributeNames, result, "deliveryCount") - oldname[0].rawDeliveryCount
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
        if (response.results)
        response.results.forEach( function (result) {
          var nameIndex = response.attributeNames.indexOf('name')
          var prettyVal = function (field) {
            var fieldIndex = response.attributeNames.indexOf(field)
            if (fieldIndex < 0) {
              return "-"
            }
            var val = result[fieldIndex]
            return QDRService.utilities.pretty(val)
          }
          var uncounts = function () {
            var und = QDRService.utilities.valFor(response.attributeNames, result, "undeliveredCount")
            var uns = QDRService.utilities.valFor(response.attributeNames, result, "unsettledCount")
            return QDRService.utilities.pretty(und + uns)
          }
          var linkName = function () {
            var namestr = QDRService.management.topology.nameFromId(nodeName)
            return namestr + ':' + QDRService.utilities.valFor(response.attributeNames, result, "identity")
          }
          var fixAddress = function () {
            var addresses = []
            var owningAddr = QDRService.utilities.valFor(response.attributeNames, result, "owningAddr") || ""
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
            var adminStatus = QDRService.utilities.valFor(response.attributeNames, result, "adminStatus")
            var operStatus = QDRService.utilities.valFor(response.attributeNames, result, "operStatus")
            var linkName = linkName()
            var linkType = QDRService.utilities.valFor(response.attributeNames, result, "linkType")
            var addresses = fixAddress();
            var link = QDRService.utilities.flatten(response.attributeNames, result)
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

              rate: QDRService.utilities.pretty(rate(linkName, response, result)),
              capacity: link.capacity,
              undeliveredCount: link.undeliveredCount,
              unsettledCount: link.unsettledCount,

              rawAddress: addresses[1],
              rawDeliveryCount: link.deliveryCount,
              name: link.name,
              linkName: link.linkName,
              connectionId: link.connectionId,
              linkDir: link.linkDir,
              linkType: linkType,
              peer: link.peer,
              type: link.type,

              uid:     linkName,
              timestamp: now,
              nodeId: nodeName,
              identity: link.identity,
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
        QDRService.management.topology.fetchEntity(nodeId, "router.link", [], gotLinkInfo);
      })
    }

    $scope.connectionPagingOptions = {
      pageSizes: [50, 100, 500],
      pageSize: 50,
      currentPage: 1
    };
    var getConnectionPagedData = function (pageSize, page) {
      $scope.totalConnections = $scope.allConnectionFields.length
      $scope.allConnectionGrid.showFooter = $scope.totalConnections > 50
      $scope.pagedConnectionsData = $scope.allConnectionFields.slice((page - 1) * pageSize, page * pageSize);
    }
    $scope.$watch('connectionPagingOptions', function (newVal, oldVal) {
      if (newVal !== oldVal && newVal.currentPage !== oldVal.currentPage) {
        getConnectionPagedData($scope.connectionPagingOptions.pageSize, $scope.connectionPagingOptions.currentPage);
      }
    }, true);

    $scope.totalConnections = 0;
    $scope.pagedConnectionsData = []
    $scope.allConnectionFields = []
    $scope.allConnectionSelections = [];
    $scope.allConnectionGrid = {
      saveKey: 'allConnGrid',
      treeKey: 'uid',
      data: 'pagedConnectionsData',
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
      },
      ],
      enablePaging: true,
      showFooter: $scope.totalConnections > 50,
      totalServerItems: 'totalConnections',
      pagingOptions: $scope.connectionPagingOptions,
      enableColumnResize: true,
      multiSelect: false,
      enableSelectAll: false,
      onRegisterApi: selectRow,
      enableSelectionBatchEvent: false,
      enableRowHeaderSelection: false,
      noUnselect: true
    };
    // get info for a all connections
    var allConnectionInfo = function (connection, callback) {
      getAllConnectionFields([updateConnectionGrid, updateConnectionTree, function () {callback(null)}])
      loadColState($scope.allConnectionGrid);
    }
    // called after conection data is available
    var updateConnectionGrid = function (connectionFields) {
      $scope.allConnectionFields = connectionFields;
      expandGridToContent("Connections", $scope.allConnectionFields.length)
      getConnectionPagedData($scope.connectionPagingOptions.pageSize, $scope.connectionPagingOptions.currentPage);
    }

    // get the connection data for all nodes
    // called periodically
    // creates a connectionFields array and calls the callbacks (updateTree and updateGrid)
    var getAllConnectionFields = function (callbacks) {
      var nodeIds = QDRService.management.topology.nodeIdList()
      var connectionFields = [];
      var expected = nodeIds.length;
      var received = 0;
      var gotConnectionInfo = function (nodeName, entity, response) {
        response.results.forEach( function (result) {

          var auth = "no_auth"
          var sasl = QDRService.utilities.valFor(response.attributeNames, result, "sasl")
          if (QDRService.utilities.valFor(response.attributeNames, result, "isAuthenticated")) {
            auth = sasl
            if (sasl === "ANONYMOUS")
              auth = "anonymous-user"
            else {
              if (sasl === "GSSAPI")
                sasl = "Kerberos"
              if (sasl === "EXTERNAL")
                sasl = "x.509"
              auth = QDRService.utilities.valFor(response.attributeNames, result, "user") + "(" +
                  QDRService.utilities.valFor(response.attributeNames, result, "sslCipher") + ")"
              }
          }

          var sec = "no-security"
          if (QDRService.utilities.valFor(response.attributeNames, result, "isEncrypted")) {
            if (sasl === "GSSAPI")
              sec = "Kerberos"
            else
              sec = QDRService.utilities.valFor(response.attributeNames, result, "sslProto") + "(" +
                  QDRService.utilities.valFor(response.attributeNames, result, "sslCipher") + ")"
          }

          var host = QDRService.utilities.valFor(response.attributeNames, result, "host")
          var connField = {
            host: host,
            security: sec,
            authentication: auth,
            routerId: nodeName,
            uid: host + QDRService.utilities.valFor(response.attributeNames, result, "identity")
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
        QDRService.management.topology.fetchEntity(nodeId, "connection", [], gotConnectionInfo)
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
      minRowsToShow: 11,
      jqueryUIDraggable: true,
      multiSelect: false
    }

    // get info for a single address
    var addressInfo = function (address, callback) {
      $scope.address = address
      var currentEntity = getCurrentLinksEntity();
      // we are viewing the addressLinks page
      if (currentEntity === 'Address' && entityModes[currentEntity].currentModeId === 'links') {
        updateModeLinks()
        callback(null)
        return;
      }

      $scope.addressFields = [];
      var fields = Object.keys(address.data.fields)
      fields.forEach( function (field) {
        if (field != "title" && field != "uid")
          $scope.addressFields.push({attribute: field, value: address.data.fields[field]})
      })
      expandGridToContent("Address", $scope.addressFields.length)
      callback(null)
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
      minRowsToShow: 24,
      jqueryUIDraggable: true,
      multiSelect: false
    }

    // display the grid detail info for a single link
    var linkInfo = function (link, callback) {
      if (!link) {
        callback(null)
        return;
      }
      $scope.link = link

      $scope.singleLinkFields = [];
      var fields = Object.keys(link.data.fields)
      var excludeFields = ["title", "uid", "uncounts", "rawDeliveryCount", "timestamp", "rawAddress"]
      fields.forEach( function (field) {
        if (excludeFields.indexOf(field) == -1)
          $scope.singleLinkFields.push({attribute: field, value: link.data.fields[field]})
      })
      expandGridToContent("Link", $scope.singleLinkFields.length)
      callback(null)
      loadColState($scope.linkGrid);
    }

    // get info for a single connection
    $scope.gridModes = [{
        content: '<a><i class="icon-list"></i> Attributes</a>',
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
          var owningAddr = QDRService.utilities.valFor(response.attributeNames, result, "owningAddr")
          var id = $scope.address.data.fields.uid
          return (owningAddr === $scope.address.data.fields.uid)
        }
      },
      Connection: {
        currentModeId: savedModeIds.Connection,
        filter: function (response, result) {
          var connectionId = QDRService.utilities.valFor(response.attributeNames, result, "connectionId")
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
        getLinkPagedData($scope.linkPagingOptions.pageSize, $scope.linkPagingOptions.currentPage);
        updateModeLinks();
      }
      updateExpanded()
    }
    $scope.isModeSelected = function (mode, entity) {
      return mode.id === entityModes[entity].currentModeId
    }
    $scope.isModeVisible = function (entity, id) {
      return entityModes[entity].currentModeId === id
    }

    var updateEntityLinkGrid = function (linkFields) {
      $scope.linkFields = linkFields
      expandGridToContent("Link", $scope.linkFields.length)
      getLinkPagedData($scope.linkPagingOptions.pageSize, $scope.linkPagingOptions.currentPage);
    }
    // based on which entity is selected, get and filter the links
    var updateModeLinks = function () {
      var currentEntity = getCurrentLinksEntity()
      if (!currentEntity)
        return;
      getAllLinkFields([updateEntityLinkGrid], entityModes[currentEntity].filter)
    }
    var getCurrentLinksEntity = function () {
      var currentEntity;
      var active = $("#overtree").fancytree("getActiveNode");
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
      $event.stopPropagation()
      QDRService.management.topology.quiesceLink(row.entity.nodeId, row.entity.name)
        .then( function (results, context) {
          var statusCode = context.message.application_properties.statusCode;
          if (statusCode < 200 || statusCode >= 300) {
            Core.notification('error', context.message.statusDescription);
            QDR.log.error('Error ' + context.message.statusDescription)
          }
        })
    }

    $scope.quiesceLinkDisabled = function (row) {
      return (row.entity.operStatus !== 'up' && row.entity.operStatus !== 'down')
    }
    $scope.quiesceLinkText = function (row) {
      return row.entity.adminStatus === 'disabled' ? "Revive" : "Quiesce";
    }

    $scope.expandAll = function () {
      $("#overtree").fancytree("getRoot").visit(function(node){
                node.expand(true);
            });
    }
    $scope.contractAll = function () {
      $("#overtree").fancytree("getRoot").visit(function(node){
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
      minRowsToShow: 21,
      jqueryUIDraggable: true,
      multiSelect: false
    }

    var connectionInfo = function (connection, callback) {
      if (!connection) {
        callback(null)
        return;
      }
      $scope.connection = connection

      var currentEntity = getCurrentLinksEntity();
      // we are viewing the connectionLinks page
      if (currentEntity === 'Connection' && entityModes[currentEntity].currentModeId === 'links') {
        updateModeLinks()
        callback(null)
        return;
      }

      $scope.connectionFields = [];
      var fields = Object.keys(connection.data.fields)
      fields.forEach( function (field) {
        if (field != "title" && field != "uid")
          $scope.connectionFields.push({attribute: field, value: connection.data.fields[field]})
      })
      expandGridToContent("Connection", $scope.connectionFields.length)
      // this is missing an argument?
      loadColState($scope.connectionGrid);
      callback(null)
    }


    //var logModuleCellTemplate = '<div ng-click="logInfoFor(row, col)" class="ui-grid-cell-contents">{{grid.getCellValue(row, col) | pretty}}</div>'
    var logModuleCellTemplate = '<div class="ui-grid-cell-contents" ng-click="grid.appScope.logInfoFor(row, col)">{{COL_FIELD CUSTOM_FILTERS | pretty}}</div>'

    $scope.logModule = {}
    $scope.logModuleSelected = []
    $scope.logModuleData = []
    $scope.logModuleGrid = {
      data: 'logModuleData',
      columnDefs: [
        {
          field: 'nodeName',
          displayName: 'Router',
        },
        {
          field: 'enable',
          displayName: 'Enable level',
        },
        {
          field: 'noticeCount',
          displayName: 'Notice',
          cellTemplate: logModuleCellTemplate,
          cellClass: 'grid-align-value',
        },
        {
          field: 'infoCount',
          displayName: 'Info',
          cellTemplate: logModuleCellTemplate,
          cellClass: 'grid-align-value',
        },
        {
          field: 'traceCount',
          displayName: 'Trace',
          cellTemplate: logModuleCellTemplate,
          cellClass: 'grid-align-value',
        },
        {
          field: 'debugCount',
          displayName: 'Debug',
          cellTemplate: logModuleCellTemplate,
          cellClass: 'grid-align-value',
        },
        {
          field: 'warningCount',
          displayName: 'Warning',
          cellTemplate: logModuleCellTemplate,
          cellClass: 'grid-align-value',
        },
        {
          field: 'errorCount',
          displayName: 'Error',
          cellTemplate: logModuleCellTemplate,
          cellClass: 'grid-align-value',
        },
        {
          field: 'criticalCount',
          displayName: 'Critical',
          cellTemplate: logModuleCellTemplate,
          cellClass: 'grid-align-value',
        },
      ],
      enableColumnResize: true,
      jqueryUIDraggable: true,
      multiSelect: false,
      enableSelectAll: false,
      onRegisterApi: selectRow,
      enableSelectionBatchEvent: false,
      enableRowHeaderSelection: false,
      minRowsToShow: Math.min(QDRService.management.topology.nodeIdList().length, 50),
      noUnselect: true
    }

    $scope.logInfoFor = function (row, col) {
      logDialog(row, col)
    }

    function logDialog(row, col) {
      var d = $uibModal.open({
      animation: true,
      templateUrl: 'viewLogs.html',
      controller: 'QDR.OverviewLogsController',
      resolve: {
        nodeName: function () {
          return row.entity.nodeName
        },
        module: function () {
          return row.entity.name
        },
        level: function () {
          return col.displayName
        },
        nodeId: function () {
          return row.entity.nodeId
        },
      }
    });
    d.result.then(function (result) {
      QDR.log.debug("d.open().then");
    }, function () {
      QDR.log.debug('Modal dismissed at: ' + new Date());
    });
  };

    var numberTemplate = '<div class="ngCellText" ng-class="col.colIndex()"><span ng-cell-text>{{COL_FIELD | pretty}}</span></div>'
    $scope.allLogFields = []
    $scope.allLogSelections = [];
    $scope.allLogGrid = {
      saveKey: 'allLogGrid',
      treeKey: 'name',
      data: 'allLogFields',
      columnDefs: [
        {
          field: 'name',
          saveKey: 'allLogGrid',
          displayName: 'Module'
        },
        {
          field: 'noticeCount',
          displayName: 'Notice',
          cellTemplate: numberTemplate,
          cellClass: 'grid-align-value',
        },
        {
          field: 'infoCount',
          displayName: 'Info',
          cellTemplate: numberTemplate,
          cellClass: 'grid-align-value',
        },
        {
          field: 'traceCount',
          displayName: 'Trace',
          cellTemplate: numberTemplate,
          cellClass: 'grid-align-value',
        },
        {
          field: 'debugCount',
          displayName: 'Debug',
          cellTemplate: numberTemplate,
          cellClass: 'grid-align-value',
        },
        {
          field: 'warningCount',
          displayName: 'Warning',
          cellTemplate: numberTemplate,
          cellClass: 'grid-align-value',
        },
        {
          field: 'errorCount',
          displayName: 'Error',
          cellTemplate: numberTemplate,
          cellClass: 'grid-align-value',
        },
        {
          field: 'criticalCount',
          displayName: 'Critical',
          cellTemplate: numberTemplate,
          cellClass: 'grid-align-value',
        },
      ],
      //enableCellSelection: true,
      enableColumnResize: true,
      minRowsToShow: 17,
      multiSelect: false,
      enableSelectAll: false,
      onRegisterApi: selectRow,
      enableSelectionBatchEvent: false,
      enableRowHeaderSelection: false,
      noUnselect: true
    };

    var allLogEntries = {}
    var allLogInfo = function (log, callback) {
        // update the count of entries for each module
        $scope.allLogFields = []
        var logResults = {}
        var logDetails = {}

        var gotLogStats = function (node, entity, response) {
          logDetails[node] = []
          response.results.forEach( function (result) {
            var oresult = QDRService.utilities.flatten(response.attributeNames, result)
            // make a copy for the details grid since logResults has the same object reference
            logDetails[node].push(angular.copy(oresult))
            if (!(oresult.name in logResults)) {
              logResults[oresult.name] = oresult
            }
            else {
              response.attributeNames.forEach( function (attr, i) {
                if (attr.substr(attr.length-5) === 'Count') {
                  logResults[oresult.name][attr] += result[i]
                }
              })
            }
          })
        }
        var gotAllLogStats = function () {
          var sortedModules = Object.keys(logResults)
          sortedModules.sort(function (a,b) {return a<b?-1:a>b?1:0})
          $scope.allLogFields = []
          sortedModules.forEach( function (module) {
            $scope.allLogFields.push(logResults[module])
          })
          expandGridToContent("Logs", $scope.allLogFields.length)
          allLogEntries = logDetails
          updateLogTree($scope.allLogFields)
          callback(null)
        }
        QDRService.management.topology.fetchAllEntities({entity: 'logStats'}, gotAllLogStats, gotLogStats)
    }

    var expandGridToContent = function (type, rows) {
      var tree = $("#overtree").fancytree("getTree")
      var node = null
      if (tree && tree.getActiveNode)
        node = tree.getActiveNode()
      if (node) {
        if (node.data.type === type) {
          var height = (rows+1) * 30 + 40 // header is 40px
          var gridDetails = $('#overview-controller .grid')
          gridDetails.css('height', height + "px")
        }
      }
    }

    $scope.logFields = []
    // get info for a single log
    var logInfo = function (node, callback) {

        var gotLogInfo = function (responses) {
          $scope.logModuleData = []
          $scope.logModule.module = node.key
          for (var n in responses) {
            var moduleIndex = responses[n]['log'].attributeNames.indexOf("module")
            var result = responses[n]['log'].results.filter( function (r) {
              return r[moduleIndex] === node.key
            })[0]
            var logInfo = QDRService.utilities.flatten(responses[n]['log'].attributeNames, result)
            var entry = allLogEntries[n]
            entry.forEach( function (module) {
              if (module.name === node.key) {
                module.nodeName = QDRService.management.topology.nameFromId(n)
                module.nodeId = n
                module.enable = logInfo.enable
                $scope.logModuleData.push(module)
              }
            })
          }
          $scope.logModuleData.sort ( function (a,b) { return a.nodeName < b.nodeName? -1 : a.nodeName> b.nodeName? 1 : 0})
          expandGridToContent("Log", $scope.logModuleData.length)
          callback(null)
        }
        QDRService.management.topology.fetchAllEntities({entity: 'log', attrs: ['module', 'enable']}, gotLogInfo)
    }

    var getExpandedList = function () {
      if (!treeRoot)
        return;
      var list = [];
      if (treeRoot.visit) {
        treeRoot.visit(function(node){
          if (node.isExpanded()) {
            list.push(node.data.parentKey)
          }
          });
      }
      return list;
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

    var setTemplate = function (node) {
      var type = node.data.type;
      var template = $scope.templates.filter( function (tpl) {
        return tpl.name == type;
      })
      $timeout( function () {
        $scope.template = template[0];
      })
    }
    $scope.template = $scope.templates[0]
    // activated is called each time a tree node is clicked
    // based on which node is clicked, load the correct data grid template and start getting the data
    var onTreeNodeActivated = function (event, data) {
      saveExpanded()
      saveActivated(data.node.key)
      $scope.ActivatedKey = data.node.key
      setTemplate(data.node)
      updateExpanded()
    }

    var onTreeNodeExpanded = function (event, data) {
      saveExpanded()
      updateExpanded()
    }

    if (!QDRService.management.connection.is_connected()) {
      QDR.redirectWhenConnected($location, "overview")
      return;
    }

    /* --------------------------------------------------
     *
     * setup the tree on the left
     *
     * -------------------------------------------------
     */
    var getActiveChild = function (node) {
      var active = node.children.filter(function (child) {
        return child.isActive()
      })
      if (active.length > 0)
        return active[0].key
      return null
    }
    // utility function called by each top level tree node when it needs to populate its child nodes
    var updateLeaves = function (leaves, entity, worker) {
      var tree = $("#overtree").fancytree("getTree"), node;
      if (tree && tree.getNodeByKey) {
        node = tree.getNodeByKey(entity)
      }
      if (!tree || !node) {
        return
      }
      var wasActive = node.isActive()
      var wasExpanded = node.isExpanded()
      var activeChildKey = getActiveChild(node)
      node.removeChildren()
      var children = []
      leaves.forEach( function (leaf) {
        children.push(worker(leaf))
      })
      node.addNode(children)
      // top level node was expanded
      if (wasExpanded)
        node.setExpanded(true, {noAnimation: true, noEvents: true})
      if (wasActive) {
        node.setActive(true, {noAnimation: true, noEvents: true})
      } else {
        // re-active the previously active child node
        if (activeChildKey) {
          var newNode = tree.getNodeByKey(activeChildKey)
          // the node may not be there after the update
          if (newNode)
            newNode.setActive(true, {noAnimation: true, noEvents: true}) // fires the onTreeNodeActivated event for this node
        }
      }
      resizer()
    }

    var resizer = function () {
      // this forces the tree and the grid to be the size of the browser window.
      // the effect is that the tree and the grid will have vertical scroll bars if needed.
      // the alternative is to let the tree and grid determine the size of the page and have
      // the scroll bar on the window
      var viewport = $('#overview-controller .pane-viewport')
      viewport.height( window.innerHeight - viewport.offset().top)

      // don't allow HTML in the tree titles
      $('.fancytree-title').each( function (idx) {
        var unsafe = $(this).html()
        $(this).html(unsafe.replace(/</g, "&lt;").replace(/>/g, "&gt;"))
      })

      // remove the comments to allow the tree to take all the height it needs
/*
      var gridDetails = $('#overview-controller .grid')
      gridDetails.height( window.innerHeight - gridDetails.offset().top)

      var gridViewport = $('#overview-controller .ui-grid-viewport')
      gridViewport.height( window.innerHeight - gridViewport.offset().top )
*/
    }
    $(window).resize(resizer);

    // get saved tree state
    $scope.ActivatedKey = loadActivatedNode();
    var expandedNodeList = loadExpandedNodeList();
    var firstTime = true;

    // create a routers tree branch
    var routers = new Folder("Routers")
    routers.type = "Routers"
    routers.info = {fn: allRouterInfo}
    routers.expanded = (expandedNodeList.indexOf("Routers") > -1)
    routers.key = "Routers"
    routers.parentKey= "Routers"
    routers.extraClasses = "routers"
    topLevelChildren.push(routers)
    // called when the list of routers changes
    var updateRouterTree = function (nodes) {
      var worker = function (node) {
        var name = QDRService.management.topology.nameFromId(node)
        var router = new Leaf(name)
        router.type = "Router"
        router.info = {fn: routerInfo}
        router.nodeId = node
        router.key = node
        router.extraClasses = "router"
        router.parentKey = "Routers"
        return router;
      }
      updateLeaves(nodes, "Routers", worker)
    }

    // create an addresses tree branch
    var addresses = new Folder("Addresses")
    addresses.type = "Addresses"
    addresses.info = {fn: allAddressInfo}
    addresses.expanded = (expandedNodeList.indexOf("Addresses") > -1)
    addresses.key = "Addresses"
    addresses.parentKey = "Addresses"
    addresses.extraClasses = "addresses"
    topLevelChildren.push(addresses)
    var updateAddressTree = function (addressFields) {
      var worker = function (address) {
        var a = new Leaf(address.title)
        a.info = {fn: addressInfo}
        a.key = address.uid
        a.fields = address
        a.type = "Address"
        a.tooltip = address['class'] + " address"
        if (address.address === '$management')
          a.tooltip = "internal " + a.tooltip
        a.extraClasses = a.tooltip
        a.parentKey = "Addresses"
        return a;
      }
      updateLeaves(addressFields, "Addresses", worker)
    }

    $scope.$watch("filter", function (newValue, oldValue) {
      if (newValue !== oldValue) {
        allLinkInfo(null, function () {$timeout(function (){})})
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
    links.info = {fn: allLinkInfo}
    links.expanded = (expandedNodeList.indexOf("Links") > -1)
    links.key = "Links"
    links.parentKey = "Links"
    links.extraClasses = "links"
    topLevelChildren.push(links)

    // called both before the tree is created and whenever a background update is done
    var updateLinkTree = function (linkFields) {
      var worker = function (link) {
        var l = new Leaf(link.title)
        var isConsole = QDRService.utilities.isConsole(QDRService.management.topology.getConnForLink(link))
        l.info = {fn: linkInfo}
        l.key = link.uid
        l.fields = link
        l.type = "Link"
        l.parentKey = "Links"
        if (isConsole)
          l.tooltip = "console link"
        else
          l.tooltip = link.linkType  + " link"
        l.extraClasses = l.tooltip
        return l;
      }
      updateLeaves(linkFields, "Links", worker)
    }

    var connections = new Folder("Connections")
    connections.type = "Connections"
    connections.info = {fn: allConnectionInfo}
    connections.expanded = (expandedNodeList.indexOf("Connections") > -1)
    connections.key = "Connections"
    connections.parentKey = "Connections"
    connections.extraClasses = "connections"
    topLevelChildren.push(connections)

    updateConnectionTree = function (connectionFields) {
      var worker = function (connection) {
        var c = new Leaf(connection.host)
        var isConsole = QDRService.utilities.isAConsole (connection.properties, connection.identity, connection.role, connection.routerId)
        c.type = "Connection"
        c.info = {fn: connectionInfo}
        c.key = connection.uid
        c.fields = connection
        if (isConsole)
          c.tooltip = "console connection"
        else
          c.tooltip = connection.role === "inter-router" ? "inter-router connection" : "external connection"
        c.extraClasses = c.tooltip
        c.parentKey = "Connections"
        return c
      }
      updateLeaves(connectionFields, "Connections", worker)
    }

    var updateLogTree = function (logFields) {
      var worker = function (log) {
        var l = new Leaf(log.name)
        l.type = "Log"
        l.info = {fn: logInfo}
        l.key = log.name
        l.parentKey = "Logs"
        l.extraClasses = "log"
        return l
      }
      updateLeaves(logFields, "Logs", worker)
    }

    var htmlReady = false;
    var dataReady = false;
    $scope.largeNetwork = QDRService.management.topology.isLargeNetwork()
    var logs = new Folder("Logs")
    logs.type = "Logs"
    logs.info = {fn: allLogInfo}
    logs.expanded = (expandedNodeList.indexOf("Logs") > -1)
    logs.key = "Logs"
    logs.parentKey = "Logs"
    if (QDRService.management.connection.versionCheck('0.8.0'))
      topLevelChildren.push(logs)

    var onTreeInitialized = function (event, data) {
      treeRoot = data.tree
      loadColState($scope.allRouters);
      loadColState($scope.routerGrid);
      loadColState($scope.addressesGrid);
      loadColState($scope.addressGrid);
      loadColState($scope.linksGrid);
      loadColState($scope.linkGrid);
      loadColState($scope.allConnectionGrid);
      loadColState($scope.connectionGrid);
      var activeNode = treeRoot.getNodeByKey($scope.ActivatedKey)
      if (activeNode)
        activeNode.setActive(true)
      updateExpanded()
    }
    var initTreeAndGrid = function () {
      if (!htmlReady || !dataReady)
        return;
      var div = angular.element("#overtree");
      if (!div.width()) {
        setTimeout(initTreeAndGrid, 100);
        return;
      }
      $('#overtree').fancytree({
        activate: onTreeNodeActivated,
        expand: onTreeNodeExpanded,
        init: onTreeInitialized,
        autoCollapse: $scope.largeNetwork,
        activeVisible: !$scope.largeNetwork,
        clickFolderMode: 1,
        source: topLevelChildren
      })
    }

    $scope.overviewLoaded = function () {
      htmlReady = true;
      initTreeAndGrid();
    }

    var nodeIds = QDRService.management.topology.nodeIdList()
    // add placeholders for the top level tree nodes
    var topLevelTreeNodes = [routers, addresses, links, connections, logs]
    topLevelTreeNodes.forEach( function (parent) {
      var placeHolder = new Leaf("loading...")
      placeHolder.extraClasses = "loading"
      parent.children = [placeHolder]
    })

    var updateExpanded = function () {
      QDR.log.debug("updateExpandedEntities")
      clearTimeout(updateIntervalHandle)

      var tree = $("#overtree").fancytree("getTree");
      if (tree && tree.visit) {
        var q = d3.queue(10)
        tree.visit( function (node) {
          if (node.isActive() || node.isExpanded()) {
            q.defer(node.data.info.fn, node)
          }
        })
        q.await( function (error) {
          if (error)
            QDR.log.error(error.message)

          // if there are no active nodes, activate the saved one
          var tree = $("#overtree").fancytree("getTree")
          if (!tree.getActiveNode()) {
            var active = tree.getNodeByKey($scope.ActivatedKey)
            if (active) {
              active.setActive(true)
            } else {
              tree.getNodeByKey("Routers").setActive(true)
            }
          }

          // fancytree animations sometimes stop short of shrinking its placeholder spans
          d3.selectAll('.ui-effects-placeholder').style('height', '0px')
          // once all expanded tree nodes have been update, schedule another update
          $timeout(function () {
            updateIntervalHandle = setTimeout(updateExpanded, updateInterval)
          })
        })
      }
    }

    dataReady = true;
    initTreeAndGrid();
    $scope.$on("$destroy", function( event ) {
      clearTimeout(updateIntervalHandle)
      $(window).off("resize", resizer);
    });

  }]);

  return QDR;

}(QDR || {}));

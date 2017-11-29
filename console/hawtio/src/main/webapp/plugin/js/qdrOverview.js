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
  QDR.module.controller("QDR.OverviewController", ['$scope', 'QDRService', '$location', '$timeout', '$dialog', function($scope, QDRService, $location, $timeout, $dialog) {

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
    $scope.filteredLinkFields = []
    $scope.Link = null;
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

    $scope.allRouterSelected = function (row ) {
      console.log("row selected" + row)
    }
    function afterSelectionChange(rowItem, checkAll) {
      var nodeId = rowItem.entity.nodeId;
      $("#overtree").dynatree("getTree").activateKey(nodeId);
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
    $scope.allRouterSelections = [];
    $scope.allRouters = {
      saveKey: 'allRouters',
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
      enablePaging: true,
      showFooter: $scope.totalRouters > 50,
      totalServerItems: 'totalRouters',
      pagingOptions: $scope.routerPagingOptions,
      multiSelect: false,
      selectedItems: $scope.allRouterSelections,
      plugins: [new ngGridFlexibleHeightPlugin()],
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
      var nodes = {}
      // gets called each node/entity response
      var gotNode = function (nodeName, entity, response) {
        if (!nodes[nodeName])
          nodes[angular.copy(nodeName)] = {}
        nodes[nodeName][entity] = angular.copy(response);
      }
      // send the requests for all connection and router info for all routers
      QDRService.fetchAllEntities([{entity: ".connection", attrs: ["role"]}], function () {
        QDRService.fetchAllEntities([{entity: ".router"}], function () {
          // we have all the data now in the nodes object
          var allRouterFields = []
          for (var node in nodes) {
            var connections = 0
            for (var i=0; i<nodes[node][".connection"].results.length; ++i) {
              // we only requested "role" so it will be at [0]
              if (nodes[node][".connection"].results[i][0] === 'inter-router')
                ++connections
            }
            var routerRow = {connections: connections, nodeId: node, id: QDRService.nameFromId(node)}
            nodes[node][".router"].attributeNames.forEach( function (routerAttr, i) {
              if (routerAttr !== "routerId" && routerAttr !== "id")
                routerRow[routerAttr] = nodes[node][".router"].results[0][i]
            })
            allRouterFields.push(routerRow)
          }
          $timeout(function () {
            $scope.allRouterFields = allRouterFields
            getPagedData($scope.routerPagingOptions.pageSize, $scope.routerPagingOptions.currentPage);
            updateRouterTree(nodeIds)
            scheduleNextUpdate()
            //if ($scope.router)
            //  routerInfo($scope.router)
          })
        }, gotNode)
      }, gotNode)
      loadColState($scope.allRouters)
    }

    $scope.router = null;
    // get info for a single router
    var routerInfo = function (node) {
      $scope.router = node

      var routerFields = [];
      $scope.allRouterFields.some( function (field) {
        if (field.id === node.data.title) {
          Object.keys(field).forEach ( function (key) {
            if (key !== '$$hashKey') {
              var attr = (key === 'connections') ? 'External connections' : key
              routerFields.push({attribute: attr, value: field[key]})
            }
          })
          return true
        }
      })
      $timeout(function () {$scope.routerFields = routerFields})
      scheduleNextUpdate()
      loadColState($scope.routerGrid);
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
      selectedItems: $scope.selectedAddresses,
      plugins: [new ngGridFlexibleHeightPlugin()],
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
        return QDRService.pretty(val || "-")
      }
      var addressFields = []
      var addressObjs = {}
      // send the requests for all connection and router info for all routers
      QDRService.fetchAllEntities({entity: ".router.address"}, function () {
        for (var node in nodes) {
          var response = nodes[node][".router.address"]
          response.results.forEach( function (result) {
            var address = QDRService.flatten(response.attributeNames, result)

            var addNull = function (oldVal, newVal) {
              if (oldVal != null && newVal != null)
                return oldVal + newVal
              if (oldVal != null)
                return oldVal
              return newVal
            }

            var uid = address.identity
            var identity = QDRService.identity_clean(uid)

            if (!addressObjs[QDRService.addr_text(identity)+QDRService.addr_class(identity)])
              addressObjs[QDRService.addr_text(identity)+QDRService.addr_class(identity)] = {
                address: QDRService.addr_text(identity),
                'class': QDRService.addr_class(identity),
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
              var sumObj = addressObjs[QDRService.addr_text(identity)+QDRService.addr_class(identity)]
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
        $timeout(function () {
          $scope.addressesData = addressFields
          getAddressPagedData($scope.addressPagingOptions.pageSize, $scope.addressPagingOptions.currentPage);
          // repopulate the tree's child nodes
          updateAddressTree(addressFields)
          scheduleNextUpdate()
        })
      }, gotNode)
      loadColState($scope.addressesGrid);
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
      QDR.log.debug("setting linkFields in updateLinkGrid filteredLinks.length=" + filteredLinks.length)
      $scope.filteredLinkFields = filteredLinks;
      getLinkPagedData($scope.linkPagingOptions.pageSize, $scope.linkPagingOptions.currentPage);
      // if we have a selected link
      if ($scope.Link) {
        // find the selected link in the array of all links
        var links = $scope.filteredLinkFields.filter(function (link) {
          return link.name === $scope.Link.data.fields.name;
        })
        if (links.length > 0) {
          // linkInfo() is the function that is called by dynatree when a link is selected
          // It is passed a dynatree node. We need to simulate that node type to update the link grid
          $scope.Link.data.info({data: {title: links[0].title, fields: links[0]}})
          //linkInfo({data: {title: links[0].title, fields: links[0]}})
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
      $scope.totalLinks = $scope.filteredLinkFields.length
      $scope.linksGrid.showFooter = $scope.totalLinks > 50
      $scope.pagedLinkData = $scope.filteredLinkFields.slice((page - 1) * pageSize, page * pageSize);
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
      // aggregateTemplate: "linkAggTemplate.html",
      multiSelect: false,
      selectedItems: $scope.selectedLinks,
      plugins: [new ngGridFlexibleHeightPlugin()],
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
      var gridCallback = function (linkFields) {
        QDRService.ensureAllEntities({entity: ".connection", force: true}, function () {
          // only update the grid with these fields if the List tree node is selected
          // this is becuase we are using the link grid in other places and we don't want to overwrite it
          if ($scope.template.name === "Links")
            updateLinkGrid(linkFields)
          updateLinkTree(linkFields)
        })
      }
      getAllLinkFields([gridCallback, scheduleNextUpdate])
      loadColState($scope.linksGrid);
    }

    var getAllLinkFields = function (completionCallbacks, selectionCallback) {
      if (!$scope.filteredLinkFields) {
        QDR.log.info("$scope.filteredLinkFields was not defined")
        return;
      }
      var nodeIds = QDRService.nodeIdList()
      var linkFields = []
      var now = Date.now()
      var rate = function (linkName, response, result) {
        if (!$scope.filteredLinkFields)
          return 0;
        var oldname = $scope.filteredLinkFields.filter(function (link) {
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
            var link = QDRService.flatten(response.attributeNames, result)
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
        QDRService.fetchEntity(nodeId, "router.link", [], gotLinkInfo);
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
      selectedItems: $scope.allConnectionSelections,
      plugins: [new ngGridFlexibleHeightPlugin()],
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
      getAllConnectionFields([updateConnectionGrid, updateConnectionTree, scheduleNextUpdate])
      loadColState($scope.allConnectionGrid);
    }
    // called after conection data is available
    var updateConnectionGrid = function (connectionFields) {
      $scope.allConnectionFields = connectionFields;
      getConnectionPagedData($scope.connectionPagingOptions.pageSize, $scope.connectionPagingOptions.currentPage);
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
          $timeout( function () {
            callbacks.forEach( function (cb) {
              cb(connectionFields)
            })
          })
        }
      }
      nodeIds.forEach( function (nodeId) {
        QDRService.fetchEntity(nodeId, ".connection", [], gotConnectionInfo)
      })
    }

    var SingleEntityGrid = function (name) {
      this.saveKey = name
      this.data = name
      this.columnDefs = [
        {
          field: 'attribute',
          displayName: 'Attribute',
          saveKey: this.saveKey,
          width: '40%'
        },
        {
          field: 'value',
          displayName: 'Value',
          width: '40%'
        }
        ]
      this.enableColumnResize = true
      this.multiSelect = false
    }
    $scope.addressFields = []
    $scope.addressGrid = new SingleEntityGrid('addressFields')
    $scope.connectionFields = []
    $scope.connectionGrid = new SingleEntityGrid('connectionFields')
    $scope.routerFields = []
    $scope.routerGrid = new SingleEntityGrid('routerFields')
    $scope.linkFields = []
    $scope.linkGrid = new SingleEntityGrid('linkFields')

    var SingleEntityInfo = function (entityName) {
      return function (entity) {
        if (!entity)
          return
        $scope[entityName] = entity
        var currentEntity = getCurrentLinksEntity()
        if (currentEntity === entityName && entityModes[currentEntity] && entityModes[currentEntity].currentModeId === 'links') {
          updateModeLinks()
          scheduleNextUpdate()
          return
        }
        var filteredFields = []
        var fields = Object.keys(entity.data.fields)
        fields.forEach( function (field) {
          if (field != "title" && field != "uid")
            filteredFields.push({attribute: field, value: entity.data.fields[field]})
        })
        $scope[entityName.toLowerCase()+'Fields'] = filteredFields
        scheduleNextUpdate()
        loadColState($scope[entityName.toLowerCase()+'Grid']);
        //$timeout(() => $scope[entityName.toLowerCase()+'Fields'] = filteredFields)
      }
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
          var owningAddr = QDRService.valFor(response.attributeNames, result, "owningAddr")
          var id = $scope.Address.data.fields.uid
          return (owningAddr === $scope.Address.data.fields.uid)
              }
          },
          Connection: {
              currentModeId: savedModeIds.Connection,
              filter: function (response, result) {
          var connectionId = QDRService.valFor(response.attributeNames, result, "connectionId")
          return (connectionId === $scope.Connection.data.fields.identity)
              }
          }
      }
    $scope.selectMode = function (mode, entity) {
      if (!mode || !entity)
        return;
      entityModes[entity].currentModeId = mode.id;
      saveModeIds();
      if (mode.id === 'links') {
QDR.log.debug("setting linkFields to [] in selectMode")
        $scope.filteredLinkFields = [];
        getLinkPagedData($scope.linkPagingOptions.pageSize, $scope.linkPagingOptions.currentPage);
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
      $timeout(function () {
        QDR.log.debug("setting linkFields in updateEntityLinkGrid");
        $scope.filteredLinkFields = linkFields
        getLinkPagedData($scope.linkPagingOptions.pageSize, $scope.linkPagingOptions.currentPage);
      })
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

    $scope.showExpandCollapseTree = function () {
      QDR.log.info("showExpandCollapseTree returning " + !QDRService.isMSIE())
      return !QDRService.isMSIE()
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

    var logModuleCellTemplate = '<div ng-click="logInfoFor(row, col)" class="ngCellText" ng-class="col.colIndex()"><span ng-cell-text>{{COL_FIELD | pretty}}</span></div>'
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
      multiSelect: false,
      selectedItems: $scope.logModuleSelected,
      plugins: [new ngGridFlexibleHeightPlugin()],
      afterSelectionChange: function(data) {
        if (data.selected) {
            var selItem = $scope.logModuleSelected[0]
            var nodeId = selItem.nodeId

        }
      }
    }

    $scope.logInfoFor = function (row, col) {
      logDialog(row, col)
    }

    function logDialog(row, col) {
        var d = $dialog.dialog({
          backdrop: false,
          keyboard: true,
          backdropClick: false,
          templateUrl: 'viewLogs.html',
          controller: "QDR.OverviewLogsController",
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
        d.open().then(function(result) { console.log("d.open().then"); });
    };

    var numberTemplate = '<div class="ngCellText" ng-class="col.colIndex()"><span ng-cell-text>{{COL_FIELD | pretty}}</span></div>'
    $scope.allLogFields = []
    $scope.allLogSelections = [];
    $scope.allLogGrid = {
      saveKey: 'allLogGrid',
      data: 'allLogFields',
      columnDefs: [
        {
          field: 'name',
          saveKey: 'allLogGrid',
          displayName: 'Module'
        },
/*        {
          field: 'enable',
          displayName: 'Enable'
        },
*/

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
      multiSelect: false,
      selectedItems: $scope.allLogSelections,
      plugins: [new ngGridFlexibleHeightPlugin()],

      afterSelectionChange: function(data) {
        if (data.selected) {
            var selItem = $scope.allLogSelections[0]
            var nodeId = selItem.name
            // activate in the tree
            $("#overtree").dynatree("getTree").activateKey(nodeId);
        }
      }

    };

    var allLogEntries = {}
    var allLogInfo = function () {
        // update the count of entries for each module
        $scope.allLogFields = []
        var logResults = {}
        var logDetails = {}

        var gotLogStats = function (node, entity, response) {
          logDetails[node] = []
          response.results.forEach( function (result) {
            var oresult = QDRService.flatten(response.attributeNames, result)
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
          sortedModules.forEach( function (module) {
            $scope.allLogFields.push(logResults[module])
          })
          allLogEntries = logDetails
          updateLogTree($scope.allLogFields)
        }
        QDRService.fetchAllEntities({entity: 'logStats'}, gotAllLogStats, gotLogStats)
    }

    $scope.logFields = []
    // get info for a single log
    var logInfo = function (node) {

        var gotLogInfo = function (responses) {
          $timeout(function () {
            $scope.logModuleData = []
            $scope.logModule.module = node.data.key
            for (var n in responses) {
              var moduleIndex = responses[n]['log'].attributeNames.indexOf("module")
              var result = responses[n]['log'].results.filter( function (r) {
                return r[moduleIndex] === node.data.key
              })[0]
              var logInfo = QDRService.flatten(responses[n]['log'].attributeNames, result)
              var entry = allLogEntries[n]
              entry.forEach( function (module) {
                if (module.name === node.data.key) {
                  module.nodeName = QDRService.nameFromId(n)
                  module.nodeId = n
                  module.enable = logInfo.enable
                  $scope.logModuleData.push(module)
                }
              })
            }
            $scope.logModuleData.sort ( function (a,b) { return a.nodeName < b.nodeName? -1 : a.nodeName> b.nodeName? 1 : 0})
            scheduleNextUpdate()
          })
        }
        QDRService.fetchAllEntities({entity: 'log', attrs: ['module', 'enable']}, gotLogInfo)
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
      try {
        return angular.fromJson(localStorage[OVERVIEWEXPANDEDKEY]) || [];
      } catch (e) {
        QDR.log.debug("localStorage[OVERVIEWEXPANDEDKEY]=" + localStorage[OVERVIEWEXPANDEDKEY])
        return ["Routers"]
      }
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
      $scope.template = template[0];
    }
    // activated is called each time a tree node is clicked
    // based on which node is clicked, load the correct data grid template and start getting the data
    var activated = function (node) {
      QDR.log.debug("node activated: " + node.data.title)
      saveExpanded()
      saveActivated(node.data.key)

      setTemplate(node)
      // the nodes info function will fetch the grids data
      if (node.data.info) {
        $timeout(function () {
          if (node.data.key === node.data.parent) {
            node.data.info()
          }
          else {
            node.data.info(node)
          }
        })
      }
    }

    var treeNodeExpanded = function (node) {
      saveExpanded()
      tick()
    }
    $scope.template = {url: ''};

    if (!QDRService.connected) {
      QDRService.redirectWhenConnected("overview")
      return;
    }

    // we are currently connected. setup a handler to get notified if we are ever disconnected
    var onDisconnect = function () {
      $timeout(function () { QDRService.redirectWhenConnected("overview") })
    }
    QDRService.addDisconnectAction( onDisconnect )

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
      var tree = $("#overtree").dynatree("getTree")
      if (!tree.getNodeByKey) {
        return
      }
      var parentNode = tree.getNodeByKey(parentKey);
      parentNode.removeChildren();

      leaves.forEach( function (leaf) {
        parentNode.addChild(worker(leaf))
      })
      scrollTree.scrollTop(scrollTop)
      if (firstTime) {
        var newActive = tree.getActiveNode();
        if (newActive &&
//            newActive.data.key === lastKey &&
            newActive.data.key !== newActive.data.parent &&  // this is a leaf node
            newActive.data.parent === parentKey) {          // the active node was just created
          firstTime = false
QDR.log.debug("newly created node needs to be activated")
          activated(newActive)
        }
      }
      $('.dynatree-title').each( function (idx) {
        //QDR.log.info('found a title of ' + $(this).html())
        var unsafe = $(this).html()
        $(this).html(unsafe.replace(/</g, "&lt;").replace(/>/g, "&gt;"))
      })
   }

    // get saved tree state
    var lastKey = loadActivatedNode();
    var expandedNodeList = loadExpandedNodeList();
    var firstTime = true;

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
      $timeout(function () {updateLeaves(nodes, "Routers", routers, worker)})
    }

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
      var info = SingleEntityInfo('Address')
      var worker = function (address) {
        var a = new Folder(address.title)
        a.info = info
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
      $timeout(function () {updateLeaves(addressFields, "Addresses", addresses, worker)})
    }

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
      var info = SingleEntityInfo('Link')
      var worker = function (link) {
        var l = new Folder(link.title)
        var isConsole = QDRService.isConsoleLink(link)
        l.info = info
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
      $timeout(function () {updateLeaves(linkFields, "Links", links, worker)})
    }

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
      var info = SingleEntityInfo('Connection')
      //var info = ConnectionInfo
      var worker = function (connection) {
        var c = new Folder(connection.host)
        var isConsole = QDRService.isAConsole (connection.properties, connection.identity, connection.role, connection.routerId)
        c.type = "Connection"
        c.info = info
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
      $timeout(function () {updateLeaves(connectionFields, "Connections", connections, worker)})
    }

    var updateLogTree = function (logFields) {
      var worker = function (log) {
        var l = new Folder(log.name)
        l.type = "Log"
        l.info = logInfo
        l.key = log.name
        l.parent = "Logs"
        l.activate = lastKey === l.key
        l.addClass = "log"
        return l
      }
      $timeout(function () {updateLeaves(logFields, "Logs", logs, worker)})
    }

    var htmlReady = false;
    var dataReady = false;
    $scope.largeNetwork = QDRService.isLargeNetwork()
    var logs = new Folder("Logs")
    logs.type = "Logs"
    logs.info = allLogInfo
    logs.activate = lastKey === 'Logs'
    logs.expand = (expandedNodeList.indexOf("Logs") > -1)
    logs.clickFolderMode = 1
    logs.key = "Logs"
    logs.parent = "Logs"
    if (QDRService.versionCheck('0.8.0'))
      topLevelChildren.push(logs)
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
        onExpand: treeNodeExpanded,
        autoCollapse: $scope.largeNetwork,
        activeVisible: !$scope.largeNetwork,
        selectMode: 1,
        debugLevel: 0,
        children: topLevelChildren
      })
      treeRoot = $("#overtree").dynatree("getRoot");
      tick()
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

    var nodeIds = QDRService.nodeIdList()
    //updateRouterTree(nodeIds);
    // add placeholders for the top level tree nodes
    var topLevelTreeNodes = [routers, addresses, links, connections, logs]
    topLevelTreeNodes.forEach( function (parent) {
      var placeHolder = new Folder("loading...")
      placeHolder.addClass = "loading"
      parent.children = [placeHolder]
    })

    var singleQ = null
    var updateExpanded = function () {
      if (!treeRoot)
        return;
      if (treeRoot.visit) {
        treeRoot.visit(function(node){
          if (node.isActive())
            setTemplate(node)
          if (node.isActive() || node.isExpanded()) {
            if (node.data.key === node.data.parent) {
              node.data.info()
            }
            else {
              node.data.info(node)
            }
          }
        })
      }
    }

    var tickTimer;
    var scheduleNextUpdate = function () {
      clearTimeout(tickTimer)
      tickTimer = setTimeout(tick, refreshInterval)
    }
    var tick = function () {
      clearTimeout(tickTimer)
      $timeout( updateExpanded )
    }
    dataReady = true;
    initTreeAndGrid();
    $scope.$on("$destroy", function( event ) {
      clearTimeout(tickTimer)
      QDRService.delDisconnectAction( onDisconnect )
    });

  }]);

  return QDR;

}(QDR || {}));

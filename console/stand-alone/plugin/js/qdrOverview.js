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
import { QDRFolder, QDRLeaf, QDRCore, QDRLogger, QDRTemplatePath, QDRRedirectWhenConnected } from './qdrGlobals.js';

export class OverviewController {
  constructor(QDRService, $scope, $log, $location, $timeout, $uibModal) {
    this.controllerName = 'QDR.OverviewController';

    let QDRLog = new QDRLogger($log, 'OverviewController');
    QDRLog.debug('QDR.OverviewControll started with location of ' + $location.path() + ' and connection of  ' + QDRService.management.connection.is_connected());
    let updateIntervalHandle = undefined;
    const updateInterval = 5000;

    const COLUMNSTATEKEY = 'QDRColumnKey.';
    const OVERVIEWEXPANDEDKEY = 'QDROverviewExpanded';
    const OVERVIEWACTIVATEDKEY = 'QDROverviewActivated';
    const FILTERKEY = 'QDROverviewFilters';
    const OVERVIEWMODEIDS = 'QDROverviewModeIds';
    let treeRoot;   // the fancytree root node. initialized once log data is received

    // we want attributes to be listed first, so add it at index 0
    $scope.subLevelTabs = [{
      content: '<i class="icon-list"></i> Attributes',
      title: 'View the attribute values on your selection',
      isValid: function () { return true; },
      href: function () { return '#/attributes'; },
      index: 0
    },
    {
      content: '<i class="icon-leaf"></i> Operations',
      title: 'Execute operations on your selection',
      isValid: function () { return true; },
      href: function () { return '#/operations'; },
      index: 1
    }];
    $scope.activeTab = $scope.subLevelTabs[0];
    $scope.setActive = function (nav) {
      $scope.activeTab = nav;
    };
    $scope.isValid = function (nav) {
      return nav.isValid();
    };
    $scope.isActive = function (nav) {
      return nav == $scope.activeTab;
    };
    $scope.linkFields = [];
    $scope.link = null;
    $scope.modes = [
      { title: 'Overview', name: 'Overview', right: false }
    ];

    $scope.tmplOverviewTree = QDRTemplatePath + 'tmplOverviewTree.html';
    $scope.templates = [
      { name: 'Charts', url: 'overviewCharts.html' },
      { name: 'Routers', url: 'routers.html' },
      { name: 'Router', url: 'router.html' },
      { name: 'Addresses', url: 'addresses.html' },
      { name: 'Address', url: 'address.html' },
      { name: 'Links', url: 'links.html' },
      { name: 'Link', url: 'link.html' },
      { name: 'Connections', url: 'connections.html' },
      { name: 'Connection', url: 'connection.html' },
      { name: 'Logs', url: 'logs.html' },
      { name: 'Log', url: 'logModule.html' }
    ];
    let topLevelChildren = [];

    var selectRow = function (gridApi) {
      if (!gridApi.selection)
        return;
      gridApi.selection.on.rowSelectionChanged($scope, function (row) {
        let treeKey = row.grid.options.treeKey;
        if (treeKey && row.entity[treeKey]) {
          let key = row.entity[treeKey];
          $('#overtree').fancytree('getTree').activateKey(key);
        }
      });
    };
    $scope.routerPagingOptions = {
      pageSizes: [50, 100, 500],
      pageSize: 50,
      currentPage: 1
    };
    var getPagedData = function (pageSize, page) {
      $scope.totalRouters = $scope.allRouterFields.length;
      $scope.allRouters.showFooter = $scope.totalRouters > 50;
      $scope.pagedRouterFields = $scope.allRouterFields.slice((page - 1) * pageSize, page * pageSize);
    };
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
      enableHorizontalScrollbar: 0,
      enableVerticalScrollbar: 0,
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
      let nodes = {};
      // gets called each node/entity response
      var gotNode = function (nodeName, entity, response) {
        if (!nodes[nodeName])
          nodes[angular.copy(nodeName)] = {};
        nodes[nodeName][entity] = response;
      };
      // send the requests for all connection and router info for all routers
      QDRService.management.topology.fetchAllEntities([
        { entity: 'connection', attrs: ['role'] },
        { entity: 'router' }], function () {
          // we have all the data now in the nodes object
          let allRouterFields = [];
          for (let node in nodes) {
            let connections = 0;
            for (let i = 0; i < nodes[node]['connection'].results.length; ++i) {
              // we only requested "role" so it will be at [0]
              if (nodes[node]['connection'].results[i][0] === 'inter-router')
                ++connections;
            }
            let routerRow = { connections: connections, nodeId: node, id: QDRService.utilities.nameFromId(node) };
            nodes[node]['router'].attributeNames.forEach(function (routerAttr, i) {
              if (routerAttr !== 'routerId' && routerAttr !== 'id')
                routerRow[routerAttr] = nodes[node]['router'].results[0][i];
            });
            allRouterFields.push(routerRow);
          }
          $scope.allRouterFields = allRouterFields;
          expandGridToContent('Routers', $scope.allRouterFields.length);
          getPagedData($scope.routerPagingOptions.pageSize, $scope.routerPagingOptions.currentPage);
          updateRouterTree(nodeIds);
          callback(null);
        }, gotNode);
    };

    $scope.routerFields = [];
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
      enableHorizontalScrollbar: 0,
      enableVerticalScrollbar: 0,
      minRowsToShow: 25,
      multiSelect: false
    };

    $scope.router = null;
    // get info for a single router
    var routerInfo = function (node, callback) {
      $scope.router = node;

      let routerFields = [];
      $scope.allRouterFields.some(function (field) {
        if (field.id === node.title) {
          Object.keys(field).forEach(function (key) {
            let attr = (key === 'connections') ? 'External connections' : key;
            routerFields.push({ attribute: attr, value: field[key] });
          });
          return true;
        }
      });
      $scope.routerFields = routerFields;
      expandGridToContent('Router', $scope.routerFields.length);
      callback(null);
    };

    $scope.addressPagingOptions = {
      pageSizes: [50, 100, 500],
      pageSize: 50,
      currentPage: 1
    };
    var getAddressPagedData = function (pageSize, page) {
      $scope.totalAddresses = $scope.addressesData.length;
      $scope.addressesGrid.showFooter = $scope.totalAddresses > 50;
      $scope.pagedAddressesData = $scope.addressesData.slice((page - 1) * pageSize, page * pageSize);
    };
    $scope.$watch('addressPagingOptions', function (newVal, oldVal) {
      if (newVal !== oldVal && newVal.currentPage !== oldVal.currentPage) {
        getAddressPagedData($scope.addressPagingOptions.pageSize, $scope.addressPagingOptions.currentPage);
      }
    }, true);

    $scope.totalAddresses = 0;
    $scope.pagedAddressesData = [];
    $scope.addressesData = [];
    $scope.selectedAddresses = [];
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
      enableHorizontalScrollbar: 0,
      enableVerticalScrollbar: 0,
      multiSelect: false,
      enableSelectAll: false,
      onRegisterApi: selectRow,
      enableSelectionBatchEvent: false,
      enableRowHeaderSelection: false,
      noUnselect: true
    };

    // get info for all addresses
    var allAddressInfo = function (address, callback) {
      let nodes = {};
      // gets called each node/entity response
      var gotNode = function (nodeName, entity, response) {
        if (!nodes[nodeName])
          nodes[nodeName] = {};
        nodes[nodeName][entity] = angular.copy(response);
      };
      var addr_phase = function (addr) {
        if (!addr)
          return '-';
        if (addr[0] == 'M')
          return addr[1];
        return '';
      };
      var prettyVal = function (val) {
        return QDRService.utilities.pretty(val || '-');
      };
      let addressFields = [];
      let addressObjs = {};
      // send the requests for all connection and router info for all routers
      QDRService.management.topology.fetchAllEntities({ entity: 'router.address' }, function () {
        for (let node in nodes) {
          let response = nodes[node]['router.address'];
          response.results.forEach(function (result) {
            let address = QDRService.utilities.flatten(response.attributeNames, result);

            var addNull = function (oldVal, newVal) {
              if (oldVal != null && newVal != null)
                return oldVal + newVal;
              if (oldVal != null)
                return oldVal;
              return newVal;
            };

            let uid = address.identity;
            let identity = QDRService.utilities.identity_clean(uid);

            if (!addressObjs[QDRService.utilities.addr_text(identity) + QDRService.utilities.addr_class(identity)])
              addressObjs[QDRService.utilities.addr_text(identity) + QDRService.utilities.addr_class(identity)] = {
                address: QDRService.utilities.addr_text(identity),
                'class': QDRService.utilities.addr_class(identity),
                phase: addr_phase(identity),
                inproc: address.inProcess,
                local: address.subscriberCount,
                remote: address.remoteCount,
                'in': address.deliveriesIngress,
                out: address.deliveriesEgress,
                thru: address.deliveriesTransit,
                toproc: address.deliveriesToContainer,
                fromproc: address.deliveriesFromContainer,
                uid: uid
              };
            else {
              let sumObj = addressObjs[QDRService.utilities.addr_text(identity) + QDRService.utilities.addr_class(identity)];
              sumObj.inproc = addNull(sumObj.inproc, address.inProcess);
              sumObj.local = addNull(sumObj.local, address.subscriberCount);
              sumObj.remote = addNull(sumObj.remote, address.remoteCount);
              sumObj['in'] = addNull(sumObj['in'], address.deliveriesIngress);
              sumObj.out = addNull(sumObj.out, address.deliveriesEgress);
              sumObj.thru = addNull(sumObj.thru, address.deliveriesTransit);
              sumObj.toproc = addNull(sumObj.toproc, address.deliveriesToContainer);
              sumObj.fromproc = addNull(sumObj.fromproc, address.deliveriesFromContainer);
            }
          });
        }
        for (let obj in addressObjs) {
          addressObjs[obj].inproc = prettyVal(addressObjs[obj].inproc);
          addressObjs[obj].local = prettyVal(addressObjs[obj].local);
          addressObjs[obj].remote = prettyVal(addressObjs[obj].remote);
          addressObjs[obj]['in'] = prettyVal(addressObjs[obj]['in']);
          addressObjs[obj].out = prettyVal(addressObjs[obj].out);
          addressObjs[obj].thru = prettyVal(addressObjs[obj].thru);
          addressObjs[obj].toproc = prettyVal(addressObjs[obj].toproc);
          addressObjs[obj].fromproc = prettyVal(addressObjs[obj].fromproc);
          addressFields.push(addressObjs[obj]);
        }
        if (addressFields.length === 0)
          return;
        // update the grid's data
        addressFields.sort(function (a, b) {
          return a.address + a['class'] < b.address + b['class'] ? -1 : a.address + a['class'] > b.address + b['class'] ? 1 : 0;
        }
        );
        addressFields[0].title = addressFields[0].address;
        for (let i = 1; i < addressFields.length; ++i) {
          // if this address is the same as the previous address, add a class to the display titles
          if (addressFields[i].address === addressFields[i - 1].address) {
            addressFields[i - 1].title = addressFields[i - 1].address + ' (' + addressFields[i - 1]['class'] + ')';
            addressFields[i].title = addressFields[i].address + ' (' + addressFields[i]['class'] + ')';
          } else
            addressFields[i].title = addressFields[i].address;
        }
        $scope.addressesData = addressFields;
        expandGridToContent('Addresses', $scope.addressesData.length);
        getAddressPagedData($scope.addressPagingOptions.pageSize, $scope.addressPagingOptions.currentPage);
        // repopulate the tree's child nodes
        updateAddressTree(addressFields);
        callback(null);
      }, gotNode);
    };

    var updateLinkGrid = function (linkFields) {
      // apply the filter
      let filteredLinks = linkFields.filter(function (link) {
        let include = true;
        if ($scope.filter.endpointsOnly === 'true') {
          if (link.linkType !== 'endpoint')
            include = false;
        }
        if ($scope.filter.hideConsoles) {
          if (QDRService.utilities.isConsole(QDRService.management.topology.getConnForLink(link)))
            include = false;
        }
        return include;
      });
      QDRLog.info('setting linkFields in updateLinkGrid');
      $scope.linkFields = filteredLinks;
      expandGridToContent('Links', $scope.linkFields.length);
      getLinkPagedData($scope.linkPagingOptions.pageSize, $scope.linkPagingOptions.currentPage);
      // if we have a selected link
      if ($scope.link) {
        // find the selected link in the array of all links
        let links = $scope.linkFields.filter(function (link) {
          return link.name === $scope.link.data.fields.name;
        });
        if (links.length > 0) {
          // linkInfo() is the function that is called by fancytree when a link is selected
          // It is passed a fancytree node. We need to simulate that node type to update the link grid
          linkInfo({ data: { title: links[0].title, fields: links[0] } }, function () { $timeout(function () { }); });
        }
      }
    };

    // get info for a all links
    $scope.linkPagingOptions = {
      pageSizes: [50, 100, 500],
      pageSize: 50,
      currentPage: 1
    };
    var getLinkPagedData = function (pageSize, page) {
      $scope.totalLinks = $scope.linkFields.length;
      $scope.linksGrid.showFooter = $scope.totalLinks > 50;
      $scope.pagedLinkData = $scope.linkFields.slice((page - 1) * pageSize, page * pageSize);
    };
    $scope.$watch('linkPagingOptions', function (newVal, oldVal) {
      if (newVal !== oldVal && newVal.currentPage !== oldVal.currentPage) {
        getLinkPagedData($scope.linkPagingOptions.pageSize, $scope.linkPagingOptions.currentPage);
      }
    }, true);

    $scope.totalLinks = 0;
    $scope.pagedLinkData = [];
    $scope.selectedLinks = [];

    $scope.linksGrid = {
      saveKey: 'linksGrid',
      treeKey: 'uid',
      data: 'pagedLinkData',
      columnDefs: [
        {
          field: 'link',
          displayName: 'Link',
          groupable: false,
          saveKey: 'linksGrid',
          width: '12%'
        },
        {
          field: 'linkType',
          displayName: 'Link type',
          groupable: false,
          width: '8%'
        },
        {
          field: 'linkDir',
          displayName: 'Link dir',
          groupable: false,
          width: '7%'
        },
        {
          field: 'adminStatus',
          displayName: 'Admin status',
          groupable: false,
          width: '8%'
        },
        {
          field: 'operStatus',
          displayName: 'Oper status',
          groupable: false,
          width: '8%'
        },
        {
          field: 'deliveryCount',
          displayName: 'Delivery Count',
          groupable: false,
          cellClass: 'grid-align-value',
          width: '10%'
        },
        {
          field: 'rate',
          displayName: 'Rate',
          groupable: false,
          cellClass: 'grid-align-value',
          width: '8%'
        },
        {
          field: 'deliveriesDelayed10Sec',
          displayName: 'Delayed 10 sec',
          groupable: false,
          cellClass: 'grid-align-value',
          width: '8%'
        },
        {
          field: 'deliveriesDelayed1Sec',
          displayName: 'Delayed 1 sec',
          groupable: false,
          cellClass: 'grid-align-value',
          width: '8%'
        },
        {
          field: 'uncounts',
          displayName: 'Outstanding',
          groupable: false,
          cellClass: 'grid-align-value',
          width: '8%'
        },
        {
          field: 'owningAddr',
          displayName: 'Address',
          groupable: false,
          width: '15%'
        }
        /*,
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
      enableHorizontalScrollbar: 0,
      enableVerticalScrollbar: 0,
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
      let saveInfo = columns.map(function (col) {
        return [col.width, col.visible];
      });
      let saveKey = columns[0].colDef.saveKey;
      if (saveKey)
        localStorage.setItem(COLUMNSTATEKEY + saveKey, JSON.stringify(saveInfo));
    });

    var allLinkInfo = function (node, callback) {
      var gridCallback = function (linkFields) {
        QDRService.management.topology.ensureAllEntities({ entity: 'connection', force: true }, function () {
          // only update the grid with these fields if the List tree node is selected
          // this is becuase we are using the link grid in other places and we don't want to overwrite it
          if ($scope.template.name === 'Links')
            updateLinkGrid(linkFields);
          updateLinkTree(linkFields);
          callback(null);
        });
      };
      getAllLinkFields([gridCallback]);
    };

    var getAllLinkFields = function (completionCallbacks, selectionCallback) {
      if (!$scope.linkFields) {
        QDRLog.debug('$scope.linkFields was not defined!');
        return;
      }
      let nodeIds = QDRService.management.topology.nodeIdList();
      let linkFields = [];
      let now = Date.now();
      var rate = function (linkName, response, result) {
        if (!$scope.linkFields)
          return 0;
        let oldname = $scope.linkFields.filter(function (link) {
          return link.link === linkName;
        });
        if (oldname.length === 1) {
          let elapsed = (now - oldname[0].timestamp) / 1000;
          if (elapsed < 0)
            return 0;
          let delivered = QDRService.utilities.valFor(response.attributeNames, result, 'deliveryCount') - oldname[0].rawDeliveryCount;
          //QDRLog.debug("elapsed " + elapsed + " delivered " + delivered)
          return elapsed > 0 ? parseFloat(Math.round((delivered / elapsed) * 100) / 100).toFixed(2) : 0;
        } else {
          //QDRLog.debug("unable to find old linkName")
          return 0;
        }
      };
      let expected = nodeIds.length;
      let received = 0;
      var gotLinkInfo = function (nodeName, entity, response) {
        if (response.results)
          response.results.forEach(function (result) {
            var prettyVal = function (field) {
              let fieldIndex = response.attributeNames.indexOf(field);
              if (fieldIndex < 0) {
                return '-';
              }
              let val = result[fieldIndex];
              return QDRService.utilities.pretty(val);
            };
            var uncounts = function () {
              let und = QDRService.utilities.valFor(response.attributeNames, result, 'undeliveredCount');
              let uns = QDRService.utilities.valFor(response.attributeNames, result, 'unsettledCount');
              return QDRService.utilities.pretty(und + uns);
            };
            var getLinkName = function () {
              let namestr = QDRService.utilities.nameFromId(nodeName);
              return namestr + ':' + QDRService.utilities.valFor(response.attributeNames, result, 'identity');
            };
            var fixAddress = function () {
              let addresses = [];
              let owningAddr = QDRService.utilities.valFor(response.attributeNames, result, 'owningAddr') || '';
              let rawAddress = owningAddr;
              /*
                 - "L*"  =>  "* (local)"
                 - "M0*" =>  "* (direct)"
                 - "M1*" =>  "* (dequeue)"
                 - "MX*" =>  "* (phase X)"
            */
              let address = undefined;
              let starts = { 'L': '(local)', 'M0': '(direct)', 'M1': '(dequeue)' };
              for (let start in starts) {
                if (owningAddr.startsWith(start)) {
                  let ends = owningAddr.substr(start.length);
                  address = ends + ' ' + starts[start];
                  rawAddress = ends;
                  break;
                }
              }
              if (!address) {
                // check for MX*
                if (owningAddr.length > 3) {
                  if (owningAddr[0] === 'M') {
                    let phase = parseInt(owningAddr.substr(1));
                    if (phase && !isNaN(phase)) {
                      let phaseStr = phase + '';
                      let star = owningAddr.substr(2 + phaseStr.length);
                      address = star + ' ' + '(phase ' + phaseStr + ')';
                    }
                  }
                }
              }
              addresses[0] = address || owningAddr;
              addresses[1] = rawAddress;
              return addresses;
            };
            if (!selectionCallback || selectionCallback(response, result)) {
              let adminStatus = QDRService.utilities.valFor(response.attributeNames, result, 'adminStatus');
              let operStatus = QDRService.utilities.valFor(response.attributeNames, result, 'operStatus');
              let linkName = getLinkName();
              let linkType = QDRService.utilities.valFor(response.attributeNames, result, 'linkType');
              let addresses = fixAddress();
              let link = QDRService.utilities.flatten(response.attributeNames, result);

              // rate: QDRService.utilities.pretty(rate(linkName, response, result)),
              linkFields.push({
                link: linkName,
                title: linkName,
                uncounts: uncounts(),
                operStatus: operStatus,
                adminStatus: adminStatus,
                owningAddr: addresses[0],

                acceptedCount: prettyVal('acceptedCount'),
                modifiedCount: prettyVal('modifiedCount'),
                presettledCount: prettyVal('presettledCount'),
                rejectedCount: prettyVal('rejectedCount'),
                releasedCount: prettyVal('releasedCount'),
                deliveryCount: prettyVal('deliveryCount') + ' ',

                rate: prettyVal('settleRate'),
                deliveriesDelayed10Sec: prettyVal('deliveriesDelayed10Sec'),
                deliveriesDelayed1Sec: prettyVal('deliveriesDelayed1Sec'),
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

                uid: linkName,
                timestamp: now,
                nodeId: nodeName,
                identity: link.identity,
              });
            }
          });
        if (expected === ++received) {
          linkFields.sort(function (a, b) { return a.link < b.link ? -1 : a.link > b.link ? 1 : 0; });
          completionCallbacks.forEach(function (cb) {
            cb(linkFields);
          });
        }
      };
      nodeIds.forEach(function (nodeId) {
        QDRService.management.topology.fetchEntity(nodeId, 'router.link', [], gotLinkInfo);
      });
    };

    $scope.connectionPagingOptions = {
      pageSizes: [50, 100, 500],
      pageSize: 50,
      currentPage: 1
    };
    var getConnectionPagedData = function (pageSize, page) {
      $scope.totalConnections = $scope.allConnectionFields.length;
      $scope.allConnectionGrid.showFooter = $scope.totalConnections > 50;
      $scope.pagedConnectionsData = $scope.allConnectionFields.slice((page - 1) * pageSize, page * pageSize);
    };
    $scope.$watch('connectionPagingOptions', function (newVal, oldVal) {
      if (newVal !== oldVal && newVal.currentPage !== oldVal.currentPage) {
        getConnectionPagedData($scope.connectionPagingOptions.pageSize, $scope.connectionPagingOptions.currentPage);
      }
    }, true);

    $scope.totalConnections = 0;
    $scope.pagedConnectionsData = [];
    $scope.allConnectionFields = [];
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
      enableHorizontalScrollbar: 0,
      enableVerticalScrollbar: 0,
      multiSelect: false,
      enableSelectAll: false,
      onRegisterApi: selectRow,
      enableSelectionBatchEvent: false,
      enableRowHeaderSelection: false,
      noUnselect: true
    };
    // get info for a all connections
    var allConnectionInfo = function (connection, callback) {
      getAllConnectionFields([updateConnectionGrid, updateConnectionTree, function () { callback(null); }]);
    };
    // called after conection data is available
    var updateConnectionGrid = function (connectionFields) {
      $scope.allConnectionFields = connectionFields;
      expandGridToContent('Connections', $scope.allConnectionFields.length);
      getConnectionPagedData($scope.connectionPagingOptions.pageSize, $scope.connectionPagingOptions.currentPage);
    };

    // get the connection data for all nodes
    // called periodically
    // creates a connectionFields array and calls the callbacks (updateTree and updateGrid)
    var getAllConnectionFields = function (callbacks) {
      let nodeIds = QDRService.management.topology.nodeIdList();
      let connectionFields = [];
      let expected = nodeIds.length;
      let received = 0;
      let gotConnectionInfo = function (nodeName, entity, response) {
        response.results.forEach(function (result) {

          let auth = 'no_auth';
          let connection = QDRService.utilities.flatten(response.attributeNames, result);
          let sasl = connection.sasl;
          if (connection.isAuthenticated) {
            auth = sasl;
            if (sasl === 'ANONYMOUS')
              auth = 'anonymous-user';
            else {
              if (sasl === 'GSSAPI')
                sasl = 'Kerberos';
              if (sasl === 'EXTERNAL')
                sasl = 'x.509';
              auth = connection.user + '(' + connection.sslCipher + ')';
            }
          }

          let sec = 'no-security';
          if (connection.isEncrypted) {
            if (sasl === 'GSSAPI')
              sec = 'Kerberos';
            else
              sec = connection.sslProto + '(' + connection.sslCipher + ')';
          }

          let host = connection.host;
          let connField = {
            host: host,
            security: sec,
            authentication: auth,
            routerId: nodeName,
            uid: host + connection.container + connection.identity
          };
          response.attributeNames.forEach(function (attribute, i) {
            connField[attribute] = result[i];
          });
          connectionFields.push(connField);
        });
        if (expected === ++received) {
          connectionFields.sort(function (a, b) {
            return a.host + a.container + a.identity < b.host + b.container + b.identity ?
              -1 : a.host + a.container + a.identity > b.host + b.container + b.identity ? 1 : 0;
          });
          callbacks.forEach(function (cb) {
            cb(connectionFields);
          });
        }
      };
      nodeIds.forEach(function (nodeId) {
        QDRService.management.topology.fetchEntity(nodeId, 'connection', [], gotConnectionInfo);
      });
    };

    $scope.addressFields = [];
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
      enableHorizontalScrollbar: 0,
      enableVerticalScrollbar: 0,
      minRowsToShow: 11,
      jqueryUIDraggable: true,
      multiSelect: false
    };

    // get info for a single address
    var addressInfo = function (address, callback) {
      $scope.address = address;
      let currentEntity = getCurrentLinksEntity();
      // we are viewing the addressLinks page
      if (currentEntity === 'Address' && entityModes[currentEntity].currentModeId === 'links') {
        updateModeLinks();
        callback(null);
        return;
      }

      $scope.addressFields = [];
      let fields = Object.keys(address.data.fields);
      fields.forEach(function (field) {
        if (field != 'title' && field != 'uid')
          $scope.addressFields.push({ attribute: field, value: address.data.fields[field] });
      });
      expandGridToContent('Address', $scope.addressFields.length);
      callback(null);
    };

    $scope.singleLinkFields = [];
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
      enableHorizontalScrollbar: 0,
      enableVerticalScrollbar: 0,
      minRowsToShow: 24,
      jqueryUIDraggable: true,
      multiSelect: false
    };

    // display the grid detail info for a single link
    var linkInfo = function (link, callback) {
      if (!link) {
        callback(null);
        return;
      }
      $scope.link = link;

      $scope.singleLinkFields = [];
      let fields = Object.keys(link.data.fields);
      let excludeFields = ['title', 'uid', 'uncounts', 'rawDeliveryCount', 'timestamp', 'rawAddress'];
      fields.forEach(function (field) {
        if (excludeFields.indexOf(field) == -1)
          $scope.singleLinkFields.push({ attribute: field, value: link.data.fields[field] });
      });
      expandGridToContent('Link', $scope.singleLinkFields.length);
      callback(null);
    };

    // get info for a single connection
    $scope.gridModes = [{
      content: '<a><i class="icon-list"></i> Attributes</a>',
      id: 'attributes',
      title: 'View attributes'
    },
    {
      content: '<a><i class="icon-link"></i> Links</a>',
      id: 'links',
      title: 'Show links'
    }
    ];
    var saveModeIds = function () {
      let modeIds = { Address: entityModes.Address.currentModeId, Connection: entityModes.Connection.currentModeId };
      localStorage[OVERVIEWMODEIDS] = JSON.stringify(modeIds);
    };
    var loadModeIds = function () {
      return angular.fromJson(localStorage[OVERVIEWMODEIDS]) ||
        { Address: 'attributes', Connection: 'attributes' };
    };
    var savedModeIds = loadModeIds();
    let entityModes = {
      Address: {
        currentModeId: savedModeIds.Address,
        filter: function (response, result) {
          let owningAddr = QDRService.utilities.valFor(response.attributeNames, result, 'owningAddr');
          return (owningAddr === $scope.address.data.fields.uid);
        }
      },
      Connection: {
        currentModeId: savedModeIds.Connection,
        filter: function (response, result) {
          let connectionId = QDRService.utilities.valFor(response.attributeNames, result, 'connectionId');
          return (connectionId === $scope.connection.data.fields.identity);
        }
      }
    };
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
      updateExpanded();
    };
    $scope.isModeSelected = function (mode, entity) {
      return mode.id === entityModes[entity].currentModeId;
    };
    $scope.isModeVisible = function (entity, id) {
      return entityModes[entity].currentModeId === id;
    };

    var updateEntityLinkGrid = function (linkFields) {
      $scope.linkFields = linkFields;
      expandGridToContent('Link', $scope.linkFields.length);
      getLinkPagedData($scope.linkPagingOptions.pageSize, $scope.linkPagingOptions.currentPage);
    };
    // based on which entity is selected, get and filter the links
    var updateModeLinks = function () {
      let currentEntity = getCurrentLinksEntity();
      if (!currentEntity)
        return;
      getAllLinkFields([updateEntityLinkGrid], entityModes[currentEntity].filter);
    };
    var getCurrentLinksEntity = function () {
      let currentEntity;
      let active = $('#overtree').fancytree('getActiveNode');
      if (active) {
        currentEntity = active.type ? active.type : active.data.fields.type;
      }
      return currentEntity;
    };

    $scope.quiesceLinkClass = function (row) {
      let stateClassMap = {
        enabled: 'btn-primary',
        disabled: 'btn-danger'
      };
      return stateClassMap[row.entity.adminStatus];
    };

    $scope.quiesceLink = function (row, $event) {
      $event.stopPropagation();
      QDRService.management.topology.quiesceLink(row.entity.nodeId, row.entity.name)
        .then(function (results, context) {
          let statusCode = context.message.application_properties.statusCode;
          if (statusCode < 200 || statusCode >= 300) {
            QDRCore.notification('error', context.message.statusDescription);
            QDRLog.error('Error ' + context.message.statusDescription);
          }
        });
    };

    $scope.quiesceLinkDisabled = function (row) {
      return (row.entity.operStatus !== 'up' && row.entity.operStatus !== 'down');
    };
    $scope.quiesceLinkText = function (row) {
      return row.entity.adminStatus === 'disabled' ? 'Revive' : 'Quiesce';
    };

    $scope.expandAll = function () {
      $('#overtree').fancytree('getRoot').visit(function (node) {
        node.expand(true);
      });
    };
    $scope.contractAll = function () {
      $('#overtree').fancytree('getRoot').visit(function (node) {
        node.expand(false);
      });
    };
    $scope.connectionFields = [];
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
      enableHorizontalScrollbar: 0,
      enableVerticalScrollbar: 0,
      minRowsToShow: 21,
      jqueryUIDraggable: true,
      multiSelect: false
    };

    var connectionInfo = function (connection, callback) {
      if (!connection) {
        callback(null);
        return;
      }
      $scope.connection = connection;

      let currentEntity = getCurrentLinksEntity();
      // we are viewing the connectionLinks page
      if (currentEntity === 'Connection' && entityModes[currentEntity].currentModeId === 'links') {
        updateModeLinks();
        callback(null);
        return;
      }

      $scope.connectionFields = [];
      for (let field in connection.data.fields) {
        if (field != 'title' && field != 'uid')
          $scope.connectionFields.push({ attribute: field, value: connection.data.fields[field] });
      }
      expandGridToContent('Connection', $scope.connectionFields.length);
      callback(null);
    };


    const logModuleCellTemplate = '<div class="ui-grid-cell-contents" ng-click="grid.appScope.logInfoFor(row, col)">{{COL_FIELD CUSTOM_FILTERS | pretty}}</div>';

    $scope.logModule = {};
    $scope.logModuleSelected = [];
    $scope.logModuleData = [];
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
      enableHorizontalScrollbar: 0,
      enableVerticalScrollbar: 0,
      jqueryUIDraggable: true,
      multiSelect: false,
      enableSelectAll: false,
      onRegisterApi: selectRow,
      enableSelectionBatchEvent: false,
      enableRowHeaderSelection: false,
      minRowsToShow: Math.min(QDRService.management.topology.nodeIdList().length, 50),
      noUnselect: true
    };

    $scope.logInfoFor = function (row, col) {
      logDialog(row, col);
    };

    function logDialog(row, col) {
      let d = $uibModal.open({
        animation: true,
        templateUrl: 'viewLogs.html',
        controller: 'QDR.OverviewLogsController',
        resolve: {
          nodeName: function () {
            return row.entity.nodeName;
          },
          module: function () {
            return row.entity.name;
          },
          level: function () {
            return col.displayName;
          },
          nodeId: function () {
            return row.entity.nodeId;
          },
        }
      });
      d.result.then(function () {
        QDRLog.debug('d.open().then');
      }, function () {
        QDRLog.debug('Modal dismissed at: ' + new Date());
      });
    }

    const numberTemplate = '<div class="ngCellText" ng-class="col.colIndex()"><span ng-cell-text>{{COL_FIELD | pretty}}</span></div>';
    $scope.allLogFields = [];
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
      enableHorizontalScrollbar: 0,
      enableVerticalScrollbar: 0,
      minRowsToShow: 17,
      multiSelect: false,
      enableSelectAll: false,
      onRegisterApi: selectRow,
      enableSelectionBatchEvent: false,
      enableRowHeaderSelection: false,
      noUnselect: true
    };

    let allLogEntries = {};
    var allLogInfo = function (log, callback) {
      // update the count of entries for each module
      $scope.allLogFields = [];
      let logResults = {};
      let logDetails = {};

      var gotLogStats = function (node, entity, response) {
        logDetails[node] = [];
        response.results.forEach(function (result) {
          let oresult = QDRService.utilities.flatten(response.attributeNames, result);
          // make a copy for the details grid since logResults has the same object reference
          logDetails[node].push(angular.copy(oresult));
          if (!(oresult.name in logResults)) {
            logResults[oresult.name] = oresult;
          }
          else {
            response.attributeNames.forEach(function (attr, i) {
              if (attr.substr(attr.length - 5) === 'Count') {
                logResults[oresult.name][attr] += result[i];
              }
            });
          }
        });
      };
      var gotAllLogStats = function () {
        let sortedModules = Object.keys(logResults);
        sortedModules.sort(function (a, b) { return a < b ? -1 : a > b ? 1 : 0; });
        $scope.allLogFields = [];
        sortedModules.forEach(function (module) {
          $scope.allLogFields.push(logResults[module]);
        });
        expandGridToContent('Logs', $scope.allLogFields.length);
        allLogEntries = logDetails;
        updateLogTree($scope.allLogFields);
        callback(null);
      };
      QDRService.management.topology.fetchAllEntities({ entity: 'logStats' }, gotAllLogStats, gotLogStats);
    };

    var expandGridToContent = function (type, rows) {
      let tree = $('#overtree').fancytree('getTree');
      let node = null;
      if (tree && tree.getActiveNode)
        node = tree.getActiveNode();
      if (node) {
        if (node.type === type || node.data.type === type) {
          let height = (rows + 1) * 30 + 46; // header is 40px
          let gridDetails = $('#overview-controller .grid');
          gridDetails.css('height', height + 'px');
        }
      }
    };

    $scope.logFields = [];
    // get info for a single log
    var logInfo = function (node, callback) {

      var gotLogInfo = function (responses) {
        $scope.logModuleData = [];
        $scope.logModule.module = node.key;
        for (let n in responses) {
          let moduleIndex = responses[n]['log'].attributeNames.indexOf('module');
          let result = responses[n]['log'].results.filter(function (r) {
            return r[moduleIndex] === node.key;
          })[0];
          let logInfo = QDRService.utilities.flatten(responses[n]['log'].attributeNames, result);
          let entry = allLogEntries[n];
          entry.forEach(function (module) {
            if (module.name === node.key) {
              module.nodeName = QDRService.utilities.nameFromId(n);
              module.nodeId = n;
              module.enable = logInfo.enable;
              $scope.logModuleData.push(module);
            }
          });
        }
        $scope.logModuleData.sort(function (a, b) { return a.nodeName < b.nodeName ? -1 : a.nodeName > b.nodeName ? 1 : 0; });
        expandGridToContent('Log', $scope.logModuleData.length);
        callback(null);
      };
      QDRService.management.topology.fetchAllEntities({ entity: 'log', attrs: ['module', 'enable'] }, gotLogInfo);
    };

    var getExpandedList = function () {
      if (!treeRoot)
        return;
      let list = [];
      if (treeRoot.visit) {
        treeRoot.visit(function (node) {
          if (node.isExpanded()) {
            list.push(node.data.parentKey);
          }
        });
      }
      return list;
    };

    // loads the tree node name that was last selected
    var loadActivatedNode = function () {
      return localStorage[OVERVIEWACTIVATEDKEY] || 'Routers';
    };
    // saved the tree node name that is currently selected
    var saveActivated = function (key) {
      localStorage[OVERVIEWACTIVATEDKEY] = key;
    };
    // loads list that was saved
    var loadExpandedNodeList = function () {
      return angular.fromJson(localStorage[OVERVIEWEXPANDEDKEY]) || [];
    };
    // called when a node is expanded
    // here we save the expanded node so it can be restored when the page reloads
    var saveExpanded = function () {
      let list = getExpandedList();
      localStorage[OVERVIEWEXPANDEDKEY] = JSON.stringify(list);
      expandedNodeList = list;
    };

    var setTemplate = function (node) {
      let type = node.type;
      let template = $scope.templates.filter(function (tpl) {
        return tpl.name == type;
      });
      $timeout(function () {
        $scope.template = template[0];
      });
    };
    // activated is called each time a tree node is clicked
    // based on which node is clicked, load the correct data grid template and start getting the data
    var onTreeNodeActivated = function (event, data) {
      saveExpanded();
      saveActivated(data.node.key);
      $scope.ActivatedKey = data.node.key;
      setTemplate(data.node);
      updateExpanded();
    };

    var onTreeNodeExpanded = function () {
      saveExpanded();
      updateExpanded();
    };
    var onTreeNodeCollapsed = function () {
      saveExpanded();
    };

    if (!QDRService.management.connection.is_connected()) {
      QDRRedirectWhenConnected($location, 'overview');
      return;
    }
    $scope.template = $scope.templates[0];

    /* --------------------------------------------------
     *
     * setup the tree on the left
     *
     * -------------------------------------------------
     */
    var getActiveChild = function (node) {
      let active = node.children.filter(function (child) {
        return child.isActive();
      });
      if (active.length > 0)
        return active[0].key;
      return null;
    };
    // utility function called by each top level tree node when it needs to populate its child nodes
    var updateLeaves = function (leaves, entity, worker) {
      let tree = $('#overtree').fancytree('getTree'), node;
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
      let children = [];
      leaves.forEach(function (leaf) {
        children.push(worker(leaf));
      });
      node.addNode(children);
      // top level node was expanded
      if (wasExpanded)
        node.setExpanded(true, { noAnimation: true, noEvents: true });
      if (wasActive) {
        node.setActive(true, { noAnimation: true, noEvents: true });
      } else {
        // re-active the previously active child node
        if (activeChildKey) {
          let newNode = tree.getNodeByKey(activeChildKey);
          // the node may not be there after the update
          if (newNode)
            newNode.setActive(true, { noAnimation: true, noEvents: true }); // fires the onTreeNodeActivated event for this node
        }
      }
      resizer();
    };

    var resizer = function () {
      // this forces the tree and the grid to be the size of the browser window.
      // the effect is that the tree and the grid will have vertical scroll bars if needed.
      // the alternative is to let the tree and grid determine the size of the page and have
      // the scroll bar on the window
      //let viewport = $('#overview-controller .pane-viewport');
      //viewport.height( window.innerHeight - viewport.offset().top);

      // don't allow HTML in the tree titles
      $('.fancytree-title').each(function () {
        let unsafe = $(this).html();
        $(this).html(unsafe.replace(/</g, '&lt;').replace(/>/g, '&gt;'));
      });

      // remove the comments to allow the tree to take all the height it needs

      let gridDetails = $('#overview-controller .grid');
      if (gridDetails.offset())
        gridDetails.height(window.innerHeight - gridDetails.offset().top);

      let gridViewport = $('#overview-controller .ui-grid-viewport');
      if (gridViewport.offset())
        gridViewport.height(window.innerHeight - gridViewport.offset().top);
      //gridViewport.

    };
    $(window).resize(resizer);

    // get saved tree state
    $scope.ActivatedKey = loadActivatedNode();
    let expandedNodeList = loadExpandedNodeList();

    var showCharts = function () {

    };
    let charts = new QDRFolder('Charts');
    charts.info = { fn: showCharts };
    charts.type = 'Charts';  // for the charts template
    charts.key = 'Charts';
    charts.extraClasses = 'charts';
    topLevelChildren.push(charts);

    // create a routers tree branch
    let routers = new QDRFolder('Routers');
    routers.type = 'Routers';
    routers.info = { fn: allRouterInfo };
    routers.expanded = (expandedNodeList.indexOf('Routers') > -1);
    routers.key = 'Routers';
    routers.parentKey = 'Routers';
    routers.extraClasses = 'routers';
    topLevelChildren.push(routers);
    // called when the list of routers changes
    var updateRouterTree = function (nodes) {
      var worker = function (node) {
        let name = QDRService.utilities.nameFromId(node);
        let router = new QDRLeaf(name);
        router.type = 'Router';
        router.info = { fn: routerInfo };
        router.nodeId = node;
        router.key = node;
        router.extraClasses = 'router';
        router.parentKey = 'Routers';
        return router;
      };
      updateLeaves(nodes, 'Routers', worker);
    };

    // create an addresses tree branch
    let addresses = new QDRFolder('Addresses');
    addresses.type = 'Addresses';
    addresses.info = { fn: allAddressInfo };
    addresses.expanded = (expandedNodeList.indexOf('Addresses') > -1);
    addresses.key = 'Addresses';
    addresses.parentKey = 'Addresses';
    addresses.extraClasses = 'addresses';
    topLevelChildren.push(addresses);
    var updateAddressTree = function (addressFields) {
      var worker = function (address) {
        let a = new QDRLeaf(address.title);
        a.info = { fn: addressInfo };
        a.key = address.uid;
        a.fields = address;
        a.type = 'Address';
        a.tooltip = address['class'] + ' address';
        if (address.address === '$management')
          a.tooltip = 'internal ' + a.tooltip;
        a.extraClasses = a.tooltip;
        a.parentKey = 'Addresses';
        return a;
      };
      updateLeaves(addressFields, 'Addresses', worker);
    };

    $scope.$watch('filter', function (newValue, oldValue) {
      if (newValue !== oldValue) {
        allLinkInfo(null, function () { $timeout(function () { }); });
        localStorage[FILTERKEY] = JSON.stringify($scope.filter);
      }
    }, true);

    $scope.filterToggle = function () {
      let filter = $('#linkFilter');
      filter.toggle();
    };

    $scope.filter = angular.fromJson(localStorage[FILTERKEY]) || { endpointsOnly: 'true', hideConsoles: true };
    let links = new QDRFolder('Links');
    links.type = 'Links';
    links.info = { fn: allLinkInfo };
    links.expanded = (expandedNodeList.indexOf('Links') > -1);
    links.key = 'Links';
    links.parentKey = 'Links';
    links.extraClasses = 'links';
    topLevelChildren.push(links);

    // called both before the tree is created and whenever a background update is done
    var updateLinkTree = function (linkFields) {
      var worker = function (link) {
        let l = new QDRLeaf(link.title);
        let isConsole = QDRService.utilities.isConsole(QDRService.management.topology.getConnForLink(link));
        l.info = { fn: linkInfo };
        l.key = link.uid;
        l.fields = link;
        l.type = 'Link';
        l.parentKey = 'Links';
        if (isConsole)
          l.tooltip = 'console link';
        else
          l.tooltip = link.linkType + ' link';
        l.extraClasses = l.tooltip;
        return l;
      };
      updateLeaves(linkFields, 'Links', worker);
    };

    let connections = new QDRFolder('Connections');
    connections.type = 'Connections';
    connections.info = { fn: allConnectionInfo };
    connections.expanded = (expandedNodeList.indexOf('Connections') > -1);
    connections.key = 'Connections';
    connections.parentKey = 'Connections';
    connections.extraClasses = 'connections';
    topLevelChildren.push(connections);

    var updateConnectionTree = function (connectionFields) {
      var worker = function (connection) {
        let host = connection.host;
        if (connection.name === 'connection/' && connection.role === 'inter-router' && connection.host === '')
          host = connection.container + ':' + connection.identity;

        let c = new QDRLeaf(host);
        let isConsole = QDRService.utilities.isAConsole(connection.properties, connection.identity, connection.role, connection.routerId);
        c.type = 'Connection';
        c.info = { fn: connectionInfo };
        c.key = connection.uid;
        c.fields = connection;
        if (isConsole)
          c.tooltip = 'console connection';
        else
          c.tooltip = connection.role === 'inter-router' ? 'inter-router connection' : 'external connection';
        c.extraClasses = c.tooltip;
        c.parentKey = 'Connections';
        return c;
      };
      updateLeaves(connectionFields, 'Connections', worker);
    };

    var updateLogTree = function (logFields) {
      var worker = function (log) {
        let l = new QDRLeaf(log.name);
        l.type = 'Log';
        l.info = { fn: logInfo };
        l.key = log.name;
        l.parentKey = 'Logs';
        l.extraClasses = 'log';
        return l;
      };
      updateLeaves(logFields, 'Logs', worker);
    };

    let htmlReady = false;
    let dataReady = false;
    $scope.largeNetwork = QDRService.management.topology.isLargeNetwork();
    let logs = new QDRFolder('Logs');
    logs.type = 'Logs';
    logs.info = { fn: allLogInfo };
    logs.expanded = (expandedNodeList.indexOf('Logs') > -1);
    logs.key = 'Logs';
    logs.parentKey = 'Logs';
    if (QDRService.management.connection.versionCheck('0.8.0'))
      topLevelChildren.push(logs);

    var onTreeInitialized = function (event, data) {
      treeRoot = data.tree;
      let activeNode = treeRoot.getNodeByKey($scope.ActivatedKey);
      if (activeNode)
        activeNode.setActive(true);
      updateExpanded();
    };
    var initTreeAndGrid = function () {
      if (!htmlReady || !dataReady)
        return;
      let div = angular.element('#overtree');
      if (!div.width()) {
        setTimeout(initTreeAndGrid, 100);
        return;
      }
      $('#overtree').fancytree({
        activate: onTreeNodeActivated,
        expand: onTreeNodeExpanded,
        collapse: onTreeNodeCollapsed,
        init: onTreeInitialized,
        autoCollapse: $scope.largeNetwork,
        activeVisible: !$scope.largeNetwork,
        clickFolderMode: 1,
        source: topLevelChildren
      });
    };

    $scope.overviewLoaded = function () {
      htmlReady = true;
      initTreeAndGrid();
    };

    let nodeIds = QDRService.management.topology.nodeIdList();
    // add placeholders for the top level tree nodes
    let topLevelTreeNodes = [routers, addresses, links, connections, logs];
    topLevelTreeNodes.forEach(function (parent) {
      let placeHolder = new QDRLeaf('loading...');
      placeHolder.extraClasses = 'loading';
      parent.children = [placeHolder];
    });

    var updateExpanded = function () {
      QDRLog.debug('updateExpandedEntities');
      clearTimeout(updateIntervalHandle);

      let tree = $('#overtree').fancytree('getTree');
      if (tree && tree.visit) {
        let q = d3.queue(10);
        tree.visit(function (node) {
          if (node.isActive() || node.isExpanded()) {
            q.defer(node.data.info.fn, node);
          }
        });
        q.await(function (error) {
          if (error)
            QDRLog.error(error.message);

          // if there are no active nodes, activate the saved one
          let tree = $('#overtree').fancytree('getTree');
          if (!tree.getActiveNode()) {
            let active = tree.getNodeByKey($scope.ActivatedKey);
            if (active) {
              active.setActive(true);
            } else {
              tree.getNodeByKey('Routers').setActive(true);
            }
          }

          // fancytree animations sometimes stop short of shrinking its placeholder spans
          d3.selectAll('.ui-effects-placeholder').style('height', '0px');
          // once all expanded tree nodes have been update, schedule another update
          $timeout(function () {
            updateIntervalHandle = setTimeout(updateExpanded, updateInterval);
          });
        });
      }
    };

    dataReady = true;
    initTreeAndGrid();
    $scope.$on('$destroy', function () {
      clearTimeout(updateIntervalHandle);
      $(window).off('resize', resizer);
    });

  }
}
OverviewController.$inject = ['QDRService', '$scope', '$log', '$location', '$timeout', '$uibModal'];

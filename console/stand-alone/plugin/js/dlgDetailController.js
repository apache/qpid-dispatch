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

/* global Promise d3 Set */
export class DetailDialogController {
  constructor(QDRService, $scope, $timeout, $uibModalInstance, d) {
    this.controllerName = 'QDR.DetailDialogController';

    let expandedRows = new Set();
    $scope.d = d;  // the node object
    $scope.detail = {
      template: 'loading.html',
    };
    let countChars = function (ar) {
      let count = 0;
      ar.forEach( function (a) {
        count += a.length;
      });
      return count;
    };

    $scope.fields = {
      detailFields: {
        cols: [
          'version',
          'mode',
          'presettledDeliveries',
          'droppedPresettledDeliveries',
          'acceptedDeliveries',
          'rejectedDeliveries',
          'releasedDeliveries',
          'modifiedDeliveries',
          'deliveriesIngress',
          'deliveriesEgress',
          'deliveriesTransit',
          'deliveriesIngressRouteContainer',
          'deliveriesEgressRouteContainer'
        ]
      },
      linkFields: {
        cols: [
          'linkType',
          'owningAddr',
          'priority',
          'acceptedCount',
          'unsettledCount'
        ]
      },
      linkRouteFields: {
        cols: [
          'prefix',
          'direction',
          'containerId'
        ]
      },
      autoLinkFields: {
        cols: [
          'addr',
          'direction',
          'containerId'
        ]
      },
      addressFields: {
        cols: [
          'prefix',
          'distribution'
        ]
      }
    };
    for (let f in $scope.fields) {
      $scope.fields[f].count = countChars($scope.fields[f].cols);
    }

    $scope.okClick = function () {
      clearInterval(updateTimer);
      $uibModalInstance.close(true);
    };
    $scope.expandClicked = function (id) {
      if (expandedRows.has(id)) {
        expandedRows.delete(id);
      } else {
        expandedRows.add(id);
        $scope.detail.moreInfo(id);
      }
    };
    $scope.expanded = function (id) {
      return expandedRows.has(id);
    };
    $scope.fieldWidth = function (val, sizes) {
      if (!sizes)
        return '10%';
      return `${Math.round(sizes[val] * 100 / sizes.total)}%`;
    };
    let updateSizes = function (fields, sizes, obj) {
      fields.forEach( function (key) {
        if (!sizes[key])
          sizes[key] = QDRService.utilities.humanify(key).length;
        sizes[key] = Math.max(sizes[key], QDRService.utilities.pretty(obj[key]).length);
      });
      sizes.total = 0;
      for (let key in sizes) {
        if (key !== 'total')
          sizes.total += sizes[key];
      }
    };

    let groupDetail = function () {
      let q_getEdgeInfo = function (n, infoPerId, callback) {
        let nodeId = QDRService.utilities.idFromName(n.container, '_edge');
        QDRService.management.topology.fetchEntities(nodeId, 
          [{entity: 'router', attrs: []},
          ],
          function (results) {
            let r = results[nodeId].router;
            infoPerId[n.container] = QDRService.utilities.flatten(r.attributeNames, r.results[0]);
            infoPerId[n.container].linkRoutes = [];
            infoPerId[n.container].autoLinks = [];
            infoPerId[n.container].addresses = [];
            callback(null);
          });
      };
      return new Promise( (function (resolve) {
        let infoPerId = {};
        if (d.nodeType === 'edge') {
          $scope.detail.moreInfo = function (id) {
            let nodeId = QDRService.utilities.idFromName(id, '_edge');
            QDRService.management.topology.fetchEntities(nodeId, 
              [{entity: 'router.link', attrs: []},
                {entity: 'linkRoute', attrs: $scope.fields.linkRouteFields.cols},
                {entity: 'autoLink', attrs: $scope.fields.autoLinkFields.cols},
                {entity: 'address', attrs: []},
              ],
              function (results) {
                $timeout( function () {
                  infoPerId[id].linkRouteSizes = {};
                  infoPerId[id].linkRoutes = QDRService.utilities.flattenAll(results[nodeId].linkRoute,
                    function (route) {
                      updateSizes($scope.fields.linkRouteFields.cols, infoPerId[id].linkRouteSizes, route);
                      return route;
                    });
                  infoPerId[id].autoLinkSizes = {};
                  infoPerId[id].autoLinks = QDRService.utilities.flattenAll(results[nodeId].autoLink, 
                    function (link) {
                      updateSizes($scope.fields.autoLinkFields.cols, infoPerId[id].autoLinkSizes, link);
                      return link;
                    });
                  infoPerId[id].addressSizes = {};
                  infoPerId[id].addresses = QDRService.utilities.flattenAll(results[nodeId].address, 
                    function (addr) {
                      updateSizes($scope.fields.addressFields.cols, infoPerId[id].addressSizes, addr);
                      return addr;
                    });
                });
              });
          };

          let q = d3.queue(10);
          for (let n=0; n<d.normals.length; n++) {
            q.defer(q_getEdgeInfo, d.normals[n], infoPerId);
            if (expandedRows.has(d.normals[n].container)) {
              $scope.detail.moreInfo(d.normals[n].container);
            }
          }
          q.await(function () {
            $scope.detail.template = 'edgeRouters.html';
            $scope.detail.title = 'edge router';
            resolve({
              description: 'Select an edge router to see more info',
              infoPerId: infoPerId
            });
          });
        } else {
          $scope.detail.moreInfo = function () {};
          let attrs = QDRService.utilities.copy($scope.fields.linkFields.cols);
          attrs.unshift('connectionId');
          QDRService.management.topology.fetchEntities(d.key, 
            [{entity: 'router.link', attrs: attrs}],
            function (results) {
              let links = results[d.key]['router.link'];
              for (let i=0; i<d.normals.length; i++) {
                let n = d.normals[i];
                let conn = {};
                infoPerId[n.container] = conn;
                conn.container = n.container;
                conn.encrypted = n.encrypted ? 'True' : 'False';
                conn.host = n.host;
                //conn.links = [];
                conn.sizes = {};
                conn.links = QDRService.utilities.flattenAll(links, function (link) {
                  if (link.connectionId === n.connectionId) {
                    link.owningAddr = QDRService.utilities.addr_text(link.owningAddr);
                    updateSizes($scope.fields.linkFields.cols, conn.sizes, link);
                    return link;
                  } else {
                    return null;
                  }
                });
                conn.linkCount = conn.links.length;
              }
              let dir = d.cdir === 'in' ? 'inbound' : d.cdir === 'both' ? 'in and outbound' : 'outbound';
              let count = d.normals.length;
              let verb = count > 1 ? 'are' : 'is';
              let preposition = d.cdir === 'in' ? 'to' : d.cdir === 'both' ? 'for' : 'from';
              let plural = count > 1 ? 's': '';
              $scope.detail.template = 'clients.html';
              $scope.detail.title = 'for client';
              resolve({
                description: `There ${verb} ${count} ${dir} connection${plural} ${preposition} ${d.routerId} with role ${d.nodeType}`,
                infoPerId: infoPerId
              });
            });
        }
      }));
    };
  
    let updateDetail = function () {
      groupDetail()
        .then( function (det) {
          $timeout( function () {
            $scope.detail.title = `for ${d.normals.length} ${$scope.detail.title}${d.normals.length > 1 ? 's' : ''}`;
            $scope.detail.description = det.description;
            $scope.detail.infoPerId = Object.keys(det.infoPerId).map( function (id) {
              return det.infoPerId[id];
            }).sort( function (a, b) {
              return a.name > b.name ? 1 : -1;
            });
          }, 10);
        });
    };
    let updateTimer = setInterval(updateDetail, 2000);
    updateDetail();

  }
}
DetailDialogController.$inject = ['QDRService', '$scope', '$timeout', '$uibModalInstance', 'd'];

export class SubTable {
  constructor () {
    this.restrict = 'E';
    this.scope = {
      sizes: '=sizes',
      cols: '=cols',
      rows: '=rows'
    };
    this.templateUrl = 'sub-table.html';
  }
  link (scope) {
    scope.fieldWidth = function (val, sizes) {
      if (!sizes)
        return '10%';
      return `${Math.round(sizes[val] * 100 / sizes.total)}%`;
    };
  }
  static create() {
    return new SubTable();
  }
}

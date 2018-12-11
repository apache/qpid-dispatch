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
import { utils } from "./amqp/utilities.js";

export class DetailDialogController {
  constructor(QDRService, $scope, $timeout, $uibModalInstance, d) {
    this.controllerName = 'QDR.DetailDialogController';
    this.rates = {};

    let expandedRows = new Set();
    $scope.d = d;  // the node object
    $scope.detail = {
      template: 'loading.html',
    };
    // count the number of characters in an array of strings
    let countChars = function (ar) {
      let count = 0;
      ar.forEach(a => count += a.length);
      return count;
    };

    // which attributes to fetch and display
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
    // used for calculating sub-table cell widths
    for (let f in $scope.fields) {
      $scope.fields[f].count = countChars($scope.fields[f].cols);
    }

    // close button clicked
    $scope.okClick = function () {
      clearTimeout(updateTimer);
      $uibModalInstance.close(true);
    };
    // a row was expanded/collapsed. add/remove it to/from the Set
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
    // keep an array of column sizes
    let updateSizes = function (fields, sizes, obj) {
      fields.forEach(function (key) {
        if (!sizes[key])
          sizes[key] = utils.humanify(key).length;
        sizes[key] = Math.max(sizes[key], utils.pretty(obj[key]).length);
      });
      sizes.total = 0;
      for (let key in sizes) {
        if (key !== 'total')
          sizes.total += sizes[key];
      }
    };

    // get the detail info for the popup
    let groupDetail = function () {
      let self = this;
      // queued function to get the .router info for an edge router
      let q_getEdgeInfo = function (n, infoPerId, callback) {
        let nodeId = utils.idFromName(n.container, '_edge');
        QDRService.management.topology.fetchEntities(nodeId,
          [{ entity: 'router', attrs: [] },
          ],
          function (results) {
            let r = results[nodeId].router;
            infoPerId[n.container] = utils.flatten(r.attributeNames, r.results[0]);
            let rates = utils.rates(infoPerId[n.container], ["acceptedDeliveries"], self.rates, n.container, 1);
            infoPerId[n.container].acceptedDeliveriesRate = Math.round(rates.acceptedDeliveries, 2);
            infoPerId[n.container].linkRoutes = [];
            infoPerId[n.container].autoLinks = [];
            infoPerId[n.container].addresses = [];
            callback(null);
          });
      };
      return new Promise((function (resolve) {
        let infoPerId = {};
        // we are getting info for an edge router
        if (d.nodeType === 'edge') {
          // called for each expanded row to get further details about the edge router
          $scope.detail.moreInfo = function (id) {
            let nodeId = utils.idFromName(id, '_edge');
            QDRService.management.topology.fetchEntities(nodeId,
              [
                { entity: 'router.link', attrs: [] },
                { entity: 'linkRoute', attrs: $scope.fields.linkRouteFields.cols },
                { entity: 'autoLink', attrs: $scope.fields.autoLinkFields.cols },
                { entity: 'address', attrs: [] },
              ],
              function (results) {
                $timeout(function () {
                  // save the results (and sizes) for each entity requested
                  if (infoPerId[id]) {
                    infoPerId[id].linkRouteSizes = {};
                    infoPerId[id].linkRoutes = utils.flattenAll(results[nodeId].linkRoute,
                      function (route) {
                        updateSizes($scope.fields.linkRouteFields.cols, infoPerId[id].linkRouteSizes, route);
                        return route;
                      });
                    infoPerId[id].autoLinkSizes = {};
                    infoPerId[id].autoLinks = utils.flattenAll(results[nodeId].autoLink,
                      function (link) {
                        updateSizes($scope.fields.autoLinkFields.cols, infoPerId[id].autoLinkSizes, link);
                        return link;
                      });
                    infoPerId[id].addressSizes = {};
                    infoPerId[id].addresses = utils.flattenAll(results[nodeId].address,
                      function (addr) {
                        updateSizes($scope.fields.addressFields.cols, infoPerId[id].addressSizes, addr);
                        return addr;
                      });
                  }
                });
              });
          };

          // async send up to 10 requests
          let q = d3.queue(10);
          for (let n = dStart; n < dStop; n++) {
            q.defer(q_getEdgeInfo, d.normals[n], infoPerId);
            if (expandedRows.has(d.normals[n].container)) {
              $scope.detail.moreInfo(d.normals[n].container);
            }
          }
          // await until all sent requests have completed
          q.await(function () {
            $scope.detail.template = 'edgeRouters.html';
            $scope.detail.title = 'edge router';
            // send the results
            resolve({
              description: 'Select an edge router to see more info',
              infoPerId: infoPerId
            });
          });
        } else {
          // we are getting info for a group of clients or consoles
          $scope.detail.moreInfo = function () { };
          let attrs = utils.copy($scope.fields.linkFields.cols);
          attrs.unshift('connectionId');
          QDRService.management.topology.fetchEntities(d.key,
            [{ entity: 'router.link', attrs: attrs }],
            function (results) {
              let links = results[d.key]['router.link'];
              for (let i = 0; i < d.normals.length; i++) {
                let n = d.normals[i];
                let conn = {};
                infoPerId[n.container] = conn;
                conn.container = n.container;
                conn.encrypted = n.encrypted ? 'True' : 'False';
                conn.host = n.host;
                //conn.links = [];
                conn.sizes = {};
                conn.links = utils.flattenAll(links, function (link) {
                  if (link.connectionId === n.connectionId) {
                    link.owningAddr = utils.addr_text(link.owningAddr);
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
              let plural = count > 1 ? 's' : '';
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

    let dStart = 0;
    let dStop = Math.min(d.normals.length, 10);
    let cachedInfo = [];
    let updateTimer;
    let doUpdateDetail = function () {
      cachedInfo = [];
      updateDetail.call(this);
    };
    let updateDetail = function () {
      groupDetail.call(this)
        .then(function (det) {
          Object.keys(det.infoPerId).forEach(function (id) {
            cachedInfo.push(det.infoPerId[id]);
          });
          if (dStop < d.normals.length) {
            dStart = dStop;
            dStop = Math.min(d.normals.length, dStart + 10);
            setTimeout(updateDetail.bind(this), 1);
          } else {
            $timeout(function () {
              $scope.detail.title = `for ${d.normals.length} ${$scope.detail.title}${d.normals.length > 1 ? 's' : ''}`;
              $scope.detail.description = det.description;
              $scope.detail.infoPerId = cachedInfo.sort(function (a, b) {
                return a.name > b.name ? 1 : -1;
              });
              dStart = 0;
              dStop = Math.min(d.normals.length, 10);
              updateTimer = setTimeout(doUpdateDetail.bind(this), 2000);
            }.bind(this));
          }
        }.bind(this));
    };
    doUpdateDetail.call(this);
  }
}
DetailDialogController.$inject = ['QDRService', '$scope', '$timeout', '$uibModalInstance', 'd'];

// SubTable directive
export class SubTable {
  constructor() {
    this.restrict = 'E';
    this.scope = {
      sizes: '=sizes',
      cols: '=cols',
      rows: '=rows'
    };
    this.templateUrl = 'sub-table.html';
  }
  link(scope) {
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

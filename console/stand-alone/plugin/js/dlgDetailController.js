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
    $scope.detailFields = [
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
    ];
    $scope.linkFields = [
      'linkType',
      'owningAddr',
      'priority',
      'acceptedCount',
      'unsettledCount'
    ];
    $scope.linkRouteFields = [
      'prefix',
      'direction',
      'containerId'
    ];
    $scope.autoLinkFields = [
      'addr',
      'direction',
      'containerId'
    ];
    $scope.addressFields = [
      'prefix',
      'distribution'
    ];

    $scope.okClick = function () {
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
                {entity: 'linkRoute', attrs: $scope.linkRouteFields},
                {entity: 'autoLink', attrs: $scope.autoLinkFields},
                {entity: 'address', attrs: []},
              ],
              function (results) {
                $timeout( function () {
                  infoPerId[id].linkRoutes = QDRService.utilities.flattenAll(results[nodeId].linkRoute);
                  infoPerId[id].autoLinks = QDRService.utilities.flattenAll(results[nodeId].autoLink);
                  infoPerId[id].addresses = QDRService.utilities.flattenAll(results[nodeId].address);
                });
              });
          };

          let q = d3.queue(10);
          for (let n=0; n<d.normals.length; n++) {
            q.defer(q_getEdgeInfo, d.normals[n], infoPerId);
          }
          q.await(function () {
            $scope.detail.template = 'edgeRouters.html';
            $scope.detail.title = 'for edge router';
            resolve({
              description: 'Expand an edge router to see more info',
              infoPerId: infoPerId
            });
          });
        } else if (d.isConsole) {
          $scope.detail.template = 'consoles.html';
          $scope.detail.title = 'for console';
          resolve({
            description: ''
          });
        } else {
          $scope.detail.moreInfo = function () {};
          QDRService.management.topology.fetchEntities(d.key, 
            [{entity: 'router.link', attrs: []}],
            function (results) {
              let links = results[d.key]['router.link'];
              for (let i=0; i<d.normals.length; i++) {
                let n = d.normals[i];
                let conn = {};
                let connectionIndex = links.attributeNames.indexOf('connectionId');
                infoPerId[n.container] = conn;
                conn.container = n.container;
                conn.encrypted = n.encrypted;
                conn.host = n.host;
                conn.links = [];
                for (let l=0; l<links.results.length; l++) {
                  if (links.results[l][connectionIndex] === n.connectionId) {
                    let link = QDRService.utilities.flatten(links.attributeNames, links.results[l]);
                    link.owningAddr = QDRService.utilities.addr_text(link.owningAddr);
                    conn.links.push(link);
                  }
                }
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
  
    groupDetail()
      .then( function (det) {
        $timeout( function () {
          $scope.detail.title = `for ${d.normals.length} ${$scope.detail.title}${d.normals.length > 1 ? 's' : ''}`;
          $scope.detail.description = det.description;
          $scope.detail.infoPerId = det.infoPerId;
        }, 10);
      });
  }
}
DetailDialogController.$inject = ['QDRService', '$scope', '$timeout', '$uibModalInstance', 'd'];

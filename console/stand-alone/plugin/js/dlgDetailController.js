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

/* global Promise d3 */
export class DetailDialogController {
  constructor(QDRService, $scope, $timeout, $uibModalInstance, $sce, d) {
    this.controllerName = 'QDR.DetailDialogController';

    $scope.d = d;  // the node object
    $scope.detail = {
      title: d.normals.length + ' ' + (d.nodeType === 'edge' ? 'edge routers' : 'clients'),
      header: '',
      details: 'loading...'
    };

    $scope.okClick = function () {
      $uibModalInstance.close(true);
    };
    let groupDetail = function () {
      let q_getEdgeInfo = function (n, response, callback) {
        let nodeId = QDRService.utilities.idFromName(n.container, '_edge');
        QDRService.management.topology.fetchEntities(nodeId, 
          [{entity: 'connection', attrs: []},
            {entity: 'router.link', attrs: []}],
          function (results) {
            response.HTML += '<tr>';
            response.HTML += `<td>${QDRService.utilities.nameFromId(nodeId)}</td>`;
  
            let connection = results[nodeId].connection;
            // count endpoint connections
            let endpoints = QDRService.utilities.countFor(connection.attributeNames, connection.results, 'role', 'endpoint');
            response.HTML += `<td align='right'>${endpoints}</td>`;
  
            //let link = results[nodeId]['router.link'];
            //let conn = QDRService.utilities.flatten(results.attributeNames, )
            response.HTML += '</tr>';
            callback(null);
          });
      };
      return new Promise( (function (resolve) {
        let response = {
          header: '<table><tr><td>Id</td><td>Endpoints</td></tr></table>', 
          HTML: '<table><tr><td>Id</td><td>endpoints</td></tr>'
        };
        if (d.nodeType === 'edge') {
          let q = d3.queue(10);
          for (let n=0; n<d.normals.length; n++) {
            q.defer(q_getEdgeInfo, d.normals[n], response);
          }
          q.await(function () {
            response.HTML += '</table>';
            resolve(response);
          });
        } else {
          response.HTML += '<tr><td>Details for clients go here</td</tr></table>';
          resolve(response);
        }
      }));
    };
  
    groupDetail()
      .then( function (det) {
        $timeout( function () {
          $scope.detail.header = $sce.trustAsHtml(det.header);
          $scope.detail.details = $sce.trustAsHtml(det.HTML);
        });
      });
  }
}
DetailDialogController.$inject = ['QDRService', '$scope', '$timeout', '$uibModalInstance', '$sce', 'd'];

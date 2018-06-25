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

import { QDRCore, QDRLogger } from './qdrGlobals.js';

export class OverviewLogsController {
  constructor(QDRService, $scope, $log, $uibModalInstance, $timeout, nodeName, nodeId, module, level) {
    this.controllerName = 'QDR.OverviewLogsController';

    let QDRLog = new QDRLogger($log, 'OverviewLogsController');
    var gotLogInfo = function (nodeId, response, context) {
      let statusCode = context.message.application_properties.statusCode;
      if (statusCode < 200 || statusCode >= 300) {
        QDRCore.notification('error', context.message.statusDescription);
        QDRLog.info('Error ' + context.message.statusDescription);
      } else {
        let levelLogs = response.filter( function (result) {
          if (result[1] == null)
            result[1] = 'error';
          return result[1].toUpperCase() === level.toUpperCase() && result[0] === module;
        });
        let logFields = levelLogs.map( function (result) {
          return {
            nodeId: QDRService.utilities.nameFromId(nodeId),
            name: result[0],
            type: result[1],
            message: result[2],
            source: result[3],
            line: result[4],
            time: Date(result[5]).toString()
          };
        });
        $timeout(function () {
          $scope.loading = false;
          $scope.logFields = logFields;
        });
      }
    };
    QDRService.management.connection.sendMethod(nodeId, undefined, {}, 'GET-LOG', {module: module})
      .then( function (response) {gotLogInfo(nodeId, response.response, response.context);});

    $scope.loading = true;
    $scope.module = module;
    $scope.level = level;
    $scope.nodeName = nodeName;
    $scope.logFields = [];
    $scope.ok = function () {
      $uibModalInstance.close(true);
    };

  }
}
OverviewLogsController.$inject = ['QDRService', '$scope', '$log', '$uibModalInstance', '$timeout', 'nodeName', 'nodeId', 'module', 'level'];

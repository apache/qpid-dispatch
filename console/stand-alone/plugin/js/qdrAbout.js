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

export class AboutController {
  constructor($scope, QDRService, $timeout) {
    this.controllerName = 'QDR.AboutController';

    $scope.additionalInfo = "Console for the Apache Qpid dispatch router: A high-performance, lightweight AMQP 1.0 message router, written in C and built on Qpid Proton. It provides flexible and scalable interconnect between any AMQP endpoints, whether they be clients, brokers or other AMQP-enabled services.";
    $scope.copyright = "Apache License, Version 2.0";
    $scope.imgAlt = "Qpid Dispatch Router Logo";
    $scope.imgSrc = "img/logo-alt.svg";
    $scope.title = "Apache Qpid Dispatch Router Console";
    $scope.productInfo = [
      { name: 'Version', value: '<not connected>' },
      { name: 'Server Name', value: window.location.host },
      { name: 'User Name', value: '<not connected>' },
      { name: 'User Role', value: 'Administrator' }];
    $scope.open = function () {
      if (QDRService.management.connection.is_connected()) {
        let parts = QDRService.management.connection.getReceiverAddress().split("/");
        parts[parts.length - 1] = "$management";
        let router = parts.join('/');
        QDRService.management.topology.fetchEntity(router, "router", ["version"], function (nodeId, entity, response) {
          $timeout(function () {
            let r = QDRService.utilities.flatten(response.attributeNames, response.results[0]);
            $scope.productInfo[0].value = r.version;
          });
        });
        QDRService.management.topology.fetchEntity(router, "connection", [], function (nodeId, entity, response) {
          $timeout(function () {
            let user = '<unknown>';
            response.results.some(result => {
              let c = QDRService.utilities.flatten(response.attributeNames, result);
              if (QDRService.utilities.isConsole(c)) {
                user = c.user;
                return true;
              }
              return false;
            });
            $scope.productInfo[2].value = user;
          });
        });

      }
      $scope.isOpen = true;
    };
    $scope.onClose = function () {
      $scope.isOpen = false;
    };

  }
}

AboutController.$inject = ['$scope', 'QDRService', '$timeout'];

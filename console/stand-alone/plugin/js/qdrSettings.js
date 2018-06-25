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
/* global angular */
import { QDR_SETTINGS_KEY, QDRLogger} from './qdrGlobals.js';

export class SettingsController {
  constructor(QDRService, QDRChartService, $scope, $log, $timeout) {
    this.controllerName = 'QDR.SettingsController';

    let QDRLog = new QDRLogger($log, 'SettingsController');
    $scope.connecting = false;
    $scope.connectionError = false;
    $scope.connectionErrorText = undefined;
    $scope.forms = {};

    $scope.formEntity = angular.fromJson(localStorage[QDR_SETTINGS_KEY]) || {
      address: '',
      port: '',
      username: '',
      password: '',
      autostart: false
    };
    $scope.formEntity.password = '';

    $scope.$watch('formEntity', function(newValue, oldValue) {
      if (newValue !== oldValue) {
        let pass = newValue.password;
        newValue.password = '';
        localStorage[QDR_SETTINGS_KEY] = angular.toJson(newValue);
        newValue.password = pass;
      }
    }, true);

    $scope.buttonText = function() {
      if (QDRService.management.connection.is_connected()) {
        return 'Disconnect';
      } else {
        return 'Connect';
      }
    };

    // connect/disconnect button clicked
    $scope.connect = function() {
      if (QDRService.management.connection.is_connected()) {
        $timeout( function () {
          QDRService.disconnect();
        });
        return;
      }

      if ($scope.settings.$valid) {
        $scope.connectionError = false;
        $scope.connecting = true;
        // timeout so connecting animation can display
        $timeout(function () {
          doConnect();
        });
      }
    };

    var doConnect = function() {
      QDRLog.info('doConnect called on connect page');
      if (!$scope.formEntity.address)
        $scope.formEntity.address = 'localhost';
      if (!$scope.formEntity.port)
        $scope.formEntity.port = 5673;

      var failed = function() {
        $timeout(function() {
          $scope.connecting = false;
          $scope.connectionErrorText = 'Unable to connect to ' + $scope.formEntity.address + ':' + $scope.formEntity.port;
          $scope.connectionError = true;
        });
      };
      let options = {address: $scope.formEntity.address, 
        port: $scope.formEntity.port, 
        password: $scope.formEntity.password,
        username: $scope.formEntity.username,
        reconnect: true};
      QDRService.connect(options)
        .then( function () {
          // register a callback for when the node list is available (needed for loading saved charts)
          QDRService.management.topology.addUpdatedAction('initChartService', function() {
            QDRService.management.topology.delUpdatedAction('initChartService');
            QDRChartService.init(); // initialize charting service after we are connected
          });
          // get the list of nodes
          QDRService.management.topology.startUpdating(false);
          // will have redirected to last known page or /overview
        }, function (e) {
          failed(e);
        });
    };
  }
}
SettingsController.$inject = ['QDRService', 'QDRChartService', '$scope', '$log', '$timeout'];


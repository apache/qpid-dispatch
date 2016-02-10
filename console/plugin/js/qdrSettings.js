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
   * @method SettingsController
   * @param $scope
   * @param QDRServer
   *
   * Controller that handles the QDR settings page
   */
  QDR.module.controller("QDR.SettingsController", ['$scope', 'QDRService', '$location', function($scope, QDRService, $location) {

    $scope.connecting = false;
    $scope.connectionError = false;
    $scope.connectionErrorText = undefined;
    $scope.forms = {};

    $scope.formEntity = angular.fromJson(localStorage[QDR.SETTINGS_KEY]) || {address: '', port: '', username: '', password: '', autostart: false};
    $scope.formConfig = {
      properties: {
        address: {
          description: "Router address",
          'type': 'java.lang.String',
          required: true
        },
        port: {
          description: 'Router port',
          'type': 'Integer',
          tooltip: 'Ports to connect to, by default 5672'
        },
        username: {
          description: 'User Name',
          'type': 'java.lang.String'
        },
        password: {
          description: 'Password',
          'type': 'password'
        },
        /*
        useSSL: {
          description: 'SSL',
          'type': 'boolean'
        },*/
        autostart: {
          description: 'Connect at startup',
          'type': 'boolean',
          tooltip: 'Whether or not the connection should be started as soon as you log into hawtio'
        }
      }
    };

    $scope.$watch('formEntity', function(newValue, oldValue) {
      if (newValue !== oldValue) {
        localStorage[QDR.SETTINGS_KEY] = angular.toJson(newValue);
      }
    }, true);

    $scope.buttonText = function() {
      if (QDRService.isConnected()) {
        return "Reconnect";
      } else {
        return "Connect";
      }
    };

    
    $scope.connect = function() {
      if ($scope.settings.$valid) {
        $scope.connectionError = false;
        $scope.connecting = true;
        console.log("attempting to connect");
        QDRService.addDisconnectAction(function() {
          //QDR.log.debug("disconnect action called");
          $scope.connecting = false;
          $scope.connectionErrorText = QDRService.errorText;
          $scope.connectionError = true;
          $scope.$apply();
        });
        QDRService.addConnectAction(function() {
          //QDR.log.debug("got connection notification");
          $scope.connecting = false;
          //console.log("we were on connect page. let's switch to topo now that we are connected");
          //QDR.log.debug("location before the connect " + $location.path());
          $location.path("/overview");
          //QDR.log.debug("location after the connect " + $location.path());
          $scope.$apply();
        });
        QDRService.connect($scope.formEntity);
      }
    };

  }]);

  return QDR;
}(QDR || {}));

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
var QDR = (function(QDR) {

  /**
   * @method SettingsController
   * @param $scope
   * @param QDRServer
   *
   * Controller that handles the QDR settings page
   */

  QDR.module.controller("QDR.SettingsController", ['$scope', 'QDRService', '$timeout', '$location', function($scope, QDRService, $timeout, $location) {

    $scope.connecting = false;
    $scope.connectionError = false;
    $scope.connectionErrorText = undefined;
    $scope.forms = {};

    $scope.formEntity = angular.fromJson(localStorage[QDR.SETTINGS_KEY]) || {
      address: '',
      port: '',
      username: '',
      password: '',
      autostart: false
    };

    $scope.$watch('formEntity', function(newValue, oldValue) {
      if (newValue !== oldValue) {
        localStorage[QDR.SETTINGS_KEY] = angular.toJson(newValue);
      }
    }, true);

    $scope.buttonText = function() {
      if (QDRService.isConnected()) {
        return "Disconnect";
      } else {
        return "Connect";
      }
    };

    $scope.connect = function() {
      if (QDRService.connected) {
        QDRService.disconnect();
        return;
      }

      if ($scope.settings.$valid) {
        $scope.connectionError = false;
        $scope.connecting = true;
        $timeout(doConnect) // timeout so connecting animation can display
      }
    }

    var doConnect = function() {
      if (!$scope.formEntity.address)
        $scope.formEntity.address = "localhost"

      console.log("attempting to connect to " + $scope.formEntity.address + ':' + $scope.formEntity.port);
      QDRService.addDisconnectAction(function() {
        $timeout(function() {
          QDR.log.debug("disconnect action called");
          $scope.connecting = false;
          $scope.connectionErrorText = QDRService.errorText;
          $scope.connectionError = true;
        })
      });
      QDRService.addConnectAction(function() {
        QDRService.getSchema(function () {
          QDR.log.debug("got schema after connection")

          $timeout(function () {
            $scope.connecting = false;
            $location.search('org', null)
            $location.path(QDR.pluginRoot + "/topology");
          })

/*

          QDRService.addUpdatedAction("initialized", function () {
            QDRService.delUpdatedAction("initialized")
            QDR.log.debug("got initial topology")
            $timeout(function() {
QDR.log.debug("changing location to ")
              $scope.connecting = false;
              $location.search('org', null)
              $location.path(QDR.pluginRoot + "/topology");
            })
          })
          QDR.log.debug("requesting a topology")
          QDRService.setUpdateEntities([])
          QDRService.topology.get()
*/
        })
      });
      QDRService.connect($scope.formEntity);
    }

  }]);


  QDR.module.directive('posint', function() {
    return {
      require: 'ngModel',

      link: function(scope, elem, attr, ctrl) {
        // input type number allows + and - but we don't want them so filter them out
        elem.bind('keypress', function(event) {
          var nkey = !event.charCode ? event.which : event.charCode;
          var skey = String.fromCharCode(nkey);
          var nono = "-+.,"
          if (nono.indexOf(skey) >= 0) {
            event.preventDefault();
            return false;
          }
          // firefox doesn't filter out non-numeric input. it just sets the ctrl to invalid
          if (/[\!\@\#\$\%^&*\(\)]/.test(skey) && event.shiftKey || // prevent shift numbers
            !( // prevent all but the following
              nkey <= 0 || // arrows
              nkey == 8 || // delete|backspace
              nkey == 13 || // enter
              (nkey >= 37 && nkey <= 40) || // arrows
              event.ctrlKey || event.altKey || // ctrl-v, etc.
              /[0-9]/.test(skey)) // numbers
          ) {
            event.preventDefault();
            return false;
          }
        })
          // check the current value of input
        var _isPortInvalid = function(value) {
          var port = value + ''
          var isErrRange = false;
          // empty string is valid
          if (port.length !== 0) {
            var n = ~~Number(port);
            if (n < 1 || n > 65535) {
              isErrRange = true;
            }
          }
          ctrl.$setValidity('range', !isErrRange)
          return isErrRange;
        }

        //For DOM -> model validation
        ctrl.$parsers.unshift(function(value) {
          return _isPortInvalid(value) ? undefined : value;
        });

        //For model -> DOM validation
        ctrl.$formatters.unshift(function(value) {
          _isPortInvalid(value);
          return value;
        });
      }
    };
  });

  return QDR;
}(QDR || {}));

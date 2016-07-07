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

			setTimeout(doConnect, 100)
		}
	}

	var doConnect = function () {
        if (!$scope.formEntity.address)
            $scope.formEntity.address = "localhost"

        console.log("attempting to connect to " + $scope.formEntity.address + ':' + $scope.formEntity.port);
        QDRService.addDisconnectAction(function() {
          QDR.log.debug("disconnect action called");
          $scope.connecting = false;
          $scope.connectionErrorText = QDRService.errorText;
          $scope.connectionError = true;
          $scope.$apply();
        });
        QDRService.addConnectAction(function() {
          //QDR.log.debug("got connection notification");
          $scope.connecting = false;

          var searchObject = $location.search();
          var goto = "overview";
          if (searchObject.org) {
			goto = searchObject.org;
          }
          //QDR.log.debug("location before the connect " + $location.path());
          $location.path(QDR.pluginRoot +"/" + goto);
          //QDR.log.debug("location after the connect " + $location.path());
          $scope.$apply();
        });


        QDRService.connect($scope.formEntity);
      }

  }]);


QDR.module.directive('posint', function (){
   return {
      require: 'ngModel',
      link: function(scope, elem, attr, ctrl) {

		  var isPortValid = function (value) {
			var port = value + ''
			var n = ~~Number(port);
			var valid = (port.length === 0) || (String(n) === port && n >= 0)
			var nono = "-+.,"
			for (var i=0; i<port.length; ++i) {
				if (nono.indexOf(port[i]) >= 0) {
					valid = false;
					break
				}
			}
			return valid;
		  }

          //For DOM -> model validation
          ctrl.$parsers.unshift(function(value) {
			var valid = isPortValid(value)
			ctrl.$setValidity('posint', valid)
			return valid ? value : undefined;
          });

          //For model -> DOM validation
          ctrl.$formatters.unshift(function(value) {
             ctrl.$setValidity('posint', isPortValid(value));
             return value;
          });
      }
   };
});

/*
QDR.module.directive('posint', function() {
  return {
    require: 'ngModel',
    link: function(scope, elm, attrs, ctrl) {
      ctrl.$validators.posint = function(modelValue, viewValue) {
        if (ctrl.$isEmpty(modelValue)) {
          // consider empty models to be valid
          return true;
        }

		var port = modelValue + ''
		var n = ~~Number(port);
		var valid = (String(n) === port && n >= 0)
		var nono = "-+.,"
		for (var i=0; i<port.length; ++i) {
			if (nono.indexOf(port[i]) >= 0) {
				valid = false;
				break
			}
		}
		return valid;
      };
    }
  };
});

*/

  return QDR;
}(QDR || {}));

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
  QDR.SettingsController = function($scope, QDRService, localStorage, $location) {

    $scope.connecting = false;
    $scope.connectionError = false;
    $scope.connectionErrorText = undefined;
    $scope.forms = {};

    $scope.formEntity = angular.fromJson(localStorage[QDR.SETTINGS_KEY]) || {};
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
      if ($scope.forms.settings.$valid) {
        $scope.connectionError = false;
        $scope.connecting = true;
        console.log("attempting to connect");
        QDRService.addDisconnectAction(function() {
          QDR.log.debug("disconnect action called");
          $scope.connecting = false;
          $scope.connectionErrorText = QDRService.errorText;
          $scope.connectionError = true;
        });
        QDRService.addConnectAction(function() {
          QDR.log.debug("got connection notification");
          $scope.connecting = false;
          //console.log("we were on connect page. let's switch to topo now that we are connected");
          QDR.log.debug("location before the connect " + $location.path());
          $location.path("/overview");
          QDR.log.debug("location after the connect " + $location.path());
          $scope.$apply();
        });
        QDRService.connect($scope.formEntity);
      }
    };

  };

  return QDR;
}(QDR || {}));

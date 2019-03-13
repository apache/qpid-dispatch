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
/* global angular d3 */

/**
 * @module QDR
 * @main QDR
 *
 * The main entry point for the QDR module
 *
 */

//import angular from 'angular';
import { QDRLogger, QDRTemplatePath, QDR_LAST_LOCATION } from './plugin/js/qdrGlobals.js';
import { QDRService } from './plugin/js/qdrService.js';
import { QDRChartService } from './plugin/js/qdrChartService.js';
import { NavBarController } from './plugin/js/navbar.js';
import { OverviewController } from './plugin/js/qdrOverview.js';
import { OverviewChartsController } from './plugin/js/qdrOverviewChartsController.js';
import { OverviewLogsController } from './plugin/js/qdrOverviewLogsController.js';
import { TopologyController } from './plugin/js/topology/qdrTopology.js';
import { ChordController } from './plugin/js/chord/qdrChord.js';
import { ListController } from './plugin/js/qdrList.js';
import { TopAddressesController } from './plugin/js/qdrTopAddressesController.js';
import { ChartDialogController } from './plugin/js/dlgChartController.js';
import { DetailDialogController, SubTable } from './plugin/js/dlgDetailController.js';
import { SettingsController } from './plugin/js/qdrSettings.js';
import { SchemaController } from './plugin/js/qdrSchema.js';
import { ChartsController } from './plugin/js/qdrCharts.js';
import { AboutController } from './plugin/js/qdrAbout.js';
import { posint } from './plugin/js/posintDirective.js';

(function (QDR) {

  /**
   * This plugin's angularjs module instance
   */
  QDR.module = angular.module('QDR', ['ngRoute', 'ngSanitize', 'ngResource', 'ui.bootstrap',
    'ui.grid', 'ui.grid.selection', 'ui.grid.autoResize', 'ui.grid.resizeColumns', 'ui.grid.saveState',
    'ui.slider', 'ui.checkbox', 'patternfly.charts', 'patternfly.card', 'patternfly.modals']);

  // set up the routing for this plugin
  QDR.module.config(function ($routeProvider) {
    $routeProvider
      .when('/', {
        templateUrl: QDRTemplatePath + 'qdrOverview.html'
      })
      .when('/overview', {
        templateUrl: QDRTemplatePath + 'qdrOverview.html'
      })
      .when('/topology', {
        templateUrl: QDRTemplatePath + 'qdrTopology.html'
      })
      .when('/list', {
        templateUrl: QDRTemplatePath + 'qdrList.html'
      })
      .when('/schema', {
        templateUrl: QDRTemplatePath + 'qdrSchema.html'
      })
      .when('/charts', {
        templateUrl: QDRTemplatePath + 'qdrCharts.html'
      })
      .when('/chord', {
        templateUrl: QDRTemplatePath + 'qdrChord.html'
      })
      .when('/connect', {
        templateUrl: QDRTemplatePath + 'qdrConnect.html'
      });
  });

  QDR.module.config(function ($compileProvider) {
    $compileProvider.aHrefSanitizationWhitelist(/^\s*(https?|ftp|mailto|chrome-extension|file|blob):/);
    $compileProvider.imgSrcSanitizationWhitelist(/^\s*(https?|ftp|mailto|chrome-extension):/);
  });

  QDR.module.filter('to_trusted', ['$sce', function ($sce) {
    return function (text) {
      return $sce.trustAsHtml(text + '');
    };
  }]);

  QDR.module.filter('humanify', ['QDRService', function (QDRService) {
    return function (input) {
      return QDRService.utilities.humanify(input);
    };
  }]);

  QDR.module.filter('Pascalcase', function () {
    return function (str) {
      if (!str)
        return '';
      return str.replace(/(\w)(\w*)/g,
        function (g0, g1, g2) { return g1.toUpperCase() + g2.toLowerCase(); });
    };
  });

  QDR.module.filter('safePlural', function () {
    return function (str) {
      var es = ['x', 'ch', 'ss', 'sh'];
      for (var i = 0; i < es.length; ++i) {
        if (str.endsWith(es[i]))
          return str + 'es';
      }
      if (str.endsWith('y'))
        return str.substr(0, str.length - 2) + 'ies';
      if (str.endsWith('s'))
        return str;
      return str + 's';
    };
  });

  QDR.module.filter('pretty', function () {
    return function (str) {
      var formatComma = d3.format(',');
      if (!isNaN(parseFloat(str)) && isFinite(str))
        return formatComma(str);
      return str;
    };
  });

  QDR.module.filter('truncate', function () {
    return function (str) {
      if (!isNaN(parseFloat(str)) && isFinite(str))
        return Math.round(str);
      return str;
    };
  });
  // one-time initialization happens in the run function
  // of our module
  QDR.module.run(['$rootScope', '$route', '$timeout', '$location', '$log', 'QDRService', 'QDRChartService', function ($rootScope, $route, $timeout, $location, $log, QDRService, QDRChartService) {
    let QDRLog = new QDRLogger($log, 'main');
    QDRLog.info('************* creating Dispatch Console ************');

    var curPath = $location.path();
    var org = curPath.substr(1);
    if (org && org.length > 0 && org !== 'connect') {
      $location.search('org', org);
    } else {
      $location.search('org', null);
    }
    QDR.queue = d3.queue;

    if (!QDRService.management.connection.is_connected()) {
      // attempt to connect to the host:port that served this page
      var host = $location.host();
      var port = $location.port();
      var search = $location.search();
      if (search.org) {
        if (search.org === 'connect')
          $location.search('org', 'overview');
      }
      var connectOptions = { address: host, port: port };
      QDRLog.info('Attempting AMQP over websockets connection using address:port of browser (' + host + ':' + port + ')');
      QDRService.management.connection.testConnect(connectOptions)
        .then(function () {
          // We didn't connect with reconnect: true flag.
          // The reason being that if we used reconnect:true and the connection failed, rhea would keep trying. There
          // doesn't appear to be a way to tell it to stop trying to reconnect.
          QDRService.disconnect();
          QDRLog.info('Connect succeeded. Using address:port of browser');
          connectOptions.reconnect = true;
          // complete the connection (create the sender/receiver)
          QDRService.connect(connectOptions)
            .then(function () {
              // register a callback for when the node list is available (needed for loading saved charts)
              QDRService.management.topology.addUpdatedAction('initChartService', function () {
                QDRService.management.topology.delUpdatedAction('initChartService');
                QDRChartService.init(); // initialize charting service after we are connected
              });
              // get the list of nodes
              QDRService.management.topology.startUpdating(false);
            });
        }, function () {
          QDRLog.info('failed to auto-connect to ' + host + ':' + port);
          QDRLog.info('redirecting to connect page');
          $timeout(function () {
            $location.path('/connect');
            $location.search('org', org);
            $location.replace();
          });
        });
    }

    $rootScope.$on('$routeChangeSuccess', function () {
      var path = $location.path();
      if (path !== '/connect') {
        localStorage[QDR_LAST_LOCATION] = path;
      }
    });
  }]);

  QDR.module.controller('QDR.MainController', ['$scope', '$log', '$location', function ($scope, $log, $location) {
    let QDRLog = new QDRLogger($log, 'MainController');
    QDRLog.debug('started QDR.MainController with location.url: ' + $location.url());
    QDRLog.debug('started QDR.MainController with window.location.pathname : ' + window.location.pathname);
    $scope.topLevelTabs = [];
    $scope.topLevelTabs.push({
      id: 'qdr',
      content: 'Qpid Dispatch Router Console',
      title: 'Dispatch Router Console',
      isValid: function () { return true; },
      href: function () { return '#connect'; },
      isActive: function () { return true; }
    });
  }]);

  QDR.module.controller('QDR.Core', function ($scope, $rootScope, $timeout, QDRService) {
    $scope.alerts = [];
    $scope.breadcrumb = {};
    $scope.closeAlert = function (index) {
      $scope.alerts.splice(index, 1);
    };
    $scope.$on('setCrumb', function (event, data) {
      $scope.breadcrumb = data;
    });
    $scope.$on('newAlert', function (event, data) {
      $scope.alerts.push(data);
      $scope.$apply();
    });
    $scope.$on('clearAlerts', function () {
      $scope.alerts = [];
      $scope.$apply();
    });
    $scope.pageMenuClicked = function () {
      $rootScope.$broadcast('pageMenuClicked');
    };
    $scope.logout = function () {
      QDRService.disconnect();
      location.href = "#/connect";
    };
    $scope.user = '';
    let onConnected = function () {
      if (!QDRService.management.connection.is_connected()) {
        setTimeout(onConnected, 1000);
        return;
      }
      QDRService.management.connection.addDisconnectAction(onDisconnect);
      let parts = QDRService.management.connection.getReceiverAddress().split("/");
      parts[parts.length - 1] = "$management";
      let router = parts.join('/');
      QDRService.management.topology.fetchEntity(router, "connection", [], function (nodeId, entity, response) {
        $timeout(function () {
          response.results.some(result => {
            let c = QDRService.utilities.flatten(response.attributeNames, result);
            if (QDRService.utilities.isConsole(c)) {
              $scope.user = c.user;
              return true;
            }
            return false;
          });
        });
      });
    };

    let onDisconnect = function () {
      QDRService.management.connection.addConnectAction(onConnected);
      $timeout(() => {
        $scope.user = '';
      });
    };
    onDisconnect();
  });

  QDR.module.controller('QDR.NavBarController', NavBarController);
  QDR.module.controller('QDR.OverviewController', OverviewController);
  QDR.module.controller('QDR.OverviewChartsController', OverviewChartsController);
  QDR.module.controller('QDR.OverviewLogsController', OverviewLogsController);
  QDR.module.controller('QDR.TopAddressesController', TopAddressesController);
  QDR.module.controller('QDR.ChartDialogController', ChartDialogController);
  QDR.module.controller('QDR.DetailDialogController', DetailDialogController);
  QDR.module.controller('QDR.SettingsController', SettingsController);
  QDR.module.controller('QDR.TopologyController', TopologyController);
  QDR.module.controller('QDR.ChordController', ChordController);
  QDR.module.controller('QDR.ListController', ListController);
  QDR.module.controller('QDR.SchemaController', SchemaController);
  QDR.module.controller('QDR.ChartsController', ChartsController);
  QDR.module.controller('QDR.AboutController', AboutController);

  QDR.module.service('QDRService', QDRService);
  QDR.module.service('QDRChartService', QDRChartService);
  QDR.module.directive('posint', posint);
  QDR.module.directive('subTable', SubTable.create);
  //  .directive('exampleDirective', () => new ExampleDirective);
}({}));


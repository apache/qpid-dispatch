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
'use strict';
/* global angular d3 */

/**
 * @module QDR
 * @main QDR
 *
 * The main entry point for the QDR module
 *
 */
var QDR = (function(QDR) {

  /**
   * @property pluginName
   * @type {string}
   *
   * The name of this plugin
   */
  QDR.pluginName = 'QDR';
  QDR.pluginRoot = '';
  QDR.isStandalone = true;

  /**
   * @property log
   * @type {Logging.Logger}
   *
   * This plugin's logger instance
   */
  //HIO QDR.log = Logger.get(QDR.pluginName);
  /**
   * @property templatePath
   * @type {string}
   *
   * The top level path to this plugin's partials
   */
  QDR.srcBase = 'node_modules/dispatch-console-pages/dist/';
  QDR.templatePath = QDR.srcBase + 'html/';
  /**
   * @property SETTINGS_KEY
   * @type {string}
   *
   * The key used to fetch our settings from local storage
   */
  QDR.SETTINGS_KEY = 'QDRSettings';
  QDR.LAST_LOCATION = 'QDRLastLocation';

  QDR.redirectWhenConnected = function ($location, org) {
    $location.path(QDR.pluginRoot + '/connect');
    $location.search('org', org);
  };

  /**
   * @property module
   * @type {object}
   *
   * This plugin's angularjs module instance
   */
  QDR.module = angular.module(QDR.pluginName, ['ngRoute', 'ngSanitize', 'ngResource', 'ui.bootstrap',
    'ui.grid', 'ui.grid.selection', 'ui.grid.autoResize', 'ui.grid.resizeColumns', 'ui.grid.saveState', 'ui.slider', 'ui.checkbox']);

  // set up the routing for this plugin
  QDR.module.config(function($routeProvider) {
    $routeProvider
      .when('/', {
        templateUrl: QDR.templatePath + 'qdrOverview.html'
      })
      .when('/overview', {
        templateUrl: QDR.templatePath + 'qdrOverview.html'
      })
      .when('/topology', {
        templateUrl: QDR.templatePath + 'qdrTopology.html'
      })
      .when('/list', {
        templateUrl: QDR.templatePath + 'qdrList.html'
      })
      .when('/schema', {
        templateUrl: QDR.templatePath + 'qdrSchema.html'
      })
      .when('/charts', {
        templateUrl: QDR.templatePath + 'qdrCharts.html'
      })
      .when('/chord', {
        templateUrl: 'plugin/html/qdrChord.html'
      })
      .when('/connect', {
        templateUrl: QDR.templatePath + 'qdrConnect.html'
      });
  });

  QDR.module.config(function ($compileProvider) {
    $compileProvider.aHrefSanitizationWhitelist(/^\s*(https?|ftp|mailto|chrome-extension|file|blob):/);
    $compileProvider.imgSrcSanitizationWhitelist(/^\s*(https?|ftp|mailto|chrome-extension):/);
    /*    var cur = $compileProvider.urlSanitizationWhitelist();
    $compileProvider.urlSanitizationWhitelist(/^\s*(https?|ftp|mailto|file|blob):/);
    cur = $compileProvider.urlSanitizationWhitelist();
*/
  });

  QDR.module.filter('to_trusted', ['$sce', function($sce){
    return function(text) {
      return $sce.trustAsHtml(text);
    };
  }]);

  QDR.module.filter('humanify', function (QDRService) {
    return function (input) {
      return QDRService.utilities.humanify(input);
    };
  });

  QDR.module.filter('Pascalcase', function () {
    return function (str) {
      if (!str)
        return '';
      return str.replace(/(\w)(\w*)/g,
        function(g0,g1,g2){return g1.toUpperCase() + g2.toLowerCase();});
    };
  });

  QDR.module.filter('safePlural', function () {
    return function (str) {
      var es = ['x', 'ch', 'ss', 'sh'];
      for (var i=0; i<es.length; ++i) {
        if (str.endsWith(es[i]))
          return str + 'es';
      }
      if (str.endsWith('y'))
        return str.substr(0, str.length-2) + 'ies';
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

  QDR.logger = function ($log) {
    var log = $log;

    this.debug = function (msg) { msg = 'QDR: ' + msg; log.debug(msg);};
    this.error = function (msg) {msg = 'QDR: ' + msg; log.error(msg);};
    this.info = function (msg) {msg = 'QDR: ' + msg; log.info(msg);};
    this.warn = function (msg) {msg = 'QDR: ' + msg; log.warn(msg);};

    return this;
  };
  // one-time initialization happens in the run function
  // of our module
  QDR.module.run( ['$rootScope', '$route', '$timeout', '$location', '$log', 'QDRService', 'QDRChartService',  function ($rootScope, $route, $timeout, $location, $log, QDRService, QDRChartService) {
    QDR.log = new QDR.logger($log);
    QDR.log.info('************* creating Dispatch Console ************');
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
      var connectOptions = {address: host, port: port};
      QDR.log.info('Attempting AMQP over websockets connection using address:port of browser ('+host+':'+port+')');
      QDRService.management.connection.testConnect(connectOptions)
        .then( function () {
          // We didn't connect with reconnect: true flag.
          // The reason being that if we used reconnect:true and the connection failed, rhea would keep trying. There
          // doesn't appear to be a way to tell it to stop trying to reconnect.
          QDRService.disconnect();
          QDR.log.info('Connect succeeded. Using address:port of browser');
          connectOptions.reconnect = true;
          // complete the connection (create the sender/receiver)
          QDRService.connect(connectOptions)
            .then( function () {
              // register a callback for when the node list is available (needed for loading saved charts)
              QDRService.management.topology.addUpdatedAction('initChartService', function() {
                QDRService.management.topology.delUpdatedAction('initChartService');
                QDRChartService.init(); // initialize charting service after we are connected
              });
              // get the list of nodes
              QDRService.management.topology.startUpdating(false);
            });
        }, function () {
          QDR.log.info('failed to auto-connect to ' + host + ':' + port);
          QDR.log.info('redirecting to connect page');
          $timeout(function () {
            $location.path('/connect');
            $location.search('org', org);
            $location.replace();
          });
        });
    }

    $rootScope.$on('$routeChangeSuccess', function() {
      var path = $location.path();
      if (path !== '/connect') {
        localStorage[QDR.LAST_LOCATION] = path;
      }
    });
  }]);

  QDR.module.controller ('QDR.MainController', ['$scope', '$location', function ($scope, $location) {
    QDR.log.debug('started QDR.MainController with location.url: ' + $location.url());
    QDR.log.debug('started QDR.MainController with window.location.pathname : ' + window.location.pathname);
    $scope.topLevelTabs = [];
    $scope.topLevelTabs.push({
      id: 'qdr',
      content: 'Qpid Dispatch Router Console',
      title: 'Dispatch Router Console',
      isValid: function() { return true; },
      href: function() { return '#connect'; },
      isActive: function() { return true; }
    });
  }]);

  QDR.module.controller ('QDR.Core', function ($scope) {
    $scope.alerts = [];
    $scope.breadcrumb = {};
    $scope.closeAlert = function(index) {
      $scope.alerts.splice(index, 1);
    };
    $scope.$on('setCrumb', function(event, data) {
      $scope.breadcrumb = data;
    });
    $scope.$on('newAlert', function(event, data) {
      $scope.alerts.push(data);
      $scope.$apply();
    });
    $scope.$on('clearAlerts', function () {
      $scope.alerts = [];
      $scope.$apply();
    });

  });

  return QDR;
}(QDR || {}));

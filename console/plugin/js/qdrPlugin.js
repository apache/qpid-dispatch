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
 * @main QDR
 *
 * The main entrypoint for the QDR module
 *
 */
var QDR = (function(QDR) {

  /**
   * @property pluginName
   * @type {string}
   *
   * The name of this plugin
   */
  QDR.pluginName = "QDR";

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
  QDR.srcBase = "../dispatch/plugin/";
  QDR.templatePath = QDR.srcBase + "html/";
  QDR.cssPath = QDR.srcBase + "css/";

  /**
   * @property SETTINGS_KEY
   * @type {string}
   *
   * The key used to fetch our settings from local storage
   */
  QDR.SETTINGS_KEY = 'QDRSettings';
  QDR.LAST_LOCATION = "QDRLastLocation";

  /**
   * @property module
   * @type {object}
   *
   * This plugin's angularjs module instance
   */
  QDR.module = angular.module(QDR.pluginName, ['ngAnimate', 'ngResource', 'ngRoute', 'ui.grid', 'ui.grid.selection',
    'ui.grid.autoResize', 'jsonFormatter', 'ui.bootstrap', 'ui.slider'/*, 'minicolors' */]);

  // set up the routing for this plugin
  QDR.module.config(function($routeProvider) {
    $routeProvider
      .when('/', {
        templateUrl: QDR.templatePath + 'qdrConnect.html'
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
      .when('/connect', {
          templateUrl: QDR.templatePath + 'qdrConnect.html'
        })
  });

  QDR.module.config(['$compileProvider', function ($compileProvider) {
	var cur = $compileProvider.aHrefSanitizationWhitelist();
    $compileProvider.aHrefSanitizationWhitelist(/^\s*(https?|ftp|mailto|file|blob):/);
	cur = $compileProvider.aHrefSanitizationWhitelist();
  }]);

	QDR.module.config(function (JSONFormatterConfigProvider) {
		// Enable the hover preview feature
        JSONFormatterConfigProvider.hoverPreviewEnabled = true;
	});

	QDR.module.filter('to_trusted', ['$sce', function($sce){
          return function(text) {
              return $sce.trustAsHtml(text);
          };
    }]);

	QDR.module.filter('humanify', function (QDRService) {
		return function (input) {
			return QDRService.humanify(input);
		};
	});

	QDR.logger = function ($log) {
		var log = $log;

		this.debug = function (msg) { msg = "QDR: " + msg; log.debug(msg)};
		this.error = function (msg) {msg = "QDR: " + msg; log.error(msg)}
		this.info = function (msg) {msg = "QDR: " + msg; log.info(msg)}
		this.warn = function (msg) {msg = "QDR: " + msg; log.warn(msg)}

		return this;
	}
    // one-time initialization happens in the run function
    // of our module
	QDR.module.run( ["$rootScope", "$location", "$log", "QDRService", "QDRChartService",  function ($rootScope, $location, $log, QDRService, QDRChartService) {
		QDR.log = new QDR.logger($log);
		QDR.log.debug("QDR.module.run()")

		QDRService.initProton();
		var settings = angular.fromJson(localStorage[QDR.SETTINGS_KEY]);
		var lastLocation = localStorage[QDR.LAST_LOCATION];
		if (!angular.isDefined(lastLocation))
			lastLocation = "/overview";

		QDRService.addConnectAction(function() {
			QDRChartService.init(); // initialize charting service after we are connected
		});
		if (settings && settings.autostart) {
			QDRService.addConnectAction(function() {
				$location.path(lastLocation);
				$location.replace();
				$rootScope.$apply();
			});
			QDRService.connect(settings);
        } else {
			setTimeout(function () {
	            $location.url('/connect')
				$location.replace();
			}, 100)
        }

        $rootScope.$on('$routeChangeSuccess', function() {
            localStorage[QDR.LAST_LOCATION] = $location.$$path;
        });


	}]);

	QDR.module.controller ("QDR.MainController", ['$scope', '$location', function ($scope, $location) {
		QDR.log.debug("started QDR.MainController with location.url: " + $location.url());
		QDR.log.debug("started QDR.MainController with window.location.pathname : " + window.location.pathname);
		$scope.topLevelTabs = [];
		$scope.topLevelTabs.push({
			id: "qdr",
			content: "Qpid Dispatch Router Console",
			title: "Dispatch Router Console",
			isValid: function() { return true; },
			href: function() { return "#connect"; },
			isActive: function() { return true; }
		});
	}])

	QDR.module.controller ("QDR.Core", function ($scope, $rootScope) {
		$scope.alerts = [];
		$scope.closeAlert = function(index) {
            $scope.alerts.splice(index, 1);
        };
		$scope.$on('newAlert', function(event, data) {
			$scope.alerts.push(data);
			$scope.$apply();
		});
		$scope.$on("clearAlerts", function () {
			$scope.alerts = [];
			$scope.$apply();
		})

	})

  return QDR;
}(QDR || {}));

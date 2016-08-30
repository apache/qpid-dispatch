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
  QDR.pluginName = "QDR";
  QDR.pluginRoot = "";
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
  QDR.srcBase = "plugin/";
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
  QDR.module = angular.module(QDR.pluginName, ['ngResource', 'ngGrid', 'ui.bootstrap', 'ui.slider'/*, 'minicolors' */]);

  Core = {
	  notification: function (severity, msg) {
        $.notify(msg, severity);
    }
  }

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
      .otherwise({
          templateUrl: QDR.templatePath + 'qdrConnect.html'
        })
  });

	QDR.module.config(function ($compileProvider) {
		var cur = $compileProvider.urlSanitizationWhitelist();
		$compileProvider.urlSanitizationWhitelist(/^\s*(https?|ftp|mailto|file|blob):/);
		cur = $compileProvider.urlSanitizationWhitelist();
	})

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

	QDR.module.filter('Pascalcase', function () {
		return function (str) {
			if (!str)
				return "";
			return str.replace(/(\w)(\w*)/g,
			function(g0,g1,g2){return g1.toUpperCase() + g2.toLowerCase();});
		}
	})

    QDR.module.filter('safePlural', function () {
	        return function (str) {
				var es = ['x', 'ch', 'ss', 'sh']
				for (var i=0; i<es.length; ++i) {
					if (str.endsWith(es[i]))
						return str + 'es'
				}
				if (str.endsWith('y'))
					return str.substr(0, str.length-2) + 'ies'
				if (str.endsWith('s'))
					return str;
				return str + 's'
	        }
	})

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
	QDR.module.run( ["$rootScope", '$route', '$timeout', "$location", "$log", "QDRService", "QDRChartService",  function ($rootScope, $route, $timeout, $location, $log, QDRService, QDRChartService) {
		QDR.log = new QDR.logger($log);
		QDR.log.info("*************creating Dispatch Console************");
		var curPath = $location.path()
		var org = curPath.substr(1)
		if (org && org.length > 0 && org !== "connect") {
			$location.search('org', org)
		} else {
			$location.search('org', null)
		}

		QDRService.initProton();
		var settings = angular.fromJson(localStorage[QDR.SETTINGS_KEY]);
		QDRService.addConnectAction(function() {
			QDRChartService.init(); // initialize charting service after we are connected
		});
		if (settings && settings.autostart) {
			QDRService.addDisconnectAction( function () {
				$timeout(function () {
					var lastLocation = localStorage[QDR.LAST_LOCATION] || "/overview";
					org = lastLocation.substr(1)
					$location.path("/connect");
					$location.search('org', org)
				})
			})
			QDRService.addConnectAction(function() {
	            var searchObject = $location.search();
				// the redirect will be handled by QDRService when connected
	            if (searchObject.org) {
					return;
	            }
				// there was no org= parameter, so redirect to last known location
	            $timeout(function () {
					var lastLocation = localStorage[QDR.LAST_LOCATION] || "/overview";
					$location.path(lastLocation);
				})
			});
			QDRService.connect(settings);
        } else {
            $timeout(function () {
				$location.path('/connect')
				$location.search('org', org)
            })
        }

        $rootScope.$on('$routeChangeSuccess', function() {
            var path = $location.path();
			if (path !== "/connect") {
	            localStorage[QDR.LAST_LOCATION] = path;
			}
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

var Folder = (function () {
    function Folder(title) {
        this.title = title;
		this.children = [];
		this.folder = true;
    }
    return Folder;
})();
var Leaf = (function () {
    function Leaf(title) {
        this.title = title;
    }
    return Leaf;
})();

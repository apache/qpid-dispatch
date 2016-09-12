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
  QDR.pluginName = 'dispatch_hawtio_console';
  QDR.pluginRoot = "/" + QDR.pluginName;
  /**
   * @property log
   * @type {Logging.Logger}
   *
   * This plugin's logger instance
   */
  QDR.log = Logger.get('QDR');

  /**
   * @property contextPath
   * @type {string}
   *
   * The top level path of this plugin on the server
   *
   */
  QDR.contextPath = "/dispatch-hawtio-console/";

  /**
   * @property templatePath
   * @type {string}
   *
   * The path to this plugin's partials
   */
  QDR.templatePath = QDR.contextPath + "plugin/html/";

  QDR.SETTINGS_KEY = 'QDRSettings';
  QDR.LAST_LOCATION = "QDRLastLocationHawt";

  /**
   * @property module
   * @type {object}
   *
   * This plugin's angularjs module instance.  This plugin only
   * needs hawtioCore to run, which provides services like
   * workspace, viewRegistry and layoutFull used by the
   * run function
   */
  QDR.module = angular.module(QDR.pluginName, ['bootstrap', 'hawtio-ui', 'hawtio-forms', 'ui.bootstrap.dialog', 'hawtioCore'])
      .config(function($routeProvider) {
        /**
         * Here we define the route for our plugin.  One note is
         * to avoid using 'otherwise', as hawtio has a handler
         * in place when a route doesn't match any routes that
         * routeProvider has been configured with.
         */
		 $routeProvider
			.when(QDR.pluginRoot, {
				templateUrl: QDR.templatePath + 'qdrConnect.html'
			})
			.when(QDR.pluginRoot + '/', {
				templateUrl: QDR.templatePath + 'qdrConnect.html'
			})
			.when(QDR.pluginRoot + '/connect', {
				templateUrl: QDR.templatePath + 'qdrConnect.html'
			})
			.when(QDR.pluginRoot + '/overview', {
				templateUrl: QDR.templatePath + 'qdrOverview.html'
			})
			.when(QDR.pluginRoot + '/topology', {
				templateUrl: QDR.templatePath + 'qdrTopology.html'
			})
			.when(QDR.pluginRoot + '/list', {
				templateUrl: QDR.templatePath + 'qdrList.html'
			})
			.when(QDR.pluginRoot + '/schema', {
				templateUrl: QDR.templatePath + 'qdrSchema.html'
			})
			.when(QDR.pluginRoot + '/charts', {
				templateUrl: QDR.templatePath + 'qdrCharts.html'
			})
      })
	  .config(function ($compileProvider) {
			var cur = $compileProvider.urlSanitizationWhitelist();
			$compileProvider.urlSanitizationWhitelist(/^\s*(https?|ftp|mailto|file|blob):/);
			cur = $compileProvider.urlSanitizationWhitelist();
	  })
	  .config(function( $controllerProvider, $provide, $compileProvider ) {

	  })
	  .filter('to_trusted', function($sce){
			return function(text) {
			debugger;
				return $sce.trustAsHtml(text);
			};
      })
      .filter('humanify', function (QDRService) {
			return function (input) {
				return QDRService.humanify(input);
			};
	  })
      .filter('shortName', function () {
			return function (name) {
				var nameParts = name.split('/')
				return nameParts.length > 1 ? nameParts[nameParts.length-1] : name;
			};
	  })
	  .filter('Pascalcase', function () {
	        return function (str) {
				if (!str)
					return "";
	            return str.replace(/(\w)(\w*)/g,
                        function(g0,g1,g2){return g1.toUpperCase() + g2.toLowerCase();});
	        }
	  })
	  .filter('safePlural', function () {
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
/*
	QDR.module.config(['$locationProvider', function($locationProvider) {
        $locationProvider.html5Mode(true);
    }]);
*/
  /**
   * Here we define any initialization to be done when this angular
   * module is bootstrapped.  In here we do a number of things:
   *
   * 1.  We log that we've been loaded (kinda optional)
   * 2.  We load our .css file for our views
   * 3.  We configure the viewRegistry service from hawtio for our
   *     route; in this case we use a pre-defined layout that uses
   *     the full viewing area
   * 4.  We configure our top-level tab and provide a link to our
   *     plugin.  This is just a matter of adding to the workspace's
   *     topLevelTabs array.
   */
  QDR.module.run(function(workspace, viewRegistry, layoutFull, $route, $rootScope, $location, localStorage, QDRService, QDRChartService) {
		QDR.log.info("*************creating Dispatch Console************");
		var curPath = $location.path()
		QDR.log.info("curPath is " + curPath)
		var lastLocation = localStorage[QDR.LAST_LOCATION] || "connect"
		if (lastLocation.startsWith(QDR.pluginRoot)) {
			lastLocation = lastLocation.substr(QDR.pluginRoot.length+1)
			if (lastLocation === '')
				lastLocation = "overview"
		}
		if (curPath.startsWith(QDR.pluginRoot)) {
			$location.path(QDR.pluginRoot + "/connect");
			var org = curPath.substr(QDR.pluginRoot.length + 1)
			if (org === '') {
				org = lastLocation
			}
			if (curPath === QDR.pluginRoot && (!org || org.length===0 || org !== 'connect')) {
				org = lastLocation
			}
			if (org === 'connect')
				$location.search('org', null);
			else if (org && org.length > 0) {
				$location.search('org', org)
			}
		}

		Core.addCSS(QDR.contextPath + "plugin/css/dispatch.css");
		Core.addCSS(QDR.contextPath + "plugin/css/plugin.css");
		//Core.addCSS("https://cdn.rawgit.com/mohsen1/json-formatter/master/dist/json-formatter.min.css");
		Core.addCSS("https://cdnjs.cloudflare.com/ajax/libs/jquery.tipsy/1.0.2/jquery.tipsy.css");
		Core.addCSS("https://code.jquery.com/ui/1.8.24/themes/base/jquery-ui.css");
		Core.addCSS("https://maxcdn.bootstrapcdn.com/font-awesome/4.5.0/css/font-awesome.min.css");

		// tell hawtio that we have our own custom layout for
		// our view
		viewRegistry[QDR.pluginName] = QDR.templatePath + "qdrLayout.html";

		var settings = angular.fromJson(localStorage[QDR.SETTINGS_KEY]);
		QDRService.addConnectAction(function() {
			QDRChartService.init(); // initialize charting service after we are connected
		});
		if (settings && settings.autostart) {
			QDRService.addDisconnectAction( function () {
				$location.path(QDR.pluginRoot + "/connect");
				$location.replace();
				$rootScope.$apply();
			})
			QDRService.addConnectAction(function() {
	            var searchObject = $location.search();
				// the redirect will be handled by QDRService when connected
	            if (searchObject.org) {
					return;
	            }

				if ($location.path().startsWith(QDR.pluginRoot)) {
					var lastLocation = localStorage[QDR.LAST_LOCATION];
					if (!angular.isDefined(lastLocation))
						lastLocation = QDR.pluginRoot + "/overview";
					$location.path(lastLocation);
					$location.replace();
					$rootScope.$apply();
				}
			});
			QDRService.connect(settings);
        }

        $rootScope.$on('$routeChangeSuccess', function() {
            var path = $location.path();
			if (path.startsWith(QDR.pluginRoot)) {
				if (path !== QDR.pluginRoot && path !== (QDR.pluginRoot + "/") && path !== (QDR.pluginRoot + "/connect")) {
		            localStorage[QDR.LAST_LOCATION] = path;
QDR.log.info("saving page changed to " + path)
				}
			}
        });

		$rootScope.$on( "$routeChangeStart", function(event, next, current) {
			if (next && next.templateUrl == QDR.templatePath + "qdrConnect.html" && QDRService.connected) {
				// clicked connect from another dispatch page
				if (current && current.loadedTemplateUrl && current.loadedTemplateUrl.startsWith(QDR.contextPath)) {
					return;
				}
				// clicked the Dispatch Router top level tab from a different plugin
				var lastLocation = localStorage[QDR.LAST_LOCATION] || (QDR.pluginRoot + "/overview");
				// show the last page visited
QDR.log.info("showing dispatch tab: going to page " + lastLocation)
				$location.path(lastLocation)
			}
	    });

    workspace.topLevelTabs.push({
      id: "dispatch",
      content: "Dispatch Router",
      title: "Dispatch console",
      isValid: function(workspace) { return true; },
      href: function() { return "#/" + QDR.pluginName; },
      isActive: function(workspace) { return workspace.isLinkActive(QDR.pluginName); }
    });

  });

  return QDR;

})(QDR || {});

// force an more modern version of d3 to load
$.getScript('https://cdnjs.cloudflare.com/ajax/libs/d3/3.5.14/d3.min.js', function() {});
// tooltips on the list page
$.getScript('https://cdn.rawgit.com/jaz303/tipsy/master/src/javascripts/jquery.tipsy.js', function() {});
// tooltips on the topology page
$.getScript('https://cdn.rawgit.com/briancray/tooltipsy/master/tooltipsy.min.js', function() {});
// download string as file
$.getScript('https://cdnjs.cloudflare.com/ajax/libs/FileSaver.js/2014-11-29/FileSaver.min.js', function() {});


// tell the hawtio plugin loader about our plugin so it can be
// bootstrapped with the rest of angular
hawtioPluginLoader.addModule(QDR.pluginName);




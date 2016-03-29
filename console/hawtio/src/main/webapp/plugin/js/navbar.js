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
   * @property breadcrumbs
   * @type {{content: string, title: string, isValid: isValid, href: string}[]}
   *
   * Data structure that defines the sub-level tabs for
   * our plugin, used by the navbar controller to show
   * or hide tabs based on some criteria
   */
  QDR.breadcrumbs = [
    {
        content: '<i class="icon-cogs"></i> Connect',
        title: "Connect to a router",
        isValid: function () { return true; },
        href: "#/dispatch_plugin/connect"
    },
    {
        content: '<i class="fa fa-home"></i> Overview',
        title: "View router overview",
        isValid: function (QDRService) { return QDRService.isConnected(); },
        href: "#/dispatch_plugin/overview"
      },
    {
        content: '<i class="icon-list "></i> Entities',
        title: "View the attributes of the router entities",
        isValid: function (QDRService) { return QDRService.isConnected(); },
        href: "#/dispatch_plugin/list"
      },
    {
        content: '<i class="icon-star-empty"></i> Topology',
        title: "View router network topology",
        isValid: function (QDRService) { return QDRService.isConnected(); },
        href: "#/dispatch_plugin/topology"
      },
/*
    {
        content: '<i class="icon-bar-chart"></i> Charts',
        title: "View charts",
        isValid: function (QDRService, $location) { return QDRService.isConnected(); },
        href: "#/dispatch_plugin/charts"
    },
*/
    {
        content: '<i class="icon-align-left"></i> Schema',
        title: "View dispatch schema",
        isValid: function (QDRService) { return QDRService.isConnected(); },
        href: "#/dispatch_plugin/schema",
        right: true

      }
  ];
  /**
   * @function NavBarController
   *
   * @param $scope
   * @param workspace
   *
   * The controller for this plugin's navigation bar
   *
   */
QDR.NavBarController = function($scope, QDRService, QDRChartService, $location, $routeParams) {
    $scope.breadcrumbs = QDR.breadcrumbs;
    $scope.isValid = function(link) {
      return link.isValid(QDRService, $location);
    };

    $scope.isActive = function(href) {
        return href.split("#")[1] == $location.path();
    };

    $scope.isRight = function (link) {
        return angular.isDefined(link.right);
    };

    $scope.hasChart = function (link) {
        if (link.href == "#/dispatch_plugin/charts") {
            return QDRChartService.charts.some(function (c) { return c.dashboard });
        }
    }

	$scope.isDashboardable = function () {
		return  ($location.path().indexOf("schema") < 0 && $location.path().indexOf("connect") < 0);
	}

	$scope.addToDashboardLink = function () {
		var href = "#" + $location.path();
		var size = angular.toJson({
                size_x: 2,
                size_y: 2
              });

        var routeParams = angular.toJson($routeParams);
        var title = "Dispatch Router";
	    return "/hawtio/#/dashboard/add?tab=dashboard" +
	          "&href=" + encodeURIComponent(href) +
	          "&routeParams=" + encodeURIComponent(routeParams) +
	          "&title=" + encodeURIComponent(title) +
	          "&size=" + encodeURIComponent(size);
    };

  };


  return QDR;

} (QDR || {}));

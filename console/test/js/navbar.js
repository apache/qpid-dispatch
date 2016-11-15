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
        href: "#" + QDR.pluginRoot + "/connect"
    },
    {
        content: '<i class="icon-star-empty"></i> Topology',
        title: "View router network topology",
        isValid: function (QDRService) { return QDRService.isConnected(); },
        href: "#" + QDR.pluginRoot + "/topology"
      },
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
  QDR.module.controller("QDR.NavBarController", ['$scope', 'QDRService', '$routeParams', '$location', function($scope, QDRService, $routeParams, $location) {
    $scope.breadcrumbs = QDR.breadcrumbs;
    $scope.isValid = function(link) {
      return link.isValid(QDRService, $location);
    };

    $scope.isActive = function(href) {
		// highlight the connect tab if we are on the root page
		if (($location.path() === QDR.pluginRoot) && (href.split("#")[1] === QDR.pluginRoot + "/connect"))
			return true
        return href.split("#")[1] == $location.path();
    };

    $scope.isRight = function (link) {
        return angular.isDefined(link.right);
    };

  }]);


  return QDR;

} (QDR || {}));

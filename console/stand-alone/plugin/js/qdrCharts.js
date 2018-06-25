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

/* global angular */

import { QDRTemplatePath, QDRRedirectWhenConnected } from './qdrGlobals.js';

export class ChartsController {
  constructor(QDRService, QDRChartService, $scope, $location, $timeout, $routeParams, $uibModal) {
    this.controllerName = 'QDR.ChartsController';

    let updateTimer = null;

    if (!QDRService.management.connection.is_connected()) {
      // we are not connected. we probably got here from a bookmark or manual page reload
      QDRRedirectWhenConnected($location, 'charts');
      return;
    }

    $scope.svgCharts = [];
    // create an svg object for each chart
    QDRChartService.charts.filter(function (chart) {return chart.dashboard;}).forEach(function (chart) {
      let svgChart = QDRChartService.pfAreaChart(chart, chart.id(), true);
      svgChart.zoomed = false;
      $scope.svgCharts.push(svgChart);
    });


    // redraw the chart every update period
    var updateCharts = function () {
      $scope.svgCharts.forEach(function (svgChart) {
        svgChart.tick(svgChart.chart.id()); // on this page we are using the chart.id() as the div id in which to render the chart
      });
      const updateRate = localStorage['updateRate'] ?  localStorage['updateRate'] : 1000;
      if (updateTimer) {
        clearTimeout(updateTimer);
      }
      updateTimer = setTimeout(updateCharts, updateRate);
    };

    // called by ng-init in the html when the page is loaded
    $scope.chartsLoaded = function () {
      // ensure the div for our chart is loaded in the dom
      let div = angular.element('.chartContainer');
      if (!div.width()) {
        setTimeout($scope.chartsLoaded, 100);
        return;
      }
      // create an svg object for each chart
      $scope.svgCharts.forEach ( function (c) {
        c.generate(380);
        QDRChartService.sendChartRequest(c.chart.request(), true);
      });
      if (updateTimer)
        clearTimeout(updateTimer);
      setTimeout(updateCharts);
    };

    $scope.zoomChart = function (chart) {
      chart.zoomed = !chart.zoomed;
      chart.zoom(chart.chart.id(), chart.zoomed);
    };
    $scope.showListPage = function () {
      $location.path('/list');
    };

    $scope.hasCharts = function () {
      return QDRChartService.numCharts() > 0;
    };

    $scope.editChart = function (chart) {
      doDialog('tmplChartConfig.html', chart.chart);
    };

    $scope.delChart = function (chart) {
      QDRChartService.unRegisterChart(chart.chart);
      // remove from svgCharts
      $scope.svgCharts.forEach(function (svgChart, i) {
        if (svgChart === chart) {
          delete $scope.svgCharts.splice(i, 1);
        }
      });
    };

    // called from dialog when we want to clone the dialog chart
    // the chart argument here is a QDRChartService chart
    $scope.addChart = function (chart) {
      let nchart = QDRChartService.pfAreaChart(chart, chart.id(), true);
      $scope.svgCharts.push(nchart);
      $timeout( function () {
        nchart.generate();
        QDRChartService.sendChartRequest(chart.request(), true);
      });
    };

    $scope.$on('$destroy', function() {
      if (updateTimer) {
        clearTimeout(updateTimer);
        updateTimer = null;
      }
      for (let i=$scope.svgCharts.length-1; i>=0; --i) {
        delete $scope.svgCharts.splice(i, 1);
      }
    });

    function doDialog(template, chart) {

      $uibModal.open({
        backdrop: true,
        keyboard: true,
        backdropClick: true,
        templateUrl: QDRTemplatePath + template,
        controller: 'QDR.ChartDialogController',
        resolve: {
          chart: function() {
            return chart;
          },
          updateTick: function () {
            return function () { return updateCharts; };
          },
          dashboard: function () {
            return $scope;
          },
          adding: function () {
            return false;
          }
        }
      });
    }
  }
}
ChartsController.$inject = ['QDRService', 'QDRChartService', '$scope', '$location', '$timeout', '$routeParams', '$uibModal'];


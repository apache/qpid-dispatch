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

export class ChartDialogController {
  constructor(QDRChartService, $scope, $location, $uibModalInstance, chart, updateTick, dashboard, adding) {
    this.controllerName = 'QDR.ChartDialogController';
    let dialogSvgChart = null;
    $scope.svgDivId = 'dialogEditChart';    // the div id for the svg chart

    let updateTimer = null;
    $scope.chart = chart;  // the underlying chart object from the dashboard
    $scope.dialogChart = $scope.chart.copy(); // the chart object for this dialog
    $scope.userTitle = $scope.chart.title();

    $scope.$watch('userTitle', function(newValue, oldValue) {
      if (newValue !== oldValue) {
        $scope.dialogChart.title(newValue);
        dialogSvgChart.tick($scope.svgDivId);
      }
    });
    $scope.$watch('dialogChart.areaColor', function (newValue, oldValue) {
      if (newValue !== oldValue) {
        if (dialogSvgChart) {
          dialogSvgChart.chart.areaColor = newValue;
          dialogSvgChart.tick($scope.svgDivId);
        }
      }
    });
    $scope.$watch('dialogChart.lineColor', function (newValue, oldValue) {
      if (newValue !== oldValue) {
        if (dialogSvgChart)
          dialogSvgChart.tick($scope.svgDivId);
      }
    });
    $scope.$watch('dialogChart.type', function (newValue, oldValue) {
      if (newValue !== oldValue) {
        if (dialogSvgChart) {
          dialogSvgChart.chart.visibleDuration = newValue === 'rate' ? 0.25 : 1;
          dialogSvgChart.tick($scope.svgDivId);
        }
      }
    });

    // the stored rateWindow is in milliseconds, but the slider is in seconds
    $scope.rateWindow = $scope.chart.rateWindow / 1000;

    $scope.addChartsPage = function () {
      QDRChartService.addDashboard(dialogSvgChart.chart);
    };
    $scope.delChartsPage = function () {
      QDRChartService.delDashboard($scope.chart);
    };

    $scope.showChartsPage = function () {
      cleanup();
      $uibModalInstance.close(true);
      $location.path('/charts');
    };

    var cleanup = function () {
      if (updateTimer) {
        clearTimeout(updateTimer);
        updateTimer = null;
      }
      if (!$scope.isOnChartsPage())
        QDRChartService.unRegisterChart($scope.dialogChart);     // remove the chart
    };
    $scope.okClick = function () {
      cleanup();
      $uibModalInstance.close(true);
    };

    var initRateSlider = function () {
      if (document.getElementById('rateSlider')) {
        $( '#rateSlider' ).slider({
          value: $scope.rateWindow,
          min: 1,
          max: 10,
          step: 1,
          slide: function( event, ui ) {
            $scope.rateWindow = ui.value;
            $scope.dialogChart.rateWindow = ui.value * 1000;
            $scope.$apply();
            if (dialogSvgChart)
              dialogSvgChart.tick($scope.svgDivId);
          }
        });
      } else {
        setTimeout(initRateSlider, 100);
      }
    };
    //initRateSlider();

    var initDurationSlider = function () {
      if (document.getElementById('durationSlider')) {
        $( '#durationSlider' ).slider({
          value: $scope.dialogChart.visibleDuration,
          min: 0.25,
          max: 10,
          step: 0.25,
          slide: function( event, ui ) {
            $scope.visibleDuration = $scope.dialogChart.visibleDuration = ui.value;
            $scope.$apply();
            if (dialogSvgChart)
              dialogSvgChart.tick($scope.svgDivId);
          }
        });
      } else {
        setTimeout(initDurationSlider, 100);
      }
    };
    initDurationSlider();

    $scope.adding = function () {
      return adding;
    };

    $scope.isOnChartsPage = function () {
      let chart = $scope.chart;
      if (adding)
        return QDRChartService.isAttrCharted(chart.nodeId(), chart.entity(), chart.name(), chart.attr(), chart.aggregate());
      else
        return $scope.chart.dashboard;
    };

    // handle the Apply button click
    // update the dashboard chart's properties
    $scope.apply = function () {
      $scope.chart.areaColor = $scope.dialogChart.areaColor;
      $scope.chart.lineColor = $scope.dialogChart.lineColor;
      $scope.chart.type = $scope.dialogChart.type;
      $scope.chart.rateWindow = $scope.rateWindow * 1000;
      $scope.chart.title($scope.dialogChart.title());
      $scope.chart.visibleDuration = $scope.dialogChart.visibleDuration;
      QDRChartService.saveCharts();
      if (typeof updateTick === 'function')
        updateTick();
    };

    // add a new chart to the dashboard based on the current dialog settings
    $scope.copyToDashboard = function () {
      let chart = $scope.dialogChart.copy();
      // set the new chart's dashboard state
      QDRChartService.addDashboard(chart);
      // notify the chart controller that it needs to display a new chart
      dashboard.addChart(chart);
    };

    // update the chart on the popup dialog
    var updateDialogChart = function () {
      // draw the chart using the current data
      if (dialogSvgChart)
        dialogSvgChart.tick($scope.svgDivId);

      // draw the chart again in 1 second
      const updateRate = localStorage['updateRate'] ? localStorage['updateRate'] : 1000;
      if (updateTimer)
        clearTimeout(updateTimer);
      updateTimer = setTimeout(updateDialogChart, updateRate);
    };

    var showChart = function () {
      // ensure the div for our chart is loaded in the dom
      let div = angular.element('#' + $scope.svgDivId);
      if (!div.width()) {
        setTimeout(showChart, 100);
        return;
      }
      dialogSvgChart = QDRChartService.pfAreaChart($scope.dialogChart, $scope.svgDivId, false, 550);
      if (updateTimer)
        clearTimeout(updateTimer);
      updateDialogChart();
    };
    showChart();

  }
}
ChartDialogController.$inject = ['QDRChartService', '$scope', '$location', '$uibModalInstance', 'chart', 'updateTick', 'dashboard', 'adding'];

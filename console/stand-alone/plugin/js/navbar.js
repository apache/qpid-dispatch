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
        href: "#!" + QDR.pluginRoot + "/connect",
        name: "Connect"
    },
    {
        content: '<i class="pficon-home"></i> Overview',
        title: "View router overview",
        isValid: function (QDRService) { return QDRService.isConnected(); },
        href: "#!" + QDR.pluginRoot + "/overview",
        name: "Overview"
      },
    {
        content: '<i class="icon-list "></i> Entities',
        title: "View the attributes of the router entities",
        isValid: function (QDRService) { return QDRService.isConnected(); },
        href: "#!" + QDR.pluginRoot + "/list",
        name: "Entities"
      },
    {
        content: '<i class="icon-star-empty"></i> Topology',
        title: "View router network topology",
        isValid: function (QDRService) { return QDRService.isConnected(); },
        href: "#!" + QDR.pluginRoot + "/topology",
        name: "Topology"
      },
    {
        content: '<i class="icon-bar-chart"></i> Charts',
        title: "View charts",
        isValid: function (QDRService, $location) { return QDRService.isConnected() && QDR.isStandalone; },
        href: "#!/charts",
        name: "Charts"
    },
    {
        content: '<i class="icon-align-left"></i> Schema',
        title: "View dispatch schema",
        isValid: function (QDRService) { return QDRService.isConnected(); },
        href: "#!" + QDR.pluginRoot + "/schema",
        right: true,
        name: "Schema"
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
  QDR.module.controller("QDR.NavBarController", ['$rootScope', '$scope', 'QDRService', 'QDRChartService', '$routeParams', '$location', function($rootScope, $scope, QDRService, QDRChartService, $routeParams, $location) {
    $scope.breadcrumbs = QDR.breadcrumbs;
    $scope.isValid = function(link) {
      if ($scope.isActive(link.href))
        $rootScope.$broadcast("setCrumb", {name: link.name, title: link.content})
      return link.isValid(QDRService, $location);
    };

    $scope.isActive = function(href) {
//QDR.log.info("isActive(" + href + ") location.path() is " + $location.path())
      // highlight the connect tab if we are on the root page
      if (($location.path() === QDR.pluginRoot) && (href.split("#")[1] === QDR.pluginRoot + "/connect")) {
//QDR.log.info("isActive is returning true for connect page")
        return true
      }
      return href.split("#")[1] === '!' + $location.path();
    };

    $scope.isRight = function (link) {
        return angular.isDefined(link.right);
    };

    $scope.hasChart = function (link) {
        if (link.href == "#/charts") {
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

  }]);

  // controller for the edit/configure chart dialog
  QDR.module.controller("QDR.ChartDialogController", function($scope, QDRChartService, $location, $uibModalInstance, chart, updateTick, dashboard, adding) {
    var dialogSvgChart = null;
    $scope.svgDivId = "dialogEditChart";    // the div id for the svg chart

    var updateTimer = null;
    $scope.chart = chart;  // the underlying chart object from the dashboard
    $scope.dialogChart = $scope.chart.copy(); // the chart object for this dialog
    $scope.userTitle = $scope.chart.title();

    $scope.$watch('userTitle', function(newValue, oldValue) {
    if (newValue !== oldValue) {
      $scope.dialogChart.title(newValue);
      dialogSvgChart.tick($scope.svgDivId);
    }
    })
    $scope.$watch("dialogChart.areaColor", function (newValue, oldValue) {
      if (newValue !== oldValue) {
        if (dialogSvgChart)
         dialogSvgChart.tick($scope.svgDivId);
      }
    })
    $scope.$watch("dialogChart.lineColor", function (newValue, oldValue) {
      if (newValue !== oldValue) {
        if (dialogSvgChart)
          dialogSvgChart.tick($scope.svgDivId);
      }
    })
    $scope.$watch("dialogChart.type", function (newValue, oldValue) {
      if (newValue !== oldValue) {
        if (dialogSvgChart)
          dialogSvgChart.tick($scope.svgDivId);
      }
    })

    // the stored rateWindow is in milliseconds, but the slider is in seconds
    $scope.rateWindow = $scope.chart.rateWindow / 1000;

    $scope.addChartsPage = function () {
      QDRChartService.addDashboard(dialogSvgChart.chart);
    };

    $scope.showChartsPage = function () {
      cleanup();
      $uibModalInstance.close(true);
      $location.path(QDR.pluginRoot + "/charts");
    };

    var cleanup = function () {
      if (updateTimer) {
        clearTimeout(updateTimer);
        updateTimer = null;
      }
      if (!$scope.isOnChartsPage())
        QDRChartService.unRegisterChart($scope.dialogChart);     // remove the chart
    }
    $scope.okClick = function () {
      cleanup();
      $uibModalInstance.close(true);
    };

    var initRateSlider = function () {
      if (document.getElementById('rateSlider')) {
        $( "#rateSlider" ).slider({
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
        setTimeout(initRateSlider, 100)
      }
    }
    initRateSlider();

    var initDurationSlider = function () {
      if (document.getElementById('durationSlider')) {
        $( "#durationSlider" ).slider({
          value: $scope.dialogChart.visibleDuration,
          min: 1,
          max: 10,
          step: 1,
          slide: function( event, ui ) {
            $scope.visibleDuration = $scope.dialogChart.visibleDuration = ui.value;
            $scope.$apply();
            if (dialogSvgChart)
              dialogSvgChart.tick($scope.svgDivId);
          }
        });
      } else {
        setTimeout(initDurationSlider, 100)
      }
    }
    initDurationSlider();

    $scope.adding = function () {
      return adding
    }

    $scope.isOnChartsPage = function () {
      if (adding)
        return dialogSvgChart ? dialogSvgChart.chart.dashboard : false;
      else
        return $scope.chart.dashboard
    }

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
      if (typeof updateTick === "function")
        updateTick();
    }

    // add a new chart to the dashboard based on the current dialog settings
    $scope.copyToDashboard = function () {
        var chart = $scope.dialogChart.copy();
        // set the new chart's dashboard state
        QDRChartService.addDashboard(chart);
        // notify the chart controller that it needs to display a new chart
        dashboard.addChart(chart);
    }

    // update the chart on the popup dialog
    var updateDialogChart = function () {
      // draw the chart using the current data
      if (dialogSvgChart)
          dialogSvgChart.tick($scope.svgDivId);

      // draw the chart again in 1 second
      var updateRate = localStorage['updateRate'] ? localStorage['updateRate'] : 5000;
      if (updateTimer)
      clearTimeout(updateTimer);
        updateTimer = setTimeout(updateDialogChart, updateRate);
    }

    var showChart = function () {
      // ensure the div for our chart is loaded in the dom
      var div = angular.element("#" + $scope.svgDivId);
      if (!div.width()) {
        setTimeout(showChart, 100);
        return;
      }
      dialogSvgChart = new QDRChartService.AreaChart($scope.dialogChart);
      $('input[name=lineColor]').val($scope.dialogChart.lineColor);
      $('input[name=areaColor]').val($scope.dialogChart.areaColor);
      $('input[name=areaColor]').on('input', function (e) {
        $scope.dialogChart.areaColor = $(this).val();
        updateDialogChart()
      })
      $('input[name=lineColor]').on('input', function (e) {
        $scope.dialogChart.lineColor = $(this).val();
        updateDialogChart()
      })
      if (updateTimer)
        clearTimeout(updateTimer);
          updateDialogChart();
    }
    showChart();
  });

  return QDR;

} (QDR || {}));

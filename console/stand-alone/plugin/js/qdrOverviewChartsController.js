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

export class OverviewChartsController {
  constructor(QDRService, QDRChartService, $scope, $timeout) {
    this.controllerName = 'QDR.OverviewChartsController';

    $scope.overviewCharts = [];
    let updateTimer;
    // milliseconds between updates
    const updateInterval = 1000;
    // called when it is time to update the chart's data
    var sum = function (request, saveResponse) {
      let attrs = angular.copy(request.overrideAttrs);
      let totalAttr = attrs.shift(); // remove totals attr
      let attrEntities = [{entity: request.entity, attrs: attrs}];
      QDRService.management.topology.fetchAllEntities(attrEntities, function (responses) {
        let total = 0;
        let response = {attributeNames: [totalAttr, 'name'], results: [[]]};
        let chart = charts.find( function (c) { return c.nodeId === request.nodeId;});
        // for each router
        for (let router in responses) {
          let record = responses[router][request.entity];
          let accessor = chart.accessor;
          // for each attribute-value (ie each address or each link)
          for (let i=0; i<record.results.length; i++) {
            total += accessor(record.attributeNames, record.results[i]);
          }
        }
        response.results[0][0] = total;
        response.results[0][1] = request.names()[0];
        saveResponse(request.nodeId, request.entity, response);
      });
    };

    let charts = [
      {
        nodeId:     '///Throughput/',
        entity:     'router.address',
        name:       'throughput',
        overrideAttrs: ['throughput', 'deliveriesEgress'],
        attr:       'throughput',
        userTitle:  'Deliveries per second',
        type:       'rate',
        hideLabel:  true,
        hideLegend:  true,
        hideGridLines: true,
        hideAxes: true,
        hideTitle: true,
        curveType: 'area',
        areaColor: '#0088ce',
        zeroFill:   true,
        visibleDuration: 1, // show data for the last 1 minute
        interval: updateInterval,
        forceCreate: true,
        accessor: function (attributes, results) {
          return results[attributes.indexOf('deliveriesEgress')];
        },
        override: sum  // called to fetch the chart data
      },
      {
        nodeId:       '///Outstanding-Deliveries/',
        entity:       'router.link',
        name:         'outstandingDeliveries',
        overrideAttrs:['outstandingDeliveries', 'undeliveredCount', 'unsettledCount', 'linkType', 'linkDir'],
        attr:         'outstandingDeliveries',
        userTitle:    'Deliveries in flight',
        hideLabel:  true,
        hideLegend:  true,
        hideGridLines: true,
        hideAxes: true,
        hideTitle: true,
        curveType: 'area',
        areaColor: '#33ff33',
        zeroFill:   true,
        visibleDuration: 1, // show data for the last 1 minute
        interval: updateInterval,
        forceCreate:  true,
        accessor: function (attributes, results) {
          return results[attributes.indexOf('linkType')] === 'endpoint' && results[attributes.indexOf('linkDir')] === 'out'
            ? results[attributes.indexOf('unsettledCount')] + results[attributes.indexOf('undeliveredCount')]
            : 0;
        },
        now: new Date(),
        override: sum  // called to fetch the chart data
      }
    ];
    $scope.overviewCharts = charts.map( function (chart) {
      let c = QDRChartService.registerChart(chart);
      let pfChart = QDRChartService.pfAreaChart(c, c.id(), true);
      pfChart.zeroFill = chart.zeroFill;
      return pfChart;
    });


    // redraw the chart every update period
    var updateCharts = function () {
      $timeout( function () {
        $scope.overviewCharts.forEach(function (svgChart) {
          svgChart.tick(svgChart.chart.id()); // on this page we are using the chart.id() as the div id in which to render the chart
        });
      });
    };

    var createCharts = function () {
      // create an svg object for each chart
      $scope.overviewCharts.forEach ( function (c) {
        // tell c3 to create the svg with undefined width and height of 60
        c.generate(undefined, 60);
      });
      // redraw the charts once every update interval
      const updateRate = localStorage['updateRate'] ?  localStorage['updateRate'] : updateInterval;
      updateTimer = setInterval(updateCharts, updateRate);
    };
    $timeout( function () {
      createCharts();
    });

    $scope.$on('$destroy', function() {
      if (updateTimer)
        clearInterval(updateTimer);
      $scope.overviewCharts.forEach( function (svg) {
        QDRChartService.unRegisterChart(svg.chart);
      });
    });
  }
}
OverviewChartsController.$inject = ['QDRService', 'QDRChartService', '$scope', '$timeout'];
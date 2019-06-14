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

export class DelayedDeliveriesController {
  constructor(QDRService, $scope, $timeout) {
    this.controllerName = "QDR.DelayedDeliveriesController";

    let rates = {};
    $scope.deliveriesData = [];
    $scope.delayedDeliverisGrid = {
      data: "deliveriesData",
      columnDefs: [
        {
          field: "router",
          displayName: "Router"
        },
        {
          field: "connection",
          displayName: "Connection"
        },
        {
          field: "deliveriesDelayed1SecRate",
          displayName: "1 sec rate",
          cellClass: "grid-align-value"
        },
        {
          field: "deliveriesDelayed10SecRate",
          displayName: "10 sec rate",
          cellClass: "grid-align-value"
        },
        {
          field: "capacity",
          displayName: "Capacity",
          cellClass: "grid-align-value"
        },
        {
          field: "unsettledCount",
          displayName: "Unsettled",
          cellClass: "grid-align-value"
        },
        {
          field: "Close",
          width: "4%",
          cellTemplate:
            '<div><button class="btn btn-danger" ng-click="grid.appScope.killConnection(row, $event)">Close</button></div>'
        }
      ],
      enableColumnResize: true,
      enableHorizontalScrollbar: 0,
      enableVerticalScrollbar: 0,
      multiSelect: false,
      enableSelectAll: false,
      enableSelectionBatchEvent: false,
      enableRowHeaderSelection: false,
      noUnselect: true
    };

    // get info for all links
    var allLinkInfo = function(link, callback) {
      let nodes = {};
      // gets called each node/entity response
      var gotNode = function(nodeName, entity, response) {
        if (!nodes[nodeName]) nodes[nodeName] = {};
        nodes[nodeName][entity] = angular.copy(response);
      };
      let links = [];
      // send the requests for all connection and router info for all routers
      QDRService.management.topology.fetchAllEntities(
        [{ entity: "router.link" }, { entity: "connection" }],
        function() {
          for (let node in nodes) {
            let response = nodes[node]["router.link"];
            response.results.forEach(function(result) {
              let link = QDRService.utilities.flatten(
                response.attributeNames,
                result
              );
              if (link.linkType === "endpoint") {
                link.router = QDRService.utilities.nameFromId(node);
                let connections = nodes[node]["connection"];
                connections.results.some(function(connection) {
                  let conn = QDRService.utilities.flatten(
                    connections.attributeNames,
                    connection
                  );
                  if (link.connectionId === conn.identity) {
                    link.connection = QDRService.utilities.clientName(conn);
                    return true;
                  }
                  return false;
                });
                let delayedRates = QDRService.utilities.rates(
                  link,
                  ["deliveriesDelayed1Sec", "deliveriesDelayed10Sec"],
                  rates,
                  link.name,
                  12 // average over 12 snapshots (each snapshot is 5 seconds apart)
                );
                link.deliveriesDelayed1SecRate = Math.round(
                  delayedRates.deliveriesDelayed1Sec,
                  1
                );
                link.deliveriesDelayed10SecRate = Math.round(
                  delayedRates.deliveriesDelayed10Sec,
                  1
                );
                /* The killConnection event handler (in qdrOverview.js) expects
                   a row object with a routerId and the identity of a connection. 
                   Here we set those attributes so that when killConnection is 
                   called, it will kill the link's connection
                */
                link.routerId = node;
                link.identity = link.connectionId;

                links.push(link);
              }
            });
          }
          if (links.length === 0) return;
          // update the grid's data
          links = links.filter(function(link) {
            return (
              link.deliveriesDelayed1SecRate > 0 ||
              link.deliveriesDelayed10SecRate > 0
            );
          });
          links.sort((a, b) => {
            if (a.deliveriesDelayed1SecRate > b.deliveriesDelayed1SecRate)
              return -1;
            else if (a.deliveriesDelayed1SecRate < b.deliveriesDelayed1SecRate)
              return 1;
            else return 0;
          });
          // take top 5 records
          links.splice(5);

          $scope.deliveriesData = links;
          callback(null);
        },
        gotNode
      );
    };
    let timer;
    var updateGrid = function() {
      $timeout(function() {
        allLinkInfo(null, function() {});
        expandGridToContent($scope.deliveriesData.length);
      });
    };
    timer = setInterval(updateGrid, 5000);
    updateGrid();

    var expandGridToContent = function(rows) {
      let height = (rows + 1) * 30 + 40; // header is 40px
      let gridDetails = $("#overview-controller .grid");
      gridDetails.css("height", height + "px");
    };

    $scope.anyLinks = function() {
      return $scope.deliveriesData.length > 0;
    };

    $scope.$on("$destroy", function() {
      if (timer) clearInterval(timer);
    });
  }
}
DelayedDeliveriesController.$inject = ["QDRService", "$scope", "$timeout"];

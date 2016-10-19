/*
 * Licensed under the Apache License, Version 2.0 (the 'License');
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an 'AS IS' BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
(function () {
  'use strict';

  angular
    .module('horizon.dashboard.dispatch.overv', [])
    .config(config)
    .run(addTemplates)

  config.$inject = [
    '$provide',
    '$windowProvider'
  ];

  addTemplates.$inject = [
    '$templateCache'
  ]

  /**
   * @name config
   * @param {Object} $provide
   * @param {Object} $windowProvider
   * @description Base path for the overview code
   * @returns {undefined} No return value
   */
  function config($provide, $windowProvider) {
    var path = $windowProvider.$get().STATIC_URL + 'dashboard/dispatch/overv/';
    $provide.constant('horizon.dashboard.dispatch.overv.basePath', path);
  }

  function addTemplates($templateCache) {
    $templateCache.put('dispatch/tplLinkRow.html',
      "<div" +
      "    ng-repeat=\"(colRenderIndex, col) in colContainer.renderedColumns track by col.uid\"" +
      "    ui-grid-one-bind-id-grid=\"rowRenderIndex + '-' + col.uid + '-cell'\"" +
      "    class=\"ui-grid-cell\"" +
      "    ng-class=\"{ 'ui-grid-row-header-cell': col.isRowHeader, linkDirIn: row.entity.linkDir=='in', linkDirOut: row.entity.linkDir=='out' }\"" +
      "    role=\"{{col.isRowHeader ? 'rowheader' : 'gridcell'}}\"" +
      "    ui-grid-cell>" +
      "</div>"
    );
    $templateCache.put('dispatch/links.html',
      "<h3>Links</h3>" +
      "<div class='grid' ui-grid='linksGrid' ui-grid-selection></div>"
    );
    $templateCache.put('dispatch/link.html',
      "<h3>Link {$ link.name $}</h3>" +
      "<div class='grid noHighlight' ui-grid='linkGrid' ui-grid-resize-columns></div>"
    );
    $templateCache.put('dispatch/overview.html',
      "<div id=\"overview-controller\" ng-controller=\"horizon.dashboard.dispatch.overv.OverviewController as ctrl\">" +
      "    <div id=\"overview_charts\" class=\"clearfix\">" +
      "        <div ng-repeat=\"chart in svgCharts\" id=\"{$ chart.chart.id() $}\" class=\"d3Chart\"></div>" +
      "    </div>" +
      "    <hr/>" +
      "    <div id=\"overview_dropdowns\" class=\"clearfix\">" +
      "        <div class=\"overview-dropdown\"" +
      "             ng-class=\"{selected1: selectedObject.type == 'router', selected: selectedObject.type == 'routers'}\">" +
      "            <div class=\"dropdown-entity\">Routers</div>" +
      "            <select id=\"selrouter\" ng-options=\"option.name for option in data.router.options track by option.id\"" +
      "                    ng-click=\"activated(data.router.sel)\" ng-model=\"data.router.sel\"></select>" +
      "        </div>" +
      "        <div class=\"overview-dropdown\"" +
      "            ng-class=\"{selected1: selectedObject.type == 'address', selected: selectedObject.type == 'addresses'}\">" +
      "            <div class=\"dropdown-entity\">Addresses</div>" +
      "            <select id=\"seladdress\" ng-options=\"option.name for option in data.address.options track by option.id\"" +
      "                ng-click=\"activated(data.address.sel)\" ng-model=\"data.address.sel\"></select>" +
      "        </div>" +
      "        <div class=\"overview-dropdown\"" +
      "            ng-class=\"{selected1: selectedObject.type == 'link', selected: selectedObject.type == 'links'}\">" +
      "            <div class=\"dropdown-entity\">Links</div>" +
      "            <select id=\"sellink\" ng-options=\"option.name for option in data.link.options track by option.id\"" +
      "                    ng-click=\"activated(data.link.sel)\" ng-model=\"data.link.sel\"></select>" +
      "        </div>" +
      "        <div class=\"overview-dropdown\"" +
      "            ng-class=\"{selected1: selectedObject.type == 'connection', selected: selectedObject.type == 'connections'}\">" +
      "            <div class=\"dropdown-entity\">Connections</div>" +
      "            <select id=\"selconnection\" ng-options=\"option.name for option in data.connection.options track by option.id\"" +
      "                    ng-click=\"activated(data.connection.sel)\" ng-model=\"data.connection.sel\"></select>" +
      "        </div>" +
      "        <div class=\"overview-dropdown\"" +
      "            ng-class=\"{selected1: selectedObject.type == 'log', selected: selectedObject.type == 'logs'}\">" +
      "            <div class=\"dropdown-entity\">Logs</div>" +
      "            <select id=\"sellog\" ng-options=\"option.name for option in data.log.options track by option.id\"" +
      "                    ng-click=\"activated(data.log.sel)\" ng-model=\"data.log.sel\"></select>" +
      "        </div>" +
      "    </div>" +
      "    <div ng-include=\"templateUrl\"></div>" +
      "    <div ng-init=\"overviewLoaded()\"></div>" +
      "</div>"
    );
    $templateCache.put('dispatch/addresss.html',
      "<h3>Addresses</h3>" +
      "<div class='grid' ui-grid='addressesGrid' ui-grid-selection></div>"
    );
    $templateCache.put('dispatch/address.html',
      "<ul class=\"nav nav-tabs\">" +
      "    <li ng-repeat=\"mode in gridModes\" ng-click=\"selectMode(mode,'Address')\" ng-class=\"{active : isModeSelected(mode,'Address')}\" title=\"{$ mode.title $}\" ng-bind-html-unsafe=\"mode.content\"> </li>" +
      "</ul>" +
      "<div ng-if=\"isModeVisible('Address','attributes')\" class=\"selectedItems\">" +
      "    <h3>Address {$ address.name $}</h3>" +
      "    <div class=\"gridStyle noHighlight\" ui-grid=\"addressGrid\"></div>" +
      "</div>" +
      "<div ng-if=\"isModeVisible('Address','links')\" class=\"selectedItems\">" +
      "    <h3>Links for address {$ address.name $}</h3>" +
      "    <div class=\"gridStyle\" ui-grid=\"linksGrid\"></div>" +
      "</div>"
    );

    $templateCache.put('dispatch/connection.html',
      "<ul class=\"nav nav-tabs\">" +
      "    <li ng-repeat=\"mode in gridModes\" ng-click=\"selectMode(mode,'Connection')\" ng-class=\"{active : isModeSelected(mode,'Connection')}\" title=\"{$ mode.title $}\" ng-bind-html-unsafe=\"mode.content\"> </li>" +
      "</ul>" +
      "<div ng-if=\"isModeVisible('Connection','attributes')\" class=\"selectedItems\">" +
      "    <h3>Connection {$ connection.name $}</h3>" +
      "    <div class=\"gridStyle noHighlight\" ui-grid=\"connectionGrid\"></div>" +
      "</div>" +
      "<div ng-if=\"isModeVisible('Connection','links')\" class=\"selectedItems\">" +
      "    <h3>Links for connection {$ connection.name $}</h3>" +
      "    <div class=\"gridStyle\" ui-grid=\"linksGrid\"></div>" +
      "</div>"
    );
    $templateCache.put('dispatch/connections.html',
      "<h3>Connections</h3>" +
      "<div class=\"overview\">" +
      "    <div class=\"grid\" ui-grid=\"allConnectionGrid\" ui-grid-selection></div>" +
      "</div>"
    );
    $templateCache.put('dispatch/log.html',
      "<h3>{$ log.name $}</h3>" +
      "<div ng-if=\"logFields.length > 0\">" +
      "    <table class=\"log-entry\" ng-repeat=\"entry in logFields track by $index\">" +
      "        <tr>" +
      "            <td>Router</td><td>{$ entry.nodeId $}</td>" +
      "        </tr>" +
      "        <tr>" +
      "            <td align=\"left\" colspan=\"2\">{$ entry.time $}</td>" +
      "        </tr>" +
      "        <tr>" +
      "            <td>Source</td><td>{$ entry.source $}:{$ entry.line $}</td>" +
      "        </tr>" +
      "        <tr>" +
      "            <td valign=\"middle\">Message</td><td valign=\"middle\"><pre>{$ entry.message $}</pre></td>" +
      "        </tr>" +
      "    </table>" +
      "</div>" +
      "<div ng-if=\"logFields.length == 0\">No log entries for {$ log.name $}</div>"
    );
    $templateCache.put('dispatch/logs.html',
      "<h3>Recent log entries</h3>" +
      "<div class=\"overview\">" +
      "    <div class=\"grid\" ui-grid=\"allLogGrid\" ui-grid-selection></div>" +
      "</div>"
    );
    $templateCache.put('dispatch/router.html',
      "<h3>Router {$ router.name $}</h3>" +
      "<div class=\"grid noHighlight\" ui-grid=\"routerGrid\"></div>"
    );
    $templateCache.put('dispatch/routers.html',
      "<h3>Routers</h3>" +
      "<div class=\"overview\">" +
      "    <div class=\"grid\" ui-grid=\"allRouters\" ui-grid-selection></div>" +
      "</div>"
    );
  }
})();

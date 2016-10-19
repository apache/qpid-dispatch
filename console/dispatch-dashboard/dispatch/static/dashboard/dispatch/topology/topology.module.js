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
    .module('horizon.dashboard.dispatch.topology', [])
    .config(config)
    .run(addTemplates)

  config.$inject = [
    '$provide',
    '$windowProvider'
  ];

  addTemplates.$inject = [
    '$templateCache',
  ];

  /**
   * @name config
   * @param {Object} $provide
   * @param {Object} $windowProvider
   * @description Base path for the overview code
   * @returns {undefined} No return value
   */
  function config($provide, $windowProvider) {
    var path = $windowProvider.$get().STATIC_URL + 'dashboard/dispatch/topology/';
    $provide.constant('horizon.dashboard.dispatch.topology.basePath', path);
  }

  function addTemplates($templateCache) {
    $templateCache.put("dispatch/topology.html",
      "<div class=\"qdrTopology\" ng-controller=\"horizon.dashboard.dispatch.topology.TopologyController as ctrl\">" +
      "    <div>" +
      "<!--" +
      "        <ul class=\"nav nav-tabs ng-scope qdrTopoModes\">" +
      "            <li ng-repeat=\"mode in modes\" ng-class=\"{active : isModeActive(mode.name), 'pull-right' : isRight(mode)}\" ng-click=\"selectMode('{{mode.name}}')\" >" +
      "                <a data-placement=\"bottom\" class=\"ng-binding\"> {{mode.name}} </a></li>" +
      "        </ul>" +
      "-->" +
      "        <div id=\"topology\" ng-show=\"mode == 'Diagram'\"><!-- d3 toplogy here --></div>" +
      "        <div id=\"geology\" ng-show=\"mode == 'Globe'\"><!-- d3 globe here --></div>" +
      "        <div id=\"crosssection\"><!-- d3 pack here --></div>" +
      "        <!-- <div id=\"addRouter\" ng-show=\"mode == 'Add Node'\"></div> -->" +
      "        <div id=\"node_context_menu\" class=\"contextMenu\">" +
      "            <ul>" +
      "                <li class=\"na\" ng-class=\"{new: contextNode.cls == 'temp'}\" ng-click=\"addingNode.trigger = 'editNode'\">Edit...</li>" +
      "                <li class=\"na\" ng-class=\"{adding: addingNode.step > 0}\" ng-click=\"addingNode.step = 0\">Cancel add</li>" +
      "                <li class=\"context-separator\"></li>" +
      "                <li class=\"na\" ng-class=\"{'force-display': !isFixed()}\" ng-click=\"setFixed(true)\">Freeze in place</li>" +
      "                <li class=\"na\" ng-class=\"{'force-display': isFixed()}\" ng-click=\"setFixed(false)\">Unfreeze</li>" +
      "            </ul>" +
      "        </div>" +
      "        <div id=\"svg_context_menu\" class=\"contextMenu\">" +
      "            <ul>" +
      "                <li ng-click=\"addingNode.step = 2\">Add a new router</li>" +
      "            </ul>" +
      "        </div>" +
      "        <div id=\"link_context_menu\" class=\"contextMenu\">" +
      "            <ul>" +
      "                <li ng-click=\"reverseLink()\">Reverse connection direction</li>" +
      "                <li ng-click=\"removeLink()\">Remove connection</li>" +
      "            </ul>" +
      "        </div>" +
      "        <div id=\"svg_legend\"></div>" +
      "        <div id=\"multiple_details\">" +
      "            <h4 class=\"grid-title\">Connections</h4>" +
      "            <div class=\"grid\" ui-grid=\"multiDetails\" ui-grid-selection></div>" +
      "         </div>" +
      "        <div id=\"link_details\">" +
      "            <h4 class=\"grid-title\">Links</h4>" +
      "            <div class=\"grid\" ui-grid=\"linkDetails\" ui-grid-selection></div>" +
      "        </div>" +
      "    </div>" +
      "    <div ng-controller=\"horizon.dashboard.dispatch.topology.TopologyFormController as fctrl\">" +
      "        <div id=\"topologyForm\" ng-class=\"{selected : isSelected()}\">" +
      "            <!-- <div ng-repeat=\"form in forms\" ng-show=\"isVisible(form)\" ng-class='{selected : isSelected(form)}'> -->" +
      "            <div ng-if=\"form == 'router'\">" +
      "                <h3>Router Info</h3>" +
      "                <div class=\"grid\" ui-grid=\"topoGridOptions\"></div>" +
      "            </div>" +
      "            <div ng-if=\"form == 'connection'\">" +
      "                <h3>Connection Info</h3>" +
      "                <div class=\"grid\" ui-grid=\"topoGridOptions\"></div>" +
      "            </div>" +
      "            <div id=\"addNodeForm\" ng-show=\"form == 'add'\">" +
      "                <h3>Add a new router</h3>" +
      "                <ul>" +
      "                    <li>Click on an existing router to create a connection to the new router</li>" +
      "                    <li>Double-click on the new router to <button ng-click=\"editNewRouter()\">edit</button> its properties</li>" +
      "                    <li ng-show=\"addingNode.hasLink\" >Right-click on a new connection to edit its properties</li>" +
      "                </ul>" +
      "                <button ng-click=\"cancel()\">Cancel</button>" +
      "            </div>" +
      "        </div>" +
      "    </div>" +
      "</div>"
    );
  }
})();

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
  'use strict';

  angular
    .module('horizon.dashboard.dispatch.topology')
    .controller('horizon.dashboard.dispatch.topology.TopologyFormController', TopologyFormController);

  TopologyFormController.$inject = [
    '$scope',
    'horizon.dashboard.dispatch.comService'
  ];

  function TopologyFormController($scope, QDRService) {
    var fctrl = this;
		$scope.attributes = []
    var nameTemplate = '<div title="{{row.entity.description}}" class="ngCellText"><span>{{row.entity.attributeName}}</span></div>';
    var valueTemplate = '<div title="{{row.entity.attributeValue}}" class="ngCellText"><span>{{row.entity.attributeValue}}</span></div>';
    $scope.topoGridOptions = {
      data: 'attributes',
			enableColumnResize: true,
			multiSelect: false,
      columnDefs: [
        {
          field: 'attributeName',
//          cellTemplate: nameTemplate,
          displayName: 'Attribute'
        },
        {
          field: 'attributeValue',
//          cellTemplate: valueTemplate,
          displayName: 'Value'
        }
      ]
    };
		$scope.form = ''
		$scope.$on('showEntityForm', function (event, args) {
			var attributes = args.attributes;
			var entityTypes = QDRService.schema.entityTypes[args.entity].attributes;
			attributes.forEach( function (attr) {
				if (entityTypes[attr.attributeName] && entityTypes[attr.attributeName].description)
					attr.description = entityTypes[attr.attributeName].description
			})
			$scope.attributes = attributes;
			$scope.form = args.entity;
		})
		$scope.$on('showAddForm', function (event) {
			$scope.form = 'add';
		})
	}

  return QDR;
}(QDR || {}));
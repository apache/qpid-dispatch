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
    .controller('horizon.dashboard.dispatch.topology.TopologyDownloadController', TopologyDownloadController);

  TopologyDownloadController.$inject = [
    '$scope',
    'horizon.dashboard.dispatch.comService'
  ]

  function TopologyDownloadController($scope, QDRService, dialog, results) {
		var result = results.entities;
		var annotations = results.annotations;
		var annotationKeys = Object.keys(annotations);
		var annotationSections = {};

		// use the router's name as the file name if present
		$scope.newRouterName = 'router';
		result.forEach( function (e) {
			if (e.actualName == 'router') {
				e.attributes.forEach( function (a) {
					if (a.name == 'name') {
						$scope.newRouterName = a.value;
					}
				})
			}
		})
		$scope.newRouterName = $scope.newRouterName + ".conf";

		var template = $templateCache.get('config-file-header.html');
		$scope.verbose = true;
		$scope.$watch('verbose', function (newVal) {
			if (newVal !== undefined) {
				// recreate output using current verbose setting
				getOutput();
			}
		})

		var getOutput = function () {
			$scope.output = template + '\n';
			$scope.parts = [];
			var commentChar = '#'
			result.forEach(function (entity) {
				// don't output a section for annotations, they get flattened into the entities
				var section = "";
				if (entity.icon) {
					section += "##\n## Add to " + entity.link.__data__.source.name + "'s configuration file\n##\n";
				}
				section += "##\n## " + QDRService.humanify(entity.actualName) + " - " + entity.description + "\n##\n";
				section += entity.actualName + " {\n";
				entity.attributes.forEach(function (attribute) {
					if (attribute.input == 'select')
						attribute.value = attribute.selected;

					// treat values with all spaces and empty strings as undefined
					attribute.value = String(attribute.value).trim();
					if (attribute.value === 'undefined' || attribute.value === '')
						attribute.value = undefined;

					if ($scope.verbose) {
						commentChar = attribute.required || attribute.value != attribute['default'] ? ' ' : '#';
						if (!attribute.value) {
							commentChar = '#';
							attribute.value = '';
						}
						section += commentChar + "    "
							+ attribute.name + ":" + Array(Math.max(20 - attribute.name.length, 1)).join(" ")
							+ attribute.value
						    + Array(Math.max(20 - ((attribute.value)+"").length, 1)).join(" ")
							+ '# ' + attribute.description
						    + "\n";
					} else {
						if (attribute.value) {
							if (attribute.value != attribute['default'] || attribute.required)
								section += "    "
									+ attribute.name + ":" + Array(20 - attribute.name.length).join(" ")
									+ attribute.value + "\n";

						}
					}
				})
				section += "}\n\n";
				// if entity.icon is true, this is a connector intended for another router
				if (entity.icon)
					$scope.parts.push({output: section,
								link: entity.link,
								name: entity.link.__data__.source.name,
								references: entity.references});
				else
					$scope.output += section;

				// if this section is actually an annotation
				if (annotationKeys.indexOf(entity.actualName) > -1) {
					annotationSections[entity.actualName] = section;
				}
			})
			// go back and add annotation sections to the parts
			$scope.parts.forEach (function (part) {
				for (var section in annotationSections) {
					if (part.references.indexOf(section) > -1) {
						part.output += annotationSections[section];
					}
				}
			})
			QDR.log.debug($scope.output);
		}

        // handle the download button click
        $scope.download = function () {
			var output = $scope.output + "\n\n"
			var blob = new Blob([output], { type: 'text/plain;charset=utf-16' });
			saveAs(blob, $scope.newRouterName);
        }

		$scope.downloadPart = function (part) {
			var linkName = part.link.__data__.source.name + 'additional.conf';
			var blob = new Blob([part.output], { type: 'text/plain;charset=utf-16' });
			saveAs(blob, linkName);
		}

		$scope.done = function () {
	        dialog.close();
		}
  };

  return QDR;
}(QDR || {}));

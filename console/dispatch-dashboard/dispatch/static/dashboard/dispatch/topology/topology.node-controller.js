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
    .controller('horizon.dashboard.dispatch.topology.TopologyNodeController', TopologyNodeController);

  TopologyNodeController.$inject = [
    '$scope',
    'horizon.dashboard.dispatch.comService'
  ]

  function TopologyNodeController($scope, QDRService, dialog, newname) {
   		var schema = QDRService.schema;
   		var myEntities = ['router', 'log', 'listener' ];
   		var typeMap = {integer: 'number', string: 'text', path: 'text', boolean: 'boolean'};
		var newLinks = $('path.temp').toArray();    // jquery array of new links for the added router
		var nodeInfo = QDRService.topology.nodeInfo();
		var separatedEntities = []; // additional entities required if a link is reversed
		var myPort = 0, myAddr = '0.0.0.0'; // port and address for new router
   		$scope.entities = [];

		// find max port number that is used in all the listeners
		var getMaxPort = function (nodeInfo) {
			var maxPort = 5674;
			for (var key in nodeInfo) {
				var node = nodeInfo[key];
				var listeners = node['.listener'];
				var attrs = listeners.attributeNames;
				for (var i=0; i<listeners.results.length; ++i) {
					var res = listeners.results[i];
					var port = QDRService.valFor(attrs, res, 'port');
					if (parseInt(port, 10) > maxPort)
						maxPort = parseInt(port, 10);
				}
			}
			return maxPort;
		}
		var maxPort = getMaxPort(nodeInfo);

		// construct an object that contains all the info needed for a single tab's fields
		var entity = function (actualName, tabName, humanName, ent, icon, link) {
			var nameIndex = -1; // the index into attributes that the name field was placed
			var index = 0;
			var info = {
			    actualName: actualName,
				tabName:    tabName,
				humanName:  humanName,
				description:ent.description,
				icon:       angular.isDefined(icon) ? icon : '',
				references: ent.references,
				link:       link,

   		        attributes: $.map(ent.attributes, function (value, key) {
					// skip identity and depricated fields
   		            if (key == 'identity' || value.description.startsWith('Deprecated'))
   		                return null;
					var val = value['default'];
					if (key == 'name')
						nameIndex = index;
					index++;
					return {    name:       key,
								humanName:  QDRService.humanify(key),
                                description:value.description,
                                type:       typeMap[value.type],
                                rawtype:    value.type,
                                input:      typeof value.type == 'string' ? value.type == 'boolean' ? 'boolean' : 'input'
                                                                          : 'select',
                                selected:   val ? val : undefined,
                                'default':  value['default'],
                                value:      val,
                                required:   value.required,
                                unique:     value.unique
                    };
                })
			}
			// move the 'name' attribute to the 1st position
			if (nameIndex > -1) {
				var tmp = info.attributes[0];
				info.attributes[0] = info.attributes[nameIndex];
				info.attributes[nameIndex] = tmp;
			}
			return info;
		}

		// remove the annotation fields
		var stripAnnotations = function (entityName, ent, annotations) {
			if (ent.references) {
				var newEnt = {attributes: {}};
				ent.references.forEach( function (annoKey) {
					if (!annotations[annoKey])
						annotations[annoKey] = {};
					annotations[annoKey][entityName] = true;    // create the key/consolidate duplicates
					var keys = Object.keys(schema.annotations[annoKey].attributes);
					for (var attrib in ent.attributes) {
						if (keys.indexOf(attrib) == -1) {
							newEnt.attributes[attrib] = ent.attributes[attrib];
						}
					}
					// add a field for the reference name
					newEnt.attributes[annoKey] = {type: 'string',
							description: 'Name of the ' + annoKey + ' section.',
							'default': annoKey, required: true};
				})
				newEnt.references = ent.references;
				newEnt.description = ent.description;
				return newEnt;
			}
			return ent;
		}

		var annotations = {};
   		myEntities.forEach(function (entityName) {
   		    var ent = schema.entityTypes[entityName];
   		    var hName = QDRService.humanify(entityName);
   		    if (entityName == 'listener')
   		        hName = "Listener for clients";
   		    var noAnnotations = stripAnnotations(entityName, ent, annotations);
			var ediv = entity(entityName, entityName, hName, noAnnotations, undefined);
			if (ediv.actualName == 'router') {
				ediv.attributes.filter(function (attr) { return attr.name == 'name'})[0].value = newname;
				// if we have any new links (connectors), then the router's mode should be interior
				if (newLinks.length) {
					var roleAttr = ediv.attributes.filter(function (attr) { return attr.name == 'mode'})[0];
					roleAttr.value = roleAttr.selected = "interior";
				}
			}
			if (ediv.actualName == 'container') {
				ediv.attributes.filter(function (attr) { return attr.name == 'containerName'})[0].value = newname + "-container";
			}
			if (ediv.actualName == 'listener') {
				// find max port number that is used in all the listeners
				ediv.attributes.filter(function (attr) { return attr.name == 'port'})[0].value = ++maxPort;
			}
			// special case for required log.module since it doesn't have a default
			if (ediv.actualName == 'log') {
				var moduleAttr = ediv.attributes.filter(function (attr) { return attr.name == 'module'})[0];
				moduleAttr.value = moduleAttr.selected = "DEFAULT";
			}
			$scope.entities.push( ediv );
   		})

		// add a tab for each annotation that was found
		var annotationEnts = [];
		for (var key in annotations) {
			ent = angular.copy(schema.annotations[key]);
			ent.attributes.name = {type: "string", unique: true, description: "Unique name that is used to refer to this set of attributes."}
			var ediv = entity(key, key+'tab', QDRService.humanify(key), ent, undefined);
			ediv.attributes.filter(function (attr) { return attr.name == 'name'})[0].value = key;
			$scope.entities.push( ediv );
			annotationEnts.push( ediv );
		}

		// add an additional listener tab if any links are reversed
		ent = schema.entityTypes['listener'];
		newLinks.some(function (link) {
			if (link.__data__.right) {
	   		    var noAnnotations = stripAnnotations('listener', ent, annotations);
				var ediv = entity("listener", "listener0", "Listener (internal)", noAnnotations, undefined);
				ediv.attributes.filter(function (attr) { return attr.name == 'port'})[0].value = ++maxPort;
				// connectors from other routers need to connect to this addr:port
				myPort = maxPort;
				myAddr = ediv.attributes.filter(function (attr) { return attr.name == 'host'})[0].value

				// override the role. 'normal' is the default, but we want inter-router
				ediv.attributes.filter(function( attr ) { return attr.name == 'role'})[0].selected = 'inter-router';
				separatedEntities.push( ediv );
				return true; // stop looping
			}
			return false;   // continue looping
		})

		// Add connector tabs for each new link on the topology graph
		ent = schema.entityTypes['connector'];
		newLinks.forEach(function (link, i) {
   		    var noAnnotations = stripAnnotations('connector', ent, annotations);
			var ediv = entity('connector', 'connector' + i, " " + link.__data__.source.name, noAnnotations, link.__data__.right, link)

			// override the connector role. 'normal' is the default, but we want inter-router
			ediv.attributes.filter(function( attr ) { return attr.name == 'role'})[0].selected = 'inter-router';

			// find the addr:port of the inter-router listener to use
			var listener = nodeInfo[link.__data__.source.key]['.listener'];
			var attrs = listener.attributeNames;
			for (var i=0; i<listener.results.length; ++i) {
				var res = listener.results[i];
				var role = QDRService.valFor(attrs, res, 'role');
				if (role == 'inter-router') {
					ediv.attributes.filter(function( attr ) { return attr.name == 'host'})[0].value =
						QDRService.valFor(attrs, res, 'host')
					ediv.attributes.filter(function( attr ) { return attr.name == 'port'})[0].value =
						QDRService.valFor(attrs, res, 'port')
					break;
				}
			}
			if (link.__data__.right) {
				// connectors from other nodes need to connect to the new router's listener addr:port
   				ediv.attributes.filter(function (attr) { return attr.name == 'port'})[0].value = myPort;
   				ediv.attributes.filter(function (attr) { return attr.name == 'host'})[0].value = myAddr;

				separatedEntities.push(ediv)
			}
			else
				$scope.entities.push( ediv );
		})
		Array.prototype.push.apply($scope.entities, separatedEntities);

		// update the description on all the annotation tabs
		annotationEnts.forEach ( function (ent) {
			var shared = Object.keys(annotations[ent.actualName]);
			ent.description += " These fields are shared by " + shared.join(" and ") + ".";

		})

		$scope.testPattern = function (attr) {
			if (attr.rawtype == 'path')
				return /^(\/)?([^/\0]+(\/)?)+$/;
				//return /^(.*\/)([^/]*)$/;
			return /(.*?)/;
		}

		$scope.attributeDescription = '';
		$scope.attributeType = '';
		$scope.attributeRequired = '';
		$scope.attributeUnique = '';
		$scope.active = 'router'
		$scope.fieldsetDivs = "/fieldsetDivs.html"
		$scope.setActive = function (tabName) {
			$scope.active = tabName
		}
		$scope.isActive = function (tabName) {
			return $scope.active === tabName
		}
		$scope.showDescription = function (attr, e) {
			$scope.attributeDescription = attr.description;
			var offset = jQuery(e.currentTarget).offset()
			jQuery('.attr-description').offset({top: offset.top})

			$scope.attributeType = "Type: " + JSON.stringify(attr.rawtype);
			$scope.attributeRequired = attr.required ? 'required' : '';
			$scope.attributeUnique = attr.unique ? 'Must be unique' : '';
		}
        // handle the download button click
        // copy the dialog's values to the original node
        $scope.download = function () {
	        dialog.close({entities: $scope.entities, annotations: annotations});
        }
        $scope.cancel = function () {
            dialog.close()
        };

		$scope.selectAnnotationTab = function (tabName) {
            var tabs = $( "#tabs" ).tabs();
            tabs.tabs("select", tabName);
		}

        var initTabs = function () {
            var div = angular.element("#tabs");
            if (!div.width()) {
                setTimeout(initTabs, 100);
                return;
            }
            $( "#tabs" )
                .tabs()
                .addClass('ui-tabs-vertical ui-helper-clearfix');
        }
        // start the update loop
        initTabs();

  };

  return QDR;
}(QDR || {}));

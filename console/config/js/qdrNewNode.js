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
var QDR = (function(QDR) {

  QDR.module.controller("QDR.NodeDialogController", function($scope, $sce, $uibModalInstance, QDRService, node, entityType, context, entityKey, maxPort, hasLinks) {
    var newname = node.name
    $scope.context = context
    if (!angular.isDefined(context))
      $scope.context = 'new'
    var newContext = {"new":maxPort+1, "artemis":61616, "qpid":5672, 'log':'', 'sslProfile':''}
    $scope.title = ((context && context in newContext) ? "Add " : "Edit ") + entityType + " section"
    if (context === 'artemis')
      $scope.title += " to an Artemis broker"
    if (context === 'qpid')
      $scope.title += " to a Qpid broker"
    var schema = QDRService.schema;
    var myEntities = []
    myEntities.push(entityType)
    var readOnlyAttrs = { 'router': ['version', 'id', 'identity'],
                          'log': ['name', 'identity'],
                          'address': ['identity'],
                          'linkRoute': ['identity'],
                          "sslProfile": ['identity'],
                          "connector": ['identity']}
    // one of the following must be present
    var requiredAttrs = {'address': ['prefix', 'pattern']}

    var typeMap = {
      integer: 'number',
      string: 'text',
      path: 'text',
      boolean: 'boolean'
    };
    var separatedEntities = []; // additional entities required if a link is reversed
    var myPort = 0,
      myAddr = '0.0.0.0'; // port and address for new router
    $scope.entities = [];

    var getEdivAttr = function (ediv, key) {
      for (var i=0; i<ediv.attributes.length; i++) {
        if (ediv.attributes[i].name === key)
          return ediv.attributes[i]
      }
    }

    // construct an object that contains all the info needed for a single tab's fields
    var entity = function(actualName, tabName, humanName, ent, icon, link) {
      var index = 0;
      var info = {
          actualName: actualName,
          tabName: tabName,
          humanName: humanName,
          description: ent.description,
          icon: angular.isDefined(icon) ? icon : '',
          references: ent.references,
          link: link,

          attributes: $.map(ent.attributes, function(value, key) {
            // skip read-only fields
            if (readOnlyAttrs[tabName] && readOnlyAttrs[tabName].indexOf(key) >= 0)
              return null;
            // skip deprecated and statistics fields
            if (key == value.description.startsWith('Deprecated') || value.graph)
              return null;
            var val = value['default'];
            index++;
            return {
              name: key,
              humanName: QDRService.humanify(key),
              description: value.description,
              type: typeMap[value.type],
              rawtype: value.type,
              input: typeof value.type == 'string' ? value.type == 'boolean' ? 'boolean' : 'input' : 'select',
              selected: val ? val : undefined,
              'default': value['default'],
              value: val,
              required: (value.required || key === entityKey || key === 'host')? true : false,
              unique: value.unique
            };
          })
        }
      return info;
    }

    // remove the annotation fields
    var stripAnnotations = function(entityName, ent, annotations) {
      if (ent.references) {
        var newEnt = {
          attributes: {}
        };
        ent.references.forEach(function(annoKey) {
          if (!annotations[annoKey])
            annotations[annoKey] = {};
          annotations[annoKey][entityName] = true; // create the key/consolidate duplicates
          var keys = Object.keys(schema.annotations[annoKey].attributes);
          for (var attrib in ent.attributes) {
            if (keys.indexOf(attrib) == -1) {
              newEnt.attributes[attrib] = ent.attributes[attrib];
            }
          }
          // add a field for the reference name
          newEnt.attributes[annoKey] = {
            type: 'string',
            description: 'Name of the ' + annoKey + ' section.',
            'default': annoKey,
            required: true
          };
        })
        newEnt.references = ent.references;
        newEnt.description = ent.description;
        return newEnt;
      }
      return ent;
    }

    var annotations = {};
    myEntities.forEach(function(entityName) {
      var ent = schema.entityTypes[entityName];
      var hName = QDRService.humanify(entityName);
      if (entityName == 'listener')
        hName = "Listener for clients";
      var noAnnotations = stripAnnotations(entityName, ent, annotations);
      var ediv = entity(entityName, entityName, hName, noAnnotations, undefined);

      if (node[entityName+'s'] && context in node[entityName+'s']) {
        // fill form with existing data
        var o = node[entityName+'s'][context]
        ediv.attributes.forEach( function (attr) {
          if (attr['name'] in o) {
            attr['value'] = o[attr['name']]
            if (attr['type'] === 'number') {
              try {
                attr['value'] = parseInt(attr['value'])
              }
              catch (e) {
                attr['value'] = 0
              }
            }

            // if the form has a select dropdown, set the selected to the current value
            if (Array.isArray(attr['rawtype']))
              attr.selected = attr['value']
          }
        })
      }

      if (ediv.actualName == 'router') {
        ediv.attributes.forEach( function (attr) {
          if (attr['name'] in node) {
            attr['value'] = node[attr['name']]
          }
        })
        // if we have any new links (connectors), then the router's mode should be interior
        var roleAttr = getEdivAttr(ediv, 'mode')
        if (hasLinks && roleAttr) {
          roleAttr.value = roleAttr.selected = "interior";
        } else {
          roleAttr.value = roleAttr.selected = "standalone";
        }
      }
      if (ediv.actualName == 'listener' && context === 'new') {
        // find max port number that is used in all the listeners
        var port = getEdivAttr(ediv, 'port')
        if (port)
          port.value = ++maxPort
        var host = getEdivAttr(ediv, 'host')
        if (host && node.host)
          host.value = node.host
      }
      if (ediv.actualName == 'connector' && context in newContext) {
        // find max port number that is used in all the listeners
        var port = getEdivAttr(ediv, 'port')
        if (port) {
          port.value = newContext[context]
        }
        if (context === 'artemis' || context === 'qpid') {
          var role = getEdivAttr(ediv, 'role')
          if (role) {
            role.value = role.selected = 'route-container'
          }
        }
        context = 'new'
      }
      // special case for required log.module since it doesn't have a default
      if (ediv.actualName == 'log') {
        // initialize module to 'DEFAULT'
        var moduleAttr = getEdivAttr(ediv, 'module')
        if (moduleAttr)
          moduleAttr.value = moduleAttr.selected = "DEFAULT"
        // adding a new log section and we already have a section. select 1st unused module
        if (context === 'new' && node.logs) {
          var modules = ent.attributes.module.type
          var availableModules = modules.filter( function (module) {
             return !(module in node.logs)
          })
          if (availableModules.length > 0) {
            moduleAttr.value = moduleAttr.selected = availableModules[0]
          }
        } else if (node.logs && context in node.logs) {
          // fill form with existing data
          var log = node.logs[context]
          ediv.attributes.forEach( function (attr) {
            if (attr['name'] in log) {
              attr['value'] = log[attr['name']]
              if (attr['name'] == 'module')
                attr.selected = attr['value']
            }
          })
        }
      }
      // sort ediv.attributes on name
      var allNames = ediv.attributes.map( function (attr) {
        return attr['name']
      })
      allNames.sort()

      // move all required entities to the top
      for (var i=0; i<ediv.attributes.length; i++) {
        var attr = ediv.attributes[i]
        if (attr.required) {
          allNames.move(allNames.indexOf(attr.name), 0)
        }
      }

      // move entities key into first position
      for (var i=0, keys=entityKey.split('|'); i<keys.length; i++) {
        var keyIndex = allNames.findIndex(function (name) {
          return name === keys[i]
        })
        if (keyIndex > 0)
          allNames.move(keyIndex, 0)
      }
      // move any entities with sort: last to end
      for (var i=0; i<ediv.attributes.length; i++) {
        var attr = ediv.attributes[i]
        if (attr.sort && attr.sort === 'last') {
          allNames.move(allNames.indexOf(attr.name), allNames.length-1)
        }
      }

      // now order the entity attributes by allNames
      ediv.attributes.sort(function(attr1, attr2){
          return allNames.indexOf(attr1['name']) - allNames.indexOf(attr2['name'])
      });
      $scope.entities.push(ediv);
    })

    $scope.testPattern = function(attr) {
      if (attr.rawtype == 'path')
        return /^(\/)?([^/\0]+(\/)?)+$/;
      return /(.*?)/;
    }

    $scope.attributeDescription = '';
    $scope.attributeType = '';
    $scope.attributeRequired = '';
    $scope.attributeUnique = '';
    $scope.active = 'router'

    $scope.isItDisabled = function (attribute) {
      if (requiredAttrs[entityType] && requiredAttrs[entityType].indexOf(attribute.name) > -1) {
        return requiredAttrs[entityType].some( function (attr) {
          return (attr !== attribute.name) && (getEdivAttr($scope.entities[0], attr).value)
        })
      } else
        return false
    }

    $scope.isItRequired = function (attribute) {
      if (requiredAttrs[entityType] && requiredAttrs[entityType].indexOf(attribute.name) > -1) {
        var s = requiredAttrs[entityType].some( function (attr) {
          return (getEdivAttr($scope.entities[0], attr).value)
        })
        return !s
      } else
        return attribute.required
    }
    $scope.setActive = function(tabName) {
      $scope.active = tabName
    }
    $scope.isActive = function(tabName) {
      return $scope.active === tabName
    }
    $scope.setDescription = function(attr, e) {
      $scope.attributeDescription = $sce.trustAsHtml(attr.description +
          '<div class="attr-type">Type: ' + JSON.stringify(attr.rawtype, null, 1) + '</div>' +
          '<div class="attr-required">' + (attr.required ? "required" : '') + '</div>' +
          '<div class="attr-unique">' + (attr.unique ? "Must be unique" : '') + '</div>')
    }
      // handle the save button click
      // copy the dialog's values to the original node
    $scope.save = function() {
      $uibModalInstance.close({
        entities: $scope.entities,
      });
    }
    $scope.cancel = function() {
      $uibModalInstance.close()
    };
    $scope.del = function() {
      $uibModalInstance.close({del: true})
    }
    $scope.showDelete = function () {
      return !(context === 'new' || !context)
    }

  });

  return QDR;
}(QDR || {}));

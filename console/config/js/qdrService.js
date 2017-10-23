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

  // The QDR service handles the connection to
  // the server in the background
  QDR.module.factory("QDRService", ['$rootScope', '$http', '$timeout', '$resource', '$location', function($rootScope, $http, $timeout, $resource, $location) {
    var self = {

      connectActions: [],
      schema: undefined,

      addConnectAction: function(action) {
        if (angular.isFunction(action)) {
          self.connectActions.push(action);
        }
      },
      executeConnectActions: function() {
        self.connectActions.forEach(function(action) {
          try {
            action.apply();
          } catch (e) {
            // in case the page that registered the handler has been unloaded
            QDR.log.info(e.message)
          }
        });
        self.connectActions = [];
      },
      nameFromId: function(id) {
        return id.split('/')[3];
      },

      humanify: function(s) {
        if (!s || s.length === 0)
          return s;
        var t = s.charAt(0).toUpperCase() + s.substr(1).replace(/[A-Z]/g, ' $&');
        return t.replace(".", " ");
      },
      pretty: function(v) {
        var formatComma = d3.format(",");
        if (!isNaN(parseFloat(v)) && isFinite(v))
          return formatComma(v);
        return v;
      },

      // given an attribute name array, find the value at the same index in the values array
      valFor: function(aAr, vAr, key) {
        var idx = aAr.indexOf(key);
        if ((idx > -1) && (idx < vAr.length)) {
          return vAr[idx];
        }
        return null;
      },

      isArtemis: function(d) {
        return d.nodeType === 'route-container' && d.properties.product === 'apache-activemq-artemis';
      },

      isQpid: function(d) {
        return d.nodeType === 'route-container' && (d.properties && d.properties.product === 'qpid-cpp');
      },

      isAConsole: function(properties, connectionId, nodeType, key) {
        return self.isConsole({
          properties: properties,
          connectionId: connectionId,
          nodeType: nodeType,
          key: key
        })
      },
      isConsole: function(d) {
        // use connection properties if available
        return (d && d['properties'] && d['properties']['console_identifier'] === 'Dispatch console')
      },

      flatten: function(attributes, result) {
        var flat = {}
        attributes.forEach(function(attr, i) {
          if (result && result.length > i)
            flat[attr] = result[i]
        })
        return flat;
      },
      getSchema: function(callback) {
        self.sendMethod("GET-SCHEMA", {}, function (response) {
          for (var entityName in response.entityTypes) {
            var entity = response.entityTypes[entityName]
            if (entity.deprecated) {
              delete response.entityTypes[entityName]
            } else {
              for (var attributeName in entity.attributes) {
                var attribute = entity.attributes[attributeName]
                if (attribute.deprecated) {
                  delete response.entityTypes[entityName].attributes[attributeName]
                }
              }
            }
          }
          self.schema = response
          callback()
        })
      },

      sendMethod: function(operation, props, callback) {
        setTimeout(function () {
          props['operation'] = operation
          $.post( "http://localhost:8000", JSON.stringify(props), function (response, status) {
            callback(response)
          }, "json" )
        }, 1)
      }

    }
    return self;
  }]);

  return QDR;

}(QDR || {}));

(function() {
  console.dump = function(o) {
    if (window.JSON && window.JSON.stringify)
      QDR.log.info(JSON.stringify(o, undefined, 2));
    else
      console.log(o);
  };
})();

if (!String.prototype.startsWith) {
  String.prototype.startsWith = function (searchString, position) {
    return this.substr(position || 0, searchString.length) === searchString
  }
}

if (!String.prototype.endsWith) {
  String.prototype.endsWith = function(searchString, position) {
      var subjectString = this.toString();
      if (typeof position !== 'number' || !isFinite(position) || Math.floor(position) !== position || position > subjectString.length) {
        position = subjectString.length;
      }
      position -= searchString.length;
      var lastIndex = subjectString.lastIndexOf(searchString, position);
      return lastIndex !== -1 && lastIndex === position;
  };
}

if (typeof Object.assign != 'function') {
  // Must be writable: true, enumerable: false, configurable: true
  Object.defineProperty(Object, "assign", {
    value: function assign(target, varArgs) { // .length of function is 2
      'use strict';
      if (target == null) { // TypeError if undefined or null
        throw new TypeError('Cannot convert undefined or null to object');
      }

      var to = Object(target);

      for (var index = 1; index < arguments.length; index++) {
        var nextSource = arguments[index];

        if (nextSource != null) { // Skip over if undefined or null
          for (var nextKey in nextSource) {
            // Avoid bugs when hasOwnProperty is shadowed
            if (Object.prototype.hasOwnProperty.call(nextSource, nextKey)) {
              to[nextKey] = nextSource[nextKey];
            }
          }
        }
      }
      return to;
    },
    writable: true,
    configurable: true
  });
}

if (!String.prototype.repeat) {
  String.prototype.repeat = function(count) {
    'use strict';
    if (this == null) {
      throw new TypeError('can\'t convert ' + this + ' to object');
    }
    var str = '' + this;
    count = +count;
    if (count != count) {
      count = 0;
    }
    if (count < 0) {
      throw new RangeError('repeat count must be non-negative');
    }
    if (count == Infinity) {
      throw new RangeError('repeat count must be less than infinity');
    }
    count = Math.floor(count);
    if (str.length == 0 || count == 0) {
      return '';
    }
    // Ensuring count is a 31-bit integer allows us to heavily optimize the
    // main part. But anyway, most current (August 2014) browsers can't handle
    // strings 1 << 28 chars or longer, so:
    if (str.length * count >= 1 << 28) {
      throw new RangeError('repeat count must not overflow maximum string size');
    }
    var rpt = '';
    for (var i = 0; i < count; i++) {
      rpt += str;
    }
    return rpt;
  }
}

if (!Array.prototype.move) {
  Array.prototype.move = function(from, to) {
      this.splice(to, 0, this.splice(from, 1)[0]);
  };
}

// https://tc39.github.io/ecma262/#sec-array.prototype.findIndex
if (!Array.prototype.findIndex) {
  Object.defineProperty(Array.prototype, 'findIndex', {
    value: function(predicate) {
     // 1. Let O be ? ToObject(this value).
      if (this == null) {
        throw new TypeError('"this" is null or not defined');
      }

      var o = Object(this);

      // 2. Let len be ? ToLength(? Get(O, "length")).
      var len = o.length >>> 0;

      // 3. If IsCallable(predicate) is false, throw a TypeError exception.
      if (typeof predicate !== 'function') {
        throw new TypeError('predicate must be a function');
      }

      // 4. If thisArg was supplied, let T be thisArg; else let T be undefined.
      var thisArg = arguments[1];

      // 5. Let k be 0.
      var k = 0;

      // 6. Repeat, while k < len
      while (k < len) {
        // a. Let Pk be ! ToString(k).
        // b. Let kValue be ? Get(O, Pk).
        // c. Let testResult be ToBoolean(? Call(predicate, T, « kValue, k, O »)).
        // d. If testResult is true, return k.
        var kValue = o[k];
        if (predicate.call(thisArg, kValue, k, o)) {
          return k;
        }
        // e. Increase k by 1.
        k++;
      }

      // 7. Return -1.
      return -1;
    }
  });
}


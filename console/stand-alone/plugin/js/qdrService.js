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
'use strict';
/* global Promise */
/**
 * @module QDR
 */
var QDR = (function(QDR) {

  // The QDR service handles the connection to the router
  QDR.module.factory('QDRService', ['$timeout', '$location', function($timeout, $location) {
    let dm = require('dispatch-management');
    let self = {
      management: new dm.Management($location.protocol()),
      utilities: dm.Utilities,

      onReconnect: function () {
        self.management.connection.on('disconnected', self.onDisconnect);
        let org = localStorage[QDR.LAST_LOCATION] || '/overview';
        $timeout ( function () {
          $location.path(org);
          $location.search('org', null);
          $location.replace();
        });
      },
      onDisconnect: function () {
        self.management.connection.on('connected', self.onReconnect);
        $timeout( function () {
          $location.path('/connect');
          let curPath = $location.path();
          let parts = curPath.split('/');
          let org = parts[parts.length-1];
          if (org && org.length > 0 && org !== 'connect') {
            $location.search('org', org);
          } else {
            $location.search('org', null);
          }
          $location.replace();
        });
      },

      connect: function (connectOptions) {
        return new Promise ( function (resolve, reject) {
          self.management.connection.connect(connectOptions)
            .then( function (r) {
              // if we are ever disconnected, show the connect page and wait for a reconnect
              self.management.connection.on('disconnected', self.onDisconnect);

              self.management.getSchema()
                .then( function () {
                  QDR.log.info('got schema after connection');
                  self.management.topology.setUpdateEntities([]);
                  QDR.log.info('requesting a topology');
                  self.management.topology.get() // gets the list of routers
                    .then( function () {
                      QDR.log.info('got initial topology');
                      let curPath = $location.path();
                      let parts = curPath.split('/');
                      let org = parts[parts.length-1];
                      if (org === '' || org === 'connect') {
                        org = localStorage[QDR.LAST_LOCATION] || QDR.pluginRoot + '/overview';
                      }
                      $timeout ( function () {
                        $location.path(org);
                        $location.search('org', null);
                        $location.replace();
                      });
                    });
                });
              resolve(r);
            }, function (e) {
              reject(e);
            });
        });
      },
      disconnect: function () {
        self.management.connection.disconnect();
        delete self.management;
        self.management = new dm.Management($location.protocol());
      }


    };

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
    return this.substr(position || 0, searchString.length) === searchString;
  };
}

if (!String.prototype.endsWith) {
  String.prototype.endsWith = function(searchString, position) {
    let subjectString = this.toString();
    if (typeof position !== 'number' || !isFinite(position) || Math.floor(position) !== position || position > subjectString.length) {
      position = subjectString.length;
    }
    position -= searchString.length;
    let lastIndex = subjectString.lastIndexOf(searchString, position);
    return lastIndex !== -1 && lastIndex === position;
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

      let o = Object(this);

      // 2. Let len be ? ToLength(? Get(O, "length")).
      let len = o.length >>> 0;

      // 3. If IsCallable(predicate) is false, throw a TypeError exception.
      if (typeof predicate !== 'function') {
        throw new TypeError('predicate must be a function');
      }

      // 4. If thisArg was supplied, let T be thisArg; else let T be undefined.
      let thisArg = arguments[1];

      // 5. Let k be 0.
      let k = 0;

      // 6. Repeat, while k < len
      while (k < len) {
        // a. Let Pk be ! ToString(k).
        // b. Let kValue be ? Get(O, Pk).
        // c. Let testResult be ToBoolean(? Call(predicate, T, « kValue, k, O »)).
        // d. If testResult is true, return k.
        let kValue = o[k];
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

// https://tc39.github.io/ecma262/#sec-array.prototype.find
if (!Array.prototype.find) {
  Object.defineProperty(Array.prototype, 'find', {
    value: function(predicate) {
      // 1. Let O be ? ToObject(this value).
      if (this == null) {
        throw new TypeError('"this" is null or not defined');
      }

      let o = Object(this);

      // 2. Let len be ? ToLength(? Get(O, "length")).
      let len = o.length >>> 0;

      // 3. If IsCallable(predicate) is false, throw a TypeError exception.
      if (typeof predicate !== 'function') {
        throw new TypeError('predicate must be a function');
      }

      // 4. If thisArg was supplied, let T be thisArg; else let T be undefined.
      let thisArg = arguments[1];

      // 5. Let k be 0.
      let k = 0;

      // 6. Repeat, while k < len
      while (k < len) {
        // a. Let Pk be ! ToString(k).
        // b. Let kValue be ? Get(O, Pk).
        // c. Let testResult be ToBoolean(? Call(predicate, T, « kValue, k, O »)).
        // d. If testResult is true, return kValue.
        let kValue = o[k];
        if (predicate.call(thisArg, kValue, k, o)) {
          return kValue;
        }
        // e. Increase k by 1.
        k++;
      }

      // 7. Return undefined.
      return undefined;
    }
  });
}

// https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Array/fill
if (!Array.prototype.fill) {
  Object.defineProperty(Array.prototype, 'fill', {
    value: function(value) {

      // Steps 1-2.
      if (this == null) {
        throw new TypeError('this is null or not defined');
      }

      var O = Object(this);

      // Steps 3-5.
      var len = O.length >>> 0;

      // Steps 6-7.
      var start = arguments[1];
      var relativeStart = start >> 0;

      // Step 8.
      var k = relativeStart < 0 ?
        Math.max(len + relativeStart, 0) :
        Math.min(relativeStart, len);

      // Steps 9-10.
      var end = arguments[2];
      var relativeEnd = end === undefined ?
        len : end >> 0;

      // Step 11.
      var final = relativeEnd < 0 ?
        Math.max(len + relativeEnd, 0) :
        Math.min(relativeEnd, len);

      // Step 12.
      while (k < final) {
        O[k] = value;
        k++;
      }

      // Step 13.
      return O;
    }
  });
}
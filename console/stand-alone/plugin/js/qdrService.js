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
/* global Promise */
import { Management as dm } from './amqp/management.js';
import { utils } from './amqp/utilities.js';

import { QDR_LAST_LOCATION, QDRLogger, QDR_INTERVAL } from './qdrGlobals.js';

// number of milliseconds between topology updates
const DEFAULT_INTERVAL = 5000;
export class QDRService {
  constructor($log, $timeout, $location) {
    this.$timeout = $timeout;
    this.$location = $location;
    this.management = new dm($location.protocol(), localStorage[QDR_INTERVAL] || DEFAULT_INTERVAL);
    this.utilities = utils;
    this.QDRLog = new QDRLogger($log, 'QDRService');
  }

  // Example service function
  onReconnect() {
    this.management.connection.on('disconnected', this.onDisconnect.bind(this));
    let org = localStorage[QDR_LAST_LOCATION] || '/overview';
    this.$timeout(function () {
      this.$location.path(org);
      this.$location.search('org', null);
      this.$location.replace();
    });
  }
  onDisconnect() {
    let self = this;
    this.$timeout(function () {
      self.$location.path('/connect');
      let curPath = self.$location.path();
      let parts = curPath.split('/');
      let org = parts[parts.length - 1];
      if (org && org.length > 0 && org !== 'connect') {
        self.$location.search('org', org);
      } else {
        self.$location.search('org', null);
      }
      self.$location.replace();
    });
    this.management.connection.on('connected', this.onReconnect.bind(this));
  }
  connect(connectOptions) {
    let self = this;
    return new Promise(function (resolve, reject) {
      self.management.connection.connect(connectOptions)
        .then(function (r) {
          // if we are ever disconnected, show the connect page and wait for a reconnect
          self.management.connection.on('disconnected', self.onDisconnect.bind(self));

          self.management.getSchema()
            .then(function () {
              self.QDRLog.info('got schema after connection');
              self.management.topology.setUpdateEntities([]);
              self.QDRLog.info('requesting a topology');
              self.management.topology.get() // gets the list of routers
                .then(function () {
                  self.QDRLog.info('got initial topology');
                  let curPath = self.$location.path();
                  let parts = curPath.split('/');
                  let org = parts[parts.length - 1];
                  if (org === '' || org === 'connect') {
                    org = localStorage[QDR_LAST_LOCATION] || '/overview';
                  }
                  self.$timeout(function () {
                    self.$location.path(org);
                    self.$location.search('org', null);
                    self.$location.replace();
                  });
                });
            });
          resolve(r);
        }, function (e) {
          reject(e);
        });
    });
  }
  disconnect() {
    this.management.connection.disconnect();
    delete this.management;
    this.management = new dm(this.$location.protocol(), localStorage[QDR_INTERVAL] || DEFAULT_INTERVAL);
  }
}

QDRService.$inject = ['$log', '$timeout', '$location'];

(function () {
  console.dump = function (o) {
    if (window.JSON && window.JSON.stringify)
      console.log(JSON.stringify(o, undefined, 2));
    else
      console.log(o);
  };
})();
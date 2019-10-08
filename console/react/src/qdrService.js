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

import { Management as dm } from "./amqp/management.js";
import { utils } from "./amqp/utilities.js";

import { QDR_LAST_LOCATION } from "./qdrGlobals.js";

// number of milliseconds between topology updates
const DEFAULT_INTERVAL = 5000;
export class QDRService {
  constructor(hooks) {
    const url = utils.getUrlParts(window.location);
    this.management = new dm(url.protocol, DEFAULT_INTERVAL);
    this.utilities = utils;
    this.hooks = hooks;
  }

  onReconnect() {
    this.management.connection.on("disconnected", this.onDisconnect.bind(this));
    let org = localStorage[QDR_LAST_LOCATION] || "charts";
    this.hooks.setLocation(org);
  }
  onDisconnect() {
    this.hooks.setLocation("connect");
    this.management.connection.on("connected", this.onReconnect.bind(this));
  }
  connect(connectOptions) {
    let self = this;
    return new Promise((resolve, reject) => {
      self.management.connection.connect(connectOptions).then(
        r => {
          // if we are ever disconnected, show the connect page and wait for a reconnect
          self.management.connection.on(
            "disconnected",
            self.onDisconnect.bind(self)
          );

          self.management.getSchema().then(schema => {
            //console.log("got schema after connection");
            //console.log(schema);
            self.management.topology.setUpdateEntities([]);
            //console.log("requesting a topology");
            self.management.topology
              .get() // gets the list of routers
              .then(t => {
                resolve(r);
              });
          });
        },
        e => {
          reject(e);
        }
      );
    });
  }
  disconnect() {
    this.management.connection.disconnect();
    delete this.management;
    this.management = new dm(this.$location.protocol(), DEFAULT_INTERVAL);
  }
}

(function() {
  console.dump = function(o) {
    if (window.JSON && window.JSON.stringify)
      console.log(JSON.stringify(o, undefined, 2));
    else console.log(o);
  };
})();
